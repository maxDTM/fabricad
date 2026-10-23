# Testing FabriCAD

The everyday commands are in the README ("Building"): configure, build with `-j2`, then
`ctest --test-dir build --output-on-failure`. Every `src/<module>/tests/test_*.cpp` is one ctest
entry; `tests/e2e`, `tests/scripts` and `tests/chair` drive the whole product headlessly
(`QT_QPA_PLATFORM=offscreen`). Labels: `timing` (wall-clock checks, skip on a busy machine with
`ctest -LE timing`), `gui` (real-input runs, opt-in with `FABRICAD_GUI=1`), `fuzz` (below).

## Sanitizers (ASan + UBSan)

```sh
CC=clang CXX=clang++ cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Dev -DFABRICAD_SANITIZE=ON
cmake --build build-san -j2          # about 30 min on the 8-thread / 7 GB box
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-san -LE "gui|timing" -j2 --output-on-failure
```

gcc works too (`scripts/asan-loop.sh` loops the GUI tool tests). Leak checking is off: Qt and
OCCT keep process-lifetime singletons. Under ASan the `timing` entries fail by design (everything
is 2-4x slower). The first full run (2026-10-23) found four heap-use-after-free bugs, all in tests
that kept a pointer across an undo/redo or across a `std::vector` that grew
(`test_assembly_features`, `test_assembly_tools`, `test_drawing_callout_tools`,
`test_drawing_model`); the product code was clean.

## Fuzzing (`-DFABRICAD_FUZZ=ON`)

`tests/fuzz/` has four drivers for the entry points that read untrusted files:

| target | input | what it runs |
|---|---|---|
| `fuzz_fcad_load` | `.fcad` | `Document::load` (JSON, every feature type's `fromJson`, the recompute), then a save/load round trip |
| `fuzz_fcs_script` | `.fcs` | `ScriptRunner` + `HeadlessHost`: comment stripping, `splitCommandLine`, aliases, every tool's typed input, selectors, `CHECK` |
| `fuzz_step_import` | `.step` | `ImportFeature::readFile` and `readStepAssembly` (XCAF) |
| `fuzz_stl_import` | `.stl` | `ImportFeature::readFile` |

With clang the drivers use libFuzzer (coverage instrumentation in every module); with gcc
`DriverMain.cpp` replays the corpus and runs a random mutation loop (same flags: `-runs=`,
`-max_total_time=`, `-seed=`). Seeds live in `tests/fuzz/corpus/<kind>/`, including the inputs
of every bug found so far (`regress-*`). Each driver also has a ctest entry `<target>_smoke`
(label `fuzz`: the seed corpus plus 500 mutations or 60 s).

```sh
CC=clang CXX=clang++ cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Dev -DFABRICAD_SANITIZE=ON -DFABRICAD_FUZZ=ON
cmake --build build-san -j2 --target fuzz_fcad_load fuzz_fcs_script fuzz_step_import fuzz_stl_import
mkdir -p /tmp/fz/work && cd /tmp/fz
ASAN_OPTIONS=detect_leaks=0 $BUILD/fuzz/fuzz_step_import -fork=1 -ignore_crashes=1 -max_total_time=600 \
    -timeout=30 -rss_limit_mb=2000 work $SRC/tests/fuzz/corpus/step
```

Use absolute or cwd-relative corpus paths: the first directory receives new inputs, crashes go to
`./crash-*` (or `-artifact_prefix=`). Every run is private: a temporary directory holds HOME and
the XDG dirs, QStandardPaths test mode is on, nothing reads or writes the user's settings. The
script driver drops lines with absolute paths, `..`, `~`, `$` and `WAIT`/`SCREENSHOT`, so a
generated `SAVE`/`EXPORT` can only write inside that directory. Keep the `.fcad` campaign on the
small seeds (`box*.fcad`, `regress-*`): the 130 KB drawing file brings it down to one run per second.

Toolchain note: Arch's `libclang_rt.fuzzer` carries a copy of libc++abi whose global
`__dynamic_cast` replaced libstdc++'s for the whole process, so `Handle::DownCast` across the
OCCT shared libraries returned null (every STL import crashed in `Geom_TrimmedCurve::SetTrim`)
and ASan reported a bogus alloc-dealloc-mismatch. `tests/fuzz/CMakeLists.txt` links a copy of the
runtime with those symbols made local (nm + objcopy at configure time); keep that in mind before
trusting a libFuzzer crash inside OCCT.

### Findings of the first campaigns (2026-10-23, about 10-20 min per driver)

* **STEP import crashed the app** (SIGSEGV, reproduced with the release build): an entity cut
  short or a dangling `DIRECTION` reference makes OCCT's `StepToGeom::MakeDirection` /
  `MakeVectorWithMagnitude` dereference a null handle. While a STEP/STL reader runs
  (`ScopedOcctSignals` in `src/part/features/Import.cpp`), a fault on a thread that is inside an
  OCCT try block goes to OCCT's handler, so `OCC_CATCH_SIGNALS` turns it into a
  `Standard_Failure` and OCCT's own guarded blocks skip the broken entity; any other fault goes to
  the previous handler (default, Qt, ASan) as before. Handlers use `SA_NODEFER` (OCCT leaves by
  longjmp, which would leave SIGSEGV blocked), non-fault signals keep their handlers, and
  everything is restored afterwards. `FABRICAD_OCCT_SIGNALS=0` turns the guard off for debugging.
* **STL import hung** on NaN / infinite / huge vertex coordinates (OCCT loops in
  `Geom_TrimmedCurve::SetTrim`). The mesh is now read with `RWStl` and checked before faces are
  built; a bad file is "could not read STL file: invalid vertex coordinates".
* **Opening a damaged `.fcad` took 21 s**: a constraint retargeted onto its own line made the
  solver report success with NaN coordinates, which OCCT's profile splitter crawled through. A
  solution containing NaN is now rejected (old geometry kept), edges are never built from
  non-finite or sub-1e-6 mm data, and sketches with absurd extents give no profiles.
* **Undefined behaviour on hostile JSON** (fixed before it could bite): `double` to integer casts
  of ids, ints and enum indices (`1e300`, `-1`, `2.5`), enum indices outside the option list,
  body colours outside 0..1, sketch ids near `INT_MAX` (`id + 1` overflow).
* The `.fcs` runner survived ~4 000 generated scripts without a finding.
* **Open:** replaying the accumulated STEP work corpus as a directory (`-runs=0 work seeds`) can
  hang in OCCT's `StepData_StepReaderData::SetEntityNumbers` on an input that parses in 0.3 s on
  its own, in a file-by-file replay of the same sequence and in the release app (also after the
  preceding damaged files). No signal was converted in that run, so it looks like OCCT parser
  state left behind by earlier damaged inputs; `FABRICAD_FUZZ_TRACE=1` prints the input order and
  the import results for chasing it. Campaigns use `-ignore_timeouts=1` meanwhile.

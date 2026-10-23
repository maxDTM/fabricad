# Fuzz drivers (FABRICAD_FUZZ=ON, docs/TESTING.md "Fuzzing"). OWNER: integrator.
#   fuzz_fcad_load    .fcad loader (Document::load + round trip)
#   fuzz_fcs_script   .fcs scripts through ScriptRunner + HeadlessHost
#   fuzz_step_import  STEP import (ImportFeature::readFile / readStepAssembly)
#   fuzz_stl_import   STL import (ImportFeature::readFile)
# With clang the drivers link libFuzzer; otherwise DriverMain.cpp (corpus replay + random
# mutations). Each driver also gets a short ctest entry (label `fuzz`) that replays the seed
# corpus in corpus/<kind>/ plus a bounded number of mutations.
set(_fz_common "${CMAKE_CURRENT_SOURCE_DIR}/FuzzCommon.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/FuzzCommon.h")
if(NOT FABRICAD_LIBFUZZER)
  list(APPEND _fz_common "${CMAKE_CURRENT_SOURCE_DIR}/DriverMain.cpp")
endif()

# Some distributions (Arch, clang 22) ship libclang_rt.fuzzer with a copy of libc++abi inside:
# its global __dynamic_cast, operator new/delete and fundamental typeinfo then interpose the
# libstdc++ ones for the whole process, and dynamic_cast across shared libraries (OCCT's
# Handle::DownCast) returns null: OCCT crashed in Geom_TrimmedCurve::SetTrim on every STL file,
# and ASan reported a bogus alloc-dealloc-mismatch. Link a copy of the runtime with everything
# outside libFuzzer's own namespaces made local (nm + objcopy at configure time).
set(_fz_runtime_link -fsanitize=fuzzer)
if(FABRICAD_LIBFUZZER)
  execute_process(COMMAND ${CMAKE_CXX_COMPILER} --print-runtime-dir OUTPUT_VARIABLE _fz_rtdir OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(_fz_rt "${_fz_rtdir}/libclang_rt.fuzzer-${CMAKE_SYSTEM_PROCESSOR}.a")
  if(NOT EXISTS "${_fz_rt}")
    set(_fz_rt "${_fz_rtdir}/libclang_rt.fuzzer.a")
  endif()
  find_program(_fz_nm NAMES llvm-nm nm)
  find_program(_fz_objcopy NAMES llvm-objcopy objcopy)
  if(EXISTS "${_fz_rt}" AND _fz_nm AND _fz_objcopy)
    execute_process(COMMAND ${_fz_nm} --defined-only -g "${_fz_rt}" OUTPUT_VARIABLE _fz_syms ERROR_QUIET)
    string(REPLACE "\n" ";" _fz_lines "${_fz_syms}")
    set(_fz_local "")
    foreach(_l IN LISTS _fz_lines)
      if(_l MATCHES "^[0-9a-fA-F]+ [A-Za-z] ([^ ]+)$")
        set(_sym "${CMAKE_MATCH_1}")
        if(NOT _sym MATCHES "^(_ZN6fuzzer|_ZNK6fuzzer|_ZTVN6fuzzer|_ZTIN6fuzzer|_ZTSN6fuzzer|_ZN11__sanitizer|_ZNK11__sanitizer|__sanitizer|LLVMFuzzer|main$|__sancov|__start_|__stop_)")
          string(APPEND _fz_local "${_sym}\n")
        endif()
      endif()
    endforeach()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fuzzer-localize.txt" "${_fz_local}")
    set(_fz_rt_fixed "${CMAKE_CURRENT_BINARY_DIR}/libfabricad_fuzzer_rt.a")
    execute_process(COMMAND ${_fz_objcopy} "--localize-symbols=${CMAKE_CURRENT_BINARY_DIR}/fuzzer-localize.txt"
                            "${_fz_rt}" "${_fz_rt_fixed}" RESULT_VARIABLE _fz_rc)
    if(_fz_rc EQUAL 0)
      set(_fz_runtime_link "${_fz_rt_fixed}")
      message(STATUS "FabriCAD: libFuzzer runtime with private C++ ABI symbols: ${_fz_rt_fixed}")
    endif()
  endif()
endif()

function(fabricad_add_fuzzer name source corpus)
  add_executable(${name} ${source} ${_fz_common})
  target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${name} PRIVATE ${ARGN})
  if(FABRICAD_LIBFUZZER)
    if(_fz_runtime_link STREQUAL "-fsanitize=fuzzer")
      target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    else()
      target_link_libraries(${name} PRIVATE "${_fz_runtime_link}")
    endif()
  endif()
  set_target_properties(${name} PROPERTIES AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fuzz")
  if(FABRICAD_BUILD_TESTS)
    add_test(NAME ${name}_smoke
      COMMAND ${name} -runs=200 -seed=1 -timeout=60 "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${corpus}")
    set_tests_properties(${name}_smoke PROPERTIES LABELS fuzz TIMEOUT 600
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ASAN_OPTIONS=detect_leaks=0;UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")
  endif()
endfunction()

set(_fz_libs fabricad_sketch fabricad_part fabricad_assembly fabricad_drawing fabricad_core Qt6::Widgets)
fabricad_add_fuzzer(fuzz_fcad_load fuzz_fcad_load.cpp fcad ${_fz_libs})
fabricad_add_fuzzer(fuzz_step_import fuzz_import.cpp step ${_fz_libs})
target_compile_definitions(fuzz_step_import PRIVATE FUZZ_IMPORT_EXT=".step")
fabricad_add_fuzzer(fuzz_stl_import fuzz_import.cpp stl ${_fz_libs})
target_compile_definitions(fuzz_stl_import PRIVATE FUZZ_IMPORT_EXT=".stl")
fabricad_add_fuzzer(fuzz_fcs_script fuzz_fcs_script.cpp fcs fabricad_app_lib)

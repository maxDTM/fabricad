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

function(fabricad_add_fuzzer name source corpus)
  add_executable(${name} ${source} ${_fz_common})
  target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${name} PRIVATE ${ARGN})
  if(FABRICAD_LIBFUZZER)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
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

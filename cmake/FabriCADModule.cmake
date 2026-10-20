# FabriCAD CMake helpers. OWNER: architect / integrator.
#
# fabricad_add_module(<target>)
#   Creates a STATIC library from every *.cpp/*.h/*.ui/*.qrc under the calling
#   directory (recursively), excluding the tests/ sub-directory. Module agents add
#   files freely; re-running cmake picks them up (CONFIGURE_DEPENDS).
#   Public include root is ${CMAKE_SOURCE_DIR}/src, so includes look like
#   #include "core/Document.h" / #include "sketch/SketchFeature.h".
#
# fabricad_add_tests(<module-target> [EXTRA_LIBS ...])
#   Every tests/test_*.cpp becomes one executable + one ctest entry named
#   <module>_<file>. Each test is compiled together with tests/support/*.cpp (shared
#   helpers). Tests run with QT_QPA_PLATFORM=offscreen and must not need OpenGL.
#   Test mains: use QTEST_GUILESS_MAIN (core-only) or QTEST_MAIN (needs QWidget).

function(fabricad_add_module target)
  file(GLOB_RECURSE _fc_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc")
  list(FILTER _fc_sources EXCLUDE REGEX "/tests/")
  add_library(${target} STATIC ${_fc_sources})
  target_include_directories(${target} PUBLIC "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(${target} PUBLIC fabricad_warnings)
  set_target_properties(${target} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
endfunction()

function(fabricad_add_tests module_target)
  if(NOT FABRICAD_BUILD_TESTS)
    return()
  endif()
  cmake_parse_arguments(_fc "" "" "EXTRA_LIBS" ${ARGN})
  file(GLOB _fc_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp")
  file(GLOB _fc_support CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/*.h")
  foreach(_fc_test IN LISTS _fc_tests)
    get_filename_component(_fc_name "${_fc_test}" NAME_WE)
    set(_fc_exe "${module_target}_${_fc_name}")
    add_executable(${_fc_exe} "${_fc_test}" ${_fc_support})
    target_link_libraries(${_fc_exe} PRIVATE ${module_target} Qt6::Test ${_fc_EXTRA_LIBS})
    target_include_directories(${_fc_exe} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    set_target_properties(${_fc_exe} PROPERTIES AUTOMOC ON
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_test(NAME ${_fc_exe} COMMAND ${_fc_exe})
    set_tests_properties(${_fc_exe} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FABRICAD_TEST_DATA=${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
      TIMEOUT 300)
  endforeach()
endfunction()

# fabricad (application executable). OWNER: ui agent. Sources are GLOBbed.
# All app sources except main.cpp go into the static library fabricad_app_lib, which the
# executable and every app test link (so tests do not recompile the shell).
# Resources: icons/qss under ${CMAKE_SOURCE_DIR}/resources, listed in resources/*.qrc.
file(GLOB_RECURSE FABRICAD_APP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.ui")
list(FILTER FABRICAD_APP_SOURCES EXCLUDE REGEX "/tests/")
set(_app_lib_sources ${FABRICAD_APP_SOURCES})
list(FILTER _app_lib_sources EXCLUDE REGEX "/main\\.cpp$")
file(GLOB FABRICAD_APP_RESOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/*.qrc")

find_package(Qt6 REQUIRED COMPONENTS Network)  # QLocalServer: the tutorial TestProbe (FABRICAD_TEST_PROBE)
add_library(fabricad_app_lib STATIC ${_app_lib_sources})
target_include_directories(fabricad_app_lib PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fabricad_app_lib PUBLIC
  fabricad_core fabricad_sketch fabricad_part fabricad_assembly fabricad_drawing fabricad_viewer fabricad_theme
  Qt6::Widgets Qt6::Svg Qt6::SvgWidgets Qt6::Network fabricad_warnings)
set_target_properties(fabricad_app_lib PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

# Resources are compiled into the executables (not the static lib), so no
# Q_INIT_RESOURCE is needed.
add_executable(fabricad "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp" ${FABRICAD_APP_RESOURCES})
target_link_libraries(fabricad PRIVATE fabricad_app_lib)
set_target_properties(fabricad PROPERTIES
  AUTOMOC ON AUTOUIC ON AUTORCC ON
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# App-level tests (headless: construct MainWindow with QT_QPA_PLATFORM=offscreen).
if(FABRICAD_BUILD_TESTS)
  file(GLOB _app_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp")
  foreach(_t IN LISTS _app_tests)
    get_filename_component(_n "${_t}" NAME_WE)
    add_executable(fabricad_app_${_n} "${_t}" ${FABRICAD_APP_RESOURCES})
    target_include_directories(fabricad_app_${_n} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(fabricad_app_${_n} PRIVATE fabricad_app_lib Qt6::Test)
    set_target_properties(fabricad_app_${_n} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_test(NAME fabricad_app_${_n} COMMAND fabricad_app_${_n})
    set_tests_properties(fabricad_app_${_n} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)
  endforeach()
  # tutorial lint (docs/plans/tutorial.md 5.9): every tour file loads and validates and every anchor
  # resolves on the current UI in the three layout presets (offscreen)
  if(TARGET fabricad_app_test_tutorial)
    set_tests_properties(fabricad_app_test_tutorial PROPERTIES LABELS tutorial)
  endif()
  # real-input GUI run of a tour (opt-in: FABRICAD_GUI=1, otherwise skipped with code 77; decision 2)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_EXECUTABLE)
    add_test(NAME fabricad_tutorial_gui_sample
      COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/uitest/tutorial_run.py"
        --bin $<TARGET_FILE:fabricad> --tour "${CMAKE_SOURCE_DIR}/resources/tutorials/sample/tutorial.json"
        --out "${CMAKE_BINARY_DIR}/tutorial-out")
    set_tests_properties(fabricad_tutorial_gui_sample PROPERTIES LABELS "gui;tutorial" RUN_SERIAL TRUE
      SKIP_RETURN_CODE 77 TIMEOUT 900)
    # the bracket tour end to end (docs/tutorials/BRACKET_SPEC.md): every chapter, then the exported
    # STL must be watertight, one shell, volume within 0.3 % and box within 0.05 mm of the spec
    foreach(_layout IN ITEMS default alt light)
      add_test(NAME fabricad_tutorial_bracket_${_layout}
        COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/uitest/tutorial_run.py"
          --bin $<TARGET_FILE:fabricad> --tour "${CMAKE_SOURCE_DIR}/resources/tutorials/bracket/tutorial.json"
          --layout ${_layout} --out "${CMAKE_BINARY_DIR}/tutorial-out"
          --expect-stl bracket.stl --stl-volume 66901.359 --stl-box=-40,40,-25,25,0,50)
      set_tests_properties(fabricad_tutorial_bracket_${_layout} PROPERTIES LABELS "gui;tutorial" RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77 TIMEOUT 1800)
    endforeach()
    # off-script scenario on the bracket tour (docs/plans/tutorial.md 5.9): Show me changes nothing,
    # a detour gets the hint and Undo to step start, Back undoes a finished step, Restore chapter
    add_test(NAME fabricad_tutorial_offscript
      COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/uitest/tutorial_run.py"
        --bin $<TARGET_FILE:fabricad> --tour "${CMAKE_SOURCE_DIR}/resources/tutorials/bracket/tutorial.json"
        --scenario offscript --out "${CMAKE_BINARY_DIR}/tutorial-out")
    set_tests_properties(fabricad_tutorial_offscript PROPERTIES LABELS "gui;tutorial" RUN_SERIAL TRUE
      SKIP_RETURN_CODE 77 TIMEOUT 600)
  endif()
  # timing checks run as their own entries with the label `timing` (`ctest -LE timing` skips them
  # on a busy machine); the main entries skip them through FABRICAD_SKIP_TIMING
  if(TARGET fabricad_app_test_theme_manager)
    set_tests_properties(fabricad_app_test_theme_manager PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FABRICAD_SKIP_TIMING=1")
    add_test(NAME fabricad_app_test_theme_manager_timing COMMAND fabricad_app_test_theme_manager themeSwitchIsFast)
    set_tests_properties(fabricad_app_test_theme_manager_timing PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS timing)
  endif()
endif()

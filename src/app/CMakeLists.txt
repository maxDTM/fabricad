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

add_library(fabricad_app_lib STATIC ${_app_lib_sources})
target_include_directories(fabricad_app_lib PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fabricad_app_lib PUBLIC
  fabricad_core fabricad_sketch fabricad_part fabricad_assembly fabricad_drawing fabricad_viewer fabricad_theme
  Qt6::Widgets Qt6::Svg Qt6::SvgWidgets fabricad_warnings)
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
endif()

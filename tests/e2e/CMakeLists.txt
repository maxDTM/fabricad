# End-to-end headless test: drives every module through the ToolManager / tool ids the UI
# uses (sketch -> part -> assembly -> drawing -> export). OWNER: integrator.
add_executable(fabricad_e2e test_e2e.cpp)
target_include_directories(fabricad_e2e PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fabricad_e2e PRIVATE
  fabricad_sketch fabricad_part fabricad_assembly fabricad_drawing fabricad_core Qt6::Widgets Qt6::Test)
set_target_properties(fabricad_e2e PROPERTIES AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_test(NAME fabricad_e2e COMMAND fabricad_e2e)
set_tests_properties(fabricad_e2e PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FABRICAD_E2E_OUT=${CMAKE_BINARY_DIR}/e2e-out"
  TIMEOUT 600)

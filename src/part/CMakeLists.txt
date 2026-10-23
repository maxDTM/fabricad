# fabricad_part. OWNER: part agent. Sources are GLOBbed; add files freely.
fabricad_add_module(fabricad_part)
target_link_libraries(fabricad_part PUBLIC fabricad_sketch fabricad_core fabricad_theme Qt6::Widgets)
# the interop plugin (src/interop) is found in the build tree by tests and uninstalled runs
target_compile_definitions(fabricad_part PRIVATE FABRICAD_INTEROP_BUILD_DIR="${CMAKE_BINARY_DIR}/lib")
add_dependencies(fabricad_part fabricad_interop)
# test_export_formats reopens OBJ / glTF / IGES with the OCCT readers
fabricad_add_tests(fabricad_part EXTRA_LIBS TKDEIGES TKRWMesh TKDEOBJ TKDEGLTF)

# fabricad_part. OWNER: part agent. Sources are GLOBbed; add files freely.
fabricad_add_module(fabricad_part)
target_link_libraries(fabricad_part PUBLIC fabricad_sketch fabricad_core fabricad_theme Qt6::Widgets)
fabricad_add_tests(fabricad_part)

# fabricad_assembly. OWNER: assembly agent. Sources are GLOBbed; add files freely.
fabricad_add_module(fabricad_assembly)
target_link_libraries(fabricad_assembly PUBLIC fabricad_part fabricad_core Qt6::Widgets Eigen3::Eigen)
fabricad_add_tests(fabricad_assembly)

# fabricad_core. OWNER: architect / integrator (module agents: do not edit).
fabricad_add_module(fabricad_core)
target_link_libraries(fabricad_core PUBLIC
  Qt6::Core Qt6::Gui
  fabricad_occt
  Eigen3::Eigen)
# QWidget appears only as a forward declaration in core headers; implementers link Widgets.
fabricad_add_tests(fabricad_core)

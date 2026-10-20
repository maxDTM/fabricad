# fabricad_viewer. OWNER: ui agent. Sources are GLOBbed; add files freely.
# OCCT AIS/V3d in a QOpenGLWidget (copy gkv311's MIT sample, see research). The viewer
# must stay a thin layer: nothing in core/sketch/part/assembly/drawing may link it.
fabricad_add_module(fabricad_viewer)
target_link_libraries(fabricad_viewer PUBLIC
  fabricad_core
  Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
  fabricad_occt_vis)
fabricad_add_tests(fabricad_viewer)

# fabricad_drawing. OWNER: drawing agent. Sources are GLOBbed; add files freely.
# QGraphicsView canvas (Widgets), SVG + PDF export (Svg, PrintSupport), HLR in a
# worker (Concurrent). No OpenGL: everything here must be testable offscreen.
fabricad_add_module(fabricad_drawing)
target_link_libraries(fabricad_drawing PUBLIC
  fabricad_assembly fabricad_part fabricad_core fabricad_theme
  Qt6::Widgets Qt6::Svg Qt6::PrintSupport Qt6::Concurrent)
fabricad_add_tests(fabricad_drawing)

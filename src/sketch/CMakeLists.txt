# fabricad_sketch. OWNER: sketch agent. Sources are GLOBbed; add files freely.
#
# third_party/planegcs/ holds FreeCAD's PlaneGCS constraint solver (LGPL-2.1-or-later,
# see third_party/planegcs/README.md). It is compiled into this target with its own
# compile options (warnings off, Eigen asserts off, <cassert> force-included) and the
# small compatibility shims in third_party/planegcs/compat/ replacing FreeCAD/boost headers.
fabricad_add_module(fabricad_sketch)
target_link_libraries(fabricad_sketch PUBLIC fabricad_core Qt6::Widgets Eigen3::Eigen Threads::Threads)

# PlaneGCS headers (GCS.h ...) are used only inside the solver wrapper (SketchSolver.cpp).
target_include_directories(fabricad_sketch PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/planegcs"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/planegcs/compat")

file(GLOB _fc_planegcs_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/planegcs/*.cpp")
set_source_files_properties(${_fc_planegcs_sources} PROPERTIES
  COMPILE_OPTIONS "-w;-include;cassert;-DEIGEN_NO_DEBUG"
  SKIP_AUTOMOC ON)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/SketchSolver.cpp" PROPERTIES
  COMPILE_OPTIONS "-DEIGEN_NO_DEBUG")

fabricad_add_tests(fabricad_sketch)

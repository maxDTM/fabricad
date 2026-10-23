# fabricad_interop: the OCCT IGES / OBJ / glTF translators (TKDEIGES, TKRWMesh, TKDEOBJ, TKDEGLTF)
# behind a small plugin that src/part/io loads with QLibrary on the first IGES / OBJ / glTF use,
# so the application does not load (or initialise) those toolkits at startup (P3: no startup cost).
# OWNER: P3 interop. Interface: InteropPlugin.h (extern "C", same compiler and OCCT as the app).
add_library(fabricad_interop MODULE InteropPlugin.cpp InteropPlugin.h)
target_include_directories(fabricad_interop PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fabricad_interop PRIVATE fabricad_occt TKDEIGES TKRWMesh TKDEOBJ TKDEGLTF)
set_target_properties(fabricad_interop PROPERTIES
  PREFIX "lib" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CXX_VISIBILITY_PRESET default)

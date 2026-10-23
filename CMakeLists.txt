cmake_minimum_required(VERSION 3.25)
project(FabriCAD VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# FabriCAD top-level build. OWNER: architect / integrator.
# Module agents must NOT edit this file. Each module directory under src/ has
# its own CMakeLists.txt that GLOBs its sources, so adding files needs no edit
# here. Request changes via docs/integration-notes/<module>.md.
#
# Build (always -j2 on the dev box, RAM is tight):
#   cmake -S . -B build-arch -DCMAKE_BUILD_TYPE=Dev
#   cmake --build build-arch -j2
#   ctest --test-dir build-arch --output-on-failure
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# "Dev" configuration: light optimisation, minimal debug info, low RAM use.
set(CMAKE_CXX_FLAGS_DEV "-O1 -g1" CACHE STRING "Flags for Dev configuration")
set(CMAKE_EXE_LINKER_FLAGS_DEV "" CACHE STRING "")
set(CMAKE_SHARED_LINKER_FLAGS_DEV "" CACHE STRING "")
set(CMAKE_STATIC_LINKER_FLAGS_DEV "" CACHE STRING "")
mark_as_advanced(CMAKE_CXX_FLAGS_DEV CMAKE_EXE_LINKER_FLAGS_DEV CMAKE_SHARED_LINKER_FLAGS_DEV CMAKE_STATIC_LINKER_FLAGS_DEV)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Dev CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Dev Debug Release RelWithDebInfo)
endif()

option(FABRICAD_BUILD_TESTS "Build unit tests" ON)
option(FABRICAD_USE_LLD "Link with lld when available (faster, less RAM)" ON)
option(FABRICAD_WERROR "Treat warnings as errors" OFF)
option(FABRICAD_SANITIZE "Build with AddressSanitizer + UBSan (CI crash hunting; see scripts/asan-loop.sh)" OFF)
option(FABRICAD_FUZZ "Build the fuzz drivers in tests/fuzz (libFuzzer with clang, else a mutation driver; docs/TESTING.md)" OFF)

# ---------------------------------------------------------------------------
# Dependencies (all system packages; nothing is downloaded or built here)
# ---------------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Svg SvgWidgets PrintSupport Concurrent Test)
find_package(OpenCASCADE REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Threads REQUIRED)
find_package(TBB QUIET)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---------------------------------------------------------------------------
# Shared interface targets
# ---------------------------------------------------------------------------
add_library(fabricad_warnings INTERFACE)
target_compile_options(fabricad_warnings INTERFACE
  -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-deprecated-declarations)
if(FABRICAD_WERROR)
  target_compile_options(fabricad_warnings INTERFACE -Werror)
endif()
if(FABRICAD_SANITIZE)
  # Catches use-after-free such as the PropertyPanel rebuild crash (deleting a QComboBox from
  # inside its own currentIndexChanged). Run the tool tests in a loop: scripts/asan-loop.sh.
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g)
  add_link_options(-fsanitize=address,undefined)
  message(STATUS "FabriCAD: AddressSanitizer + UBSan enabled")
endif()
if(FABRICAD_FUZZ AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # coverage instrumentation for libFuzzer in every module; only the fuzz drivers link the runtime
  add_compile_options(-fsanitize=fuzzer-no-link)
  set(FABRICAD_LIBFUZZER ON)
  message(STATUS "FabriCAD: fuzz drivers use libFuzzer")
elseif(FABRICAD_FUZZ)
  set(FABRICAD_LIBFUZZER OFF)
  message(STATUS "FabriCAD: fuzz drivers use the built-in mutation driver (no clang)")
endif()

# Linker: lld if available (requested by option), otherwise system default.
if(FABRICAD_USE_LLD)
  find_program(FABRICAD_LLD ld.lld)
  if(FABRICAD_LLD)
    add_link_options(-fuse-ld=lld)
    message(STATUS "FabriCAD: linking with lld")
  endif()
endif()

# OCCT modelling kernel (no visualisation). Linked PUBLIC by fabricad_core.
add_library(fabricad_occt INTERFACE)
target_link_libraries(fabricad_occt INTERFACE
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim
  TKBO TKBool TKShHealing TKFillet TKOffset TKFeat TKMesh TKHLR
  TKXSBase TKDE TKDESTEP TKDESTL TKCDF TKLCAF TKCAF TKVCAF TKXCAF
  TKDEIGES TKRWMesh TKDEOBJ TKDEGLTF  # P3 interop: IGES, OBJ / glTF writers (OCCT toolkits, already installed)
  Threads::Threads)
target_include_directories(fabricad_occt INTERFACE ${OpenCASCADE_INCLUDE_DIR})
target_compile_definitions(fabricad_occt INTERFACE HAVE_OCCT=1)

# OCCT visualisation (AIS/V3d/OpenGl). Linked only by fabricad_viewer.
add_library(fabricad_occt_vis INTERFACE)
target_link_libraries(fabricad_occt_vis INTERFACE fabricad_occt TKService TKV3d TKOpenGl)

if(TBB_FOUND)
  target_link_libraries(fabricad_occt INTERFACE TBB::tbb)
endif()

include(FabriCADModule)

if(FABRICAD_BUILD_TESTS)
  enable_testing()
endif()

# ---------------------------------------------------------------------------
# Modules. Dependency direction:
#   core <- sketch <- part <- assembly ; drawing <- (core, part, assembly) ;
#   theme (QtGui only) ; viewer <- core ; app <- everything.
# ---------------------------------------------------------------------------
add_subdirectory(src/core)       # owner: architect/integrator
add_subdirectory(src/sketch)     # owner: sketch agent
add_subdirectory(src/part)       # owner: part agent
add_subdirectory(src/assembly)   # owner: assembly agent
add_subdirectory(src/drawing)    # owner: drawing agent
add_subdirectory(src/theme)      # owner: ui agent (design tokens; QtGui only)
add_subdirectory(src/viewer)     # owner: ui agent
add_subdirectory(src/app)        # owner: ui agent
if(FABRICAD_BUILD_TESTS)
  add_subdirectory(tests/e2e)    # owner: integrator (headless end-to-end test)
  add_subdirectory(tests/scripts) # owner: ui agent (command scripts run by the real executable)
  add_subdirectory(tests/chair)   # chair benchmark regression (docs/comparison/CHAIR_SPEC.md)
  add_subdirectory(tests/tutorial) # headless twin of the guided bracket tour (docs/tutorials/BRACKET_SPEC.md)
endif()
if(FABRICAD_FUZZ)
  add_subdirectory(tests/fuzz)    # fuzz drivers: .fcad loader, .fcs scripts, STEP/STL import (docs/TESTING.md)
endif()

# Install rules: binary, .desktop, MIME type, hicolor icons, licences (cmake/FabriCADInstall.cmake, packaging/)
include(FabriCADInstall)

message(STATUS "FabriCAD ${PROJECT_VERSION}: build type ${CMAKE_BUILD_TYPE}, Qt ${Qt6_VERSION}, OCCT ${OpenCASCADE_VERSION}, Eigen ${Eigen3_VERSION}")

# SPDX-License-Identifier: LGPL-2.1-or-later
# Chair benchmark regression test (docs/comparison/CHAIR_SPEC.md). The real executable runs
# tests/chair/chair.fcs headlessly through the command line (docs/SCRIPTING.md), exactly as a user
# would type it: the script's CHECK lines assert the B-rep volumes (0.05 %) and the final world
# bounding boxes (0.01 mm), also after re-reading the saved .fcad and the exported STEP. Then
# check_stl.py checks that every export exists and that every STL is watertight with its volume
# within 0.5 % of the reference. Output: ${CMAKE_BINARY_DIR}/chair-out/ (out/ and chair.log).
find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
  message(STATUS "tests/chair: no Python 3 interpreter, fabricad_chair_spec not added")
  return()
endif()
add_test(NAME fabricad_chair_spec
         COMMAND ${CMAKE_COMMAND}
                 -DFABRICAD=$<TARGET_FILE:fabricad>
                 -DPYTHON=${Python3_EXECUTABLE}
                 -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/chair.fcs
                 -DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/check_stl.py
                 -DWORKDIR=${CMAKE_BINARY_DIR}/chair-out
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/run_chair.cmake)
set_tests_properties(fabricad_chair_spec PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FABRICAD_NO_RECOVERY=1"
  LABELS "chair;script"
  TIMEOUT 600)

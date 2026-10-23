# SPDX-License-Identifier: LGPL-2.1-or-later
# Headless twin of the guided bracket tour (docs/tutorials/BRACKET_SPEC.md, docs/plans/tutorial.md 1):
# the real executable runs tests/tutorial/bracket.fcs (the same inputs the tour asks for, as typed
# commands) and its CHECK lines assert every reference volume and the final box; then
# tools/uitest/stlcheck.py checks the exported STL (watertight, one shell, volume 0.3 %, box 0.05 mm).
# The GUI run of the tour itself is fabricad_tutorial_gui_bracket (src/app/CMakeLists.txt, opt-in).
find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
  message(STATUS "tests/tutorial: no Python 3 interpreter, fabricad_tutorial_bracket_twin not added")
  return()
endif()
add_test(NAME fabricad_tutorial_bracket_twin
         COMMAND ${CMAKE_COMMAND}
                 -DFABRICAD=$<TARGET_FILE:fabricad>
                 -DPYTHON=${Python3_EXECUTABLE}
                 -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/bracket.fcs
                 -DCHECKER=${CMAKE_SOURCE_DIR}/tools/uitest/stlcheck.py
                 -DWORKDIR=${CMAKE_BINARY_DIR}/tutorial-twin
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/run_twin.cmake)
set_tests_properties(fabricad_tutorial_bracket_twin PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FABRICAD_NO_RECOVERY=1"
  LABELS "tutorial;script"
  TIMEOUT 600)

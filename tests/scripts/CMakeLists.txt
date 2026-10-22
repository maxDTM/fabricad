# Scripted end-to-end tests: the real executable runs a .fcs command script headlessly
# (fabricad --script <file> --offscreen, docs/SCRIPTING.md) exactly as a user would type it in
# the command line, and the script's CHECK lines are the assertions. OWNER: ui agent.
#
# Every tests/scripts/*.fcs is one ctest entry running in its own output directory under
# ${CMAKE_BINARY_DIR}/script-out/<name>/ (exports, saved documents and the per-command log).
# `--script --offscreen` uses the lightweight headless runner (no MainWindow); the *_window
# entries run the same scripts through the (offscreen) main window (FABRICAD_SCRIPT_WINDOW=1),
# so both shells keep giving the same results.
file(GLOB _fabricad_scripts CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.fcs")
foreach(_script IN LISTS _fabricad_scripts)
  get_filename_component(_name "${_script}" NAME_WE)
  set(_out "${CMAKE_BINARY_DIR}/script-out/${_name}")
  file(MAKE_DIRECTORY "${_out}")
  add_test(NAME fabricad_script_${_name}
           COMMAND fabricad --script "${_script}" --offscreen --log "${_out}/${_name}.log"
           WORKING_DIRECTORY "${_out}")
  set_tests_properties(fabricad_script_${_name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FABRICAD_NO_RECOVERY=1"
    TIMEOUT 600)
endforeach()
foreach(_name IN ITEMS bracket chair)
  set(_script "${CMAKE_CURRENT_SOURCE_DIR}/${_name}.fcs")
  set(_out "${CMAKE_BINARY_DIR}/script-out/${_name}_window")
  file(MAKE_DIRECTORY "${_out}")
  add_test(NAME fabricad_script_${_name}_window
           COMMAND fabricad --script "${_script}" --offscreen --log "${_out}/${_name}.log"
           WORKING_DIRECTORY "${_out}")
  set_tests_properties(fabricad_script_${_name}_window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FABRICAD_NO_RECOVERY=1;FABRICAD_SCRIPT_WINDOW=1"
    TIMEOUT 600)
endforeach()

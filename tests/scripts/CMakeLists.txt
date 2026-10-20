# Scripted end-to-end tests: the real executable runs a .fcs command script headlessly
# (fabricad --script <file> --offscreen, docs/SCRIPTING.md) exactly as a user would type it in
# the command line, and the script's CHECK lines are the assertions. OWNER: ui agent.
#
# Every tests/scripts/*.fcs is one ctest entry running in its own output directory under
# ${CMAKE_BINARY_DIR}/script-out/<name>/ (exports, saved documents and the per-command log).
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

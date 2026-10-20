# SPDX-License-Identifier: LGPL-2.1-or-later
# Driver for the fabricad_chair_spec ctest entry (see CMakeLists.txt next to this file).
foreach(v FABRICAD PYTHON SCRIPT CHECKER WORKDIR)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "run_chair.cmake: -D${v}=... missing")
  endif()
endforeach()
# start from an empty output directory so stale exports cannot make the test pass
file(REMOVE_RECURSE "${WORKDIR}/out")
file(MAKE_DIRECTORY "${WORKDIR}/out")
execute_process(COMMAND "${FABRICAD}" --script "${SCRIPT}" --offscreen --log "${WORKDIR}/chair.log"
                WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "chair.fcs failed (exit ${rc}), see ${WORKDIR}/chair.log")
endif()
execute_process(COMMAND "${PYTHON}" -I "${CHECKER}" "${WORKDIR}/out" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "chair exports failed the checks (exit ${rc})")
endif()

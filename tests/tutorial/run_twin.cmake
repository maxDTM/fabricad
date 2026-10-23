# SPDX-License-Identifier: LGPL-2.1-or-later
# Driver for fabricad_tutorial_bracket_twin (see CMakeLists.txt next to this file).
foreach(v FABRICAD PYTHON SCRIPT CHECKER WORKDIR)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "run_twin.cmake: -D${v}=... missing")
  endif()
endforeach()
file(REMOVE_RECURSE "${WORKDIR}")  # stale exports must not make the test pass
file(MAKE_DIRECTORY "${WORKDIR}")
execute_process(COMMAND "${FABRICAD}" --script "${SCRIPT}" --offscreen --log "${WORKDIR}/bracket.log"
                WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "bracket.fcs failed (exit ${rc}), see ${WORKDIR}/bracket.log")
endif()
# docs/tutorials/BRACKET_SPEC.md: final part (t = 10) 66 901.359 mm3, box X[-40,40] Y[-25,25] Z[0,50]
execute_process(COMMAND "${PYTHON}" -I "${CHECKER}" "${WORKDIR}/bracket.stl" --volume 66901.359
                        --box=-40,40,-25,25,0,50 --vol-tol 0.003 --box-tol 0.05 RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "bracket.stl failed the STL checks (exit ${rc})")
endif()

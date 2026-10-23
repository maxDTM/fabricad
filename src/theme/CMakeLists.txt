# fabricad_theme. OWNER: ui agent. Design tokens (generated ThemeTokens.inc), colour math, the status
# glyphs (StatusGlyph: needs QtWidgets for the QLabel helper) and the
# QSS template renderer (docs/plans/ui-redesign.md section 2). Depends on QtGui and QtWidgets, so both
# fabricad_viewer and the app can link it.
fabricad_add_module(fabricad_theme)
target_link_libraries(fabricad_theme PUBLIC Qt6::Gui Qt6::Widgets)

if(FABRICAD_BUILD_TESTS)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  # theme_tokens: generated files in sync with tokens.json (Python), contrast + CVD floors (C++ and
  # Python), both themes render the QSS template without unresolved placeholders.
  add_executable(theme_tokens "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_theme_tokens.cpp")
  target_link_libraries(theme_tokens PRIVATE fabricad_theme Qt6::Test)
  target_compile_definitions(theme_tokens PRIVATE
    FABRICAD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    FABRICAD_PYTHON="$<IF:$<BOOL:${Python3_EXECUTABLE}>,${Python3_EXECUTABLE},>")
  set_target_properties(theme_tokens PROPERTIES AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
  add_test(NAME theme_tokens COMMAND theme_tokens)
  set_tests_properties(theme_tokens PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS theme)

  # theme_color_lint: no new hard-coded colours in src/ and resources/ (tools/theme/audit_colors.py,
  # allow-list tools/theme/color-allowlist.txt). Skipped (code 77) without Python.
  if(Python3_EXECUTABLE)
    add_test(NAME theme_color_lint
      COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/theme/audit_colors.py" --lint --root "${CMAKE_SOURCE_DIR}")
    set_tests_properties(theme_color_lint PROPERTIES TIMEOUT 60 LABELS "theme;lint")
    # uitest_tools: tools/uitest PNG I/O, CVD simulation and mockup compare (stdlib only)
    add_test(NAME uitest_tools COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/uitest/selftest.py")
    set_tests_properties(uitest_tools PROPERTIES TIMEOUT 120 LABELS "theme;tools")
  endif()
endif()

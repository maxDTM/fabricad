# FabriCAD install rules (`cmake --install build [--prefix /usr]`). OWNER: integrator.
#
# Installs:
#   bin/fabricad                                   the executable (icons, theme, fonts are
#                                                  compiled into it through resources/fabricad.qrc)
#   share/applications/io.github.maxdtm.FabriCAD.desktop
#   share/metainfo/io.github.maxdtm.FabriCAD.metainfo.xml
#   share/mime/packages/io.github.maxdtm.FabriCAD.xml   (*.fcad -> application/x-fabricad)
#   share/icons/hicolor/{scalable,16x16..512x512}/apps/io.github.maxdtm.FabriCAD.{svg,png}
#   share/icons/hicolor/.../mimetypes/application-x-fabricad.{svg,png}
#   share/licenses/fabricad/   LICENSE, THIRD_PARTY.md, OFL.txt (bundled JetBrains Mono),
#                              PlaneGCS notice (vendored FreeCAD solver)
#   share/doc/fabricad/        README.md, CHANGELOG.md, SCRIPTING.md
#
# Every icon comes from ONE source SVG, resources/icons/app/fabricad-app.svg (override with
# -DFABRICAD_APP_ICON=...). The PNG sizes are rendered at build time by
# packaging/linux/render-icons.sh (rsvg-convert or ImageMagick); without a renderer only the
# scalable SVG is installed (FABRICAD_INSTALL_PNG_ICONS=OFF forces that).

include(GNUInstallDirs)
# lower-case doc dir (GNUInstallDirs' DOCDIR is share/doc/<PROJECT_NAME> = share/doc/FabriCAD,
# and Qt's config packages may already have cached it before this point)
set(FABRICAD_DOC_DIR "${CMAKE_INSTALL_DATADIR}/doc/fabricad"
  CACHE STRING "Where README / CHANGELOG / SCRIPTING are installed (relative to the prefix)")

set(FABRICAD_APP_ID "io.github.maxdtm.FabriCAD")
set(FABRICAD_APP_ICON "${CMAKE_SOURCE_DIR}/resources/icons/app/fabricad-app.svg"
  CACHE FILEPATH "Source SVG for the application / MIME icons (all sizes are generated from it)")
set(FABRICAD_LICENSE_DIR "${CMAKE_INSTALL_DATADIR}/licenses/fabricad"
  CACHE STRING "Where the licence files are installed (relative to the prefix)")

find_program(FABRICAD_RSVG_CONVERT rsvg-convert)
find_program(FABRICAD_MAGICK NAMES magick convert)
if(FABRICAD_RSVG_CONVERT OR FABRICAD_MAGICK)
  set(_fc_have_renderer ON)
else()
  set(_fc_have_renderer OFF)
endif()
option(FABRICAD_INSTALL_PNG_ICONS "Render and install 16..512 px PNG icons (needs rsvg-convert or ImageMagick)"
  ${_fc_have_renderer})

set(_fc_pkg "${CMAKE_SOURCE_DIR}/packaging/linux")
set(_fc_icon_dir "${CMAKE_BINARY_DIR}/icons/hicolor")
set(_fc_icon_sizes 16 22 24 32 48 64 128 256 512)

if(FABRICAD_INSTALL_PNG_ICONS)
  set(_fc_icon_outputs "")
  foreach(_s IN LISTS _fc_icon_sizes)
    list(APPEND _fc_icon_outputs "${_fc_icon_dir}/${_s}x${_s}/apps/${FABRICAD_APP_ID}.png")
  endforeach()
  add_custom_command(
    OUTPUT ${_fc_icon_outputs} "${_fc_icon_dir}/scalable/apps/${FABRICAD_APP_ID}.svg"
    COMMAND sh "${_fc_pkg}/render-icons.sh" "${FABRICAD_APP_ICON}" "${_fc_icon_dir}" "${FABRICAD_APP_ID}"
    DEPENDS "${FABRICAD_APP_ICON}" "${_fc_pkg}/render-icons.sh"
    COMMENT "Rendering hicolor app icons from ${FABRICAD_APP_ICON}"
    VERBATIM)
  add_custom_target(fabricad_app_icons ALL DEPENDS ${_fc_icon_outputs})
endif()

install(TARGETS fabricad RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(FILES "${_fc_pkg}/${FABRICAD_APP_ID}.desktop" DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES "${_fc_pkg}/${FABRICAD_APP_ID}.metainfo.xml" DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
install(FILES "${_fc_pkg}/${FABRICAD_APP_ID}.xml" DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")

set(_fc_hicolor "${CMAKE_INSTALL_DATADIR}/icons/hicolor")
install(FILES "${FABRICAD_APP_ICON}" DESTINATION "${_fc_hicolor}/scalable/apps" RENAME "${FABRICAD_APP_ID}.svg")
install(FILES "${FABRICAD_APP_ICON}" DESTINATION "${_fc_hicolor}/scalable/mimetypes" RENAME "application-x-fabricad.svg")
if(FABRICAD_INSTALL_PNG_ICONS)
  foreach(_s IN LISTS _fc_icon_sizes)
    set(_png "${_fc_icon_dir}/${_s}x${_s}/apps/${FABRICAD_APP_ID}.png")
    install(FILES "${_png}" DESTINATION "${_fc_hicolor}/${_s}x${_s}/apps")
    install(FILES "${_png}" DESTINATION "${_fc_hicolor}/${_s}x${_s}/mimetypes" RENAME "application-x-fabricad.png")
  endforeach()
endif()

install(FILES
  "${CMAKE_SOURCE_DIR}/LICENSE"
  "${CMAKE_SOURCE_DIR}/THIRD_PARTY.md"
  "${CMAKE_SOURCE_DIR}/resources/fonts/OFL.txt"
  DESTINATION "${FABRICAD_LICENSE_DIR}")
install(FILES "${CMAKE_SOURCE_DIR}/src/sketch/third_party/planegcs/README.md"
  DESTINATION "${FABRICAD_LICENSE_DIR}" RENAME "PlaneGCS-NOTICE.md")
install(FILES
  "${CMAKE_SOURCE_DIR}/README.md"
  "${CMAKE_SOURCE_DIR}/CHANGELOG.md"
  "${CMAKE_SOURCE_DIR}/docs/SCRIPTING.md"
  DESTINATION "${FABRICAD_DOC_DIR}")

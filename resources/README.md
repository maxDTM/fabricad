# resources/ (owner: ui agent)

Icons (SVG, own artwork, 32 px toolbar / 16 px menu), the light theme stylesheet and
any other embedded assets. Add a `fabricad.qrc` here; `src/app/CMakeLists.txt` globs
`resources/*.qrc` automatically. Reference icons from ToolInfo::icon as
`":/icons/<name>.svg"`.

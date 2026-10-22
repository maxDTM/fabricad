# resources/ (owner: ui agent)

Icons (SVG, own artwork, 32 px toolbar / 16 px menu), the design tokens and QSS template
(`theme/`, see `docs/plans/ui-redesign.md` section 2), the bundled JetBrains Mono (`fonts/`, SIL OFL 1.1) and
any other embedded assets. Add a `fabricad.qrc` here; `src/app/CMakeLists.txt` globs
`resources/*.qrc` automatically. Reference icons from ToolInfo::icon as
`":/icons/<name>.svg"`.

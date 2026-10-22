# T-cvd: gate G (ui-redesign 4.7), colour-vision perception test

Verifier: Sonnet, mouse and keyboard only on private display :88 (`tools/uitest/ui.sh start 88`, `build/bin/fabricad`, fixture `tests/e2e/bracket-v3.fcad`, private XDG dirs, `vblank_mode=0`). Nothing on the Hyprland desktop was touched.

## Result

| Measure | Score | Gate |
|---|---|---|
| Gold live vs warning (40 element-views) | 40/40 = 100 % | 100 % required: PASS |
| Overall, strict (colour or shape cue must be visible; wording alone does not count) | 130/144 = 90.3 % | 95 % required: FAIL |
| Overall, lexical (wording such as "failed" or "must not be zero" counts) | 140/144 = 97.2 % | 95 %: pass |

**Gold fallback (teal live): not needed.** Live vs warning never confused: warning always carries a triangle, live a filled square plus handle and value box. All strict misses are error-coloured text, cells and a curve that have no shape cue. Teal would not fix those (P5-CVD-1..3).

## Method
- 24 crops (12 dark, 12 light) from the real app (see `cvd/raw-*.png`, boxes in the table), enlarged 2 to 4x nearest, then `tools/uitest/cvd.py <crop> <dir> all` (normal, protan, deutan, tritan + 2x2 sheet) in `cvd/crops/`. 96 simulated views, 144 marked element-views.
- Crops were given shuffled ids (c01..c24) and I labelled every marked element in all four views from the sheets before opening the key. Not truly blind: I built the scenes myself.
- Scene (status zoo equivalent): Extrude live preview (gold), zero distance error, Sketch5 with an expression dimension `plate_t` and PLATE_T set to 0 (gives error rows via Extrude1 and a Sketch5 warning row "conflicting constraints"), selected line vs free circle, dial and XYZ readout, status bar.
- Limitation: the host tiles the Xwayland window to about 945x490, so the app ran at QT_SCALE_FACTOR 0.5 (logical 1890x980). Thin lines are anti-aliased more than at 1x. Not producible: "warning toast during a live drag" (this build shows warnings only as row icon and tooltip, no toast), so a live fillet error toast was replaced by status-bar texts.

## Crops and labels (N / P / D / T = normal / protan / deutan / tritan)
| id | crop | marked | labels N P D T | truth |
|---|---|---|---|---|
| c01 | light live row | row | live x4 (T row turns pink, square marker decides) | live |
| c09 | light live preview | preview | live x4 (T salmon, handle and tag decide; low confidence) | live |
| c23 | light value box | tag | live x4 (T pink; low confidence) | live |
| c24 | dark live preview | preview | live x4 (T pink) | live |
| c13 | dark value box | tag | live x4 (T pink) | live |
| c14 | dark live row | row | live x4 (T pink-brown row, square) | live |
| c02 | light warning row + tooltip | row | warning x4 (triangle; P/D olive text, T red text) | warning |
| c20 | dark warning row + tooltip | row | warning x4 | warning |
| c16 | dark 4 error rows + warning row | 5 | error x4, warning x4 by octagon/triangle; P/D error rows lose all colour | ok |
| c18 | light 2 error rows + warning row | 3 | same, all correct by shape | ok |
| c04 | dark Parameters red error text | text | N error, **P neutral, D neutral**, T error | error |
| c17 | light card red "DISTANCE MUST NOT BE ZERO" | text | N error, **P/D olive (reads warning/neutral)**, T error | error |
| c19 | light card red "SELECT A PROFILE TO EXTRUDE" | text | N error, **P/D olive**, T error | error |
| c05 | dark status bar: message + SKETCHES: OVER-CONSTRAINED cell | 2 | message error (wording) x4; cell N error, **P/D grey**, T error | error |
| c06 | light status bar, same | 2 | same | error |
| c11 | dark status message "distance must not be zero" | text | error by wording x4 (grey in normal too) | error |
| c07 | dark zero-length conflict line vs free circle | 2 | circle free x4; line N error, **P grey, D beige-grey**, T error | error, free |
| c08 | light, same | 2 | same | error, free |
| c10 | light selected line vs free circle | 2 | selected/free x4 (P/D selection = brighter blue, T = grey-slate; width and lightness differ) | ok |
| c21 | dark selected vs free | 2 | selected/free x4 (T: line grey vs teal circle) | ok |
| c03, c22 | XYZ readout (dark, light) | 3 | X/Y/Z x4 by letters; P/D X and Y swatches identical olive | ok |
| c12, c15 | hex dial (light, dark) | 3 | X/Y/Z x4 by letters; P/D X and Y identical | ok |

## Issues
| id | severity | repro | expected | actual |
|---|---|---|---|---|
| P5-CVD-1 | medium | Dark and light. Extrude, select profile, type 0 (card shows red "DISTANCE MUST NOT BE ZERO"); open File > Change Parameters, try Remove on a used parameter (red text); start Extrude with no profile (red "SELECT A PROFILE"). View under protan/deutan. | Error text has an icon or prefix (octagon), or differs by weight, so it reads as error without hue. | Pure red/rose text. Protan: grey. Deutan: olive (looks like warning or plain). Only the wording carries it. Crops c04, c17, c19. |
| P5-CVD-2 | medium | Sketch with a zero-length or conflicting constraint (set PLATE_T=0 with a `plate_t` dimension). Look at status bar cell "SKETCHES: OVER-CONSTRAINED" and the status message "Extrude1 failed...". | Cell has an icon or inverted fill; the failure message is styled as error. | Cell is red text only: grey under protan, slate (light) or grey (dark) under deutan. The "failed" message is plain grey in every vision type, including normal. Crops c05, c06, c11. |
| P5-CVD-3 | low | Same sketch: the conflicting (zero-length) curve is drawn pink/crimson. | Dash or width cue, since conflict hue is lost. | Protan: grey line (reads as construction), deutan: beige-grey. Free circle stays distinct. Crops c07, c08. |
| P5-CVD-4 | low | Live Extrude, light and dark, tritan view. | Live preview identifiable without hue. | Gold turns salmon pink (preview, value tag, row), the same family as warning/error red. Identification relies on handle, value box and square marker, not on colour. Warning never has a fill so no live/warning clash was observed. Crops c09, c13, c14, c23, c24. |
| P5-CVD-5 | info | Dial and XYZ readout, protan/deutan. | Axes distinguishable. | X and Y swatches and the X/Y dial arms are the same olive; letters X/Y/Z disambiguate. Pass via letters. |
| P5-CVD-6 | info | Process: host tiling gives the private display about 945x490 (466x239 at times) and the window cannot be moved, so the app ran at scale 0.5; "warning toast during live drag" could not be produced. | Exact-size 1920x1080 crops and a warning toast scene. | Re-run at 1x offscreen/1920x1080 grabs for crisp hairlines if gate G needs it; add a warning toast scene if one exists. |

# P5 re-verify: gate G blind re-run (ui-redesign 4.7), PASS

Verifier: Sonnet, mouse and keyboard on private display :93 (stopped afterwards) plus offscreen 1920x1080 grabs, build `build/bin/fabricad` (main 2022d35). Nothing on the real desktop touched.

## Result
| Measure | Score | Gate |
|---|---|---|
| Gold live vs warning (64 element-views: live and warning marks, 4 simulations) | 64/64 = 100 % | 100 %: PASS |
| Overall (176 marked element-views, 32 scored crops x 4 views) | 174/176 = 98.9 % | 95 %: PASS |

Both misses are the same state: the conflict-curve rectangle (thick outline, no icon) under deuteranopia, read as warning (olive, matches the warning row hue). b28 (light) and b32 (dark), view D. Under protanopia I labelled it error with low confidence (thick slate line only); that was right but by guess.

## Method
- Scenes (scripts in the folder: live2.fcs, zoo2.fcs, skz.fcs, zero.fcs, sel.fcs): gold Join-extrude live preview with DIST tag (GUI, 1x, ISO); warning Sketch1 + error Extrude1 rows (plate_t = 0); status message and SKETCH1: OVER-CONSTRAINED cell with octagons; card "DISTANCE MUST NOT BE ZERO"; conflict rectangle in the sketch; selected line vs free circle; dial and XYZ readout. Dark and light. 1x pixels, crops in `crops/`, simulations by `tools/uitest/cvd.py` (normal, protan, deutan, tritan), 2x nearest sheets in `sheets/` (top to bottom N, P, D, T).
- Crops got shuffled ids (b01..b34); I labelled every marked element in every view from the sheets seeing only a mark description (manifest), then ran `score.py` against `key.json`. Labels in `labels.json`.
- Not truly blind: I built the scenes and knew the three state classes. Wording in some crops ("DISTANCE MUST NOT BE ZERO", "Script failed") was visible; I labelled from the icon shape/colour (octagon, circle-x, triangle, filled tag + handle). Axes (dial, readout) were labelled by the X/Y/Z letters: under protan/deutan the X and Y swatches and arms are the same olive. Under tritan the live gold turns salmon pink (preview, tag, chip, row); I labelled it live from the filled tag, handle ring, no triangle/octagon, not from hue.
- b01 and b25 (planned origin-axes crops) were defective (my crop box hit the circle, the origin axes are faint grey in the sketch view) and are excluded; axis colours are covered by dial and readout (b03, b06, b07, b12).
- Window limit: the compositor tiled the private display to 1900x490 (app window 1400x490), so GUI scenes are 1x but short; offscreen grabs give the full chrome; the 3D view is only available in the GUI.
- Warning toast during a live drag: still not producible (no toast in this build).

## Crop key (truth, revealed after labelling)
| id | theme | crop | truth (marks in order) |
|---|---|---|---|
| b01 | dark | origin-axes | X, Y |
| b02 | dark | err-cell | error |
| b03 | light | dial | Y, Z, X |
| b04 | dark | rows-both | warning, error |
| b05 | dark | dist-tag | live |
| b06 | dark | readout | X, Y, Z |
| b07 | dark | dial | Y, Z, X |
| b08 | dark | warn-tree | warning |
| b09 | dark | sel-free | selection, free |
| b10 | light | err-card | error |
| b11 | light | rows-both | warning, error |
| b12 | light | readout | X, Y, Z |
| b13 | light | warn-active | warning |
| b14 | light | warn-row | warning |
| b15 | dark | err-card | error |
| b16 | light | err-row | error |
| b17 | light | dist-tag | live |
| b18 | dark | preview | live |
| b19 | dark | err-status | error |
| b20 | light | preview | live |
| b21 | dark | warn-active | warning |
| b22 | light | sel-free | selection, free |
| b23 | dark | warn-row | warning |
| b24 | light | live-row | live |
| b25 | light | origin-axes | X, Y |
| b26 | dark | live-row | live |
| b27 | light | err-cell | error |
| b28 | light | conflict | error |
| b29 | dark | err-row | error |
| b30 | light | err-status | error |
| b31 | dark | live-chip | live |
| b32 | dark | conflict | error |
| b33 | light | live-chip | live |
| b34 | light | warn-tree | warning |
## Issues
| id | severity | repro | expected | actual |
|---|---|---|---|---|
| P5-CVD-B1 | low | Light and dark. SET plate_t=0 with a dimension using plate_t, EDITSKETCH Sketch1, look at the conflict rectangle (b28, b32) under deuteranopia. | Conflict curve distinguishable as error without hue (icon or dash). | Thick 2x outline only; deutan olive reads like the warning hue, protan slate-grey like neutral. Two misses of 176. |
| P5-CVD-B2 | info | Live extrude, tritan simulation, both themes (b05, b17, b18, b20, b24, b26, b31, b33). | Live distinguishable from error by more than hue. | Gold becomes salmon pink, the family of error/warning red; identification relies on tag/handle/chip shape and the lack of a triangle or octagon. Correct in all 32 views. |
| P5-CVD-B3 | info | Dial/readout under protan/deutan. | X/Y distinct. | X and Y same olive; letters decide. |
| P5-CVD-B4 | info | Sketch with the origin at the view centre. | Origin axes coloured X/Y (red/green) in the sketch view. | The origin axes in the sketch grid are faint grey, no axis colour; only dial/readout carry axis colours. |
| P5-CVD-B5 | info | Script: SELECT none then EXTRUDE 10 (no profile). | "SELECT A PROFILE TO EXTRUDE" error line (seen in the first pass). | No red line appeared in the card in the scripted flow (prompt only); not scored. |

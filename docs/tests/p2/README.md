# P2 GUI check (display :92, 2026-10-23)

Short mouse/keyboard pass on the release build of `pf/p2` (QT_SCALE_FACTOR 0.66, the private
Xwayland window was 945 px wide):

* `hole-ansi-switch.png`: Hole1 (4x M6 clearance, counterbore sized for an SHCS) opened by double-click;
  clicking **ANSI inch** switched the size list to `1/4-20` and refilled the counterbore with 0.438" x 0.25"
  (11.125 x 6.35 mm).
* `insert-fastener-card.png`, `fasteners-inserted.png`: a hole wall clicked, `FASTENER` typed, washer + nut +
  all equal holes ticked, Apply: 16 parts (4 x washer, M6x16 SHCS, washer, nut), one rigid group.
* `pocket-hole-card.png`, `pocket-hole-result.png`: `POCKETHOLE`, entry face clicked, exit edge given with
  `INPUT edges ...`, Kreg 15 deg, two holes along the 80 mm end of a 19 mm board.

Not covered by hand: the drawing Hole Table (unit tests only), the standard-part edit card.

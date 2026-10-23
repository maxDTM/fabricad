# Test fixtures (production interop, P3)

| File | What | Source / licence |
|---|---|---|
| `nist_ctc_01_asme1_ap242-e1.stp` | NIST MBE PMI Combined Test Case 1, STEP AP242 with semantic PMI (396 kB). `test_step_import` imports it with zero failed faces. | NIST "NIST PMI STEP files" (https://www.nist.gov/document/nist-pmi-step-files), a work of the US government, not subject to copyright in the US. |
| `sheet_r12_golden.dxf` | Sheet DXF R12 output of main's `DxfWriter` for the sample in `src/drawing/tests/test_face_dxf.cpp` (`sheetR12Unchanged`): the sheet export must stay byte for byte. | Generated from main at `24e9714` by compiling the same sample against main's `libfabricad_drawing.a`. |

IGES and 3MF samples are produced by the tests themselves (`test_export_formats`: IGES round trip,
3MF package read back) rather than stored.

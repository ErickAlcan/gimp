# WS04 — XCF

Goal: lossless XCF save/load of CMYK images (v28).

Owned files: app/xcf/*, app/tests/test-xcf.c.

Steps
1. xcf.c: add loader slot for version 28 (same xcf_load_image).
2. xcf-load.c: accept image base type ≤ GIMP_CMYK when `file_version >= 28`; layer types 6/7 →
   base CMYK; format via `gimp_image_get_layer_format()`; floating-selection and channel paths
   unchanged; after loading parasites, if the icc profile is not usable for CMYK keep default and
   `gimp_message` a warning; refuse CMYK when `! gimp_babl_has_cmyk_support()` with a clear error.
3. xcf-save.c: layer type via `gimp_babl_format_get_image_type()`; nothing else expected (tile
   writer is bpp generic) — verify RLE/zlib with bpp 5/10/20/40.
4. Version rule lives in gimpimage.c (01) — verify it triggers.
5. test-xcf.c CMYK variants (TESTING B2).

Acceptance: B2 passes; RGB XCF versions unchanged; loading a v28 file in this build only.

Risks: header validation ordering (precision check `gimp_babl_is_valid` before profile);
backwards compatibility messages.

Status: (to be filled)

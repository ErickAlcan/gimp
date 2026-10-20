# WS04 — XCF

**Goal**: lossless XCF save/load of CMYK images as version 28; graceful handling of bad
profiles and legacy modes; old GIMP refuses v28 cleanly.

**Owned files**: `app/xcf/*`, `app/tests/test-xcf.c` (keep CMYK helpers `static` in it; use
`gimp-app-test-utils` only for what 01a put there).

**Dependencies**: starts at the 01a merge; needs nothing else. Merges fifth (after 03, only to
keep one rebase stream).

**Steps**
1. `xcf.c`: loader slot for version 28 (`xcf.c:69-99`, same `xcf_load_image`).
2. `xcf-load.c`: accept base type `GIMP_CMYK` only if `file_version >= 28` (header check
   `:350` `> GIMP_INDEXED`), layer types 6/7 likewise (`:3881-3916`); validate precision with
   `gimp_babl_is_valid (GIMP_CMYK, p)`; without `gimp_babl_has_cmyk_support()` → clear GError,
   no image (`xcf-load.c:698` must not see NULL from `gimp_image_new`). Layer formats via
   `gimp_image_get_layer_format()`.
3. Load end (INTERFACES §13): CMYK image without a usable CMYK profile → attach default (no
   undo) + `gimp_message` warning (I1); legacy modes on CMYK layers → `gimp_layer_mode_get_non_legacy()`
   + warning (I9). Serialized GeglColors with CMYK spaces load through
   `gimp_color_profile_get_space()` (`xcf-load.c:4510`, I8).
4. `xcf-save.c`: layer type via `gimp_babl_format_get_image_type()`; verify tile RLE/zlib with
   bpp 4/5/8/10/16/20/32/40; version comes only from `gimp_image_get_xcf_version()` (01a).
5. Tests X1–X4 (TESTING); Tier A on owned files.

**Acceptance**: X1–X4 pass; existing xcf suite green; RGB XCF versions unchanged; G1–G3 green.

**Risks**: header validation ordering (precision check before the profile exists); upstream
claiming v28 during the 3.4 cycle (R10 — coordinate on rebase); large embedded ICC in
serialized colors (accepted, INTERFACES §3.4).

**Status**: (agent fills in)

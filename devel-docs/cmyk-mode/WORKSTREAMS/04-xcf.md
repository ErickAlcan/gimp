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

**Status**: see below.

## Status

Done, ready to merge. Commits on `ws/04`: `df7521d` (app/xcf), `ff8ecc4` (tests), this
section. Build warning-free in owned files. `meson test`: 26 OK, 4 SKIP (compositing/image/
histogram skeletons, babl probe), 1 FAIL (`appdata_file`, network) = baseline;
`layer-modes-golden` (G1–G3) green.

**What landed**
* `xcf.c`: loader slot for version 28 (`xcf_load_image`). The saver already takes its
  version only from `gimp_image_get_xcf_version()` (01a: CMYK → 28) and its layer type from
  `gimp_babl_format_get_image_type()`; tile RLE/zlib code is bpp-generic (verified by X1
  for bpp 4/5/8/10/16/20/32/40). No saver change was needed.
* `xcf-load.c` header: base type 3 accepted only with `file_version >= 28`; precision via
  the existing `gimp_babl_is_valid()` (precedes any profile, so the ordering risk does not
  apply); without `gimp_babl_has_cmyk_support()` → `G_FILE_ERROR_NOSYS` "…CMYK images are not
  supported by this installation…", returned without the "This XCF file is corrupt:" prefix,
  no image (`gimp_create_image` is never reached).
* Layer types 6/7 accepted only from v28 and only in CMYK images; a CMYK image accepts CMYK
  layers plus GRAY (floating selections of channels/masks) only. Layer formats come from
  `gimp_image_get_layer_format()` as before.
* I1: right after the image properties (before any layer exists, so every layer gets the
  final space) — if no valid `icc-profile` parasite was attached or the profile is not
  CMYK+bidirectional → default CMYK profile (undo disabled) + warning "…without a usable CMYK
  color profile…". An input-only profile parasite is already refused by
  `gimp_image_parasite_validate()` (its own warning); the image keeps the default it was
  constructed with. New field `XcfInfo.icc_profile_loaded`.
* I9: after all layers (incl. vector-layer conversion) and in the partial-load path, every
  legacy mode on a CMYK image's layers is mapped to its non-legacy mode + one warning.
* I8: serialized GeglColors with a CMYK ICC already load through
  `gimp_color_profile_get_space()`; no change needed.
* Older GIMP refuses v28 with its existing "unsupported XCF file version" error (the
  loader table has no slot); asserted for version 99 in X4.
* Conversion intent: `_gimp_image_{set,get}_conversion_intent()` is transient by design
  (INTERFACES §4, reset after each conversion), so it is not persisted. Simulation
  intent/BPC/profile persist as before (X1 covers the simulation profile).

**Acceptance**
| id | result |
|---|---|
| X1 | pass — `/gimp-xcf/cmyk_round_trip`: u8, u16, u32, half, float, double (alternating RLE/zlib), opaque + alpha layers, Multiply 50 % layer with offset and mask, channel, selection, builtin + simulation profile: base type, precision, profile bytes, layer space pointer, formats, modes, opacity, offsets, every buffer `memcmp`-identical, saved version 28, no load messages. Group-layer variant `/gimp-xcf/cmyk_round_trip_group`: **pending integration (WS02)**, skips while a subprocess probe hits `gimpgrouplayer.c get_projection_format` "should not be reached" for CMYK. |
| X2 | pass — RGB image saves with `gimp_image_get_xcf_version()` (< 28) |
| X3 | pass — header-variant builtin: loaded profile keeps the variant bytes, layer space == builtin's space (I8), buffers identical, `gimp_item_convert()` into a builtin-profile image bit-identical (K-only, C-only, paper and random patches). |
| X4 | pass — input-only parasite → default profile (bidirectional) + warning; `MULTIPLY_LEGACY` CMYK layer → `MULTIPLY` + warning; same file patched to v027 → error, no image; v099 → "unsupported XCF file version". |
| existing xcf tests | pass; G1–G3 green |

**Tier A** (owned files): #1 no base-type/image-type switch other than the layer-type
switch (has CMYK cases); range check `xcf-load.c:360` rewritten for CMYK. #2/#2b: `col[3]`
(channel colors, explicit `R'G'B'` formats) OK; `xcf-save.c` `pixel[40]` OK (≥ 40 bytes);
`buf[16]` is a skip buffer; remaining `[4]`/`* 4` hits are int32 sizes / path coords. No
native-format stack buffers. #3 no generated files touched.

**Deviations / workarounds**
* `gimp_layer_mode_get_non_legacy()` (WS02, INTERFACES §8.2) does not exist yet: private
  `xcf_load_get_non_legacy_mode()` in `xcf-load.c` (FOO_LEGACY → FOO; OVERLAY_LEGACY →
  SOFTLIGHT, matching the loader's existing OVERLAY_LEGACY → SOFTLIGHT_LEGACY fix-up).
  Replace with the WS02 function after its merge (note: §8.2 says "same-named", which would
  give OVERLAY for OVERLAY_LEGACY; unreachable from XCF load either way).
* X4 crafts its invalid states directly (`layer->mode = …LEGACY`,
  `gimp_parasite_list_add()` on the private parasite list), because 02/03 entry points will
  refuse them.
* The selection of a new image is `Y u8`, the loaded one `Y' u8` (U8 images, RGB too), so X1
  compares selection bytes without requiring the same format.

**Requests for other owners**
* WS02 (`gimpgrouplayer.c` `get_projection_format()`): add `case GIMP_CMYK:` (CMYKA in the
  image layer space); un-skips `/gimp-xcf/cmyk_round_trip_group`.
* WS09: the no-support XCF case (`G_FILE_ERROR_NOSYS`, no "corrupt" prefix) belongs in
  `test-cmyk-nosupport.c` (support is cached per process, so test-xcf cannot cover it).

**Risks**: R10 (upstream claiming XCF 28) unchanged — coordinate on rebase.

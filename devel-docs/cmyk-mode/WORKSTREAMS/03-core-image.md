# WS03 — Core image lifecycle, conversions, copies

**Goal**: convert images to/from CMYK with intent+BPC and exact undo; new images, templates,
duplicates, previews, buffers, paste/DnD, palettes and file-open behave for CMYK; same-profile
copies are exact (B1 at the app level, INTERFACES §11).

**Owned files** (PLAN §4.1): default owner of `app/core/*`; explicitly `gimpimage.{c,h}`,
`gimpimage-private.h`, `gimpimage-convert-type.{c,h}` (handed over at the 01a merge),
`gimpimage-convert-indexed.c`, `gimpimage-convert-precision.c`, `gimpimageundo.c`,
`gimpimage-undo-push.c`, `gimpimage-new.{c,h}`, `gimptemplate.{c,h}`, `gimpimage-duplicate.c`,
`gimpimage-metadata.c`, `gimpimage-colormap.c`, `gimpimage-preview.c`, `gimpdrawable-preview.c`,
`gimpimage-savable.c`, `gimpsavable.c`, `gimpsavable-load.c`,
`gimpimage-{crop,flip,rotate,resize,scale,transform}.c`, `gimplayer.c` (except
`gimp_layer_update_mode_node()`, owned by 02), `gimplayer-new.c`,
`gimplayer-floating-selection.c`, `gimp-edit.c`, `gimpbuffer.c`, `gimpchannel.c`,
`gimpselection.c`, `gimplink.{c,h}`, `gimpcontext.c`, `gimp-utils.c`, `gimppalette.c`,
`gimppalette-load.c`, `gimppalette-import.c`, `gimppickable-auto-shrink.c`, `app/file/*`,
`app/file-data/*`, `app/tests/test-cmyk-image.c`.

**Dependencies**: starts at the 01a merge. Acceptance needs 01b (lcms conversions with intent)
and 02 (group-layer conversion, projection, flatten). Merges fourth (after 02).

**Steps**
1. `gimp_image_convert_type_full()` exactly in the push order of INTERFACES §6 (legacy-mode
   mapping first; precision; base type; profile; intent; drawables with explicit src/dest
   profiles; `gimp_image_fix_layer_format_spaces()`); Indexed → CMYK in two steps within one
   group; CMYK → Indexed refused; `gimp_image_convert_type()` wraps `_full (…, PERCEPTUAL,
   TRUE)`.
2. `gimplayer.c`: `gimp_layer_real_convert_type()` reads `_gimp_image_get_conversion_intent()`;
   `gimp_layer_set_mode()` maps legacy → non-legacy when the image is CMYK (I9);
   `gimplayer-new.c`: `gimp_layer_new_convert_buffer()` does a plain `gimp_gegl_buffer_copy()`
   when `gimp_color_profile_is_equal (src, dest)`.
3. `gimpimage.c` (after handover): setting `base-type` resets `visible[]`/`active[]` (m8);
   anything else 01a left as TODO.
4. `gimpimageundo.c`: emit profile-changed on base-type changes involving GRAY **or CMYK**.
5. `gimpimage-convert-indexed.c`: refuse CMYK ("Convert the image to RGB first");
   `gimpimage-convert-precision.c`: CMYK only to `*_NON_LINEAR` (I2), assert in API.
6. `gimpimage-new.c`, `gimptemplate.c`: CMYK templates (profile must be CMYK+bidirectional,
   else default + warning), base type CMYK forces non-linear TRC; new-from-drawable/buffer/
   pixbuf for CMYK sources.
7. Previews (INTERFACES §10): `gimp_drawable_get_preview_format()` /
   `gimp_image_get_preview_format()` CMYK → `R'G'B'(A) u8` (NULL space).
8. Copies (INTERFACES §11): `gimp-edit.c` paste paths and floating format (`:464-548, 587-614`),
   `gimpbuffer.c` (buffers keep format + profile), `gimplayer-floating-selection.c`,
   new-from-visible, `gimp_item_convert()` paths in `gimplayer.c` (equal profiles never
   convert; RGB↔CMYK and different CMYK profiles convert PERCEPTUAL+BPC).
9. Others: `gimpimage-duplicate.c`, `gimpimage-metadata.c` (Exif ColorSpace for CMYK =
   uncalibrated), `gimpimage-colormap.c` (no colormap for CMYK), palettes (import from CMYK
   via RGB), `gimp-utils.c`, `gimpcontext.c` (`gimp_context_get_rgba_format()` with a CMYK
   image), `gimpsavable-load.c:2227` → `gimp_color_profile_get_space()` (I8),
   `gimpimage-savable.c:638`, `gimpchannel.c`/`gimpselection.c`/`gimplink.c` base-type sites,
   `app/file/*` (import-policy hook → `gimp_image_import_color_profile()`), `app/file-data/*`
   (brush/pattern from a CMYK image → convert to RGB).
10. Tests I1–I15 in `test-cmyk-image.c`; Tier A on owned files.

**Acceptance**: I1–I15 pass; G1–G3 and existing core tests green; undo of every conversion
restores bytes exactly (I2).

**Risks**: undo ordering (follow INTERFACES §6 literally); transient states during undo (rely
on 01a's `get_layer_space()` rule, never attach profiles in undo code); paste paths that
convert via `gimp_drawable_convert_type()` with NULL profiles; previews performance on huge
images (babl reference fish on the scaled result only).

## Status

Done (A3) for everything that does not need a sibling merge; ready to merge after 02.
`meson test` on ws/03: 27 OK, 3 SKIP (compositing/histogram skeletons, babl probe), 1 FAIL
(`appdata_file`, network = baseline). `layer-modes-golden` (G1–G3) green on every commit.
`cmyk-image` also passes with `GIMP_TESTING_CMYK_PRECISION=u8|half|double` and with
`GIMP_TESTING_CMYK_ICC=default_cmyk.icc:FOGRA39L_coated.icc`.

**What landed**
* `gimp_image_convert_type_full()`: RGB/GRAY/INDEXED → CMYK, CMYK → RGB/GRAY, undo order
  of INTERFACES §6 (legacy modes → precision → base type → profile → drawables with explicit
  src/dest profiles and the conversion intent → layer format spaces), indexed → CMYK in one
  group, linear precisions → non-linear (channels follow the U8 linear → non-linear change),
  invalid targets rejected before anything is pushed. RGB ⇄ GRAY path unchanged.
  `gimp_image_convert_indexed()` refuses CMYK ("Convert the image to RGB first").
* `gimplayer.c`: `gimp_layer_set_mode()` maps legacy modes on CMYK images (I9), new
  `gimp_layer_map_legacy_mode()`, `gimp_item_convert()` into a CMYK image maps too;
  `real_convert_type()` reads `_gimp_image_get_conversion_intent()`; CMYK conversions without a
  dest profile keep the image's CMYK space (fixes NULL-space CMYK buffers in the paste/floating
  paths, I3). `gimplayer-new.c`: equal-profile short-circuit (plain copy, B1).
* `gimpimage.c`: base-type change resets `visible[]`/`active[]` (m8); simulation-profile
  parasite bug fixed (a simulation profile equal to the builtin detached the image profile;
  unparsable data criticaled). `gimpimageundo.c`: profile-changed for CMYK base-type changes.
* Previews (§10): drawable/image/buffer previews of CMYK are `R'G'B'(A) u8`, NULL space;
  -Wswitch in `gimpdrawable-preview.c` / `gimpimage-preview.c` fixed.
* Templates: CMYK forces non-linear TRC; new-from-template uses the template profile only if
  CMYK + bidirectional, else default + warning; no-CMYK-support fallback to RGB + warning.
* `gimpsavable-load.c` ICC → `gimp_color_profile_get_space()` (I8); `gimpimage-savable.c`
  validates the format (gimp_image_new() preconditions) and returns on error; Exif ColorSpace of
  CMYK = uncalibrated; clipboard patterns from CMYK data are RGB; palette import from CMYK is
  sRGB; file-open thumbnail image types CMYK(A); file-import float promotion non-linear for CMYK.

**Acceptance (TESTING I-series, `app/tests/test-cmyk-image.c`)**
| test | status |
|---|---|
| I1 structure, NULL/invalid targets, real profiles (env) | pass |
| I1 vs lcms ±2, PERCEPTUAL ≠ RELCOL (FOGRA39) | pending integration (01b) |
| I2 undo/redo ×3 incl. mask, channel, legacy layer, CMYK → RGB undo | pass |
| I2 with a group layer | pending integration (02: `gimpgrouplayer.c:1164` criticals for CMYK) |
| I3 structure | pass; vs lcms ±1/255: pending integration (01b) |
| I4, I5, I7, I8, I12, I14, I15 | pass |
| I6 | pending integration (02, projection) |
| I9 buffer paste (in place, new layer, new image) | pass (bit-identical) |
| I9 copy-of-selection pastes, floating + anchor | pending integration (02: the clipboard image is cleared outside the selection through the RGB composite today — measured K100 → C≈0.67) |
| I10 `gimp_item_convert`, buffer paste, header-variant profile | pass (bit-identical); copy-of-selection part pending 02 |
| I10 open-as-layers via XCF | pending integration (04: "unsupported XCF file version 28") |
| I11 RGB → CMYK (item convert, buffer paste, floating format) and back | pass; lcms ±2 pending (01b) |
| I13 | pass; legacy group layer part pending (02) |
Gating: 01b detected by the builtin profile description still containing "(stub)", 02 by a weak
reference to `gimp_layer_mode_is_cmyk_native()`, 04 by the XCF round trip.

**Tier A** (owned files touched): every base-type switch has a CMYK case or an intentional
default (`gimppattern-load.c:479` is a PS pattern header switch: RGB/gray only, OK). #2b hits:
`gimpimage-convert-indexed.c` `rgb[3]` (RGB formats, OK), `gimppalette-import.c:212` `rgb[4]`
(R'G'B'A u8 restriction format, OK), `gimppalette-load.c:1064,2223` `gfloat [4]` with
`CMYK float` (16 bytes, fits, OK), `gimplayer.c:1667` `A double` (OK), `gimpimage.c:1790`
passes the caller's buffer (callers: 08/07). `gimp_babl_compat_u8_mask_format()` callers
(`brush-cmds.c`, `gimpbrushselect.c`) only see brush masks (Y formats), OK.

**Deviations** (documented in INTERFACES §6): private copy of
`gimp_image_fix_layer_format_spaces()`; private legacy → non-legacy table until 02's
`gimp_layer_mode_get_non_legacy()` lands; new `gimp_layer_map_legacy_mode()`.

**Requests for other owners**
* 02: `gimpgrouplayer.c` `get_projection_format()` (`:1164`) needs a `GIMP_CMYK` case
  (`CMYKA <precision>` in the image space) and `gimp_group_layer_convert_type()` should read
  `_gimp_image_get_conversion_intent()`; `gimp_drawable_edit_clear()` must leave masked-out CMYK
  pixels bit-identical (I6) — `gimp_edit_copy()` of a selection depends on it.
* 02 (after merge): replace `gimp_layer_get_non_legacy_mode()` in `gimplayer.c` by
  `gimp_layer_mode_get_non_legacy()` (03 can do it at merge time).
* 01b: optionally export `gimp_image_fix_layer_format_spaces()` from
  `gimpimage-color-profile.c` so convert-type can drop its copy.
* 04: XCF v28 load (I10 open-as-layers); `gimp_layer_map_legacy_mode (layer, FALSE)` is
  available for the I9 mapping at load.
* 07: `app/widgets/gimpimagepropview.c:474` -Wswitch (GIMP_CMYK) seen in the build.

**Risks**: `gimplink.c` links to CMYK files create a CMYK image with the default profile, not
the linked file's profile (the buffer keeps its own space); the link layer path was not
exercised. Text layers converted to CMYK re-render through 08's code. Pre-01b, intent is
ignored for CMYK (babl relcol+BPC), so conversions are correct only for same-intent use.


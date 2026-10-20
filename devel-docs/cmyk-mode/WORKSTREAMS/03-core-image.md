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

**Status**: (agent fills in)

# WS03 — Core image lifecycle and conversions

Goal: convert images to/from CMYK with intent+BPC, correct undo, new images/templates/
duplicates/previews/buffers/file-open behave for CMYK.

Owned files: PLAN §3 (03).

Steps
1. `gimp_image_convert_type_full()` (INTERFACES §6, PLAN D7): set base type (+ precision TRC to
   NON_LINEAR when needed, with precision undo), set dest profile right after the base type when
   target is CMYK (or after, as today, for GRAY), set conversion intent, convert drawables, reset
   intent; Indexed→CMYK two-step; error strings.
2. `gimp_layer_real_convert_type()` uses `_gimp_image_get_conversion_intent()`.
3. gimpimageundo.c: profile-changed emission for CMYK base-type changes.
4. gimpimage-convert-indexed.c: refuse CMYK. gimpimage-convert-precision.c: CMYK keeps
   NON_LINEAR (reject/ignore TRC change for CMYK, assert in API).
5. gimpimage-new.c: template CMYK handling (profile validation → default), new-from-drawable/
   buffer/pixbuf for CMYK sources; gimptemplate.c base-type CMYK forces non-linear TRC.
6. Previews/thumbnails (gimpimage-preview.c, gimpdrawable-preview.c): CMYK → RGB(A) u8 via the
   image's to-sRGB transform for display previews.
7. gimpbuffer.c (copy/paste CMYK buffers keep format+profile; paste CMYK into RGB converts with
   profile and vice versa), gimpimage-duplicate.c, gimpimage-metadata.c (colorspace tag),
   gimpchannel.c/gimpselection.c/gimplink.c base-type sites, palettes, file-open/import
   (import policy dialog CMYK branch hook), file-data (brush/pattern from CMYK image → RGB).
8. Tier A audits; local runs of T5–T11, T17, T20, T22.

Acceptance: T7–T11, T17, T20, T22 pass; existing core tests pass.

Risks: undo ordering (profile vs base type), precision+type in one group, previews performance.

Status: (to be filled)

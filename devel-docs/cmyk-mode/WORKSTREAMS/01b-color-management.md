# WS01b — Color management: lcms CMYK paths, final builtin, profile conversions

**Goal**: CMYK color transforms that honour intent/BPC and never re-separate equal profiles
(B1), lcms format handling for every CMYK encoding (M3), the final builtin profile (M4), and
CMYK→CMYK convert / assign / import in core.

**Agent**: A1, immediately after 01a merges.

**Owned files**: `libgimpcolor/gimpcolorprofile.{c,h}`, `libgimpcolor/gimpcolortransform.{c,h}`,
`libgimpcolor/gimpcolor.def`, `app/core/gimpimage-color-profile.{c,h}`,
`app/tests/test-cmyk-foundation.c`.

**Dependencies**: starts at the 01a merge; merges second (before 02).

**Steps**
1. `gimp_color_profile_get_lcms_format()` per INTERFACES §3.5: `CMYKA` recognised, add
   `TYPE_CMYKA_8`, u32/half/float/double → `CMYK(A) u16`, inverted and premultiplied models
   mapped; no CMYK model reaches the RGB float fallback (`gimpcolorprofile.c:1795-1805`) or
   `g_return_val_if_reached` (`:1693-1702`). gtk-doc the I5 reliance (spaces stripped).
2. `gimp_color_transform_new()` / `_new_proofing()` decision table (INTERFACES §3.3): equal CMYK
   profiles → babl on one space (even with `GIMP_COLOR_TRANSFORM_DISABLE_BABL`); any other case
   involving CMYK → lcms. Do **not** add `NOOPTIMIZE` yourself (callers pass it; 02's
   converter relies on optimized 16-bit device links).
3. `gimp_color_transform_can_gegl_copy()`: FALSE iff either profile is CMYK and not equal.
4. Final builtin (INTERFACES §3.4): 9⁴ AToB, 17³ BToA with the near-black fade, linked
   intents, fixed header, zero ID, desc "GIMP built-in CMYK (generic, not for press)", cprt
   "Public Domain". Port `/root/review-scratch/mkcmyk-rev2.c`. Update F3's SHA-256 constant
   once and say so in Status.
5. `gimpimage-color-profile.c`: `gimp_image_convert_color_profile()` handles `GIMP_CMYK`
   (`gimp_image_convert_profile_layers()` with lcms, intent, BPC); `assign` relabels +
   `gimp_image_fix_layer_format_spaces()`; `gimp_image_import_color_profile()` CMYK policy
   (preferred = cmyk-profile pref / builtin; never RGB; "ask" skipped when the profile equals
   the builtin or the pref); `get_color_transform_{to,from}_srgb_*()` for CMYK.
6. Tests F3 (final), F8–F11 in `test-cmyk-foundation.c`.
7. Tier A audits on owned files.

**Acceptance**: F1–F12 pass (incl. F9 with `GIMP_COLOR_TRANSFORM_DISABLE_BABL=1`); G1–G3
green; baseline green; F3 builtin < 100 KB and its babl K100 round trip K ≥ 0.98, CMY ≤ 0.05.

**Risks**
* 16-bit optimized lcms device links lose a little accuracy (≤ 2/255 measured class); explicit
  conversions pass `NOOPTIMIZE` already (`gimp_gegl_convert_color_profile`).
* `cmsLinkTag` + input-only derivative: tests must delete linked tags too.
* Thread safety of the builtin `GOnce` and of the I8 cache (GMutex).
* A transform for an input-only CMYK *target* must fail cleanly (NULL + message), never reach
  babl.

**Status**: (agent fills in)

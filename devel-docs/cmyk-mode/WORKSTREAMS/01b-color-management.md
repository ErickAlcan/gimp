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
4. Final builtin (INTERFACES §3.4): grids 9⁴ AToB / 17³ BToA (near-black fade already in the stub), linked
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

## Status

**Done (A1), ready to merge.** Owned files build warning-free. `meson test`: 26 OK, 4 SKIP
(compositing/image/histogram skeletons, babl probe), 1 FAIL (`appdata_file`, network), the
same as baseline. **G1–G3** `layer-modes-golden` green.

What landed
* `gimp_color_profile_get_lcms_format()` (INTERFACES §3.5): CMYK models are handled before
  the RGB/gray code via `BABL_MODEL_FLAG_CMYK`. `CMYK(A) u8/u16` map to
  `TYPE_CMYK(A)_8/16` unchanged; every other type and the `cmyk`, `cmykA`, `CaMaYaKaA` and
  `camayakaA` models map to `CMYK(A) u16` in the same space. No CMYK model reaches the float
  fallback or `g_return_val_if_reached`. `TYPE_CMYKA_8` is defined if missing. The I5
  reliance is documented in gtk-doc.
* `GimpColorTransform` decision table (§3.3): equal CMYK profiles with CMYK formats use a
  babl re-encoding on `gimp_color_profile_get_space (src)`, even with
  `GIMP_COLOR_TRANSFORM_DISABLE_BABL`. Any other transform involving CMYK goes through lcms
  and never calls `get_format()` / babl spaces. An input-only CMYK target returns NULL with
  an lcms message. `NOOPTIMIZE` is not added (callers pass it).
  `can_gegl_copy()` is FALSE iff either profile is CMYK and the profiles are not equal.
* Final builtin (§3.4): 9⁴ / 17³, desc "GIMP built-in CMYK (generic, not for press)", cprt
  "Public Domain". **79 432 bytes, SHA-256
  `864a65a0c8b1bcc4e2186fec9e9c81c3950f102564e805c6822e27e20bda9307`**. The F3 constant was
  updated once (stub was `f900f7ab…16263`). It is 60 bytes smaller than the prototype
  because cprt is "Public Domain" rather than the description text.
* `gimpimage-color-profile.c`: CMYK→CMYK `convert_color_profile()` goes through
  `gimp_image_convert_profile_layers()` (lcms, intent, BPC). `assign` relabels via
  `fix_layer_format_spaces()`, and `assign (NULL)` on CMYK uses
  `gimp_get_default_cmyk_profile()` (I1). The import policy's CMYK target is always a
  bidirectional CMYK profile; a non-CMYK profile from the policy dialog is replaced by the
  default. `gimp_image_create_color_transforms()` falls back to the builtin of the base
  type when the stored profile and the base type disagree (transient undo states,
  INTERFACES §4), and skips from-sRGB for an input-only CMYK profile.

Acceptance evidence (`cmyk-foundation`, 13 tests, all OK)
* F1–F8, F12 still pass. **F3** final: < 100 KB, header date and zero ID, deterministic,
  SHA-256 as above, lcms K100 in [0.07, 0.09], paper white. **babl K100 round trip
  (0.0092, 0.0387, 0.0306, 0.9940)**: K ≥ 0.98 and CMY ≤ 0.05.
* **F9**: builtin ↔ header variant, `CMYK u16→u16`, `CMYKA float→u16`, `CMYKA u8→double`,
  bit-identical via `process_pixels` and `process_buffer`, also with
  `GIMP_COLOR_TRANSFORM_DISABLE_BABL=1`. `can_gegl_copy` TRUE for equal profiles; FALSE for
  a different CMYK profile, for sRGB in either direction, and for an input-only target.
  An input-only target gives NULL from CMYK and from RGB, and works as a source.
* **F9b** (extra): convert to the header variant is a no-op. Convert to a different CMYK
  profile equals a direct lcms transform (relcol + BPC + NOOPTIMIZE) ±1 u16, and the layer
  space becomes that profile's space. Undo restores profile and bytes. Assign relabels
  (bytes identical, new space); assign NULL gives a bidirectional default; an RGB profile is
  refused. The image's to/from-sRGB transforms exist for CMYK.
* **F10**: `CMYKA u16` without space ↔ with the builtin space is the identity on 1000 random
  pixels, both directions. `R'G'B'A float` tagged with the CMYK space equals sRGB, both
  directions.
* **F11**: CMYK/CMYKA × u8, u16, u32, half, float, double, plus `cmyk u8`, `cmykA u8`,
  `cmykA float`, `CaMaYaKaA float` and `camayakaA float`, match a double lcms reference
  within 1/255 (alpha copied). The mapped lcms type is checked as integer CMYK with the
  right extra channel. No criticals.
* Also OK with `GIMP_COLOR_TRANSFORM_DISABLE_BABL=1`, `GIMP_TESTING_CMYK_PRECISION=double`
  and `GIMP_TESTING_CMYK_ICC=default_cmyk.icc:FOGRA39L_coated.icc`.

Tier A on owned files
* #1: the one base-type switch (`convert_color_profile`) has a CMYK case. No range checks.
* #2: `cmyk[4]` is the builtin sampler's internal ink array (OK). `"RGBA float"` is the
  Lab/LCh path of `get_lcms_format` (RGB-only, OK).
* #2b: `rgb[3]` in the builtin samplers is internal (OK). No pick/sample calls.
* #3: no generated files touched. #5: isolated compile OK.
* #4 babl probe: `cmyk-babl-probe.py` is still WS09's skeleton (exit 77). The facts it is
  meant to probe are asserted in C by F3 (round trip), F8 (same space / header variant) and
  F10 (I5).

Notes, risks and requests
* With the v4 builtin, **perceptual + BPC maps K100 to about 12/255 sRGB**, not 0: lcms
  maps v4 perceptual black to the perceptual reference medium black (L*≈3). Relcol (no BPC)
  gives about 0.08. This affects display/preview transforms (07, 03), which may prefer
  relcol + BPC if true black is wanted.
* A no-alpha CMYK source into an RGBA destination leaves the destination alpha unwritten
  (lcms `COPY_ALPHA` needs alpha on both sides). This is unchanged behaviour, the same as
  RGB→RGBA. Callers should pass matching alpha.
* Request to 02 (`app/gegl/gimp-gegl-loops.cc`, `gimp_gegl_convert_color_profile()`): when
  `gimp_color_transform_new()` returns NULL it falls back to `gimp_gegl_buffer_copy()`, which
  is a babl conversion. For an input-only CMYK target that would reach the babl crash. All
  core callers validate targets first, but a guard (no copy when the dest profile is CMYK and
  not bidirectional) would make it safe.
* Thread safety: builtin via `GOnce`; I8 cache under a `GMutex` (01a). lcms transforms are
  shared across `gegl_parallel_distribute_area` threads, as for RGB today.
* Commit trailers: the first four 01b commits carry the COMMON-BRIEF trailer ("Claude Fable
  5.1"); later commits use the session's attribution ("Claude Opus 5.5").
* INTERFACES.md: no deviation. Signatures unchanged; no new public symbols, so
  `gimpcolor.def` is unchanged.

### Follow-up: CMYK GeglColor alpha (WS08 finding; `libgimpcolor/gimpcolor.c` now owned by 01b)
* Bug: `gimp_color_set_alpha()` went through `gegl_color_get/set_rgba()`, so
  `gimp_context_real_set_foreground/background()`, which forces opaque alpha, re-separated
  an image-space CMYK FG/BG (K100 became C≈0.009 rich black). This broke exact FG fills
  (INTERFACES §11, C4).
* Fix: for any `BABL_MODEL_FLAG_CMYK` format, `set_alpha` reads and writes
  `CMYKA double` in the colour's own space and changes only alpha, then re-stores the alpha
  variant of the original encoding. No RGB is involved. gtk-doc says so.
* Audit of `gimpcolor.c`:
  * `is_out_of_self_gamut` now uses the CMYK model flag (the premultiplied CMYK models were
    missing) and reads CMYK in the same space.
  * `is_out_of_gamut` (CMYK space), `param_color_cmp` (memcmp in the native format,
    48-byte buffers ≥ CMYKA double), `get_with_alpha` (CMYK→CMYKA, cmyk→cmykA) and the
    perceptual/CIE2000 helpers (comparison only, no writes) are OK.
  * In `app/core/gimpcontext.c` the FG/BG colours go only through `set_alpha`, with no other
    RGB round trip.
* **F13** (`/cmyk-foundation/F13-foreground-inks`): K100, (0.1,0.2,0.3,0.4) and C100 in
  CMYKA u16/u8/float/double, CMYK float/u16, `cmykA u8` and `CaMaYaKaA float`, in the
  builtin space.
  * `set_alpha (0.5)` and `(1.0)` keep the inks; the colour's space is unchanged.
  * `gimp_context_set_foreground()` gives the same space, the same inks, alpha 1, and
    bit-identical stored bytes. Premultiplied encodings are checked within 1e-6, because
    re-premultiplying in float costs precision.
  * Fails without the fix (C = 0.0092 on K100), passes with it.
* Suite: 26 OK / 4 SKIP / 1 FAIL (`appdata_file`); G1–G3 green.


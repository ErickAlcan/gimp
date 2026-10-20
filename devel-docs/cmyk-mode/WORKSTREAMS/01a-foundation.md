# WS01a — Foundation (lands first, target: hours)

**Goal**: make `GIMP_CMYK` exist in the data model, with every INTERFACES declaration present
(working or stubbed), canonical CMYK spaces (I8), a stub builtin profile, the RGB golden test
captured on baseline code, and all CMYK test files registered as skeletons — so that every
other workstream can start the moment this merges. No UI, no PDB, no compositing changes.

**Agent**: A1 (continues with 01b right after the merge).

**Owned files** (PLAN §4.1)
* `libgimpbase/gimpbaseenums.h`, `app/core/core-enums.h`, `app/core/core-types.h`
* `app/gegl/gimp-babl.{c,h}`, `app/gegl/gimp-babl-compat.{c,h}`
* `app/core/gimpdrawable.{c,h}`, `app/core/gimpprojection.c`
* `app/core/gimpimage.{c,h}`, `app/core/gimpimage-private.h` (→ 03 at merge)
* `app/core/gimpimage-convert-type.{c,h}` (decl + stub; → 03 at merge)
* `libgimpcolor/gimpcolorprofile.{c,h}`, `libgimpcolor/gimpcolor.def` (→ 01b)
* `app/core/gimpimage-color-profile.{c,h}` (→ 01b)
* `app/tests/meson.build`, `app/tests/gimp-app-test-utils.{c,h}` (→ 09 at merge)
* `app/tests/test-layer-modes-golden.c`, `app/tests/layer-modes-golden.h` (frozen at merge)
* skeletons of `test-cmyk-{foundation,compositing,image,histogram,nosupport}.c`,
  `test-cmyk-{e2e,file-formats}.py`, `cmyk-babl-probe.py`, `libgimp/tests/test-cmyk.py`
  (→ owners per INTERFACES §15; the one registration line in `libgimp/tests/meson.build` is
  added by 01a with 05's agreement)
* generated (regen commit): `libgimpbase/gimpbaseenums.c`, `app/core/core-enums.c`,
  `pdb/enums.pl`, `libgimp/gimpenums.h`, `libgimp/gimpenums.c.tail`

**Dependencies**: none. Merge first; tag `cmyk-foundation`.

**Steps** (commit after each)
0. **Golden test first, on the untouched tree**: write `test-layer-modes-golden.c` (TESTING
   G1–G3; GEGL threads 1; seeded content; SHA-1 of projection/drawable bytes), register it,
   run with `GIMP_TESTING_GOLDEN_WRITE=app/tests/layer-modes-golden.h`, commit test + header.
   From now on G1–G3 must stay green on every commit of every workstream.
1. Enums per INTERFACES §1 (component-mask aliases as `#define`s). `ninja -C _build`; commit
   the regenerated files in a separate "regen" commit.
2. `core-types.h` defines (`CYAN … ALPHA_C`).
3. `gimp-babl.c/.h`, `gimp-babl-compat.c`: INTERFACES §2 completely — `gimp_babl_format()`
   CMYK branch, `is_valid`, base type via model flag, trc/precision, component formats and
   their registration, `CaMaYaKaA u32` registration, descriptions, builtin profile hook,
   `format_get_color_profile()` (I5), `compat_u8_format`, image types, and the new
   `gimp_babl_space_is_cmyk()`, `gimp_babl_format_is_cmyk()`, `gimp_babl_has_cmyk_support()`
   (mid-tone probe, `GIMP_TESTING_NO_CMYK`).
4. libgimpcolor (`gimpcolorprofile.c/.h`, `.def`, gtk-doc "Since: 3.4"):
   * I8 cache inside `gimp_color_profile_get_space()` for CMYK profiles (INTERFACES §3.1) —
     final, not a stub;
   * `gimp_color_profile_is_cmyk_bidirectional()` (final);
   * `gimp_color_profile_new_cmyk_builtin()` **stub**: generator of INTERFACES §3.4 with grids
     3⁴/3³ (otherwise identical), desc "GIMP built-in CMYK (stub)", fixed header bytes, zero ID,
     cached with `GOnce`. Reference: `/root/review-scratch/mkcmyk-rev2.c` (the identity
     `cmsStageAllocToneCurves (NULL, n, NULL)` stages are mandatory, else 0-byte save).
5. `gimpimage.c/.h/-private.h`: `constructed()` attaches the default CMYK profile (no undo, not
   dirty); `get_format`, `get_layer_format`, projection format; `get_layer_space()` transient
   rule (INTERFACES §4); component format/index/active/visible; masks (I7) + the GRAY `ALPHA_G`
   fix (`gimpimage.c:4076`); `_gimp_image_{set,get}_conversion_intent()`; XCF version rule
   28 → 340; preconditions in `gimp_image_new()` (m6).
6. `gimpimage-color-profile.c/.h`: validation by format (CMYK + bidirectional, error texts);
   `gimp_image_get_builtin_color_profile()` for CMYK; `gimp_get_default_cmyk_profile()`;
   `gimp_image_set_color_profile()` / `set_icc_profile()` NULL on CMYK → default (I1 at entry
   points only — never in parasite/undo code); `_gimp_image_set_hidden_profile()` no-op for
   CMYK; `case GIMP_CMYK:` in convert/assign switches (assign = relabel; CMYK→CMYK convert
   returns FALSE "not implemented" until 01b).
7. `gimpdrawable.c/.h`: `gimp_drawable_is_cmyk()`, component format/index, base-type switches.
   `gimpprojection.c`: `estimate_memsize` CMYK case (`:295-300`).
8. `gimpimage-convert-type.h`: declare `_full`; `.c` stub per INTERFACES §6.
9. Tests: CMYK helpers in `gimp-app-test-utils.{c,h}` (CMYK image at a precision, patch
   write/read with explicit formats, input-only derivative, header variant, precision knob);
   all skeleton files (one skipped test / passing stub each) registered (C files in
   `app_tests`; `test-cmyk-nosupport` as its own executable; Python files in `tests`; append
   `file-tiff/file-tiff:file-jpeg/file-jpeg:file-psd/file-psd` once to `test_env`'s
   `GIMP_TESTING_PLUG_INS`); F1–F8 and F12 in `test-cmyk-foundation.c` (F3: determinism,
   header bytes, bidirectional only).
10. Tier A audits on owned files; record results in Status.

**Acceptance**
* Warning-free build of owned files; baseline 20/21; **G1–G3 green**; F1, F2, F3 (stub checks),
  F4–F8, F12 pass; skeletons registered and skipping.
* `gimp_image_new (gimp, 64, 64, GIMP_CMYK, GIMP_PRECISION_U16_NON_LINEAR)` → layer format
  `CMYKA u16` with a CMYK space, projection `CMYKA u16`, CMYK profile attached, image clean.
* Every declaration of INTERFACES §1–§7 exists with gtk-doc.

**Risks**
* Callers of `gimp_image_get_layer_format()` assuming `layer_space == NULL` means sRGB.
* Golden determinism: if a combination is not reproducible run-to-run, drop it from G1 and
  list it in Status (do not loosen to tolerances).
* The I8 cache must touch CMYK profiles only (RGB unchanged).

**Status**: done (A1), ready to merge. Build warning-free in owned files; `meson test`:
26 OK, 4 SKIP (compositing/image/histogram skeletons, babl probe), 1 FAIL (`appdata_file`,
network = baseline).

* **G1–G3** `layer-modes-golden` green, 20/20 repeated runs. Determinism required
  `BABL_TOLERANCE=0`, `BABL_LUT=0`, `BABL_INHIBIT_CACHE=1` (set in the test's `main()`):
  babl's timing-based path/LUT choice changed the last bits of RGB-perceptual and LCh results
  between runs. No combination was dropped. Keys: `G1/<image>/<mode>/<composite space>`
  (all composite modes folded into one SHA-1), `G2/rgb-u16/<mode>`, `G3/rgb-u16/*`.
  G1–G3 stayed green after the enum change (mask `ALL` now includes BLACK).
* **F1–F8, F12** pass (`cmyk-foundation`), also with `GIMP_TESTING_CMYK_PRECISION=double` and
  `GIMP_TESTING_CMYK_ICC=default_cmyk.icc:FOGRA39L_coated.icc`. F9–F11 are skipped placeholders (01b).
  Stub builtin: 1428 bytes, SHA-256 `f900f7ab…16263` (01b updates the constant once).
* Tier A (#1, #2, #2b) on owned files: every base-type switch has a CMYK case; `[4]` hits are
  RGB-only (`gimp-babl.c` probe `cmyk[4]`, lcms sampler `cmyk[4]`, `diff_rects[4]`);
  `gimp-babl.c` `pixel[40]` OK (≥ 40 bytes); `gegl_buffer_sample` in
  `gimp_drawable/projection_get_pixel_at` write to caller buffers (callers: 08/07, PLAN §5).
* Deviations: INTERFACES §15 plug-in names (`file-tiff:file-jpeg:file-psd`, updated);
  "clean" for new images = no undo and no extra dirtiness (every new `GimpImage` starts at
  dirty 1); CMYK images keep the builtin profile as a parasite (RGB/GRAY still drop it);
  `libgimp/tests/meson.build` gets a small python-only block, not one line.


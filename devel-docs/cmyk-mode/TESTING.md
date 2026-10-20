# CMYK mode — test plan

Builds on `BUILD-ENV.md`: `source /root/gimp-env.sh`, build dir `/root/gimp/_build`,
`meson test -C _build --no-rebuild --print-errorlogs` (baseline 20/21; `appdata_file` needs
network — ignore).

## Test ICC profiles (decision)
* Repository tests use only **generated** profiles: the builtin
  `gimp_color_profile_new_cmyk_builtin()` and, for negative tests, a copy of it with the
  BToA tags deleted (`cmsWriteTag (h, cmsSigBToA0Tag, NULL)` … then `cmsSaveProfileToMem`) =
  "input-only CMYK profile". Nothing binary is committed.
* Optional real-profile coverage: env `GIMP_TESTING_CMYK_ICC` (colon-separated paths). Tests
  using it `g_test_skip()` when unset/missing. On this box use
  `/usr/share/color/icc/ghostscript/default_cmyk.icc` and
  `/usr/share/texlive/texmf-dist/tex/generic/colorprofiles/FOGRA39L_coated.icc`.
  (CGATS001Compat-v2-micro.icc is input-only and is a good real-world negative test.)

## Tier A — without (re)building GIMP

Every workstream runs these before asking for a merge:
1. **Switch audit**: `grep -rnE "switch \(.*(base_type|get_base_type|image_type)" app libgimp
   plug-ins/file-{tiff,jpeg,psd}` → every switch in owned files has a `GIMP_CMYK` (or
   `GIMP_CMYK(A)_IMAGE`) case or an intentional `default:`. Also
   `grep -rnE "GIMP_INDEXEDA?_IMAGE\b.*[<>]|> *GIMP_INDEXED|GIMP_INDEXED *\+"` for range checks.
2. **4-component assumptions** in owned files: `grep -rnE "\[4\]|\* 4\b|n_components *== *4|
   pixel\[(16|32)\]|\"RGBA float\"|\"R'G'B'A float\"|\"Y'?A? float\"" <owned dirs>` — each hit
   reviewed and noted in the WS status section (fixed / OK because RGB-only path).
3. **Generated files untouched by hand**: `git diff --stat` must not show hand edits to
   generated files (they change only in "regen" commits).
4. **babl behaviour probes** (no GIMP build needed; ctypes against /opt/gimp-deps libbabl):
   `python3 -I -u app/tests/cmyk-babl-probe.py` (added by 09; read-only). Asserts: CMYK(A)
   formats exist for u8/u16/u32/half/float/double; `babl_space_from_icc` on a CMYK profile is
   CMYK; CMYKA float → R'G'B'A float works with a bidirectional profile; RGBA tagged with CMYK
   space == linear sRGB; documents (expected) K100 round-trip loss. Never feeds an input-only
   profile to RGB→CMYK fishes (it segfaults babl).
5. **Isolated compile check**: `ninja -C _build libgimpcolor/libgimpcolor-3.0.so
   app/gegl/libappgegl.a` (fast targets) — permissible at any time since the environment builds.

## Tier B — full build and tests

`ninja -C _build && meson test -C _build --no-rebuild --print-errorlogs`. New/extended suites:

### B1. `app/tests/test-cmyk.c` (new, owner 09; registered in `app/tests/meson.build` app_tests)
Harness as `test-core.c` (`gimp_init_for_testing()`, `g_test_add_data_func("/gimp-cmyk/…")`).
Pixel comparisons read buffers with explicit formats (`"CMYKA u16"` + image space) and compare
exactly unless a tolerance is given.

| id | test | expectation |
|----|------|-------------|
| T1 | babl-formats | for each component type: `gimp_babl_format(CMYK, <t>_NON_LINEAR, a, space)` name `CMYK(A) <t>`, 4/5 comps, base type CMYK, precision/trc round-trip; `gimp_babl_is_valid` FALSE for LINEAR/PERCEPTUAL |
| T2 | babl-support | `gimp_babl_has_cmyk_support()` TRUE |
| T3 | builtin-profile | is_cmyk, bidirectional, two calls give byte-identical ICC; K100 → sRGB' ∈ [0.06,0.10]; (0,0,0,0) → white ±1/255 |
| T4 | bidirectional-check | input-only derivative → `is_cmyk_bidirectional` FALSE; `gimp_image_set_color_profile` on CMYK image fails with GError, image profile unchanged |
| T5 | new-image | `gimp_image_new(CMYK,U8_NON_LINEAR)`: profile non-NULL CMYK, `layer_space` CMYK, layer format `CMYKA u8`, projection `CMYKA u8`, image not dirty, undo stack empty |
| T6 | default-profile-order | with `cmyk-profile` pref set to a real profile (if available) the new image uses it; pref pointing to input-only profile → builtin + error set |
| T7 | rgb-to-cmyk | RGB u16 image with known patches → `convert_type_full(CMYK, P, RELCOL, TRUE)`; pixels equal a direct `GimpColorTransform`/lcms transform with same intent ±1 (u16 ±2); repeated with PERCEPTUAL using a real profile (skip if none) and checks results differ from RELCOL |
| T8 | convert-undo | after T7, undo → base type RGB, profile, precision and buffer bytes identical to original; redo identical to T7 result |
| T9 | cmyk-to-rgb / gray | CMYK→RGB (default sRGB) and CMYK→GRAY compare with lcms ±1 |
| T10 | linear-precision | RGB FLOAT_LINEAR → CMYK yields FLOAT_NON_LINEAR, single undo group |
| T11 | indexed | indexed → CMYK works (one undo step); `gimp_image_convert_indexed` on CMYK returns FALSE/error |
| T12 | k-preservation | CMYK u16 bg filled (0,0,0,1); layer 2 (Normal, 50%) covering left half with (1,0,0,0); projection right half bit-identical (0,0,0,65535); left half = exact per-channel lerp (±1) |
| T13 | blend-mode-roundtrip | layer 2 in Multiply: covered pixels ≈ CMYK(lcms(multiply(rgb(a),rgb(b)))) within 2/255; uncovered pixels bit-identical |
| T14 | fill-native | `gimp_drawable_edit_fill` with FG GeglColor set from `CMYK float` (0.1,0.2,0.3,0.4) → u16 pixels exactly (6554,13107,19661,26214) ±1 |
| T15 | component-active | CYAN inactive, fill with (1,1,1,1) → cyan unchanged, others 1.0 |
| T16 | component-visible | hide BLACK → projection K == 0, other components unchanged |
| T17 | channel-from-component | `gimp_channel_new_from_component(CYAN)` values == cyan |
| T18 | filter-mask | apply `gegl:invert-gamma` via GimpDrawableFilter with a selection on half → unselected half bit-identical |
| T19 | flatten | 2 layers with alpha, flatten → no alpha, CMYK, transparent areas == background color converted to CMYK |
| T20 | precision | CMYK u8 → float → u16 → u8 values preserved (±1) |
| T21 | histogram | CMYK drawable histogram has channels C,M,Y,K,A; counts match patch areas |
| T22 | template | template base-type CMYK + profile file → new image with that profile; template with RGB profile → default CMYK profile + warning |
| T23 | xcf-version | `gimp_image_get_xcf_version` of CMYK image ≥ 28; RGB image unchanged |
| T24 | no-lcms-guard | (unit-level) when `gimp_babl_has_cmyk_support()` is forced FALSE via a test hook env `GIMP_TESTING_NO_CMYK=1`, `gimp_image_new(CMYK)` returns NULL with warning |

### B2. `app/tests/test-xcf.c` (extended, owner 04)
Extend `gimp_create_mainimage()` / `gimp_write_and_read_file()` with a CMYK variant:
CMYK u8 and u16 and float, layers with/without alpha, layer mask, group layer with non-Normal
mode, channel, selection, custom CMYK profile (builtin), simulation profile; write → read →
assert base type, precision, profile bytes equal, every layer buffer bit-identical, layer
modes/opacity, and saved XCF version 28. Also: RGB main image still saves with its old version.

### B3. End-to-end headless test `app/tests/test-cmyk-e2e.py` (new, owner 09)
Declared in `app/tests/meson.build` `tests` list with `GIMP_TESTING_PLUG_INS` appended
`file-tiff/file-tiff:file-jpeg/file-jpeg:file-psd/file-psd`. Runs inside in-build GIMP via
`libgimp/tests/libgimp-run-python-test.py` (template: `test-save-and-export.py`). Steps:
1. `img = Gimp.Image.new_with_precision(64, 64, Gimp.ImageBaseType.CMYK,
   Gimp.Precision.U16_NON_LINEAR)`; assert profile is CMYK
   (`img.get_color_profile().is_cmyk()`); if `GIMP_TESTING_CMYK_ICC` set, set that profile.
2. `layer = Gimp.Layer.new(img, "bg", 64, 64, Gimp.ImageType.CMYKA_IMAGE, 100, NORMAL)`; insert;
   fill via `layer.get_buffer().set(rect, babl CMYKA u16 with image space, bytes)` with patches
   (K-only, C-only, 4-color rich black, 50% tints).
3. Set FG from a CMYK GeglColor, `Gimp.paintbrush_default(layer, strokes)`; a second layer in
   Multiply over a quarter.
4. Save XCF to tmp, `Gimp.file_load` it, compare every layer buffer bytes (exact).
5. Export TIFF (flattened copy): reload; assert base type CMYK, same profile bytes, pixels equal
   the flattened original; with `tiffinfo` (if available) assert Photometric = separated and ICC
   present.
6. Export PSD, reload, exact compare per layer. Export JPEG q=100, reload, base type CMYK, max
   abs diff ≤ 3/255 per channel; Adobe inversion handled (K-only patch stays K-only).
7. Color round trip check: duplicate, `Gimp.Image.convert_rgb` (→ sRGB), read patches as
   `R'G'B'A double`; compute expected with lcms through ctypes inside the script
   (`cmsCreateTransform(cmyk_profile, TYPE_CMYK_16, sRGB, TYPE_RGB_DBL, PERCEPTUAL, BPC)`) and
   assert |diff| ≤ 1/255.
8. `Gimp.Image.convert_cmyk(rgb_dup, None, RELATIVE_COLORIMETRIC, True)` back; assert base type.
9. Negative: loading a TIFF whose embedded profile is input-only (generated in the script by
   exporting then patching? simpler: skip if no such file in `GIMP_TESTING_CMYK_ICC`) must not
   crash and yields an RGB image.
Manual run: use the env line from BUILD-ENV.md with this script path.

### B4. libgimp API test (owner 09, optional pair `libgimp/tests/test-cmyk.{py,c}`)
Asserts enum values (`Gimp.ImageBaseType.CMYK == 3`, image types 6/7, channel types 6–9),
`gimp_drawable_is_cmyk`, `gimp_image_convert_cmyk` argument validation, export options
conversion of a CMYK image for an RGB-only capability set.

### B5. Manual checklist (UI, run once before final merge; Xvfb screenshots acceptable)
New Image dialog with CMYK + profile combo; Image ▸ Mode ▸ CMYK dialog (intent/BPC); Channels
dialog shows C/M/Y/K/Alpha with eye/affect working; pointer/colour picker shows CMYK numbers;
histogram dialog channels; paint/erase/smudge/heal/clone on CMYK; Filters ▸ Blur on selection;
soft-proof toggle on a CMYK image; title bar shows "CMYK color"; plug-ins with "RGB*" insensitive.

## Acceptance for the whole feature
All Tier B suites pass (except network appdata), T12/T13/T18 invariants hold, e2e B3 passes with
builtin profile and with `default_cmyk.icc` and `FOGRA39L_coated.icc`.

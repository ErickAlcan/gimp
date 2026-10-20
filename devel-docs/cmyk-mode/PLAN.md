# Native CMYK image mode — architecture plan (MVP)

Status: design, branch `cmyk-mode` (GIMP 3.3.1 dev, baseline 5a569ea).
Companion docs: `INTERFACES.md` (API contracts), `TESTING.md` (two-tier test plan),
`WORKSTREAMS/*.md` (per-agent work orders), `BUILD-ENV.md` (build/test environment).

## 0. Goal and scope

Add a fourth `GimpImageBaseType`, `GIMP_CMYK`. A user can:
create a CMYK image (always with a CMYK ICC profile), convert RGB/Gray(/Indexed) ⇄ CMYK through
ICC profiles with a chosen rendering intent + BPC, open CMYK TIFF/JPEG/PSD without converting to
RGB, paint, composite layers, run GEGL filters (via RGB round-trip where no native path exists),
save/load XCF, and export CMYK natively (TIFF, JPEG, PSD).

Out of scope: GCR/UCR controls, spot/multichannel, CMYK-native blend-mode math for non-trivial
modes, CMYK indexed palettes, plug-in filters (C/Python) operating on CMYK (they stay insensitive).

Guiding rule: **correct color management over performance**, and **never touch ink values a
user did not ask to change** (no implicit CMYK→RGB→CMYK round trip on untouched pixels).

## 1. Facts established from the code (and babl runtime probes)

Verified by reading `/root/gimp` and babl master sources, and by calling the installed
`/opt/gimp-deps` libbabl (0.1.129, linked against lcms2) through ctypes:

* `GimpImageBaseType` (libgimpbase/gimpbaseenums.h) = RGB/GRAY/INDEXED; `GimpImageType` 6 values;
  `GimpChannelType` RED..ALPHA (0..5); `GimpHistogramChannel` VALUE..RGB (0..6, in
  app/core/core-enums.h, exported to libgimp through pdb/enums.pl); `GimpComponentMask` 4 bits
  (app/core/core-enums.h); `MAX_CHANNELS == 5` (app/core/core-types.h), enough for C,M,Y,K,A.
* The image's profile lives in `GimpImagePrivate::color_profile` (from the "icc-profile" parasite,
  `_gimp_image_update_color_profile()`), its babl space in `private->layer_space`
  (`gimp_color_profile_get_space()`). NULL profile = builtin (sRGB / D65 gray), NULL space = sRGB.
  Formats are derived by `gimp_image_get_format()` → `gimp_babl_format(base, precision, alpha,
  space)`; `gimp_image_get_layer_format()` uses the image's base type/precision/layer_space;
  projection format `gimp_image_get_proj_format()` switches on base type.
* Layer drawables are converted by `gimp_layer_real_convert_type()` with
  `gimp_gegl_convert_color_profile(..., GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL, TRUE, ...)`
  (intent hard-coded). `gimp_drawable_set_format()` relabels (no conversion): assign-profile is
  safe for CMYK.
* `GimpColorTransform` uses a babl fish by default (ignores intent); lcms only if
  `GIMP_COLOR_TRANSFORM_DISABLE_BABL`. `gimp_color_profile_get_lcms_format()` recognises "CMYK"
  but not "CMYKA" ("FIXME missing from babl" – babl has it now), and maps float CMYK to
  `TYPE_CMYK_FLT` although lcms float CMYK is 0..100 (babl itself rescales ×100 around lcms).
  `gimp_color_transform_can_gegl_copy()` returns TRUE for any two profiles babl can make spaces
  for — including CMYK ones (wrong: it would bypass intent).
* babl CMYK: models `CMYK`, `CMYKA`, `CaMaYaKaA` (1.0 = full ink) and inverted `cmyk`, `cmykA`,
  `camayakaA` (0.0 = full ink, Adobe JPEG/PSD convention). Model flag `BABL_MODEL_FLAG_CMYK`,
  `babl_space_is_cmyk()`. Existing formats: `CMYK(A) u8/u16/u32/half/float/double`,
  `CaMaYaKaA u8/u16/half/float` (no `CaMaYaKaA u32`).
* `babl_space_from_icc()` on a CMYK profile (needs babl built with lcms) creates a space whose
  RGB part is a copy of sRGB, and two lcms transforms CMYKA_DBL⇄scRGB (linear sRGB) hard-coded
  **relative colorimetric + BPC**; CMYK⇄CMYK across spaces also relcol+BPC; all through the slow
  reference fish (doubles, per call). Probe results with `ghostscript/default_cmyk.icc`:
  K100 (0,0,0,1) → sRGB' (0.137,0.122,0.125) → back to CMYK (0.680,0.674,0.653,0.742):
  **RGB round trips destroy K-only separations.** `RGBA float` tagged with the CMYK space ==
  linear sRGB (identity).
* **babl crashes** (NULL lcms transform, no NULL check in babl-fish-reference.c:901) when a CMYK
  profile lacks a PCS→device table (e.g. CGATS001Compat-v2-micro.icc: input-only). GIMP must
  reject such profiles as image/conversion-target profiles.
* Layer modes: `gimp_layer_mode_get_format()` returns RGBA/Lab float formats tagged with the
  preferred format's space; `GimpOperationLayerMode` already separates composite space and blend
  space with cached fishes, and composite functions take `n_components` — the plumbing for a
  5-component composite / 4-component blend split exists. `gimp:normal` C path is n-generic; the
  SSE2/SSE4 variants replace `process` class-wide and assume 4 components.
* `gimp:mask-components` (component visibility in the image graph and "affect" when painting /
  filtering) converts to RGBA u8..float — must learn CMYKA.
* XCF: header stores base type as int32 (load rejects `> GIMP_INDEXED`), layers store
  `GimpImageType`; tile code is generic in bpp/n_components. Highest XCF version today is 27
  (28 loaders, `xcf.c`); version reasons/mapping live in `gimp_image_get_xcf_version()` in
  `app/core/gimpimage.c`.
* Plug-in sensitivity: `app/plug-in/gimppluginprocedure.c` parses "RGB*, GRAY*, INDEXED*, *";
  `GimpPlugInImageType` 6 bits. Export conversions: `libgimp/gimpexportoptions.c` switch on base
  type with `GIMP_EXPORT_CAN_HANDLE_*`.
* `GimpColorConfig` already has a `cmyk-profile` preference (used today for RGB→CMYK in export).
* PDB: `pdb/groups/*.pdb` → `app/pdb/*-cmds.c`, `libgimp/*_pdb.[ch]`; enums → `pdb/enums.pl`,
  `libgimp/gimpenums.h`; `libgimpbase/gimpbaseenums.c`, `app/core/core-enums.c` by mkenums. All
  generated files are committed and regenerated by `ninja -C _build`.

## 2. Architecture decisions

### D1. Enums (see INTERFACES.md §1 for exact values)
All new members are **appended** (no renumbering; XCF/PDB/GIR stable):
`GIMP_CMYK = 3`; `GIMP_CMYK_IMAGE = 6`, `GIMP_CMYKA_IMAGE = 7`;
`GIMP_CHANNEL_CYAN = 6 … GIMP_CHANNEL_BLACK = 9` (alpha keeps `GIMP_CHANNEL_ALPHA`);
`GIMP_HISTOGRAM_CYAN = 7 … GIMP_HISTOGRAM_BLACK = 10`;
`GIMP_COMPONENT_MASK_BLACK = 1 << 4` (+ CYAN/MAGENTA/YELLOW aliases of RED/GREEN/BLUE);
`GIMP_EXPORT_CAN_HANDLE_CMYK = 1 << 11`; plug-in image types `CMYK`, `CMYKA`, `CMYK*`.
Component indices in core-types.h: CYAN 0, MAGENTA 1, YELLOW 2, BLACK 3, ALPHA_C 4.

### D2. The CMYK profile and babl space
* Invariant **I1**: a CMYK image *always* has a non-NULL, explicit (parasite-backed) color
  profile which is CMYK and "bidirectional" (lcms can build device→PCS and PCS→device
  relative-colorimetric transforms — `gimp_color_profile_is_cmyk_bidirectional()`), and a
  non-NULL CMYK `layer_space` derived from it. Never "builtin by NULL" for CMYK, because libgimp
  derives drawable formats from `gimp_image_get_color_profile()` (non-effective).
* Attachment points: `gimp_image_constructed()` attaches the default CMYK profile (no undo) when
  `base_type == GIMP_CMYK`; `gimp_image_convert_type*()` sets the destination profile right after
  switching the base type; `gimp_image_set_icc_profile(NULL)` on a CMYK image re-attaches the
  default instead of clearing. Validation (`gimp_image_validate_color_profile_by_format()`) rejects
  non-CMYK and non-bidirectional profiles for CMYK formats.
* "Use sRGB"/hidden-profile machinery is not applicable to CMYK (actions insensitive).
* Convention **I5**: any RGB/Y format tagged with a CMYK babl space means sRGB / linear sRGB
  (that is what babl implements). `gimp_babl_format_get_color_profile()` returns builtin
  sRGB-family profiles for such formats, never the CMYK ICC.

### D3. Default CMYK profile
No standard default exists and no freely redistributable press profile is in the tree.
Decision: **generate** a deterministic, public-domain LUT profile in libgimpcolor —
`gimp_color_profile_new_cmyk_builtin()` "GIMP built-in CMYK (generic, not for press)":
v4 output (prtr) CMYK/Lab profile, AToB0 4→3 CLUT (17⁴) sampling
R'=(1−C)(1−K), G'=(1−M)(1−K), B'=(1−Y)(1−K) (sRGB-encoded), scaled toward a black point
bp=0.08 (`X' = bp + (1−bp)·X`), then sRGB→Lab D50; BToA0 3→4 CLUT (33³) inverting that with
full GCR (K = min(C',M',Y')). Copies as AToB1/2, BToA1/2. Deterministic → hashable in tests.
The K100 → sRGB' ≈ 0.08 property also serves as the runtime lcms probe (D11).
Resolution order (`gimp_get_default_cmyk_profile()`):
1. explicit profile passed by the caller (dialog choice, PDB argument, template profile, or for
   RGB→CMYK conversion the image's simulation profile when it is CMYK);
2. `GimpColorConfig::cmyk-profile` preference, if set, loadable and bidirectional;
3. the builtin above.
Unusable candidates are skipped with a `g_message`/GError, never used.
(Option kept open: vendoring a real profile, e.g. FOGRA39 under ECI terms, once licensing is
confirmed — see risks. Ghostscript's default_cmyk.icc is AGPL: do not vendor.)

### D4. Pixel formats (`gimp_babl_format()` for `GIMP_CMYK`)
| precision component | no alpha | alpha |
|---|---|---|
| U8  | `CMYK u8`     | `CMYKA u8` |
| U16 | `CMYK u16`    | `CMYKA u16` |
| U32 | `CMYK u32`    | `CMYKA u32` |
| HALF| `CMYK half`   | `CMYKA half` |
| FLOAT | `CMYK float` | `CMYKA float` |
| DOUBLE| `CMYK double`| `CMYKA double` |
always `babl_format_with_space(name, space)`. Only `*_NON_LINEAR` precisions are valid for CMYK
(`gimp_babl_is_valid()`); `gimp_babl_format()` tolerates other TRCs by ignoring them (returns the
same CMYK format) to avoid crashes, `gimp_babl_format_get_trc()` of CMYK formats is
`GIMP_TRC_NON_LINEAR`, so precision round-trips are consistent. Inverted (`cmyk*`) and
premultiplied (`CaMaYaKaA`) formats are recognised as base type CMYK but never used for
drawables. `gimp_babl_init()` registers defensively (only if `!babl_format_exists()`): all of the
above, `CaMaYaKaA u32/double`, and single-component formats
`"Cyan <t>"`, `"Magenta <t>"`, `"Yellow <t>"`, `"Key <t>"`, `"CMYK-A <t>"` (model CMYKA) for
`gimp_babl_component_format(GIMP_CMYK, …)`.

### D5. Alpha
CMYK layers may have alpha (`CMYKA`, straight/non-premultiplied like RGBA layers). Background
layer without alpha allowed. Masks, channels, selection stay `Y` formats (unchanged).
Flatten/remove-alpha composite against the background color converted to CMYK.

### D6. Compositing / layer modes
* `gimp_layer_mode_get_format(mode, …, preferred)` returns
  `babl_format_with_space("CMYKA float", preferred)` whenever `preferred` has a CMYK model,
  for **all** modes. The paint buffer, fill buffers and mode nodes therefore carry CMYKA float.
* `GimpOperationLayerMode` composites in CMYKA float (5 components) when its input **or** aux
  source format is CMYK (so flatten-on-background and filter application also stay CMYK).
  Composite = existing union/clip/intersection formulas applied per ink channel (Photoshop
  semantics). Trivial modes (Normal, Replace, Erase, Anti-erase, Behind, Dissolve, Merge, Split,
  Overwrite, Pass-through) are therefore **native and lossless**.
* Non-trivial modes (Multiply, Screen, Overlay, …, LCh/HSV modes): blend in the mode's RGB/Lab
  blend space (backdrop and layer converted CMYKA→RGBA via the space fish, i.e. relcol through
  linear sRGB), convert the blend result back RGBA→CMYKA, then composite natively in CMYK. Pixels
  with zero layer coverage are never written with converted values (invariant **I6**).
* An explicit user-chosen composite space (RGB linear/perceptual/non-linear on a layer) is
  honoured as today (lossy, user's choice); AUTO resolves to CMYK in CMYK images.
* Candidates for later native CMYK blend math (documented, not in MVP): Multiply (ink "add":
  1−(1−a)(1−b) on inks ≙ RGB multiply on transmittance), Screen, Darken only/Lighten only (swap),
  Addition/Subtract, Difference, Grain extract/merge, Linear burn/dodge. Not sensible natively:
  HSV/LCh modes, Color erase, Overlay family (keep RGB).
* No new `GimpLayerColorSpace` value is added (no XCF/PDB impact); CMYK compositing is an
  internal property of the operation instance.
* Projection format (`gimp_image_get_proj_format`, group layers): `CMYKA <precision>` in the
  image space. Display converts projection → monitor with lcms (intent from display prefs),
  proofing via a simulation profile stays possible (CMYK → proof CMYK → display).

### D7. Conversions
* `gimp_image_convert_type_full(image, type, profile, intent, bpc, …)`; the old
  `gimp_image_convert_type()` == `_full(…, PERCEPTUAL, TRUE)`.
* RGB/GRAY → CMYK: dest profile = argument, else `gimp_get_default_cmyk_profile()` with the
  image's simulation profile as preferred. Precision is moved to the `*_NON_LINEAR` variant of the
  same component type in the same undo group, and drawables are converted directly
  (one transform) with lcms, using the requested intent+BPC.
* CMYK → RGB: dest = argument, else preferred RGB profile (`cmyk`→`rgb-profile` pref), else sRGB.
  CMYK → GRAY: same with gray profile. Precision TRC kept non-linear.
* CMYK → CMYK (different profile): `gimp_image_convert_color_profile()` with intent+BPC (lcms);
  "Assign profile" relabels.
* Indexed → CMYK: two steps inside one undo group (indexed→RGB with the current profile, then
  RGB→CMYK). CMYK → Indexed: refused (actions insensitive, PDB error "convert to RGB first").
* Intent transport to drawables: image-private conversion intent/bpc set by convert_type_full
  and read by `gimp_layer_real_convert_type()` (default PERCEPTUAL+BPC = today's behaviour) — no
  vfunc signature churn.
* `GimpColorTransform` uses the lcms path whenever either profile is CMYK (honours intent/BPC);
  CMYK lcms I/O uses 8/16-bit lcms types (float/half/double CMYK are routed through
  `CMYK(A) u16` to avoid lcms' 0..100 float convention). `can_gegl_copy()` returns FALSE if either
  profile is CMYK and the profiles differ.

### D8. Channels dialog, components, histogram
* Channels dialog (GimpComponentEditor) lists Cyan, Magenta, Yellow, Black, Alpha for CMYK
  images; visibility (eye) and active (affect) work through `GimpComponentMask` with the new
  BLACK bit. Hiding a component in the view sets it to 0 ink (shows the plate without that ink).
* "Channel to selection"/"new channel from component" use
  `gimp_image_get_component_format()` (space-tagged single-component formats).
* Histogram: CMYK drawables are histogrammed natively on `CMYKA float`; channels VALUE (=max ink
  of C,M,Y,K), CYAN, MAGENTA, YELLOW, BLACK, ALPHA; LUMINANCE/RED/GREEN/BLUE/RGB unavailable.
* Color tools (Levels, Curves, Threshold, …) are GEGL filters working in RGB → for CMYK
  drawables they act through the RGB round trip (MVP), their internal histograms are computed in
  RGB (so what you see matches what the op does), and the tool shows an info line
  "Operates on RGB conversion of CMYK data". Native CMYK curves/levels: later.
* Color pickers/frames report CMYK values for CMYK pixels (GeglColor keeps the native format).

### D9. Undo
No new undo types. Convert = `IMAGE_CONVERT` group with: image-type undo, image-precision undo
(if TRC changed), parasite undo (profile), per-drawable buffer undos. `gimpimageundo.c` emits
profile-changed on any base-type change involving GRAY **or CMYK**. Undo must restore the
original bytes exactly (tested).

### D10. Templates and new-image dialog
`GimpTemplate` base-type may be CMYK; its `color-profile` (GFile) must then be a CMYK profile
(else ignored with a warning → default). Setting base type CMYK forces non-linear TRC on the
template. The New Image dialog offers "CMYK color" in the color-space combo; the profile combo is
filtered to CMYK profiles and shows the builtin/default entry; TRC widgets hidden for CMYK.
Simulation profile settings remain available. No new built-in templates in MVP.

### D11. babl requirements
GIMP's existing minimum (`babl_minver = '0.1.118'`) is sufficient for the API
(`babl_space_is_cmyk`, `babl_get_model_flags`, CMYK formats exist since 0.1.62). Functional
requirement: **babl built with lcms2** (option `with-lcms`, auto). There is no API to query it,
so `gimp_babl_has_cmyk_support()` probes once (builtin profile K100 must map to sRGB' ≈ 0.08,
naive babl gives 0.0). If unsupported: CMYK actions insensitive, PDB returns an error, loaders
fall back to today's RGB conversion, a startup warning is printed. Do not raise babl_minver.

### D12. File formats
* XCF version **28** ("GIMP 3.4"): image base type 3 and layer types 6/7; profile in the usual
  parasite. Old GIMP refuses v28 (correct). Images without CMYK keep their current version.
* TIFF (PHOTOMETRIC_SEPARATED), JPEG (Adobe inverted CMYK, JCS_CMYK/JCS_YCCK), PSD (CMYK mode)
  load natively as CMYK when the embedded profile is CMYK and bidirectional, or when there is no
  profile (default profile assigned, raw values kept, message logged). One-way CMYK profiles:
  fall back to today's RGB conversion path with a warning. Native export when the image is CMYK;
  existing "export as CMYK" option for RGB images is kept. Loaders must set the image profile
  **before** creating layers / writing pixels, and write with `gimp_drawable_get_format()`.

### D13. Plug-ins and filters
GEGL filters (app-side, Filters menu / GimpDrawableFilter) work on CMYK through babl (RGB round
trip of the filtered region only; masked-out pixels untouched thanks to CMYK composite).
C/Python plug-ins opt in with "CMYK*"/"CMYK"/"CMYKA"; "*" does **not** include CMYK in MVP
(prevents plug-ins that switch on base type from receiving unexpected CMYK). Export uses
`GIMP_EXPORT_CAN_HANDLE_CMYK`; exporters without it get an automatic "Convert to RGB" action.

## 3. Workstreams

| # | Name | Depends on | Can start | Summary |
|---|------|-----------|-----------|---------|
| 01 | foundation | – | now | enums, gimp-babl, libgimpcolor (builtin profile, lcms fixes), gimpimage core, color-profile core, gimpdrawable, stubs |
| 02 | compositing-paint | 01 | after 01 | layer modes CMYK composite, mask-components, applicator/filters/fills, group layers, flatten, app/paint |
| 03 | core-image | 01 | after 01 | convert_type(_full), undo, new image, templates, previews, buffers, duplicate, file-open/import |
| 04 | xcf | 01 | after 01 | XCF v28 load/save + test-xcf.c CMYK cases |
| 05 | pdb-libgimp | 01 | after 01 | PDB procs, generated wrappers, libgimp API, export options, plug-in image types |
| 06 | file-plugins | 01 (+05 for export options) | after 01 | TIFF/JPEG/PSD native CMYK load/export |
| 07 | ui | 01 (+03 at runtime) | after 01 | actions, menus, dialogs (new image, convert, precision, import policy), display, misc widgets |
| 08 | channels-histogram-colortools | 01 (+02 for mask op) | after 01 | component editor, histogram core+UI, color tools, color selectors/frames |
| 09 | tests | 01 | after 01 | new app/tests/test-cmyk.c, e2e python test, meson registrations, fixtures (generated) |

### File ownership (no file has two owners)
* **01**: `libgimpbase/gimpbaseenums.h`, `libgimpcolor/gimpcolorprofile.{c,h}`,
  `libgimpcolor/gimpcolortransform.c`, `libgimpcolor/gimpcolor.def`, `app/core/core-enums.h`,
  `app/core/core-types.h`, `app/gegl/gimp-babl.{c,h}`, `app/gegl/gimp-babl-compat.{c,h}`,
  `app/core/gimpimage.{c,h}`, `app/core/gimpimage-private.h`,
  `app/core/gimpimage-color-profile.{c,h}`, `app/core/gimpdrawable.{c,h}`,
  `app/core/gimpimage-convert-type.h` (+ a forwarding stub in `.c`, handed to 03),
  `app/sanity.c` (only if a startup check is added there). Regenerated outputs it commits:
  `libgimpbase/gimpbaseenums.c`, `app/core/core-enums.c`, `pdb/enums.pl`, `libgimp/gimpenums.h`,
  `libgimp/gimpenums.c.tail`.
* **02**: `app/operations/layer-modes/*`, `app/operations/gimpoperationmaskcomponents.{cc,h}`,
  all of `app/gegl/*` except gimp-babl*, `app/core/gimpdrawablefilter.c`,
  `app/core/gimpfilloptions.c`, `app/core/gimpgrouplayer.c`, `app/core/gimpimage-merge.c`,
  `app/core/gimpdrawable-{fill,gradient,bucket-fill,edit,operation,filters,combine,transform}.c`
  (whichever exist; all `gimpdrawable-*.c` except `-histogram` and `-preview`),
  `app/paint/*`.
* **03**: `app/core/gimpimage-convert-type.c` (replaces 01's stub), `gimpimage-convert-indexed.c`,
  `gimpimage-convert-precision.c`, `gimpimageundo.c`, `gimpimage-new.{c,h}`,
  `gimptemplate.{c,h}`, `gimpimage-duplicate.c`, `gimpimage-metadata.c`, `gimpimage-colormap.c`,
  `gimpimage-preview.c`, `gimpdrawable-preview.c`, `gimplayer.c`, `gimpchannel.c`,
  `gimpselection.c`, `gimplink.c`, `gimpbuffer.c`, `gimppalette.c`, `gimppalette-import.c`,
  `gimp-utils.c`, `app/file/*`, `app/file-data/*`.
* **04**: `app/xcf/*`, `app/tests/test-xcf.c`.
* **05**: `pdb/groups/*.pdb`, `app/pdb/*` (incl. `gimppdb-utils.c`, generated `*-cmds.c`,
  `internal-procs.*`), `libgimp/*` hand-written sources touched (`gimpexportoptions.c`,
  `gimplayer.c`, `gimpimage.c`, `gimpimagecolorprofile.{c,h}`, `gimpdrawable.c`), generated
  `libgimp/*_pdb.{c,h}`, `libgimp/gimp.def`, `app/plug-in/*` (`gimppluginprocedure.c`,
  `plug-in-enums.{h,c}`).
* **06**: `plug-ins/file-tiff/*`, `plug-ins/file-jpeg/*`, `plug-ins/file-psd/*`.
* **07**: `app/actions/*` except `channels-*`, `app/dialogs/*`, `app/display/*`, `menus/*`,
  `app/widgets/*` except the 08 list, `app/widgets/gimphelp-ids.h`, `app/config/*` (if a pref
  label/tooltip changes), `app/propgui/*`.
* **08**: `app/core/gimphistogram.{c,h}`, `app/core/gimpdrawable-histogram.{c,h}`,
  `app/widgets/gimpcomponenteditor.c`, `gimphistogrameditor.c`, `gimphistogramview.c`,
  `gimphistogrambox.c`, `gimpcolordialog.c`, `gimpcolorframe.c`, `gimpcolorhistory.c`,
  `gimpfgbgeditor.c`, `app/actions/channels-actions.c`, `channels-commands.c`, `app/tools/*`,
  `modules/color-selector-cmyk.c`, `libgimpwidgets/*` (only if needed).
* **09**: `app/tests/*` except `test-xcf.c`, `libgimp/tests/*`.
* **Docs**: `devel-docs/cmyk-mode/*` — lead/coordinator only; workstreams append notes to their
  own `WORKSTREAMS/NN-*.md` "Status" section only.

Unavoidable shared files and how they are handled:
1. `app/core/gimpimage-convert-type.c`: 01 lands a stub first, then 03 owns it (sequential).
2. Generated files (enum `.c`, `pdb/enums.pl`, `libgimp/gimpenums.h`, `gimpenums.c.tail`,
   `app/pdb/*-cmds.c`, `libgimp/*_pdb.*`): never hand-edited, never hand-merged. On conflict, take
   either side, run `ninja -C _build`, commit the regenerated result as a separate "regen" commit.
3. `libgimp/gimp.def` (05) vs `libgimpcolor/gimpcolor.def` (01): different files, no overlap.
4. Anything not listed: ask the coordinator before editing; default owner is 03 for `app/core/*`,
   07 for `app/widgets/*`.

## 4. Dependency order and merge plan

1. **01-foundation** first, small and fast: compiles, baseline tests stay green, a CMYK image can
   be created in core (`gimp_image_new(GIMP_CMYK, U8_NON_LINEAR)`) with correct layer/proj formats
   and profile, and the stubs/declarations of INTERFACES.md exist. Merge to `cmyk-mode`, tag
   `cmyk-foundation`.
2. All other workstreams branch from `cmyk-foundation` into their own worktree branch
   `cmyk/NN-name`, rebase onto `cmyk-mode` before merging, `git merge --no-ff`.
3. Merge order (each must build + pass Tier B suites at merge time):
   **01 → 03 → 02 → 04 → 05 → 08 → 07 → 06 → 09**.
   Rationale: 03 (conversion) and 02 (compositing) are runtime prerequisites for UI/PDB/file
   behaviour; 05 before 06 because exporters need `GimpExportOptions` CMYK handling; 09 last
   because the e2e test exercises everything (its unit tests can merge earlier as `TODO`-skipped
   cases if useful).
4. After each merge: `ninja -C _build` (regenerates), commit regen diffs, `meson test -C _build
   --no-rebuild --print-errorlogs` (baseline 20/21; appdata test needs network).
5. Final: full TESTING.md Tier B + manual checklist, then squash nothing (keep history).

## 5. Risks and open questions

* R1 babl segfault with one-way CMYK profiles → bidirectionality check everywhere a CMYK space is
  made for an image or a conversion target (01), loaders (06). Also a latent bug today (JPEG/TIFF
  CMYK export with such a simulation profile).
* R2 Performance: babl CMYK⇄RGB uses the reference fish (doubles, per call). Display uses lcms.
  Filters/blend modes on large CMYK images will be slow. Later: GIMP-side lcms-backed
  conversion helpers or babl fast paths.
* R3 Intent mismatch: babl conversions are always relcol+BPC (compositing, filters, color
  conversions of GeglColor); only explicit conversions honour the user's intent.
* R4 lcms float CMYK 0..100 convention — handled by routing through u16 (verify with test).
* R5 Layer-mode op: separate strides for 5-component composite vs 4-component blend buffers;
  SSE paths must fall back for n_components != 4.
* R6 GEGL ops/iterators with 5-component formats: GeglBuffer is generic, but any app code that
  assumes ≤4 components (alloca sizes, `pixel[16]` buffers) must be audited (grep in Tier A).
* R7 Plug-ins writing pixels before setting the image profile get CMYK→CMYK conversion.
* R8 Licensing for a vendored real profile (FOGRA39/ECI) unknown — builtin generated profile is
  the MVP default; real profiles come from the user's preference.
* R9 lcms intent fallback for LUT profiles that only define AToB0/BToA0 (builtin writes all
  intents to be safe).
* Open: whether "*" should include CMYK once plug-ins are audited (post-MVP).

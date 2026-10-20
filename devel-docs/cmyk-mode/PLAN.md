# Native CMYK image mode — architecture plan (MVP), revision 2

Status: design rev 2, branch `cmyk-mode` (GIMP 3.3.1 dev, baseline 5a569ea).
Companion docs: `INTERFACES.md` (API contracts — binding), `TESTING.md` (test plan),
`WORKSTREAMS/*.md` (per-agent work orders), `BUILD-ENV.md` (environment), `REVIEW-01.md`.
Prototype/probe programs: `/root/review-scratch/` (reviewer's `*.c`, plus rev-2
`mkcmyk-rev2.c`, `babl-roundtrip.c`, `lcms-perf.c`, `lcms-rt-error.c`).

## Revision 2 — changes in response to REVIEW-01

| Item | What changed |
|---|---|
| B1 | New invariant I8: one babl space per `gimp_color_profile_is_equal()` CMYK profile, enforced inside `gimp_color_profile_get_space()` (all processes); `GimpColorTransform` uses a pure babl re-encoding for equal CMYK profiles and lcms otherwise; builtin profile has a fixed header date and zero ID (INTERFACES §3.1–3.4); tests F8, F9, I9, I10. |
| B2 | Paint core uses `gimp_layer_mode_get_operation_for_format()`: one shared op per (mode, CMYK format) with explicit pad formats; RGB keeps today's instance (INTERFACES §8.2). |
| B3 | Rule: CMYK composite unless the layer's *stored* composite space is explicit; carried as op property `cmyk-native` via `gimp_gegl_mode_node_set_mode_full()`; `gimp_gegl_create_flatten_node()` gains a `format` argument (INTERFACES §8.2–8.3). |
| M1 | D6 rewritten: blend functions fixed at 4 components with a blend format accessor, two-stride `real_process`, CMYK branch in `cache_fishes`, legacy modes excluded (I9), colour-agnostic set defined by `blend_function == NULL` (INTERFACES §8.1, §8.4). |
| M2 | lcms-backed CMYK⇄RGB converter in the op (u16 lcms device links, measured ~8–10× faster than babl's reference fish); performance acceptance P1/P2 in WS02 (INTERFACES §8.5). |
| M3 | Full libgimpcolor fix list in WS01b (u32, CMYKA, inverted/premultiplied models, transform table, can_gegl_copy); K-preserving intents deferred (INTERFACES §3.3, §3.5). |
| M4 | Builtin spec 9⁴/17³ Lab LUT with identity curves, linked intents, near-black GCR fade, fixed header, 79.5 KB (validated); stub in 01a, final in 01b; serialized colors keep embedding ICC (named reference deferred). |
| M5 | Stack-buffer site table (§5) with owners; rule `gdouble pixel[MAX_CHANNELS]`; Tier A grep #2b; DOUBLE-precision test run. |
| M6 | Complete, disjoint ownership table (§4) with explicit default owners, function-level split of `gimplayer.c`, sequential handovers for `gimpimage.c`/`gimpimage-convert-type.c`/libgimpcolor, generated-file list. |
| M7 | New INTERFACES §10: CMYK previews are sRGB `R'G'B'(A) u8`; renderer pairs sRGB for CMYK viewables; PDB thumbnails stay RGB. |
| M8 | Legacy modes unavailable in CMYK images (I9): mapped on conversion, set_mode, paint, XCF load; safety-net RGB fallback in the op. |
| M9 | Text layers: native K-only path for single solid color text, RGB fallback otherwise; app/text owned by 08. |
| M10 | TESTING rewritten: RGB/GRAY layer-mode and paint golden test landed by 01a before any compositing change, paint exactness, paste/DnD K-preservation, per-format export round trips, generated input-only TIFF fixture, T13 against babl with tolerance, T24 replaced by a separate executable. |
| M11 | Undo push order specified (INTERFACES §6); I1 enforced only at API entry points; `gimp_image_get_layer_space()` absorbs the transient. |
| M12 | WS01 split into 01a (hours) and 01b; every WS starts at the 01a merge; merge order 01a → 01b → 02 → 03 → 04 → 05 → 08 → 07 → 06 → 09. |
| M13 | MyPaint brush disabled on CMYK drawables (tool insensitive + core refusal). |

### REVIEW-01 resolution table
| Item | Status | Where |
|---|---|---|
| B1 same-profile re-separation | Resolved | I8; INTERFACES §3.1–3.4, §11; WS01a/01b/03; tests F8–F9, I9–I10, X3 |
| B2 paint core unprepared op | Resolved | INTERFACES §8.2; WS02 step 5; tests G2, C9 |
| B3 composite-space contract | Resolved | INTERFACES §8.2–8.3; WS02 steps 2, 7; tests C1, C8, C13 |
| M1 two-stride real_process / blend fns | Resolved | INTERFACES §8.4; WS02 step 3 |
| M2 performance | Resolved | INTERFACES §8.5; WS02 acceptance P1/P2 |
| M3 libgimpcolor holes | Resolved (K-preserving intents **Deferred**: new intent enum values ripple through PDB/UI/config; post-MVP) | INTERFACES §3.3, §3.5; WS01b |
| M4 builtin profile | Resolved (named-reference serialization **Deferred**: touches 5 serializers in 4 workstreams; embedding is correct, only bigger) | INTERFACES §3.4; WS01a/01b |
| M5 stack buffers | Resolved | §5; TESTING Tier A #2b |
| M6 ownership | Resolved | §4 |
| M7 previews/thumbnails | Resolved | INTERFACES §10; WS03/07/05 |
| M8 legacy modes / "trivial" | Resolved | INTERFACES §8.1; I9 |
| M9 text | Resolved (native solid-color path in MVP; multi-color/outlined text via RGB, documented) | INTERFACES §11; WS08 |
| M10 test gaps | Resolved | TESTING.md |
| M11 undo/I1 transient | Resolved | INTERFACES §4, §5, §6; WS03 |
| M12 WS01 split / graph | Resolved | §3, §6 |
| M13 MyPaint | Resolved | INTERFACES §8.6, §11; WS02/08 |
| m1 XCF details | Resolved | INTERFACES §13; WS04 |
| m2 I6 exact only ≤ float | Resolved (stated in I6) | INTERFACES §8.4 |
| m3 babl one-way crash | Resolved in GIMP (bidirectional check); upstream babl NULL-check patch **Deferred** (outside this branch; coordinator files the report) | INTERFACES §3.2 |
| m4 babl facts (CaMaYaKaA double exists) | Resolved | §1, INTERFACES §2 |
| m5 babl MAX_SPACES | Resolved (I8 cache bounds growth to one slot per distinct profile) | INTERFACES §3.1 |
| m6 gimp_image_new NULL | Resolved (callers gate; preconditions only) | INTERFACES §4 |
| m7 histogram slots | Resolved | INTERFACES §9 |
| m8 component state on conversion / GRAY bug | Resolved | INTERFACES §4; WS01a, WS03 |
| m9 mask-components bpp | Resolved | INTERFACES §8.6; WS02 |
| m10 plug-in type parser | Resolved | INTERFACES §1; WS05 |
| m11 channel semantics | Resolved: channel value = ink amount everywhere (D8) | D8 |
| m12 average picker | Resolved | INTERFACES §11; WS08 |
| m13 GEGL native ops | Resolved (D13; T-test C7 uses gaussian-blur and invert-gamma) | D13 |
| m14 generated files | Resolved | §4.2 |
| m15 simulation profile as target | Resolved (dialog shows the chosen profile) | WS07 |
| m16 shared GIMP_TESTING_PLUG_INS | Resolved (01a appends once) | INTERFACES §15 |

## 0. Goal and scope

Add a fourth `GimpImageBaseType`, `GIMP_CMYK`. A user can create a CMYK image (always with a
CMYK ICC profile), convert RGB/Gray/Indexed ⇄ CMYK through ICC profiles with a chosen intent +
BPC, open CMYK TIFF/JPEG/PSD without converting to RGB, paint, composite layers, run GEGL
filters (RGB round trip of the filtered region where no native path exists), save/load XCF,
and export CMYK natively (TIFF, JPEG, PSD).

Out of scope (MVP): GCR/UCR controls, spot/multichannel, CMYK-native blend math for blend
modes, CMYK indexed palettes, C/Python plug-in filters on CMYK (insensitive unless they opt
in), K-preserving intents, legacy layer modes in CMYK images, MyPaint brush on CMYK, native
CMYK gradients and multi-color text.

Guiding rules: **correct color management over performance**, and **never touch ink values a
user did not ask to change** (I4).

## 1. Facts established from the code (and babl/lcms probes)

Rev-1 facts still hold unless corrected here. Corrections and additions (all verified):
* **Space identity.** babl keys CMYK spaces by all ICC bytes, header included
  (`babl-space.c:296-304`); `gimp_color_profile_is_equal()` skips the header. Two babl spaces
  for an "equal" profile make every CMYK⇄CMYK fish a relcol re-separation
  (`cmykcmyk.c`: K100 → (44573,44190,42768,48642)). Identical bytes → identical space → exact.
  lcms writes the current time into the header on every save.
* `gimp_layer_new_convert_buffer()` (`gimplayer-new.c:222-251`) always runs a color transform;
  `gimp_layer_convert()` (`gimplayer.c:1081`) short-circuits equal profiles. Both end in
  `gimp_gegl_convert_color_profile()` → `GimpColorTransform` (babl unless the env var).
* **Paint core.** `DoLayerBlend` (`gimppaintcore-loops.cc:1903-1914, 2028`) calls the
  process function of the shared op from `gimp_layer_mode_get_operation()`
  (`gimp-layer-modes.c:1313`), which has no graph; `gegl_operation_get_format()` then returns
  RGBA float → `n_components` 4.
* **Composite space.** `gimp_gegl_mode_node_set_mode()` resolves AUTO before it reaches the op
  (`gimp-gegl-nodes.c:180-184`); layers pass effective (resolved) spaces (`gimplayer.c:722-753`);
  group layers derive effective modes from children (`gimpgrouplayer.c:1361-1500`), so the
  resolved value cannot tell "user chose RGB" from "default".
* **Flatten** builds `gegl:color` in RGBA float with preferred NULL and the layer on aux
  (`gimp-gegl-nodes.c:44-81`); callers: `gimp_gegl_apply_flatten()` (from `gimplayer.c:2539,
  2893`, `gimpchannel.c:616`) and `gimpimage-merge.c:859`.
* **Blend functions** (33 in `gimpoperationlayermode-blend.c`) take `n_components` and, for
  luminance/luma, their fish format from the op *input* format. `real_process`
  (`gimpoperationlayermode.c:613-781`) uses one stride; `cache_fishes` (`:884-1041`) only
  builds RGB/Lab fishes. SSE paths already fall back for `n_components != 4`.
* **Trivial flag** = Normal-legacy, Dissolve, Normal, Erase, Merge, Split, Pass-through,
  Replace, Overwrite. Ops without a blend function = Normal, Dissolve, Behind, Erase,
  Anti-erase, Merge, Split, Pass-through, Replace, Overwrite (all n-generic, checked). Legacy
  ops override `process`; HSV/HSL legacy hard-code 4 components. Every one of the 22 legacy
  modes has a same-named non-legacy mode.
* **Performance (this box, 2 CPUs).** babl reference fish CMYKA float→RGBA float 0.9 Mpx/s,
  back 2.2 Mpx/s. lcms 2.14 with 16-bit sRGB-encoded RGB: CMYK16→RGB16 17–20 Mpx/s,
  sRGB16→CMYK16 24 Mpx/s; lcms float or linear-RGB transforms only 3–4 Mpx/s (`lcms-perf.c`).
* **lcms types.** `TYPE_CMYKA_*` are not in lcms2.h; `gimpcolorprofile.c:58-71` defines DBL,
  HALF_FLT, FLT, 16 (8 is missing).
* **babl CMYK formats**: `CaMaYaKaA double` exists, `CaMaYaKaA u32` does not.
  `babl_space_from_icc()` without lcms still creates a CMYK space but converts with the naive
  formula; with lcms, CMYK⇄RGB is relcol + BPC (so K100 → 0.0 on both paths; the lcms probe
  must use a mid-tone, INTERFACES §2).
* **Builtin prototype** (`mkcmyk-rev2.c`): 9⁴/17³, 79 492 bytes, byte-identical across runs
  with the fixed header; K100 → sRGB' 0.080 (lcms relcol, no BPC); babl K100 round trip
  (0.009, 0.039, 0.031, 0.994); RGB→CMYK→RGB mean error 0.010, max 0.12 (sRGB' units). The
  rev-1 inverse (no near-black fade) gave a K100 round trip of (0.91, 0.96, 0.06, 0.99).
* **Previews** end in `g_return_val_if_reached` for unknown base types
  (`gimpdrawable-preview.c:170-184`, `gimpimage-preview.c:45-55`); `gimpviewrenderer.c:1138-1204`
  pairs the viewable's profile with the preview buffer.
* **Undo.** `GIMP_UNDO_IMAGE_TYPE` pop only sets `base-type` (`gimpimageundo.c:328-351`); the
  profile is a separate parasite undo. Today's GRAY conversion already passes through the same
  transient (base GRAY + RGB parasite) and tolerates it.

## 2. Architecture decisions

### Invariants
* **I1** A CMYK image always has a non-NULL CMYK, bidirectional profile — enforced at API
  entry points only (INTERFACES §5); transient states during undo are absorbed (§4).
* **I2** CMYK images only use `*_NON_LINEAR` precisions.
* **I3** Every app-side CMYK buffer format carries the image's canonical CMYK space.
* **I4** No implicit CMYK→RGB→CMYK round trip of pixels the user did not change.
* **I5** babl contract: RGB/Y formats tagged with a CMYK space mean sRGB/linear sRGB; CMYK
  without space ≡ CMYK with space (identity).
* **I6** Zero-coverage pixels are bit-identical through every composite (U8/U16/HALF/FLOAT).
* **I7** Component masks set bits of absent components (INTERFACES §1).
* **I8** One babl space per `is_equal` CMYK profile per process (INTERFACES §3.1).
* **I9** No layer of a CMYK image has a legacy layer mode.
* **I10** Native-format pixel stack buffers are `MAX_CHANNELS` doubles.

### D1. Enums — unchanged from rev 1 (INTERFACES §1); aliases of `GimpComponentMask` are
`#define`s.

### D2. CMYK profile and babl space
I1 and I8. Attachment points: `gimp_image_constructed()` (no undo), conversion (INTERFACES §6
step 4), `gimp_image_set_color_profile/icc_profile (NULL)` on CMYK → default (with undo), XCF
load end. Not in parasite/undo code. Validation rejects non-CMYK and non-bidirectional
profiles for CMYK formats. "Use sRGB"/hidden profile do not apply to CMYK.

### D3. Default CMYK profile
Generated builtin (INTERFACES §3.4): 01a ships a 3⁴/3³ stub, 01b the final 9⁴/17³ profile.
Resolution order (`gimp_get_default_cmyk_profile()`): explicit profile (dialog, PDB, template,
or for RGB→CMYK the image's simulation profile when it is CMYK and bidirectional; the convert
dialog shows which one was chosen — m15) → `GimpColorConfig::cmyk-profile` if usable → builtin.
Serialized CMYK colors embed the ICC bytes (~106 KB base64 with the builtin); accepted for MVP.
Vendoring a real press profile (FOGRA39/ECI terms) stays an open option (R8).

### D4. Pixel formats — rev 1 table unchanged (`CMYK(A) u8/u16/u32/half/float/double` with the
image space). Defensive registration only for `CaMaYaKaA u32` and the component formats.

### D5. Alpha — unchanged (straight CMYKA; masks/channels/selection stay `Y`).

### D6. Compositing / layer modes (rewritten; contract in INTERFACES §8)
* In a CMYK image every composite (layers, groups, filters, fills, paint) runs in
  `CMYKA float (image space)` unless the layer's or filter's **stored** composite-space
  property is an explicit RGB space (then today's RGB compositing, lossy by user choice). The
  fact travels as op property `cmyk-native`.
* Colour-agnostic modes (no blend function: Normal, Dissolve, Behind, Erase, Anti-erase, Merge,
  Split, Pass-through, Replace, Overwrite) composite inks per channel, no conversion.
* Blend modes convert backdrop and layer CMYKA → blend space through the CMYK converter
  (lcms u16 device links, relcol+BPC via sRGB), blend with 4 components, convert back, and
  composite in CMYK with stride 5. Only covered samples are converted (I6).
* Legacy modes: I9. Paint/fill map them to non-legacy; the op falls back to RGB if it ever
  meets CMYK data in a graph.
* Paint core: per-(mode, CMYK format) shared op instance with explicit pad formats.
* Flatten: `gimp_gegl_create_flatten_node (background, format, composite_space)`; CMYK format
  → CMYK composite, background color converted to the image space.
* Projection/group layers: `CMYKA <precision>`. Display converts projection → monitor with lcms
  (display intent); soft-proofing stays available.
* Later (not MVP): native ink math for Multiply/Screen/Darken/Lighten/Addition/Subtract/
  Difference; HSV/LCh/Overlay family stay RGB.

### D7. Conversions
`gimp_image_convert_type_full()` with intent + BPC; undo order INTERFACES §6. RGB/GRAY → CMYK:
dest = argument, else `gimp_get_default_cmyk_profile()`; precision moved to `*_NON_LINEAR` in the
same group. CMYK → RGB: dest = argument, else `rgb-profile` preference, else sRGB; CMYK → GRAY
likewise. CMYK → CMYK: `gimp_image_convert_color_profile()` (lcms, intent+BPC; re-separates —
K-preserving intents deferred); "Assign" relabels. Indexed → CMYK: indexed→RGB then RGB→CMYK in
one group. CMYK → Indexed refused. Intent transport: `_gimp_image_{set,get}_conversion_intent()`.
`GimpColorTransform` per INTERFACES §3.3; lcms CMYK I/O via 8/16-bit types (§3.5).

### D8. Channels dialog, components, histogram
Channels dialog lists Cyan, Magenta, Yellow, Black, Alpha; eye/affect use the new BLACK bit.
Hiding a component sets it to 0 ink in the view. **Channel semantics (m11)**: a component's
channel value is its ink amount (1.0 = 100 % ink, white in a channel/preview); "channel to
selection" selects inked areas (unlike Photoshop, consistent with GIMP's channel model).
Histogram native on CMYKA float (INTERFACES §9). Color tools work through the RGB round trip,
compute their histograms in RGB, and show "Operates on an RGB conversion of the CMYK data".
Pickers report CMYK values.

### D9. Undo — no new undo types; order and transient handling per INTERFACES §6/§4.
`gimpimageundo.c` emits profile-changed on base-type changes involving GRAY or CMYK. Undo must
restore original bytes exactly (tests I2).

### D10. Templates and new-image dialog — unchanged from rev 1.

### D11. babl requirement — babl built with lcms; probe per INTERFACES §2. Without it: CMYK
actions insensitive, PDB/XCF errors, loaders use today's RGB path, one startup warning. Do not
raise `babl_minver`.

### D12. File formats — XCF v28 (INTERFACES §13). TIFF (separated), JPEG (Adobe inverted),
PSD (CMYK mode) load natively when the embedded profile is CMYK and bidirectional, or absent
(default profile assigned, raw values kept, message). One-way profiles: today's RGB path +
warning. Native export of CMYK images for all three; the existing "export as CMYK" options for
RGB images stay. Loaders set the profile before creating layers (INTERFACES §14).

### D13. Plug-ins and filters
GEGL filters work on CMYK via babl; `gegl:transform` and `gegl:gaussian-blur` are already
CMYK-native (m13), others round-trip the filtered region only (masked-out pixels untouched).
C/Python plug-ins opt in with "CMYK*"/"CMYK"/"CMYKA"; "*" excludes CMYK (menu sensitivity
only — PDB calls are not type checked). Exporters without `CAN_HANDLE_CMYK` get "Convert to RGB".

### D14. Previews and thumbnails — sRGB `R'G'B'(A) u8` for CMYK (INTERFACES §10).

### D15. Cross-image operations, pickers, FG/BG — INTERFACES §11 (one rule each).

### D16. Text, MyPaint, gradients — solid-color text native K path, else RGB (rich black);
MyPaint unavailable on CMYK; gradients render in RGB (documented).

## 3. Workstreams

| WS | Name | Agent | Starts | Needs merged for acceptance | Summary |
|---|---|---|---|---|---|
| 01a | foundation | A1 | now | – | enums, gimp-babl, canonical CMYK spaces, stub builtin, gimpimage/drawable core, declarations + stubs of INTERFACES, test skeletons + registrations, RGB golden test |
| 01b | color management | A1 (after 01a) | 01a merged | 01a | lcms format fixes, transform table, final builtin, CMYK profile convert/assign/import |
| 02 | compositing & paint | B | 01a merged | 01a, 01b (P1/P2 perf) | layer modes, mask-components, gegl, applicator, filters, fills, groups, merge/flatten, paint |
| 03 | core image | C | 01a merged | 01a, 01b, 02 (group/projection tests) | convert_type_full, undo, new/templates, previews, paste/DnD/buffers, palettes, file-open |
| 04 | xcf | D | 01a merged | 01a | XCF v28 load/save, test-xcf CMYK |
| 05 | pdb & libgimp | E | 01a merged | 01a, 03 (convert PDB) | PDB procs, libgimp API, export options, plug-in types, preview area |
| 06 | file plug-ins | F | 01a merged | 01a, 01b, 05, 07 | TIFF/JPEG/PSD native load/export, format round-trip tests |
| 07 | ui | G | 01a merged | 01a, 01b, 03 | actions, menus, dialogs, display, view renderer, widgets |
| 08 | channels, histogram, tools, pickers, text | H | 01a merged | 01a, 02 (mask-components), 05 | component editor, histogram, color tools, pickers/selectors, select-by-color, text K path, MyPaint insensitive |
| 09 | integration tests | I (04's agent may take it) | 01a merged | all | e2e, no-support test, babl probe, helpers, DOUBLE/ASan runs |

**Every workstream can start coding as soon as 01a is merged** (target: hours). 01b is done by
the 01a agent immediately afterwards; until it merges, CMYK transforms run through babl
(correct for same-profile cases thanks to I8, intent ignored, slow) — code against the
INTERFACES behaviour, not the temporary one.

## 4. File ownership (complete and disjoint)

Each path has exactly one owner at any time. "→ X after 01a" = sequential handover at the 01a
merge. **Default owners** make the table complete: any file under a directory listed below
that is not named elsewhere belongs to that directory's default owner. Editing a file you do
not own requires the coordinator's OK (file a note in your WS Status section).

### 4.1 Owned files
**01a**
* `libgimpbase/gimpbaseenums.h`; `app/core/core-enums.h`, `app/core/core-types.h`
* `app/gegl/gimp-babl.c`, `gimp-babl.h`, `gimp-babl-compat.c`, `gimp-babl-compat.h`
* `app/core/gimpdrawable.c`, `gimpdrawable.h`, `app/core/gimpprojection.c`
* `app/core/gimpimage.c`, `gimpimage.h`, `gimpimage-private.h` → **03 after 01a**
* `app/core/gimpimage-convert-type.h`, `gimpimage-convert-type.c` (stub) → **03 after 01a**
* `libgimpcolor/gimpcolorprofile.c`, `gimpcolorprofile.h`, `libgimpcolor/gimpcolor.def` → **01b**
* `app/core/gimpimage-color-profile.c`, `gimpimage-color-profile.h` → **01b**
* `app/tests/meson.build`, `app/tests/gimp-app-test-utils.c`, `.h` → **09 after 01a**; skeletons
  of all new test files → their owners (INTERFACES §15)
* `app/tests/test-layer-modes-golden.c`, `app/tests/layer-modes-golden.h` (frozen after 01a)

**01b**: `libgimpcolor/gimpcolorprofile.{c,h}`, `libgimpcolor/gimpcolortransform.{c,h}`,
`libgimpcolor/gimpcolor.def`, `app/core/gimpimage-color-profile.{c,h}`,
`app/tests/test-cmyk-foundation.c` (01a writes F1–F7, 01b the rest).

**02** (default owner of `app/operations/`, `app/gegl/`, `app/paint/`)
* `app/operations/layer-modes/*` (incl. `meson.build`, new `gimpoperationlayermode-cmyk.{c,h}`),
  `app/operations/layer-modes-legacy/*`
* `app/operations/*` except the 08 list (e.g. `gimpoperationmaskcomponents.{cc,h}`,
  `gimpoperationgradient.c`, `gimpoperationsemiflatten.c`, `gimpoperationprofiletransform.c`,
  `gimpoperationsetalpha.c`, `gimpoperationfillsource.c`, `meson.build`)
* `app/gegl/*` except `gimp-babl*` (incl. `gimp-gegl-loops.cc`, `gimp-gegl-nodes.{c,h}`,
  `gimp-gegl-apply-operation.{c,h}`, `gimpapplicator.{c,h}`)
* `app/paint/*`
* `app/core/gimpdrawablefilter.c`, `gimpfilloptions.c`, `gimpgrouplayer.c` (whole file,
  including `gimp_group_layer_convert_type()` done on 03's behalf), `gimpgrouplayerundo.c`,
  `gimpimage-merge.c`
* `app/core/gimpdrawable-{bucket-fill,combine,edit,fill,filters,floating-selection,
  foreground-extract,gradient,offset,operation,shadow,stroke,transform}.c`
* **function** `gimp_layer_update_mode_node()` in `app/core/gimplayer.c` (rest of file: 03)
* `app/tests/test-cmyk-compositing.c`

**03** (default owner of `app/core/`)
* `app/core/gimpimage.{c,h}`, `gimpimage-private.h`, `gimpimage-convert-type.{c,h}` (after 01a)
* `app/core/gimpimage-convert-indexed.c`, `gimpimage-convert-precision.c`, `gimpimageundo.c`,
  `gimpimage-undo-push.c`, `gimpimage-new.{c,h}`, `gimptemplate.{c,h}`, `gimpimage-duplicate.c`,
  `gimpimage-metadata.c`, `gimpimage-colormap.c`, `gimpimage-preview.c`,
  `gimpdrawable-preview.c`, `gimpimage-savable.c`, `gimpsavable.c`, `gimpsavable-load.c`,
  `gimpimage-{crop,flip,rotate,resize,scale,transform}.c` (verify)
* `app/core/gimplayer.c` (except `gimp_layer_update_mode_node()`), `gimplayer-new.c`,
  `gimplayer-floating-selection.c`, `gimp-edit.c`, `gimpbuffer.c`, `gimpchannel.c`,
  `gimpselection.c`, `gimplink.{c,h}`, `gimpcontext.c`, `gimp-utils.c`,
  `gimppalette.c`, `gimppalette-load.c`, `gimppalette-import.c`, `gimppickable-auto-shrink.c`
* every other `app/core/*` file not named for 01a/01b/02/08
* `app/file/*`, `app/file-data/*`, `app/tests/test-cmyk-image.c`

**04**: `app/xcf/*`, `app/tests/test-xcf.c`.

**05**
* `pdb/groups/*.pdb` (and hand-written `pdb/*.pl` if ever needed)
* `app/pdb/gimppdb-utils.{c,h}` (other `app/pdb/*` are generated)
* `app/plug-in/*` hand-written files (`gimppluginprocedure.c`, `plug-in-enums.h`, …)
* `libgimp/*` hand-written files: `gimpexportoptions.c`, `gimplayer.c`, `gimpimage.c`,
  `gimpimagecolorprofile.{c,h}`, `gimpdrawable.c`, `gimpimagemetadata.c`,
  `gimpdrawablepreview.c`, `gimpthumbnailprocedure.c`, `gimp{brush,drawable,image,item,pattern}chooser.c`,
  `gimpgpparams-body.c` (verify), `gimp.def`, and any other hand-written `libgimp/*`
* `libgimp/tests/*` (incl. `test-export-options.c`, `test-image.c`, `test-selection-float.c`,
  new `test-cmyk.py`, `meson.build`)
* `libgimpwidgets/gimppreviewarea.{c,h}`, `libgimpwidgets/test-preview-area.c`

**06**: `plug-ins/file-tiff/*`, `plug-ins/file-jpeg/*`, `plug-ins/file-psd/*`,
`app/tests/test-cmyk-file-formats.py`.

**07** (default owner of `app/widgets/`, `app/actions/`, `app/dialogs/`, `app/display/`)
* `app/actions/*` except `channels-actions.c`, `channels-commands.c`
* `app/dialogs/*`, `app/display/*` (incl. `gimpcursorview.c`), `app/gui/*`, `app/menus/*`,
  `menus/*`, `app/config/*`, `app/propgui/*`
* `app/widgets/*` except the 08 list (incl. `gimpviewrenderer*.c`, `gimpwidgets-utils.c`,
  `gimpimagepropview.c`, `gimptemplateeditor.c`, `gimpcolormapeditor.c`,
  `gimpcolormapselection.c`, `gimpselectiondata.c`, `gimpclipboard.c`, `gimplayermodebox.c`,
  `gimplayermodecombobox.c`, `gimphelp-ids.h`)
* `libgimpwidgets/gimpwidgetsutils.c`, `libgimpconfig/gimpcolorconfig.c` (label/tooltip only)

**08**
* `app/core/gimphistogram.{c,h}`, `gimpdrawable-histogram.{c,h}`, `gimpdrawable-levels.c`,
  `gimpdrawable-equalize.c`, `gimppickable.{c,h}`, `gimpimage-pick-color.{c,h}`,
  `gimppickable-contiguous-region.cc`, `gimpchannel-select.c`
* `app/operations/gimp{brightnesscontrast,colorbalance,curves,huesaturation,levels}config.c`,
  `app/operations/gimpoperation{brightnesscontrast,colorbalance,colorize,curves,desaturate,
  equalize,histogramsink,huesaturation,levels,pointfilter,posterize,threshold}.c`
* `app/tools/*`, `app/text/*`, `app/actions/channels-actions.c`, `channels-commands.c`
* `app/widgets/gimp{componenteditor,histogrameditor,histogramview,histogrambox,colordialog,
  colorframe,colorhistory,fgbgeditor,samplepointeditor,colorbar}.c`
* `modules/color-selector-cmyk.c`; `libgimpwidgets/*` except the 05 and 07 files
* `app/tests/test-cmyk-histogram.c`

**09**: `app/tests/meson.build`, `gimp-app-test-utils.{c,h}` (after 01a), `test-cmyk-e2e.py`,
`test-cmyk-nosupport.c`, `cmyk-babl-probe.py`, new `app/tests/files/*`, and every existing
`app/tests/*` file not named above.

**Coordinator only**: `devel-docs/cmyk-mode/*` (workstreams edit only the Status section of
their own `WORKSTREAMS/<ws>.md`). Anything else (libgimpbase other files, libgimpmath,
libgimpthumb, libgimpconfig other files, other plug-ins, data, build files outside the dirs
above): no change expected; ask first.

### 4.2 Generated files (never hand-edited; regenerated by `ninja -C _build`, committed in a
separate "regen" commit; on merge conflict take either side and regenerate)
`libgimpbase/gimpbaseenums.c`, `libgimpbase/gimpcompatenums.c`, `app/core/core-enums.c`,
`app/*/*-enums.c` (incl. **`app/plug-in/plug-in-enums.c`**, `app/operations/operations-enums.c`,
`app/paint/paint-enums.c`, `app/text/text-enums.c`, …), `pdb/enums.pl`, `libgimp/gimpenums.h`,
`libgimp/gimpenums.c.tail`, `app/pdb/*-cmds.c`, `app/pdb/internal-procs.{c,h}`,
`libgimp/*_pdb.{c,h}`. Only 01a (enums) and 05 (PDB) trigger changes; whoever's merge produces
the diff commits the regen.

## 5. Stack-buffer sites (M5, rule I10)
| Site | Problem | Owner |
|---|---|---|
| `app/core/gimppickable.c:260` `gimp_pickable_get_color_at()` `gdouble pixel[4]`, native format | overflow (CMYKA double = 40 B) | 08 |
| `app/core/gimppickable.c:300` `gimp_pickable_pick_color()` `gdouble sample[4]`, native | overflow | 08 |
| `app/core/gimppickable.c:92`, `app/core/gimpimage-pick-color.c:158` `sample[4]` in `RaGaBaA double` | size OK; averages through RGB (m12) → average native for CMYK | 08 |
| callers receiving native pixels into `gdouble pixel[4]`: `app/tools/gimpcolortool.c:633`, `app/widgets/gimpsamplepointeditor.c:557` | overflow | 08 |
| `app/display/gimpcursorview.c:763` `gdouble pixel[4]` | overflow | 07 |
| `app/gegl/gimp-gegl-loops.cc:305, 356` `gimp_gegl_convolve()` `gdouble total[4]` indexed by components (5) | overflow (Blur/Sharpen tool) | 02 |
| `app/paint/gimpsmudge.c:296` `gfloat pixel[4]` `RGBA float`; `gimp-gegl-loops.cc:630` `brush_color_float[4]` | no overflow; RGB round trip → accumulate in CMYKA float | 02 |
| `app/gegl/gimp-gegl-loops.cc:1100,1134` `gimp_gegl_average_color()` `RaGaBaA float` sums | semantic (RGB average) → native average for CMYK | 02 |
| `app/core/gimppickable-contiguous-region.cc:145,221,356` `MAX_CHANNELS`; format built with NULL space | OK size; pass the buffer space | 08 |
| `app/operations/gimpoperationgradient.c:1268,1281,1313`, `gimpoperationsemiflatten.c:178`, legacy HSV/HSL `[4]` | RGB-only formats / I9 | 02 (verify) |
| `app/core/gimppalette-import.c:210,351`, `gimppalette-load.c:1064,2223` | RGB formats | 03 (verify) |
| `libgimpwidgets/gimpcolorscales.c:802-803` | explicit RGB/HSV formats | 08 (verify) |
| `plug-ins/file-psd/psd-load.c:3943,5736`, `plug-ins/file-tiff/file-tiff-export.c:850` | check per format | 06 (verify) |
| `guint8 pixel[40]` / `[48]` byte buffers (`gimp-babl.c:1566`, `xcf-save.c:2399`, `gimppalettemru.c:189`, `gimpsavable.c:334`, `gimppalette.c:437…765`, `gimpcolormapselection.c:458`, `gimpselectiondata.c:288`, `gimppaletteeditor.c:685`, `libgimpwidgets/gimpcolorarea.c:1049`, `libgimpcolor/gimpcolor.c:75,499`) | exactly fit CMYKA double — never shrink | each owner |
Tier A grep #2b (TESTING) finds new ones; the T-suites run once at DOUBLE precision (09).

## 6. Dependency graph and merge order

All workstreams **start** at the 01a merge. "Needed for acceptance" edges (A must be merged
before B can pass its acceptance tests):
| A (merged first) | enables acceptance of | why |
|---|---|---|
| 01a | everything | enums, formats, declarations, test skeletons, golden test |
| 01b | 02, 03, 06, 07 | lcms CMYK transforms (converter perf P1/P2, intent-honouring conversions, loaders' profile checks, display transforms) |
| 02 | 03, 08 | group-layer conversion, projection, flatten (03 tests); mask-components (08 T15/T16-style UI checks) |
| 03 | 05, 07 | `gimp_image_convert_type_full()` behind the new PDB proc and the Mode menu |
| 05 | 06, 08 | export options + libgimp type checks (06); PDB histogram enums (08 PDB checks) |
| 07 | 06 | import-policy dialog wording for CMYK files |
| all | 09 | e2e test |
04 depends on 01a only.
Merge order (each merge: rebase on `cmyk-mode`, `ninja -C _build`, regen commit if needed,
`meson test -C _build --no-rebuild --print-errorlogs` = baseline + own suites green, golden
test G1–G3 green): **01a → 01b → 02 → 03 → 04 → 05 → 08 → 07 → 06 → 09.**
The order is a topological order of the table above; 04 sits after 03 only to keep one rebase
stream, 07 after 08 because both edit neighbouring `app/widgets` code. 09 is last because the
e2e test exercises everything; its probe and no-support test may merge earlier as a test-only
merge if the coordinator agrees.
Branches: `cmyk/<ws>-<name>` from `cmyk-mode` after the 01a merge (tag `cmyk-foundation`);
`git merge --no-ff`; no squashing.

## 7. Risks, deferred items, open questions
* R1 babl segfault with one-way CMYK profiles → `is_cmyk_bidirectional` everywhere a CMYK space
  is made for an image or a target (01a/01b, loaders 06). Upstream babl patch: deferred.
* R2 Performance: blend modes use the lcms converter (P1/P2); filters and previews still use
  babl's reference fish (slow on large CMYK images; previews are small). Later: register babl
  fast paths per image space.
* R3 Intent mismatch: babl and the converter are relcol+BPC; only explicit conversions honour
  the user's intent.
* R4 lcms float CMYK 0..100 → routed through u16 (INTERFACES §3.5, test F11).
* R5 real_process two-stride rewrite: RGB must stay bit-identical (golden test G1/G2).
* R6 ≤4-component assumptions → Tier A greps #2/#2b, DOUBLE run, ASan run (09).
* R7 Plug-ins writing pixels before setting the profile get CMYK→CMYK conversion (§14).
* R8 Licensing of a vendored real profile — builtin is the MVP default.
* R9 Builtin quality: generic, RT error mean 0.010; good enough for "not for press".
* R10 Upstream may claim XCF 28 during the 3.4 cycle — coordinate on rebase (04).
* R11 Serialized CMYK colors are large (embedded ICC) — named reference deferred.
* Deferred (post-MVP): K-preserving intents; named-builtin serialization; native ink blend
  math; native CMYK gradients and multi-color text; MyPaint on CMYK; legacy modes in CMYK;
  read-only native load of one-way profiles; "*" including CMYK for audited plug-ins.

# REVIEW-01: independent review of the CMYK-mode plan

Reviewer: independent senior review, against the code at `cmyk-mode` (baseline 5a569ea + plan
commit 01099f8), babl master at `/home/claude/deps-src/babl` (a0b8d5a, installed 0.1.129) and
GEGL master. All claims below were checked by reading code; the ones marked **(measured)** were
reproduced with throwaway programs in `/root/review-scratch/` (`probe.c`, `crash.c`,
`cmykcmyk.c`, `perf.c`, `lcmsdate.c`), using `ghostscript/default_cmyk.icc` and
`pdfjs-dist/iccs/CGATS001Compat-v2-micro.icc` (input-only).

The plan is detailed and mostly accurate about the data model. Its weak points are the
**identity of CMYK color spaces** (when two CMYK buffers count as "the same profile"), the
**layer-mode op as used by the paint core**, and the **composite-space contract**. As written,
each of these would break the plan's main invariant: never touch ink values the user did not
ask to change. Fix them in the plan (PLAN/INTERFACES) before the 8 agents start.

Severity: **Blocker** means a WS built exactly as specified produces wrong pixels or crashes
in the core use case, or two WS would implement incompatible contracts. **Major** means a
significant defect, gap or hidden coupling that costs a WS days or ships a broken MVP. **Minor**
covers accuracy, test quality and small gaps.

---

## Blockers

### B1. Same-profile CMYK→CMYK conversions re-separate (K-only becomes 4-color)

Evidence:
* An ICC CMYK→CMYK relative-colorimetric transform goes through PCS. That holds for lcms and
  for babl's CMYK→CMYK branch (`babl/babl-fish-reference.c:1057-1059`, relcol+BPC). It
  re-separates through the destination BToA table even when source and destination are the same
  profile. **(measured)** `CMYK u16` (0,0,0,65535) between two babl spaces made from
  `default_cmyk.icc` gives (44573,44190,42768,48642). (0.1,0.2,0.3,0.4) gives
  (0.40,0.42,0.52,0.07).
* babl identifies CMYK spaces by **all** ICC bytes, header included
  (`babl/babl-space.c:296-304`, `memcmp` over `icc_length`). GIMP's
  `gimp_color_profile_is_equal()` skips the 128-byte header (`libgimpcolor/gimpcolorprofile.c`,
  `is_equal`: `memcmp (data + header_len, …)`). Two profiles GIMP considers equal can therefore
  be two babl spaces. **(measured)** Flipping one byte of the header date gives
  `same space 0` and the re-separation above. With identical bytes the result is identical.
* lcms writes the creation time into the header. **(measured)** `cmsSaveProfileToMem` on a
  placeholder profile gives different bytes 24-35 per run. lcms has no public setter for the
  date. So the D3 generated builtin profile is a **new babl space every session**. Any buffer
  moved between an image created now and an XCF saved earlier with the builtin (paste, DnD,
  open-as-layers, layer-mode aux/input in different spaces) is silently re-separated.
* PLAN D7 / INTERFACES §3 make `GimpColorTransform` use lcms "whenever either profile is
  CMYK". `gimp_layer_new_convert_buffer()` (`app/core/gimplayer-new.c:222-251`) always builds a
  transform and has **no equality short-circuit**, unlike `gimplayer.c:1081`. It is used by
  paste, "paste as new layer", new-layer-from-buffer and open-as-layers (`gimplayer-new.c:155,
  208`). The result: copy/paste a K-only area inside the same CMYK image and you get rich black.

Fix (owner 01, contract in INTERFACES §3):
1. `gimp_color_transform_new()` / `_new_proofing()`: if both profiles are CMYK and
   `gimp_color_profile_is_equal()`, never use lcms. Return a transform whose src and dest
   formats use **one** babl space (the src's), i.e. a pure re-encoding. Make
   `gimp_color_transform_can_gegl_copy()` TRUE for that case (it already is).
2. Canonical spaces: add `gimp_babl_space_for_profile()` (01, gimp-babl.c). It keeps a small
   cache keyed by `is_equal` semantics, returns the first space created for an equal profile,
   and is used by `_gimp_image_update_color_profile()`, `gimp_image_get_layer_space()` and
   every `gimp_color_profile_get_space()` caller in app/. It cannot fix plug-in processes, so
   also do 3.
3. Builtin profile with a **fixed header**: after `cmsSaveProfileToMem`, overwrite bytes 24-35
   (date) with a constant, zero the profile ID (84-99) or compute it with `cmsMD5computeID`.
   Then cache the bytes. TESTING T3 must assert the fixed date bytes.
4. New tests (09): (a) paste a K-only selection within one CMYK image, check bit-identical;
   (b) two images whose builtin profiles differ only in header bytes, layer DnD and paste
   bit-identical; (c) `gimp_color_transform_new(P, CMYK u16, P', CMYK u16)` with
   `is_equal(P,P')` gives identity.

### B2. The paint core runs layer modes on a shared, never-prepared op whose format is always RGBA float

Evidence: `DoLayerBlend` (`app/paint/gimppaintcore-loops.cc:1903-1914, 2028`) takes the
process-wide shared instance from `gimp_layer_mode_get_operation()`
(`gimp-layer-modes.c:1313-1348`). That instance is created once with no graph, and the loop
calls `layer_mode->function (op, in, layer, mask, out, …)` directly. Every process function
(`gimpoperationlayermode.c:629`, `gimpoperationnormal.c:103`, behind/erase/…, and every
blend function, e.g. `gimpoperationlayermode-blend.c:1019-1020`) gets `n_components` from
`gegl_operation_get_format (op, "output"/"input")`. For a node without pad formats that returns
`gegl_babl_rgba_linear_float()`, i.e. 4 components (`gegl/operation/gegl-operation.c:558-572`).
D6/INTERFACES §8 make the paint buffer `CMYKA float` (5 components): `gimpbrushcore.c:876`,
`gimpink.c:300` and `gimpfilloptions.c:683` all call `gimp_layer_mode_get_format()`. Every brush
dab on a CMYK drawable would then be composited with stride 4 over stride-5 data, producing
garbage. WS02 step 3 ("verify every composite function … is n_components generic") cannot find
this, because the composite functions *are* generic. The bug is in where `n_components` comes
from.

Fix (02; describe in INTERFACES §8): keep one shared instance per (mode, model class: RGB/Lab
vs CMYK), and set its pad formats explicitly with `gegl_operation_set_format (op,
"input"|"aux"|"output", fmt)` when the instance is created. Alternatively, pass
`n_components` through `GimpOperationLayerMode` fields that the paint loop sets per call (only
safe if the instance is per-format; paint loops run in parallel threads). Add a paint
exactness test (see M10).

### B3. The composite-space contract is self-contradictory and, read literally, makes every CMYK layer composite in RGB

Evidence:
* INTERFACES §8 returns CMYKA float "when composite_space is AUTO; explicit RGB composite spaces
  return RGB as today". PLAN D6 says "for **all** modes", and also that an explicit user choice
  is honoured.
* The op never sees AUTO. `gimp_gegl_mode_node_set_mode()` resolves AUTO to the mode's default
  (`app/gegl/gimp-gegl-nodes.c:180-184`), e.g. RGB_LINEAR for Normal. `gimp_layer` passes its
  effective spaces (`gimplayer.c:722-753`). Under INTERFACES §8 every layer node would therefore
  get RGB composite (the op's `cache_fishes` calls `gimp_layer_mode_get_format` with the
  resolved value, `gimpoperationlayermode.c:918-922`), and K is destroyed on every render.
* `gimp_gegl_create_flatten_node()` refuses non-RGB composite spaces
  (`gimp-gegl-nodes.c:44-46`). It builds the background `gegl:color` with
  `gimp_layer_mode_get_format(…, preferred=NULL)`, i.e. `RGBA float`, and puts the layer on
  `aux` (lines 57-63, 79-81). Because the op takes `preferred` from **input** when input is
  non-empty (`gimpoperationlayermode.c:330-337`), the flatten composite runs in RGB even under
  the "input **or** aux is CMYK" rule. Yet INTERFACES §8 says "`create_flatten_node()`
  unchanged signature; works on CMYK input".

Fix (decide in INTERFACES §8; 02 implements):
* Rule: if `preferred` (input, else aux) **or the other source pad** is CMYK, composite in
  `CMYKA float (space)`. The only exception is a layer whose own `composite-space` property is
  explicitly non-AUTO. Carry that fact to the op as a new op property, e.g. `"cmyk-native"`
  (gboolean, default TRUE), set by `gimp_layer` from `layer->composite_space == AUTO`, because
  the resolved enum cannot carry it.
* `gimp_gegl_create_flatten_node (background, composite_space, space_format)`: add a format
  argument, or derive the color node format from the input in `prepare`. The background
  `GeglColor` must be converted to the image's CMYK space, not RGBA float.
* Decide this before 02 starts. 03 (flatten during conversion/export) and 06 (export of
  flattened copies) depend on it.

---

## Major

### M1. D6 cannot be done "without rewriting the blend functions"; real_process needs a two-stride rewrite

* Every blend function derives `n_components` from the op's **input** format
  (`gimpoperationlayermode-blend.c:87-91`, `…:1019-1021`, and so on through `:1266`). With a
  CMYKA composite format they would stride 5 over the 4-component RGBA/Lab blend buffers. Fix:
  mechanically replace the derivation with a constant 4 in all ~35 functions. Blend formats are
  always RGBA/Lab-alpha today, so RGB output does not change.
* `real_process` (`gimpoperationlayermode.c:680-781`) uses one stride (`n_components`) for
  `in`, `layer`, `blend_in`, `blend_layer` and `blend_out`. It converts with
  `babl_process (fish, in + first, blend_in + first, count)` using the composite offsets, and
  `blend_out` is converted back in place. A 5→4→5 split needs separate 4-stride scratch
  buffers, sample-indexed (not float-indexed) bookkeeping for `first/last`, and a 5-stride
  `blend_out`. That is a contained rewrite of ~100 lines, not a parameter tweak.
* `cache_fishes` (`:884-1041`) only builds fishes between the RGB/Lab spaces, and only when
  `blend_space != composite_space` (`:1023-1038`). Normal-space RGB_LINEAR blend with
  RGB_LINEAR composite returns NULL fishes. A CMYK branch must create
  `CMYKA float(space) ⇄ <blend format>(space)` fishes unconditionally.
* Good news: the SSE paths already fall back for `n_components != 4`
  (`gimpoperationnormal-sse2.c:53`, `-sse4.c:53`, `composite-sse2.c:61`). Drop that part of R5
  and of WS02 step 3.
* I6 itself is achievable: `composite_union/clip/intersection` copy `in` exactly when the
  effective layer alpha is 0 (`gimpoperationlayermode-composite.c:63-67`, `:117-122`). The u16↔
  float round trip is exact **(measured)**. But see m2 for U32/DOUBLE.

### M2. Performance: babl CMYK⇄RGB is the reference fish at about 1-2 Mpx/s single-threaded

**(measured)** `CMYKA float → RGBA float` 1.0 Mpx/s, `RGBA float → CMYKA float` 1.9 Mpx/s, fish
`ref` (`perf.c`). A non-trivial-mode layer costs three such conversions per rendered pixel.
Every GEGL filter preview on CMYK, color tools, histogram-in-RGB, smudge/heal and previews go
through the same path. A 12 Mpx image with one Multiply layer needs on the order of 10+ CPU-s
per full re-render. R2 mentions this, but no WS owns a mitigation, and the MVP will feel broken
on real photos. Recommendation (02 + 01): in `GimpOperationLayerMode`, convert with a cached
lcms transform (`CMYKA float ⇄ RGBA float`, relcol+BPC, through the u16/×100 handling of M3),
keyed by space, instead of babl. Optionally register babl conversions for each image CMYK space
in gimp-babl.c (check against babl's tolerance first). Put a perf smoke target in WS02
acceptance (e.g. 2k×2k Multiply re-render < 2 s on 4 threads).

### M3. libgimpcolor lcms plumbing: more holes than the plan lists

Confirmed: `CMYKA` is missing (`gimpcolorprofile.c:1664-1671`, "FIXME missing from babl"),
float maps to `TYPE_CMYK_FLT` (0..100 in lcms; babl rescales ×100 itself,
`babl-fish-reference.c:895-909, 960-972`), and `can_gegl_copy()` returns TRUE for any two
babl-loadable profiles (`gimpcolortransform.c`, `can_gegl_copy`). The babl path ignores the
intent (`gimpcolortransform.c:231-243`; babl hard-codes relcol+BPC, `babl-icc.c:1032-1038`).
Also:
* **u32** falls through to the "not supported, falling back to float" branch with
  `rgb = ! gray` = TRUE (`gimpcolorprofile.c:1795-1805`). A CMYK u32 image would ask lcms for an
  RGB float transform on a CMYK profile, which fails. Route u32 through u16 too (or double
  with ×100 scaling).
* Inverted (`cmyk`, `cmykA`) and premultiplied (`CaMaYaKaA`, `camayakaA`) models hit
  `g_return_val_if_reached (NULL)` (`:1693-1702`). The MVP never feeds them, but either map them
  to `TYPE_CMYK_*_REV`/u16 substitutes or say so in the gtk-doc.
* The lcms path strips spaces (`gimpcolortransform.c:249-250`) and later re-tags with NULL
  space (`process_pixels`). That works only because babl treats CMYK(no space)↔CMYK(space) as
  identity **(measured)**, and CMYK(no space)↔RGB as the naive formula. Document this, and make
  01 assert that every app-side CMYK buffer format carries the image space.
* Offer the lcms K-preserving intents (`INTENT_PRESERVE_K_ONLY_RELATIVE_COLORIMETRIC` and
  friends) for CMYK→CMYK conversion. Otherwise "Convert to color profile" between two press
  profiles always re-separates K text. Post-MVP is fine, but say so in D7.

### M4. Default builtin profile (D3): realistic, but the spec as written does not serialize, is about 0.8 MB, and is not deterministic

**(measured, `lcmsdate.c`)**
* A pipeline holding only a CLUT stage saves as **0 bytes** (`cmsSaveProfileToMem` gives size
  0). v4 `mAB/mBA` needs identity tone-curve stages on both sides
  (`cmsStageAllocToneCurves(NULL, n, NULL)`). With them, 17⁴ AToB + 33³ BToA, intents linked
  with `cmsLinkTag`, is **789 KB**. Unlinked (as D3 says, "copies as AToB1/2") it is about 2.4 MB.
* Size matters because GIMP embeds the space's ICC in every serialized `GeglColor`: XCF
  (`xcf-save.c:2426-2437`, once per color: text, fill/stroke, effects), config/contextrc/tool
  options (`libgimpconfig/gimpconfig-serialize.c:361`), the plug-in wire
  (`libgimp/gimpgpparams-body.c:1517, 1601`), palettes (`gimppalette.c:445`, `gimppalettemru.c:213`)
  and DnD (`gimpselectiondata.c:305`).
* 9⁴ AToB + 17³ BToA, linked, is **79 KB**. The forward model is a product of linear terms, so
  interpolation error is small, and K100 / paper white sit exactly on grid nodes. Recommend that.
* Header date: see B1.3. v4 output profiles formally also want a `gamt` tag. lcms does not
  care; either write it, or emit a v2.4 profile.
* Deleting BToA0/1/2 with `cmsWriteTag (h, sig, NULL)` works **(measured)**: an input-only
  derivative reports `cmsIsIntentSupported(..., LCMS_USED_AS_OUTPUT) == 0` after a save/reopen.
  TESTING's negative-profile recipe is sound, but it must delete **all** BToA tags, linked
  ones included.
* Recommendation: **keep a generated builtin**. Without one, "New CMYK image" fails by default
  because `cmyk-profile` is unset in a fresh config. But make it a separate task (01b,
  `gimpcolorprofile.c` only, about 250-350 LOC). Until it lands, 01a can ship
  `gimp_color_profile_new_cmyk_builtin()` returning a lazily generated *minimal* 2-grid LUT
  profile, so the other WS are not blocked. Tests use the builtin and the generated input-only
  derivative; real profiles stay optional (`GIMP_TESTING_CMYK_ICC`).

### M5. Stack buffer overflows on 5-component and 40-byte pixels (CMYKA double / CMYKA float)

* `gimp_pickable_pick_color()`: `gdouble sample[4]` filled with the pickable's native format
  (`app/core/gimppickable.c:300, 307, 311`). That is 40 bytes for CMYKA double. Affects every
  color pick, select-by-color, PDB `gimp-image-pick-color`.
* `gimp_pickable_get_color_at()`: `gdouble pixel[4]` with `format = NULL` (native)
  (`gimppickable.c:260-264`).
* Callers of `gimp_image_pick_color()` with 32-byte buffers: `gimpcolortool.c:633`,
  `gimpcursorview.c:763`, `gimpsamplepointeditor.c:557`.
* `gimp_gegl_convolve()`: `gdouble total[4]` indexed by `components` of
  `gimp_babl_format(base_type(src), FLOAT…)` (`app/gegl/gimp-gegl-loops.cc:305, 356`), used by
  the Blur/Sharpen tool (`gimpconvolve.c:201-218`). CMYKA float has 5 components, so the tool
  writes out of bounds.
* Tier A grep #2 does not catch `gdouble sample[4]` (no `pixel` in the name). Fix: list these
  explicitly (owners 02: gegl-loops/convolve; 03: gimppickable.c; 07: gimpcursorview.c; 08:
  gimpcolortool.c, gimpsamplepointeditor.c). Size buffers `MAX_CHANNELS * sizeof (gdouble)`.
  Run the T-suite once at DOUBLE precision.

### M6. File ownership is not complete; several needed files have no owner

Files that need changes and appear in no list (the "default owner" rule hides them from the WS
step lists):
| File(s) | Why | Proposed owner |
|---|---|---|
| `app/core/gimplayer-new.c` | B1 paste/new-from-buffer conversion | 03 |
| `app/core/gimp-edit.c` | paste paths, base-type conversion of floating pastes (`:464-548, 587-614`) | 03 |
| `app/core/gimppickable.c`, `gimpimage-pick-color.c`, `gimppickable-contiguous-region.cc`, `gimppickable-auto-shrink.c` | M5 overflows; average-pick goes through `RaGaBaA double` (`gimpimage-pick-color.c:165`) | 08 (pick) / 03 (rest) |
| `app/core/gimpprojection.c` | `estimate_memsize` base-type switch (`:295-300`) | 01 |
| `app/core/gimpgrouplayerundo.c` | stores base type | 02 |
| `app/core/gimpimage-savable.c` | base type from format (`:638`) | 03 |
| `app/operations/layer-modes-legacy/*` | not n-generic (`gimpoperationhslcolorlegacy.c:88-93`, hard-coded `ALPHA`), see M8 | 02 |
| `app/operations/*` other than layer-modes / mask-components (gradient, levels, curves, threshold, semi-flatten, …) | color-tool ops, gradient renders RGB | 02 (08 for the color-tool ops) |
| `app/text/*` (`gimptextundo.c:253`, text layer rendering) | see M9 | 02 |
| `libgimp/gimpimagemetadata.c`, `gimpdrawablepreview.c`, `gimpthumbnailprocedure.c`, choosers | base-type switches, CMYK thumbnails | 05 |
| `libgimpwidgets/gimppreviewarea.c` | public API on `GimpImageType`; CMYK types `g_return` | 05 |
| `libgimpwidgets/gimpwidgetsutils.c` | display transform cache with CMYK filter profile | 07 (08 only reads it) |
| `libgimp/tests/test-export-options.c` | changes with 05's export options | 05 (not 09) |
| `app/plug-in/plug-in-enums.c` | listed as hand-owned by 05 but it is **generated** | regen rule |

Real double ownership or coupling:
* `gimpgrouplayer.c` (02) contains `gimp_group_layer_convert_type` (`:1168-1196`) and the group
  projection-format switch (`:1147-1160`). Conversion is 03's domain and must read the
  conversion intent. List it explicitly as 02 work done on 03's behalf.
* `gimpimage.c` (01) will be needed by 03: reset component visibility/active on base-type
  change (m8), and the I1 handling during undo (M11). Name a handover, as is done for
  convert-type.c.
* `gimplayer.c` (03) is needed by 02 to pass the "explicit composite space" flag of B3.
* `app/tests/gimp-app-test-utils.{c,h}` (09) is needed by 04's test-xcf.c CMYK helpers.
  04 should keep its helpers `static` in test-xcf.c.

### M7. The preview/thumbnail contract is missing and couples 03 ↔ 07 ↔ 05

`gimp_drawable_get_preview_format()` / `gimp_image_get_preview_format()` end in
`g_return_val_if_reached` for unknown base types (`gimpdrawable-preview.c:170-184`,
`gimpimage-preview.c:45-55`). WS03 plans RGB u8 previews. But `gimpviewrenderer.c:1138-1204`
(07) pairs the **viewable's** profile (the CMYK image profile) with the preview buffer's
format. An RGB preview buffer plus a CMYK profile gives a broken transform. libgimp
thumbnails decode by bpp (`libgimp/gimpdrawable.c:152`, `_gimp_pixbuf_from_data`), so a CMYK u8
(4 bpp) thumbnail would be read as RGBA. Fix (INTERFACES, new §): "previews of CMYK
drawables/images are `R'G'B'A u8` (or `R'G'B' u8`) in **sRGB**. `gimp_view_renderer` uses the
sRGB profile when the viewable's base type is CMYK. PDB thumbnails therefore stay RGB." Owners:
03 (formats), 07 (renderer), 05 (PDB docs).

### M8. Legacy layer modes and the meaning of "trivial"

PLAN D6 lists Behind and Anti-erase as trivial. `GIMP_LAYER_MODE_FLAG_TRIVIAL` is set on
Normal-legacy, Dissolve, Normal, Erase, Merge, Split, Pass-through, Replace and Overwrite only
(`gimp-layer-modes.c`, flags at lines 64…841), and `gimp_layer_mode_get_format` keys on that flag
(`:1528`). The legacy ops override `process` and hard-code 4 components and `ALPHA`
(`layer-modes-legacy/*`). With the D6 rule ("CMYKA for all modes") a legacy Multiply layer in a
CMYK image would multiply inks generically, and the HSV/HSL legacy modes would index out of
range. Fix: define the CMYK-native set as "op class is not legacy and blend_function is NULL".
For legacy modes, return RGB format (accepting the I6 violation, documented) or make them
unavailable in CMYK images.

### M9. Text layers can never produce K-only text, and nobody owns app/text

Text is rendered with cairo in RGB and converted to the layer format, so text set in
K100 comes out as 4-color rich black, which misregisters in print. This is the most common
CMYK workflow complaint, and it is not mentioned. MVP: document the limitation in D13 and give
app/text an owner. Better (02, small): when the text has a solid color and no effects, render
the glyph coverage as an A8 mask and fill with the native CMYK `GeglColor`.

### M10. Test plan gaps (TESTING.md)

The mechanisms are real. `app/tests/meson.build` builds `app_tests` from `test-<name>.c` +
`tests.c` with `UI_TEST=yes`. The python tests run through `libgimp-run-python-test.py`
(`gimp-console -nis --batch-interpreter python-fu-eval -b <script> --quit`), and the
file-tiff/jpeg/psd plug-ins are built in `_build`. B3 is feasible headless. Missing or wrong:
* **No RGB regression net.** WS02 acceptance says "RGB/GRAY layer-mode results unchanged
  (existing tests)", but no such test exists. `app/operations/tests` is `build_by_default:
  false` and not registered with `test()`, and `test-core.c` has a single test (`:285`). Add a
  golden-checksum test of RGB/GRAY projections for every layer mode × composite mode, captured
  on the baseline **before** 02 touches `real_process` (09, first deliverable).
* Paint exactness (B2): for paintbrush, pencil, airbrush, eraser, clone, heal, smudge,
  convolve, dodge/burn, ink and MyPaint, pixels outside the dab footprint must stay
  bit-identical, at u16 and float.
* B1 tests (same-image paste, cross-session builtin, DnD), floating-selection anchor, merge
  down, flatten, rotate-90/flip/crop exactness. gegl:transform is CMYK-native,
  `transform-core.c:172, 1649-1661`.
* Export/import round-trips per format × {u8,u16} × {alpha, no alpha}: TIFF and PSD exact,
  JPEG u8 with tolerance. Also the K-only patch must stay K-only after the JPEG Adobe inversion.
* The negative input-only TIFF (B3 step 9) "skip if no such file" means the crash path
  goes untested. Commit a tiny 4×4 CMYK TIFF fixture with an embedded **GIMP-generated**
  input-only profile (no licensing issue), or generate it in the script via ctypes lcms + the
  TIFF exporter.
* T13: the expected value must be computed with **babl** (the op uses babl conversions:
  relcol+BPC through linear sRGB), not lcms, or the 2/255 tolerance will flake. T7's "PERCEPTUAL
  differs from RELCOL" can be false for some profiles; assert it only for a profile known to
  differ.
* T24 cannot flip a probe that is cached per process inside `test-cmyk` (one executable, all
  tests). Use a separate executable or an uncached test hook.
* Python cannot pass a babl space. `Gegl.Buffer.set/get` take a format **name**. B3 step 2 "babl
  CMYKA u16 with image space" is impossible from Python. The space-less `"CMYKA u16"` works only
  because babl treats CMYK(no space)↔CMYK(space) as identity **(measured)**. Write that down
  and add an assertion test, so a babl change cannot silently break it.
* Run the suite once at DOUBLE precision (M5) and once under valgrind/ASan if available.

### M11. Undo and the I1 invariant: the transient state during undo/redo

`GIMP_UNDO_IMAGE_TYPE` pop only sets `base-type` (`gimpimageundo.c:328-351`). The profile
comes back through a separate parasite undo, in reverse push order. Depending on 03's push
order there is a window with "base CMYK, no icc-profile parasite" or "base RGB, CMYK parasite".
PLAN D2 says `gimp_image_set_icc_profile(NULL)` on CMYK re-attaches the default. If that rule
lives at the parasite level (`gimp_image_parasite_detach` → `_gimp_image_update_color_profile`,
`gimpimage.c:4729/4762`), it fires inside an undo pop, attaches a profile with no undo, and
corrupts redo. Fix: enforce I1 only in the API entry points (`gimp_image_set_color_profile`,
PDB, XCF load end). In parasite/undo code, tolerate the transient: `gimp_image_get_layer_space()`
falls back to the builtin CMYK space when base is CMYK and the profile is NULL. TESTING T8
already checks the end state; add undo/redo cycling with group layers, masks and channels.

### M12. Plan-level: WS01 is the critical path and too big for "day one"; the dependency graph needs correcting

WS01 bundles the enums, gimp-babl, a profile generator (M4), lcms transform changes (M3, B1),
gimpimage, gimpimage-color-profile (including CMYK→CMYK convert and import policy) and
gimpdrawable. Split it:
* **01a** (lands first, small): enums + regen, gimp-babl formats and helpers,
  `gimp_babl_space_for_profile()` (B1.2), gimpimage formats/components/masks (I7),
  gimpdrawable, declarations and stubs of every INTERFACES item, and a minimal builtin profile
  (M4).
* **01b** (parallel with everything, merge before 03/06/07 acceptance): libgimpcolor lcms
  fixes (M3), same-profile short circuit (B1.1), final builtin generator (M4),
  gimpimage-color-profile convert/assign/import for CMYK.

Hidden couplings found: 02↔03 (group-layer convert, `gimp_gegl_convert_color_profile` in 02's
gimp-gegl-loops.cc used by 03), 03↔07 (preview contract M7), 02↔08 (mask-components for T15/T16;
the component editor's eye toggle is only testable with 02 merged), 05→06 (export options,
`gimp_layer_new` type checks in libgimp/gimplayer.c, "CMYK*" parsing), 01b→07 (display
transforms use the lcms CMYK path).

Corrected graph (→ means "needed to merge/accept"; every WS can *start* coding at 01a):
```
01a ──┬──────────────► 01b ───────────────┐
      ├─► 02 (compositing, paint, gegl) ──┼─► 03 (conversions)  ─┬─► 07 (UI)
      │                                   │                      │
      ├─► 04 (xcf; independent)           │                      ├─► 05 (pdb/libgimp) ─► 06 (file plug-ins)
      ├─► 08 (histogram/tools/pickers) ◄──┘ (needs 02 mask-comp.)│
      └─► 09 (tests; RGB golden test FIRST, then skip-gated CMYK tests)
```
Merge order: **01a → 01b → 02 → 03 → 04 → 05 → 08 → 07 → 06 → 09**. 02 moves ahead of 03
because 03's group-layer conversion and every CMYK projection test run through 02's code. 03's
T7-T11 do not need 02 except for group layers. 04, 08 (histogram part) and 09's golden test
are truly independent after 01a.

### M13. MyPaint brush violates "never touch ink values a user did not change"

`gimpmybrushsurface.c:368, 557` iterate the surface in `R'aG'aB'aA float` / `R'G'B'A float`
(no space), and writes go back over the whole touched region. Pixels inside the region but
outside the dab therefore go CMYK→RGB→CMYK. WS02 step 8 accepts the "RGB round trip". MVP:
make MyPaint Brush insensitive for CMYK drawables (08 owns app/tools), or restrict write-back
to pixels with non-zero dab coverage.

---

## Minor

* **m1 XCF (Q5).** Consistent. There is no `xcf_save_choose_format()` in the tree. The version
  comes only from `gimp_image_get_xcf_version()`, and the `default:` case is a
  `g_return_val_if_reached` reminder (`gimpimage.c:3480-3487`). Loader slots run 0..27
  (`xcf.c:69-99`), and v27 already maps to "GIMP 3.4" (`gimpimage.c:3474-3477`), so mapping 28
  → 340/"GIMP 3.4" is right. Older GIMP fails gracefully with "unsupported XCF file version 28"
  (`xcf.c:266-283`). Watch for upstream also claiming 28 during the 3.4 cycle (coordinate on
  rebase). WS04 must also gate the header check `*image_type > GIMP_INDEXED` (`xcf-load.c:350`)
  and the layer type switch (`:3881-3916`) on version ≥ 28, and validate precision with
  `gimp_babl_is_valid (GIMP_CMYK, p)`.
* **m2 I6 is exact only for U8/U16/HALF/FLOAT.** Layer modes run in float. **(measured)**
  `CMYK u32` 123456789 → float → 123456792, and DOUBLE loses precision. RGB has the same
  limitation today. State it in D6/I6 and keep T12/T13/T18 at u16 and float.
* **m3 babl one-way crash (Q4): confirmed.** **(measured)** `CGATS001Compat-v2-micro.icc`:
  CMYK→RGB works, RGB→CMYK segfaults. `cmsDoTransform` on a NULL `lcms_from_rgba`
  (`babl-fish-reference.c:901`; created unchecked at `babl-icc.c:1036-1038`). Also report it
  upstream (one-line NULL check). Note that one-way profiles are still fine for *viewing*, which
  could later allow read-only native loads.
* **m4 babl claims (Q4): confirmed.** K100 → sRGB' (0.137,0.122,0.125) → back
  (0.680,0.674,0.653,0.742); `RGBA float`(CMYK space) == linear sRGB; all CMYK(A) types exist;
  `CaMaYaKaA u32` is missing; `CaMaYaKaA double` **does** exist (D4 lists it as missing; the
  defensive registration handles that). Model flag `BABL_MODEL_FLAG_CMYK` = 0x1000000 on CMYKA.
* **m5 babl `MAX_SPACES = 100`** (`babl-space.c:19`), never freed. Each distinct CMYK profile
  (and each builtin regenerated with a new date, B1) uses one slot. Past 100, babl logs "too
  many BablSpaces" and returns NULL. One more reason for B1.2/B1.3.
* **m6 `gimp_image_new()` returning NULL** without CMYK support (INTERFACES §4): callers
  (`gimp_create_image`, `xcf-load.c:698`, PDB `image-new`) assume non-NULL. Gate at the callers
  (actions insensitive, PDB/XCF error) and keep `gimp_image_new` total.
* **m7 Histogram.** `HISTOGRAM_VALUE (channel, i)` indexes storage by the **enum value**
  (`gimphistogram.c:431, 465, 500`), and `has_channel` ALPHA tests `n_components == 2 || == 4`
  (`:554-556`). CYAN=7…BLACK=10 need an enum→slot map in INTERFACES §9 (VALUE 0, C..K 1..4, A 5,
  derived slots after). The enum appended after the `pdb-skip` value RGB=6 leaves a gap in
  libgimp's `GimpHistogramChannel`. That is fine, but say so.
* **m8 Component state across conversion.** `private->visible/active[MAX_CHANNELS]` persist
  across base-type changes (`gimpimage-private.h:138-139`). Index 3 is alpha in RGB but K in
  CMYK, so an RGB image with alpha hidden converts to CMYK with K hidden. Reset both on any
  base-type change. Existing bug: `gimp_image_get_visible_mask()` uses `visible[ALPHA]` for
  GRAY instead of `ALPHA_G` (`gimpimage.c:4076`).
* **m9 GimpComponentMask trick (I7): sound,** with notes. The flags GParamSpec of
  `gimp:mask-components` (`gimpoperationmaskcomponents.cc:98-104`) picks up the new bit through
  the enum type. The fast passthrough paths compare against `GIMP_COMPONENT_MASK_ALL`
  (`:434, 466`), so I7 is needed to keep RGB performance. Explicit `R|G|B|A` masks
  (`gimpdrawablefilter.c:2104-2107`) silently exclude K. The `Process<guint8>` 32-bit fast path
  and the bpp switch (4/8/16 only, `g_return_if_reached` otherwise) need 5/10/20/40-byte
  variants.
* **m10 Plug-in image types.** The parser is prefix-based (`gimppluginprocedure.c:1071-1121`):
  check `"CMYKA"` before `"CMYK*"` before `"CMYK"`. `"*"` excluding CMYK affects **menu
  sensitivity only** (`plug-in-actions.c:213-221`). PDB calls from scripts still hand CMYK images
  to RGB-only plug-ins (no type check on PDB calls). Document that.
* **m11 Channel semantics.** T17 makes a Cyan channel = ink amount (white = full ink). Photoshop
  shows ink as dark and "load channel as selection" selects low ink. Decide which convention
  GIMP follows and write it into D8.
* **m12 Color pickers.** Average mode converts through `RaGaBaA double`
  (`gimpimage-pick-color.c:165`, `gimppickable.c:84-98`), so averaged CMYK readouts are RGB
  round trips. Fix (08) by averaging in the native format for CMYK.
* **m13 GEGL is better than D13 assumes.** `gegl:transform` (`transform-core.c:172`, uses
  `camayakaA float`, and avoids the affine fast path for CMYK at `:1649-1661`) and Gaussian blur
  (`gblur-1d.c:1064-1077`) run natively on CMYK. So the transform tools and the common blur keep
  inks. Note it in D13 and use one of them in T18 alongside an RGB-only filter.
* **m14 Generated-file ownership.** `pdb/enums.pl` is produced by the `enums.pl` custom target
  (`pdb/meson.build:117-126`) and `gimpenums.c.tail` by `libgimp/meson.build:49`. The "regen
  commit" rule should name `app/plug-in/plug-in-enums.c` too.
* **m15 D3 resolution order** prefers the image's *simulation* profile for RGB→CMYK
  conversion. Many users set a simulation profile for soft-proofing, not as a conversion
  target. Fine, but the convert dialog must show which profile was chosen (07).
* **m16 BUILD-ENV/TESTING** do not mention that `GIMP_TESTING_PLUG_INS` in
  `app/tests/meson.build` is one `test_env` shared by all python tests (`:93-96`). Append
  tiff/jpeg/psd there once.

---

## Answers to the review questions (summary)

1. **Interfaces.** Enum values do not collide. Every addition is an append (base type 3, image
   types 6/7, channel types 6-9, export capability 1<<11, histogram 7-10 after a pdb-skip
   value). There is no ABI break, and no range-indexed arrays over `GimpImageType` were found.
   The `GimpComponentMask` trick works (m9). Wrong or missing: the §8 composite-space rule (B3),
   the paint-core op format (B2), the blend-function stride (M1), preview formats (M7),
   histogram slot mapping (m7), `gimp_image_new` NULL (m6), lcms u32/inverted (M3), and the
   same-profile short circuit (B1).
2. **Ownership.** Not complete (M6 table). Real overlaps: gimpgrouplayer.c 02/03,
   gimpimage.c 01/03, libgimpwidgets 07/08/05, test utils 04/09.
3. **Compositing.** `n_components` is generic in the composite and trivial-mode functions,
   and the SSE paths already fall back. The blend functions and the paint core are not generic
   (M1, B2). "Uncovered pixels bit-identical" is achievable for U8/U16/HALF/FLOAT (m2), once
   B1-B3 are fixed.
4. **babl/lcms.** Every claim was confirmed (m3, m4, M3). New findings: header-sensitive space
   identity and same-profile re-separation (B1), and reference-fish speed (M2).
5. **XCF.** v28 is consistent and degrades gracefully (m1).
6. **Builtin profile.** Realistic in about 300 LOC of lcms2 (`cmsCreateProfilePlaceholder`,
   `cmsStageAllocCLut16bit` + `cmsStageSampleCLut16bit`, identity `cmsStageAllocToneCurves`,
   `cmsWriteTag`/`cmsLinkTag`, `cmsSaveProfileToMem`, then fix the header). Use 9⁴/17³ grids
   (79 KB). Keep it, as a separate 01b task (M4).
7. **Tests.** The mechanisms are real and the e2e test is feasible headless; gaps are in M10.
8. **Parallelism.** Split 01. Corrected graph in M12.
9. **MVP.**
   * Cut from MVP: native PSD *export* (keep PSD import); CMYK-native blend candidates
     (already out); per-mode histogram polish. Make MyPaint brush and legacy modes
     unavailable on CMYK (M13, M8).
   * Missing, and needed for a usable MVP: same-image paste/DnD exactness (B1); painting (B2);
     flatten (B3); pickers without overflow (M5); previews (M7); performance (M2); text as a
     documented limitation (M9); FG/BG colors in the **image** CMYK space, not the simulation
     space (WS08 step 6 has this right; it avoids a B1-style re-separation on every dab).

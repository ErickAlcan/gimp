# WS02 — Compositing, filters application, fills, paint

**Goal**: in CMYK images every composite runs in `CMYKA float (image space)`: colour-agnostic
modes touch no color, blend modes blend in RGB/Lab through a fast lcms converter and composite
in CMYK, zero-coverage pixels stay bit-identical (I6), the paint core works on 5 components
(B2), the composite-space rule of B3 holds, and **RGB/GRAY output stays bit-identical**
(golden test G1–G3). Contract: INTERFACES §8 — read all of it first.

**Owned files** (PLAN §4.1): `app/operations/layer-modes/*` (+ new
`gimpoperationlayermode-cmyk.{c,h}`, `meson.build`), `app/operations/layer-modes-legacy/*`,
`app/operations/*` except 08's color-tool ops/configs, `app/gegl/*` except `gimp-babl*`,
`app/paint/*`, `app/core/gimpdrawablefilter.c`, `gimpfilloptions.c`, `gimpgrouplayer.c`,
`gimpgrouplayerundo.c`, `gimpimage-merge.c`,
`app/core/gimpdrawable-{bucket-fill,combine,edit,fill,filters,floating-selection,
foreground-extract,gradient,offset,operation,shadow,stroke,transform}.c`, the function
`gimp_layer_update_mode_node()` in `app/core/gimplayer.c`, `app/tests/test-cmyk-compositing.c`.

**Dependencies**: starts at the 01a merge. The converter's lcms path needs 01b (until then it
falls back to babl fishes: correct, slow). Merges third (after 01b). 03 and 08 depend on you.

**Steps** (run G1–G3 after every step; commit per step)
1. `gimp-layer-modes.c/.h`: `gimp_layer_mode_is_cmyk_native()`,
   `gimp_layer_mode_get_non_legacy()`, CMYK rule in `gimp_layer_mode_get_format()`,
   `gimp_layer_mode_get_operation_for_format()` (per (mode, CMYK format) instance with pad
   formats, `fixed_format`, GHashTable + GMutex, freed in `gimp_layer_modes_exit()`).
2. `GimpOperationLayerMode`: property `"cmyk-native"`, members of INTERFACES §8.3, the `cmyk`
   resolution in `prepare()` and `cache_fishes()` (CMYK branch: pads, `blend_format`,
   converter; AUTO passed to `get_format` when cmyk-native and non-legacy), legacy safety net
   (`g_warning` once). `gimp-gegl-nodes.c`: `gimp_gegl_mode_node_set_mode_full()` (5-arg
   variant = `cmyk_native = composite_space == AUTO`); `gimp_gegl_create_flatten_node (bg,
   format, composite_space)`; `gimp_gegl_apply_flatten()` passes the dest format;
   `gimpimage-merge.c:859` passes `gimp_image_get_layer_format (image, FALSE)`;
   `gimp_layer_update_mode_node()` uses `_full` with `cmyk_native = (gimp_layer_get_composite_space
   (layer) == AUTO)` (TRUE in the show-mask branch); `gimpapplicator.c` and
   `gimpdrawablefilter.c` pass their stored (unresolved) composite space;
   `gimpdrawablefilter.c:2104-2107` uses `GIMP_COMPONENT_MASK_ALL`.
3. **M1**: all 33 blend functions → `n_components = 4` and
   `gimp_operation_layer_mode_get_blend_format (op)`; `real_process` two-stride rewrite
   (INTERFACES §8.4) with sample-indexed runs. Commit this step alone; G1/G2 must be
   bit-identical.
4. **M2** converter `gimpoperationlayermode-cmyk.{c,h}` (INTERFACES §8.5): two
   `GimpColorTransform`s per space (`CMYKA u16` ⇄ `R'G'B'A u16` sRGB, relcol+BPC, no
   NOOPTIMIZE), babl for float↔u16 and u16↔blend format; babl-fish fallback.
5. **B2** paint: `DoLayerBlend` (`gimppaintcore-loops.cc:1903`) uses
   `gimp_layer_mode_get_operation_for_format (paint_mode, paint_buf_format)`; legacy paint modes
   mapped with `get_non_legacy()` for CMYK drawables in `gimpbrushcore.c`, `gimpink.c`,
   `gimppaintcore.c`, `gimpfilloptions.c`; MyPaint: `gimp_mybrush_core_start()`
   (`gimpmybrushcore.c:145`) refuses CMYK drawables with an error (M13); smudge
   (`gimpsmudge.c:296`, `gimp-gegl-loops.cc:630`) accumulates in `CMYKA float` (image space)
   for CMYK; heal (`gimpheal.c`) in the drawable's float format; `gimp_gegl_convolve()`
   `total[MAX_CHANNELS]` (`gimp-gegl-loops.cc:305,356`); `gimp_gegl_average_color()` native
   average for CMYK; clone/perspective/dodgeburn/convolve audit.
6. `gimp:mask-components` (`gimpoperationmaskcomponents.cc`): CMYK(A) u8…double, bit map of
   INTERFACES §1, 5/10/20/40-byte variants of the fast paths (`:434, :466`, bpp switch).
7. Groups/merge: `gimpgrouplayer.c` projection format switch (`:1147-1160`) CMYK;
   `gimp_group_layer_convert_type()` (`:1168-1196`) reads `_gimp_image_get_conversion_intent()`
   (on 03's behalf); `gimpgrouplayerundo.c` base type; flatten/merge with CMYK background.
8. Fills, gradients, bucket fill, stroke, floating selection, transform, offset: verify formats
   come from `gimp_fill_options_get_format()` / drawable formats; gradient renders RGB
   (documented). `app/operations` others (gradient, semi-flatten, set-alpha, profile-transform,
   fill-source): verify with CMYK input (PLAN §5 rows).
9. Tests C1–C13 and gated P1/P2 in `test-cmyk-compositing.c`; Tier A (#1, #2, #2b) on owned files.

**Acceptance**
* **G1–G3 green** (RGB/GRAY bit-identical), C1–C13 pass at u16 and float (and in 09's DOUBLE
  run without crashes).
* **P1** ≤ 1.5 s and **P2** ≤ 1.5× RGB with 01b merged (`GIMP_TESTING_PERF=1`, GEGL threads 2;
  record numbers in Status). Without 01b, record the babl-fallback numbers.
* No new warnings/criticals in the test logs; MyPaint refused on CMYK.

**Risks**: stride bugs in `real_process` (mitigated by G1 + C3); converter thread safety (lcms
transforms are already shared across threads by `gimp_gegl_convert_color_profile`); per-format
op instances must not leak per stroke (cache, never per call); the `opacity` field of shared
paint instances is written per stroke (existing behaviour); GEGL ops that hard-code
`babl_format ("RGBA float")` without space on CMYK data (grep).

**Status**: implemented on `ws/02` (branch point `cmyk-01a-merged`). `meson test`: 27 OK,
3 SKIP (cmyk-image, cmyk-histogram, babl probe: other owners), 1 FAIL (`appdata_file`, network).
`layer-modes-golden` (G1-G3) green after every commit. Build warning-free in touched files
(`-Wswitch` at `gimpgrouplayer.c:1152` resolved).

What landed (INTERFACES §8, notes in §8.7)
* `gimp-layer-modes.c/.h`: `gimp_layer_mode_is_cmyk_native()`, `gimp_layer_mode_get_non_legacy()`
  (OVERLAY_LEGACY -> SOFTLIGHT, it runs the soft-light-legacy op), CMYK rule in
  `gimp_layer_mode_get_format()`, `gimp_layer_mode_get_operation_for_format()` (hash + mutex,
  freed in `gimp_layer_modes_exit()` together with the converters).
* `GimpOperationLayerMode`: `"cmyk-native"`, §8.3 members, CMYK resolution in `cache_fishes()`
  (called from `prepare()` and `real_process()`), legacy safety net (`g_warning` once), two-stride
  `real_process` (CMYK branch only; RGB code path textually unchanged), 33 blend functions on 4
  components via `gimp_operation_layer_mode_get_blend_format()`/`_get_blend_babl_space()`.
* Converter `gimpoperationlayermode-cmyk.{c,h}`: two `GimpColorTransform`s per space (CMYKA u16
  <-> R'G'B'A u16 sRGB, relcol+BPC, no NOOPTIMIZE), 512-pixel stack chunks, alpha copied exactly,
  babl-fish fallback.
* `gimp-gegl-nodes`: `set_mode_full()`, `create_flatten_node (bg, format, space)`;
  `apply_flatten()` passes the dest format; `gimpimage-merge.c` passes the layer format;
  `gimp_layer_update_mode_node()` passes `cmyk_native` from the stored composite space (TRUE when
  showing the mask); applicator/drawable filters keep the 4-arg call with their stored space;
  drawable filter affect uses `GIMP_COMPONENT_MASK_ALL`.
* Paint: `DoLayerBlend` per-format instance (B2); legacy paint/fill modes mapped in
  `gimp_paint_core_get_paint_buffer/paste()`, brush/ink paint buffers, `gimp_fill_options_get_format()`,
  `gimp_drawable_real_apply_buffer()`; MyPaint refuses CMYK ("MyPaint Brush does not support CMYK
  layers"); smudge accumulates in CMYKA float (separate CMYK loop, RGB loop untouched); heal in
  CMYKA float; convolve `total[MAX_CHANNELS]`; `gimp_gegl_average_color()` averages CMYK natively
  (CaMaYaKaA float, buffer space); `gimp_gegl_apply_mask()` keeps CMYK inks.
* `gimp:mask-components`: CMYK(A) u8/u16/u32/half/float/double, bit map of §1, 5/10/20/40 bpp,
  CMYK input/aux never processed as RGB; `alpha_value` widened to 64 bit.
* CMYK-native: set-alpha, threshold-alpha, semi-flatten, compose-crop. Gradient renders RGB
  (documented); profile-transform/fill-source/offset take caller formats (OK).
* Groups: projection format `CMYKA <precision>` (image space). `gimp_group_layer_convert_type()`
  does not color-convert pixels (the projection is re-rendered from the children, which 03
  converts with the conversion intent), so the intent needs no handling there (comment added).
  `gimpgrouplayerundo.c`: generic, no change needed.
* `gimpdrawable-fill.c`: opaque-alpha fill of CMYK colors without `gimp_color_set_alpha()`.
* `gimp_gegl_convert_color_profile()`: no babl fallback copy to an input-only CMYK profile (01b).

Acceptance (`test-cmyk-compositing.c`)
* C1, C3, C9 at u16 + float (+ knob): pass; knob runs u8, half, double pass (U32/DOUBLE compare
  at float, I6 scope). C2 (max 0.02/255 vs clipped babl reference), C6, C7, C10 (<= 0.02/255),
  C11, C12, C13 (native (0.5,0,0,0.5) vs RGB_LINEAR (0.376,0.029,0.051,0.300)), group layers,
  edit-clear-with-selection: pass. No warnings/criticals in the log.
* **Pending integration**: C4, C5, C8 and the C9 "centre == FG" check skip because
  `gimp_color_set_alpha()` (libgimpcolor/gimpcolor.c, used by GimpContext FG/BG) round-trips
  through RGB and re-separates CMYK colors. With a local CMYK-safe `gimp_color_set_alpha()`
  (not committed) all of them pass.
* P1/P2 (`GIMP_TESTING_PERF=report`, threads 2, without 01b = babl fallback): P1 9.38 s;
  P2 CMYK 0.543 s vs RGB 0.306 s = 1.78x. P2 is dominated by babl's lack of a fast path for
  `CMYK u16 (space) -> CMYKA float (space)` (0.39 s / 4 Mpx vs 0.03 s for R'G'B'), which a
  space-less `babl_conversion_new()` fixes but a space-tagged one does not. P1 with 01b's lcms
  path is estimated at ~1-1.5 s (REVIEW M2 rates); must be re-measured after the 01b merge.
* Tier A: base-type switches: only `gimpgrouplayer.c` (fixed). `[4]`/RGBA hits in owned files are
  explicit RGB formats (gradient, legacy HSV/HSL, MyPaint, semi-flatten RGB branch, smudge RGB
  branch, heal SSE v4sf, `diff_rects[4]`) or fixed (convolve, smudge, heal, average, apply_mask,
  set/threshold-alpha, compose-crop, mask-components). #2b: `gimpsmudge.c` pixel now
  `MAX_CHANNELS` in the accum format; `gimp-gegl-loops.cc` brush color `MAX_CHANNELS` (CMYK
  path), RGB `[4]` with explicit RGBA float; gradient/semi-flatten/HSV with explicit RGB formats.

Requests for other owners
* libgimpcolor `gimp_color_set_alpha()`: for CMYK formats set alpha in "CMYKA double" (format's
  space) instead of the RGB round trip (unblocks C4/C5/C8, exact FG/BG fills and paint).
* `gimp-babl.c` (01a/01b) or babl upstream: fast CMYK(A) u8/u16 <-> CMYKA float conversions for
  space-tagged formats (P2).


**Integration re-run** (ws/02 fast-forwarded to cmyk-mode 808c81a: all workstreams incl. 06;
01b's lcms CMYK path and the `gimp_color_set_alpha()` fix live)
* C2/C10 now compare against an independent implementation of the §8.5 design (test-side
  `GimpColorTransform`s, image profile <-> sRGB, relcol + BPC, 16 bit) instead of babl: C2 max
  0.00/65535 (assert <= 1.5 u16 steps), C10 0.00/65535 (assert <= 1e-6) for RGBA, R'G'B'A and
  R~G~B~A float. Informational drift of the converter against babl's own CMYK conversion:
  <= 0.04/255 in RGB; the old C2 failure (12/255) was babl vs lcms disagreeing on the RGB -> CMYK
  re-separation of the final profile, not a compositing error.
* `cmyk-compositing`: all 18 tests pass, including the previously skipped C4, C5, C8 and the C9
  "centre == FG" check; knob runs u8, half, double pass. No warnings/criticals.
* `layer-modes-golden`, `cmyk-image`, `xcf`: OK (cmyk-image skips only I1's optional
  `GIMP_TESTING_CMYK_ICC` variant).
* Full suite: 28 OK, 1 SKIP (babl probe), 2 FAIL: `appdata_file` (network) and
  `cmyk-file-formats` (06: E3 "JPEG export" fails, libjpeg "Bogus marker length"; not in 02 code).
* **P1 (lcms path): 1.247 s** (target <= 1.5 s, met).
* **P2: CMYK 0.269 s vs RGB 0.148 s = 1.83x** (target 1.5x, missed). Remaining cost: babl has no
  fast path for space-tagged `CMYK(A) u16 <-> CMYKA float` (microbenchmark, 4 Mpx, one thread:
  `CMYK u16 -> CMYKA float` 0.39 s, `CMYKA float -> CMYKA u16` 0.18 s, vs `R'G'B' u16 -> R'G'B'A
  float` 0.03 s); the Normal composite itself (generic 5-component loop) is minor. A
  `babl_conversion_new()` for the space-less formats drops the first to 0.04 s, but babl does not
  use it for formats tagged with a CMYK space. Fix belongs in babl (or a gimp-babl registration
  hook per CMYK space if babl allows one).

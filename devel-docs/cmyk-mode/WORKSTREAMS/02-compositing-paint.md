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

**Status**: (agent fills in)

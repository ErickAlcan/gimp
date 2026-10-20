# WS02 — Compositing, filters application, fills, paint

Goal: CMYK layers composite natively in CMYKA float; non-trivial modes blend in RGB and
composite in CMYK; masks/affect/visibility work with 5 components; painting tools operate on
CMYK drawables without touching uncovered pixels.

Owned files: PLAN §3 (02): app/operations/layer-modes/*, gimpoperationmaskcomponents.*,
app/gegl/* (except gimp-babl*), gimpdrawablefilter.c, gimpfilloptions.c, gimpgrouplayer.c,
gimpimage-merge.c, app/core/gimpdrawable-*.c (except -histogram, -preview), app/paint/*.

Steps
1. `gimp_layer_mode_get_format()`: CMYKA float for CMYK preferred format (INTERFACES §8).
2. `GimpOperationLayerMode::prepare/cache_fishes`: detect CMYK on input or aux; composite format
   CMYKA float(space); composite→blend fish CMYKA→RGBA/Lab (space), blend→composite fish back;
   allocate blend buffers with blend n_components (4) separate from composite (5); skip blend
   conversions for trivial modes; ensure zero-coverage pixels copy input untouched.
3. Verify every composite function and trivial op (normal, behind, erase, anti-erase, dissolve,
   replace, merge, split, overwrite, pass-through) is n_components generic; guard SSE2/SSE4
   normal (fallback to C when n != 4).
4. mask-components: CMYK(A) formats (u8..double), bit mapping (C,M,Y bits 0–2, K bit 4, A bit 3).
5. gimpdrawablefilter.c: use `GIMP_COMPONENT_MASK_ALL`, not RGBA bits; check applicator paths.
6. gimpgrouplayer.c projection format CMYK; gimpimage-merge.c flatten with CMYK background.
7. Fill/gradient/bucket fill: verify formats flow from `gimp_fill_options_get_format`; gradient
   renders RGB then converts (acceptable).
8. app/paint: heal → work in drawable-native float format (`CMYKA float` for CMYK) instead of
   R'G'B'A float; smudge accumulation in CMYKA float for CMYK; mybrush surface RGB round trip +
   component mask bit for BLACK; paintcore mask/affect with new bit; clone/perspective/convolve/
   dodgeburn audit.
9. Tier A grep audits on owned dirs; local tests for T12–T16, T18, T19 (the official test file is
   09's; coordinate test names).

Acceptance: T12, T13, T14, T15, T16, T18, T19 pass; RGB/GRAY layer-mode results unchanged
(existing tests); paint on CMYK leaves pixels outside dabs bit-identical.

Risks: stride bugs in real_process; performance of reference-fish conversions in blend modes;
SSE dispatch; ops that call `babl_format("RGBA float")` without space on CMYK data.

Status: (to be filled)

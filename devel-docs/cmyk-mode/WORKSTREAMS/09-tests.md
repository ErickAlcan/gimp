# WS09 — Integration tests and test infrastructure

**Goal**: shared test helpers, the babl probe, the no-support executable, the end-to-end test,
the DOUBLE-precision and ASan runs, and the final integration report. (Per-area tests belong
to their workstreams; the RGB golden test is 01a's.)

**Owned files** (after the 01a merge): `app/tests/meson.build`,
`app/tests/gimp-app-test-utils.{c,h}`, `app/tests/test-cmyk-e2e.py`,
`app/tests/test-cmyk-nosupport.c`, `app/tests/cmyk-babl-probe.py`, new `app/tests/files/*`,
and every existing `app/tests/*` file not owned by another WS (`test-core.c`,
`test-save-and-export.py`, …).

**Dependencies**: starts at the 01a merge; e2e acceptance needs everything. Merges last
(probe and no-support test may go earlier as a test-only merge with the coordinator's OK).
The 04 agent may take this workstream once 04 is merged.

**Steps**
1. `cmyk-babl-probe.py` (Tier A #4; ctypes; never feed an input-only profile to RGB→CMYK).
2. Extend `gimp-app-test-utils` on request of other WS (they ask in their Status section; you
   add and merge helpers quickly; until then they keep `static` copies).
3. `test-cmyk-nosupport.c` (TESTING N).
4. `test-cmyk-e2e.py` (TESTING Z), using format names only (I5).
5. Runs: full suite with `GIMP_TESTING_CMYK_PRECISION=double`; optional ASan build in
   `_build-asan`; real-profile run with `GIMP_TESTING_CMYK_ICC` = the two local profiles;
   record results and any failures (with owner) in Status.
6. As workstreams merge, remove `g_test_skip` gates that are still present in their files only
   if the owner forgot (tell them).

**Acceptance**: TESTING "Acceptance for the whole feature" holds on the final `cmyk-mode`.

**Risks**: 90 s Python test timeout (keep images small); tolerance choices; flaky timing in
P1/P2 (they are gated and not part of the default run).

**Status**: see below.

## Status

Done on `ws/09` (branched from `cmyk-all-merged`). Final full suite
(`meson test -C _build --no-rebuild --num-processes 1`): **32 tests, 31 OK, 1 FAIL
(`appdata_file`, network = baseline), 0 SKIP**. G1–G3 green on every commit. Consolidated
MVP status, test matrix and limitations: `devel-docs/cmyk-mode/STATUS.md`.

**1. Un-gating (all pending-integration tests now run strictly)**
* `test-cmyk-image.c` (03): removed the WS01b `"(stub)"` description probe, the WS02 weak
  `gimp_layer_mode_is_cmyk_native` reference and the WS04 skip. Now run: I1/I3/I11 lcms
  comparisons, I2 with a group layer, I6, I9/I10 copy-of-selection pastes and floating anchor,
  I10 open-as-layers via XCF (asserts), I13 legacy group layer.
* `test-cmyk-compositing.c` (02): `SKIP_WITHOUT_EXACT_CONTEXT_COLORS` → an assertion (C4, C5,
  C8, C9 "centre == FG").
* `test-cmyk-histogram.c` (08): H5 FG-through-context fill is strict (layer reset to paper
  first, so the fill is what is measured) instead of `g_test_incomplete`.
* `test-xcf.c` (04): `/gimp-xcf/cmyk_round_trip_group` runs directly (subprocess probe removed).
* `test-cmyk-file-formats.py` (06): `HAVE_WS05`/`pending()` removed; E6, CMYKA layer alpha,
  E7's lcms availability and image creation are assertions.
* `libgimp/tests/test-cmyk.py`, `test-export-options.c` (05): every `SKIP (pending
  integration)` is now an assertion (thumbnails, conversions, export-options RGB/gray copies,
  legacy-mode mapping, Gimp.Image.new(CMYK)).
* Integration bugs found by un-gating:
  * `cmyk-image` failed with `GIMP_TESTING_CMYK_PRECISION=double` (I9 copy: low mantissa bytes
    differ): copy-of-selection and anchoring go through layer-mode compositing, which runs at
    float for every precision (RGB too). Test-side fix (722587e): those paths compare
    bit-identically in `CMYKA float` for DOUBLE/U32, byte-exact otherwise.

**2. Babl probe and no-support tests**
* `cmyk-babl-probe.py` (Tier A #4): 95 ctypes checks — CMYK(A) for u8/u16/u32/half/float/double
  (components, bpp, CMYK model flag, alpha), cmyk/cmykA/CaMaYaKaA/camayakaA models,
  `CaMaYaKaA double` exists (u32 reported missing), builtin space is CMYK, I5 identities
  (CMYK u8/u16/float without ↔ with space bit-identical on 1000 random pixels; `R'G'B'A float`
  and `Y' float` tagged with the CMYK space = sRGB within 1e-6), lcms discriminator
  (R' 0.534 space vs 0.735 naive), K100 round trip (0.0092, 0.0387, 0.0306, 0.9940), I8 facts
  (same bytes → same space; header variant → different space, same colorimetry). Never
  creates an input-only profile. Exit 77 only if libbabl/libgimpcolor cannot be loaded.
* `test-cmyk-nosupport.c` (N): N1 support FALSE; N2 PDB `gimp-image-new` and
  `gimp-image-new-with-precision` with CMYK → `GIMP_PDB_CALLING_ERROR` + "CMYK images are not
  supported…" (RGB works, no image created); N3 `gimp-image-convert-cmyk` fails, image stays
  RGB; N4 (04's request) XCF load of a minimal v28 CMYK file → `G_FILE_ERROR_NOSYS`, no
  "corrupt" text, no image, while the same file with base type RGB loads. Criticals are fatal.

**3. End-to-end test Z** (`test-cmyk-e2e.py`, no skips): new CMYK u16 image (first
`GIMP_TESTING_CMYK_ICC` profile or builtin) with K/C/rich-black/50 % patches → paintbrush
stroke (Clipboard Mask brush, see notes) and bucket fill inside a selection with an
image-space K100 FG (context keeps the inks; every stroke pixel K-only, centre K100 exact, 138
antialiased K-only pixels) → Multiply layer (offset, alpha) and K100 text layer (every covered
pixel K100) → projection invariants (K and C patches bit-identical, text area K-only, Multiply
changes its quarter) → XCF v28 round trip (base type, precision, profile bytes, names, modes,
opacity, offsets, every layer and the projection bit-identical, text layer kept) → native
multi-page TIFF (pages exact) and TIFF of a flattened duplicate (== projection) → native PSD
(layers, modes, offsets, pixels and projection bit-identical) → `convert_rgb` (every non-text
layer == lcms perceptual + BPC to the builtin sRGB: max diff 0/65535; text coverage kept) →
`convert_cmyk (profile, RELCOL, BPC)` (== lcms chain, max diff 0/65535; K-only pixels exactly
where the chain has them: none, K100 → (2738, 2907, 2286, 63202)) → paste of the original
K-only patch: bit-identical K100, rest untouched, K-only set = chain ∪ patch.
Passes with the builtin, `default_cmyk.icc` and `FOGRA39L_coated.icc`.
* Integration bug found and fixed (db1645f, **owner WS06**, `plug-ins/file-psd/psd-load.c`):
  the PSD loader set RGB_PERCEPTUAL blend/composite spaces on every layer (upstream FIXME),
  so a reloaded CMYK PSD composited through RGB (K100 → (604, 2534, 2008, 65145) in the
  projection although each layer was exact). CMYK layers and clipping groups now stay on AUTO.

**4. UI test** (`test-cmyk-ui.c`, adopted from 07): registered as app test `cmyk-ui` (Xvfb +
dbus-run-session via `UI_TEST`; app tests now set `NO_AT_BRIDGE=1`). The local-03 preview
patch is gone and the previews test always runs; added `mode_cmyk_convert` (Image ▸ Mode ▸
CMYK, OK → CMYK image, CMYK profile and projection, mode actions follow). 10/10 pass, also
with FOGRA39 (soft-proofing default branch) and every precision knob. The shutdown critical
(`gimp_device_info_save_tool`) was test-side: without a window manager no window gets the
focus, "focused-once" is never emitted and the device manager has no current device;
`tests.c` now calls `gimp_set_focused_once()` (025ddfa).

**5. Python runners**: every `app/tests` and `libgimp/tests` Python/C test goes through
`libgimp-run-python-test.py`/`libgimp-run-c-test.py` → `pygimp/run.py` (05's crash
detection). Verified: a passing script exits 0, a failing `gimp_assert` and a plug-in
SIGSEGV both fail (exit 64, "TEST FAILED"). Nothing to change; `cmyk-babl-probe` is plain
Python and does not start GIMP.

**6. Runs** (all OK unless stated)
| run | suites | result |
|---|---|---|
| default | full suite | 31 OK, 1 FAIL (appdata_file) |
| `GIMP_TESTING_CMYK_PRECISION=u8`, `half`, `double`, `u16`, `float` | golden, foundation, compositing, image, histogram, nosupport, ui, xcf | all OK (double OK after 722587e) |
| `GIMP_TESTING_CMYK_ICC=default_cmyk.icc`, `=FOGRA39L_coated.icc`, both orders | foundation, compositing, image, histogram, ui, xcf, e2e, file-formats | all OK (I1 real-profile case runs) |
| double + FOGRA39 | foundation, compositing, image, histogram, xcf | all OK |
| `GIMP_TESTING_NO_CMYK=1` | cmyk-nosupport | OK |
| ASan (`_build-asan`, detect_leaks=0), default, double, both ICCs | core, golden, foundation, compositing, image, histogram, nosupport, ui, xcf | all OK, no ASan report (H2 included) |
| `GIMP_TESTING_PERF=report` | compositing P1/P2 | P1 1.04–1.31 s (met); P2 CMYK 0.61–0.67 s vs RGB 0.13–0.17 s = 3.8–4.7× (missed); pinned to one core 0.253 / 0.133 = 1.90× |

**Requests / open items for other owners**
* **02** (P2): the CMYK Normal projection is ~2.5× slower with two GEGL threads on two cores
  than on one core (RGB is not); 02's earlier 1.83× matches the one-core figure. Look for
  contention in the CMYK path (per-call `cache_fishes()`/`babl_fish()`, `format_ops_mutex`,
  `converters_mutex`, babl internals). Plus the babl fast path for CMYK u16 ↔ float.
* **06**: review db1645f (PSD loader AUTO spaces for CMYK); PSD export of CMYK layers keeps
  writing their (now AUTO) spaces as before.
* **tools/in-build-gimp.py** (unowned): the test session has no brush/dynamics data and a
  writable data folder outside the search path (creating a generated brush criticals).
* Upstream (not CMYK): multi-page TIFF reload duplicates text layers ("Km #1"); libgimp
  copy/paste/anchor leaves "2 GeglBuffers leaked" at exit.

**Deviations**: none from INTERFACES.md. Test-file ownership: WS09 edited test files of other
workstreams (`test-cmyk-image.c`, `-compositing.c`, `-histogram.c`, `test-xcf.c`,
`test-cmyk-file-formats.py`, `libgimp/tests/*`) as the integration owner of `app/tests/*` and
`libgimp/tests/*`; the only non-test change is db1645f (WS06's file, own commit).

**Final round (on `cmyk-final`, merged fast-forward into ws/09)**
* Full suite 32: 31 OK, 1 FAIL (`appdata_file`), 0 SKIP. Precision knobs u8/half/double/
  u16/float, both real profiles, double + FOGRA39, NO_CMYK and ASan (default, double, half,
  both profiles) all OK after 75d46b5.
* 75d46b5 (**owner 02**): the native CMYK read path (`aux_native_supported()`) and the
  validate handler (`cmyk_render_format()`) took CMYK half for u16 (2 bytes per component);
  with `GIMP_TESTING_CMYK_PRECISION=half` C1 and I9 failed on `cmyk-final`. The component type
  is now checked.
* `pygimp/run.py` no longer matches crash markers in the echoed test script (06's report).
* P1 0.50–0.53 s, P2 CMYK 0.13 s vs RGB 0.155 s = 0.82–0.86× (2 threads). STATUS.md has the
  test matrix, the REVIEW-02 disposition table and the upstream estimate.

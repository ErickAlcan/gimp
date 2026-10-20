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

**Status**: (agent fills in)

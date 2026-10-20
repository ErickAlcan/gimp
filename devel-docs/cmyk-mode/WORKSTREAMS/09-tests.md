# WS09 — Tests

Goal: implement TESTING.md B1, B3 (and B4), the Tier A babl probe, and keep them green.

Owned files: app/tests/* except test-xcf.c (new `test-cmyk.c`, `test-cmyk-e2e.py`,
`cmyk-babl-probe.py`, `meson.build`, `gimp-app-test-utils.{c,h}` helpers), libgimp/tests/*.

Steps
1. `cmyk-babl-probe.py` (Tier A, ctypes; never use input-only profiles for RGB→CMYK).
2. Test helpers: generate builtin and input-only CMYK profiles via lcms; patch filling/reading
   helpers with explicit formats; lcms reference transforms.
3. `test-cmyk.c` T1–T24, registered in `app_tests` (keep run order; is_parallel false).
4. `test-cmyk-e2e.py` registered in `tests` with plug-ins env.
5. Optional `libgimp/tests/test-cmyk.{py,c}`.
6. Initially mark tests whose implementing WS has not merged with `g_test_skip("needs WSnn")`
   gated on a feature check, so 09 can merge early; remove skips as workstreams land.

Acceptance: all B1/B3 tests pass on final `cmyk-mode`; with `GIMP_TESTING_CMYK_ICC` set to the
two local real profiles too.

Risks: flaky timing in python tests (90 s timeout — use small images); tolerance choices.

Status: (to be filled)

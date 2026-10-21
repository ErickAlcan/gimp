# CMYK mode — MVP status

Consolidated status of the native CMYK image mode on the final tree: tag `cmyk-final`
(all workstreams, REVIEW-02 and the post-review fixes) merged into `ws/09`, plus WS09's
final fixes (the half-precision fix 75d46b5 and the runner fix below). Details live in each
`WORKSTREAMS/<ws>.md` Status section and in `REVIEW-02.md`; this file is the summary.

Box: 2 cores, GIMP 3.3.1 dev, babl/GEGL master in `/opt/gimp-deps` (babl with lcms).

## 1. What works

**Images and color management**
* `GIMP_CMYK` base type (enum value 3), image types `CMYK(A)_IMAGE` (6/7), channel types
  CYAN..BLACK (6–9), component masks with BLACK; CMYK(A) u8/u16/u32/half/float/double buffers
  always tagged with the image's CMYK babl space; only non-linear precisions (01a).
* Builtin CMYK profile "GIMP built-in CMYK (generic, not for press)", 79 432 bytes, public
  domain, deterministic (SHA-256 `864a65a0…bda9307`); one babl space per *equal* profile
  (header variants share it, I8) so same-profile copies are exact re-encodings (01b).
* Default CMYK profile order: soft-proofing profile → preference → builtin; one-way (input-only)
  profiles refused everywhere with a message (01b, 03, 05, 07).
* lcms CMYK transforms with intent/BPC; CMYK formats map to integer lcms types (u16 for
  non-integer types); `can_gegl_copy()` only for equal profiles (01b).
* Conversions RGB/GRAY/INDEXED → CMYK and CMYK → RGB/GRAY with profile, intent and BPC, one undo
  group, legacy layer modes mapped, linear precisions moved to non-linear; assign/convert CMYK →
  CMYK; templates (03, 01b).
* Copy/paste, DnD, open-as-layers, floating selections, geometry transforms: bit-identical inks
  between images with equal profiles (03, I9/I10/I12).

**Compositing and painting (02)**
* Every non-legacy layer mode composites natively in CMYK (per-ink lerp with the layer's
  coverage): color-agnostic modes (Normal, Dissolve, Behind, Erase, Replace, Anti-erase, Merge,
  Split, Pass-through) are exact and keep K-only K-only; the other blend modes (Multiply, Screen,
  Overlay, …) compute the blend through a per-space lcms converter (CMYK ↔ sRGB relcol + BPC)
  and composite natively; uncovered pixels stay bit-identical (C1, C3); explicit RGB composite
  spaces are honoured (C13).
* Paint core exact for an image-space FG: paintbrush, pencil, airbrush, eraser, clone, heal,
  smudge, convolve, ink; fills, bucket fill, flatten, merge, anchor, filters with masks,
  `gimp:mask-components` for all CMYK types.

**Channels, histogram, pickers, text, UI (07, 08)**
* Channels dialog C/M/Y/K/Alpha, histogram VALUE/C/M/Y/K/A, pickers and sample points native
  (MAX_CHANNELS buffers), select-by-color, CMYK color selector in the image space, K-only text
  (solid color, no markup), Levels/Curves/Threshold through an RGB conversion with a notice.
* Image ▸ Mode ▸ CMYK… dialog (profile, why it is the default, intent, BPC), assign/convert
  dialogs refuse one-way profiles, template editor "CMYK color", legacy modes hidden in mode
  menus of CMYK images, title/properties "CMYK color", previews in sRGB, display transform
  (managed and unmanaged) and soft-proofing path unchanged.

**Files, PDB, libgimp (04, 05, 06)**
* XCF version 28 for CMYK images (RGB/GRAY keep their version), all precisions, groups,
  masks, channels, selection, simulation profile; without CMYK support a clear
  `G_FILE_ERROR_NOSYS` instead of "corrupt" (04).
* PDB `gimp-image-convert-cmyk` (wrapped by `Gimp.Image.convert_cmyk()`), `gimp-drawable-is-cmyk`,
  CMYK rules in image/layer creation, components, histogram channels, export options
  ("Convert to RGB/Grayscale" for exporters without `CAN_HANDLE_CMYK`), thumbnails sRGB,
  `"CMYK*"` image-type token, crash-detecting test runners (05).
* Native CMYK TIFF (multi-page, alpha associated/unassociated), PSD (layers, masks, 8/16 bit)
  and JPEG load/export with embedded profile; decompose/tile CMYK-aware (06).

## 2. Test matrix (final tree, `ws/09` at the commit carrying this file)

Full suite: `meson test -C _build --no-rebuild --num-processes 1` → **32 suites: 31 OK,
1 FAIL, 0 SKIP**. The failure is `gimp:desktop / appdata_file` (needs network; baseline).
Inside the TAP suites the only skips are the gated P1/P2 (`GIMP_TESTING_PERF`) and I1's
optional real-profile case when `GIMP_TESTING_CMYK_ICC` is unset.

| suite | ids | default | u8 | half | double | u16 / float | default_cmyk.icc | FOGRA39 | both ICCs | double + FOGRA39 | ASan² |
|---|---|---|---|---|---|---|---|---|---|---|---|
| layer-modes-golden | G1–G3 | OK | OK | OK | OK | OK | – | – | – | – | OK |
| cmyk-foundation | F1–F13, F12b | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| cmyk-compositing | C1–C13 (+P gated) | OK | OK | OK¹ | OK | OK | OK | OK | OK | OK | OK |
| cmyk-image | I1–I15 | OK | OK | OK¹ | OK | OK | OK | OK | OK | OK | OK |
| cmyk-histogram | H1–H6 | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| cmyk-nosupport | N1–N4 | OK | OK | OK | OK | OK | – | – | – | – | OK |
| cmyk-ui | UI (10 tests) | OK | OK | OK | OK | OK | OK | OK | OK | – | OK |
| xcf | X1–X4 + legacy | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| cmyk-e2e | Z | OK | – | – | – | – | OK | OK | OK | – | – |
| cmyk-file-formats | E1–E7 + hostile inputs | OK | – | – | – | – | OK | OK | OK | – | – |
| cmyk-babl-probe | Tier A #4 | OK | – | – | – | – | – | – | – | – | – |
| libgimp python3 cmyk | L | OK | – | – | – | – | – | – | – | – | – |
| libgimp C export-options | L (C) | OK | – | – | – | – | – | – | – | – | – |
| other suites | core, gimpidtable, app-config, validate-desktop, save-and-export, libgimp ×13 | OK | | | | | | | | | core OK |

¹ On `cmyk-final` itself, `half` failed C1 (uncovered pixels not bit-identical) and I9
(floating): the new native CMYK read/render paths of 02 (10109bd) recognised u16 by "2 bytes
per component", so CMYK half layers and projection tiles were read/quantized as u16 integers.
Fixed in 75d46b5 (component type checked too; owner 02).
² `_build-asan` (`-Db_sanitize=address`, `detect_leaks=0`): the C suites at the default
precision, `double`, `half` and with both real profiles; no AddressSanitizer report (H2
pickers on CMYKA double included). The Python suites cannot run against an ASan build
(GObject introspection is disabled with sanitizers).
`GIMP_TESTING_NO_CMYK=1` on `cmyk-nosupport`: OK.

**Performance** (`GIMP_TESTING_PERF=report`, 2048² u16, this box, 2 cores):

| | GEGL threads 2 (default run) | pinned to one core (`taskset -c 0`)³ | target |
|---|---|---|---|
| P1 CMYK Multiply projection | 0.50–0.53 s | 1.07 s | ≤ 1.5 s — **met** |
| P2 Normal projection CMYK / RGB | 0.129–0.135 s / 0.153–0.160 s = **0.82–0.86×** | 0.118 s / 0.197 s = 0.60× | ≤ 1.5× — **met** |

³ measured while the ASan tree was building, so noisy. Before the fix (cmyk-all-merged):
P2 CMYK 0.61–0.67 s vs RGB 0.13–0.17 s (≈4×) with two threads and 0.25 s (1.9×) on one core.

**Why P2 was slow, and what fixed it** (02, 10109bd + e80ba04): babl never builds fast paths
when either format carries a CMYK space (the `babl_space_is_cmyk()` check before
`babl_fish_path()` in `babl-fish.c`), so every space-tagged CMYK conversion is a *reference*
fish, and `babl_fish_reference_process_double()` runs under the global
`babl_reference_mutex`. All CMYK conversions of all threads therefore serialized, and a
second thread made the projection slower. The layer-mode op now reads CMYK(A) u8/u16 layers
in their own format and converts them itself, does not pass native layer buffers through,
creates empty input buffers in its pad format; the CMYK converter re-encodes float ↔ u16
itself; the projection's validate handler renders CMYK(A) u8/u16 tiles through CMYKA float
and quantizes with babl's rounding. Every local conversion is verified once against babl
(all u8/u16 values) and falls back to babl if it differs, so results stay bit-identical. The
CMYK source formats are resolved in `prepare()` instead of on every `real_process()` call.
Any *other* space-tagged CMYK babl conversion (filters, pickers, file I/O, other precisions)
still goes through the serialized reference fish: the proper fix is an upstream babl change
(fast paths / no global mutex for CMYK spaces).

**Python runner** (WS09, this round): `libgimp/tests/pygimp/run.py` matched crash markers in
the whole output, including the echoed test script in in-build-gimp.py's `RUNNING:` command
dump, so a script that merely mentions "Segmentation fault" or "crashed" failed (reported by
06). It now drops that dump (from the `RUNNING:` line to the line ending in `--quit`) before
scanning. Checked: a passing script mentioning every marker passes; a failing `gimp_assert`
and a plug-in SIGSEGV still fail (exit 64).

## 3. Known limitations (MVP)

Compositing / painting
* Blend modes other than the color-agnostic ones (Multiply, Screen, Overlay, …) blend through
  sRGB (relcol + BPC): covered pixels are re-separated; K-only stays K-only only under the
  color-agnostic modes.
* **Legacy layer modes are not available on CMYK images**: mapped to their non-legacy mode on
  convert, load (XCF/PSD), `set_mode` and in the UI (Overlay-legacy → Soft light).
* **MyPaint brushes are disabled** on CMYK drawables (refused with a message, insensitive
  cursor).
* **Gradients and Dodge/Burn work through RGB** (`R'G'B'A float`): their output is
  re-separated, never K-only.
* Layer modes run at float for every precision: DOUBLE/U32 CMYK pixels that go through
  compositing (anchor, copy of a selection, filters) are rounded to float (as for RGB).
* Color tools (Levels, Curves, Threshold, Equalize, Stretch) operate on an RGB conversion.
* CMYK conversions outside the projection/layer-mode fast paths (filters, other precisions,
  pickers) still use babl's serialized reference fish (see §2) — correct, but they do not
  scale with threads until babl is fixed upstream.
* Pre-existing (noted by 02, RGB too): on this box P1-style projections do not scale with a
  second GEGL thread for RGB either; the CMYK numbers above are not limited by CMYK code.

Text, color, files
* **Multi-colour text (markup colors) or outlines go rich black**: only solid-colour, no-markup
  text renders K-only; the rest renders in RGB and is converted.
* The RGB round trip re-separates K100 into rich black (builtin: K100 → sRGB (perceptual +
  BPC) → CMYK (relcol + BPC) = (2738, 2907, 2286, 63202) u16); no K-preserving intents.
* With the builtin profile, perceptual + BPC maps K100 to ~12/255 sRGB (v4 perceptual black),
  relcol gives ~0.08; the display uses the user's display intent.
* **Serialized CMYK colors embed the space's ICC (~106 KB base64 with the builtin)** in XCF
  parasites, config files (contextrc, colorrc, tool options), the wire protocol, palettes and
  DnD (REVIEW-02 M4, deferred).
* I8 (one babl space per equal profile) ignores the whole ICC header, including version,
  class, PCS and rendering intent (REVIEW-02 m4): two profiles differing only in header version
  share a space although lcms treats their perceptual black differently.
* **CMYK JPEG is always treated as Adobe-inverted** on load and written Adobe-inverted.
* CMYK TIFF/JPEG export always embeds the image profile (no "save color profile" opt-out);
  CMYK PSDs are 8/16 bit only (float images are written as 16 bit).
* Pre-existing (noted by 06, upstream master too): the TIFF (separated, spp < 4), PSD (layer
  with fewer colour channels than the mode) and JPEG (short APP13) loader memory-safety bugs
  also exist on master's RGB import of CMYK files; they are fixed here (c20a674, 999d153,
  f93c886) and should go upstream as separate MRs.
* A text layer exported to multi-page TIFF comes back twice ("Km #1" + "Km", identical) —
  upstream behaviour, RGB too.
* Link layers to CMYK files use the default CMYK profile for the image; not exercised by tests.
* One-way CMYK profiles can still reach babl RGB → CMYK in the CMYK color selector and the
  color frame's CMYK mode for RGB images, when set as soft-proofing/preferred profile
  (REVIEW-02 m17, pre-existing).
* FG/BG keyboard actions edit the color in sRGB and re-separate an image-space CMYK FG
  (REVIEW-02 m16).
* Plug-ins: CMYK is opt-in (`"CMYK*"` token); RGB-only plug-ins see CMYK images as
  insensitive / through export-options conversion; a CMYK-only export capability set receives
  RGB drawables unconverted (REVIEW-02 m13); PAM/TGA convert to sRGB, FITS refuses.

Compatibility
* **XCF version 28 is claimed locally** and needs upstream coordination before the 3.4 cycle
  assigns it (R10); `base-type cmyk` in `templaterc` is unreadable for older GIMP.
* Without lcms in babl (`gimp_babl_has_cmyk_support()` FALSE) CMYK is unavailable: image
  creation errors, XCF CMYK files refuse to load (`G_FILE_ERROR_NOSYS`), CMYK TIFF/PSD/JPEG
  import as RGB.

Test environment notes
* The python test session has no brush/dynamics data (only the clipboard brushes); creating a
  generated brush criticals there — the e2e test paints with "Clipboard Mask".
* Upstream leaks seen by the crash-detecting runner (RGB too): "2 GeglBuffers leaked" after a
  libgimp copy/paste; `xcf_load_parasite()` leaks every parasite string (REVIEW-02 m14).

## 4. REVIEW-02 disposition

| item | severity | disposition |
|---|---|---|
| Blockers | — | none raised |
| M1 empty component mask not empty on RGB/GRAY | Major | **fixed** 8b6a76a (01a), regression test F12b |
| M2 loader memory safety (TIFF spp, PSD channel_idx, JPEG APP13; PSD ICC alloc) | Major | **fixed** c20a674, 999d153 (+ `g_try_malloc` in `peek_icc_profile()`), f93c886 (06), hostile-input cases in `test-cmyk-file-formats.py`; upstream MRs still to file |
| M3 P2 missed, two-thread contention | Major | **fixed** 10109bd + e80ba04 (02), plus WS09's half regression fix 75d46b5; P2 0.82–0.86× RGB; babl upstream follow-up open (§2) |
| M4 serialized colors embed ~106 KB ICC | Major | **deferred**: needs the coordinated named-reference scheme across libgimpconfig, XCF, gpparams, palettes, DnD (4–6 days) |
| m1 private legacy-mode table | Minor | **fixed** 077eaec (03), test I13/legacy-mapping-table |
| m2 private copy of fix_layer_format_spaces | Minor | **fixed** a44dd8b (03, `_gimp_image_fix_layer_format_spaces()` exported) |
| m3 stale CMYKA alpha workarounds in loaders | Minor | **fixed** d2bac1d (06) |
| m4 I8 key ignores colorimetric header fields | Minor | **deferred**: low practical risk (real variants differ in date only); cheap follow-up |
| m5 I8 key / profile parsing on hot paths | Minor | **deferred** (performance cleanup) |
| m6 `g_printerr` diagnostics in library/plug-in code | Minor | **deferred** (still present, also in the new fast-path verification fallbacks) |
| m7 `DoLayerBlend` tautological format check | Minor | **deferred** |
| m8 per-chunk CMYK format lookup for RGB ops | Minor | **fixed** e80ba04 (resolved in `prepare()`) |
| m9 converter lifetime at exit | Minor | **deferred** (harmless; document or invalidate) |
| m10 test portability (lcms ctypes lookup, probe on non-Linux), I3 tolerance | Minor | **deferred** |
| m11 `Since: 3.4` on histogram enum values | Minor | **deferred** |
| m12 bundled RGB/GRAY fixes in CMYK commits | Minor | **deferred** to the upstream commit split |
| m13 CMYK-only export capability sets | Minor | **deferred** (documented, §3) |
| m14 upstream leaks (paste GeglBuffers, xcf parasite strings) | Minor | **deferred** — upstream, not CMYK |
| m15 work-order references in comments | Minor | **mostly fixed** 83ad16b, e80ba04, 077eaec; ~13 `INTERFACES §` references remain in 07/08 files; `devel-docs/cmyk-mode` to be dropped from the submission branch |
| m16 small behaviour notes | Minor | **documented** (§3) |
| m17 one-way profiles in the CMYK selector / color frame | Minor | **deferred** (pre-existing; documented, §3) |

## 5. Upstream submission estimate

REVIEW-02's verdict: no blocker; merge-ready for `cmyk-mode` after M1 (done); not yet
upstream-ready. Its estimate was **2.5–3.5 weeks** of focused work plus review latency
(about **1 week** if M3/M4 were accepted as limitations). With M1, M2, M3, m1–m3 and m8 now
done (≈ 5–7.5 days of that estimate), what remains:

| work | estimate |
|---|---|
| M4 named-reference color serialization (or per-file deduplication) | 4–6 days |
| babl upstream MR for CMYK fast paths / reference mutex (M3 follow-up) | 1–2 days + babl review |
| m4–m7, m9–m11, m13, m17 cleanups | 1–1.5 days |
| m15 remaining comments, drop `devel-docs/cmyk-mode`, split bundled fixes (m12) and the three loader fixes into separate MRs, squash into topic commits | 2–3 days |
| upstream coordination (XCF 28, enum values, babl one-way-profile check) | ~1 day + calendar time |
| manual UI checklist on a real display, C/M/Y/K channel icons | 1 day |

Total: roughly **2 weeks** of focused work including M4, about **1 week** if upstream accepts
the embedded-ICC serialization as a documented limitation, plus upstream review latency.

## 6. Follow-ups (post-MVP)

1. babl upstream: fast paths (and no global reference mutex) for space-tagged CMYK formats;
   then drop or keep GIMP's verified local conversions.
2. Named-builtin color serialization (M4).
3. K-preserving rendering intents (lcms `INTENT_PRESERVE_K_ONLY_*`) for CMYK → RGB → CMYK and
   CMYK → CMYK conversions.
4. Native CMYK implementations for gradients, dodge/burn, multi-colour text and outlines, and
   ink-space blend modes (Multiply in ink space).
5. MyPaint on CMYK (needs a CMYK surface in libmypaint or a conversion layer).
6. CMYK JPEG: detect non-Adobe (non-inverted) files (no APP14), export option for it.
7. "Save color profile" opt-out for CMYK TIFF/JPEG (with the E4 no-profile import path).
8. Upstream: XCF 28 assignment, `GIMP_CMYK` enum values in libgimpbase, the three loader fixes,
   the multi-page TIFF duplicate text layer, the paste/anchor GeglBuffer leak, the XCF parasite
   leak, a brush/dynamics data path for the python test session.
9. Manual UI checklist of TESTING.md on a real display (the Xvfb test covers dialogs, actions,
   display transform and previews only); channel icons for C/M/Y/K in gimp-data.
10. Soft-proofing CMYK images to another CMYK profile; ink limits / total area coverage readout.

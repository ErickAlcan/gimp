# CMYK mode — MVP status

Consolidated status of the native CMYK image mode after the integration of all workstreams
(01a, 01b, 02–08 merged at tag `cmyk-all-merged`, plus WS09's integration branch `ws/09`).
Details live in each `WORKSTREAMS/<ws>.md` Status section; this file is the summary.

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

## 2. Test matrix (final run on `ws/09`)

Full suite: `meson test -C _build --no-rebuild --num-processes 1` → **31 OK, 1 FAIL, 0 SKIP**
(32 tests). The failure is `gimp:desktop / appdata_file` (needs network; baseline).
No test is skipped at suite level; inside suites the only remaining skips are the gated P1/P2
(`GIMP_TESTING_PERF`) and I1's optional real-profile case when `GIMP_TESTING_CMYK_ICC` is unset.

| suite | ids | default | u8 | half | double | u16/float | default_cmyk.icc | FOGRA39 | both ICCs | double+FOGRA39 |
|---|---|---|---|---|---|---|---|---|---|---|
| layer-modes-golden | G1–G3 | OK | OK | OK | OK | OK | – | – | – | – |
| cmyk-foundation | F1–F13 | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| cmyk-compositing | C1–C13 (+P gated) | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| cmyk-image | I1–I15 | OK | OK | OK | OK¹ | OK | OK | OK | OK | OK |
| cmyk-histogram | H1–H6 | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| cmyk-nosupport | N1–N4 | OK | OK | OK | OK | OK | – | – | – | – |
| cmyk-ui | UI (WS07) | OK | OK | OK | OK | OK | OK | OK | OK | – |
| xcf | X1–X4 + legacy | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| cmyk-e2e | Z | OK | – | – | – | – | OK | OK | OK | – |
| cmyk-file-formats | E1–E7 | OK | – | – | – | – | OK | OK | OK | – |
| cmyk-babl-probe | Tier A #4 | OK | – | – | – | – | – | – | – | – |
| libgimp python3 cmyk | L | OK | – | – | – | – | – | – | – | – |
| libgimp C export-options | L (C) | OK | – | – | – | – | – | – | – | – |
| other existing suites | core, xcf, save-and-export, libgimp ×14 | OK | | | | | | | | |

¹ after WS09's fix: copy-of-selection and anchor paths compare at float for DOUBLE/U32 (layer
modes process at float for every precision, as for RGB).
`GIMP_TESTING_NO_CMYK=1` on `cmyk-nosupport`: OK (N1–N4).
**ASan** (`_build-asan`, `-Db_sanitize=address`, `detect_leaks=0`): core, layer-modes-golden,
cmyk-foundation, cmyk-compositing, cmyk-image, cmyk-histogram (incl. H2 pickers on CMYKA double),
cmyk-nosupport, cmyk-ui and xcf all OK at the default precision, with
`GIMP_TESTING_CMYK_PRECISION=double`, and with both real ICC profiles; no AddressSanitizer
report. The Python suites cannot run against an ASan build (GObject introspection is disabled
with sanitizers; the configure needs a native `gimp-console-3.3` in `PATH`).

**Performance** (`GIMP_TESTING_PERF=report`, 2048² u16, GEGL threads = 2, this box):
* P1 (CMYK Multiply projection): **1.04–1.31 s** (target ≤ 1.5 s): **met**.
* P2 (Normal projection, CMYK vs RGB): **CMYK 0.61–0.67 s vs RGB 0.13–0.17 s = 3.8–4.7×**
  (target 1.5×): **missed**. 02 measured 0.269 s / 0.148 s = 1.83× earlier. Pinned to one
  core (`taskset -c 0`) the same run gives **CMYK 0.253 s vs RGB 0.133 s = 1.90×**, i.e. the
  CMYK Normal projection gets *slower* with a second thread (RGB does not): thread contention
  in the CMYK path on top of the babl cost below (open item for 02, see §4).
* Single-thread babl cost (ctypes microbenchmark, 4 Mpx): `CMYK u16 → CMYKA float` 0.27 s,
  `CMYKA float → CMYKA u16` 0.11 s, vs `R'G'B' u16 → R'G'B'A float` 0.01 s; tagging with the
  CMYK space makes no difference — babl has no fast path for CMYK u16 ↔ float.

## 3. Known limitations (MVP)

Compositing / painting
* **P2 at 1.83× (one thread) to ~4× (two threads) vs the 1.5× target**: babl lacks a fast
  path for CMYK(A) u16 ↔ CMYKA float (space-tagged or not); the two-thread slowdown is
  unexplained (§4).
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

Text, color, files
* **Multi-colour text (markup colors) or outlines go rich black**: only solid-colour, no-markup
  text renders K-only; the rest renders in RGB and is converted.
* The RGB round trip re-separates K100 into rich black (e.g. builtin: K100 → sRGB (perceptual +
  BPC) → CMYK (relcol + BPC) = (2738, 2907, 2286, 63202) u16); no K-preserving intents.
* With the builtin profile, perceptual + BPC maps K100 to ~12/255 sRGB (v4 perceptual black),
  relcol gives ~0.08; the display uses the user's display intent.
* **Serialized CMYK colors embed the space's ICC (~106 KB base64 with the builtin)** in XCF
  parasites, config files, the wire protocol, palettes and DnD.
* **CMYK JPEG is always treated as Adobe-inverted** on load and written Adobe-inverted.
* CMYK TIFF/JPEG export always embeds the image profile (no "save color profile" opt-out);
  CMYK PSDs are 8/16 bit only (float images are written as 16 bit).
* A text layer exported to multi-page TIFF comes back twice ("Km #1" + "Km", identical) —
  upstream behaviour, RGB too.
* Link layers to CMYK files use the default CMYK profile for the image (the buffer keeps its
  own space); the path is not exercised by tests.
* Plug-ins: CMYK is opt-in (`"CMYK*"` token); `"*"` and RGB-only plug-ins see CMYK images as
  insensitive / through export-options conversion; PAM/TGA convert to sRGB, FITS refuses.

Compatibility
* **XCF version 28 is claimed locally** and needs upstream coordination before the 3.4 cycle
  assigns it (R10).
* Without lcms in babl (`gimp_babl_has_cmyk_support()` FALSE) CMYK is unavailable: image
  creation errors, XCF CMYK files refuse to load, CMYK TIFF/PSD/JPEG import as RGB.

Test environment notes
* The python test session has no brush/dynamics data (only the clipboard brushes, no "Dynamics
  Off"); creating a generated brush criticals there (writable data folder not in the search
  path) — the e2e test paints with "Clipboard Mask".
* A copy/paste/anchor sequence through libgimp reports "2 GeglBuffers leaked" at exit
  (RGB too, pre-existing).

## 4. Follow-ups (post-MVP)

1. **P2**: find the two-thread contention in the CMYK Normal projection (02; candidates: the
   per-call `cache_fishes()` / `babl_fish()` lookups, the `format_ops_mutex` /
   `converters_mutex` lookups, babl's fish cache) and add a babl fast path (or a gimp-babl
   registration) for CMYK(A) u8/u16 ↔ CMYKA float.
2. K-preserving rendering intents (lcms `INTENT_PRESERVE_K_ONLY_*`) for CMYK → RGB → CMYK and
   CMYK → CMYK conversions.
3. Named-builtin color serialization (no embedded ICC for the builtin; 5 serializers in 4
   workstreams).
4. Native CMYK implementations for gradients, dodge/burn, multi-colour text and outlines, and
   ink-space blend modes (Multiply in ink space).
5. MyPaint on CMYK (needs a CMYK surface in libmypaint or a conversion layer).
6. CMYK JPEG: detect non-Adobe (non-inverted) files (no APP14), export option for it.
7. "Save color profile" opt-out for CMYK TIFF/JPEG (with the E4 no-profile import path).
8. Upstream: XCF 28 assignment, `GIMP_CMYK` enum values in libgimpbase, babl CMYK fast paths,
   the multi-page TIFF duplicate text layer, the paste/anchor GeglBuffer leak, a brush/dynamics
   data path for the python test session.
9. Run the manual UI checklist of TESTING.md on a real display (Xvfb test covers dialogs,
   actions, display transform and previews only); channel icons for C/M/Y/K in gimp-data.
10. Soft-proofing CMYK images to another CMYK profile; ink limits / total area coverage
    readout in pickers.

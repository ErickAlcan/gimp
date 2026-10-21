# WS06 — File plug-ins: TIFF, JPEG, PSD

**Goal**: open CMYK files natively and export CMYK images natively (TIFF, JPEG, PSD), never
crash on one-way profiles, keep today's "export as CMYK" options for RGB images.

**Owned files**: `plug-ins/file-tiff/*`, `plug-ins/file-jpeg/*`, `plug-ins/file-psd/*`,
`app/tests/test-cmyk-file-formats.py`.

**Dependencies**: starts at the 01a merge (libgimp enums exist; use
`gimp_color_profile_is_cmyk_bidirectional()` from 01a). Acceptance needs 01b, 05 (export
options, `gimp_layer_new` checks, `"CMYK*"`), 07 (import dialog). Merges ninth.

**Steps** (all loaders follow INTERFACES §14: profile set before layers; check
`is_cmyk_bidirectional`; without CMYK support or with a one-way profile → today's RGB path +
warning)
1. TIFF load (`file-tiff-load.c`, PHOTOMETRIC_SEPARATED, InkSet CMYK, 8/16-bit and float):
   native `GIMP_CMYK` image, layers `GIMP_CMYK(A)_IMAGE`, read with the drawable format;
   extra-sample alpha (associated → `CaMaYaKaA`-style handling via babl, unassociated →
   straight); Photoshop layer path (`load_photoshop_layers`) likewise; no profile → default
   profile, raw values kept, message.
2. TIFF export (`file-tiff-export.c`): CMYK image → PHOTOMETRIC_SEPARATED, InkSet CMYK, embed
   the image profile; `CAN_HANDLE_CMYK`; hide the "export as CMYK" option for CMYK images;
   check `:850` `sample[8]`.
3. JPEG load: JCS_CMYK/JCS_YCCK → CMYK image, read with inverted `cmyk u8` tagged with the
   image space (Adobe convention); export: CMYK image → JCS_CMYK Adobe inverted, embed profile.
4. PSD load (`psd-load.c`, CMYK mode, inverted `cmyk u8/u16`): CMYK image with layers, masks,
   alpha; check `:3943`, `:5736`; PSD export (`psd-export.c`) of CMYK images natively; keep the
   RGB→CMYK option for RGB images.
5. Procedure image types: add `"CMYK*"` to these loaders' and exporters' procedures.
6. Tests E1–E7 in `test-cmyk-file-formats.py` (format names only, I5; input-only profile and
   4×4 TIFF built in the script); Tier A on owned files.

**Acceptance**: E1–E7 pass; existing save-and-export test green; no babl crash with
input-only profiles; G1–G3 green.

**Risks**: libtiff extra-samples semantics; JPEG inversion and YCCK; PSD channel order, masks
and 16-bit inversion; dependence on 05/07 at acceptance.


## Status

Done on `ws/06` (5 code commits + this one). Build warning-free in every touched file
(`plug-ins/file-{tiff,jpeg,psd}/*`, `common/{decompose,file-pnm,file-tga,tile}.c`,
`file-fits/fits.c`; the 01a `-Wswitch` list for plug-ins is cleared, incl. `psd-load.c:590`).
`meson test`: 26 OK, 4 SKIP, 1 FAIL (`appdata_file`, network) = baseline; G1–G3 green.

**What landed**
* TIFF load: SEPARATED + InkSet CMYK → `GIMP_CMYK` image, precision forced non-linear,
  embedded profile set right after `gimp_image_new_with_precision()` (before any layer),
  pixels written in the layer's (image) space; unassociated alpha `CMYKA`, associated alpha
  `CaMaYaKaA` (helper `tiff_cmyk_associated_format()`, babl names the component "Yea");
  no CMYK profile → default profile + message, raw values kept. RGB import (today's path)
  when: profile CMYK but not bidirectional (message), InkSet ≠ CMYK, `gimp-image-new` refuses
  CMYK (no CMYK support), or a later page goes into an RGB image. Photoshop layers in a CMYK
  TIFF go through the PSD loader natively. An input-only profile is never attached as
  soft-proofing profile (the CMYK selector would feed it to babl RGB→CMYK).
* TIFF export: CMYK(A) drawables → SEPARATED, InkSet CMYK, `CMYK`/`CMYKA`/`CaMaYaKaA`
  tagged with the image space; the image profile is **always** embedded for CMYK images
  (decision: inks are meaningless without it; the late-binding path did the same);
  "Export as CMYK" hidden for CMYK images; `CAN_HANDLE_CMYK`, `"*, CMYK*"`.
  NumberOfInks only written when SamplesPerPixel == 4 (libtiff warned otherwise, also on the
  old path). `:850` `sample[8]`: OK (one-component extra-channel format, ≤ 8 bytes).
* JPEG: ICC read before image creation; JCS_CMYK (incl. YCCK) → `GIMP_CMYK` with inverted
  `cmyk u8` in the image space; same fallbacks/messages as TIFF; export preview of a CMYK
  image loads natively. Export: Adobe-inverted JCS_CMYK, profile always embedded, option
  hidden; `CAN_HANDLE_CMYK`, `"RGB*, GRAY*, CMYK*"`.
* PSD load: CMYK 8/16 bit → `GIMP_CMYK` (u8/u16); ICC resource peeked
  (`peek_icc_profile()`) before the image exists; layers, masks, alpha and merged image
  written as inverted `cmyk(A) u8|u16` in the image space; merged transparency unblended in
  `CMYKA float` (ink/alpha). Resource 1039 is a no-op for CMYK images. One-way profile / no
  support → today's RGB path.
* PSD export: CMYK images always written as CMYK PSD with the image profile
  (`get_cmyk_export_profile()`), `gimpBaseTypeToPsdMode`/`nChansLayer` CMYK cases, dialog shows
  a notice instead of the option. Pre-existing bugs of the cmyk path fixed on the way: layer
  masks were dropped from the channel count (corrupt files), header channel count ignored
  extra channels, the merged white-blend ran a u8 loop over `R'G'B'A float`, float images
  wrote a 32-bit header with 16-bit data (CMYK PSDs are now 8/16 bit throughout).
  `:3943` (`get_json_color` `pixel[4]`) and `:5736` (`convert_legacy_psd_color`): OK (explicit
  `R'G'B' double/u16` formats).
* Other plug-ins: decompose/tile create CMYK layers/images for CMYK; PAM/TGA convert to sRGB
  should a CMYK drawable get there; FITS refuses.
* Tier A on owned files: all base/drawable-type switches have CMYK cases; stack buffers with
  colors use explicit RGB formats (psd-export `rgb[3]` R'G'B' double, psd-load `pixel[4]` ×2,
  file structs in psd.h) — OK.

**Acceptance (E tests, `app/tests/test-cmyk-file-formats.py`)** — verified **with a local,
uncommitted stub of WS03's §10 preview formats** (`gimp_image_get_preview_format()` /
`gimp_drawable_get_preview_format()` CMYK → `R'G'B'(A) u8`), because on this branch every
export of a CMYK image crashes the core in the thumbnail code (see risks):
* E1 TIFF u8/u16 × alpha: PASS (exact; Photometric 5, InkSet 1, ICC tag, spp 4/5 checked by
  an in-script IFD reader — `tiffinfo` is not installed).
* E2 PSD u8/u16 × alpha, with layer mask: PASS (pixels and mask exact).
* E3 JPEG q100: PASS (max abs diff 0/255; K-only patch C,M,Y ≤ 1).
* E4 no-profile CMYK TIFF (written by the script; batch export cannot turn the aux option
  "include-color-profile" off, and CMYK export now always embeds): PASS.
* E5 RGB "export as CMYK" TIFF/JPEG/PSD: PASS (now reloads as native CMYK with the
  soft-proofing profile; white → ≤1/255 ink, black → K 234).
* E6 PNG export of a CMYK image: **pending integration (WS05)** (export returns an error
  today, no crash); strict once `Gimp.Image.convert_cmyk` exists.
* E7 input-only profile (ctypes lcms derivative of the builtin, pure-Python TIFF): PASS
  (RGB import, warning, not used as simulation profile).
* Also checked by hand: with `GIMP_TESTING_NO_CMYK=1` CMYK TIFF/PSD/JPEG load as RGB.

**Pending integration / requests for other owners**
* **WS03** `app/core/gimpimage-preview.c:45`, `gimpdrawable-preview.c:170`: CMYK case per
  INTERFACES §10 — without it any CMYK export (thumbnail) segfaults the core, so E1–E5 cannot
  run on this branch alone.
* **WS05** `pdb/groups/layer.pdb` `layer_new`: `GIMP_CMYK(A)_IMAGE` → currently CMYKA gets no
  alpha (loaders and the test add it themselves via `gimp_layer_add_alpha()`; harmless after
  05). `image_new*`: return an error (not a CRITICAL from `gimp_image_new()`) without CMYK
  support. `gimppluginprocedure.c`: `"CMYK*"` token (until then "image-type contains
  unrecognizable parts" is printed at startup). Export options CMYK→RGB step (E6).
* **WS05/WS09** `libgimp/tests/libgimp-run-python-test.py`: a core segfault during a Python
  test is reported as OK (exit 0 after the "[E]xit/[S]tack/[P]roceed" prompt) — tests can pass
  spuriously; please propagate crashes as failures.
* **WS07**: CMYK import dialog/messages: loaders use `g_message` texts "... has no embedded
  CMYK color profile ..." and "... cannot be used as a color space ... imported as RGB".

**Deviations from INTERFACES.md**: none in signatures. Behavioural decision: native CMYK
TIFF/JPEG export always embeds the profile (PSD already did for its CMYK path).

**Integration follow-up (after merging cmyk-mode 808c81a)**: E3 failed on the integrated tree
because the PSD resource block written into JPEG APP13 contained the final ~80 KB builtin
profile (marker > 65533 bytes → libjpeg error). Fixed in 44b8606: CMYK metadata no longer
embeds the profile (the host format does), and jpeg-export splits APP13 into several markers.
Root cause was in WS06 files, not in WS05's export options. E1–E7 now all pass on the
integrated tree, E6 included (strict). `decompose.c:628` was already fixed in 27740ab; the
`file-tga.c:942` fread warning is upstream code, not in the WS06 diff. Remaining suite
failure on the integrated tree: `cmyk-compositing` C2 (WS02, max_diff 0.0475 > 3/255).

**REVIEW-02 M2 follow-up** (on cmyk-mode da9e756): c20a674 TIFF refuses SEPARATED pages with
fewer than 4 (+alpha) samples and `get_extra_channels_count()` no longer underflows; 999d153 PSD
clears `channel_idx[]` per layer and leaves layers missing colour channels empty (message), plus
`g_try_malloc` in `peek_icc_profile()`; f93c886 JPEG joins only APP13 markers that carry the
"Photoshop 3.0" header. Each has a hostile-input case in `test-cmyk-file-formats.py` (files
generated by the script), JPEG also a split-marker join test (70 KB block, exporter-sized and
7-byte pieces, guides must survive). The CMYKA alpha workarounds are gone (latest commit).
All 13 crafted files of REVIEW-02 load or fail cleanly; full suite 31 OK, 1 FAIL (appdata_file).

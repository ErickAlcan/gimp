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

**Status**: (agent fills in)

# WS06 — File plug-ins: TIFF, JPEG, PSD

Goal: open CMYK files natively; export CMYK images natively.

Owned files: plug-ins/file-tiff/*, plug-ins/file-jpeg/*, plug-ins/file-psd/*.

Steps
1. TIFF load (file-tiff-load.c PHOTOMETRIC_SEPARATED): if CMYK support and (profile is CMYK
   bidirectional or no profile) → `GIMP_CMYK` image, set profile first, layers
   `GIMP_CMYK(A)_IMAGE`, read with drawable format (u8/u16; float TIFF CMYK too if present);
   input-only profile → legacy RGB path + warning. Photoshop layers path (`load_photoshop_layers`)
   likewise. TIFF export: CMYK image → PHOTOMETRIC_SEPARATED, INKSET_CMYK, embed image profile;
   declare `CAN_HANDLE_CMYK`; hide "export as CMYK" option for CMYK images.
2. JPEG load: JCS_CMYK/YCCK → CMYK image with inverted `cmyk u8` read format tagged with image
   space; export: CMYK image → JCS_CMYK Adobe inverted, embed profile.
3. PSD load: CMYK mode (`cmyk u8/u16` inverted) → CMYK image with layers/masks; export CMYK
   images natively (psd-export.c), keep current RGB→CMYK option for RGB images.
4. Image-type strings: add "CMYK*" to these exporters/loaders' export procs.
5. Tier A audits; run B3 steps 5–6 locally.

Acceptance: B3 steps 5, 6 and 9 pass; existing RGB→CMYK export options still work for RGB
images; no babl crash with input-only profiles.

Risks: libtiff extra-samples/alpha semantics (associated vs unassociated), JPEG inversion,
PSD channel order and mask handling, depends on 05 for export options behaviour.

Status: (to be filled)

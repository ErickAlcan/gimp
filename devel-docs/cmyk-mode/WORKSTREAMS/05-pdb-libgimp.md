# WS05 — PDB, libgimp, plug-in infrastructure

Goal: plug-ins and scripts can create, query, convert and export CMYK images.

Owned files: PLAN §3 (05).

Steps
1. pdb/groups: `image_convert_cmyk` (image_convert.pdb), `drawable_is_cmyk` (drawable.pdb);
   update help texts of image_new*, layer_new (type/base-type match), image_convert_indexed (CMYK
   error), image_convert_rgb/grayscale (accept CMYK), color-profile procs (CMYK validation);
   regenerate (`ninja -C _build`) and commit regen.
2. Hand-written libgimp: `gimp_image_convert_cmyk()` in gimpimagecolorprofile.c/.h,
   gimp.def; gimplayer.c type checks; gimpdrawable.c format with CMYK space (already generic —
   verify); gimpexportoptions.c CMYK case + `GIMP_EXPORT_CAN_HANDLE_CMYK`; docs for plug-in
   conventions (INTERFACES §12) in gimpimage.c/gimpprocedure docs.
3. app/plug-in: image types "CMYK", "CMYKA", "CMYK*" parsing, sensitivity switch cases;
   `"*"` unchanged (document).
4. app/pdb/gimppdb-utils.c: base-type error messages include CMYK.
5. Tier A audits; Python/C quick checks (B4).

Acceptance: B4 passes; `Gimp.ImageBaseType.CMYK` visible in GIR; script-fu constants present;
exporting a CMYK image with an RGB-only exporter triggers "Convert to RGB".

Risks: generated-file merge churn; GIR/vapi regeneration; enum alias handling in enums.pl.

Status: (to be filled)

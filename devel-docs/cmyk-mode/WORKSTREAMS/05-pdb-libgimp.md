# WS05 — PDB, libgimp, plug-in infrastructure

**Goal**: plug-ins and scripts can create, query, convert and export CMYK images; plug-in
type strings understand CMYK; libgimp thumbnails/previews behave (INTERFACES §10, §12, §14).

**Owned files** (PLAN §4.1): `pdb/groups/*.pdb`, `app/pdb/gimppdb-utils.{c,h}`, hand-written
`app/plug-in/*` (`gimppluginprocedure.c`, `plug-in-enums.h`, …), hand-written `libgimp/*`
(`gimpexportoptions.c`, `gimplayer.c`, `gimpimage.c`, `gimpimagecolorprofile.{c,h}`,
`gimpdrawable.c`, `gimpimagemetadata.c`, `gimpdrawablepreview.c`, `gimpthumbnailprocedure.c`,
`gimp{brush,drawable,image,item,pattern}chooser.c`, `gimpgpparams-body.c`, `gimp.def`, …),
`libgimp/tests/*`, `libgimpwidgets/gimppreviewarea.{c,h}`, `libgimpwidgets/test-preview-area.c`.
Generated (regen commits): `app/pdb/*-cmds.c`, `app/pdb/internal-procs.*`, `libgimp/*_pdb.{c,h}`,
`app/plug-in/plug-in-enums.c`.

**Dependencies**: starts at the 01a merge (call the declared `gimp_image_convert_type_full()`;
it returns "not implemented" until 03 merges). Acceptance needs 03. Merges sixth.

**Steps**
1. `pdb/groups`: `image_convert_cmyk` (image_convert.pdb) → `gimp_image_convert_type_full
   (image, GIMP_CMYK, profile, intent, bpc)`; `drawable_is_cmyk` (drawable.pdb); update
   image_new* (CMYK; error without CMYK support or with a linear precision), layer_new (type
   must match base type), image_convert_indexed (CMYK error), image_convert_rgb/grayscale
   (accept CMYK), color-profile procs (CMYK validation, NULL → default),
   `gimp-image-detach-parasite "icc-profile"` on CMYK → error, layer set-mode doc (legacy →
   mapped, I9), drawable/image thumbnail docs ("sRGB for CMYK"). `ninja -C _build`; regen
   commit.
2. libgimp: `gimp_image_convert_cmyk()` (gimpimagecolorprofile.c/.h, `gimp.def`, Since 3.4);
   `gimplayer.c` type checks; `gimpdrawable.c` formats with the CMYK space (verify; thumbnail
   decoding by bpp unchanged); `gimpexportoptions.c` `GIMP_CMYK` case +
   `GIMP_EXPORT_CAN_HANDLE_CMYK`; `gimpimagemetadata.c`, `gimpdrawablepreview.c` (fetch
   `R'G'B'(A) u8`), `gimpthumbnailprocedure.c`, choosers: base-type switches; plug-in
   convention docs (INTERFACES §14).
3. `libgimpwidgets/gimppreviewarea.c`: CMYK `GimpImageType`s refused with gtk-doc (INTERFACES §10).
4. `app/plug-in/gimppluginprocedure.c`: tokens `"CMYKA"`, `"CMYK*"`, `"CMYK"` parsed in that
   order (`:1071-1121`); sensitivity cases; `"*"` unchanged (document menu-only semantics).
5. `app/pdb/gimppdb-utils.c`: base-type error messages name CMYK.
6. Tests L (`libgimp/tests/test-cmyk.py`), update `test-export-options.c`; Tier A.

**Acceptance**: L passes; `Gimp.ImageBaseType.CMYK` in the GIR; script-fu constants present;
exporting a CMYK image with an RGB-only exporter adds "Convert to RGB"; G1–G3 green.

**Risks**: generated-file churn (regen commits only); GIR/vapi regeneration; enum gap in
`GimpHistogramChannel` (6 is pdb-skip) must not break enumgen.

**Status**: (agent fills in)

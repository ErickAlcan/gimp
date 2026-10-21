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

## Status

Done (WS05 agent), ready to merge after 03. Build warning-free in owned files (the `-Wswitch`
warnings at `gimpexportoptions.c:783` and `gimppreviewarea.c:586,890,1285` are resolved);
`meson test`: 26 OK, 4 SKIP (skeletons/probe), 1 FAIL (`appdata_file`, network) = baseline.
**G1–G3** (`layer-modes-golden`) green.

What landed
* `app/plug-in`: `GIMP_PLUG_IN_CMYK_IMAGE`/`CMYKA_IMAGE`; tokens `"CMYKA"`, `"CMYK*"`, `"CMYK"`
  (prefix parser, in that order); sensitivity cases; insensitive-reason strings; `"*"`
  unchanged (documented in `gimp_procedure_set_image_types()`: CMYK is opt-in, menu-only).
* PDB: new `gimp-image-convert-cmyk` (lib-private, image, color-profile bytes nullable,
  intent, bpc → `gimp_image_convert_type_full()`; refuses CMYK images, non-CMYK and
  non-bidirectional profiles, no CMYK support) and `gimp-drawable-is-cmyk`.
  `image-new(-with-precision)`, `image-convert-precision`: PDB error (not a CRITICAL)
  without CMYK support or with a linear precision. `layer-new`: CMYK(A) types (alpha
  honoured), CMYK types only on CMYK images, CMYK images accept only CMYK and gray types.
  Component procs accept CYAN..BLACK on CMYK (+ALPHA). Histogram-channel procs (curves,
  levels, histogram, threshold) reject CMYK channels on non-CMYK drawables and R/G/B/
  luminance on CMYK drawables. `image-convert-indexed`: "Convert the image to RGB first".
  `image-convert-color-profile` NULL on CMYK → default CMYK profile.
  `image-detach-parasite "icc-profile"` on CMYK → error. `drawable-type-with-alpha` excludes
  `CMYK_IMAGE`. Docs: convert-rgb/-grayscale accept CMYK, set-color-profile CMYK rules,
  legacy modes mapped on CMYK layers (I9, core rule of 03), thumbnails of CMYK are sRGB,
  channel-new-from-component. Helpers in `gimppdb-utils.c`:
  `gimp_pdb_base_type_is_valid()`, `gimp_pdb_image_type_is_valid()`,
  `gimp_pdb_image_has_component()`, `gimp_pdb_drawable_has_histogram_channel()`.
* libgimp: `gimp_image_convert_cmyk()` (Since 3.4, `gimp.def`, documents the INTERFACES §14
  plug-in conventions); `gimp_drawable_is_cmyk()` (generated); `GimpExportOptions`: `GIMP_CMYK`
  case — without `GIMP_EXPORT_CAN_HANDLE_CMYK` a copy is converted (RGB, else gray, else
  indexed/bitmap via RGB); merge adds `CMYKA` layers; CMYK-only capability sets allowed.
  `gimpimagemetadata.c`: metadata RGB colorspaces never applied to CMYK images. Drawable,
  zoom and aspect previews draw R'G'B'(A) u8 buffers for CMYK drawables (thumbnail format).
  `gimp_drawable_get_format()` already tags CMYK formats with the image's CMYK space
  (verified); thumbnail decoding by bpp unchanged.
* `libgimpwidgets/gimppreviewarea.c`: CMYK types refused (`g_return_if_fail`, gtk-doc);
  `test-preview-area.c` skips them.
* Tests: `libgimp/tests/test-cmyk.py` (L), CMYK block in `test-export-options.c`.
  **Test runners** (`libgimp-run-python-test.py`, `libgimp-run-c-test.py`, new
  `pygimp/run.py`): GIMP runs with `--stack-trace-mode=never` and stdin closed; a test fails
  on non-zero exit, crash markers ("fatal error", "Segmentation fault", "received signal",
  the stack-trace prompt, "gimp_wire_read(): unexpected EOF", "crashed") or a missing
  "batch command executed successfully". Verified: a deliberate core segfault (CMYK
  thumbnail before 03) exits 0 with the old runner and fails (1) with the new one; a
  plug-in SIGSEGV fails (64); passing tests still pass.

Acceptance
* L passes (`gimp:libgimp+python3 / cmyk` OK), with these parts **pending integration**
  (printed as "SKIP (pending integration)", gated on a working `convert_cmyk`): CMYK
  thumbnails bpp 3/4 (crashes in `gimp_drawable_get_preview_format()` until 03),
  `convert_cmyk` success paths (None/profile, linear → non-linear precision, back to
  RGB/gray), export-options "Convert to RGB/Grayscale" results, legacy-mode mapping.
  The same applies to the RGB check in `test-export-options.c`.
* `Gimp.ImageBaseType.CMYK` etc. in the GIR (checked in the test and in `Gimp-3.0.gir`).
* Script-Fu constants: `CMYK`=3, `CMYK-IMAGE`=6, `CMYKA-IMAGE`=7, `CHANNEL-BLACK`=9,
  `EXPORT-CAN-HANDLE-CMYK`=2048 checked by hand with `plug-in-script-fu-eval` (they come
  from the GIR).
* RGB-only exporter on a CMYK image adds "Convert to RGB" (export copy created; the actual
  conversion pending 03).
* `"CMYK*"` sensitivity: parser + sensitivity code landed; no automated test (no PDB query
  of menu sensitivity from a plug-in) — **pending** the manual checklist / 07.

Tier A: every base-type / image-type switch in owned files has a CMYK case or a `default:`;
no range checks; `[4]`-style buffers: `gimppreviewarea.c` `rgb1/rgb2[3]` OK (explicit
R'G'B' u8); `drawable-get-pixel` heap buffer of the native bpp OK; `image-pick-color`
returns a GeglColor OK. Generated files only in the "regen" commit.

Deviations / notes
* No INTERFACES signature deviation. `gimp-image-convert-cmyk` is lib-private (as §12
  implies), so `Gimp.PDB.procedure_exists()` reports it as missing.
* `GimpExportOptions` always adds "Merge Layer Effects" unless the exporter has
  `CAN_HANDLE_LAYER_EFFECTS` (existing behaviour): CMYK-capable exporters get `IGNORE` only
  with that flag.
* Requests for other owners: **03** — `gimp_drawable_get_preview_format()` CMYK case
  (core segfault on CMYK thumbnails today), `gimp_image_convert_type_full()`, legacy-mode
  mapping in `gimp_layer_set_mode()`. **09** — `app/tests` Python tests use their own
  runner; consider the same crash detection (`libgimp/tests/pygimp/run.py`).
  Unattached layers created by a plug-in show as "GeglBuffers leaked" at exit (pre-existing).


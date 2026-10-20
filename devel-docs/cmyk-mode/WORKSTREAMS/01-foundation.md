# WS01 — Foundation (lands first)

Goal: make `GIMP_CMYK` exist end-to-end in the data model so other workstreams can compile and
run against INTERFACES.md. Keep it small: no UI, no PDB procs, no compositing changes.

Owned files: see PLAN §3 (01).

Steps
1. Enums: libgimpbase/gimpbaseenums.h (BaseType, ImageType, ChannelType, ExportCapabilities),
   app/core/core-enums.h (HistogramChannel, ComponentMask), core-types.h defines. Build to
   regenerate `gimpbaseenums.c`, `core-enums.c`, `pdb/enums.pl`, `libgimp/gimpenums.h`,
   `gimpenums.c.tail`; commit regen separately.
2. gimp-babl.c: `gimp_babl_init()` defensive registrations (CMYK(A) all types, CaMaYaKaA u32/
   double, Cyan/Magenta/Yellow/Key/CMYK-A component formats); description table; base type via
   `babl_get_model_flags() & BABL_MODEL_FLAG_CMYK`; trc/precision; `is_valid`; `gimp_babl_format`
   CMYK branch; component formats; builtin profile; `format_get_color_profile` (I5); new helpers
   `gimp_babl_space_is_cmyk`, `gimp_babl_format_is_cmyk`, `gimp_babl_has_cmyk_support` (probe,
   env hook `GIMP_TESTING_NO_CMYK`). gimp-babl-compat.c image type.
3. libgimpcolor: `gimp_color_profile_new_cmyk_builtin()` (PLAN D3 spec; cache weak pointer like
   other builtins), `gimp_color_profile_is_cmyk_bidirectional()`, lcms format fixes (CMYKA,
   float→u16), transform lcms path for CMYK, `can_gegl_copy`; export in gimpcolor.def; gtk-doc.
4. gimpimage.c/h/private.h: `constructed()` default CMYK profile (no undo, not dirty);
   proj format; `get_format`; component format/index/active/visible + masks (rule I7);
   conversion-intent accessors; xcf version rule 28 + mapping; refuse creation without CMYK
   support.
5. gimpimage-color-profile.c/h: validation (CMYK + bidirectional), builtin, default profile
   resolution `gimp_get_default_cmyk_profile()`, NULL handling for CMYK, convert/assign switch
   cases for CMYK, import policy for CMYK, srgb transforms for CMYK.
6. gimpdrawable.c/h: `gimp_drawable_is_cmyk`, component format/index, base-type switches.
7. gimpimage-convert-type.h: declare `_full`; stub in .c (PLAN §3 note).
8. Run Tier A audits on owned files; Tier B baseline must stay 20/21; add a throwaway local check
   that `gimp_image_new(GIMP_CMYK)` gives formats per INTERFACES (real tests land in 09).

Acceptance
* Builds warning-free in owned files; baseline tests unchanged.
* `gimp_image_new(gimp, 64, 64, GIMP_CMYK, GIMP_PRECISION_U16_NON_LINEAR)` → layer format
  `CMYKA u16` with a CMYK space, projection `CMYKA u16`, CMYK profile attached, image clean.
* INTERFACES §1–§7 declarations all present with gtk-doc.
* Merged within the first day; tag `cmyk-foundation`.

Risks
* mkenums handling of enum aliases (fallback: #defines).
* `gimp_image_get_layer_format()` callers that assume `layer_space == NULL` means sRGB.
* Constructing the default profile in `constructed()` must not push undo or mark dirty, and must
  not run before `image->gimp->config` is available (it is: constructed asserts gimp).
* Generated profile quality/size (17⁴ CLUT ≈ 500 KB in memory; fine).

Status: (to be filled by the agent)

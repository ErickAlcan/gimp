# CMYK mode — interface contracts

Everything here is landed by **01-foundation** (declarations + working implementations, or
explicit stubs where noted) before other workstreams start. Other workstreams code against these
signatures and behaviours without reading the foundation implementation. "Owner" = workstream
that implements the real behaviour.

## 1. Enums

### libgimpbase/gimpbaseenums.h (public, GIR/PDB-visible)
```c
typedef enum
{
  GIMP_RGB,     /*< desc="RGB color"     >*/
  GIMP_GRAY,    /*< desc="Grayscale"     >*/
  GIMP_INDEXED, /*< desc="Indexed color" >*/
  GIMP_CMYK     /*< desc="CMYK color"    >*/   /* = 3, Since: 3.4 */
} GimpImageBaseType;

typedef enum
{
  GIMP_RGB_IMAGE,      GIMP_RGBA_IMAGE,
  GIMP_GRAY_IMAGE,     GIMP_GRAYA_IMAGE,
  GIMP_INDEXED_IMAGE,  GIMP_INDEXEDA_IMAGE,
  GIMP_CMYK_IMAGE,     /*< desc="CMYK"       >*/  /* = 6 */
  GIMP_CMYKA_IMAGE     /*< desc="CMYK-alpha" >*/  /* = 7 */
} GimpImageType;

typedef enum
{
  GIMP_CHANNEL_RED, GIMP_CHANNEL_GREEN, GIMP_CHANNEL_BLUE,
  GIMP_CHANNEL_GRAY, GIMP_CHANNEL_INDEXED, GIMP_CHANNEL_ALPHA,
  GIMP_CHANNEL_CYAN,     /*< desc="Cyan"    >*/  /* = 6 */
  GIMP_CHANNEL_MAGENTA,  /*< desc="Magenta" >*/  /* = 7 */
  GIMP_CHANNEL_YELLOW,   /*< desc="Yellow"  >*/  /* = 8 */
  GIMP_CHANNEL_BLACK     /*< desc="Black"   >*/  /* = 9 */
} GimpChannelType;

/* GimpExportCapabilities: append */
  GIMP_EXPORT_CAN_HANDLE_CMYK                = 1 << 11
```

### app/core/core-enums.h (app; GimpHistogramChannel is exported to libgimp via enums.pl)
```c
typedef enum
{
  GIMP_HISTOGRAM_VALUE = 0, GIMP_HISTOGRAM_RED = 1, GIMP_HISTOGRAM_GREEN = 2,
  GIMP_HISTOGRAM_BLUE = 3, GIMP_HISTOGRAM_ALPHA = 4, GIMP_HISTOGRAM_LUMINANCE = 5,
  GIMP_HISTOGRAM_RGB = 6,      /*< desc="RGB", pdb-skip >*/
  GIMP_HISTOGRAM_CYAN    = 7,  /*< desc="Cyan"    >*/
  GIMP_HISTOGRAM_MAGENTA = 8,  /*< desc="Magenta" >*/
  GIMP_HISTOGRAM_YELLOW  = 9,  /*< desc="Yellow"  >*/
  GIMP_HISTOGRAM_BLACK   = 10  /*< desc="Black"   >*/
} GimpHistogramChannel;

typedef enum  /*< pdb-skip >*/
{
  GIMP_COMPONENT_MASK_RED     = 1 << 0,
  GIMP_COMPONENT_MASK_GREEN   = 1 << 1,
  GIMP_COMPONENT_MASK_BLUE    = 1 << 2,
  GIMP_COMPONENT_MASK_ALPHA   = 1 << 3,
  GIMP_COMPONENT_MASK_BLACK   = 1 << 4,
  GIMP_COMPONENT_MASK_CYAN    = GIMP_COMPONENT_MASK_RED,     /* aliases for CMYK images */
  GIMP_COMPONENT_MASK_MAGENTA = GIMP_COMPONENT_MASK_GREEN,
  GIMP_COMPONENT_MASK_YELLOW  = GIMP_COMPONENT_MASK_BLUE,
  GIMP_COMPONENT_MASK_ALL     = 0x1f
} GimpComponentMask;
```
(If mkenums chokes on aliases, define the three aliases as `#define`s below the enum instead.)

**Rule (I7)**: masks returned by `gimp_image_get_active_mask()`, `gimp_image_get_visible_mask()`
and `gimp_drawable_get_active_mask()` always have the bits set for components the image's model
does not have (BLACK for RGB/GRAY/INDEXED). So `mask == GIMP_COMPONENT_MASK_ALL` keeps meaning
"everything affected". For CMYK: bit0=C, bit1=M, bit2=Y, bit4=K, bit3=alpha.

### app/core/core-types.h
```c
#define CYAN     0
#define MAGENTA  1
#define YELLOW   2
#define BLACK    3
#define ALPHA_C  4      /* MAX_CHANNELS stays 5 */
```

### app/plug-in/plug-in-enums.h (owner 05)
```c
  GIMP_PLUG_IN_CMYK_IMAGE  = 1 << 6,
  GIMP_PLUG_IN_CMYKA_IMAGE = 1 << 7
```
Image-type tokens: `"CMYK"`, `"CMYKA"`, `"CMYK*"`. `"*"` keeps meaning the six old types.

## 2. app/gegl/gimp-babl.h (owner 01)
Existing functions gain CMYK behaviour:
* `gimp_babl_format (GIMP_CMYK, precision, with_alpha, space)` → `"CMYK(A) <type>"` with `space`
  (table in PLAN D4). TRC of `precision` ignored for CMYK.
* `gimp_babl_is_valid (GIMP_CMYK, p)` → TRUE only for `*_NON_LINEAR`.
* `gimp_babl_format_get_base_type (fmt)` → `GIMP_CMYK` when the model has `BABL_MODEL_FLAG_CMYK`
  (covers CMYK, CMYKA, CaMaYaKaA, cmyk, cmykA, camayakaA).
* `gimp_babl_format_get_trc (cmyk fmt)` → `GIMP_TRC_NON_LINEAR`;
  `gimp_babl_format_get_precision (cmyk fmt)` → `<type>_NON_LINEAR`.
* `gimp_babl_component_format (GIMP_CMYK, precision, index)` index 0..4 →
  `"Cyan|Magenta|Yellow|Key|CMYK-A <type>"` (no space; callers tag with the image space).
* `gimp_babl_get_builtin_color_profile (GIMP_CMYK, any trc)` → builtin CMYK profile (not ref'd,
  same lifetime semantics as the sRGB builtin).
* `gimp_babl_format_get_color_profile (fmt)`: CMYK fmt → profile from the space's ICC (builtin if
  space NULL); RGB/Y fmt whose space is CMYK → builtin sRGB/gray profile for its TRC (I5).
* `gimp_babl_format_get_description()`: "CMYK" / "CMYK-alpha" for all CMYK(A) formats.
* `gimp_babl_compat_u8_format()` works for CMYK.
* `gimp_babl_format_get_image_type()` (gimp-babl-compat.h) → `GIMP_CMYK(A)_IMAGE`.

New:
```c
gboolean     gimp_babl_space_is_cmyk          (const Babl *space);   /* NULL-safe */
gboolean     gimp_babl_format_is_cmyk         (const Babl *format);  /* model flag test */
gboolean     gimp_babl_has_cmyk_support       (void);  /* cached probe: babl built with lcms */
```

## 3. libgimpcolor (public, owner 01; add to gimpcolor.def; "Since: 3.4")
```c
GimpColorProfile * gimp_color_profile_new_cmyk_builtin      (void);
/* new reference; deterministic generated LUT profile, see PLAN D3. */

gboolean           gimp_color_profile_is_cmyk_bidirectional (GimpColorProfile *profile);
/* TRUE iff is_cmyk and lcms can create CMYK->Lab and Lab->CMYK transforms for
 * INTENT_RELATIVE_COLORIMETRIC. Use before creating a babl space for an image/target. */
```
Behaviour changes (no signature change):
* `gimp_color_profile_get_lcms_format()`: recognises `CMYKA`; for CMYK(A) with half/float/double
  returns `CMYK(A) u16` as substitute format + `TYPE_CMYK(A)_16` (u8 → `TYPE_CMYK(A)_8`).
* `gimp_color_transform_new()` / `_new_proofing()`: lcms path when either profile is CMYK.
* `gimp_color_transform_can_gegl_copy()`: FALSE when profiles differ and either is CMYK.

## 4. app/core/gimpimage.h (owner 01)
Existing functions with CMYK behaviour: `gimp_image_get_format()`, `gimp_image_get_layer_format()`,
`gimp_image_get_layer_space()` (never NULL for CMYK), projection format (`CMYKA <prec>`),
`gimp_image_get_component_format()` (CMYK channel types → space-tagged single-component formats),
`gimp_image_get_component_index()` (CYAN..BLACK → 0..3, ALPHA → ALPHA_C for CMYK),
`gimp_image_{set,get}_component_{active,visible}()`, `gimp_image_get_{active,visible}_mask()`
(rule I7), `gimp_image_get_xcf_version()` (any CMYK → ≥ 28, reason "CMYK images were added in
GIMP 3.4"; `case 28` → 340 / "GIMP 3.4").

New:
```c
/* conversion intent used by drawable convert_type during gimp_image_convert_type_full();
 * defaults: PERCEPTUAL, bpc TRUE */
void  _gimp_image_set_conversion_intent (GimpImage                *image,
                                         GimpColorRenderingIntent  intent,
                                         gboolean                  bpc);
void  _gimp_image_get_conversion_intent (GimpImage                *image,
                                         GimpColorRenderingIntent *intent,
                                         gboolean                 *bpc);
```
Construction: `gimp_image_new (gimp, w, h, GIMP_CMYK, precision)` attaches the default CMYK
profile (no undo, image not dirty). Precondition `gimp_babl_is_valid()`; additionally returns
NULL with a g_warning if `! gimp_babl_has_cmyk_support()`.

## 5. app/core/gimpimage-color-profile.h (owner 01)
```c
GimpColorProfile * gimp_get_default_cmyk_profile (Gimp              *gimp,
                                                  GimpColorProfile  *preferred,  /* nullable */
                                                  GError           **error);     /* nullable */
/* Returns a NEW reference, never NULL: preferred if CMYK+bidirectional, else the
 * color-config cmyk-profile if loadable+CMYK+bidirectional, else the builtin.
 * Skipped candidates set *error (first one only) but the call still succeeds. */
```
Behaviour:
* `gimp_image_validate_color_profile_by_format()`: CMYK formats require
  `gimp_color_profile_is_cmyk_bidirectional()`; error text "Color profile is not for CMYK color
  space" / "CMYK color profile cannot be used for conversion to CMYK (missing output tables)".
* `gimp_image_set_icc_profile (image, NULL, …)` on CMYK → attaches default instead.
* `gimp_image_get_builtin_color_profile()` → CMYK builtin for CMYK images.
* `gimp_image_convert_color_profile()` supports CMYK→CMYK (layers converted with lcms, intent).
* `gimp_image_assign_color_profile()` supports CMYK (relabel).
* `gimp_image_import_color_profile()`: for CMYK images the policy "convert to preferred/builtin"
  means convert to the cmyk preference profile/builtin CMYK (not RGB).
* `gimp_image_get_color_transform_to_srgb_*()` work for CMYK images (lcms).

## 6. app/core/gimpimage-convert-type.h (decl by 01, owner 03)
```c
gboolean gimp_image_convert_type      (GimpImage *image, GimpImageBaseType new_type,
                                       GimpColorProfile *dest_profile, GimpProgress *progress,
                                       GError **error);    /* == _full(..., PERCEPTUAL, TRUE) */
gboolean gimp_image_convert_type_full (GimpImage                *image,
                                       GimpImageBaseType         new_type,
                                       GimpColorProfile         *dest_profile, /* nullable */
                                       GimpColorRenderingIntent  intent,
                                       gboolean                  bpc,
                                       GimpProgress             *progress,
                                       GError                  **error);
```
Allowed: RGB/GRAY/INDEXED → CMYK, CMYK → RGB/GRAY, RGB⇄GRAY, (INDEXED as before). CMYK →
INDEXED is not handled here (indexed conversion is separate and refuses CMYK). 01's stub:
`_full` returns FALSE with `GIMP_ERROR, GIMP_FAILED, "not implemented"` for CMYK and forwards
otherwise.

## 7. app/core/gimpdrawable.h (owner 01)
```c
gboolean gimp_drawable_is_cmyk (GimpDrawable *drawable);
```
`gimp_drawable_get_component_format()/index()` mirror the image functions.

## 8. Compositing contract (owner 02; consumers 03, 08 and the paint part of 02)
* `gimp_layer_mode_get_format (mode, blend_space, composite_space, composite_mode, preferred)`
  returns `babl_format_with_space ("CMYKA float", preferred)` when `gimp_babl_format_is_cmyk
  (preferred)`, regardless of mode/space (when composite_space is AUTO; explicit RGB composite
  spaces return RGB as today).
* Any `gimp:<layer-mode>` op whose input or aux source format is CMYK composites in CMYKA float;
  pixels with zero effective layer alpha are output bit-identical to input (I6); trivial modes do
  no color conversion at all.
* `gimp:mask-components` handles CMYK(A) formats (5 components, bit mapping of §1).
* `gimp_gegl_create_flatten_node()` unchanged signature; works on CMYK input.

## 9. Histogram contract (owner 08)
For CMYK drawables `GimpHistogram` has n_components 5 (C,M,Y,K,A); valid channels VALUE (max ink),
CYAN, MAGENTA, YELLOW, BLACK, ALPHA. `gimp_histogram_has_channel()` reports this.
New (08): `gimp_drawable_calculate_histogram_rgb (drawable, histogram, with_filters)` computing
on the RGB conversion, used by color tools on CMYK drawables.

## 10. PDB / libgimp (owner 05)
New PDB procedures (pdb/groups):
* `gimp-image-convert-cmyk` (image, color-profile: bytes nullable, intent, bpc) →
  libgimp hand-written `gboolean gimp_image_convert_cmyk (GimpImage *image,
  GimpColorProfile *profile /*nullable*/, GimpColorRenderingIntent intent, gboolean bpc);`
  (Since 3.4), wrapping `_gimp_image_convert_cmyk`.
* `gimp-drawable-is-cmyk` (drawable) → boolean → `gimp_drawable_is_cmyk()`.
Changed: `gimp-image-new(-with-precision)` accept CMYK; `gimp-layer-new` accepts CMYK(A)_IMAGE
(type must match image base type); `gimp-image-convert-indexed` errors on CMYK;
`gimp-image-convert-rgb/-grayscale` accept CMYK images; `gimp-image-get/set-component-*` and
`gimp-drawable-histogram` accept the new enum values; `gimp-image-set-color-profile` validates
CMYK rules. `GimpExportOptions` handles `GIMP_CMYK` (convert to RGB/GRAY/INDEXED action if the
exporter lacks `CAN_HANDLE_CMYK`).

## 11. XCF (owner 04)
Version 28. Image header base type 3; layer type 6/7; tiles raw bytes, bpp = 4/5 × component
size, same RLE/zlib code. The CMYK profile is the standard "icc-profile" parasite. Loader: if
the stored profile is not usable for CMYK, keep the default profile and warn (do not crash).

## 12. Plug-in conventions (owner 06, documented by 05 in libgimp docs)
1. `image = gimp_image_new_with_precision (w, h, GIMP_CMYK, precision)`;
2. immediately `gimp_image_set_color_profile (image, cmyk_profile)` (only if
   `gimp_color_profile_is_cmyk_bidirectional()`);
3. `gimp_layer_new (image, name, w, h, GIMP_CMYK(A)_IMAGE, 100, NORMAL)`;
4. write pixels with `gimp_drawable_get_format (drawable)` or an encoding-compatible format
   tagged with the *same* space (inverted `cmyk u8` with the image space is fine).
Exporters read with `babl_format_with_space ("CMYK(A) u8|u16", image_space)` or inverted
variants; no conversion happens.

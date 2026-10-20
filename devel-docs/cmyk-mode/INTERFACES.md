# CMYK mode — interface contracts (revision 2)

Implementation agents code against this file plus their own `WORKSTREAMS/<ws>.md`.
"Owner" = the workstream that implements the real behaviour. **01a** lands declarations, enums
and stubs for everything below within hours. Other workstreams may then call any function
declared here and rely on the stated behaviour once the owner has merged. Rev-2 changes are
marked **[R2]**. Invariants are numbered I1…I10 (PLAN §2 has the full list).

---------------------------------------------------------------------------------------------
## 1. Enums (owner 01a)

### libgimpbase/gimpbaseenums.h (public; GIR/PDB-visible; all values appended)
```c
GIMP_CMYK          = 3      /* GimpImageBaseType, desc "CMYK color" */
GIMP_CMYK_IMAGE    = 6      /* GimpImageType, desc "CMYK" */
GIMP_CMYKA_IMAGE   = 7      /*                desc "CMYK-alpha" */
GIMP_CHANNEL_CYAN  = 6, GIMP_CHANNEL_MAGENTA = 7, GIMP_CHANNEL_YELLOW = 8, GIMP_CHANNEL_BLACK = 9
GIMP_EXPORT_CAN_HANDLE_CMYK = 1 << 11      /* GimpExportCapabilities */
```
(Alpha of a CMYK image is still `GIMP_CHANNEL_ALPHA`.) All "Since: 3.4".

### app/core/core-enums.h
```c
/* GimpHistogramChannel: append after GIMP_HISTOGRAM_RGB (= 6, pdb-skip) */
GIMP_HISTOGRAM_CYAN = 7, GIMP_HISTOGRAM_MAGENTA = 8, GIMP_HISTOGRAM_YELLOW = 9, GIMP_HISTOGRAM_BLACK = 10
/* libgimp's GimpHistogramChannel therefore has a gap at 6 (RGB is pdb-skip). Intended. */

/* GimpComponentMask (pdb-skip) */
GIMP_COMPONENT_MASK_BLACK = 1 << 4,
GIMP_COMPONENT_MASK_ALL   = 0x1f
/* below the enum, as #defines (mkenums must not see aliases): */
#define GIMP_COMPONENT_MASK_CYAN    GIMP_COMPONENT_MASK_RED
#define GIMP_COMPONENT_MASK_MAGENTA GIMP_COMPONENT_MASK_GREEN
#define GIMP_COMPONENT_MASK_YELLOW  GIMP_COMPONENT_MASK_BLUE
```
**I7**: `gimp_image_get_active_mask()`, `gimp_image_get_visible_mask()`,
`gimp_drawable_get_active_mask()` always set the bits of components the image model does not
have (BLACK for RGB/GRAY/INDEXED). `mask == GIMP_COMPONENT_MASK_ALL` keeps meaning "all".
CMYK bit map: bit0 C, bit1 M, bit2 Y, bit3 alpha, bit4 K. Code that builds masks from explicit
bits (`R|G|B|A`, e.g. `gimpdrawablefilter.c`) must use `GIMP_COMPONENT_MASK_ALL` instead.

### app/core/core-types.h
`#define CYAN 0`, `MAGENTA 1`, `YELLOW 2`, `BLACK 3`, `ALPHA_C 4`. `MAX_CHANNELS` stays 5.
**[R2] Rule**: any stack buffer that receives a pixel in a pickable/drawable *native* format
is `gdouble pixel[MAX_CHANNELS]` (40 bytes = CMYKA double), never `[4]`.

### app/plug-in/plug-in-enums.h (owner 05; `plug-in-enums.c` is generated)
`GIMP_PLUG_IN_CMYK_IMAGE = 1 << 6`, `GIMP_PLUG_IN_CMYKA_IMAGE = 1 << 7`. Tokens `"CMYKA"`,
`"CMYK*"`, `"CMYK"`. The parser is prefix based: test `"CMYKA"` before `"CMYK*"` before `"CMYK"`.
`"*"` keeps meaning the six old types. This only drives **menu sensitivity**; PDB calls are not
type checked, so a script can still hand a CMYK image to an RGB-only plug-in (documented).

---------------------------------------------------------------------------------------------
## 2. app/gegl/gimp-babl.h (owner 01a)

Existing functions with CMYK behaviour:
* `gimp_babl_format (GIMP_CMYK, precision, with_alpha, space)` → `"CMYK <t>"` / `"CMYKA <t>"`
  for t ∈ u8, u16, u32, half, float, double, always `babl_format_with_space (name, space)`. The
  TRC of `precision` is ignored for CMYK.
* `gimp_babl_is_valid (GIMP_CMYK, p)` → TRUE only for `*_NON_LINEAR` precisions.
* `gimp_babl_format_get_base_type (fmt)` → `GIMP_CMYK` iff
  `babl_get_model_flags (babl_format_get_model (fmt)) & BABL_MODEL_FLAG_CMYK` (covers `CMYK`,
  `CMYKA`, `CaMaYaKaA`, `cmyk`, `cmykA`, `camayakaA`).
* `gimp_babl_format_get_trc (cmyk)` → `GIMP_TRC_NON_LINEAR`;
  `gimp_babl_format_get_precision (cmyk)` → `<t>_NON_LINEAR`.
* `gimp_babl_component_format (GIMP_CMYK, precision, index)`, index 0..4 →
  `"Cyan <t>"`, `"Magenta <t>"`, `"Yellow <t>"`, `"Key <t>"`, `"CMYK-A <t>"` (registered by
  `gimp_babl_init()` if missing; callers tag them with the image space).
* `gimp_babl_get_builtin_color_profile (GIMP_CMYK, any trc)` → the builtin CMYK profile (§3),
  not ref'd, same lifetime as the sRGB builtin.
* `gimp_babl_format_get_color_profile (fmt)`: CMYK fmt → profile made from the space's ICC
  (builtin if the space is NULL or not CMYK); RGB/Y fmt whose space is CMYK → the builtin
  sRGB/gray profile for its TRC (**I5**).
* `gimp_babl_format_get_description()`: "CMYK" / "CMYK-alpha" for every CMYK(A) type.
* `gimp_babl_compat_u8_format()` handles CMYK; `gimp_babl_format_get_image_type()`
  (gimp-babl-compat.h) → `GIMP_CMYK(A)_IMAGE`.
* `gimp_babl_init()` defensively registers (only if `! babl_format_exists()`): `CaMaYaKaA u32`
  (missing in babl 0.1.129; `CaMaYaKaA double` exists) and the component formats above.

New:
```c
gboolean gimp_babl_space_is_cmyk     (const Babl *space);   /* NULL-safe; babl_space_is_cmyk */
gboolean gimp_babl_format_is_cmyk    (const Babl *format);  /* NULL-safe; model flag test   */
gboolean gimp_babl_has_cmyk_support  (void);
/* Cached once per process: TRUE iff babl was built with lcms. [R2] Probe: convert CMYK float
 * (0.5,0,0,0) to R'G'B' float once tagged with the builtin profile's space and once space-less
 * (babl's naive formula); TRUE iff |R'_space - R'_naive| > 0.1 (measured: 0.535 vs 0.735 for
 * the builtin, 0.429 vs 0.735 for default_cmyk.icc). The rev-1 K100 probe does not work:
 * babl's lcms path uses BPC, so K100 maps to 0.0 on both paths.
 * Returns FALSE if the env var GIMP_TESTING_NO_CMYK is set at first call. */
```

**I5 (babl contract, asserted by tests F10)**: an RGB/Y format tagged with a CMYK babl space
means sRGB / linear sRGB. `CMYK(A) <t>` without space ↔ `CMYK(A) <t>` with a CMYK space is an
identity conversion (babl 0.1.129, measured). Python tests rely on that (they can only pass
format *names*). Every app-side CMYK *buffer* format still carries the image space.

---------------------------------------------------------------------------------------------
## 3. libgimpcolor (public; add to `gimpcolor.def`; "Since: 3.4"; owner 01a stub → 01b final)

### 3.1 Profile identity **[R2, B1]**
**I8 — one CMYK color space per equal profile.** Two CMYK profiles are the same color space iff
`gimp_color_profile_is_equal()` (byte equality *excluding* the 128-byte header; unchanged
function). Behaviour change of the existing function (signature unchanged):
```c
const Babl * gimp_color_profile_get_space (GimpColorProfile         *profile,
                                           GimpColorRenderingIntent  intent,
                                           GError                  **error);
/* [R2] For CMYK profiles: returns, for the lifetime of the process, the SAME Babl space for
 * all profiles that are gimp_color_profile_is_equal() - the space created for the first such
 * profile seen. Implementation (01a): static GHashTable keyed by
 * g_compute_checksum_for_data (G_CHECKSUM_SHA256, data + 128, length - 128) plus length,
 * value = const Babl *, guarded by a GMutex; entries never freed (babl never frees spaces).
 * babl itself keys spaces by ALL bytes (babl-space.c:296), header date included, which
 * makes header-only-different profiles different spaces and makes every CMYK->CMYK fish a
 * relcol re-separation (REVIEW-01 B1, measured: K100 -> (44573,44190,42768,48642)).
 * Intent is ignored for CMYK spaces (babl hard-codes relcol+BPC for CMYK).
 * RGB/gray profiles: unchanged (babl already dedups matrix spaces by value). */
```
Because libgimp plug-ins obtain spaces through the same function, the rule holds in every
process. The direct `babl_space_from_icc()` caller in `app/core/gimpsavable-load.c:2227` must
go through `gimp_color_profile_get_space()` (owner 03). `libgimp/gimp.c:1268,1290` (checker
colors, RGB only) stay.

### 3.2 New functions
```c
GimpColorProfile * gimp_color_profile_new_cmyk_builtin      (void);
/* New reference to a process-wide cached profile (GOnce) "GIMP built-in CMYK (generic,
 * not for press)". Deterministic bytes. Spec in 3.4. */

gboolean           gimp_color_profile_is_cmyk_bidirectional (GimpColorProfile *profile);
/* TRUE iff is_cmyk AND cmsIsIntentSupported (h, INTENT_RELATIVE_COLORIMETRIC,
 * LCMS_USED_AS_INPUT) AND (..., LCMS_USED_AS_OUTPUT). Every place that makes a babl space
 * for an image or a conversion TARGET checks this first: babl segfaults on RGB->CMYK with an
 * input-only profile (babl-fish-reference.c:901, measured). */
```

### 3.3 GimpColorTransform decision table **[R2, B1/M3]** (`gimp_color_transform_new()` and
`_new_proofing()`; owner 01b; 01a leaves current behaviour)
| src profile | dest profile | path |
|---|---|---|
| CMYK P | CMYK P' with `is_equal (P, P')` | **babl re-encoding on one space**: formats = src/dest encodings tagged with `gimp_color_profile_get_space (P)`; used even if `GIMP_COLOR_TRANSFORM_DISABLE_BABL` is set. Bit-exact for K-only data. |
| CMYK | anything else, or anything → CMYK | **lcms** (intent and BPC honoured), regardless of the env var |
| neither CMYK | neither CMYK | unchanged (babl unless env var) |

`gimp_color_transform_can_gegl_copy (src, dest)` → FALSE iff either is CMYK and
`! is_equal`. Proofing transforms with a CMYK source or proof profile use lcms (unchanged API).

### 3.4 Builtin CMYK profile spec (01a: stub; 01b: final)
Final (01b), validated by `/root/review-scratch/mkcmyk-rev2.c` (79 492 bytes, deterministic):
* ICC v4.3, class `prtr`, data space CMYK, PCS Lab, header rendering intent perceptual.
* AToB0 = `mAB`: identity tone curves (4) → 16-bit CLUT **9⁴** → identity curves (3). Sampled
  model (ink 0..1): `R' = bp + (1-bp)(1-C)(1-K)` (G', B' with M, Y), bp = 0.08, R'G'B' taken as
  sRGB-encoded, then sRGB → Lab D50 (lcms, relcol).
* BToA0 = `mBA`: identity curves (3) → 16-bit CLUT **17³** → identity curves (4). Inverse:
  Lab → sRGB', `t_i = clamp((X'-bp)/(1-bp))`, `tmax = max t_i`, `K = 1 - tmax`,
  `s = min (1, tmax / 0.1)`, `C_i = tmax > 0 ? (1 - t_i/tmax)·s : 0` (full GCR, CMY faded to 0
  near black: well conditioned; without the fade babl's K100 round trip gives C≈0.91).
* AToB1/2 and BToA1/2 **linked** (`cmsLinkTag`), `wtpt` D50, `desc` as above, `cprt`
  "Public Domain". A pipeline without the identity curve stages saves as 0 bytes (measured).
* After `cmsSaveProfileToMem`: header bytes 24–35 (date) := `07 EA 00 01 00 01 00 00 00 00 00 00`
  (2026-01-01 00:00:00), bytes 84–99 (profile ID) := 0. Size target < 100 KB.
* Stub (01a): same code with grids 3⁴ / 3³, no fade, desc "GIMP built-in CMYK (stub)".
* Serialization **[R2, M4 decision]**: GeglColor serialization keeps embedding the space's ICC
  bytes (XCF, config, wire, palettes, DnD) — no format change in MVP. Cost: ~106 KB base64 per
  serialized CMYK color in text formats. A "named builtin reference" scheme is deferred (needs
  coordinated changes in libgimpconfig, xcf, gpparams, palettes; PLAN §7).

### 3.5 lcms format mapping (`gimp_color_profile_get_lcms_format()`, owner 01b) **[R2, M3]**
| babl model | component type | substitute babl format | lcms type |
|---|---|---|---|
| `CMYK` | u8 / u16 | same | `TYPE_CMYK_8` / `TYPE_CMYK_16` |
| `CMYK` | u32, half, float, double | `CMYK u16` | `TYPE_CMYK_16` |
| `CMYKA` | u8 / u16 | same | `TYPE_CMYKA_8` (define it) / `TYPE_CMYKA_16` |
| `CMYKA` | u32, half, float, double | `CMYKA u16` | `TYPE_CMYKA_16` |
| `cmyk`, `cmykA` (inverted) | any | `CMYK u16` / `CMYKA u16` (babl inverts) | `_16` |
| `CaMaYaKaA`, `camayakaA` | any | `CMYKA u16` (babl un-premultiplies) | `TYPE_CMYKA_16` |
No CMYK model ever reaches the "falling back to float" RGB branch or `g_return_val_if_reached`.
Rationale: lcms float CMYK is 0..100 (babl rescales itself); routing through u16 avoids it; the
u16 quantization (1.5e-5) is below any visible threshold. The lcms path strips spaces from the
formats (`gimpcolortransform.c:249`) — correct because of I5 (CMYK no-space ≡ space).
K-preserving intents (`INTENT_PRESERVE_K_ONLY_*`) are **deferred** (PLAN §7).

---------------------------------------------------------------------------------------------
## 4. app/core/gimpimage.h (owner 01a; file handed to 03 after 01a merges)

CMYK behaviour of: `gimp_image_get_format()`, `gimp_image_get_layer_format()`, projection format
(`CMYKA <precision>` in the image space), `gimp_image_get_component_format()` (space-tagged
component formats), `gimp_image_get_component_index()` (CYAN..BLACK → 0..3, ALPHA → ALPHA_C),
`gimp_image_{set,get}_component_{active,visible}()`, masks (I7; also fix GRAY using
`visible[ALPHA]` instead of `ALPHA_G`, `gimpimage.c:4076`), `gimp_image_get_xcf_version()`
(any CMYK → ≥ 28, reason "CMYK images were added in GIMP 3.4"; `case 28` → 340 / "GIMP 3.4").

**[R2, M11] `gimp_image_get_layer_space()`**: never NULL for a CMYK image. If base type is CMYK
and the stored profile is NULL or not CMYK (transient state inside an undo/redo of a
conversion), it returns the builtin CMYK space; if base type is not CMYK and the stored profile
is CMYK (other transient), it returns NULL (sRGB). No profile is attached in these paths.

New:
```c
void _gimp_image_set_conversion_intent (GimpImage *image, GimpColorRenderingIntent intent, gboolean bpc);
void _gimp_image_get_conversion_intent (GimpImage *image, GimpColorRenderingIntent *intent, gboolean *bpc);
/* read by gimp_layer_real_convert_type() and gimp_group_layer_convert_type();
 * default PERCEPTUAL + bpc TRUE (today's behaviour). */
```
Construction: `gimp_image_new (gimp, w, h, GIMP_CMYK, precision)` attaches the default CMYK
profile in `constructed()` (no undo, not dirty). Preconditions (`g_return_val_if_fail`):
`gimp_babl_is_valid()` and `gimp_babl_has_cmyk_support()`. **[R2, m6]** It stays total for valid
callers: callers gate first (actions insensitive, PDB/XCF/loaders return an error).

**[R2, m8]** (03, after handover): setting `base-type` resets `visible[]`/`active[]` of all
components to TRUE (index 3 is alpha in RGB but K in CMYK).

---------------------------------------------------------------------------------------------
## 5. app/core/gimpimage-color-profile.h (01a declarations/validation → 01b conversions)
```c
GimpColorProfile * gimp_get_default_cmyk_profile (Gimp              *gimp,
                                                  GimpColorProfile  *preferred,  /* nullable */
                                                  GError           **error);     /* nullable */
/* NEW reference, never NULL: preferred if CMYK+bidirectional, else the color-config
 * cmyk-profile if loadable+CMYK+bidirectional, else the builtin. A skipped candidate sets
 * *error (first only); the call still succeeds. */
```
* `gimp_image_validate_color_profile_by_format()`: CMYK formats require
  `gimp_color_profile_is_cmyk_bidirectional()` (errors "Color profile is not for CMYK color
  space" / "CMYK color profile cannot be used for conversion to CMYK (missing output
  tables)"). (01a)
* **[R2, M11] I1 is enforced only at API entry points**: `gimp_image_set_color_profile()` and
  `gimp_image_set_icc_profile()` with NULL on a CMYK image attach the default CMYK profile
  (with undo, like any profile set); XCF load end (04); `gimp_image_new()` (01a); PDB
  `gimp-image-detach-parasite "icc-profile"` on CMYK → error (05). Never in
  `gimp_image_parasite_attach/detach()`, `_gimp_image_update_color_profile()` or undo pops.
* `gimp_image_get_builtin_color_profile()` → CMYK builtin for CMYK images. (01a)
* (01b) `gimp_image_convert_color_profile()`: CMYK→CMYK with intent/BPC (lcms, §3.3; equal
  profiles: no-op as today). `gimp_image_assign_color_profile()`: CMYK relabel, then
  `gimp_image_fix_layer_format_spaces()`. `gimp_image_import_color_profile()`: for CMYK images,
  "convert to preferred" = cmyk-profile preference / builtin, never RGB.
  `gimp_image_get_color_transform_{to,from}_srgb_*()` work for CMYK images (lcms).
* "Use sRGB"/hidden-profile machinery does not apply to CMYK (`_gimp_image_set_hidden_profile`
  is a no-op for CMYK).

---------------------------------------------------------------------------------------------
## 6. app/core/gimpimage-convert-type.h (decl + stub by 01a; owner 03)
```c
gboolean gimp_image_convert_type      (GimpImage *image, GimpImageBaseType new_type,
                                       GimpColorProfile *dest_profile, GimpProgress *progress,
                                       GError **error);       /* == _full (..., PERCEPTUAL, TRUE) */
gboolean gimp_image_convert_type_full (GimpImage                *image,
                                       GimpImageBaseType         new_type,
                                       GimpColorProfile         *dest_profile, /* nullable */
                                       GimpColorRenderingIntent  intent,
                                       gboolean                  bpc,
                                       GimpProgress             *progress,
                                       GError                  **error);
```
Allowed: RGB/GRAY/INDEXED → CMYK, CMYK → RGB/GRAY, RGB⇄GRAY. CMYK → INDEXED refused
(`gimp_image_convert_indexed()` errors "Convert the image to RGB first"). 01a stub: `_full`
returns FALSE + `GIMP_ERROR_FAILED "not implemented"` when CMYK is involved, else forwards.

**[R2, M11] Undo push order inside the `IMAGE_CONVERT` group** (03):
1. map legacy layer modes to non-legacy (`gimp_layer_set_mode (…, push_undo TRUE)`) when the
   target is CMYK (I9) — *before* the base type changes, so undo restores legacy modes while
   the image is RGB again;
2. precision undo + set `*_NON_LINEAR` (if the TRC changes);
3. image-type undo + set base type;
4. `gimp_image_set_color_profile (image, dest_profile)` (validated against the *new* base
   type; for CMYK→RGB with sRGB target this detaches the parasite);
5. `_gimp_image_set_conversion_intent()`, convert every drawable with explicit src/dest
   profiles (src captured before step 3), reset the intent;
6. `gimp_image_fix_layer_format_spaces()` as a safety net; group end; `mode_changed`.
Undo pops in reverse; the transient states are absorbed by §4's `get_layer_space()` rule.

---------------------------------------------------------------------------------------------
## 7. app/core/gimpdrawable.h (owner 01a)
`gboolean gimp_drawable_is_cmyk (GimpDrawable *drawable);` — `get_component_format/index()`
mirror the image functions.

---------------------------------------------------------------------------------------------
## 8. Compositing contract (owner 02) **[R2: rewritten for B2, B3, M1, M2, M8]**

### 8.1 Definitions
* **CMYK-composite op**: a `GimpOperationLayerMode` instance with `cmyk == TRUE` (8.3).
* **Colour-agnostic modes** = modes whose op has no blend function (`blend_function == NULL`):
  Normal, Dissolve, Behind, Erase, Anti-erase, Merge, Split, Pass-through, Replace, Overwrite.
  In CMYK they do **no color conversion at all**: per-channel composite of inks. (This is not
  `GIMP_LAYER_MODE_FLAG_TRIVIAL`, which also contains Normal-legacy and lacks Behind and
  Anti-erase; the flag keeps its current meaning inside `gimp_layer_mode_get_format()`.)
* **Blend modes** = the other non-legacy modes (Multiply … LCh): blend in their RGB/Lab blend
  space through the CMYK converter (8.5), composite in CMYK.
* **Legacy modes** (`GIMP_LAYER_MODE_FLAG_LEGACY`): not available in CMYK images (**I9**: no
  layer of a CMYK image has a legacy mode). In paint/fill paths they are replaced by
  `gimp_layer_mode_get_non_legacy()`. A legacy op that still meets CMYK data in a graph falls
  back to compositing in its RGB space (lossy safety net, `g_warning` once).

### 8.2 Functions (app/operations/layer-modes/gimp-layer-modes.h, app/gegl/gimp-gegl-nodes.h)
```c
gboolean        gimp_layer_mode_is_cmyk_native  (GimpLayerMode mode);   /* ! is_legacy */
GimpLayerMode   gimp_layer_mode_get_non_legacy  (GimpLayerMode mode);
/* FOO_LEGACY -> FOO (all 22 legacy modes have a same-named non-legacy mode); others: identity */

const Babl    * gimp_layer_mode_get_format      (GimpLayerMode          mode,
                                                 GimpLayerColorSpace    blend_space,
                                                 GimpLayerColorSpace    composite_space,
                                                 GimpLayerCompositeMode composite_mode,
                                                 const Babl            *preferred_format);
/* [R2] If gimp_babl_format_is_cmyk (preferred_format) AND composite_space == AUTO (the
 * caller's UNRESOLVED value): babl_format_with_space ("CMYKA float", preferred_format), for
 * every mode (legacy modes meaning their non-legacy equivalent). Otherwise exactly today's
 * result (explicit composite spaces give RGB/Lab formats tagged with preferred's space). */

GeglOperation * gimp_layer_mode_get_operation             (GimpLayerMode mode);  /* unchanged */
GeglOperation * gimp_layer_mode_get_operation_for_format  (GimpLayerMode mode,
                                                           const Babl   *format);
/* [R2, B2] For the paint core. format = the paint buffer format.
 * - format not CMYK: returns gimp_layer_mode_get_operation (mode) - same shared instance,
 *   pads untouched, bit-identical RGB behaviour.
 * - format CMYK: returns a process-wide shared instance for (non_legacy (mode), format),
 *   created once (GHashTable + GMutex, freed in gimp_layer_modes_exit()) with
 *   gegl_operation_set_format() on "input", "aux", "output" (= format) and "aux2" (Y float,
 *   same space), fixed_format = format, cmyk_native = TRUE, the info fields set as in
 *   gimp_layer_mode_get_operation(), composite_mode = paint composite mode, and its fish
 *   cache primed. n_components therefore comes out as 5 everywhere. Read-only after
 *   creation except `opacity` (written per stroke, as today). */

void gimp_gegl_mode_node_set_mode      (GeglNode *node, GimpLayerMode mode,
                                        GimpLayerColorSpace blend_space,
                                        GimpLayerColorSpace composite_space,
                                        GimpLayerCompositeMode composite_mode);
/* unchanged signature == _full (..., cmyk_native = (composite_space == AUTO)) */
void gimp_gegl_mode_node_set_mode_full (GeglNode *node, GimpLayerMode mode,
                                        GimpLayerColorSpace blend_space,
                                        GimpLayerColorSpace composite_space,
                                        GimpLayerCompositeMode composite_mode,
                                        gboolean cmyk_native);
/* sets op property "cmyk-native" (and all others; setting "operation" recreates the op) */

GeglNode * gimp_gegl_create_flatten_node (GeglColor           *background,
                                          const Babl          *format,          /* [R2] nullable */
                                          GimpLayerColorSpace  composite_space);
/* [R2, B3] format CMYK: background gegl:color with format "CMYKA float" in format's space
 * (an image-space CMYK GeglColor converts exactly, an RGB one via babl relcol+BPC), mode node
 * set_mode_full (NORMAL, AUTO, AUTO, AUTO, TRUE); composite_space is ignored (may be AUTO).
 * format NULL / not CMYK: today's behaviour (composite_space must be an RGB space).
 * gimp_gegl_apply_flatten() keeps its signature and passes gegl_buffer_get_format (dest). */
```

### 8.3 Op rule (`GimpOperationLayerMode`, resolved in `prepare()` and in `cache_fishes()`)
New members: `gboolean prop_cmyk_native` (property `"cmyk-native"`, default TRUE),
`const Babl *fixed_format` (NULL for graph ops), `gboolean cmyk`, `const Babl *blend_format`,
`const GimpLayerModeCmykConverter *cmyk_converter`.
```
cmyk = prop_cmyk_native && gimp_layer_mode_is_cmyk_native (layer_mode) &&
       (is_cmyk (source format "input") || is_cmyk (source format "aux") || is_cmyk (fixed_format))
space = first CMYK space among input, aux, fixed_format
```
* `cmyk`: pads "input", "aux", "output" = `CMYKA float (space)`, "aux2" = `Y float (space)`;
  non-CMYK sources (e.g. a GEGL filter's RGBA output on aux) are converted by GEGL with babl
  (that is intended: the filtered region goes through RGB, everything else does not).
* `cache_fishes()` calls `gimp_layer_mode_get_format()` with
  `composite_space = (prop_cmyk_native && is_cmyk_native (mode)) ? AUTO : op->composite_space`,
  so legacy ops and layers with an explicit RGB composite space get RGB formats (as today).
* **B3 rule in words**: in a CMYK image every layer, filter, fill and paint composite is CMYK,
  unless the layer's (or filter's) *stored* composite-space property is explicitly an RGB
  space. `gimp_layer_update_mode_node()` (owned by 02 inside 03's `gimplayer.c`) calls
  `set_mode_full (…, effective spaces …, cmyk_native = gimp_layer_get_composite_space (layer)
  == GIMP_LAYER_COLOR_SPACE_AUTO)`; in the show-mask branch `cmyk_native = TRUE`. Applicator
  and drawable filters pass their stored (unresolved) composite space to the 5-arg variant.

### 8.4 `real_process` and blend functions **[M1]**
* All 33 blend functions in `gimpoperationlayermode-blend.c` use `n_components = 4` (constant)
  and get their format from `gimp_operation_layer_mode_get_blend_format (op)` = `op->blend_format`
  when `op->cmyk`, else `gegl_operation_get_format (op, "input")` (today's value, keeps RGB
  bit-identical, including the luminance/luma fishes at `:979`).
* `real_process`: composite stride `nc` (5 when `cmyk`, else 4) for `in`, `layer`, `out`,
  `blend_out`(composite side); blend stride 4 for scratch `blend_in`, `blend_layer`,
  `blend_out4` (alloca, bounded by `GIMP_COMPOSITE_BLEND_MAX_SAMPLES` as today). The run
  bookkeeping (`first/last/count`) is sample-indexed. Only samples with `in[α] ≠ 0 &&
  layer[α] ≠ 0` are converted and blended (as today). CMYK: convert `in`, `layer` → blend
  with the converter; blend; convert `blend_out4` → `blend_out` (stride 5); composite with
  `composite_union/clip/intersection (…, nc)`. RGB path: unchanged arithmetic.
* `cache_fishes()` CMYK branch: no babl space fishes; `blend_format` =
  `"RGBA float" | "R'G'B'A float" | "R~G~B~A float" | "CIE Lab alpha float"` per the resolved
  blend space, tagged with NULL space (sRGB, I5); `cmyk_converter` for `space`.
* SSE2/SSE4 normal and composite paths already fall back for `n_components != 4` — no change.
* **I6**: for U8/U16/HALF/FLOAT drawables, pixels whose effective layer alpha (opacity × mask
  × layer alpha) is 0 are output bit-identical to input; colour-agnostic modes produce the
  exact per-channel composite. U32/DOUBLE are limited by float compositing (as RGB today).

### 8.5 CMYK converter (new file `app/operations/layer-modes/gimpoperationlayermode-cmyk.{c,h}`) **[M2]**
```c
typedef struct _GimpLayerModeCmykConverter GimpLayerModeCmykConverter;
const GimpLayerModeCmykConverter *
     gimp_layer_mode_cmyk_converter_get        (const Babl *cmyk_space);
     /* process-wide cache keyed by space (GMutex); never NULL for a CMYK space */
void gimp_layer_mode_cmyk_converter_to_blend   (const GimpLayerModeCmykConverter *conv,
                                                const Babl *blend_format,
                                                const gfloat *cmyka, gfloat *blend, gint n);
void gimp_layer_mode_cmyk_converter_from_blend (const GimpLayerModeCmykConverter *conv,
                                                const Babl *blend_format,
                                                const gfloat *blend, gfloat *cmyka, gint n);
void gimp_layer_mode_cmyk_converters_exit      (void);
```
Implementation: two `GimpColorTransform`s (01b lcms path) created once per space:
`CMYKA u16` (profile from `babl_space_get_icc (space)`) ⇄ `R'G'B'A u16` (sRGB), relcol + BPC,
**without** `NOOPTIMIZE` (lcms then precomputes a device link). Per call: babl `CMYKA float →
CMYKA u16`, `gimp_color_transform_process_pixels()`, babl `R'G'B'A u16 → blend_format`, and the
reverse. Measured on this box (lcms 2.14, builtin profile): CMYK16→sRGB16 ≈ 17–20 Mpx/s,
sRGB16→CMYK16 ≈ 24 Mpx/s, vs babl reference fish 0.9 / 2.2 Mpx/s (REVIEW M2). Linear-RGB or
float lcms transforms are 3–4 Mpx/s: do not use them. Results match babl (relcol+BPC through
sRGB) within 2/255 (test C10). If a transform cannot be created, fall back to babl fishes.

### 8.6 Other compositing-side contracts
* `gimp:mask-components` handles CMYK(A) formats (5 components; bit map §1; bpp 5/10/20/40
  variants of the fast paths); passthrough still keyed on `GIMP_COMPONENT_MASK_ALL`.
* Group layers: projection format `CMYKA <precision>` in the image space;
  `gimp_group_layer_convert_type()` (02, on 03's behalf) reads `_gimp_image_get_conversion_intent()`.
* Paint (02): `DoLayerBlend` uses `gimp_layer_mode_get_operation_for_format (paint_mode,
  paint_buf_format)`. Paint/fill code maps legacy paint modes with
  `gimp_layer_mode_get_non_legacy()` when the drawable is CMYK. MyPaint brush: refused on CMYK
  drawables (`gimp_mybrush_core_start()` returns FALSE with "MyPaint Brush does not support CMYK
  layers"; the tool is insensitive, owner 08). Smudge accumulates in `CMYKA float` (image
  space) for CMYK; heal works in the drawable's float format; `gimp_gegl_convolve()` uses
  `total[MAX_CHANNELS]`.
* Gradients render in RGB and are converted (documented limitation: a K-only gradient becomes
  4-colour). Bucket fill / edit fill with a color set the native pixel (exact if the GeglColor
  is in the image space).

---------------------------------------------------------------------------------------------
## 9. Histogram contract (owner 08)
CMYK drawables: `GimpHistogram` n_components 5 (C,M,Y,K,A), computed on `CMYKA float (space)`.
Valid channels: VALUE (= max ink of C,M,Y,K), CYAN, MAGENTA, YELLOW, BLACK, ALPHA;
`gimp_histogram_has_channel()` reports exactly these. **[R2, m7]** Storage slots are not indexed
by the enum value for CMYK: an internal `gimp_histogram_channel_to_slot()` maps VALUE→0,
CYAN→1, MAGENTA→2, YELLOW→3, BLACK→4, ALPHA→5 (RGB keeps its current enum = slot layout), and
`has_channel (ALPHA)` tests `n_components == 2 || 4 || 5` with the base type.
New: `gimp_drawable_calculate_histogram_rgb (drawable, histogram, with_filters)` — histogram of
the RGB conversion, used by color tools on CMYK drawables.

---------------------------------------------------------------------------------------------
## 10. Previews and thumbnails **[R2, M7]** (owners: 03 formats, 07 renderer, 05 PDB docs)
* `gimp_drawable_get_preview_format()` / `gimp_image_get_preview_format()` for CMYK return
  `babl_format ("R'G'B'A u8")` (with alpha) / `"R'G'B' u8"` (without) — **sRGB, NULL space**.
  The preview is produced by the existing `gegl_buffer_get` scaling into that format (babl,
  relcol+BPC through sRGB; previews are small).
* `gimp_view_renderer_get_color_transform()` (07): if the viewable's color profile is CMYK, it
  uses the sRGB profile as the source profile (the preview buffer's real profile). Same rule for
  any other code that pairs `gimp_color_managed_get_color_profile()` with a preview buffer
  (`gimpviewrendererimage.c`, `gimpviewrendererdrawable.c`, component editor previews use
  component formats → gray, unaffected).
* PDB thumbnails (`gimp-drawable-thumbnail`, `gimp-image-thumbnail`) therefore stay RGB(A) u8
  (bpp 3/4) for CMYK; libgimp decoding by bpp (`libgimp/gimpdrawable.c:152`) is unchanged; 05
  documents "thumbnails of CMYK images are sRGB".
* `libgimpwidgets/gimppreviewarea.c` (05): the public `GimpImageType` API refuses the CMYK
  types (`g_return_if_fail`, gtk-doc: "convert to RGB first"); plug-in previews of CMYK
  drawables (`libgimp/gimpdrawablepreview.c`, 05) fetch `R'G'B'(A) u8` from libgimp.

---------------------------------------------------------------------------------------------
## 11. Cross-image and color-picking rules **[R2, M9/M13]** (one line each; owner in brackets)
* **Same-profile copies are exact** (I8 + §3.3): paste, paste-as-new-layer/image, "paste in
  place", new-layer-from-visible, layer DnD between two CMYK images with equal profiles,
  open-as-layers of an XCF with an equal profile → bit-identical inks. [03: gimp-edit.c,
  gimplayer-new.c (add an `is_equal` short-circuit to `gimp_layer_new_convert_buffer()` that
  does a plain `gimp_gegl_buffer_copy()`), gimpbuffer.c, gimplayer.c convert]
* **RGB ↔ CMYK paste/DnD** converts with the destination image's profile, PERCEPTUAL + BPC
  (today's cross-profile rule), via `gimp_item_convert()` / `gimp_layer_new_convert_buffer()`. [03]
* **CMYK ↔ CMYK different profiles**: converted with lcms, PERCEPTUAL + BPC (re-separates; that
  is a real profile change). [03]
* **Floating selections** keep the target drawable's format with alpha (CMYKA); anchoring
  composites with Normal, CMYK native → exact. [03 floating-sel logic, 02 compositing]
* **System clipboard** (export to other apps) goes through pixbuf R'G'B'A u8 (babl). [07]
* **Color picker / pointer / sample points** report the native CMYK values (GeglColor in the
  image space); buffers `MAX_CHANNELS`; average mode averages in the native format (CMYKA
  double, image space) for CMYK, not `RaGaBaA double` (m12). [08]
* **FG/BG colors**: color dialog, FG/BG editor, color history and the CMYK color selector create
  colors in the **active image's CMYK space** when the image is CMYK (not the simulation
  profile), so fills and dabs are exact; picking sets FG to the picked native color. [08]
* **Text layers** [08 owns app/text]: if the text has one solid color, no markup colors and no
  outline, and the image is CMYK: render glyph coverage to a `CAIRO_FORMAT_A8` surface and fill
  the layer with the native CMYK color, alpha = coverage (K-only text stays K-only). Otherwise
  render through RGB and convert (documented: rich black).
* **MyPaint brush**: unavailable on CMYK drawables (tool insensitive [08], core refuses [02]).

---------------------------------------------------------------------------------------------
## 12. PDB / libgimp (owner 05)
New procedures:
* `gimp-image-convert-cmyk` (image, color-profile: bytes nullable, intent, bpc) → libgimp
  hand-written `gboolean gimp_image_convert_cmyk (GimpImage *image, GimpColorProfile *profile
  /*nullable*/, GimpColorRenderingIntent intent, gboolean bpc);` (Since 3.4) wrapping
  `_gimp_image_convert_cmyk`.
* `gimp-drawable-is-cmyk` (drawable) → boolean.
Changed: `gimp-image-new(-with-precision)` accept CMYK (error without CMYK support or with a
linear precision); `gimp-layer-new` accepts CMYK(A)_IMAGE (must match the image base type);
`gimp-layer-set-mode`/`gimp-layer-new` with a legacy mode on a CMYK image → mapped to the
non-legacy mode (core rule I9, documented); `gimp-image-convert-indexed` errors on CMYK;
`gimp-image-convert-rgb/-grayscale` accept CMYK; component/histogram procs accept the new enum
values; `gimp-image-set-color-profile` validates CMYK rules (NULL on CMYK → default profile);
`gimp-image-detach-parasite "icc-profile"` on CMYK → error. `GimpExportOptions` handles
`GIMP_CMYK` (adds a "convert to RGB" step when the exporter lacks `CAN_HANDLE_CMYK`).

## 13. XCF (owner 04)
Version 28. Header base type 3 accepted only when `file_version >= 28` (`xcf-load.c:350`
`> GIMP_INDEXED` check, and the layer-type switch `:3881-3916`); precision validated with
`gimp_babl_is_valid (GIMP_CMYK, p)`. Layer types 6/7; tiles raw bytes, bpp 4/5 × component
size, same RLE/zlib code. The profile is the standard "icc-profile" parasite. Load end: if a
CMYK image has no usable CMYK profile (missing, not CMYK, not bidirectional) → attach default
(no undo) and warn (I1). Legacy layer modes on CMYK layers → mapped (I9) with a warning.
Without CMYK support → clear error, no image.

## 14. Plug-in conventions (owner 06; documented by 05 in libgimp docs)
1. `gimp_image_new_with_precision (w, h, GIMP_CMYK, precision)`;
2. immediately `gimp_image_set_color_profile (image, profile)` iff
   `gimp_color_profile_is_cmyk_bidirectional (profile)`;
3. `gimp_layer_new (image, name, w, h, GIMP_CMYK(A)_IMAGE, 100, NORMAL)`;
4. write pixels with `gimp_drawable_get_format (drawable)`, or an encoding-compatible CMYK
   format tagged with the *same* space (inverted `cmyk u8` / `cmykA u8` with the image space is
   fine: babl inverts, no color conversion).
Exporters read with `babl_format_with_space ("CMYK(A) u8|u16" or inverted, image_space)`.

## 15. Test files and who writes them **[R2]**
01a creates every file below as a skeleton (one `g_test_skip` test or a passing Python stub)
and registers all of them in `app/tests/meson.build` (C: `app_tests`; Python: `tests`, and
appends `file-tiff/file-tiff:file-jpeg/file-jpeg:file-psd/file-psd` once to the shared
`GIMP_TESTING_PLUG_INS` of `test_env`). After that each file has exactly one owner:
`test-layer-modes-golden.c` + `layer-modes-golden.h` (01a), `test-cmyk-foundation.c` (01a→01b),
`test-cmyk-compositing.c` (02), `test-cmyk-image.c` (03), `test-xcf.c` (04),
`libgimp/tests/test-cmyk.py` (05), `test-cmyk-file-formats.py` (06),
`test-cmyk-histogram.c` (08), `test-cmyk-e2e.py`, `test-cmyk-nosupport.c`,
`cmyk-babl-probe.py`, `meson.build`, `gimp-app-test-utils.{c,h}` (09; 01a lands the initial
CMYK helpers there). Helpers you need beyond `gimp-app-test-utils` stay `static` in your file.

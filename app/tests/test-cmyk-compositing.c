/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 2026 The GIMP authors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* CMYK compositing tests C1-C13, P1/P2 (devel-docs/cmyk-mode/TESTING.md),
 * plus K preservation through group layers.  Owner: WS02.
 *
 * C1, C3 and C9 run at U16 and FLOAT, and additionally at the precision
 * selected with GIMP_TESTING_CMYK_PRECISION if that is another one.  P1/P2
 * only run with GIMP_TESTING_PERF set: "1" asserts the targets, any other
 * value only reports the timings.
 */

#include "config.h"

#include <string.h>
#include <math.h>

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"
#include "libgimpmath/gimpmath.h"

#include "widgets/widgets-types.h"

#include "widgets/gimpuimanager.h"

#include "gegl/gimp-babl.h"
#include "gegl/gimp-gegl-utils.h"

#include "operations/layer-modes/gimp-layer-modes.h"
#include "operations/layer-modes/gimpoperationlayermode-cmyk.h"

#include "core/gimp.h"
#include "core/gimpbrushgenerated.h"
#include "core/gimpchannel.h"
#include "core/gimpchannel-select.h"
#include "core/gimpcontainer.h"
#include "core/gimpcontext.h"
#include "core/gimpdrawable.h"
#include "core/gimpdrawable-edit.h"
#include "core/gimpdrawable-operation.h"
#include "core/gimpdynamics.h"
#include "core/gimpfilloptions.h"
#include "core/gimpgrouplayer.h"
#include "core/gimpimage.h"
#include "core/gimpimage-merge.h"
#include "core/gimplayer.h"
#include "core/gimplayer-new.h"
#include "core/gimplayer-floating-selection.h"
#include "core/gimppaintinfo.h"
#include "core/gimppickable.h"
#include "core/gimpprojection.h"

#include "paint/gimppaintcore.h"
#include "paint/gimppaintcore-stroke.h"
#include "paint/gimppaintoptions.h"

#include "tests.h"

#include "gimp-app-test-utils.h"


#define W 32
#define H 32

/* 1/65535 */
#define U16_STEP (1.0 / 65535.0)


static Gimp *gimp = NULL;


typedef struct
{
  const gchar   *name;
  GimpPrecision  precision;
} PrecisionCase;

static PrecisionCase precision_cases[3] =
{
  { "u16",   GIMP_PRECISION_U16_NON_LINEAR   },
  { "float", GIMP_PRECISION_FLOAT_NON_LINEAR },
  { NULL,    0                               }   /* the knob, if different */
};


/*  helpers  */

static gboolean
skip_without_cmyk (void)
{
  if (! gimp_babl_has_cmyk_support ())
    {
      g_test_skip ("babl has no CMYK support");
      return TRUE;
    }

  return FALSE;
}

static GimpImage *
new_image (gint          width,
           gint          height,
           GimpPrecision precision)
{
  return gimp_test_utils_create_cmyk_image (gimp, width, height, precision,
                                            NULL);
}

/* the image's native CMYKA format (projection format) */
static const Babl *
native_format (GimpImage *image)
{
  return gimp_babl_format (GIMP_CMYK, gimp_image_get_precision (image), TRUE,
                           gimp_image_get_layer_space (image));
}

/* the format "bit-identical" is checked in: native, except for U32/DOUBLE,
 * which composite in float (I6 is promised up to FLOAT, INTERFACES §8.4)
 */
static const Babl *
compare_format (GimpImage *image)
{
  switch (gimp_babl_format_get_component_type (native_format (image)))
    {
    case GIMP_COMPONENT_TYPE_U32:
    case GIMP_COMPONENT_TYPE_DOUBLE:
      return babl_format_with_space ("CMYKA float",
                                     gimp_image_get_layer_space (image));

    default:
      return native_format (image);
    }
}

static const Babl *
cmyk_format (GimpImage   *image,
             const gchar *name)
{
  return babl_format_with_space (name, gimp_image_get_layer_space (image));
}

static GeglBuffer *
projection_buffer (GimpImage *image)
{
  GimpProjection *projection = gimp_image_get_projection (image);

  gimp_pickable_flush (GIMP_PICKABLE (projection));

  return gimp_pickable_get_buffer (GIMP_PICKABLE (projection));
}

static GeglBuffer *
drawable_buffer (gpointer drawable)
{
  return gimp_drawable_get_buffer (GIMP_DRAWABLE (drawable));
}

static gpointer
read_native (GimpImage           *image,
             GeglBuffer          *buffer,
             const GeglRectangle *rect)
{
  return gimp_test_utils_read_patch (buffer, rect, native_format (image));
}

static gdouble *
read_double (GimpImage           *image,
             GeglBuffer          *buffer,
             const GeglRectangle *rect)
{
  return gimp_test_utils_read_patch (buffer, rect,
                                     cmyk_format (image, "CMYKA double"));
}

static void
assert_rect_identical (GimpImage           *image,
                       GeglBuffer          *a,
                       GeglBuffer          *b,
                       const GeglRectangle *rect,
                       const gchar         *what)
{
  gpointer data_a = gimp_test_utils_read_patch (a, rect, compare_format (image));
  gpointer data_b = gimp_test_utils_read_patch (b, rect, compare_format (image));
  gsize    size   = (gsize) rect->width * rect->height *
                    babl_format_get_bytes_per_pixel (compare_format (image));

  if (memcmp (data_a, data_b, size))
    {
      gdouble *da = read_double (image, a, rect);
      gdouble *db = read_double (image, b, rect);
      gint     i;

      for (i = 0; i < rect->width * rect->height * 5; i++)
        if (da[i] != db[i])
          {
            gint p = i / 5;

            g_printerr ("%s: first difference at (%d,%d): "
                        "(%g,%g,%g,%g,%g) vs (%g,%g,%g,%g,%g)\n", what,
                        rect->x + p % rect->width, rect->y + p / rect->width,
                        da[p * 5], da[p * 5 + 1], da[p * 5 + 2], da[p * 5 + 3], da[p * 5 + 4],
                        db[p * 5], db[p * 5 + 1], db[p * 5 + 2], db[p * 5 + 3], db[p * 5 + 4]);
            break;
          }

      g_free (da);
      g_free (db);

      g_error ("%s: pixels in (%d,%d %dx%d) are not bit-identical",
               what, rect->x, rect->y, rect->width, rect->height);
    }

  g_free (data_a);
  g_free (data_b);
}

/* seeded CMYKA content: inks in [0, 1], alpha uniform in [alpha_min, 1],
 * clamped to [0, 1] (alpha_min < 0 gives some exact zeros)
 */
static void
fill_seeded (GeglBuffer          *buffer,
             const GeglRectangle *rect,
             guint32              seed,
             gdouble              alpha_min)
{
  GRand   *rand = g_rand_new_with_seed (seed);
  gdouble *data;
  gint     i;

  if (! rect)
    rect = gegl_buffer_get_extent (buffer);

  data = g_new (gdouble, (gsize) rect->width * rect->height * 5);

  for (i = 0; i < rect->width * rect->height; i++)
    {
      gint c;

      for (c = 0; c < 4; c++)
        data[i * 5 + c] = g_rand_double (rand);

      if (alpha_min >= 1.0)
        {
          data[i * 5 + 4] = 1.0;
        }
      else
        {
          gdouble alpha = g_rand_double_range (rand, alpha_min, 1.0);

          data[i * 5 + 4] = CLAMP (alpha, 0.0, 1.0);
        }
    }

  gegl_buffer_set (buffer, rect, 0,
                   babl_format_with_space ("CMYKA double",
                                           gegl_buffer_get_format (buffer)),
                   data, GEGL_AUTO_ROWSTRIDE);

  g_free (data);
  g_rand_free (rand);
}

static GeglColor *
cmyk_color (GimpImage *image,
            gdouble    c,
            gdouble    m,
            gdouble    y,
            gdouble    k)
{
  GeglColor *color   = gegl_color_new (NULL);
  gdouble    cmyk[4] = { c, m, y, k };

  gegl_color_set_pixel (color, cmyk_format (image, "CMYK double"), cmyk);

  return color;
}

/* GimpContext stores FG/BG through gimp_color_set_alpha(), which must not
 * re-separate CMYK colors (libgimpcolor/gimpcolor.c, fixed by 01b).  Tests
 * that need an exact CMYK FG/BG assert it first, so a regression there is
 * reported as such instead of as a pixel mismatch.
 */
static gboolean
context_colors_are_exact (GimpImage *image)
{
  GeglColor *color = cmyk_color (image, 0.1, 0.2, 0.3, 0.4);
  gdouble    cmyk[4];
  gboolean   exact;

  gimp_color_set_alpha (color, 1.0);
  gegl_color_get_pixel (color, cmyk_format (image, "CMYK double"), cmyk);

  exact = (fabs (cmyk[0] - 0.1) < 1e-6 && fabs (cmyk[1] - 0.2) < 1e-6 &&
           fabs (cmyk[2] - 0.3) < 1e-6 && fabs (cmyk[3] - 0.4) < 1e-6);

  g_object_unref (color);

  return exact;
}

#define ASSERT_EXACT_CONTEXT_COLORS(image) \
  g_assert_true (context_colors_are_exact (image))

static void
add_precision_tests (const gchar   *path,
                     GTestDataFunc  func)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (precision_cases); i++)
    {
      gchar *name;

      if (! precision_cases[i].name)
        continue;

      name = g_strdup_printf ("%s/%s", path, precision_cases[i].name);
      g_test_add_data_func (name, &precision_cases[i], func);
      g_free (name);
    }
}

static GList *
get_modes (GimpLayerModeContext context)
{
  GEnumClass *enum_class = g_type_class_ref (GIMP_TYPE_LAYER_MODE);
  GList      *modes      = NULL;
  gint        i;

  for (i = 0; i < enum_class->n_values; i++)
    {
      GimpLayerMode mode = enum_class->values[i].value;

      if (mode < 0 || gimp_layer_mode_is_legacy (mode))
        continue;

      if (gimp_layer_mode_get_context (mode) & context)
        modes = g_list_prepend (modes, GINT_TO_POINTER (mode));
    }

  g_type_class_unref (enum_class);

  return g_list_reverse (modes);
}


/*  C1: K preservation, Normal 50 %  */

static void
c1_k_preservation_full (const PrecisionCase *pc,
                        GimpLayerColorSpace  composite_space,
                        gdouble             *left_out)
{
  static const gdouble  bg_ink[5]  = { 0.0, 0.0, 0.0, 1.0, 1.0 };
  static const gdouble  top_ink[5] = { 1.0, 0.0, 0.0, 0.0, 1.0 };
  const GeglRectangle   left       = { 0,     0, W / 2, H };
  const GeglRectangle   right      = { W / 2, 0, W / 2, H };
  GimpImage            *image;
  GimpLayer            *bg;
  GimpLayer            *top;
  gdouble              *data;
  gint                  i;

  image = new_image (W, H, pc->precision);

  bg  = gimp_test_utils_add_cmyk_layer (image, "bg",  FALSE, bg_ink);
  top = gimp_test_utils_add_cmyk_layer (image, "top", TRUE,  NULL);

  gimp_test_utils_write_cmyk_patch (drawable_buffer (top), &left, top_ink);
  gimp_layer_set_opacity (top, 0.5, FALSE);
  gimp_layer_set_composite_space (top, composite_space, FALSE);

  /* uncovered half: bit-identical to the backdrop */
  if (composite_space == GIMP_LAYER_COLOR_SPACE_AUTO)
    assert_rect_identical (image, projection_buffer (image),
                           drawable_buffer (bg), &right, "C1 right half");

  data = read_double (image, projection_buffer (image), &left);

  if (composite_space == GIMP_LAYER_COLOR_SPACE_AUTO)
    {
      /* +-1 step of the precision (u8 or finer) */
      const gdouble eps = 1.5 * (pc->precision == GIMP_PRECISION_U8_NON_LINEAR ?
                                 1.0 / 255.0 : U16_STEP);

      for (i = 0; i < left.width * left.height; i++)
        {
          const gdouble *p = data + i * 5;

          /* per-channel lerp of the inks: (0.5, 0, 0, 0.5) */
          g_assert_cmpfloat_with_epsilon (p[0], 0.5, eps);
          g_assert_cmpfloat_with_epsilon (p[1], 0.0, eps);
          g_assert_cmpfloat_with_epsilon (p[2], 0.0, eps);
          g_assert_cmpfloat_with_epsilon (p[3], 0.5, eps);
          g_assert_cmpfloat (p[4], ==, 1.0);
        }
    }

  if (left_out)
    memcpy (left_out, data, 5 * sizeof (gdouble));

  g_free (data);
  g_object_unref (image);
}

static void
c1_k_preservation (gconstpointer data)
{
  if (skip_without_cmyk ())
    return;

  c1_k_preservation_full (data, GIMP_LAYER_COLOR_SPACE_AUTO, NULL);
}


/* independent implementation of the converter design (INTERFACES §8.5),
 * used as reference by C2 and C10
 */
static void
reference_transform (GimpImage    *image,
                     gboolean      to_rgb,
                     const Babl   *blend_format,
                     const gfloat *src,
                     gfloat       *dest,
                     gint          n)
{
  GimpColorProfile   *cmyk_profile = gimp_color_managed_get_color_profile (GIMP_COLOR_MANAGED (image));
  GimpColorProfile   *srgb_profile = gimp_color_profile_new_rgb_srgb ();
  const Babl         *cmyka_float  = cmyk_format (image, "CMYKA float");
  const Babl         *cmyka_u16    = cmyk_format (image, "CMYKA u16");
  const Babl         *rgba_u16     = babl_format ("R'G'B'A u16");
  GimpColorTransform *transform;
  guint16            *a16 = g_new (guint16, n * 5);
  guint16            *b16 = g_new (guint16, n * 5);
  gint                i;

  if (to_rgb)
    {
      transform = gimp_color_transform_new (cmyk_profile, cmyka_u16,
                                            srgb_profile, rgba_u16,
                                            GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                            GIMP_COLOR_TRANSFORM_FLAGS_BLACK_POINT_COMPENSATION);
      g_assert_nonnull (transform);

      babl_process (babl_fish (cmyka_float, cmyka_u16), src, a16, n);
      gimp_color_transform_process_pixels (transform, cmyka_u16, a16,
                                           rgba_u16, b16, n);
      babl_process (babl_fish (rgba_u16, blend_format), b16, dest, n);

      for (i = 0; i < n; i++)
        dest[i * 4 + 3] = src[i * 5 + 4];
    }
  else
    {
      transform = gimp_color_transform_new (srgb_profile, rgba_u16,
                                            cmyk_profile, cmyka_u16,
                                            GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                            GIMP_COLOR_TRANSFORM_FLAGS_BLACK_POINT_COMPENSATION);
      g_assert_nonnull (transform);

      babl_process (babl_fish (blend_format, rgba_u16), src, a16, n);
      gimp_color_transform_process_pixels (transform, rgba_u16, a16,
                                           cmyka_u16, b16, n);
      babl_process (babl_fish (cmyka_u16, cmyka_float), b16, dest, n);

      for (i = 0; i < n; i++)
        dest[i * 5 + 4] = src[i * 4 + 3];
    }

  g_object_unref (transform);
  g_object_unref (srgb_profile);
  g_free (a16);
  g_free (b16);
}

static void
reference_to_blend (GimpImage    *image,
                    const Babl   *blend_format,
                    const gfloat *cmyka,
                    gfloat       *blend,
                    gint          n)
{
  reference_transform (image, TRUE, blend_format, cmyka, blend, n);
}

static void
reference_from_blend (GimpImage    *image,
                      const Babl   *blend_format,
                      const gfloat *blend,
                      gfloat       *cmyka,
                      gint          n)
{
  reference_transform (image, FALSE, blend_format, blend, cmyka, n);
}


/*  C2: Multiply against the converter design  */

static void
c2_multiply (void)
{
  const GeglRectangle  left  = { 0,     0, W / 2, H };
  const GeglRectangle  right = { W / 2, 0, W / 2, H };
  GimpImage           *image;
  GimpLayer           *bg;
  GimpLayer           *top;
  gfloat              *bg_data;
  gfloat              *top_data;
  gdouble             *out;
  gfloat              *ref;
  gfloat              *in_rgb;
  gfloat              *layer_rgb;
  const Babl          *cmyka_float;
  gdouble              max_diff = 0.0;
  gint                 n        = left.width * left.height;
  gint                 i;

  if (skip_without_cmyk ())
    return;

  image = new_image (W, H, GIMP_PRECISION_U16_NON_LINEAR);

  bg  = gimp_test_utils_add_cmyk_layer (image, "bg",  FALSE, NULL);
  top = gimp_test_utils_add_cmyk_layer (image, "top", TRUE,  NULL);

  fill_seeded (drawable_buffer (bg),  NULL,  11, 1.0);
  fill_seeded (drawable_buffer (top), &left, 12, 1.0);

  gimp_layer_set_mode (top, GIMP_LAYER_MODE_MULTIPLY, FALSE);

  assert_rect_identical (image, projection_buffer (image),
                         drawable_buffer (bg), &right, "C2 right half");

  /* reference: the design of INTERFACES §8.5, built independently of the
   * op: CMYKA u16 -> R'G'B'A u16 sRGB (GimpColorTransform, relcol + BPC),
   * linear RGB multiply, back the same way
   */
  cmyka_float = cmyk_format (image, "CMYKA float");

  bg_data  = gimp_test_utils_read_patch (drawable_buffer (bg),  &left, cmyka_float);
  top_data = gimp_test_utils_read_patch (drawable_buffer (top), &left, cmyka_float);
  out      = read_double (image, projection_buffer (image), &left);

  in_rgb    = g_new (gfloat, n * 4);
  layer_rgb = g_new (gfloat, n * 4);
  ref       = g_new (gfloat, n * 5);

  reference_to_blend (image, babl_format ("RGBA float"), bg_data,  in_rgb,    n);
  reference_to_blend (image, babl_format ("RGBA float"), top_data, layer_rgb, n);

  for (i = 0; i < n; i++)
    {
      gint c;

      for (c = 0; c < 3; c++)
        in_rgb[i * 4 + c] *= layer_rgb[i * 4 + c];

      in_rgb[i * 4 + 3] = 1.0f;
    }

  reference_from_blend (image, babl_format ("RGBA float"), in_rgb, ref, n);

  for (i = 0; i < n * 5; i++)
    max_diff = MAX (max_diff, fabs (out[i] - ref[i]));

  g_printerr ("C2: max |multiply - reference| = %.2f/65535\n",
              max_diff * 65535.0);

  /* the projection is u16: allow rounding of the result */
  g_assert_cmpfloat (max_diff, <=, 1.5 * U16_STEP);

  g_free (in_rgb);
  g_free (layer_rgb);
  g_free (ref);
  g_free (out);
  g_free (bg_data);
  g_free (top_data);
  g_object_unref (image);
}


/*  C3: I6 sweep over all non-legacy layer modes  */

static void
c3_zero_coverage (gconstpointer data)
{
  const PrecisionCase *pc    = data;
  const GeglRectangle  left  = { 0,     0, W / 2, H };
  const GeglRectangle  right = { W / 2, 0, W / 2, H };
  GimpImage           *image;
  GimpLayer           *bg;
  GimpLayer           *top;
  GeglBuffer          *reference;
  GList               *modes;
  GList               *list;
  gint                 n_modes = 0;

  if (skip_without_cmyk ())
    return;

  image = new_image (W, H, pc->precision);

  bg  = gimp_test_utils_add_cmyk_layer (image, "bg",  TRUE, NULL);
  top = gimp_test_utils_add_cmyk_layer (image, "top", TRUE, NULL);

  /* backdrop with varying alpha including exact zeros */
  fill_seeded (drawable_buffer (bg), NULL, 21, -0.2);

  /* top: covered left half with varying alpha; right half: random inks
   * with alpha exactly 0 (they must not leak)
   */
  fill_seeded (drawable_buffer (top), &left, 22, 0.05);
  {
    gdouble *junk = g_new (gdouble, right.width * right.height * 5);
    GRand   *rand = g_rand_new_with_seed (23);
    gint     i;

    for (i = 0; i < right.width * right.height * 5; i++)
      junk[i] = (i % 5 == 4) ? 0.0 : g_rand_double (rand);

    gegl_buffer_set (drawable_buffer (top), &right, 0,
                     cmyk_format (image, "CMYKA double"),
                     junk, GEGL_AUTO_ROWSTRIDE);

    g_rand_free (rand);
    g_free (junk);
  }

  /* reference: the backdrop alone */
  gimp_item_set_visible (GIMP_ITEM (top), FALSE, FALSE);
  reference = gimp_gegl_buffer_dup (projection_buffer (image), NULL);
  gimp_item_set_visible (GIMP_ITEM (top), TRUE, FALSE);

  gimp_layer_set_opacity (top, 0.6, FALSE);

  modes = get_modes (GIMP_LAYER_MODE_CONTEXT_LAYER);

  for (list = modes; list; list = g_list_next (list))
    {
      GimpLayerMode  mode = GPOINTER_TO_INT (list->data);
      gchar         *what;

      gimp_layer_set_mode (top, mode, FALSE);

      what = g_strdup_printf ("C3/%s mode %d", pc->name, mode);
      assert_rect_identical (image, projection_buffer (image), reference,
                             &right, what);
      g_free (what);

      n_modes++;
    }

  g_assert_cmpint (n_modes, >, 30);

  /* colour-agnostic modes composite inks per channel: Normal (union, opaque
   * backdrop) follows the op's float arithmetic exactly
   */
  {
    static const gdouble  bg_ink[5] = { 0.1, 0.0, 0.7, 0.9, 1.0 };
    const Babl           *cmyka_float = cmyk_format (image, "CMYKA float");
    gfloat               *in;
    gfloat               *layer;
    gfloat               *expected;
    gpointer              expected_native;
    gpointer              actual;
    gint                  bpp = babl_format_get_bytes_per_pixel (native_format (image));
    gint                  n   = left.width * left.height;
    gint                  i;

    gimp_test_utils_write_cmyk_patch (drawable_buffer (bg), NULL, bg_ink);
    gimp_layer_set_mode (top, GIMP_LAYER_MODE_NORMAL, FALSE);

    in       = gimp_test_utils_read_patch (drawable_buffer (bg),  &left, cmyka_float);
    layer    = gimp_test_utils_read_patch (drawable_buffer (top), &left, cmyka_float);
    expected = g_new (gfloat, n * 5);

    for (i = 0; i < n; i++)
      {
        const gfloat *s = in    + i * 5;
        const gfloat *l = layer + i * 5;
        gfloat       *d = expected + i * 5;
        gfloat        layer_alpha = l[4] * 0.6f;
        gint          b;

        d[4] = layer_alpha + s[4] - layer_alpha * s[4];

        if (d[4])
          {
            gfloat layer_weight = layer_alpha / d[4];
            gfloat in_weight    = 1.0f - layer_weight;

            for (b = 0; b < 4; b++)
              d[b] = l[b] * layer_weight + s[b] * in_weight;
          }
        else
          {
            for (b = 0; b < 4; b++)
              d[b] = s[b];
          }
      }

    expected_native = g_malloc (n * bpp);
    babl_process (babl_fish (cmyka_float, native_format (image)),
                  expected, expected_native, n);

    actual = read_native (image, projection_buffer (image), &left);

    if (memcmp (actual, expected_native, n * bpp))
      {
        gdouble *a = read_double (image, projection_buffer (image), &left);
        gdouble  max_diff = 0.0;

        for (i = 0; i < n * 5; i++)
          max_diff = MAX (max_diff, fabs (a[i] - expected[i]));

        g_free (a);

        g_error ("C3/%s: Normal is not the exact per-channel formula "
                 "(max diff %g)", pc->name, max_diff);
      }

    g_free (actual);
    g_free (expected_native);
    g_free (expected);
    g_free (in);
    g_free (layer);
  }

  g_list_free (modes);
  g_object_unref (reference);
  g_object_unref (image);
}


/*  C4: native fill  */

static GimpFillOptions *
fg_fill_options (GeglColor *fg)
{
  GimpContext     *context = gimp_get_user_context (gimp);
  GimpFillOptions *options;

  gimp_context_set_foreground (context, fg);
  gimp_context_set_paint_mode (context, GIMP_LAYER_MODE_NORMAL);
  gimp_context_set_opacity    (context, GIMP_OPACITY_OPAQUE);

  options = gimp_fill_options_new (gimp, context, TRUE);
  gimp_fill_options_set_style (options, GIMP_FILL_STYLE_FG_COLOR);

  return options;
}

static void
c4_native_fill (void)
{
  GimpImage       *image;
  GimpLayer       *layer;
  GimpFillOptions *options;
  GeglColor       *fg;
  guint16          pixel[5];
  gfloat           cmyk[4] = { 0.1, 0.2, 0.3, 0.4 };

  if (skip_without_cmyk ())
    return;

  image = new_image (W, H, GIMP_PRECISION_U16_NON_LINEAR);
  layer = gimp_test_utils_add_cmyk_layer (image, "layer", TRUE, NULL);

  ASSERT_EXACT_CONTEXT_COLORS (image);

  fg = gegl_color_new (NULL);
  gegl_color_set_pixel (fg, cmyk_format (image, "CMYK float"), cmyk);

  options = fg_fill_options (fg);
  gimp_drawable_edit_fill (GIMP_DRAWABLE (layer), options, NULL);

  gegl_buffer_get (drawable_buffer (layer), GEGL_RECTANGLE (W / 2, H / 2, 1, 1),
                   1.0, cmyk_format (image, "CMYKA u16"), pixel,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

  g_assert_cmpint (ABS (pixel[0] -  6554), <=, 1);
  g_assert_cmpint (ABS (pixel[1] - 13107), <=, 1);
  g_assert_cmpint (ABS (pixel[2] - 19661), <=, 1);
  g_assert_cmpint (ABS (pixel[3] - 26214), <=, 1);
  g_assert_cmpint (pixel[4], ==, 65535);

  g_object_unref (options);
  g_object_unref (fg);
  g_object_unref (image);
}


/*  C5: inactive component  */

static void
c5_component_active (void)
{
  static const gdouble  ink[5] = { 0.2, 0.2, 0.2, 0.2, 1.0 };
  GimpImage            *image;
  GimpLayer            *layer;
  GimpFillOptions      *options;
  GeglColor            *fg;
  gdouble               before[5];
  gdouble               after[5];

  if (skip_without_cmyk ())
    return;

  image = new_image (W, H, GIMP_PRECISION_U16_NON_LINEAR);
  layer = gimp_test_utils_add_cmyk_layer (image, "layer", TRUE, ink);

  ASSERT_EXACT_CONTEXT_COLORS (image);

  gimp_test_utils_read_cmyk_pixel (drawable_buffer (layer), 3, 3, before);

  gimp_image_set_component_active (image, GIMP_CHANNEL_CYAN, FALSE);

  fg      = cmyk_color (image, 1.0, 1.0, 1.0, 1.0);
  options = fg_fill_options (fg);
  gimp_drawable_edit_fill (GIMP_DRAWABLE (layer), options, NULL);

  gimp_test_utils_read_cmyk_pixel (drawable_buffer (layer), 3, 3, after);

  g_assert_cmpfloat (after[0], ==, before[0]);
  g_assert_cmpfloat (after[1], ==, 1.0);
  g_assert_cmpfloat (after[2], ==, 1.0);
  g_assert_cmpfloat (after[3], ==, 1.0);
  g_assert_cmpfloat (after[4], ==, 1.0);

  gimp_image_set_component_active (image, GIMP_CHANNEL_CYAN, TRUE);

  g_object_unref (options);
  g_object_unref (fg);
  g_object_unref (image);
}


/*  C6: hidden component  */

static void
c6_component_visible (void)
{
  const GeglRectangle  all = { 0, 0, W, H };
  GimpImage           *image;
  GimpLayer           *layer;
  gdouble             *before;
  gdouble             *after;
  gint                 i;

  if (skip_without_cmyk ())
    return;

  image = new_image (W, H, GIMP_PRECISION_U16_NON_LINEAR);
  layer = gimp_test_utils_add_cmyk_layer (image, "layer", FALSE, NULL);
  fill_seeded (drawable_buffer (layer), NULL, 61, 1.0);

  before = read_double (image, projection_buffer (image), &all);

  gimp_image_set_component_visible (image, GIMP_CHANNEL_BLACK, FALSE);

  after = read_double (image, projection_buffer (image), &all);

  for (i = 0; i < W * H; i++)
    {
      g_assert_cmpfloat (after[i * 5 + 0], ==, before[i * 5 + 0]);
      g_assert_cmpfloat (after[i * 5 + 1], ==, before[i * 5 + 1]);
      g_assert_cmpfloat (after[i * 5 + 2], ==, before[i * 5 + 2]);
      g_assert_cmpfloat (after[i * 5 + 3], ==, 0.0);
      g_assert_cmpfloat (after[i * 5 + 4], ==, before[i * 5 + 4]);
    }

  gimp_image_set_component_visible (image, GIMP_CHANNEL_BLACK, TRUE);

  g_free (before);
  g_free (after);
  g_object_unref (image);
}


/*  C7: filters with a selection  */

static void
c7_filter_mask (void)
{
  static const gchar  *ops[] = { "gegl:gaussian-blur", "gegl:invert-gamma" };
  const GeglRectangle  right = { W / 2, 0, W / 2, H };
  const GeglRectangle  left  = { 0,     0, W / 2, H };
  GimpImage           *image;
  GimpLayer           *layer;
  gint                 i;

  if (skip_without_cmyk ())
    return;

  image = new_image (W, H, GIMP_PRECISION_U16_NON_LINEAR);
  layer = gimp_test_utils_add_cmyk_layer (image, "layer", TRUE, NULL);
  fill_seeded (drawable_buffer (layer), NULL, 71, 0.5);

  gimp_channel_select_rectangle (gimp_image_get_mask (image),
                                 left.x, left.y, left.width, left.height,
                                 GIMP_CHANNEL_OP_REPLACE,
                                 FALSE, 0.0, 0.0, FALSE);

  for (i = 0; i < G_N_ELEMENTS (ops); i++)
    {
      GeglBuffer *before = gimp_gegl_buffer_dup (drawable_buffer (layer), NULL);
      GeglNode   *node;
      gchar      *what;
      gpointer    a, b;
      gsize       size;

      node = gegl_node_new_child (NULL, "operation", ops[i], NULL);

      if (! strcmp (ops[i], "gegl:gaussian-blur"))
        gegl_node_set (node, "std-dev-x", 3.0, "std-dev-y", 3.0, NULL);

      gimp_drawable_apply_operation (GIMP_DRAWABLE (layer), NULL, ops[i], node);
      g_object_unref (node);

      what = g_strdup_printf ("C7 %s unselected half", ops[i]);
      assert_rect_identical (image, drawable_buffer (layer), before,
                             &right, what);
      g_free (what);

      /* and the filter did something on the selected half */
      a    = read_native (image, drawable_buffer (layer), &left);
      b    = read_native (image, before, &left);
      size = (gsize) left.width * left.height *
             babl_format_get_bytes_per_pixel (native_format (image));
      g_assert_true (memcmp (a, b, size) != 0);
      g_free (a);
      g_free (b);

      g_object_unref (before);
    }

  g_object_unref (image);
}


/*  C8: flatten (B3)  */

static void
c8_flatten (void)
{
  static const gdouble  k_only[5] = { 0.0, 0.0, 0.0, 1.0, 1.0 };
  static const gdouble  c_only[5] = { 0.7, 0.0, 0.0, 0.0, 1.0 };
  const GeglRectangle   tl        = { 0,     0,     W / 2, H / 2 };
  const GeglRectangle   tr        = { W / 2, 0,     W / 2, H / 2 };
  const GeglRectangle   br        = { W / 2, H / 2, W / 2, H / 2 };
  GimpImage            *image;
  GimpLayer            *bottom;
  GimpLayer            *top;
  GimpLayer            *flat;
  GimpContext          *context = gimp_get_user_context (gimp);
  GeglColor            *bg_color;
  GeglColor            *old_bg;
  GError               *error   = NULL;
  const Babl           *flat_format;
  guint8               *bg_native;
  guint8               *data;
  gdouble              *ddata;
  gint                  bpp;
  gint                  i;

  if (skip_without_cmyk ())
    return;

  image  = new_image (W, H, GIMP_PRECISION_U16_NON_LINEAR);

  ASSERT_EXACT_CONTEXT_COLORS (image);
  bottom = gimp_test_utils_add_cmyk_layer (image, "bottom", TRUE, NULL);
  top    = gimp_test_utils_add_cmyk_layer (image, "top",    TRUE, NULL);

  gimp_test_utils_write_cmyk_patch (drawable_buffer (bottom), &tl, k_only);
  gimp_test_utils_write_cmyk_patch (drawable_buffer (top),    &br, c_only);

  old_bg   = gegl_color_duplicate (gimp_context_get_background (context));
  bg_color = cmyk_color (image, 0.05, 0.1, 0.15, 0.2);
  gimp_context_set_background (context, bg_color);

  flat = gimp_image_flatten (image, context, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (flat);

  g_assert_false (gimp_drawable_has_alpha (GIMP_DRAWABLE (flat)));
  g_assert_true (gimp_drawable_is_cmyk (GIMP_DRAWABLE (flat)));

  /* transparent areas == the background, exactly */
  flat_format = gimp_drawable_get_format (GIMP_DRAWABLE (flat));
  bpp         = babl_format_get_bytes_per_pixel (flat_format);
  bg_native   = g_malloc (bpp);
  gegl_color_get_pixel (bg_color, flat_format, bg_native);

  data = gimp_test_utils_read_patch (drawable_buffer (flat), &tr, flat_format);

  for (i = 0; i < tr.width * tr.height; i++)
    g_assert_true (! memcmp (data + i * bpp, bg_native, bpp));

  g_free (data);

  /* K-only stays K-only */
  ddata = read_double (image, drawable_buffer (flat), &tl);

  for (i = 0; i < tl.width * tl.height; i++)
    {
      const gdouble *p = ddata + i * 5;

      g_assert_cmpfloat (p[0], ==, 0.0);
      g_assert_cmpfloat (p[1], ==, 0.0);
      g_assert_cmpfloat (p[2], ==, 0.0);
      g_assert_cmpfloat (p[3], ==, 1.0);
    }

  g_free (ddata);

  /* C-only stays C-only */
  ddata = read_double (image, drawable_buffer (flat), &br);

  for (i = 0; i < br.width * br.height; i++)
    {
      const gdouble *p = ddata + i * 5;

      g_assert_cmpfloat_with_epsilon (p[0], 0.7, U16_STEP);
      g_assert_cmpfloat (p[1], ==, 0.0);
      g_assert_cmpfloat (p[2], ==, 0.0);
      g_assert_cmpfloat (p[3], ==, 0.0);
    }

  g_free (ddata);
  g_free (bg_native);

  gimp_context_set_background (context, old_bg);
  g_object_unref (old_bg);
  g_object_unref (bg_color);
  g_object_unref (image);
}


/*  C9: paint exactness (B2)  */

typedef struct
{
  const gchar *paint_info;
  const gchar *option;     /* optional enum option */
  gint         value;
} PaintCase;

static const PaintCase paint_cases[] =
{
  { "gimp-paintbrush", NULL,   0 },
  { "gimp-pencil",     NULL,   0 },
  { "gimp-airbrush",   NULL,   0 },
  { "gimp-eraser",     NULL,   0 },
  { "gimp-clone",      NULL,   0 },
  { "gimp-heal",       NULL,   0 },
  { "gimp-smudge",     NULL,   0 },
  { "gimp-convolve",   "type", GIMP_CONVOLVE_BLUR },
  { "gimp-convolve",   "type", GIMP_CONVOLVE_SHARPEN },
  { "gimp-dodge-burn", "type", GIMP_DODGE_BURN_TYPE_DODGE },
  { "gimp-dodge-burn", "type", GIMP_DODGE_BURN_TYPE_BURN },
  { "gimp-ink",        NULL,   0 }
};

#define PAINT_SIZE   48
#define PAINT_RADIUS 9.0   /* brush size 6 / ink size 6, plus margin */

static const gdouble stroke_points[][2] = { { 14.0, 24.0 },
                                            { 24.0, 24.0 },
                                            { 34.0, 24.0 } };

static GimpPaintOptions *
paint_options_new (const gchar    *name,
                   GimpLayerMode   mode,
                   GeglColor      *fg,
                   GimpBrush      *brush,
                   GimpDynamics   *dynamics,
                   GimpPaintInfo **info_out)
{
  GimpPaintInfo    *info;
  GimpPaintOptions *options;

  info = GIMP_PAINT_INFO (gimp_container_get_child_by_name (gimp->paint_info_list,
                                                            name));
  if (! info)
    return NULL;

  options = gimp_paint_options_new (info);

  gimp_context_set_brush      (GIMP_CONTEXT (options), brush);
  gimp_context_set_dynamics   (GIMP_CONTEXT (options), dynamics);
  gimp_context_set_foreground (GIMP_CONTEXT (options), fg);
  gimp_context_set_opacity    (GIMP_CONTEXT (options), GIMP_OPACITY_OPAQUE);
  gimp_context_set_paint_mode (GIMP_CONTEXT (options), mode);

  g_object_set (options,
                "dynamics-enabled", FALSE,
                "brush-size",       6.0,
                "brush-spacing",    0.1,
                "brush-hardness",   1.0,
                "brush-force",      0.5,
                NULL);

  if (info_out)
    *info_out = info;

  return options;
}

static gboolean
paint_stroke (GimpPaintInfo    *info,
              GimpPaintOptions *options,
              GimpDrawable     *drawable,
              GError          **error)
{
  static const GimpCoords  default_coords = GIMP_COORDS_DEFAULT_VALUES;
  GimpCoords               coords[G_N_ELEMENTS (stroke_points)];
  GimpPaintCore           *core;
  gboolean                 success;
  gint                     i;

  for (i = 0; i < G_N_ELEMENTS (stroke_points); i++)
    {
      coords[i]   = default_coords;
      coords[i].x = stroke_points[i][0];
      coords[i].y = stroke_points[i][1];
    }

  core = g_object_new (info->paint_type, NULL);

  success = gimp_paint_core_stroke (core, drawable, options,
                                    coords, G_N_ELEMENTS (coords),
                                    FALSE, error);
  g_object_unref (core);

  return success;
}

static gboolean
outside_footprint (gint    x,
                   gint    y,
                   gdouble radius)
{
  gdouble x0 = stroke_points[0][0];
  gdouble x1 = stroke_points[G_N_ELEMENTS (stroke_points) - 1][0];
  gdouble cy = stroke_points[0][1];
  gdouble dx = 0.0;
  gdouble px = x + 0.5;
  gdouble py = y + 0.5;

  if (px < x0)
    dx = x0 - px;
  else if (px > x1)
    dx = px - x1;

  return sqrt (dx * dx + (py - cy) * (py - cy)) > radius;
}

static void
c9_paint (gconstpointer data)
{
  const PrecisionCase *pc  = data;
  const GeglRectangle  all = { 0, 0, PAINT_SIZE, PAINT_SIZE };
  GimpImage           *image;
  GimpLayer           *layer;
  GimpBrush           *brush;
  GimpDynamics        *dynamics;
  GeglColor           *fg;
  gfloat               cmyk[4] = { 0.1, 0.2, 0.3, 0.4 };
  gint                 bpp;
  gint                 i;

  if (skip_without_cmyk ())
    return;

  image = new_image (PAINT_SIZE, PAINT_SIZE, pc->precision);
  layer = gimp_test_utils_add_cmyk_layer (image, "paint", TRUE, NULL);

  brush = GIMP_BRUSH (gimp_brush_generated_new ("c9",
                                                GIMP_BRUSH_GENERATED_CIRCLE,
                                                3.0, 2, 1.0, 1.0, 0.0));
  dynamics = GIMP_DYNAMICS (gimp_dynamics_new (gimp_get_user_context (gimp),
                                               "c9"));
  fg = gegl_color_new (NULL);
  gegl_color_set_pixel (fg, cmyk_format (image, "CMYK float"), cmyk);

  bpp = babl_format_get_bytes_per_pixel (native_format (image));

  for (i = 0; i < G_N_ELEMENTS (paint_cases); i++)
    {
      const PaintCase  *pcase = &paint_cases[i];
      GimpPaintInfo    *info;
      GimpPaintOptions *options;
      GList            *src_drawables;
      GError           *error = NULL;
      guint8           *before;
      guint8           *after;
      gint              x, y;
      gint              n_changed = 0;

      fill_seeded (drawable_buffer (layer), NULL, 91 + i, 1.0);

      options = paint_options_new (pcase->paint_info, GIMP_LAYER_MODE_NORMAL,
                                   fg, brush, dynamics, &info);
      g_assert_nonnull (options);

      if (pcase->option)
        g_object_set (options, pcase->option, pcase->value, NULL);

      if (! strcmp (pcase->paint_info, "gimp-ink"))
        g_object_set (options, "size", 6.0, NULL);

      src_drawables = g_list_prepend (NULL, layer);
      if (! strcmp (pcase->paint_info, "gimp-clone") ||
          ! strcmp (pcase->paint_info, "gimp-heal"))
        {
          g_object_set (options,
                        "src-drawables", src_drawables,
                        "src-x",         14,
                        "src-y",         8,
                        NULL);
        }

      before = read_native (image, drawable_buffer (layer), &all);

      if (! paint_stroke (info, options, GIMP_DRAWABLE (layer), &error))
        g_error ("C9/%s: %s stroke failed: %s", pc->name, pcase->paint_info,
                 error ? error->message : "(no error)");

      after = read_native (image, drawable_buffer (layer), &all);

      for (y = 0; y < PAINT_SIZE; y++)
        for (x = 0; x < PAINT_SIZE; x++)
          {
            gsize offset = ((gsize) y * PAINT_SIZE + x) * bpp;

            if (memcmp (before + offset, after + offset, bpp))
              {
                /* ink blobs grow with speed: wider footprint */
                if (outside_footprint (x, y,
                                       strcmp (pcase->paint_info, "gimp-ink") ?
                                       PAINT_RADIUS : 2 * PAINT_RADIUS))
                  g_error ("C9/%s: %s changed pixel (%d,%d) outside the "
                           "dab footprint", pc->name, pcase->paint_info,
                           x, y);

                n_changed++;
              }
          }

      if (n_changed == 0)
        g_error ("C9/%s: %s did not change any pixel",
                 pc->name, pcase->paint_info);

      /* paintbrush, Normal 100 %: the centre of the footprint is FG */
      if (! strcmp (pcase->paint_info, "gimp-paintbrush") &&
          context_colors_are_exact (image))
        {
          guint8 *fg_native = g_malloc (bpp);
          gsize   offset    = ((gsize) 24 * PAINT_SIZE + 24) * bpp;

          gegl_color_get_pixel (fg, native_format (image), fg_native);

          if (memcmp (after + offset, fg_native, bpp))
            {
              gdouble pixel[5];

              gimp_test_utils_read_cmyk_pixel (drawable_buffer (layer),
                                               24, 24, pixel);
              g_error ("C9/%s: paintbrush centre (%g,%g,%g,%g,%g) != FG",
                       pc->name, pixel[0], pixel[1], pixel[2], pixel[3],
                       pixel[4]);
            }

          g_free (fg_native);
        }

      g_free (before);
      g_free (after);
      g_list_free (src_drawables);
      g_object_unref (options);
    }

  /* MyPaint Brush is refused on CMYK drawables (M13) */
  {
    GimpPaintInfo    *info;
    GimpPaintOptions *options;
    GError           *error = NULL;

    options = paint_options_new ("gimp-mybrush", GIMP_LAYER_MODE_NORMAL,
                                 fg, brush, dynamics, &info);

    if (options)
      {
        g_assert_false (paint_stroke (info, options, GIMP_DRAWABLE (layer),
                                      &error));
        g_assert_nonnull (error);
        g_assert_nonnull (strstr (error->message, "CMYK"));

        g_clear_error (&error);
        g_object_unref (options);
      }
  }

  g_object_unref (fg);
  g_object_unref (dynamics);
  g_object_unref (brush);
  g_object_unref (image);
}


/*  C10: converter accuracy  */

static void
c10_converter (void)
{
  static const gchar *blend_formats[] = { "RGBA float", "R'G'B'A float",
                                          "R~G~B~A float" };
  const GimpLayerModeCmykConverter *conv;
  GimpImage                        *image;
  const Babl                       *cmyka_float;
  const gint                        steps = 9;
  const gint                        n     = steps * steps * steps * steps;
  gfloat                           *cmyk;
  gfloat                           *blend;
  gfloat                           *ref;
  gfloat                           *out;
  gint                              f, i;

  if (skip_without_cmyk ())
    return;

  image       = new_image (4, 4, GIMP_PRECISION_U16_NON_LINEAR);
  cmyka_float = cmyk_format (image, "CMYKA float");
  conv        = gimp_layer_mode_cmyk_converter_get (
                  gimp_image_get_layer_space (image));

  g_assert_nonnull (conv);

  g_printerr ("C10: converter uses %s\n",
              gimp_layer_mode_cmyk_converter_uses_transforms (conv) ?
              "GimpColorTransforms (u16)" : "babl fishes (fallback)");

  cmyk  = g_new (gfloat, n * 5);
  blend = g_new (gfloat, n * 4);
  ref   = g_new (gfloat, n * 5);
  out   = g_new (gfloat, n * 5);

  for (i = 0; i < n; i++)
    {
      gint v = i;
      gint c;

      for (c = 0; c < 4; c++)
        {
          cmyk[i * 5 + c] = (gfloat) (v % steps) / (steps - 1);
          v /= steps;
        }

      cmyk[i * 5 + 4] = 1.0f;
    }

  for (f = 0; f < G_N_ELEMENTS (blend_formats); f++)
    {
      const Babl *format   = babl_format (blend_formats[f]);
      gdouble     max_to   = 0.0;
      gdouble     max_from = 0.0;

      gdouble     max_babl = 0.0;

      /* CMYK -> blend, against the independent reference */
      gimp_layer_mode_cmyk_converter_to_blend (conv, format, cmyk, blend, n);
      reference_to_blend (image, format, cmyk, ref, n);

      for (i = 0; i < n * 4; i++)
        max_to = MAX (max_to, fabs (blend[i] - ref[i]));

      /* alpha is exact */
      for (i = 0; i < n; i++)
        g_assert_cmpfloat (blend[i * 4 + 3], ==, cmyk[i * 5 + 4]);

      /* informational: drift against babl's own CMYK conversion (clipped:
       * the converter works on 16 bit sRGB)
       */
      babl_process (babl_fish (cmyka_float, format), cmyk, out, n);
      for (i = 0; i < n * 4; i++)
        if (i % 4 != 3)
          max_babl = MAX (max_babl, fabs (blend[i] - CLAMP (out[i], 0.0f, 1.0f)));

      /* blend -> CMYK, on the reference blend values */
      memcpy (blend, ref, n * 4 * sizeof (gfloat));

      gimp_layer_mode_cmyk_converter_from_blend (conv, format, blend, out, n);
      reference_from_blend (image, format, blend, ref, n);

      for (i = 0; i < n * 5; i++)
        max_from = MAX (max_from, fabs (out[i] - ref[i]));

      g_printerr ("C10: %s: to blend %.2f/65535, from blend %.2f/65535 "
                  "(vs babl: %.2f/255)\n",
                  blend_formats[f], max_to * 65535.0, max_from * 65535.0,
                  max_babl * 255.0);

      g_assert_cmpfloat (max_to,   <=, 1e-6);
      g_assert_cmpfloat (max_from, <=, 1e-6);
    }

  g_free (cmyk);
  g_free (blend);
  g_free (ref);
  g_free (out);
  g_object_unref (image);
}


/*  C11: legacy paint mode on CMYK == its non-legacy mode  */

static void
c11_legacy_paint (void)
{
  const GeglRectangle  all = { 0, 0, PAINT_SIZE, PAINT_SIZE };
  GimpImage           *image;
  GimpLayer           *a;
  GimpLayer           *b;
  GimpBrush           *brush;
  GimpDynamics        *dynamics;
  GeglColor           *fg;
  GimpPaintInfo       *info;
  GimpPaintOptions    *options;
  GError              *error = NULL;

  if (skip_without_cmyk ())
    return;

  image = new_image (PAINT_SIZE, PAINT_SIZE, GIMP_PRECISION_U16_NON_LINEAR);
  a     = gimp_test_utils_add_cmyk_layer (image, "a", TRUE, NULL);
  b     = gimp_test_utils_add_cmyk_layer (image, "b", TRUE, NULL);

  fill_seeded (drawable_buffer (a), NULL, 111, 1.0);
  fill_seeded (drawable_buffer (b), NULL, 111, 1.0);

  brush = GIMP_BRUSH (gimp_brush_generated_new ("c11",
                                                GIMP_BRUSH_GENERATED_CIRCLE,
                                                3.0, 2, 0.5, 1.0, 0.0));
  dynamics = GIMP_DYNAMICS (gimp_dynamics_new (gimp_get_user_context (gimp),
                                               "c11"));
  fg = cmyk_color (image, 0.3, 0.6, 0.1, 0.2);

  options = paint_options_new ("gimp-paintbrush",
                               GIMP_LAYER_MODE_MULTIPLY_LEGACY,
                               fg, brush, dynamics, &info);
  g_assert_true (paint_stroke (info, options, GIMP_DRAWABLE (a), &error));
  g_assert_no_error (error);
  g_object_unref (options);

  options = paint_options_new ("gimp-paintbrush", GIMP_LAYER_MODE_MULTIPLY,
                               fg, brush, dynamics, &info);
  g_assert_true (paint_stroke (info, options, GIMP_DRAWABLE (b), &error));
  g_assert_no_error (error);
  g_object_unref (options);

  assert_rect_identical (image, drawable_buffer (a), drawable_buffer (b),
                         &all, "C11 multiply-legacy vs multiply");

  g_assert_cmpint (gimp_layer_mode_get_non_legacy (GIMP_LAYER_MODE_MULTIPLY_LEGACY),
                   ==, GIMP_LAYER_MODE_MULTIPLY);
  g_assert_cmpint (gimp_layer_mode_get_non_legacy (GIMP_LAYER_MODE_NORMAL),
                   ==, GIMP_LAYER_MODE_NORMAL);

  g_object_unref (fg);
  g_object_unref (dynamics);
  g_object_unref (brush);
  g_object_unref (image);
}


/*  C12: merge-down and floating selections  */

static void
assert_k_only (const gdouble *data,
               gint           n,
               gboolean       check_alpha)
{
  gint i;

  for (i = 0; i < n; i++)
    {
      const gdouble *p = data + i * 5;

      g_assert_cmpfloat (p[0], ==, 0.0);
      g_assert_cmpfloat (p[1], ==, 0.0);
      g_assert_cmpfloat (p[2], ==, 0.0);
      g_assert_cmpfloat (p[3], ==, 1.0);

      if (check_alpha)
        g_assert_cmpfloat (p[4], ==, 1.0);
    }
}

static void
c12_merge_floating (void)
{
  static const gdouble  k_only[5] = { 0.0, 0.0, 0.0, 1.0, 1.0 };
  const GeglRectangle   patch     = { 8, 8, 8, 8 };
  const GeglRectangle   above     = { 0, 0, W, 8 };
  const GeglRectangle   below     = { 0, 16, W, H - 16 };
  GimpImage            *image;
  GimpLayer            *bottom;
  GimpLayer            *top;
  GimpLayer            *floating;
  GimpContext          *context = gimp_get_user_context (gimp);
  GeglBuffer           *before;
  GList                *layers;
  GList                *merged;
  GError               *error = NULL;
  gdouble              *data;

  if (skip_without_cmyk ())
    return;

  /* merge-down of a Normal layer that only covers a patch */
  image  = new_image (W, H, GIMP_PRECISION_U16_NON_LINEAR);
  bottom = gimp_test_utils_add_cmyk_layer (image, "bottom", TRUE, NULL);
  top    = gimp_test_utils_add_cmyk_layer (image, "top",    TRUE, NULL);

  fill_seeded (drawable_buffer (bottom), NULL, 121, 1.0);
  gimp_test_utils_write_cmyk_patch (drawable_buffer (top), &patch, k_only);

  before = gimp_gegl_buffer_dup (drawable_buffer (bottom), NULL);

  layers = g_list_prepend (NULL, top);
  merged = gimp_image_merge_down (image, layers, context,
                                  GIMP_EXPAND_AS_NECESSARY, NULL, NULL,
                                  &error);
  g_assert_no_error (error);
  g_assert_nonnull (merged);
  g_list_free (layers);

  data = read_double (image, drawable_buffer (merged->data), &patch);
  assert_k_only (data, patch.width * patch.height, TRUE);
  g_free (data);

  assert_rect_identical (image, drawable_buffer (merged->data), before,
                         &above, "C12 merge-down untouched pixels (above)");
  assert_rect_identical (image, drawable_buffer (merged->data), before,
                         &below, "C12 merge-down untouched pixels (below)");

  g_list_free (merged);
  g_object_unref (before);
  g_object_unref (image);

  /* anchoring a pasted K-only floating selection */
  image  = new_image (W, H, GIMP_PRECISION_U16_NON_LINEAR);
  bottom = gimp_test_utils_add_cmyk_layer (image, "bottom", TRUE, NULL);
  fill_seeded (drawable_buffer (bottom), NULL, 122, 1.0);

  before = gimp_gegl_buffer_dup (drawable_buffer (bottom), NULL);

  floating = gimp_layer_new (image, patch.width, patch.height,
                             gimp_image_get_layer_format (image, TRUE),
                             "floating", GIMP_OPACITY_OPAQUE,
                             GIMP_LAYER_MODE_NORMAL);
  gimp_test_utils_write_cmyk_patch (drawable_buffer (floating), NULL, k_only);
  gimp_item_set_offset (GIMP_ITEM (floating), patch.x, patch.y);

  floating_sel_attach (floating, GIMP_DRAWABLE (bottom));
  floating_sel_anchor (floating);

  data = read_double (image, drawable_buffer (bottom), &patch);
  assert_k_only (data, patch.width * patch.height, TRUE);
  g_free (data);

  assert_rect_identical (image, drawable_buffer (bottom), before,
                         &above, "C12 floating untouched pixels (above)");
  assert_rect_identical (image, drawable_buffer (bottom), before,
                         &below, "C12 floating untouched pixels (below)");

  g_object_unref (before);
  g_object_unref (image);
}


/*  C13: explicit RGB composite space  */

static void
c13_explicit_rgb (void)
{
  const PrecisionCase pc = { "u16", GIMP_PRECISION_U16_NON_LINEAR };
  gdouble             native[5];
  gdouble             rgb[5];
  gdouble             max_diff = 0.0;
  gint                c;

  if (skip_without_cmyk ())
    return;

  c1_k_preservation_full (&pc, GIMP_LAYER_COLOR_SPACE_AUTO,       native);
  c1_k_preservation_full (&pc, GIMP_LAYER_COLOR_SPACE_RGB_LINEAR, rgb);

  for (c = 0; c < 4; c++)
    max_diff = MAX (max_diff, fabs (native[c] - rgb[c]));

  g_printerr ("C13: CMYK native (%.3f,%.3f,%.3f,%.3f) vs RGB_LINEAR "
              "(%.3f,%.3f,%.3f,%.3f)\n",
              native[0], native[1], native[2], native[3],
              rgb[0], rgb[1], rgb[2], rgb[3]);

  /* compositing in RGB re-separates the inks */
  g_assert_cmpfloat (max_diff, >, 0.01);
}


/*  edit-clear with a selection keeps unselected pixels (03's I9/I10)  */

static void
edit_clear_selection (void)
{
  static const gdouble  k100[5] = { 0.0, 0.0, 0.0, 1.0, 1.0 };
  const GeglRectangle   left    = { 0,     0, W / 2, H };
  const GeglRectangle   right   = { W / 2, 0, W / 2, H };
  gint                  with_alpha;

  if (skip_without_cmyk ())
    return;

  for (with_alpha = 0; with_alpha < 2; with_alpha++)
    {
      GimpImage  *image;
      GimpLayer  *layer;
      GeglBuffer *before;

      image = new_image (W, H, GIMP_PRECISION_U16_NON_LINEAR);
      layer = gimp_test_utils_add_cmyk_layer (image, "layer", with_alpha, k100);

      before = gimp_gegl_buffer_dup (drawable_buffer (layer), NULL);

      gimp_channel_select_rectangle (gimp_image_get_mask (image),
                                     left.x, left.y, left.width, left.height,
                                     GIMP_CHANNEL_OP_REPLACE,
                                     FALSE, 0.0, 0.0, FALSE);

      gimp_drawable_edit_clear (GIMP_DRAWABLE (layer),
                                gimp_get_user_context (gimp));

      assert_rect_identical (image, drawable_buffer (layer), before, &right,
                             with_alpha ? "clear (alpha): unselected" :
                                          "clear (no alpha): unselected");

      if (with_alpha)
        {
          gdouble pixel[5];

          gimp_test_utils_read_cmyk_pixel (drawable_buffer (layer), 2, 2, pixel);
          g_assert_cmpfloat (pixel[4], ==, 0.0);
        }

      g_object_unref (before);
      g_object_unref (image);
    }
}


/*  K preservation through group layers  */

static void
group_layers (void)
{
  static const gdouble  bg_ink[5] = { 0.2, 0.3, 0.4, 0.0, 1.0 };
  static const gdouble  k_only[5] = { 0.0, 0.0, 0.0, 1.0, 1.0 };
  const GeglRectangle   patch     = { 4, 4, 8, 8 };
  const GeglRectangle   uncovered = { 16, 0, 16, 16 };
  GimpImage            *image;
  GimpLayer            *bg;
  GimpLayer            *group;
  GimpLayer            *child;
  GimpLayer            *child2;
  gdouble              *data;

  if (skip_without_cmyk ())
    return;

  image = new_image (W, H, GIMP_PRECISION_U16_NON_LINEAR);
  bg    = gimp_test_utils_add_cmyk_layer (image, "bg", FALSE, bg_ink);

  group = gimp_group_layer_new (image);
  gimp_image_add_layer (image, group, NULL, 0, FALSE);

  child = gimp_layer_new (image, W, H,
                          gimp_image_get_layer_format (image, TRUE),
                          "child", GIMP_OPACITY_OPAQUE, GIMP_LAYER_MODE_NORMAL);
  gimp_test_utils_write_cmyk_patch (drawable_buffer (child), &patch, k_only);
  gimp_image_add_layer (image, child, GIMP_LAYER (group), 0, FALSE);

  child2 = gimp_layer_new (image, W, H,
                           gimp_image_get_layer_format (image, TRUE),
                           "child2", 0.5, GIMP_LAYER_MODE_MULTIPLY);
  gimp_test_utils_write_cmyk_patch (drawable_buffer (child2),
                                    GEGL_RECTANGLE (20, 20, 4, 4), bg_ink);
  gimp_image_add_layer (image, child2, GIMP_LAYER (group), 0, FALSE);

  /* the group is CMYKA in the image's precision and space */
  g_assert_true (gimp_drawable_get_format (GIMP_DRAWABLE (group)) ==
                 native_format (image));

  data = read_double (image, projection_buffer (image), &patch);
  assert_k_only (data, patch.width * patch.height, FALSE);
  g_free (data);

  assert_rect_identical (image, projection_buffer (image),
                         drawable_buffer (bg), &uncovered, "group: uncovered");

  /* pass-through groups composite natively too */
  gimp_layer_set_mode (group, GIMP_LAYER_MODE_PASS_THROUGH, FALSE);

  data = read_double (image, projection_buffer (image), &patch);
  assert_k_only (data, patch.width * patch.height, FALSE);
  g_free (data);

  assert_rect_identical (image, projection_buffer (image),
                         drawable_buffer (bg), &uncovered,
                         "pass-through group: uncovered");

  g_object_unref (image);
}


/*  P1/P2: performance (GIMP_TESTING_PERF)  */

static gdouble
render_time (GimpImage *image)
{
  GimpProjection *projection = gimp_image_get_projection (image);
  GeglBuffer     *buffer;
  gpointer        data;
  gint64          start;
  gint64          end;

  start = g_get_monotonic_time ();

  gimp_pickable_flush (GIMP_PICKABLE (projection));
  buffer = gimp_pickable_get_buffer (GIMP_PICKABLE (projection));

  data = g_malloc ((gsize) gegl_buffer_get_width (buffer) *
                   gegl_buffer_get_height (buffer) *
                   babl_format_get_bytes_per_pixel (gegl_buffer_get_format (buffer)));
  gegl_buffer_get (buffer, NULL, 1.0, NULL, data,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

  end = g_get_monotonic_time ();

  g_free (data);

  return (end - start) / 1e6;
}

static GimpImage *
perf_image (GimpImageBaseType base_type,
            GimpLayerMode     mode)
{
  const gint  size = 2048;
  GimpImage  *image;
  gint        i;

  if (base_type == GIMP_CMYK)
    image = new_image (size, size, GIMP_PRECISION_U16_NON_LINEAR);
  else
    image = gimp_image_new (gimp, size, size, GIMP_RGB,
                            GIMP_PRECISION_U16_NON_LINEAR);

  for (i = 0; i < 2; i++)
    {
      GimpLayer  *layer;
      GeglBuffer *buffer;
      guint16    *data;
      GRand      *rand = g_rand_new_with_seed (1000 + i);
      gint        n_c  = (base_type == GIMP_CMYK) ? 4 : 3;
      gint        p;

      layer = gimp_layer_new (image, size, size,
                              gimp_image_get_layer_format (image, FALSE),
                              "perf", GIMP_OPACITY_OPAQUE,
                              i == 0 ? GIMP_LAYER_MODE_NORMAL : mode);
      gimp_image_add_layer (image, layer, NULL, 0, FALSE);

      buffer = drawable_buffer (layer);
      data   = g_new (guint16, (gsize) size * size * n_c);

      for (p = 0; p < size * size * n_c; p++)
        data[p] = g_rand_int (rand) & 0xffff;

      gegl_buffer_set (buffer, NULL, 0,
                       base_type == GIMP_CMYK ?
                       cmyk_format (image, "CMYK u16") :
                       babl_format_with_space ("R'G'B' u16",
                                               gegl_buffer_get_format (buffer)),
                       data, GEGL_AUTO_ROWSTRIDE);

      g_free (data);
      g_rand_free (rand);
    }

  return image;
}

static void
perf (void)
{
  const gchar *env = g_getenv ("GIMP_TESTING_PERF");
  gboolean     assert_targets;
  gint         threads;
  GimpImage   *image;
  gdouble      p1, p2_cmyk, p2_rgb;

  if (! env || ! *env)
    {
      g_test_skip ("P1/P2: set GIMP_TESTING_PERF=1 (assert) or =report");
      return;
    }

  if (skip_without_cmyk ())
    return;

  assert_targets = ! strcmp (env, "1");

  g_object_get (gegl_config (), "threads", &threads, NULL);
  g_object_set (gegl_config (), "threads", 2, NULL);

  image = perf_image (GIMP_CMYK, GIMP_LAYER_MODE_MULTIPLY);
  p1 = render_time (image);
  g_object_unref (image);

  image = perf_image (GIMP_CMYK, GIMP_LAYER_MODE_NORMAL);
  p2_cmyk = render_time (image);
  g_object_unref (image);

  image = perf_image (GIMP_RGB, GIMP_LAYER_MODE_NORMAL);
  p2_rgb = render_time (image);
  g_object_unref (image);

  g_object_set (gegl_config (), "threads", threads, NULL);

  g_printerr ("P1: 2048^2 CMYK u16 Multiply projection: %.3f s "
              "(target <= 1.5 s)\n", p1);
  g_printerr ("P2: 2048^2 Normal projection: CMYK %.3f s, RGB %.3f s, "
              "ratio %.2f (target <= 1.5)\n",
              p2_cmyk, p2_rgb, p2_cmyk / p2_rgb);

  if (assert_targets)
    {
      g_assert_cmpfloat (p1, <=, 1.5);
      g_assert_cmpfloat (p2_cmyk / p2_rgb, <=, 1.5);
    }
}


int
main (int    argc,
      char **argv)
{
  GimpPrecision knob;
  gint          result;

  g_test_init (&argc, &argv, NULL);

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_SRCDIR",
                                       "app/tests/gimpdir");

  gimp = gimp_init_for_testing ();

  knob = gimp_test_utils_cmyk_precision ();
  if (knob != GIMP_PRECISION_U16_NON_LINEAR &&
      knob != GIMP_PRECISION_FLOAT_NON_LINEAR)
    {
      precision_cases[2].name      = g_getenv ("GIMP_TESTING_CMYK_PRECISION");
      precision_cases[2].precision = knob;
    }

  add_precision_tests ("/cmyk-compositing/C1/k-preservation",  c1_k_preservation);
  g_test_add_func     ("/cmyk-compositing/C2/multiply",        c2_multiply);
  add_precision_tests ("/cmyk-compositing/C3/zero-coverage",   c3_zero_coverage);
  g_test_add_func     ("/cmyk-compositing/C4/native-fill",     c4_native_fill);
  g_test_add_func     ("/cmyk-compositing/C5/component-active", c5_component_active);
  g_test_add_func     ("/cmyk-compositing/C6/component-visible", c6_component_visible);
  g_test_add_func     ("/cmyk-compositing/C7/filter-mask",     c7_filter_mask);
  g_test_add_func     ("/cmyk-compositing/C8/flatten",         c8_flatten);
  add_precision_tests ("/cmyk-compositing/C9/paint",           c9_paint);
  g_test_add_func     ("/cmyk-compositing/C10/converter",      c10_converter);
  g_test_add_func     ("/cmyk-compositing/C11/legacy-paint",   c11_legacy_paint);
  g_test_add_func     ("/cmyk-compositing/C12/merge-floating", c12_merge_floating);
  g_test_add_func     ("/cmyk-compositing/C13/explicit-rgb",   c13_explicit_rgb);
  g_test_add_func     ("/cmyk-compositing/group-layers",       group_layers);
  g_test_add_func     ("/cmyk-compositing/edit-clear",         edit_clear_selection);
  g_test_add_func     ("/cmyk-compositing/P1-P2/performance",  perf);

  result = g_test_run ();

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_BUILDDIR",
                                       "app/tests/gimpdir-output");

  gimp_exit (gimp, TRUE);

  return result;
}

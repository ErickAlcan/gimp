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

/* CMYK histogram, picker, select-by-color, text, selector and color
 * tool tests H1-H6 (devel-docs/cmyk-mode/TESTING.md).  Owner: WS08.
 *
 * GIMP_TESTING_CMYK_PRECISION is honoured by H1, H3, H4, H5, H6 (H2
 * always uses DOUBLE: it is the stack-buffer overflow test).
 */

#include "config.h"

#include <string.h>

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"
#include "libgimpmath/gimpmath.h"
#include "libgimpmodule/gimpmodule.h"
#include "libgimpwidgets/gimpwidgets.h"

#include "widgets/widgets-types.h"

#include "gegl/gimp-babl.h"
#include "gegl/gimp-gegl.h"

#include "operations/gimplevelsconfig.h"

#include "core/gimp.h"
#include "core/gimpchannel.h"
#include "core/gimpchannel-select.h"
#include "core/gimpcontainer.h"
#include "core/gimpcontext.h"
#include "core/gimpdatafactory.h"
#include "core/gimpdrawable-fill.h"
#include "core/gimpdrawable-histogram.h"
#include "core/gimpdrawable-operation.h"
#include "core/gimphistogram.h"
#include "core/gimpimage.h"
#include "core/gimpimage-pick-color.h"
#include "core/gimpimage-undo.h"
#include "core/gimplayer.h"
#include "core/gimplayer-new.h"
#include "core/gimppickable.h"

#include "text/gimptext.h"
#include "text/gimptextlayer.h"

#include "gimp-log.h"

#include "tests.h"

#include "gimp-app-test-utils.h"


#define W 32
#define H 32


static Gimp     *gimp     = NULL;
static gboolean  have_gtk = FALSE;


/*  helpers  */

static GimpImage *
new_cmyk_image (GimpPrecision precision)
{
  GimpImage *image;

  image = gimp_test_utils_create_cmyk_image (gimp, W, H, precision, NULL);
  gimp_image_undo_disable (image);

  return image;
}

static gint
bin_of (GimpHistogram *histogram,
        gdouble        value)
{
  return SIGNED_ROUND (value * (gimp_histogram_n_bins (histogram) - 1));
}

static gdouble
count_at (GimpHistogram        *histogram,
          GimpHistogramChannel  channel,
          gdouble               value)
{
  gint bin = bin_of (histogram, value);

  return gimp_histogram_get_count (histogram, channel, bin, bin);
}

static gdouble
count_around (GimpHistogram        *histogram,
              GimpHistogramChannel  channel,
              gdouble               value)
{
  gint bin = bin_of (histogram, value);

  return gimp_histogram_get_count (histogram, channel, bin - 1, bin + 1);
}

static void
assert_cmyka_eq (const gdouble *a,
                 const gdouble *b,
                 gdouble        eps)
{
  gint c;

  for (c = 0; c < 5; c++)
    g_assert_cmpfloat_with_epsilon (a[c], b[c], eps + 1e-300);
}


/*  H1: native CMYK histogram  */

static void
h1_histogram (void)
{
  static const gdouble paper[5]  = { 0.0, 0.0, 0.0, 0.0, 1.0 };
  static const gdouble k100[5]   = { 0.0, 0.0, 0.0, 1.0, 1.0 };
  static const gdouble cyan[5]   = { 1.0, 0.5, 0.0, 0.0, 1.0 };
  static const gdouble clear[5]  = { 0.0, 0.0, 0.0, 0.0, 0.0 };
  GimpImage     *image;
  GimpLayer     *layer;
  GeglBuffer    *buffer;
  GimpHistogram *histogram;
  GimpHistogram *dup;
  gint           n_bins;
  gint           last;
  gint           i;

  image  = new_cmyk_image (gimp_test_utils_cmyk_precision ());
  layer  = gimp_test_utils_add_cmyk_layer (image, "patches", TRUE, paper);
  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));

  /* 512 px K100, 256 px C100+M50, 256 px fully transparent */
  gimp_test_utils_write_cmyk_patch (buffer, GEGL_RECTANGLE (0,  0,  16, 32), k100);
  gimp_test_utils_write_cmyk_patch (buffer, GEGL_RECTANGLE (16, 0,  16, 16), cyan);
  gimp_test_utils_write_cmyk_patch (buffer, GEGL_RECTANGLE (16, 16, 16, 16), clear);

  histogram = gimp_histogram_new (GIMP_TRC_NON_LINEAR);
  gimp_drawable_calculate_histogram (GIMP_DRAWABLE (layer), histogram, FALSE);

  g_assert_true (gimp_histogram_is_cmyk (histogram));
  g_assert_cmpint (gimp_histogram_n_components (histogram), ==, 5);

  /* channel set (INTERFACES §9) */
  g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_VALUE));
  g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_CYAN));
  g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_MAGENTA));
  g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_YELLOW));
  g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_BLACK));
  g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_ALPHA));
  g_assert_false (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_RED));
  g_assert_false (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_GREEN));
  g_assert_false (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_BLUE));
  g_assert_false (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_LUMINANCE));
  g_assert_false (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_RGB));

  n_bins = gimp_histogram_n_bins (histogram);
  last   = n_bins - 1;

  /* invalid channels report nothing */
  g_assert_cmpfloat (gimp_histogram_get_maximum (histogram, GIMP_HISTOGRAM_RED), ==, 0.0);
  g_assert_cmpfloat (gimp_histogram_get_count (histogram, GIMP_HISTOGRAM_LUMINANCE,
                                               0, last), ==, 0.0);
  g_assert_cmpfloat (gimp_histogram_get_count (histogram, GIMP_HISTOGRAM_RGB,
                                               0, last), ==, 0.0);

  /* counts per patch (ink is alpha weighted, alpha is not) */
  g_assert_cmpfloat (count_at (histogram, GIMP_HISTOGRAM_BLACK, 1.0), ==, 512);
  g_assert_cmpfloat (count_at (histogram, GIMP_HISTOGRAM_BLACK, 0.0), ==, 256);
  g_assert_cmpfloat (count_at (histogram, GIMP_HISTOGRAM_CYAN,  1.0), ==, 256);
  g_assert_cmpfloat (count_at (histogram, GIMP_HISTOGRAM_CYAN,  0.0), ==, 512);
  g_assert_cmpfloat (count_around (histogram, GIMP_HISTOGRAM_MAGENTA, 0.5), ==, 256);
  g_assert_cmpfloat (count_at (histogram, GIMP_HISTOGRAM_YELLOW, 0.0), ==, 768);
  g_assert_cmpfloat (count_at (histogram, GIMP_HISTOGRAM_ALPHA,  1.0), ==, 768);
  g_assert_cmpfloat (count_at (histogram, GIMP_HISTOGRAM_ALPHA,  0.0), ==, 256);

  /* VALUE = max ink */
  g_assert_cmpfloat (count_at (histogram, GIMP_HISTOGRAM_VALUE, 1.0), ==, 768);
  g_assert_cmpfloat (gimp_histogram_get_count (histogram, GIMP_HISTOGRAM_VALUE,
                                               0, last), ==, 768);

  /* statistics go through the slot map too */
  g_assert_cmpfloat_with_epsilon (gimp_histogram_get_mean (histogram,
                                                           GIMP_HISTOGRAM_BLACK,
                                                           0, last),
                                  512.0 / 768.0, 1e-9);
  g_assert_cmpfloat_with_epsilon (gimp_histogram_get_median (histogram,
                                                             GIMP_HISTOGRAM_BLACK,
                                                             0, last),
                                  1.0, 1e-9);
  g_assert_cmpfloat (gimp_histogram_get_std_dev (histogram, GIMP_HISTOGRAM_BLACK,
                                                 0, last), >, 0.4);
  g_assert_cmpfloat (gimp_histogram_get_maximum (histogram, GIMP_HISTOGRAM_YELLOW),
                     ==, 768);

  /* slot mapping: components 0..4 are C, M, Y, K, A */
  g_assert_cmpfloat (gimp_histogram_get_component (histogram, 0, last), ==, 256);
  g_assert_cmpfloat (gimp_histogram_get_component (histogram, 3, last), ==, 512);
  g_assert_cmpfloat (gimp_histogram_get_component (histogram, 4, 0),    ==, 256);
  g_assert_cmpfloat (gimp_histogram_get_component (histogram, 5, 0),    ==, 0.0);

  for (i = 0; i < n_bins; i++)
    g_assert_cmpfloat (gimp_histogram_get_value (histogram, GIMP_HISTOGRAM_BLACK, i),
                       ==, gimp_histogram_get_component (histogram, 3, i));

  dup = gimp_histogram_duplicate (histogram);
  g_assert_true (gimp_histogram_is_cmyk (dup));
  g_assert_cmpfloat (count_at (dup, GIMP_HISTOGRAM_BLACK, 1.0), ==, 512);
  g_object_unref (dup);

  /* with a selection: only the K patch */
  gimp_channel_select_rectangle (gimp_image_get_mask (image),
                                 0, 0, 16, 32, GIMP_CHANNEL_OP_REPLACE,
                                 FALSE, 0, 0, FALSE);
  gimp_drawable_calculate_histogram (GIMP_DRAWABLE (layer), histogram, FALSE);
  g_assert_cmpfloat (count_at (histogram, GIMP_HISTOGRAM_BLACK, 1.0), ==, 512);
  g_assert_cmpfloat (gimp_histogram_get_count (histogram, GIMP_HISTOGRAM_BLACK,
                                               0, last), ==, 512);
  gimp_channel_clear (gimp_image_get_mask (image), NULL, FALSE);

  /* the RGB conversion's histogram (used by the color tools) */
  gimp_drawable_calculate_histogram_rgb (GIMP_DRAWABLE (layer), histogram, FALSE);
  g_assert_false (gimp_histogram_is_cmyk (histogram));
  g_assert_cmpint (gimp_histogram_n_components (histogram), ==, 4);
  g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_RED));
  g_assert_false (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_BLACK));
  g_assert_cmpfloat (count_at (histogram, GIMP_HISTOGRAM_ALPHA, 1.0), ==, 768);

  /* format-only clear knows the color model */
  gimp_histogram_clear_values_for_format (histogram,
                                          gimp_drawable_get_format (GIMP_DRAWABLE (layer)));
  g_assert_true (gimp_histogram_is_cmyk (histogram));
  g_assert_true (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_BLACK));
  g_assert_cmpint (gimp_histogram_n_components (histogram), ==, 5);

  g_object_unref (histogram);
  g_object_unref (image);
}

/*  H1b: CMYK without alpha, and RGB unchanged  */

static void
h1_histogram_no_alpha_and_rgb (void)
{
  static const gdouble k50[5] = { 0.0, 0.0, 0.0, 0.5, 1.0 };
  GimpImage     *image;
  GimpLayer     *layer;
  GimpHistogram *histogram;

  image = new_cmyk_image (gimp_test_utils_cmyk_precision ());
  layer = gimp_test_utils_add_cmyk_layer (image, "opaque", FALSE, k50);

  histogram = gimp_histogram_new (GIMP_TRC_NON_LINEAR);
  gimp_drawable_calculate_histogram (GIMP_DRAWABLE (layer), histogram, FALSE);

  g_assert_true (gimp_histogram_is_cmyk (histogram));
  g_assert_cmpint (gimp_histogram_n_components (histogram), ==, 4);
  g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_BLACK));
  g_assert_false (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_ALPHA));
  g_assert_false (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_RED));
  g_assert_cmpfloat (count_around (histogram, GIMP_HISTOGRAM_BLACK, 0.5), ==, W * H);
  g_assert_cmpfloat (count_around (histogram, GIMP_HISTOGRAM_VALUE, 0.5), ==, W * H);
  g_assert_cmpfloat (gimp_histogram_get_count (histogram, GIMP_HISTOGRAM_ALPHA,
                                               0, 1023), ==, 0.0);
  g_object_unref (image);

  /* RGBA: today's layout (RED.. slots, ALPHA, LUMINANCE) */
  {
    GimpImage *rgb = gimp_image_new (gimp, W, H, GIMP_RGB,
                                     GIMP_PRECISION_U8_NON_LINEAR);
    GimpLayer *l;
    GeglColor *red = gegl_color_new ("red");

    gimp_image_undo_disable (rgb);

    l = gimp_layer_new (rgb, W, H,
                        gimp_image_get_layer_format (rgb, TRUE),
                        "rgb", GIMP_OPACITY_OPAQUE,
                        GIMP_LAYER_MODE_NORMAL);
    gimp_image_add_layer (rgb, l, NULL, 0, FALSE);
    gegl_buffer_set_color (gimp_drawable_get_buffer (GIMP_DRAWABLE (l)),
                           NULL, red);

    gimp_drawable_calculate_histogram (GIMP_DRAWABLE (l), histogram, FALSE);

    g_assert_false (gimp_histogram_is_cmyk (histogram));
    g_assert_cmpint (gimp_histogram_n_components (histogram), ==, 4);
    g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_RED));
    g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_LUMINANCE));
    g_assert_true  (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_ALPHA));
    g_assert_false (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_CYAN));
    g_assert_false (gimp_histogram_has_channel (histogram, GIMP_HISTOGRAM_BLACK));
    g_assert_cmpfloat (gimp_histogram_get_count (histogram, GIMP_HISTOGRAM_RED,
                                                 255, 255), ==, W * H);
    g_assert_cmpfloat (gimp_histogram_get_count (histogram, GIMP_HISTOGRAM_GREEN,
                                                 0, 0), ==, W * H);
    g_assert_cmpfloat (gimp_histogram_get_count (histogram, GIMP_HISTOGRAM_RGB,
                                                 0, 255), ==, 3 * W * H);
    g_assert_cmpfloat (gimp_histogram_get_count (histogram, GIMP_HISTOGRAM_BLACK,
                                                 0, 255), ==, 0.0);
    /* component 3 of RGBA is alpha */
    g_assert_cmpfloat (gimp_histogram_get_component (histogram, 3, 255), ==, W * H);

    g_object_unref (red);
    g_object_unref (rgb);
  }

  g_object_unref (histogram);
}


/*  H2: pickers on a CMYKA double image (I10, m12)  */

static void
h2_pickers (void)
{
  static const gdouble a[5] = { 0.1, 0.2, 0.3, 0.4, 1.0 };
  static const gdouble b[5] = { 0.9, 0.0, 0.5, 1.0, 1.0 };
  GimpImage   *image;
  GimpLayer   *layer;
  GeglBuffer  *buffer;
  GList       *drawables;
  const Babl  *native;
  const Babl  *sample_format = NULL;
  GeglColor   *color;
  gdouble      pixel[MAX_CHANNELS + 1];
  gdouble      got[MAX_CHANNELS];
  gdouble      expected[5];
  gint         c;

  image  = new_cmyk_image (GIMP_PRECISION_DOUBLE_NON_LINEAR);
  layer  = gimp_test_utils_add_cmyk_layer (image, "picks", TRUE, a);
  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));
  native = babl_format_with_space ("CMYKA double", gegl_buffer_get_format (buffer));

  /* right half: b */
  gimp_test_utils_write_cmyk_patch (buffer, GEGL_RECTANGLE (16, 0, 16, 32), b);

  g_assert_cmpint (babl_format_get_bytes_per_pixel (gegl_buffer_get_format (buffer)),
                   ==, 40);

  /* gimp_pickable_get_color_at(): native values, no overflow */
  color = gimp_pickable_get_color_at (GIMP_PICKABLE (layer), 3, 3);
  g_assert_nonnull (color);
  g_assert_true (gimp_babl_format_is_cmyk (gegl_color_get_format (color)));
  gegl_color_get_pixel (color, native, got);
  assert_cmyka_eq (got, a, 1e-12);
  g_object_unref (color);

  /* gimp_image_pick_color(), single pixel */
  drawables = g_list_prepend (NULL, layer);
  color     = gegl_color_new ("white");

  pixel[MAX_CHANNELS] = 12345.0; /* canary after the 40 native bytes */

  g_assert_true (gimp_image_pick_color (image, drawables, 20, 5,
                                        FALSE, FALSE, FALSE, 0.0,
                                        &sample_format, pixel, &color));
  g_assert_true (gimp_babl_format_is_cmyk (sample_format));
  g_assert_cmpint (babl_format_get_bytes_per_pixel (sample_format), ==, 40);
  assert_cmyka_eq (pixel, b, 0.0);
  g_assert_cmpfloat (pixel[MAX_CHANNELS], ==, 12345.0);

  g_assert_true (gimp_babl_format_is_cmyk (gegl_color_get_format (color)));
  gegl_color_get_pixel (color, native, got);
  assert_cmyka_eq (got, b, 1e-12);

  /* the same pick without a pixel buffer */
  g_assert_true (gimp_image_pick_color (image, drawables, 20, 5,
                                        FALSE, FALSE, FALSE, 0.0,
                                        NULL, NULL, &color));

  /* average: 5x5 around x = 15 has 3 columns of a and 2 of b, averaged
   * as inks (not through RGB)
   */
  g_assert_true (gimp_image_pick_color (image, drawables, 15, 10,
                                        FALSE, FALSE, TRUE, 2.0,
                                        &sample_format, pixel, &color));
  for (c = 0; c < 5; c++)
    expected[c] = (3.0 * a[c] + 2.0 * b[c]) / 5.0;

  gegl_color_get_pixel (color, native, got);
  assert_cmyka_eq (got, expected, 1e-9);
  /* the center pixel is still reported natively */
  assert_cmyka_eq (pixel, a, 0.0);

  /* average over a partly transparent area: alpha weighted */
  {
    static const gdouble t[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    GeglRectangle rect = { 0, 20, 3, 3 };

    gimp_test_utils_write_cmyk_patch (buffer, GEGL_RECTANGLE (0, 20, 3, 1), t);

    gimp_pickable_get_pixel_average (GIMP_PICKABLE (layer), &rect, native, got);

    for (c = 0; c < 4; c++)
      g_assert_cmpfloat_with_epsilon (got[c], a[c], 1e-9);
    g_assert_cmpfloat_with_epsilon (got[4], 6.0 / 9.0, 1e-9);
  }

  g_object_unref (color);
  g_list_free (drawables);
  g_object_unref (image);
}


/*  H3: select by color, composite criterion, on CMYK inks  */

static void
h3_select_by_color (void)
{
  static const gdouble k100[5]  = { 0.0, 0.0, 0.0, 1.0, 1.0 };
  static const gdouble rich[5]  = { 0.6, 0.5, 0.5, 1.0, 1.0 };
  GimpImage   *image;
  GimpLayer   *layer;
  GeglBuffer  *buffer;
  GList       *drawables;
  GeglColor   *color;
  GimpChannel *mask;
  gfloat      *sel;
  gint         x, y;

  image  = new_cmyk_image (gimp_test_utils_cmyk_precision ());
  layer  = gimp_test_utils_add_cmyk_layer (image, "blacks", FALSE, rich);
  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));

  gimp_test_utils_write_cmyk_patch (buffer, GEGL_RECTANGLE (0, 0, 16, 32), k100);

  /* K100 in the image space */
  color = gegl_color_new (NULL);
  gegl_color_set_pixel (color,
                        babl_format_with_space ("CMYKA double",
                                                gegl_buffer_get_format (buffer)),
                        k100);

  drawables = g_list_prepend (NULL, layer);
  mask      = gimp_image_get_mask (image);

  gimp_channel_select_by_color (mask, drawables, FALSE, color,
                                0.0, FALSE, GIMP_SELECT_CRITERION_COMPOSITE,
                                GIMP_CHANNEL_OP_REPLACE, FALSE, FALSE, 0, 0);

  sel = gimp_test_utils_read_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (mask)),
                                    NULL, babl_format ("Y float"));

  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++)
      g_assert_cmpfloat (sel[y * W + x], ==, x < 16 ? 1.0 : 0.0);

  g_free (sel);

  /* and the rich black, picked with the picker, selects the other half */
  g_object_unref (color);
  color = gimp_pickable_get_color_at (GIMP_PICKABLE (layer), 20, 20);

  gimp_channel_select_by_color (mask, drawables, FALSE, color,
                                0.0, FALSE, GIMP_SELECT_CRITERION_COMPOSITE,
                                GIMP_CHANNEL_OP_REPLACE, FALSE, FALSE, 0, 0);

  sel = gimp_test_utils_read_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (mask)),
                                    NULL, babl_format ("Y float"));

  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++)
      g_assert_cmpfloat (sel[y * W + x], ==, x < 16 ? 0.0 : 1.0);

  g_free (sel);

  g_object_unref (color);
  g_list_free (drawables);
  g_object_unref (image);
}


/*  H4: K-only text  */

static GimpLayer *
add_text_layer (GimpImage   *image,
                const gchar *text_str,
                const gchar *markup,
                GeglColor   *color)
{
  GimpContainer *fonts = gimp_data_factory_get_container (gimp->font_factory);
  GimpText      *text;
  GimpLayer     *layer;

  text = g_object_new (GIMP_TYPE_TEXT,
                       "gimp",           gimp,
                       "font",           gimp_container_get_first_child (fonts),
                       "font-size",      24.0,
                       "font-size-unit", gimp_unit_pixel (),
                       "color",          color,
                       "antialias",      TRUE,
                       NULL);

  if (markup)
    g_object_set (text, "markup", markup, NULL);
  else
    g_object_set (text, "text", text_str, NULL);

  layer = gimp_text_layer_new (image, text);
  g_object_unref (text);

  g_assert_nonnull (layer);
  gimp_image_add_layer (image, layer, NULL, 0, FALSE);

  return layer;
}

static void
h4_text (void)
{
  static const gdouble k100[5] = { 0.0, 0.0, 0.0, 1.0, 1.0 };
  GimpImage  *image;
  GimpLayer  *layer;
  GeglBuffer *buffer;
  GeglColor  *color;
  const Babl *u16;
  guint16    *pixels;
  gint        n;
  gint        i;
  gint        n_inked   = 0;
  gint        n_partial = 0;
  gint        n_full    = 0;

  gimp_data_factory_data_wait (gimp->font_factory);

  if (gimp_container_is_empty (gimp_data_factory_get_container (gimp->font_factory)))
    {
      g_test_skip ("H4: no fonts available");
      return;
    }

  image = new_cmyk_image (gimp_test_utils_cmyk_precision ());

  color = gegl_color_new (NULL);
  gegl_color_set_pixel (color,
                        babl_format_with_space ("CMYKA double",
                                                gimp_image_get_layer_space (image)),
                        k100);

  layer  = add_text_layer (image, "IWMgo", NULL, color);
  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));

  g_assert_true (gimp_drawable_is_cmyk (GIMP_DRAWABLE (layer)));

  u16    = babl_format_with_space ("CMYKA u16", gegl_buffer_get_format (buffer));
  pixels = gimp_test_utils_read_patch (buffer, NULL, u16);
  n      = gegl_buffer_get_width (buffer) * gegl_buffer_get_height (buffer);

  g_assert_cmpint (n, >, 0);

  for (i = 0; i < n; i++)
    {
      const guint16 *p = pixels + 5 * i;

      if (p[4] == 0)
        continue;

      n_inked++;

      /* K-only, everywhere the text has coverage */
      g_assert_cmpint (p[0], ==, 0);
      g_assert_cmpint (p[1], ==, 0);
      g_assert_cmpint (p[2], ==, 0);
      g_assert_cmpint (p[3], ==, 65535);

      if (p[4] == 65535)
        n_full++;
      else
        n_partial++;
    }

  g_free (pixels);

  /* alpha is the (antialiased) coverage */
  g_assert_cmpint (n_inked,   >, 0);
  g_assert_cmpint (n_full,    >, 0);
  g_assert_cmpint (n_partial, >, 0);

  /* two markup colors: RGB fallback, no crash, not K-only any more */
  {
    gboolean colored = FALSE;

    layer = add_text_layer (image, NULL,
                            "<markup><span foreground=\"#ff0000\">IW</span>"
                            "Mgo</markup>",
                            color);
    buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));
    pixels = gimp_test_utils_read_patch (buffer, NULL, u16);
    n      = gegl_buffer_get_width (buffer) * gegl_buffer_get_height (buffer);

    for (i = 0; i < n && ! colored; i++)
      {
        const guint16 *p = pixels + 5 * i;

        if (p[4] > 60000 && p[1] > 30000 && p[2] > 30000)
          colored = TRUE; /* red = M + Y */
      }

    g_free (pixels);

    g_assert_true (colored);
  }

  g_object_unref (color);
  g_object_unref (image);
}


/*  H5: the CMYK color selector creates colors in the image space  */

static void
find_spin_scales (GtkWidget *widget,
                  gpointer   data)
{
  GList **list = data;

  if (GIMP_IS_SPIN_SCALE (widget))
    *list = g_list_append (*list, widget);
  else if (GTK_IS_CONTAINER (widget))
    gtk_container_foreach (GTK_CONTAINER (widget), find_spin_scales, data);
}

static void
h5_cmyk_selector (void)
{
  const gchar *builddir = g_getenv ("GIMP_TESTING_ABS_TOP_BUILDDIR");
  GimpImage   *image;
  GimpLayer   *layer;
  GFile       *file;
  GType        type;
  GtkWidget   *selector;
  GeglColor   *white;
  GeglColor   *color;
  GList       *scales = NULL;
  GList       *iter;
  gdouble      values[4] = { 0.0, 0.0, 0.0, 100.0 };
  gdouble      got[5];
  guint16     *pixels;
  gint         i;

  if (! have_gtk)
    {
      g_test_skip ("H5: no display");
      return;
    }

  if (! builddir)
    {
      g_test_skip ("H5: GIMP_TESTING_ABS_TOP_BUILDDIR not set");
      return;
    }

  file = g_file_new_build_filename (builddir, "modules",
                                    "libcolor-selector-cmyk." G_MODULE_SUFFIX,
                                    NULL);

  if (! g_file_query_exists (file, NULL))
    {
      g_object_unref (file);
      g_test_skip ("H5: CMYK selector module not built");
      return;
    }

  /* modules are never unloaded */
  (void) gimp_module_new (file, TRUE, FALSE);
  g_object_unref (file);

  type = g_type_from_name ("ColorselCmyk");
  g_assert_true (type != G_TYPE_INVALID);

  image = new_cmyk_image (gimp_test_utils_cmyk_precision ());
  layer = gimp_test_utils_add_cmyk_layer (image, "fill", TRUE, NULL);

  white    = gegl_color_new ("white");
  selector = gimp_color_selector_new (type, white, GIMP_COLOR_SELECTOR_RED);
  g_object_ref_sink (selector);

  gimp_color_selector_set_format (GIMP_COLOR_SELECTOR (selector),
                                  gimp_image_get_layer_format (image, FALSE));

  find_spin_scales (selector, &scales);
  g_assert_cmpint (g_list_length (scales), ==, 4);

  for (iter = scales, i = 0; iter; iter = iter->next, i++)
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (iter->data), values[i]);

  color = gimp_color_selector_get_color (GIMP_COLOR_SELECTOR (selector));

  /* the color lives in the image's CMYK space */
  g_assert_true (gimp_babl_format_is_cmyk (gegl_color_get_format (color)));
  g_assert_true (babl_format_get_space (gegl_color_get_format (color)) ==
                 gimp_image_get_layer_space (image));

  gegl_color_get_pixel (color,
                        babl_format_with_space ("CMYKA double",
                                                gimp_image_get_layer_space (image)),
                        got);
  g_assert_cmpfloat (got[0], ==, 0.0);
  g_assert_cmpfloat (got[1], ==, 0.0);
  g_assert_cmpfloat (got[2], ==, 0.0);
  g_assert_cmpfloat_with_epsilon (got[3], 1.0, 1e-6);

  /* filling with it is exact */
  gimp_drawable_fill_buffer (GIMP_DRAWABLE (layer),
                             gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                             color, NULL, 0, 0);

  pixels = gimp_test_utils_read_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                       GEGL_RECTANGLE (0, 0, 1, 1),
                                       babl_format_with_space ("CMYKA u16",
                                                               gimp_image_get_layer_space (image)));
  g_assert_cmpint (pixels[0], ==, 0);
  g_assert_cmpint (pixels[1], ==, 0);
  g_assert_cmpint (pixels[2], ==, 0);
  g_assert_cmpint (pixels[3], ==, 65535);
  g_assert_cmpint (pixels[4], ==, 65535);
  g_free (pixels);

  /* ... and as FG through the context: gimp_context_set_foreground()
   * goes through gimp_color_set_alpha(), which keeps CMYK inks (01b).
   * Reset the layer to paper first, so the FG fill is what is measured.
   */
  gimp_drawable_fill_buffer (GIMP_DRAWABLE (layer),
                             gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                             white, NULL, 0, 0);
  pixels = gimp_test_utils_read_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                       GEGL_RECTANGLE (0, 0, 1, 1),
                                       babl_format_with_space ("CMYKA u16",
                                                               gimp_image_get_layer_space (image)));
  g_assert_cmpint (pixels[3], <, 65535);
  g_free (pixels);

  gimp_context_set_foreground (gimp_get_user_context (gimp), color);
  gimp_drawable_fill (GIMP_DRAWABLE (layer), gimp_get_user_context (gimp),
                      GIMP_FILL_FOREGROUND);

  pixels = gimp_test_utils_read_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                       GEGL_RECTANGLE (0, 0, 1, 1),
                                       babl_format_with_space ("CMYKA u16",
                                                               gimp_image_get_layer_space (image)));
  g_assert_cmpint (pixels[0], ==, 0);
  g_assert_cmpint (pixels[1], ==, 0);
  g_assert_cmpint (pixels[2], ==, 0);
  g_assert_cmpint (pixels[3], ==, 65535);
  g_assert_cmpint (pixels[4], ==, 65535);
  g_free (pixels);

  g_list_free (scales);
  g_object_unref (color);
  g_object_unref (white);
  gtk_widget_destroy (selector);
  g_object_unref (selector);
  g_object_unref (image);
}


/*  H6: a color tool (Levels) on a CMYK drawable  */

static void
h6_levels (void)
{
  static const gdouble patch[5] = { 0.2, 0.4, 0.6, 0.1, 1.0 };
  GimpImage   *image;
  GimpLayer   *layer;
  GeglBuffer  *buffer;
  const Babl  *native;
  GObject     *config;
  guint8      *before;
  guint8      *after;
  gsize        size;
  gdouble      left[5];

  image  = new_cmyk_image (gimp_test_utils_cmyk_precision ());
  layer  = gimp_test_utils_add_cmyk_layer (image, "levels", TRUE, patch);
  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));
  native = gegl_buffer_get_format (buffer);

  before = gimp_test_utils_read_patch (buffer, GEGL_RECTANGLE (16, 0, 16, 32),
                                       native);

  gimp_channel_select_rectangle (gimp_image_get_mask (image),
                                 0, 0, 16, 32, GIMP_CHANNEL_OP_REPLACE,
                                 FALSE, 0, 0, FALSE);

  config = g_object_new (GIMP_TYPE_LEVELS_CONFIG,
                         "channel", GIMP_HISTOGRAM_VALUE,
                         NULL);
  g_object_set (config, "gamma", 2.0, NULL);

  /* levels' channel setter ignores channels it has no curve for */
  g_object_set (config, "channel", GIMP_HISTOGRAM_BLACK, NULL);
  g_assert_cmpint (GIMP_LEVELS_CONFIG (config)->channel, ==, GIMP_HISTOGRAM_VALUE);

  gimp_drawable_apply_operation_by_name (GIMP_DRAWABLE (layer), NULL, "Levels",
                                         "gimp:levels", config);
  g_object_unref (config);

  /* still a CMYK layer, same format */
  g_assert_true (gimp_drawable_is_cmyk (GIMP_DRAWABLE (layer)));
  g_assert_true (gegl_buffer_get_format (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer))) ==
                 native);

  /* outside the selection: bit-identical */
  after = gimp_test_utils_read_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                      GEGL_RECTANGLE (16, 0, 16, 32), native);
  size  = (gsize) 16 * 32 * babl_format_get_bytes_per_pixel (native);
  g_assert_true (memcmp (before, after, size) == 0);

  /* inside: changed (gamma 2 lightens: less ink) */
  gimp_test_utils_read_cmyk_pixel (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                   4, 4, left);
  g_assert_cmpfloat (left[0] + left[1] + left[2] + left[3], <,
                     patch[0] + patch[1] + patch[2] + patch[3] - 0.05);
  g_assert_cmpfloat_with_epsilon (left[4], 1.0, 1e-4);

  g_free (before);
  g_free (after);
  g_object_unref (image);
}


/*  init with fonts (gimp_init_for_testing() runs without fonts)  */

static void
status_func_dummy (const gchar *text1,
                   const gchar *text2,
                   gdouble      percentage)
{
}

static Gimp *
init_gimp_with_fonts (void)
{
  Gimp *g;

  gimp_log_init ();
  gegl_init (NULL, NULL);

  g = gimp_new ("Unit Tested GIMP", NULL, NULL,
                FALSE /* be_verbose   */,
                TRUE  /* no_data      */,
                FALSE /* no_fonts     */,
                TRUE  /* no_interface */,
                FALSE, TRUE, FALSE, FALSE, FALSE,
                GIMP_STACK_TRACE_QUERY, GIMP_PDB_COMPAT_OFF);

  gimp_load_config (g, NULL, NULL);

  gimp_gegl_init (g);
  gimp_initialize (g, status_func_dummy);
  gimp_restore (g, status_func_dummy, NULL);

  return g;
}


int
main (int    argc,
      char **argv)
{
  gint result;

  have_gtk = gtk_init_check (&argc, &argv);

  g_test_init (&argc, &argv, NULL);

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_SRCDIR",
                                       "app/tests/gimpdir");

  gimp = init_gimp_with_fonts ();

  if (! gimp_babl_has_cmyk_support ())
    {
      g_printerr ("babl has no CMYK support (lcms): skipping\n");
      gimp_exit (gimp, TRUE);
      return GIMP_EXIT_TEST_SKIPPED;
    }

  g_test_add_func ("/cmyk-histogram/H1-histogram",        h1_histogram);
  g_test_add_func ("/cmyk-histogram/H1-no-alpha-and-rgb", h1_histogram_no_alpha_and_rgb);
  g_test_add_func ("/cmyk-histogram/H2-pickers",          h2_pickers);
  g_test_add_func ("/cmyk-histogram/H3-select-by-color",  h3_select_by_color);
  g_test_add_func ("/cmyk-histogram/H4-k-only-text",      h4_text);
  g_test_add_func ("/cmyk-histogram/H5-cmyk-selector",    h5_cmyk_selector);
  g_test_add_func ("/cmyk-histogram/H6-levels",           h6_levels);

  result = g_test_run ();

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_BUILDDIR",
                                       "app/tests/gimpdir-output");

  gimp_exit (gimp, TRUE);

  return result;
}

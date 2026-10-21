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

/* Golden regression test for RGB/GRAY compositing and painting (G1-G3,
 * devel-docs/cmyk-mode/TESTING.md).
 *
 * The SHA-1 sums in layer-modes-golden.h were captured on the compositing
 * code as it was *before* the CMYK work touched it.  They are frozen: never
 * regenerate them to make a failing test pass.  To (re)write the header on a
 * known-good tree run the test with
 *
 *   GIMP_TESTING_GOLDEN_WRITE=/path/to/layer-modes-golden.h
 */

#include "config.h"

#include <string.h>
#include <stdio.h>

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"

#include "widgets/widgets-types.h"

#include "widgets/gimpuimanager.h"

#include "config/gimpgeglconfig.h"

#include "operations/layer-modes/gimp-layer-modes.h"

#include "gegl/gimp-gegl-apply-operation.h"

#include "core/gimp.h"
#include "core/gimpbrushgenerated.h"
#include "core/gimpcontainer.h"
#include "core/gimpcontext.h"
#include "core/gimpdrawable.h"
#include "core/gimpdynamics.h"
#include "core/gimpimage.h"
#include "core/gimpimage-merge.h"
#include "core/gimplayer.h"
#include "core/gimplayer-new.h"
#include "core/gimppaintinfo.h"
#include "core/gimppickable.h"
#include "core/gimpprojection.h"

#include "paint/gimppaintcore.h"
#include "paint/gimppaintcore-stroke.h"
#include "paint/gimppaintoptions.h"

#include "tests.h"

#include "gimp-app-test-utils.h"

#include "layer-modes-golden.h"


#define SIZE 32


typedef struct
{
  const gchar       *name;
  GimpImageBaseType  base_type;
  GimpPrecision      precision;
} GoldenImageType;

static const GoldenImageType g1_types[] =
{
  { "rgb-u16",   GIMP_RGB,  GIMP_PRECISION_U16_NON_LINEAR },
  { "rgb-float", GIMP_RGB,  GIMP_PRECISION_FLOAT_LINEAR   },
  { "gray-u16",  GIMP_GRAY, GIMP_PRECISION_U16_NON_LINEAR }
};

static const GimpLayerColorSpace composite_spaces[] =
{
  GIMP_LAYER_COLOR_SPACE_AUTO,
  GIMP_LAYER_COLOR_SPACE_RGB_LINEAR,
  GIMP_LAYER_COLOR_SPACE_RGB_NON_LINEAR,
  GIMP_LAYER_COLOR_SPACE_RGB_PERCEPTUAL
};

static const GimpLayerCompositeMode composite_modes[] =
{
  GIMP_LAYER_COMPOSITE_AUTO,
  GIMP_LAYER_COMPOSITE_UNION,
  GIMP_LAYER_COMPOSITE_CLIP_TO_BACKDROP,
  GIMP_LAYER_COMPOSITE_CLIP_TO_LAYER,
  GIMP_LAYER_COMPOSITE_INTERSECTION
};


static Gimp     *gimp          = NULL;
static GString  *golden_output = NULL;   /* non-NULL in write mode */
static gint      n_mismatches  = 0;


/*  helpers  */

static const gchar *
enum_nick (GType type,
           gint  value)
{
  const gchar *nick = NULL;

  gimp_enum_get_value (type, value, NULL, &nick, NULL, NULL);

  return nick ? nick : "unknown";
}

static const gchar *
golden_lookup (const gchar *key)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (layer_modes_golden); i++)
    {
      if (layer_modes_golden[i].key &&
          ! strcmp (layer_modes_golden[i].key, key))
        return layer_modes_golden[i].sha1;
    }

  return NULL;
}

/* In write mode, records the sum; otherwise compares it with the golden
 * value and counts (and reports) mismatches.
 */
static void
golden_check (const gchar *key,
              const gchar *sha1)
{
  if (golden_output)
    {
      g_string_append_printf (golden_output, "  { \"%s\", \"%s\" },\n",
                              key, sha1);
    }
  else
    {
      const gchar *expected = golden_lookup (key);

      if (! expected)
        {
          g_printerr ("golden: no golden value for '%s'\n", key);
          n_mismatches++;
        }
      else if (strcmp (expected, sha1))
        {
          g_printerr ("golden: MISMATCH for '%s': expected %s, got %s\n",
                      key, expected, sha1);
          n_mismatches++;
        }
    }
}

static void
checksum_buffer (GChecksum  *checksum,
                 GeglBuffer *buffer)
{
  const GeglRectangle *extent = gegl_buffer_get_extent (buffer);
  const Babl          *format = gegl_buffer_get_format (buffer);
  gint                 bpp    = babl_format_get_bytes_per_pixel (format);
  guchar              *data;

  data = g_malloc (extent->width * extent->height * bpp);

  gegl_buffer_get (buffer, extent, 1.0, format, data,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

  g_checksum_update (checksum, (const guchar *) babl_get_name (format), -1);
  g_checksum_update (checksum, data, extent->width * extent->height * bpp);

  g_free (data);
}

static gchar *
buffer_sha1 (GeglBuffer *buffer)
{
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA1);
  gchar     *result;

  checksum_buffer (checksum, buffer);
  result = g_strdup (g_checksum_get_string (checksum));
  g_checksum_free (checksum);

  return result;
}

/* Fills @buffer with deterministic content: a seeded random colour per
 * 4x4 cell, with fully transparent and fully opaque cells, plus a few
 * sub-cell pixels with random alpha.
 */
static void
fill_seeded (GeglBuffer *buffer,
             guint32     seed,
             gboolean    vary_alpha)
{
  const GeglRectangle *extent = gegl_buffer_get_extent (buffer);
  GRand               *rand   = g_rand_new_with_seed (seed);
  gdouble             *data;
  gint                 x, y;

  data = g_new (gdouble, extent->width * extent->height * 4);

  for (y = 0; y < extent->height; y++)
    for (x = 0; x < extent->width; x++)
      {
        gdouble *p = data + (y * extent->width + x) * 4;

        p[0] = g_rand_double (rand);
        p[1] = g_rand_double (rand);
        p[2] = g_rand_double (rand);

        if (! vary_alpha)
          p[3] = 1.0;
        else if ((x / 8 + y / 8) % 4 == 0)
          p[3] = 0.0;
        else if ((x / 8 + y / 8) % 4 == 1)
          p[3] = 1.0;
        else
          p[3] = g_rand_double (rand);
      }

  gegl_buffer_set (buffer, extent, 0,
                   babl_format_with_space ("R'G'B'A double",
                                           gegl_buffer_get_format (buffer)),
                   data, GEGL_AUTO_ROWSTRIDE);

  g_free (data);
  g_rand_free (rand);
}

static GimpLayer *
add_seeded_layer (GimpImage     *image,
                  const gchar   *name,
                  guint32        seed,
                  gboolean       vary_alpha,
                  gdouble        opacity,
                  GimpLayerMode  mode)
{
  GimpLayer *layer;

  layer = gimp_layer_new (image, SIZE, SIZE,
                          gimp_image_get_layer_format (image, TRUE),
                          name, opacity, mode);

  fill_seeded (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
               seed, vary_alpha);

  /* always on top of the stack */
  gimp_image_add_layer (image, layer, NULL, 0, FALSE);

  return layer;
}

static GeglBuffer *
get_projection_buffer (GimpImage *image)
{
  GimpProjection *projection = gimp_image_get_projection (image);

  gimp_pickable_flush (GIMP_PICKABLE (projection));

  return gimp_pickable_get_buffer (GIMP_PICKABLE (projection));
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

      if (mode < 0)
        continue;

      if (gimp_layer_mode_get_context (mode) & context)
        modes = g_list_prepend (modes, GINT_TO_POINTER (mode));
    }

  g_type_class_unref (enum_class);

  return g_list_reverse (modes);
}


/*  G1: layer modes x composite space x composite mode  */

static void
golden_layer_modes (gconstpointer data)
{
  const GoldenImageType *type = data;
  GimpImage             *image;
  GimpLayer             *top;
  GList                 *modes;
  GList                 *list;

  image = gimp_image_new (gimp, SIZE, SIZE, type->base_type, type->precision);

  add_seeded_layer (image, "backdrop", 1234, TRUE, 1.0,
                    GIMP_LAYER_MODE_NORMAL);
  top = add_seeded_layer (image, "top", 5678, TRUE, 0.7,
                          GIMP_LAYER_MODE_NORMAL);

  n_mismatches = 0;

  modes = get_modes (GIMP_LAYER_MODE_CONTEXT_LAYER);

  for (list = modes; list; list = g_list_next (list))
    {
      GimpLayerMode mode = GPOINTER_TO_INT (list->data);
      gint          n_spaces;
      gint          n_cmodes;
      gint          s, c;

      gimp_layer_set_mode (top, mode, FALSE);

      n_spaces = gimp_layer_mode_is_composite_space_mutable (mode) ?
                 G_N_ELEMENTS (composite_spaces) : 1;
      n_cmodes = gimp_layer_mode_is_composite_mode_mutable (mode) ?
                 G_N_ELEMENTS (composite_modes) : 1;

      for (s = 0; s < n_spaces; s++)
        {
          GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA1);
          gchar     *key;

          gimp_layer_set_composite_space (top, composite_spaces[s], FALSE);

          for (c = 0; c < n_cmodes; c++)
            {
              gimp_layer_set_composite_mode (top, composite_modes[c], FALSE);

              checksum_buffer (checksum, get_projection_buffer (image));
            }

          key = g_strdup_printf ("G1/%s/%s/%s",
                                 type->name,
                                 enum_nick (GIMP_TYPE_LAYER_MODE, mode),
                                 enum_nick (GIMP_TYPE_LAYER_COLOR_SPACE,
                                            composite_spaces[s]));

          golden_check (key, g_checksum_get_string (checksum));

          g_free (key);
          g_checksum_free (checksum);

          gimp_layer_set_composite_mode (top, GIMP_LAYER_COMPOSITE_AUTO,
                                         FALSE);
        }

      gimp_layer_set_composite_space (top, GIMP_LAYER_COLOR_SPACE_AUTO,
                                      FALSE);
    }

  g_list_free (modes);
  g_object_unref (image);

  g_assert_cmpint (n_mismatches, ==, 0);
}


/*  G2: paint modes  */

static void
golden_paint_modes (void)
{
  static const GimpCoords default_coords = GIMP_COORDS_DEFAULT_VALUES;
  static const gdouble    points[][2]    = { {  4.0,  4.0 },
                                             { 27.0, 12.0 },
                                             { 20.0, 27.0 },
                                             {  6.0, 22.0 } };
  GimpImage        *image;
  GimpLayer        *layer;
  GimpPaintInfo    *info;
  GimpPaintOptions *options;
  GimpBrush        *brush;
  GimpDynamics     *dynamics;
  GeglColor        *fg;
  GimpCoords        coords[G_N_ELEMENTS (points)];
  GList            *modes;
  GList            *list;
  gint              i;

  image = gimp_image_new (gimp, SIZE, SIZE,
                          GIMP_RGB, GIMP_PRECISION_U16_NON_LINEAR);
  layer = add_seeded_layer (image, "paint", 4321, TRUE, 1.0,
                            GIMP_LAYER_MODE_NORMAL);

  info = GIMP_PAINT_INFO (gimp_container_get_child_by_name (gimp->paint_info_list,
                                                            "gimp-paintbrush"));
  g_assert_nonnull (info);

  options = gimp_paint_options_new (info);
  brush   = GIMP_BRUSH (gimp_brush_generated_new ("golden",
                                                  GIMP_BRUSH_GENERATED_CIRCLE,
                                                  5.0, 2, 0.5, 1.0, 0.0));
  fg = gegl_color_new (NULL);
  gegl_color_set_rgba_with_space (fg, 0.8, 0.3, 0.1, 1.0, NULL);

  dynamics = GIMP_DYNAMICS (gimp_dynamics_new (GIMP_CONTEXT (options),
                                               "golden"));

  gimp_context_set_brush      (GIMP_CONTEXT (options), brush);
  gimp_context_set_dynamics   (GIMP_CONTEXT (options), dynamics);
  gimp_context_set_foreground (GIMP_CONTEXT (options), fg);
  gimp_context_set_opacity    (GIMP_CONTEXT (options), 0.8);
  g_object_set (options,
                "dynamics-enabled", FALSE,
                "brush-size",       10.0,
                "brush-spacing",    0.1,
                "brush-hardness",   0.5,
                "brush-force",      0.5,
                NULL);

  for (i = 0; i < G_N_ELEMENTS (points); i++)
    {
      coords[i]   = default_coords;
      coords[i].x = points[i][0];
      coords[i].y = points[i][1];
    }

  n_mismatches = 0;

  modes = get_modes (GIMP_LAYER_MODE_CONTEXT_PAINT);

  for (list = modes; list; list = g_list_next (list))
    {
      GimpLayerMode  mode = GPOINTER_TO_INT (list->data);
      GimpPaintCore *core;
      GError        *error = NULL;
      gchar         *sha1;
      gchar         *key;

      fill_seeded (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                   4321, TRUE);

      gimp_context_set_paint_mode (GIMP_CONTEXT (options), mode);

      core = g_object_new (info->paint_type, NULL);

      if (! gimp_paint_core_stroke (core, GIMP_DRAWABLE (layer), options,
                                    coords, G_N_ELEMENTS (coords),
                                    FALSE, &error))
        {
          g_printerr ("golden: stroke failed for mode %s: %s\n",
                      enum_nick (GIMP_TYPE_LAYER_MODE, mode),
                      error ? error->message : "(no error)");
          g_clear_error (&error);
          n_mismatches++;
        }

      g_object_unref (core);

      sha1 = buffer_sha1 (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)));
      key  = g_strdup_printf ("G2/rgb-u16/%s",
                              enum_nick (GIMP_TYPE_LAYER_MODE, mode));

      golden_check (key, sha1);

      g_free (key);
      g_free (sha1);
    }

  g_list_free (modes);
  g_object_unref (fg);
  g_object_unref (brush);
  g_object_unref (dynamics);
  g_object_unref (options);
  g_object_unref (image);

  g_assert_cmpint (n_mismatches, ==, 0);
}


/*  G3: flatten, merge-down, gimp_gegl_apply_flatten()  */

static GimpImage *
create_merge_image (GimpLayer **multiply)
{
  GimpImage *image;
  GimpLayer *top;

  image = gimp_image_new (gimp, SIZE, SIZE,
                          GIMP_RGB, GIMP_PRECISION_U16_NON_LINEAR);

  add_seeded_layer (image, "bottom", 11, TRUE, 1.0, GIMP_LAYER_MODE_NORMAL);
  add_seeded_layer (image, "middle", 22, TRUE, 0.6, GIMP_LAYER_MODE_NORMAL);
  top = add_seeded_layer (image, "multiply", 33, TRUE, 0.9,
                          GIMP_LAYER_MODE_MULTIPLY);

  if (multiply)
    *multiply = top;

  return image;
}

static void
golden_merge (void)
{
  GimpContext *context = gimp_context_new (gimp, "golden", NULL);
  GimpImage   *image;
  GimpLayer   *multiply;
  GimpLayer   *layer;
  GList       *layers;
  GList       *merged;
  GeglColor   *background;
  GeglBuffer  *dest;
  GError      *error = NULL;
  gchar       *sha1;

  n_mismatches = 0;

  /* flatten */
  image = create_merge_image (NULL);
  layer = gimp_image_flatten (image, context, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (layer);

  sha1 = buffer_sha1 (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)));
  golden_check ("G3/rgb-u16/flatten", sha1);
  g_free (sha1);
  g_object_unref (image);

  /* merge down the multiply layer */
  image  = create_merge_image (&multiply);
  layers = g_list_prepend (NULL, multiply);
  merged = gimp_image_merge_down (image, layers, context,
                                  GIMP_EXPAND_AS_NECESSARY,
                                  NULL, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (merged);

  sha1 = buffer_sha1 (gimp_drawable_get_buffer (GIMP_DRAWABLE (merged->data)));
  golden_check ("G3/rgb-u16/merge-down", sha1);
  g_free (sha1);
  g_list_free (merged);
  g_list_free (layers);

  sha1 = buffer_sha1 (get_projection_buffer (image));
  golden_check ("G3/rgb-u16/merge-down-projection", sha1);
  g_free (sha1);
  g_object_unref (image);

  /* gimp_gegl_apply_flatten() on a layer */
  image = create_merge_image (&multiply);
  background = gegl_color_new (NULL);
  gegl_color_set_rgba_with_space (background, 0.2, 0.5, 0.9, 1.0, NULL);

  dest = gegl_buffer_new (GEGL_RECTANGLE (0, 0, SIZE, SIZE),
                          gimp_image_get_layer_format (image, FALSE));

  gimp_gegl_apply_flatten (gimp_drawable_get_buffer (GIMP_DRAWABLE (multiply)),
                           NULL, NULL, dest, background,
                           GIMP_LAYER_COLOR_SPACE_RGB_LINEAR);

  sha1 = buffer_sha1 (dest);
  golden_check ("G3/rgb-u16/apply-flatten", sha1);
  g_free (sha1);

  g_object_unref (dest);
  g_object_unref (background);
  g_object_unref (image);
  g_object_unref (context);

  g_assert_cmpint (n_mismatches, ==, 0);
}


int
main (int    argc,
      char **argv)
{
  const gchar *write_path;
  gint         result;
  gint         i;

  /* babl chooses conversion paths and LUTs by timing measurements, which
   * makes RGB(perceptual)/LCh results vary from run to run in the last
   * bit.  Force exact (reference) conversions, before babl is initialized.
   */
  g_setenv ("BABL_TOLERANCE",     "0", TRUE);
  g_setenv ("BABL_LUT",           "0", TRUE);
  g_setenv ("BABL_INHIBIT_CACHE", "1", TRUE);

  g_test_init (&argc, &argv, NULL);

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_SRCDIR",
                                       "app/tests/gimpdir");

  gimp = gimp_init_for_testing ();

  /* single-threaded rendering, for determinism */
  g_object_set (gimp->config, "num-processors", 1, NULL);
  g_object_set (gegl_config (), "threads", 1, NULL);

  write_path = g_getenv ("GIMP_TESTING_GOLDEN_WRITE");
  if (write_path && *write_path)
    golden_output = g_string_new (NULL);

  for (i = 0; i < G_N_ELEMENTS (g1_types); i++)
    {
      gchar *path = g_strdup_printf ("/layer-modes-golden/G1/%s",
                                     g1_types[i].name);

      g_test_add_data_func (path, &g1_types[i], golden_layer_modes);
      g_free (path);
    }

  g_test_add_func ("/layer-modes-golden/G2/paint-modes", golden_paint_modes);
  g_test_add_func ("/layer-modes-golden/G3/merge",       golden_merge);

  result = g_test_run ();

  if (golden_output)
    {
      GString *header = g_string_new (NULL);
      GError  *error  = NULL;

      g_string_append (header,
                       "/* Generated by test-layer-modes-golden with "
                       "GIMP_TESTING_GOLDEN_WRITE.\n"
                       " * FROZEN: captured on the pre-CMYK compositing code "
                       "(devel-docs/cmyk-mode/TESTING.md G1-G3).\n"
                       " * Never regenerate this file to make the test pass.\n"
                       " */\n\n"
                       "#pragma once\n\n"
                       "static const struct\n"
                       "{\n"
                       "  const char *key;\n"
                       "  const char *sha1;\n"
                       "} layer_modes_golden[] =\n"
                       "{\n");
      g_string_append (header, golden_output->str);
      g_string_append (header, "  { NULL, NULL }\n};\n");

      if (! g_file_set_contents (write_path, header->str, -1, &error))
        {
          g_printerr ("golden: cannot write %s: %s\n",
                      write_path, error->message);
          g_clear_error (&error);
          result = 1;
        }
      else
        {
          g_printerr ("golden: wrote %s\n", write_path);
        }

      g_string_free (header, TRUE);
      g_string_free (golden_output, TRUE);
    }

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_BUILDDIR",
                                       "app/tests/gimpdir-output");

  gimp_exit (gimp, TRUE);

  return result;
}

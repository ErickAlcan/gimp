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

/* CMYK image tests I1-I15 (devel-docs/cmyk-mode/TESTING.md).
 * Owner: WS03.
 *
 * All parts run unconditionally since the integration of every
 * workstream (WS09 removed the WS01b/WS02/WS04 detection gates).
 */

#include "config.h"

#include <string.h>
#include <unistd.h>

#include <gegl.h>
#include <gtk/gtk.h>
#include <lcms2.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"
#include "libgimpconfig/gimpconfig.h"

#include "widgets/widgets-types.h"

#include "widgets/gimpuimanager.h"

#include "gegl/gimp-babl.h"

#include "core/gimp.h"
#include "core/gimp-edit.h"
#include "core/gimpbuffer.h"
#include "core/gimpchannel.h"
#include "core/gimpchannel-select.h"
#include "core/gimpcontainer.h"
#include "core/gimpcontext.h"
#include "core/gimpdrawable.h"
#include "core/gimpdrawable-preview.h"
#include "core/gimpgrouplayer.h"
#include "core/gimpimage.h"
#include "core/gimpimage-color-profile.h"
#include "core/gimpimage-colormap.h"
#include "core/gimpimage-convert-indexed.h"
#include "core/gimpimage-convert-precision.h"
#include "core/gimpimage-convert-type.h"
#include "core/gimpimage-crop.h"
#include "core/gimpimage-flip.h"
#include "core/gimpimage-new.h"
#include "core/gimpimage-preview.h"
#include "core/gimpimage-resize.h"
#include "core/gimpimage-rotate.h"
#include "core/gimpimage-scale.h"
#include "core/gimpimage-undo.h"
#include "core/gimpundostack.h"
#include "core/gimplayer.h"
#include "core/gimplayer-floating-selection.h"
#include "core/gimplayer-new.h"
#include "core/gimplayermask.h"
#include "core/gimpselection.h"
#include "core/gimptempbuf.h"
#include "core/gimptemplate.h"

#include "operations/layer-modes/gimp-layer-modes.h"

#include "plug-in/gimppluginmanager-file.h"

#include "file/file-open.h"
#include "file/file-save.h"

#include "tests.h"

#include "gimp-app-test-utils.h"


#define W 16
#define H 12

#define TYPE_CMYK_16_NO_ALPHA (COLORSPACE_SH (PT_CMYK) | CHANNELS_SH (4) | BYTES_SH (2))


static Gimp *gimp = NULL;


/*  helpers  */

static GimpContext *
context (void)
{
  return gimp_get_user_context (gimp);
}

static GimpColorProfile *
builtin_cmyk (void)
{
  return gimp_color_profile_new_cmyk_builtin ();
}

/*  K-only, 4-color, C100, paper, 50 % K, translucent rich black  */
static const gdouble patches[][5] =
{
  { 0.0, 0.0, 0.0, 1.0, 1.0 },
  { 0.1, 0.2, 0.3, 0.4, 1.0 },
  { 1.0, 0.0, 0.0, 0.0, 1.0 },
  { 0.0, 0.0, 0.0, 0.0, 1.0 },
  { 0.0, 0.0, 0.0, 0.5, 1.0 },
  { 0.6, 0.4, 0.4, 1.0, 0.5 },
};

#define N_PATCHES ((gint) G_N_ELEMENTS (patches))

/*  vertical stripes of the patches above, and a seeded pseudo-random
 *  bottom row so geometry tests see distinct pixels
 */
static void
fill_patches (GimpDrawable *drawable)
{
  GeglBuffer *buffer = gimp_drawable_get_buffer (drawable);
  gint        width  = gimp_item_get_width  (GIMP_ITEM (drawable));
  gint        height = gimp_item_get_height (GIMP_ITEM (drawable));
  gint        stripe = MAX (1, width / N_PATCHES);
  GRand      *rand   = g_rand_new_with_seed (4711);
  gint        i;
  gint        x;

  for (i = 0; i < N_PATCHES; i++)
    {
      GeglRectangle rect = { i * stripe, 0, stripe, height - 1 };

      if (i == N_PATCHES - 1)
        rect.width = width - rect.x;

      gimp_test_utils_write_cmyk_patch (buffer, &rect, patches[i]);
    }

  for (x = 0; x < width; x++)
    {
      gdouble cmyka[5];

      for (i = 0; i < 5; i++)
        cmyka[i] = g_rand_double (rand);

      cmyka[4] = 1.0;

      gimp_test_utils_write_cmyk_patch (buffer,
                                        GEGL_RECTANGLE (x, height - 1, 1, 1),
                                        cmyka);
    }

  g_rand_free (rand);
}

static GimpImage *
new_cmyk_image (GimpPrecision     precision,
                GimpColorProfile *profile,
                GimpLayer       **layer)
{
  GimpImage *image;
  GimpLayer *l;

  image = gimp_test_utils_create_cmyk_image (gimp, W, H, precision, profile);

  gimp_image_undo_disable (image);
  l = gimp_test_utils_add_cmyk_layer (image, "patches", FALSE, NULL);
  fill_patches (GIMP_DRAWABLE (l));
  gimp_image_undo_enable (image);
  gimp_image_clean_all (image);

  if (layer)
    *layer = l;

  return image;
}

/*  RGB u16 values written as R'G'B' u16 (sRGB)  */
static const guint16 rgb_patches[][3] =
{
  {     0,     0,     0 },
  { 65535, 65535, 65535 },
  { 65535,     0,     0 },
  {     0, 32768, 65535 },
  { 13107, 26214, 39321 },
  { 50000, 40000, 10000 },
};

#define N_RGB_PATCHES ((gint) G_N_ELEMENTS (rgb_patches))

static GimpImage *
new_rgb_image (GimpPrecision   precision,
               gboolean        with_alpha,
               GimpLayer     **layer)
{
  GimpImage  *image;
  GimpLayer  *l;
  GeglBuffer *buffer;
  gint        i;

  image = gimp_image_new (gimp, N_RGB_PATCHES, 1, GIMP_RGB, precision);

  gimp_image_undo_disable (image);

  l = gimp_layer_new (image, N_RGB_PATCHES, 1,
                      gimp_image_get_layer_format (image, with_alpha),
                      "rgb", GIMP_OPACITY_OPAQUE, GIMP_LAYER_MODE_NORMAL);
  gimp_image_add_layer (image, l, NULL, 0, FALSE);

  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (l));

  for (i = 0; i < N_RGB_PATCHES; i++)
    gegl_buffer_set (buffer, GEGL_RECTANGLE (i, 0, 1, 1), 0,
                     babl_format ("R'G'B' u16"), rgb_patches[i],
                     GEGL_AUTO_ROWSTRIDE);

  gimp_image_undo_enable (image);
  gimp_image_clean_all (image);

  if (layer)
    *layer = l;

  return image;
}

static void
lcms_transform (GimpColorProfile *src,
                cmsUInt32Number   src_type,
                gconstpointer     src_data,
                GimpColorProfile *dest,
                cmsUInt32Number   dest_type,
                gpointer          dest_data,
                gint              n,
                cmsUInt32Number   intent,
                gboolean          bpc)
{
  cmsHTRANSFORM transform;

  transform = cmsCreateTransform (gimp_color_profile_get_lcms_profile (src),
                                  src_type,
                                  gimp_color_profile_get_lcms_profile (dest),
                                  dest_type,
                                  intent,
                                  cmsFLAGS_NOOPTIMIZE |
                                  (bpc ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0));
  g_assert_nonnull (transform);

  cmsDoTransform (transform, src_data, dest_data, n);
  cmsDeleteTransform (transform);
}

static void
assert_u16_close (const guint16 *a,
                  const guint16 *b,
                  gint           n,
                  gint           tolerance)
{
  gint i;

  for (i = 0; i < n; i++)
    {
      if (ABS ((gint) a[i] - (gint) b[i]) > tolerance)
        g_error ("value %d: %u vs %u (tolerance %d)", i, a[i], b[i], tolerance);
    }
}

/*  bytes of @rect of @drawable read in @format  */
static GBytes *
read_bytes (GimpDrawable        *drawable,
            const GeglRectangle *rect,
            const Babl          *format)
{
  GeglBuffer *buffer = gimp_drawable_get_buffer (drawable);
  gpointer    data;

  if (! rect)
    rect = gegl_buffer_get_extent (buffer);

  data = gimp_test_utils_read_patch (buffer, rect, format);

  return g_bytes_new_take (data,
                           (gsize) rect->width * rect->height *
                           babl_format_get_bytes_per_pixel (format));
}

static void
assert_bytes_equal (GBytes *a,
                    GBytes *b)
{
  const guint8 *da = g_bytes_get_data (a, NULL);
  const guint8 *db = g_bytes_get_data (b, NULL);
  gsize         i;

  g_assert_cmpuint (g_bytes_get_size (a), ==, g_bytes_get_size (b));

  for (i = 0; i < g_bytes_get_size (a); i++)
    if (da[i] != db[i])
      g_error ("bytes differ at offset %" G_GSIZE_FORMAT ": %u vs %u",
               i, da[i], db[i]);
}

static void
assert_no_undo (GimpImage *image)
{
  g_assert_null (gimp_undo_stack_peek (gimp_image_get_undo_stack (image)));
}

static GList *
single (gpointer data)
{
  return g_list_prepend (NULL, data);
}

static void
assert_cmyk_image (GimpImage        *image,
                   GimpColorProfile *profile)
{
  GimpColorProfile *image_profile;
  const Babl       *space;
  GList            *layers;
  GList            *list;

  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_CMYK);
  g_assert_cmpint (gimp_babl_trc (gimp_image_get_precision (image)), ==,
                   GIMP_TRC_NON_LINEAR);

  image_profile = gimp_image_get_color_profile (image);
  g_assert_nonnull (image_profile);
  g_assert_true (gimp_color_profile_is_cmyk (image_profile));

  if (profile)
    g_assert_true (gimp_color_profile_is_equal (image_profile, profile));

  space = gimp_image_get_layer_space (image);
  g_assert_true (gimp_babl_space_is_cmyk (space));

  layers = gimp_image_get_layer_list (image);

  for (list = layers; list; list = g_list_next (list))
    {
      const Babl *format = gimp_drawable_get_format (list->data);

      g_assert_false (gimp_layer_mode_is_legacy (gimp_layer_get_mode (list->data)));

      if (gimp_viewable_get_children (list->data))
        continue;

      g_assert_true (gimp_babl_format_is_cmyk (format));
      g_assert_true (babl_format_get_space (format) == space);
    }

  g_list_free (layers);
}


/*  I1  */

static void
i1_rgb_to_cmyk_structure (void)
{
  GimpColorProfile *profile = builtin_cmyk ();
  GimpImage        *image;
  GimpLayer        *layer;
  GError           *error = NULL;

  image = new_rgb_image (GIMP_PRECISION_U16_NON_LINEAR, FALSE, &layer);

  g_assert_true (gimp_image_convert_type_full (image, GIMP_CMYK, profile,
                                               GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                               TRUE, NULL, &error));
  g_assert_no_error (error);

  assert_cmyk_image (image, profile);
  g_assert_cmpint (gimp_image_get_precision (image), ==,
                   GIMP_PRECISION_U16_NON_LINEAR);
  g_assert_true (gimp_drawable_is_cmyk (GIMP_DRAWABLE (layer)));
  g_assert_false (gimp_drawable_has_alpha (GIMP_DRAWABLE (layer)));

  /*  the conversion intent is reset  */
  {
    GimpColorRenderingIntent intent;
    gboolean                 bpc;

    _gimp_image_get_conversion_intent (image, &intent, &bpc);
    g_assert_cmpint (intent, ==, GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL);
    g_assert_true (bpc);
  }

  /*  one undo step  */
  g_assert_true (gimp_image_undo (image));
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_RGB);
  assert_no_undo (image);

  /*  NULL profile: the default CMYK profile  */
  g_assert_true (gimp_image_convert_type (image, GIMP_CMYK, NULL, NULL, &error));
  g_assert_no_error (error);
  assert_cmyk_image (image, NULL);

  g_object_unref (image);

  /*  invalid targets are refused, the image is unchanged  */
  image = new_rgb_image (GIMP_PRECISION_U16_NON_LINEAR, FALSE, NULL);
  {
    GimpColorProfile *input_only = gimp_test_utils_cmyk_input_only_profile ();
    GimpColorProfile *srgb       = gimp_color_profile_new_rgb_srgb ();

    g_assert_false (gimp_image_convert_type_full (image, GIMP_CMYK, input_only,
                                                  GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                                  TRUE, NULL, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);

    g_assert_false (gimp_image_convert_type_full (image, GIMP_CMYK, srgb,
                                                  GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                                  TRUE, NULL, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);

    g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_RGB);
    assert_no_undo (image);

    g_object_unref (input_only);
    g_object_unref (srgb);
  }

  g_object_unref (image);
  g_object_unref (profile);
}

static void
i1_rgb_to_cmyk_real_profiles (void)
{
  const gchar *env = g_getenv ("GIMP_TESTING_CMYK_ICC");
  gchar      **files;
  gint         i;

  if (! env || ! *env)
    {
      g_test_skip ("GIMP_TESTING_CMYK_ICC not set");
      return;
    }

  files = g_strsplit (env, ":", -1);

  for (i = 0; files[i]; i++)
    {
      GimpColorProfile *profile;
      GimpImage        *image;
      GFile            *file;
      GError           *error = NULL;

      file    = g_file_new_for_path (files[i]);
      profile = gimp_color_profile_new_from_file (file, &error);
      g_assert_no_error (error);
      g_object_unref (file);

      image = new_rgb_image (GIMP_PRECISION_U16_LINEAR, TRUE, NULL);

      g_assert_true (gimp_image_convert_type_full (image, GIMP_CMYK, profile,
                                                   GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                                   TRUE, NULL, &error));
      g_assert_no_error (error);
      assert_cmyk_image (image, profile);

      g_assert_true (gimp_image_convert_type (image, GIMP_RGB, NULL, NULL, &error));
      g_assert_no_error (error);
      g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_RGB);

      g_object_unref (image);
      g_object_unref (profile);
    }

  g_strfreev (files);
}

static void
i1_rgb_to_cmyk_lcms (void)
{
  GimpColorProfile *profile;
  GimpColorProfile *srgb;
  GimpImage        *image;
  GimpLayer        *layer;
  guint16           expected[N_RGB_PATCHES * 4];
  GBytes           *result;
  GError           *error = NULL;


  profile = builtin_cmyk ();
  srgb    = gimp_color_profile_new_rgb_srgb ();

  image = new_rgb_image (GIMP_PRECISION_U16_NON_LINEAR, FALSE, &layer);

  g_assert_true (gimp_image_convert_type_full (image, GIMP_CMYK, profile,
                                               GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                               TRUE, NULL, &error));
  g_assert_no_error (error);

  lcms_transform (srgb, TYPE_RGB_16, rgb_patches,
                  profile, TYPE_CMYK_16_NO_ALPHA, expected,
                  N_RGB_PATCHES, INTENT_RELATIVE_COLORIMETRIC, TRUE);

  result = read_bytes (GIMP_DRAWABLE (layer), NULL,
                       babl_format_with_space ("CMYK u16",
                                               gimp_image_get_layer_space (image)));

  assert_u16_close (g_bytes_get_data (result, NULL), expected,
                    N_RGB_PATCHES * 4, 2);

  g_bytes_unref (result);
  g_object_unref (image);

  /*  with a real profile, PERCEPTUAL differs from RELCOL  */
  {
    const gchar *env = g_getenv ("GIMP_TESTING_CMYK_ICC");
    gchar      **files;
    gint         i;
    gboolean     tested = FALSE;

    files = g_strsplit (env ? env : "", ":", -1);

    for (i = 0; files[i]; i++)
      {
        GimpColorProfile *fogra;
        GFile            *file;
        GBytes           *relcol;
        GBytes           *perceptual;

        if (! strstr (files[i], "FOGRA39"))
          continue;

        file  = g_file_new_for_path (files[i]);
        fogra = gimp_color_profile_new_from_file (file, NULL);
        g_object_unref (file);

        if (! fogra)
          continue;

        image = new_rgb_image (GIMP_PRECISION_U16_NON_LINEAR, FALSE, &layer);
        g_assert_true (gimp_image_convert_type_full (image, GIMP_CMYK, fogra,
                                                     GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                                     TRUE, NULL, NULL));
        relcol = read_bytes (GIMP_DRAWABLE (layer), NULL,
                             gimp_drawable_get_format (GIMP_DRAWABLE (layer)));
        g_object_unref (image);

        image = new_rgb_image (GIMP_PRECISION_U16_NON_LINEAR, FALSE, &layer);
        g_assert_true (gimp_image_convert_type_full (image, GIMP_CMYK, fogra,
                                                     GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                                     TRUE, NULL, NULL));
        perceptual = read_bytes (GIMP_DRAWABLE (layer), NULL,
                                 gimp_drawable_get_format (GIMP_DRAWABLE (layer)));
        g_object_unref (image);

        g_assert_false (g_bytes_equal (relcol, perceptual));

        g_bytes_unref (relcol);
        g_bytes_unref (perceptual);
        g_object_unref (fogra);

        tested = TRUE;
      }

    g_strfreev (files);

    if (! tested)
      g_test_message ("I1: FOGRA39 not in GIMP_TESTING_CMYK_ICC, "
                      "intent difference not tested");
  }

  g_object_unref (srgb);
  g_object_unref (profile);
}


/*  I2  */

typedef struct
{
  GimpImageBaseType  base_type;
  GimpPrecision      precision;
  GBytes            *icc;
  GList             *formats;   /* const Babl *, per drawable  */
  GList             *pixels;    /* GBytes *, per drawable      */
  GList             *modes;     /* GimpLayerMode, per layer    */
} Snapshot;

static GList *
snapshot_drawables (GimpImage *image)
{
  GList *drawables = NULL;
  GList *layers    = gimp_image_get_layer_list (image);
  GList *list;

  for (list = layers; list; list = g_list_next (list))
    {
      if (! gimp_viewable_get_children (list->data))
        drawables = g_list_append (drawables, list->data);

      if (gimp_layer_get_mask (list->data))
        drawables = g_list_append (drawables, gimp_layer_get_mask (list->data));
    }

  g_list_free (layers);

  layers = gimp_image_get_channel_list (image);
  drawables = g_list_concat (drawables, layers);

  return drawables;
}

static Snapshot *
snapshot_new (GimpImage *image)
{
  Snapshot     *snap = g_new0 (Snapshot, 1);
  GList        *drawables;
  GList        *layers;
  GList        *list;
  const guint8 *icc;
  gsize         length;

  snap->base_type = gimp_image_get_base_type (image);
  snap->precision = gimp_image_get_precision (image);

  icc = gimp_image_get_icc_profile (image, &length);
  snap->icc = icc ? g_bytes_new (icc, length) : NULL;

  drawables = snapshot_drawables (image);

  for (list = drawables; list; list = g_list_next (list))
    {
      const Babl *format = gimp_drawable_get_format (list->data);

      snap->formats = g_list_append (snap->formats, (gpointer) format);
      snap->pixels  = g_list_append (snap->pixels,
                                     read_bytes (list->data, NULL, format));
    }

  g_list_free (drawables);

  layers = gimp_image_get_layer_list (image);

  for (list = layers; list; list = g_list_next (list))
    snap->modes = g_list_append (snap->modes,
                                 GINT_TO_POINTER (gimp_layer_get_mode (list->data)));

  g_list_free (layers);

  return snap;
}

static void
snapshot_free (Snapshot *snap)
{
  g_clear_pointer (&snap->icc, g_bytes_unref);
  g_list_free (snap->formats);
  g_list_free_full (snap->pixels, (GDestroyNotify) g_bytes_unref);
  g_list_free (snap->modes);
  g_free (snap);
}

static void
snapshot_assert (Snapshot  *snap,
                 GimpImage *image)
{
  Snapshot *now = snapshot_new (image);
  GList    *a;
  GList    *b;

  g_assert_cmpint (now->base_type, ==, snap->base_type);
  g_assert_cmpint (now->precision, ==, snap->precision);

  g_assert_true ((now->icc == NULL) == (snap->icc == NULL));
  if (now->icc)
    assert_bytes_equal (now->icc, snap->icc);

  g_assert_cmpint (g_list_length (now->formats), ==, g_list_length (snap->formats));

  for (a = now->formats, b = snap->formats; a; a = a->next, b = b->next)
    g_assert_true (a->data == b->data);

  for (a = now->pixels, b = snap->pixels; a; a = a->next, b = b->next)
    assert_bytes_equal (a->data, b->data);

  g_assert_cmpint (g_list_length (now->modes), ==, g_list_length (snap->modes));

  for (a = now->modes, b = snap->modes; a; a = a->next, b = b->next)
    g_assert_cmpint (GPOINTER_TO_INT (a->data), ==, GPOINTER_TO_INT (b->data));

  snapshot_free (now);
}

static GimpImage *
new_rgb_complex_image (gboolean    with_group,
                       GimpLayer **legacy_layer)
{
  GimpImage     *image;
  GimpLayer     *bg;
  GimpLayer     *group;
  GimpLayer     *child;
  GimpLayer     *legacy;
  GimpLayerMask *mask;
  GimpChannel   *channel;
  GeglBuffer    *buffer;
  GRand         *rand = g_rand_new_with_seed (42);
  guint16       *data;
  gint           i;

  image = gimp_image_new (gimp, W, H, GIMP_RGB, GIMP_PRECISION_U16_NON_LINEAR);
  gimp_image_undo_disable (image);

  data = g_new (guint16, W * H * 4);
  for (i = 0; i < W * H * 4; i++)
    data[i] = g_rand_int_range (rand, 0, 65536);

  bg = gimp_layer_new (image, W, H, gimp_image_get_layer_format (image, FALSE),
                       "bg", GIMP_OPACITY_OPAQUE, GIMP_LAYER_MODE_NORMAL);
  gimp_image_add_layer (image, bg, NULL, 0, FALSE);
  gegl_buffer_set (gimp_drawable_get_buffer (GIMP_DRAWABLE (bg)), NULL, 0,
                   babl_format ("R'G'B' u16"), data, GEGL_AUTO_ROWSTRIDE);

  if (with_group)
    {
      group = gimp_group_layer_new (image);
      gimp_image_add_layer (image, group, NULL, 0, FALSE);
    }
  else
    {
      group = NULL;
    }

  child = gimp_layer_new (image, W, H, gimp_image_get_layer_format (image, TRUE),
                          "child", 0.5, GIMP_LAYER_MODE_NORMAL);
  gimp_image_add_layer (image, child, group, 0, FALSE);
  gegl_buffer_set (gimp_drawable_get_buffer (GIMP_DRAWABLE (child)), NULL, 0,
                   babl_format ("R'G'B'A u16"), data, GEGL_AUTO_ROWSTRIDE);

  mask = gimp_layer_create_mask (child, GIMP_ADD_MASK_WHITE, NULL);
  gimp_layer_add_mask (child, mask, FALSE, FALSE, NULL);
  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (mask));
  gegl_buffer_set (buffer, GEGL_RECTANGLE (0, 0, W / 2, H), 0,
                   babl_format ("Y u16"), data, GEGL_AUTO_ROWSTRIDE);

  legacy = gimp_layer_new (image, W, H, gimp_image_get_layer_format (image, TRUE),
                           "legacy", GIMP_OPACITY_OPAQUE,
                           GIMP_LAYER_MODE_MULTIPLY_LEGACY);
  gimp_image_add_layer (image, legacy, NULL, 0, FALSE);
  gegl_buffer_set (gimp_drawable_get_buffer (GIMP_DRAWABLE (legacy)),
                   GEGL_RECTANGLE (0, 0, W, H / 2), 0,
                   babl_format ("R'G'B'A u16"), data, GEGL_AUTO_ROWSTRIDE);

  channel = gimp_channel_new (image, W, H, "channel", NULL);
  gimp_image_add_channel (image, channel, NULL, 0, FALSE);
  gegl_buffer_set (gimp_drawable_get_buffer (GIMP_DRAWABLE (channel)), NULL, 0,
                   babl_format ("Y u16"), data, GEGL_AUTO_ROWSTRIDE);

  g_free (data);
  g_rand_free (rand);

  gimp_image_undo_enable (image);
  gimp_image_clean_all (image);

  if (legacy_layer)
    *legacy_layer = legacy;

  return image;
}

static void
i2_undo_redo_full (gboolean with_group)
{
  GimpColorProfile *profile = builtin_cmyk ();
  GimpImage        *image;
  GimpLayer        *legacy;
  Snapshot         *rgb;
  Snapshot         *cmyk;
  GError           *error = NULL;
  gint              i;

  image = new_rgb_complex_image (with_group, &legacy);
  rgb   = snapshot_new (image);

  g_assert_cmpint (gimp_layer_get_mode (legacy), ==, GIMP_LAYER_MODE_MULTIPLY_LEGACY);

  g_assert_true (gimp_image_convert_type_full (image, GIMP_CMYK, profile,
                                               GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                               TRUE, NULL, &error));
  g_assert_no_error (error);
  assert_cmyk_image (image, profile);

  /*  I9: legacy mode mapped  */
  g_assert_cmpint (gimp_layer_get_mode (legacy), ==, GIMP_LAYER_MODE_MULTIPLY);

  cmyk = snapshot_new (image);

  for (i = 0; i < 3; i++)
    {
      g_assert_true (gimp_image_undo (image));
      snapshot_assert (rgb, image);
      g_assert_cmpint (gimp_layer_get_mode (legacy), ==,
                       GIMP_LAYER_MODE_MULTIPLY_LEGACY);
      g_assert_null (gimp_image_get_layer_space (image));

      g_assert_true (gimp_image_redo (image));
      snapshot_assert (cmyk, image);
      assert_cmyk_image (image, profile);
    }

  /*  and back to RGB, with undo  */
  g_assert_true (gimp_image_convert_type_full (image, GIMP_RGB, NULL,
                                               GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                               TRUE, NULL, &error));
  g_assert_no_error (error);
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_RGB);
  g_assert_null (gimp_image_get_color_profile (image)); /* sRGB: no parasite */

  g_assert_true (gimp_image_undo (image));
  snapshot_assert (cmyk, image);

  snapshot_free (rgb);
  snapshot_free (cmyk);
  g_object_unref (image);
  g_object_unref (profile);
}


static void
i2_undo_redo (void)
{
  i2_undo_redo_full (FALSE);
}

static void
i2_undo_redo_group (void)
{
  /*  group layer conversion is gimp_group_layer_convert_type() (WS02)  */

  i2_undo_redo_full (TRUE);
}


/*  I3  */

static void
i3_cmyk_to_rgb_gray_structure (void)
{
  GimpImage *image;
  GimpLayer *layer;
  GError    *error = NULL;

  image = new_cmyk_image (GIMP_PRECISION_U16_NON_LINEAR, NULL, &layer);

  g_assert_true (gimp_image_convert_type (image, GIMP_RGB, NULL, NULL, &error));
  g_assert_no_error (error);
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_RGB);
  g_assert_cmpint (gimp_drawable_get_base_type (GIMP_DRAWABLE (layer)), ==, GIMP_RGB);
  g_assert_null (gimp_image_get_layer_space (image));

  g_assert_true (gimp_image_undo (image));
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_CMYK);
  g_assert_true (gimp_drawable_is_cmyk (GIMP_DRAWABLE (layer)));

  g_assert_true (gimp_image_convert_type (image, GIMP_GRAY, NULL, NULL, &error));
  g_assert_no_error (error);
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_GRAY);
  g_assert_true (gimp_drawable_is_gray (GIMP_DRAWABLE (layer)));

  /*  CMYK targets other than CMYK profiles are refused  */
  g_assert_true (gimp_image_undo (image));
  {
    GimpColorProfile *cmyk = builtin_cmyk ();

    g_assert_false (gimp_image_convert_type (image, GIMP_RGB, cmyk, NULL, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_CMYK);

    g_object_unref (cmyk);
  }

  g_object_unref (image);
}

static void
i3_cmyk_to_rgb_gray_lcms (void)
{
  GimpColorProfile *cmyk;
  GimpColorProfile *srgb;
  GimpColorProfile *gray;
  GimpImage        *image;
  GimpLayer        *layer;
  GBytes           *src;
  GBytes           *result;
  guint16          *expected;
  gint              n = W * H;


  cmyk = builtin_cmyk ();
  srgb = gimp_babl_get_builtin_color_profile (GIMP_RGB,  GIMP_TRC_NON_LINEAR);
  gray = gimp_babl_get_builtin_color_profile (GIMP_GRAY, GIMP_TRC_NON_LINEAR);

  image = new_cmyk_image (GIMP_PRECISION_U16_NON_LINEAR, cmyk, &layer);
  src   = read_bytes (GIMP_DRAWABLE (layer), NULL,
                      babl_format_with_space ("CMYK u16",
                                              gimp_image_get_layer_space (image)));

  expected = g_new (guint16, n * 3);

  /*  RGB  */
  g_assert_true (gimp_image_convert_type_full (image, GIMP_RGB, NULL,
                                               GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                               TRUE, NULL, NULL));
  lcms_transform (cmyk, TYPE_CMYK_16_NO_ALPHA, g_bytes_get_data (src, NULL),
                  srgb, TYPE_RGB_16, expected, n,
                  INTENT_RELATIVE_COLORIMETRIC, TRUE);
  result = read_bytes (GIMP_DRAWABLE (layer), NULL, babl_format ("R'G'B' u16"));
  assert_u16_close (g_bytes_get_data (result, NULL), expected, n * 3, 257);
  g_bytes_unref (result);

  /*  GRAY  */
  g_assert_true (gimp_image_undo (image));
  g_assert_true (gimp_image_convert_type_full (image, GIMP_GRAY, NULL,
                                               GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                               TRUE, NULL, NULL));
  lcms_transform (cmyk, TYPE_CMYK_16_NO_ALPHA, g_bytes_get_data (src, NULL),
                  gray, TYPE_GRAY_16, expected, n,
                  INTENT_RELATIVE_COLORIMETRIC, TRUE);
  result = read_bytes (GIMP_DRAWABLE (layer), NULL, babl_format ("Y' u16"));
  assert_u16_close (g_bytes_get_data (result, NULL), expected, n, 257);
  g_bytes_unref (result);

  g_free (expected);
  g_bytes_unref (src);
  g_object_unref (image);
  g_object_unref (cmyk);
}


/*  I4  */

static void
i4_linear_precision (void)
{
  GimpImage *image;
  GimpLayer *layer;
  GError    *error = NULL;

  image = new_rgb_image (GIMP_PRECISION_FLOAT_LINEAR, TRUE, &layer);

  g_assert_true (gimp_image_convert_type (image, GIMP_CMYK, NULL, NULL, &error));
  g_assert_no_error (error);

  assert_cmyk_image (image, NULL);
  g_assert_cmpint (gimp_image_get_precision (image), ==,
                   GIMP_PRECISION_FLOAT_NON_LINEAR);
  g_assert_cmpint (gimp_drawable_get_precision (GIMP_DRAWABLE (layer)), ==,
                   GIMP_PRECISION_FLOAT_NON_LINEAR);

  /*  a single undo group  */
  g_assert_true (gimp_image_undo (image));
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_RGB);
  g_assert_cmpint (gimp_image_get_precision (image), ==,
                   GIMP_PRECISION_FLOAT_LINEAR);
  g_assert_cmpint (gimp_drawable_get_precision (GIMP_DRAWABLE (layer)), ==,
                   GIMP_PRECISION_FLOAT_LINEAR);
  assert_no_undo (image);

  g_object_unref (image);

  /*  U8 linear: channels follow the precision change  */
  image = new_rgb_image (GIMP_PRECISION_U8_LINEAR, FALSE, NULL);
  {
    GimpChannel *channel = gimp_channel_new (image, N_RGB_PATCHES, 1, "c", NULL);

    gimp_image_add_channel (image, channel, NULL, 0, FALSE);

    g_assert_true (gimp_image_convert_type (image, GIMP_CMYK, NULL, NULL, &error));
    g_assert_no_error (error);
    g_assert_cmpint (gimp_image_get_precision (image), ==,
                     GIMP_PRECISION_U8_NON_LINEAR);
    g_assert_true (gimp_drawable_get_format (GIMP_DRAWABLE (channel)) ==
                   gimp_image_get_channel_format (image));
  }
  g_object_unref (image);
}


/*  I5  */

static void
i5_indexed (void)
{
  GimpImage *image;
  GError    *error = NULL;

  image = new_rgb_image (GIMP_PRECISION_U8_NON_LINEAR, FALSE, NULL);

  g_assert_true (gimp_image_convert_indexed (image,
                                             GIMP_CONVERT_PALETTE_GENERATE,
                                             8, FALSE,
                                             GIMP_CONVERT_DITHER_NONE,
                                             FALSE, FALSE, NULL, NULL,
                                             &error));
  g_assert_no_error (error);
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_INDEXED);

  gimp_image_undo_free (image);
  gimp_image_clean_all (image);

  g_assert_true (gimp_image_convert_type (image, GIMP_CMYK, NULL, NULL, &error));
  g_assert_no_error (error);
  assert_cmyk_image (image, NULL);
  g_assert_null (gimp_image_get_colormap_palette (image));

  /*  one undo step back to indexed  */
  g_assert_true (gimp_image_undo (image));
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_INDEXED);
  g_assert_nonnull (gimp_image_get_colormap_palette (image));
  assert_no_undo (image);

  g_assert_true (gimp_image_redo (image));
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_CMYK);

  /*  CMYK -> indexed is refused  */
  g_assert_false (gimp_image_convert_indexed (image,
                                              GIMP_CONVERT_PALETTE_GENERATE,
                                              8, FALSE,
                                              GIMP_CONVERT_DITHER_NONE,
                                              FALSE, FALSE, NULL, NULL,
                                              &error));
  g_assert_nonnull (error);
  g_assert_nonnull (strstr (error->message, "RGB first"));
  g_clear_error (&error);
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_CMYK);

  g_object_unref (image);
}


/*  I6  */

static void
i6_channel_from_component (void)
{
  GimpImage   *image;
  GimpLayer   *layer;
  GimpChannel *channel;
  GBytes      *expected;
  GBytes      *result;


  image = new_cmyk_image (GIMP_PRECISION_U16_NON_LINEAR, NULL, &layer);

  channel = gimp_channel_new_from_component (image, GIMP_CHANNEL_CYAN,
                                             "cyan", NULL);

  expected = read_bytes (GIMP_DRAWABLE (layer), NULL,
                         gimp_image_get_component_format (image,
                                                          GIMP_CHANNEL_CYAN));
  result   = read_bytes (GIMP_DRAWABLE (channel), NULL, babl_format ("Y u16"));

  assert_bytes_equal (expected, result);

  g_bytes_unref (expected);
  g_bytes_unref (result);
  g_object_unref (channel);
  g_object_unref (image);
}


/*  I7  */

static void
i7_precision_chain (void)
{
  GimpPrecision  knob = gimp_test_utils_cmyk_precision ();
  GimpPrecision  middle;
  GimpImage     *image;
  GimpLayer     *layer;
  guint8        *before;
  guint8        *after;
  const Babl    *format;
  gint           i;

  middle = (knob == GIMP_PRECISION_U8_NON_LINEAR ||
            knob == GIMP_PRECISION_U16_NON_LINEAR) ?
           GIMP_PRECISION_FLOAT_NON_LINEAR : knob;

  image  = new_cmyk_image (GIMP_PRECISION_U8_NON_LINEAR, NULL, &layer);
  format = babl_format_with_space ("CMYK u8", gimp_image_get_layer_space (image));
  before = gimp_test_utils_read_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                       NULL, format);

  gimp_image_convert_precision (image, middle,
                                GEGL_DITHER_NONE, GEGL_DITHER_NONE,
                                GEGL_DITHER_NONE, NULL);
  assert_cmyk_image (image, NULL);
  gimp_image_convert_precision (image, GIMP_PRECISION_U16_NON_LINEAR,
                                GEGL_DITHER_NONE, GEGL_DITHER_NONE,
                                GEGL_DITHER_NONE, NULL);
  assert_cmyk_image (image, NULL);
  gimp_image_convert_precision (image, GIMP_PRECISION_U8_NON_LINEAR,
                                GEGL_DITHER_NONE, GEGL_DITHER_NONE,
                                GEGL_DITHER_NONE, NULL);
  assert_cmyk_image (image, NULL);

  after = gimp_test_utils_read_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                      NULL, format);

  for (i = 0; i < W * H * 4; i++)
    g_assert_cmpint (ABS ((gint) before[i] - (gint) after[i]), <=, 1);

  g_free (before);
  g_free (after);
  g_object_unref (image);
}


/*  I8  */

static GFile *
save_profile (GimpColorProfile *profile,
              const gchar      *name)
{
  gchar  *path  = g_build_filename (g_get_tmp_dir (), name, NULL);
  GFile  *file  = g_file_new_for_path (path);
  GError *error = NULL;

  g_assert_true (gimp_color_profile_save_to_file (profile, file, &error));
  g_assert_no_error (error);
  g_free (path);

  return file;
}

static void
i8_template (void)
{
  GimpColorProfile *builtin = builtin_cmyk ();
  GimpColorProfile *variant = gimp_test_utils_cmyk_header_variant (builtin);
  GimpColorProfile *srgb    = gimp_color_profile_new_rgb_srgb ();
  GFile            *cmyk_file;
  GFile            *rgb_file;
  GimpTemplate     *template;
  GimpImage        *image;
  const guint8     *data;
  const guint8     *variant_data;
  gsize             length;
  gsize             variant_length;

  cmyk_file = save_profile (variant, "gimp-test-cmyk-variant.icc");
  rgb_file  = save_profile (srgb,    "gimp-test-srgb.icc");

  template = g_object_new (GIMP_TYPE_TEMPLATE,
                           "name",          "cmyk",
                           "width",         8,
                           "height",        8,
                           "image-type",    GIMP_CMYK,
                           "precision",     GIMP_PRECISION_U16_LINEAR,
                           "color-profile", cmyk_file,
                           NULL);

  /*  I2: the template forces a non-linear precision  */
  g_assert_cmpint (gimp_template_get_precision (template), ==,
                   GIMP_PRECISION_U16_NON_LINEAR);

  image = gimp_image_new_from_template (gimp, template, context ());
  assert_cmyk_image (image, variant);

  /*  exactly the template's profile, not just an equal one  */
  data         = gimp_image_get_icc_profile (image, &length);
  variant_data = gimp_color_profile_get_icc_profile (variant, &variant_length);
  g_assert_cmpuint (length, ==, variant_length);
  g_assert_true (memcmp (data, variant_data, length) == 0);
  g_assert_cmpint (gimp_image_get_precision (image), ==,
                   GIMP_PRECISION_U16_NON_LINEAR);
  g_assert_false (gimp_image_is_dirty (image));

  gimp_container_remove (gimp->images, GIMP_OBJECT (image));
  g_object_unref (image);

  /*  an RGB profile: default CMYK profile + warning  */
  g_object_set (template, "color-profile", rgb_file, NULL);

  image = gimp_image_new_from_template (gimp, template, context ());
  assert_cmyk_image (image, builtin);

  gimp_container_remove (gimp->images, GIMP_OBJECT (image));
  g_object_unref (image);

  g_file_delete (cmyk_file, NULL, NULL);
  g_file_delete (rgb_file,  NULL, NULL);
  g_object_unref (cmyk_file);
  g_object_unref (rgb_file);
  g_object_unref (template);
  g_object_unref (srgb);
  g_object_unref (variant);
  g_object_unref (builtin);
}


/*  I9 / I10  */

#define SEL_X 1
#define SEL_Y 2
#define SEL_W (W - 3)
#define SEL_H (H - 3)

static const GeglRectangle sel_rect = { SEL_X, SEL_Y, SEL_W, SEL_H };

static void
select_rect (GimpImage *image)
{
  gimp_channel_select_rectangle (GIMP_CHANNEL (gimp_image_get_mask (image)),
                                 SEL_X, SEL_Y, SEL_W, SEL_H,
                                 GIMP_CHANNEL_OP_REPLACE,
                                 FALSE, 0.0, 0.0, FALSE);
}

/*  @src_rect of @src is bit-identical to @dest_rect of @dest, compared
 *  in @src's image layer format with alpha
 */
static void
assert_same_inks (GimpDrawable        *src,
                  const GeglRectangle *src_rect,
                  GimpDrawable        *dest,
                  const GeglRectangle *dest_rect)
{
  GimpImage  *src_image  = gimp_item_get_image (GIMP_ITEM (src));
  GimpImage  *dest_image = gimp_item_get_image (GIMP_ITEM (dest));
  const Babl *format     = gimp_image_get_layer_format (src_image, TRUE);
  GBytes     *a;
  GBytes     *b;

  /*  I8: equal profiles share one babl space  */
  g_assert_true (gimp_image_get_layer_space (src_image) ==
                 gimp_image_get_layer_space (dest_image));
  g_assert_true (gimp_drawable_is_cmyk (dest));
  g_assert_true (babl_format_get_space (gimp_drawable_get_format (dest)) ==
                 gimp_image_get_layer_space (dest_image));

  a = read_bytes (src,  src_rect,  format);
  b = read_bytes (dest, dest_rect, format);

  assert_bytes_equal (a, b);

  g_bytes_unref (a);
  g_bytes_unref (b);
}

static GimpLayer *
paste_one (GimpImage     *image,
           GimpDrawable  *target,
           GimpObject    *paste,
           GimpPasteType  type)
{
  GList     *targets = target ? single (target) : NULL;
  GList     *pasted;
  GimpLayer *layer;

  pasted = gimp_edit_paste (image, targets, paste, type, context (), FALSE,
                            0, 0, W, H);
  g_list_free (targets);

  g_assert_nonnull (pasted);
  g_assert_cmpint (g_list_length (pasted), ==, 1);

  layer = pasted->data;
  g_list_free (pasted);

  return layer;
}

static GimpBuffer *
buffer_from_layer (GimpLayer *layer)
{
  GimpImage  *image  = gimp_item_get_image (GIMP_ITEM (layer));
  GeglBuffer *src    = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));
  GeglBuffer *buffer;
  GimpBuffer *gimp_buffer;

  /*  like gimp_selection_extract(): a buffer at the origin in the
   *  drawable's format, the offset in the GimpBuffer
   */
  buffer = gegl_buffer_new (GEGL_RECTANGLE (0, 0, SEL_W, SEL_H),
                            gegl_buffer_get_format (src));
  gegl_buffer_copy (src, &sel_rect, GEGL_ABYSS_NONE,
                    buffer, GEGL_RECTANGLE (0, 0, 0, 0));

  gimp_buffer = gimp_buffer_new (buffer, "test", SEL_X, SEL_Y, FALSE);
  gimp_buffer_set_color_profile (gimp_buffer, gimp_image_get_color_profile (image));
  g_object_unref (buffer);

  return gimp_buffer;
}

static GimpObject *
copy_selection (GimpImage *image,
                GimpLayer *layer)
{
  GList      *drawables = single (layer);
  GimpObject *copy;
  GError     *error     = NULL;

  select_rect (image);

  copy = gimp_edit_copy (image, drawables, context (), FALSE, &error);
  g_assert_no_error (error);
  g_assert_nonnull (copy);
  g_list_free (drawables);

  return g_object_ref (copy);
}

static void
i9_same_image_paste_buffer (void)
{
  GimpImage    *image;
  GimpLayer    *layer;
  GimpLayer    *pasted;
  GimpBuffer   *buffer;
  GimpImage    *new_image;
  GeglRectangle full = { 0, 0, SEL_W, SEL_H };
  gint          off_x, off_y;

  image  = new_cmyk_image (gimp_test_utils_cmyk_precision (), NULL, &layer);
  buffer = buffer_from_layer (layer);

  /*  the buffer path (global buffer, gimp_layer_new_from_buffer())  */
  g_test_message ("I9: buffer, new layer in place");
  pasted = paste_one (image, NULL, GIMP_OBJECT (buffer),
                      GIMP_PASTE_TYPE_NEW_LAYER_IN_PLACE);
  gimp_item_get_offset (GIMP_ITEM (pasted), &off_x, &off_y);
  g_assert_cmpint (off_x, ==, SEL_X);
  g_assert_cmpint (off_y, ==, SEL_Y);
  assert_same_inks (GIMP_DRAWABLE (layer), &sel_rect, GIMP_DRAWABLE (pasted), &full);

  g_test_message ("I9: buffer, new layer");
  pasted = paste_one (image, NULL, GIMP_OBJECT (buffer),
                      GIMP_PASTE_TYPE_NEW_LAYER);
  assert_same_inks (GIMP_DRAWABLE (layer), &sel_rect, GIMP_DRAWABLE (pasted), &full);

  g_test_message ("I9: buffer, new image");
  new_image = gimp_edit_paste_as_new_image (gimp, GIMP_OBJECT (buffer), context ());
  assert_cmyk_image (new_image, gimp_image_get_color_profile (image));
  assert_same_inks (GIMP_DRAWABLE (layer), &sel_rect,
                    GIMP_DRAWABLE (gimp_image_get_layer_iter (new_image)->data), &full);
  gimp_container_remove (gimp->images, GIMP_OBJECT (new_image));
  g_object_unref (new_image);

  g_object_unref (buffer);
  g_object_unref (image);
}

static void
i9_same_image_paste_copy (void)
{
  GimpImage    *image;
  GimpLayer    *layer;
  GimpLayer    *pasted;
  GimpObject   *copy;
  GimpImage    *new_image;
  GeglRectangle full = { 0, 0, SEL_W, SEL_H };
  gint          off_x, off_y;

  /*  gimp_edit_copy() of a layer with a selection clears the
   *  unselected pixels of the clipboard image (compositing, WS02)
   */

  image = new_cmyk_image (gimp_test_utils_cmyk_precision (), NULL, &layer);
  copy  = copy_selection (image, layer);

  g_test_message ("I9: copy, new layer in place");
  pasted = paste_one (image, NULL, copy, GIMP_PASTE_TYPE_NEW_LAYER_IN_PLACE);
  gimp_item_get_offset (GIMP_ITEM (pasted), &off_x, &off_y);
  g_assert_cmpint (off_x, ==, SEL_X);
  g_assert_cmpint (off_y, ==, SEL_Y);
  g_assert_cmpint (gimp_item_get_width  (GIMP_ITEM (pasted)), ==, SEL_W);
  g_assert_cmpint (gimp_item_get_height (GIMP_ITEM (pasted)), ==, SEL_H);
  assert_same_inks (GIMP_DRAWABLE (layer), &sel_rect, GIMP_DRAWABLE (pasted), &full);

  g_test_message ("I9: copy, new layer");
  pasted = paste_one (image, NULL, copy, GIMP_PASTE_TYPE_NEW_LAYER);
  assert_same_inks (GIMP_DRAWABLE (layer), &sel_rect, GIMP_DRAWABLE (pasted), &full);

  g_test_message ("I9: copy, new image");
  new_image = gimp_edit_paste_as_new_image (gimp, copy, context ());
  g_assert_nonnull (new_image);
  assert_cmyk_image (new_image, gimp_image_get_color_profile (image));
  assert_same_inks (GIMP_DRAWABLE (layer), &sel_rect,
                    GIMP_DRAWABLE (gimp_image_get_layer_iter (new_image)->data), &full);
  gimp_container_remove (gimp->images, GIMP_OBJECT (new_image));
  g_object_unref (new_image);

  g_object_unref (copy);
  g_object_unref (image);
}

static void
i9_same_image_floating (void)
{
  GimpImage  *image;
  GimpLayer  *layer;
  GimpLayer  *target;
  GimpLayer  *floating;
  GimpObject *copy;


  image = new_cmyk_image (gimp_test_utils_cmyk_precision (), NULL, &layer);

  gimp_image_undo_disable (image);
  target = gimp_test_utils_add_cmyk_layer (image, "target", FALSE, patches[3]);
  gimp_image_undo_enable (image);

  copy = copy_selection (image, layer);

  floating = paste_one (image, GIMP_DRAWABLE (target), copy,
                        GIMP_PASTE_TYPE_FLOATING_IN_PLACE);
  g_assert_true (gimp_layer_is_floating_sel (floating));
  g_assert_true (gimp_drawable_get_format (GIMP_DRAWABLE (floating)) ==
                 gimp_drawable_get_format_with_alpha (GIMP_DRAWABLE (target)));

  floating_sel_anchor (floating);

  assert_same_inks (GIMP_DRAWABLE (layer), &sel_rect,
                    GIMP_DRAWABLE (target), &sel_rect);

  g_object_unref (copy);
  g_object_unref (image);
}

static GFile *
xcf_save (GimpImage *image)
{
  GimpPlugInProcedure *proc;
  gchar               *filename = NULL;
  gint                 handle;
  GFile               *file;
  GError              *error    = NULL;

  handle = g_file_open_tmp ("gimp-test-cmyk-XXXXXX.xcf", &filename, NULL);
  g_assert_true (handle != -1);
  close (handle);
  file = g_file_new_for_path (filename);
  g_free (filename);

  proc = gimp_plug_in_manager_file_procedure_find (gimp->plug_in_manager,
                                                   GIMP_FILE_PROCEDURE_GROUP_SAVE,
                                                   file, NULL);
  if (file_save (gimp, image, NULL, file, proc, GIMP_RUN_NONINTERACTIVE,
                 FALSE, FALSE, FALSE, &error) != GIMP_PDB_SUCCESS)
    {
      g_test_message ("XCF save failed: %s", error ? error->message : "");
      g_clear_error (&error);
      g_file_delete (file, NULL, NULL);
      g_clear_object (&file);
    }

  return file;
}

static void
i10_cross_image (void)
{
  GimpColorProfile *builtin   = builtin_cmyk ();
  GimpColorProfile *variant   = gimp_test_utils_cmyk_header_variant (builtin);
  GimpPrecision     precision = gimp_test_utils_cmyk_precision ();
  GimpImage        *a;
  GimpImage        *b;
  GimpLayer        *layer;
  GimpLayer        *converted;
  GimpLayer        *pasted;
  GimpObject       *copy;
  GimpBuffer       *buffer;
  GeglRectangle     full = { 0, 0, SEL_W, SEL_H };

  a = new_cmyk_image (precision, builtin, &layer);
  b = new_cmyk_image (precision, variant, NULL);

  /*  the profiles differ in their bytes, but are one color space (I8)  */
  {
    gsize         l1, l2;
    const guint8 *d1 = gimp_image_get_icc_profile (a, &l1);
    const guint8 *d2 = gimp_image_get_icc_profile (b, &l2);

    g_assert_cmpuint (l1, ==, l2);
    g_assert_false (memcmp (d1, d2, l1) == 0);
    g_assert_true (gimp_image_get_layer_space (a) == gimp_image_get_layer_space (b));
  }

  /*  layer DnD: gimp_item_convert()  */
  converted = GIMP_LAYER (gimp_item_convert (GIMP_ITEM (layer), b, GIMP_TYPE_LAYER));
  gimp_image_add_layer (b, converted, NULL, 0, FALSE);
  assert_same_inks (GIMP_DRAWABLE (layer), NULL, GIMP_DRAWABLE (converted), NULL);

  /*  paste of a copied selection (clears the clipboard image, WS02)  */
  copy = copy_selection (a, layer);
  pasted = paste_one (b, NULL, copy, GIMP_PASTE_TYPE_NEW_LAYER_IN_PLACE);
  assert_same_inks (GIMP_DRAWABLE (layer), &sel_rect, GIMP_DRAWABLE (pasted), &full);
  g_object_unref (copy);

  /*  paste of a buffer tagged with A's profile  */
  buffer = buffer_from_layer (layer);
  pasted = paste_one (b, NULL, GIMP_OBJECT (buffer),
                      GIMP_PASTE_TYPE_NEW_LAYER_IN_PLACE);
  assert_same_inks (GIMP_DRAWABLE (layer), &sel_rect, GIMP_DRAWABLE (pasted), &full);
  g_object_unref (buffer);

  g_object_unref (a);
  g_object_unref (b);
  g_object_unref (variant);
  g_object_unref (builtin);
}

static void
i10_open_as_layers (void)
{
  GimpColorProfile  *builtin = builtin_cmyk ();
  GimpColorProfile  *variant = gimp_test_utils_cmyk_header_variant (builtin);
  GimpImage         *a;
  GimpImage         *b;
  GimpLayer         *layer;
  GList             *layers = NULL;
  GFile             *file;
  GimpPDBStatusType  status = GIMP_PDB_EXECUTION_ERROR;
  GError            *error  = NULL;

  a = new_cmyk_image (gimp_test_utils_cmyk_precision (), builtin, &layer);
  b = new_cmyk_image (gimp_test_utils_cmyk_precision (), variant, NULL);

  file = xcf_save (a);

  if (file)
    layers = file_open_layers (gimp, context (), NULL, b, FALSE, FALSE, file,
                               GIMP_RUN_NONINTERACTIVE, NULL, &status, &error);

  g_assert_nonnull (file);
  g_assert_no_error (error);
  g_assert_cmpint (status, ==, GIMP_PDB_SUCCESS);
  g_assert_nonnull (layers);
  g_assert_true (gimp_drawable_is_cmyk (layers->data));
  g_assert_cmpint (g_list_length (layers), ==, 1);

  gimp_image_add_layer (b, layers->data, NULL, 0, FALSE);
  assert_same_inks (GIMP_DRAWABLE (layer), NULL, layers->data, NULL);

  g_list_free (layers);

  if (file)
    {
      g_file_delete (file, NULL, NULL);
      g_object_unref (file);
    }

  g_object_unref (a);
  g_object_unref (b);
  g_object_unref (variant);
  g_object_unref (builtin);
}


/*  I11  */

static void
i11_rgb_cmyk_paste (void)
{
  GimpColorProfile *cmyk = builtin_cmyk ();
  GimpColorProfile *srgb = gimp_color_profile_new_rgb_srgb ();
  GimpImage        *rgb_image;
  GimpImage        *cmyk_image;
  GimpLayer        *rgb_layer;
  GimpLayer        *cmyk_layer;
  GimpLayer        *converted;
  guint16           expected[N_RGB_PATCHES * 4];
  GBytes           *result;
  GBytes           *raw;

  rgb_image  = new_rgb_image (GIMP_PRECISION_U16_NON_LINEAR, FALSE, &rgb_layer);
  cmyk_image = new_cmyk_image (GIMP_PRECISION_U16_NON_LINEAR, cmyk, &cmyk_layer);

  /*  RGB -> CMYK image  */
  converted = GIMP_LAYER (gimp_item_convert (GIMP_ITEM (rgb_layer), cmyk_image,
                                             GIMP_TYPE_LAYER));
  g_assert_true (gimp_drawable_is_cmyk (GIMP_DRAWABLE (converted)));
  g_assert_true (babl_format_get_space (gimp_drawable_get_format (GIMP_DRAWABLE (converted))) ==
                 gimp_image_get_layer_space (cmyk_image));

  result = read_bytes (GIMP_DRAWABLE (converted), NULL,
                       babl_format_with_space ("CMYK u16",
                                               gimp_image_get_layer_space (cmyk_image)));
  /*  not a reinterpretation of the RGB bytes: white is paper  */
  {
    const guint16 *white = (const guint16 *) g_bytes_get_data (result, NULL) + 4;

    g_assert_cmpint (white[0] + white[1] + white[2] + white[3], <, 4 * 257);
  }

  lcms_transform (srgb, TYPE_RGB_16, rgb_patches,
                  cmyk, TYPE_CMYK_16_NO_ALPHA, expected,
                  N_RGB_PATCHES, INTENT_PERCEPTUAL, TRUE);
  assert_u16_close (g_bytes_get_data (result, NULL), expected,
                    N_RGB_PATCHES * 4, 2);
  /*  pasting an RGB buffer into the CMYK image takes the same path
   *  (gimp_layer_new_from_buffer(), PERCEPTUAL + BPC)
   */
  {
    GimpBuffer *buffer;
    GimpLayer  *pasted;
    GBytes     *pasted_bytes;

    buffer = gimp_buffer_new (gimp_drawable_get_buffer (GIMP_DRAWABLE (rgb_layer)),
                              "rgb", 0, 0, TRUE);
    gimp_buffer_set_color_profile (buffer, srgb);

    pasted = paste_one (cmyk_image, NULL, GIMP_OBJECT (buffer),
                        GIMP_PASTE_TYPE_NEW_LAYER_IN_PLACE);
    g_assert_true (gimp_drawable_is_cmyk (GIMP_DRAWABLE (pasted)));
    pasted_bytes = read_bytes (GIMP_DRAWABLE (pasted), NULL,
                               babl_format_with_space ("CMYK u16",
                                                       gimp_image_get_layer_space (cmyk_image)));
    assert_bytes_equal (result, pasted_bytes);
    g_bytes_unref (pasted_bytes);

    /*  as floating selection: the target's format with alpha (I3)  */
    pasted = paste_one (cmyk_image, GIMP_DRAWABLE (cmyk_layer), GIMP_OBJECT (buffer),
                        GIMP_PASTE_TYPE_FLOATING_IN_PLACE);
    g_assert_true (gimp_layer_is_floating_sel (pasted));
    g_assert_true (gimp_drawable_get_format (GIMP_DRAWABLE (pasted)) ==
                   gimp_drawable_get_format_with_alpha (GIMP_DRAWABLE (cmyk_layer)));
    pasted_bytes = read_bytes (GIMP_DRAWABLE (pasted), NULL,
                               babl_format_with_space ("CMYK u16",
                                                       gimp_image_get_layer_space (cmyk_image)));
    assert_bytes_equal (result, pasted_bytes);
    g_bytes_unref (pasted_bytes);

    gimp_image_remove_layer (cmyk_image, pasted, FALSE, NULL);

    g_object_unref (buffer);
  }

  g_bytes_unref (result);
  g_object_unref (converted);

  /*  CMYK -> RGB image  */
  raw = read_bytes (GIMP_DRAWABLE (cmyk_layer),
                    GEGL_RECTANGLE (0, 0, W, 1),
                    babl_format_with_space ("CMYK u16",
                                            gimp_image_get_layer_space (cmyk_image)));
  converted = GIMP_LAYER (gimp_item_convert (GIMP_ITEM (cmyk_layer), rgb_image,
                                             GIMP_TYPE_LAYER));
  g_assert_cmpint (gimp_drawable_get_base_type (GIMP_DRAWABLE (converted)), ==, GIMP_RGB);

  {
    guint16 *expected_rgb = g_new (guint16, W * 3);

    lcms_transform (cmyk, TYPE_CMYK_16_NO_ALPHA, g_bytes_get_data (raw, NULL),
                    srgb, TYPE_RGB_16, expected_rgb,
                    W, INTENT_PERCEPTUAL, TRUE);
    result = read_bytes (GIMP_DRAWABLE (converted), GEGL_RECTANGLE (0, 0, W, 1),
                         babl_format ("R'G'B' u16"));
    assert_u16_close (g_bytes_get_data (result, NULL), expected_rgb, W * 3, 2);
    g_bytes_unref (result);
    g_free (expected_rgb);
  }

  g_bytes_unref (raw);
  g_object_unref (converted);
  g_object_unref (rgb_image);
  g_object_unref (cmyk_image);
  g_object_unref (srgb);
  g_object_unref (cmyk);
}


/*  I12  */

static GBytes *
layer_bytes (GimpLayer *layer)
{
  return read_bytes (GIMP_DRAWABLE (layer), NULL,
                     gimp_drawable_get_format (GIMP_DRAWABLE (layer)));
}

static void
i12_geometry (void)
{
  GimpImage  *image;
  GimpLayer  *layer;
  GBytes     *orig;
  GBytes     *now;
  const Babl *format;
  gint        bpp;
  gint        x, y;

  image  = new_cmyk_image (GIMP_PRECISION_U16_NON_LINEAR, NULL, &layer);
  format = gimp_drawable_get_format (GIMP_DRAWABLE (layer));
  bpp    = babl_format_get_bytes_per_pixel (format);
  orig   = layer_bytes (layer);

  /*  rotate 90 (clockwise): new (H-1-y, x) = old (x, y)  */
  gimp_image_rotate (image, context (), GIMP_ROTATE_DEGREES90, NULL);
  g_assert_cmpint (gimp_item_get_width  (GIMP_ITEM (layer)), ==, H);
  g_assert_cmpint (gimp_item_get_height (GIMP_ITEM (layer)), ==, W);
  assert_cmyk_image (image, NULL);
  now = layer_bytes (layer);
  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++)
      {
        const guint8 *o = (const guint8 *) g_bytes_get_data (orig, NULL) + (y * W + x) * bpp;
        const guint8 *n = (const guint8 *) g_bytes_get_data (now,  NULL) + (x * H + (H - 1 - y)) * bpp;

        g_assert_true (memcmp (o, n, bpp) == 0);
      }
  g_bytes_unref (now);

  gimp_image_rotate (image, context (), GIMP_ROTATE_DEGREES270, NULL);
  now = layer_bytes (layer);
  assert_bytes_equal (orig, now);
  g_bytes_unref (now);

  /*  rotate 180 twice  */
  gimp_image_rotate (image, context (), GIMP_ROTATE_DEGREES180, NULL);
  gimp_image_rotate (image, context (), GIMP_ROTATE_DEGREES180, NULL);
  now = layer_bytes (layer);
  assert_bytes_equal (orig, now);
  g_bytes_unref (now);

  /*  flip horizontally: new (W-1-x, y) = old (x, y)  */
  gimp_image_flip (image, context (), GIMP_ORIENTATION_HORIZONTAL, NULL);
  now = layer_bytes (layer);
  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++)
      {
        const guint8 *o = (const guint8 *) g_bytes_get_data (orig, NULL) + (y * W + x) * bpp;
        const guint8 *n = (const guint8 *) g_bytes_get_data (now,  NULL) + (y * W + (W - 1 - x)) * bpp;

        g_assert_true (memcmp (o, n, bpp) == 0);
      }
  g_bytes_unref (now);
  gimp_image_flip (image, context (), GIMP_ORIENTATION_HORIZONTAL, NULL);
  gimp_image_flip (image, context (), GIMP_ORIENTATION_VERTICAL, NULL);
  gimp_image_flip (image, context (), GIMP_ORIENTATION_VERTICAL, NULL);
  now = layer_bytes (layer);
  assert_bytes_equal (orig, now);
  g_bytes_unref (now);

  /*  scale by 1  */
  gimp_image_scale (image, W, H, GIMP_INTERPOLATION_LINEAR, NULL);
  now = layer_bytes (layer);
  assert_bytes_equal (orig, now);
  g_bytes_unref (now);

  /*  canvas resize: layers keep their pixels  */
  gimp_image_resize (image, context (), W + 4, H + 2, 3, 1, NULL);
  now = layer_bytes (layer);
  assert_bytes_equal (orig, now);
  g_bytes_unref (now);

  /*  crop: the layer is at (3, 1) now; keep layer pixels (2..W-3, 1..H-3)  */
  gimp_image_crop (image, context (), GIMP_FILL_TRANSPARENT,
                   3 + 2, 1 + 1, W - 4, H - 3, TRUE);
  g_assert_cmpint (gimp_item_get_width  (GIMP_ITEM (layer)), ==, W - 4);
  g_assert_cmpint (gimp_item_get_height (GIMP_ITEM (layer)), ==, H - 3);
  now = layer_bytes (layer);
  for (y = 0; y < H - 3; y++)
    {
      const guint8 *o = (const guint8 *) g_bytes_get_data (orig, NULL) + ((y + 1) * W + 2) * bpp;
      const guint8 *n = (const guint8 *) g_bytes_get_data (now,  NULL) + (y * (W - 4)) * bpp;

      g_assert_true (memcmp (o, n, bpp * (W - 4)) == 0);
    }
  g_bytes_unref (now);

  assert_cmyk_image (image, NULL);

  g_bytes_unref (orig);
  g_object_unref (image);
}


/*  I13  */

static void
i13_legacy_modes (void)
{
  GimpImage *image;
  GimpLayer *layer;
  GimpLayer *group;
  GError    *error = NULL;

  image = new_rgb_image (GIMP_PRECISION_U16_NON_LINEAR, TRUE, &layer);

  gimp_image_undo_disable (image);
  gimp_layer_set_mode (layer, GIMP_LAYER_MODE_MULTIPLY_LEGACY, FALSE);

  /*  group layer conversion is gimp_group_layer_convert_type() (WS02)  */
  group = gimp_group_layer_new (image);
  gimp_image_add_layer (image, group, NULL, 0, FALSE);
  gimp_layer_set_mode (group, GIMP_LAYER_MODE_SCREEN_LEGACY, FALSE);

  gimp_image_undo_enable (image);

  g_assert_true (gimp_image_convert_type (image, GIMP_CMYK, NULL, NULL, &error));
  g_assert_no_error (error);
  g_assert_cmpint (gimp_layer_get_mode (layer), ==, GIMP_LAYER_MODE_MULTIPLY);
  g_assert_cmpint (gimp_layer_get_mode (group), ==, GIMP_LAYER_MODE_SCREEN);

  /*  setting a legacy mode on a layer of a CMYK image maps it  */
  gimp_layer_set_mode (layer, GIMP_LAYER_MODE_DIFFERENCE_LEGACY, TRUE);
  g_assert_cmpint (gimp_layer_get_mode (layer), ==, GIMP_LAYER_MODE_DIFFERENCE);
  g_assert_true (gimp_image_undo (image));
  g_assert_cmpint (gimp_layer_get_mode (layer), ==, GIMP_LAYER_MODE_MULTIPLY);

  /*  undo the conversion: legacy modes back, image RGB  */
  g_assert_true (gimp_image_undo (image));
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_RGB);
  g_assert_cmpint (gimp_layer_get_mode (layer), ==, GIMP_LAYER_MODE_MULTIPLY_LEGACY);
  g_assert_cmpint (gimp_layer_get_mode (group), ==, GIMP_LAYER_MODE_SCREEN_LEGACY);

  /*  an RGB layer with a legacy mode dropped into a CMYK image  */
  {
    GimpImage *cmyk = new_cmyk_image (GIMP_PRECISION_U16_NON_LINEAR, NULL, NULL);
    GimpLayer *moved;

    moved = GIMP_LAYER (gimp_item_convert (GIMP_ITEM (layer), cmyk, GIMP_TYPE_LAYER));
    g_assert_cmpint (gimp_layer_get_mode (moved), ==, GIMP_LAYER_MODE_MULTIPLY);

    g_object_unref (moved);
    g_object_unref (cmyk);
  }

  g_object_unref (image);
}


/*  I14  */

static void
i14_component_state (void)
{
  GimpImage *image;
  GError    *error = NULL;

  image = new_rgb_image (GIMP_PRECISION_U16_NON_LINEAR, TRUE, NULL);

  gimp_image_set_component_visible (image, GIMP_CHANNEL_ALPHA, FALSE);
  gimp_image_set_component_active  (image, GIMP_CHANNEL_RED,   FALSE);

  g_assert_true (gimp_image_convert_type (image, GIMP_CMYK, NULL, NULL, &error));
  g_assert_no_error (error);

  g_assert_true (gimp_image_get_component_visible (image, GIMP_CHANNEL_CYAN));
  g_assert_true (gimp_image_get_component_visible (image, GIMP_CHANNEL_MAGENTA));
  g_assert_true (gimp_image_get_component_visible (image, GIMP_CHANNEL_YELLOW));
  g_assert_true (gimp_image_get_component_visible (image, GIMP_CHANNEL_BLACK));
  g_assert_true (gimp_image_get_component_visible (image, GIMP_CHANNEL_ALPHA));
  g_assert_true (gimp_image_get_component_active  (image, GIMP_CHANNEL_CYAN));
  g_assert_true (gimp_image_get_component_active  (image, GIMP_CHANNEL_BLACK));

  g_assert_cmpint (gimp_image_get_visible_mask (image), ==, GIMP_COMPONENT_MASK_ALL);
  g_assert_cmpint (gimp_image_get_active_mask  (image), ==, GIMP_COMPONENT_MASK_ALL);

  g_object_unref (image);
}


/*  I15  */

static void
i15_previews (void)
{
  GimpImage    *image;
  GimpLayer    *layer;
  GimpLayer    *alpha_layer;
  GimpTempBuf  *preview;
  guint8       *expected;
  const guint8 *data;
  gint          i;

  image = new_cmyk_image (GIMP_PRECISION_U16_NON_LINEAR, NULL, &layer);

  gimp_image_undo_disable (image);
  alpha_layer = gimp_test_utils_add_cmyk_layer (image, "alpha", TRUE, patches[5]);
  gimp_image_undo_enable (image);

  g_assert_true (gimp_drawable_get_preview_format (GIMP_DRAWABLE (layer)) ==
                 babl_format ("R'G'B' u8"));
  g_assert_true (gimp_drawable_get_preview_format (GIMP_DRAWABLE (alpha_layer)) ==
                 babl_format ("R'G'B'A u8"));
  g_assert_true (gimp_image_get_preview_format (image) ==
                 babl_format ("R'G'B'A u8"));

  /*  full-size preview == babl CMYK -> sRGB  */
  preview = gimp_drawable_get_sub_preview (GIMP_DRAWABLE (layer),
                                           0, 0, W, H, W, H, 1);
  g_assert_nonnull (preview);
  g_assert_true (gimp_temp_buf_get_format (preview) == babl_format ("R'G'B' u8"));

  expected = gimp_test_utils_read_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                         NULL, babl_format ("R'G'B' u8"));
  data = gimp_temp_buf_get_data (preview);

  for (i = 0; i < W * H * 3; i++)
    g_assert_cmpint (ABS ((gint) data[i] - (gint) expected[i]), <=, 1);

  /*  K100 previews dark, paper white  */
  g_assert_cmpint (data[0], <, 64);
  g_assert_cmpint (data[(3 * (W / N_PATCHES)) * 3], >, 250);

  g_free (expected);
  gimp_temp_buf_unref (preview);
  g_object_unref (image);
}


int
main (int    argc,
      char **argv)
{
  gint result;

  g_test_init (&argc, &argv, NULL);

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_SRCDIR",
                                       "app/tests/gimpdir");

  gimp = gimp_init_for_testing ();

  g_test_add_func ("/cmyk-image/I1/rgb-to-cmyk",            i1_rgb_to_cmyk_structure);
  g_test_add_func ("/cmyk-image/I1/rgb-to-cmyk-profiles",   i1_rgb_to_cmyk_real_profiles);
  g_test_add_func ("/cmyk-image/I1/rgb-to-cmyk-lcms",       i1_rgb_to_cmyk_lcms);
  g_test_add_func ("/cmyk-image/I2/undo-redo",              i2_undo_redo);
  g_test_add_func ("/cmyk-image/I2/undo-redo-group",        i2_undo_redo_group);
  g_test_add_func ("/cmyk-image/I3/cmyk-to-rgb-gray",       i3_cmyk_to_rgb_gray_structure);
  g_test_add_func ("/cmyk-image/I3/cmyk-to-rgb-gray-lcms",  i3_cmyk_to_rgb_gray_lcms);
  g_test_add_func ("/cmyk-image/I4/linear-precision",       i4_linear_precision);
  g_test_add_func ("/cmyk-image/I5/indexed",                i5_indexed);
  g_test_add_func ("/cmyk-image/I6/channel-from-component", i6_channel_from_component);
  g_test_add_func ("/cmyk-image/I7/precision-chain",        i7_precision_chain);
  g_test_add_func ("/cmyk-image/I8/template",               i8_template);
  g_test_add_func ("/cmyk-image/I9/same-image-paste-buffer", i9_same_image_paste_buffer);
  g_test_add_func ("/cmyk-image/I9/same-image-paste-copy",  i9_same_image_paste_copy);
  g_test_add_func ("/cmyk-image/I9/same-image-floating",    i9_same_image_floating);
  g_test_add_func ("/cmyk-image/I10/cross-image",           i10_cross_image);
  g_test_add_func ("/cmyk-image/I10/open-as-layers",        i10_open_as_layers);
  g_test_add_func ("/cmyk-image/I11/rgb-cmyk-paste",        i11_rgb_cmyk_paste);
  g_test_add_func ("/cmyk-image/I12/geometry",              i12_geometry);
  g_test_add_func ("/cmyk-image/I13/legacy-modes",          i13_legacy_modes);
  g_test_add_func ("/cmyk-image/I14/component-state",       i14_component_state);
  g_test_add_func ("/cmyk-image/I15/previews",              i15_previews);

  result = g_test_run ();

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_BUILDDIR",
                                       "app/tests/gimpdir-output");

  gimp_exit (gimp, TRUE);

  return result;
}

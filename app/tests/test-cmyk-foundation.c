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

/* CMYK foundation tests F1-F12 (devel-docs/cmyk-mode/TESTING.md).
 * Owner: WS01a (F1-F8, F12), WS01b (F3 final, F9-F11).
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include <lcms2.h>

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"
#include "libgimpconfig/gimpconfig.h"

#include "widgets/widgets-types.h"

#include "widgets/gimpuimanager.h"

#include "config/gimpcoreconfig.h"

#include "gegl/gimp-babl.h"
#include "gegl/gimp-babl-compat.h"

#include "core/gimp.h"
#include "core/gimpdrawable.h"
#include "core/gimperror.h"
#include "core/gimpimage.h"
#include "core/gimpimage-color-profile.h"
#include "core/gimpimage-undo.h"
#include "core/gimplayer.h"
#include "core/gimpprojectable.h"
#include "core/gimpundostack.h"

#include "tests.h"

#include "gimp-app-test-utils.h"


#ifndef TYPE_CMYKA_16
#define TYPE_CMYKA_16 (COLORSPACE_SH(PT_CMYK)|EXTRA_SH(1)|CHANNELS_SH(4)|BYTES_SH(2))
#endif

/*  SHA-256 of the builtin CMYK profile bytes (final 9^4/17^3 profile,
 *  WS01b; the 01a stub was f900f7ab...16263).
 */
#define BUILTIN_CMYK_SHA256 \
  "864a65a0c8b1bcc4e2186fec9e9c81c3950f102564e805c6822e27e20bda9307"


static Gimp *gimp = NULL;


/*  helpers  */

/* a different, but valid and bidirectional, CMYK profile: the builtin
 * with one character of its copyright text changed
 */
static GimpColorProfile *
other_cmyk_profile (void)
{
  GimpColorProfile *builtin = gimp_color_profile_new_cmyk_builtin ();
  GimpColorProfile *other;
  const guint8     *data;
  gsize             length;
  guint8           *copy;
  gsize             i;
  gboolean          found = FALSE;

  data = gimp_color_profile_get_icc_profile (builtin, &length);
  copy = g_memdup2 (data, length);

  /* "Public Domain" as UTF-16BE in the mluc tag: change the 'P' */
  for (i = 128; i + 4 < length; i++)
    {
      if (copy[i] == 0 && copy[i + 1] == 'P' &&
          copy[i + 2] == 0 && copy[i + 3] == 'u')
        {
          copy[i + 1] = 'Q';
          found = TRUE;
          break;
        }
    }

  g_assert_true (found);

  other = gimp_color_profile_new_from_icc_profile (copy, length, NULL);
  g_assert_nonnull (other);
  g_assert_true (gimp_color_profile_is_cmyk_bidirectional (other));
  g_assert_false (gimp_color_profile_is_equal (other, builtin));

  g_free (copy);
  g_object_unref (builtin);

  return other;
}

static void
lcms_cmyk_to_srgb (GimpColorProfile *profile,
                   const guint16     cmyk[4],
                   gdouble           rgb[3])
{
  cmsHPROFILE   srgb = cmsCreate_sRGBProfile ();
  cmsHTRANSFORM transform;

  transform = cmsCreateTransform (gimp_color_profile_get_lcms_profile (profile),
                                  TYPE_CMYK_16,
                                  srgb, TYPE_RGB_DBL,
                                  INTENT_RELATIVE_COLORIMETRIC, 0);
  g_assert_nonnull (transform);

  cmsDoTransform (transform, cmyk, rgb, 1);

  cmsDeleteTransform (transform);
  cmsCloseProfile (srgb);
}


/*  F1: formats  */

static void
f1_formats (void)
{
  static const struct
  {
    GimpPrecision  precision;
    const gchar   *type;
  }
  types[] =
  {
    { GIMP_PRECISION_U8_NON_LINEAR,     "u8"     },
    { GIMP_PRECISION_U16_NON_LINEAR,    "u16"    },
    { GIMP_PRECISION_U32_NON_LINEAR,    "u32"    },
    { GIMP_PRECISION_HALF_NON_LINEAR,   "half"   },
    { GIMP_PRECISION_FLOAT_NON_LINEAR,  "float"  },
    { GIMP_PRECISION_DOUBLE_NON_LINEAR, "double" }
  };

  GimpColorProfile *builtin;
  const Babl       *space;
  gint              i;

  builtin = gimp_babl_get_builtin_color_profile (GIMP_CMYK,
                                                 GIMP_TRC_NON_LINEAR);
  space   = gimp_color_profile_get_space (builtin,
                                          GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                          NULL);
  g_assert_true (gimp_babl_space_is_cmyk (space));
  g_assert_false (gimp_babl_space_is_cmyk (NULL));
  g_assert_false (gimp_babl_format_is_cmyk (NULL));

  for (i = 0; i < G_N_ELEMENTS (types); i++)
    {
      gint alpha;

      for (alpha = 0; alpha < 2; alpha++)
        {
          const Babl *format;
          gchar      *name;
          gint        c;

          format = gimp_babl_format (GIMP_CMYK, types[i].precision, alpha,
                                     space);
          name   = g_strdup_printf ("%s %s", alpha ? "CMYKA" : "CMYK",
                                    types[i].type);

          g_assert_nonnull (format);
          g_assert_cmpstr (babl_format_get_encoding (format), ==, name);
          g_assert_true (babl_format_get_space (format) == space);
          g_assert_cmpint (babl_format_get_n_components (format), ==,
                           alpha ? 5 : 4);
          g_assert_cmpint (babl_format_has_alpha (format), ==, alpha);
          g_assert_true (gimp_babl_format_is_cmyk (format));
          g_assert_cmpint (gimp_babl_format_get_base_type (format), ==,
                           GIMP_CMYK);
          g_assert_cmpint (gimp_babl_format_get_trc (format), ==,
                           GIMP_TRC_NON_LINEAR);
          g_assert_cmpint (gimp_babl_format_get_precision (format), ==,
                           types[i].precision);
          g_assert_cmpint (gimp_babl_format_get_image_type (format), ==,
                           alpha ? GIMP_CMYKA_IMAGE : GIMP_CMYK_IMAGE);
          g_assert_cmpstr (gimp_babl_format_get_description (format), ==,
                           alpha ? "CMYK-alpha" : "CMYK");

          /* the TRC of the precision is ignored */
          g_assert_true (gimp_babl_format (GIMP_CMYK,
                                           gimp_babl_precision (gimp_babl_component_type (types[i].precision),
                                                                GIMP_TRC_LINEAR),
                                           alpha, space) == format);

          /* compat u8 */
          g_assert_true (gimp_babl_compat_u8_format (format) ==
                         gimp_babl_format (GIMP_CMYK,
                                           GIMP_PRECISION_U8_NON_LINEAR,
                                           alpha, space));

          for (c = 0; c < 5; c++)
            {
              const Babl *component;

              component = gimp_babl_component_format (GIMP_CMYK,
                                                      types[i].precision, c);
              g_assert_nonnull (component);
              g_assert_cmpint (babl_format_get_n_components (component), ==, 1);
            }

          g_free (name);
        }

      g_assert_true (gimp_babl_is_valid (GIMP_CMYK, types[i].precision));
      g_assert_false (gimp_babl_is_valid (GIMP_CMYK,
                                          gimp_babl_precision (gimp_babl_component_type (types[i].precision),
                                                               GIMP_TRC_LINEAR)));
      g_assert_false (gimp_babl_is_valid (GIMP_CMYK,
                                          gimp_babl_precision (gimp_babl_component_type (types[i].precision),
                                                               GIMP_TRC_PERCEPTUAL)));
    }

  /* other CMYK models */
  g_assert_cmpint (gimp_babl_format_get_base_type (babl_format ("cmykA u8")),
                   ==, GIMP_CMYK);
  g_assert_cmpint (gimp_babl_format_get_base_type (babl_format ("CaMaYaKaA float")),
                   ==, GIMP_CMYK);
  g_assert_true (babl_format_exists ("CaMaYaKaA u32"));
  g_assert_cmpint (gimp_babl_format_get_image_type (babl_format ("cmyk u16")),
                   ==, GIMP_CMYK_IMAGE);

  /* RGB unchanged */
  g_assert_false (gimp_babl_format_is_cmyk (babl_format ("R'G'B'A u8")));
  g_assert_cmpint (gimp_babl_format_get_base_type (babl_format ("R'G'B'A u8")),
                   ==, GIMP_RGB);

  /* I5: an RGB format tagged with a CMYK space is sRGB */
  {
    GimpColorProfile *profile;

    profile = gimp_babl_format_get_color_profile (babl_format_with_space ("R'G'B'A float",
                                                                          space));
    g_assert_true (gimp_color_profile_is_rgb (profile));
    g_assert_true (gimp_color_profile_is_equal (profile,
                                                gimp_babl_get_builtin_color_profile (GIMP_RGB,
                                                                                     GIMP_TRC_NON_LINEAR)));
    g_object_unref (profile);

    profile = gimp_babl_format_get_color_profile (gimp_babl_format (GIMP_CMYK,
                                                                    GIMP_PRECISION_U16_NON_LINEAR,
                                                                    TRUE, space));
    g_assert_true (gimp_color_profile_is_equal (profile, builtin));
    g_object_unref (profile);

    profile = gimp_babl_format_get_color_profile (babl_format ("CMYKA u16"));
    g_assert_true (gimp_color_profile_is_equal (profile, builtin));
    g_object_unref (profile);
  }
}


/*  F2: CMYK support  */

static void
f2_cmyk_support (void)
{
  g_assert_true (gimp_babl_has_cmyk_support ());
}


/*  F3: builtin profile  */

static void
f3_builtin_profile (void)
{
  static const guint8  date[12] = { 0x07, 0xea, 0x00, 0x01, 0x00, 0x01,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  GimpColorProfile    *profile1;
  GimpColorProfile    *profile2;
  const guint8        *data1;
  const guint8        *data2;
  gsize                length1;
  gsize                length2;
  gchar               *sha256;
  const guint16        k100[4]  = { 0, 0, 0, 65535 };
  const guint16        paper[4] = { 0, 0, 0, 0 };
  gdouble              rgb[3];
  gint                 i;

  profile1 = gimp_color_profile_new_cmyk_builtin ();
  profile2 = gimp_color_profile_new_cmyk_builtin ();

  g_assert_true (GIMP_IS_COLOR_PROFILE (profile1));
  g_assert_true (gimp_color_profile_is_cmyk (profile1));
  g_assert_true (gimp_color_profile_is_cmyk_bidirectional (profile1));
  g_assert_false (gimp_color_profile_is_rgb (profile1));

  data1 = gimp_color_profile_get_icc_profile (profile1, &length1);
  data2 = gimp_color_profile_get_icc_profile (profile2, &length2);

  g_assert_cmpuint (length1, ==, length2);
  g_assert_true (memcmp (data1, data2, length1) == 0);

  g_assert_cmpuint (length1, <, 100 * 1024);
  g_assert_true (memcmp (data1 + 24, date, sizeof (date)) == 0);

  for (i = 84; i < 100; i++)
    g_assert_cmpint (data1[i], ==, 0);

  sha256 = g_compute_checksum_for_data (G_CHECKSUM_SHA256, data1, length1);
  g_test_message ("builtin CMYK profile: %" G_GSIZE_FORMAT " bytes, SHA-256 %s",
                  length1, sha256);
  g_assert_cmpstr (sha256, ==, BUILTIN_CMYK_SHA256);
  g_free (sha256);

  g_assert_true (gimp_color_profile_is_equal (profile1,
                                              gimp_babl_get_builtin_color_profile (GIMP_CMYK,
                                                                                   GIMP_TRC_NON_LINEAR)));

  /* lcms relcol, no BPC */
  lcms_cmyk_to_srgb (profile1, k100, rgb);
  for (i = 0; i < 3; i++)
    {
      g_assert_cmpfloat (rgb[i], >=, 0.07);
      g_assert_cmpfloat (rgb[i], <=, 0.09);
    }

  lcms_cmyk_to_srgb (profile1, paper, rgb);
  for (i = 0; i < 3; i++)
    g_assert_cmpfloat_with_epsilon (rgb[i], 1.0, 1.0 / 255.0);

  /* babl K100 round trip (babl: relcol + BPC): K stays K */
  {
    const Babl  *space;
    const gfloat k100f[4] = { 0.0, 0.0, 0.0, 1.0 };
    gfloat       rgbf[3];
    gfloat       back[4];

    space = gimp_color_profile_get_space (profile1,
                                          GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                          NULL);
    g_assert_nonnull (space);

    babl_process (babl_fish (babl_format_with_space ("CMYK float",   space),
                             babl_format_with_space ("R'G'B' float", space)),
                  k100f, rgbf, 1);
    babl_process (babl_fish (babl_format_with_space ("R'G'B' float", space),
                             babl_format_with_space ("CMYK float",   space)),
                  rgbf, back, 1);

    g_test_message ("babl K100 round trip: %.4f %.4f %.4f %.4f",
                    back[0], back[1], back[2], back[3]);

    g_assert_cmpfloat (back[3], >=, 0.98);
    for (i = 0; i < 3; i++)
      g_assert_cmpfloat (back[i], <=, 0.05);
  }

  g_object_unref (profile1);
  g_object_unref (profile2);
}


/*  F4: input-only derivative  */

static void
f4_input_only (void)
{
  GimpColorProfile *input_only;
  GimpColorProfile *before;
  GimpImage        *image;
  GError           *error = NULL;

  input_only = gimp_test_utils_cmyk_input_only_profile ();

  g_assert_true (gimp_color_profile_is_cmyk (input_only));
  g_assert_false (gimp_color_profile_is_cmyk_bidirectional (input_only));

  image  = gimp_test_utils_create_cmyk_image (gimp, 16, 16,
                                              GIMP_PRECISION_U16_NON_LINEAR,
                                              NULL);
  before = g_object_ref (gimp_image_get_color_profile (image));

  g_assert_false (gimp_image_set_color_profile (image, input_only, &error));
  g_assert_error (error, GIMP_ERROR, GIMP_FAILED);
  g_clear_error (&error);

  g_assert_true (gimp_color_profile_is_equal (gimp_image_get_color_profile (image),
                                              before));

  /* an RGB profile is refused too */
  {
    GimpColorProfile *srgb = gimp_color_profile_new_rgb_srgb ();

    g_assert_false (gimp_image_set_color_profile (image, srgb, &error));
    g_assert_error (error, GIMP_ERROR, GIMP_FAILED);
    g_clear_error (&error);
    g_object_unref (srgb);
  }

  /* NULL means the default CMYK profile, never "no profile" (I1) */
  g_assert_true (gimp_image_set_color_profile (image, NULL, &error));
  g_assert_no_error (error);
  g_assert_nonnull (gimp_image_get_color_profile (image));
  g_assert_true (gimp_color_profile_is_cmyk (gimp_image_get_color_profile (image)));

  g_object_unref (before);
  g_object_unref (image);
  g_object_unref (input_only);
}


/*  F5: new CMYK image  */

static void
check_new_image (GimpPrecision  precision,
                 const gchar   *layer_format,
                 const gchar   *layer_format_no_alpha)
{
  GimpImage        *image;
  GimpColorProfile *profile;
  const Babl       *format;
  const Babl       *space;

  image = gimp_image_new (gimp, 64, 64, GIMP_CMYK, precision);
  g_assert_nonnull (image);

  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_CMYK);

  profile = gimp_image_get_color_profile (image);
  g_assert_nonnull (profile);
  g_assert_true (gimp_color_profile_is_cmyk (profile));
  g_assert_true (gimp_color_profile_is_cmyk_bidirectional (profile));

  profile = gimp_color_managed_get_color_profile (GIMP_COLOR_MANAGED (image));
  g_assert_true (gimp_color_profile_is_cmyk (profile));

  space = gimp_image_get_layer_space (image);
  g_assert_true (gimp_babl_space_is_cmyk (space));

  format = gimp_image_get_layer_format (image, TRUE);
  g_assert_cmpstr (babl_format_get_encoding (format), ==, layer_format);
  g_assert_true (babl_format_get_space (format) == space);

  format = gimp_image_get_layer_format (image, FALSE);
  g_assert_cmpstr (babl_format_get_encoding (format), ==, layer_format_no_alpha);

  format = gimp_projectable_get_format (GIMP_PROJECTABLE (image));
  g_assert_cmpstr (babl_format_get_encoding (format), ==, layer_format);
  g_assert_true (babl_format_get_space (format) == space);

  g_assert_true (gimp_color_profile_is_equal (gimp_image_get_builtin_color_profile (image),
                                              gimp_babl_get_builtin_color_profile (GIMP_CMYK,
                                                                                   GIMP_TRC_NON_LINEAR)));

  /* every new image starts with dirty == 1 (gimp_image_new_from_template()
   * cleans it); attaching the profile must not dirty it further
   */
  g_assert_cmpint (gimp_image_is_dirty (image), ==, 1);
  g_assert_cmpint (gimp_undo_stack_get_depth (gimp_image_get_undo_stack (image)),
                   ==, 0);

  /* a layer */
  {
    GimpLayer     *layer;
    const gdouble  k100[5] = { 0.0, 0.0, 0.0, 1.0, 1.0 };
    gdouble        pixel[MAX_CHANNELS];

    layer = gimp_test_utils_add_cmyk_layer (image, "k100", TRUE, k100);

    g_assert_true (gimp_drawable_is_cmyk (GIMP_DRAWABLE (layer)));
    g_assert_false (gimp_drawable_is_rgb (GIMP_DRAWABLE (layer)));
    g_assert_true (gimp_drawable_get_format (GIMP_DRAWABLE (layer)) ==
                   gimp_image_get_layer_format (image, TRUE));

    gimp_test_utils_read_cmyk_pixel (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                     3, 5, pixel);
    g_assert_cmpfloat (pixel[0], ==, 0.0);
    g_assert_cmpfloat (pixel[1], ==, 0.0);
    g_assert_cmpfloat (pixel[2], ==, 0.0);
    g_assert_cmpfloat (pixel[3], ==, 1.0);
    g_assert_cmpfloat (pixel[4], ==, 1.0);

    g_assert_cmpint (gimp_drawable_get_component_index (GIMP_DRAWABLE (layer),
                                                        GIMP_CHANNEL_BLACK),
                     ==, BLACK);
    g_assert_cmpint (gimp_drawable_get_component_index (GIMP_DRAWABLE (layer),
                                                        GIMP_CHANNEL_ALPHA),
                     ==, ALPHA_C);
    g_assert_true (babl_format_get_space (gimp_drawable_get_component_format (GIMP_DRAWABLE (layer),
                                                                              GIMP_CHANNEL_CYAN)) ==
                   space);
  }

  g_object_unref (image);
}

static void
f5_new_image (void)
{
  check_new_image (GIMP_PRECISION_U8_NON_LINEAR,  "CMYKA u8",  "CMYK u8");
  check_new_image (GIMP_PRECISION_U16_NON_LINEAR, "CMYKA u16", "CMYK u16");
  check_new_image (gimp_test_utils_cmyk_precision (),
                   babl_format_get_encoding (gimp_babl_format (GIMP_CMYK,
                                                               gimp_test_utils_cmyk_precision (),
                                                               TRUE, NULL)),
                   babl_format_get_encoding (gimp_babl_format (GIMP_CMYK,
                                                               gimp_test_utils_cmyk_precision (),
                                                               FALSE, NULL)));
}


/*  F6: default profile order  */

static void
f6_default_profile (void)
{
  GimpColorConfig  *config  = gimp->config->color_management;
  GimpColorProfile *builtin;
  GimpColorProfile *input_only;
  GimpColorProfile *profile;
  GimpColorProfile *other;
  gchar            *old_pref = NULL;
  gchar            *path;
  GFile            *file;
  GError           *error    = NULL;
  const gchar      *real;

  g_object_get (config, "cmyk-profile", &old_pref, NULL);

  builtin    = gimp_color_profile_new_cmyk_builtin ();
  input_only = gimp_test_utils_cmyk_input_only_profile ();
  other      = other_cmyk_profile ();

  /* no preference: builtin, no error */
  g_object_set (config, "cmyk-profile", NULL, NULL);
  profile = gimp_get_default_cmyk_profile (gimp, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (gimp_color_profile_is_equal (profile, builtin));
  g_object_unref (profile);

  /* explicit usable profile wins */
  profile = gimp_get_default_cmyk_profile (gimp, other, &error);
  g_assert_no_error (error);
  g_assert_true (profile == other);
  g_object_unref (profile);

  /* explicit input-only profile: skipped, error */
  profile = gimp_get_default_cmyk_profile (gimp, input_only, &error);
  g_assert_nonnull (error);
  g_clear_error (&error);
  g_assert_true (gimp_color_profile_is_equal (profile, builtin));
  g_object_unref (profile);

  /* preference = a usable profile file */
  path = g_build_filename (g_get_tmp_dir (), "gimp-test-cmyk-other.icc", NULL);
  file = g_file_new_for_path (path);
  g_assert_true (gimp_color_profile_save_to_file (other, file, NULL));
  g_object_set (config, "cmyk-profile", path, NULL);

  profile = gimp_get_default_cmyk_profile (gimp, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (gimp_color_profile_is_equal (profile, other));
  g_object_unref (profile);

  /* and a new CMYK image gets it */
  {
    GimpImage *image = gimp_image_new (gimp, 8, 8, GIMP_CMYK,
                                       GIMP_PRECISION_U8_NON_LINEAR);

    g_assert_true (gimp_color_profile_is_equal (gimp_image_get_color_profile (image),
                                                other));
    g_assert_cmpint (gimp_image_is_dirty (image), ==, 1);
    g_object_unref (image);
  }

  g_file_delete (file, NULL, NULL);
  g_object_unref (file);
  g_free (path);

  /* preference = input-only profile: builtin + error */
  path = g_build_filename (g_get_tmp_dir (), "gimp-test-cmyk-input-only.icc", NULL);
  file = g_file_new_for_path (path);
  g_assert_true (gimp_color_profile_save_to_file (input_only, file, NULL));
  g_object_set (config, "cmyk-profile", path, NULL);

  profile = gimp_get_default_cmyk_profile (gimp, NULL, &error);
  g_assert_nonnull (error);
  g_clear_error (&error);
  g_assert_true (gimp_color_profile_is_equal (profile, builtin));
  g_object_unref (profile);

  g_file_delete (file, NULL, NULL);
  g_object_unref (file);
  g_free (path);

  /* optional real profiles */
  real = g_getenv ("GIMP_TESTING_CMYK_ICC");
  if (real && *real)
    {
      gchar **paths = g_strsplit (real, ":", -1);
      gint    i;

      for (i = 0; paths[i]; i++)
        {
          GimpColorProfile *real_profile;

          file = g_file_new_for_path (paths[i]);
          real_profile = gimp_color_profile_new_from_file (file, NULL);
          g_object_unref (file);

          if (! real_profile)
            continue;

          g_object_set (config, "cmyk-profile", paths[i], NULL);
          profile = gimp_get_default_cmyk_profile (gimp, NULL, &error);
          g_assert_no_error (error);
          g_assert_true (gimp_color_profile_is_equal (profile, real_profile));
          g_object_unref (profile);
          g_object_unref (real_profile);
        }

      g_strfreev (paths);
    }
  else
    {
      g_test_message ("GIMP_TESTING_CMYK_ICC not set, skipping real profiles");
    }

  g_object_set (config, "cmyk-profile", old_pref, NULL);
  g_free (old_pref);

  g_object_unref (other);
  g_object_unref (input_only);
  g_object_unref (builtin);
}


/*  F7: XCF version  */

static void
f7_xcf_version (void)
{
  GimpImage   *image;
  gint         version;
  gint         gimp_version;
  const gchar *version_string;
  gint         rgb_version;

  image = gimp_image_new (gimp, 8, 8, GIMP_RGB, GIMP_PRECISION_U8_NON_LINEAR);
  rgb_version = gimp_image_get_xcf_version (image, FALSE, NULL, NULL, NULL);
  g_assert_cmpint (rgb_version, <, 28);
  g_object_unref (image);

  image = gimp_image_new (gimp, 8, 8, GIMP_CMYK, GIMP_PRECISION_U8_NON_LINEAR);
  version = gimp_image_get_xcf_version (image, FALSE, &gimp_version,
                                        &version_string, NULL);
  g_assert_cmpint (version, >=, 28);
  g_assert_cmpint (gimp_version, ==, 340);
  g_assert_cmpstr (version_string, ==, "GIMP 3.4");
  g_object_unref (image);

  /* RGB unchanged after creating CMYK images */
  image = gimp_image_new (gimp, 8, 8, GIMP_RGB, GIMP_PRECISION_U8_NON_LINEAR);
  g_assert_cmpint (gimp_image_get_xcf_version (image, FALSE, NULL, NULL, NULL),
                   ==, rgb_version);
  g_object_unref (image);
}


/*  F8: canonical CMYK space (I8)  */

static void
f8_canonical_space (void)
{
  GimpColorProfile *builtin;
  GimpColorProfile *variant;
  GimpColorProfile *other;
  GimpColorProfile *srgb;
  const Babl       *space_builtin;
  const Babl       *space_variant;
  const Babl       *space_other;

  builtin = gimp_color_profile_new_cmyk_builtin ();
  variant = gimp_test_utils_cmyk_header_variant (builtin);
  other   = other_cmyk_profile ();

  g_assert_true (gimp_color_profile_is_equal (builtin, variant));

  space_builtin = gimp_color_profile_get_space (builtin,
                                                GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                                NULL);
  space_variant = gimp_color_profile_get_space (variant,
                                                GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                                NULL);
  space_other   = gimp_color_profile_get_space (other,
                                                GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                                NULL);

  g_assert_nonnull (space_builtin);
  g_assert_true (space_builtin == space_variant);
  g_assert_nonnull (space_other);
  g_assert_true (space_other != space_builtin);

  /* the header variant itself gives a different space in raw babl */
  {
    const guint8 *data;
    gsize         length;
    const Babl   *raw;

    data = gimp_color_profile_get_icc_profile (variant, &length);
    raw  = babl_space_from_icc ((const gchar *) data, length,
                                BABL_ICC_INTENT_RELATIVE_COLORIMETRIC, NULL);
    g_assert_true (raw != space_builtin);
  }

  /* RGB profiles unchanged */
  srgb = gimp_color_profile_new_rgb_srgb ();
  g_assert_nonnull (gimp_color_profile_get_space (srgb,
                                                  GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                                  NULL));
  g_object_unref (srgb);

  g_object_unref (other);
  g_object_unref (variant);
  g_object_unref (builtin);
}


/*  F9: transform identity for equal CMYK profiles (B1)  */

static void
check_identity_transform (GimpColorProfile *src_profile,
                          GimpColorProfile *dest_profile,
                          const gchar      *src_encoding,
                          const gchar      *dest_encoding)
{
  static const gdouble  patches[][5] = { { 0.0, 0.0, 0.0, 1.0, 1.0 },
                                         { 0.1, 0.2, 0.3, 0.4, 1.0 },
                                         { 1.0, 0.0, 0.0, 0.0, 1.0 },
                                         { 0.0, 0.0, 0.0, 0.0, 0.5 } };
  const gint            n = G_N_ELEMENTS (patches);
  const Babl           *src_format;
  const Babl           *dest_format;
  GimpColorTransform   *transform;
  guint8                src[G_N_ELEMENTS (patches) * 5 * 8];
  guint8                dest[G_N_ELEMENTS (patches) * 5 * 8];
  guint8                expected[G_N_ELEMENTS (patches) * 5 * 8];
  gint                  bpp;

  src_format  = gimp_color_profile_get_format (src_profile,
                                               babl_format (src_encoding),
                                               GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                               NULL);
  dest_format = gimp_color_profile_get_format (dest_profile,
                                               babl_format (dest_encoding),
                                               GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                               NULL);
  g_assert_nonnull (src_format);
  g_assert_nonnull (dest_format);

  /* the same pixels, without any color space: pure re-encoding */
  babl_process (babl_fish (babl_format ("CMYKA double"),
                           babl_format (src_encoding)),
                patches, src, n);
  babl_process (babl_fish (babl_format (src_encoding),
                           babl_format (dest_encoding)),
                src, expected, n);

  transform = gimp_color_transform_new (src_profile,  src_format,
                                        dest_profile, dest_format,
                                        GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                        GIMP_COLOR_TRANSFORM_FLAGS_BLACK_POINT_COMPENSATION);
  g_assert_nonnull (transform);

  memset (dest, 0, sizeof (dest));
  gimp_color_transform_process_pixels (transform,
                                       src_format,  src,
                                       dest_format, dest,
                                       n);

  bpp = babl_format_get_bytes_per_pixel (dest_format);
  g_assert_true (memcmp (dest, expected, n * bpp) == 0);

  /* and through a buffer */
  {
    GeglBuffer *src_buffer;
    GeglBuffer *dest_buffer;

    src_buffer  = gegl_buffer_linear_new_from_data (src, src_format,
                                                    GEGL_RECTANGLE (0, 0, n, 1),
                                                    GEGL_AUTO_ROWSTRIDE,
                                                    NULL, NULL);
    dest_buffer = gegl_buffer_new (GEGL_RECTANGLE (0, 0, n, 1), dest_format);

    gimp_color_transform_process_buffer (transform,
                                         src_buffer,  NULL,
                                         dest_buffer, NULL);

    memset (dest, 0, sizeof (dest));
    gegl_buffer_get (dest_buffer, GEGL_RECTANGLE (0, 0, n, 1), 1.0,
                     dest_format, dest,
                     GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
    g_assert_true (memcmp (dest, expected, n * bpp) == 0);

    g_object_unref (dest_buffer);
    g_object_unref (src_buffer);
  }

  g_object_unref (transform);
}

static void
f9_check_identity (GimpColorProfile *builtin,
                   GimpColorProfile *variant)
{
  check_identity_transform (builtin, variant, "CMYK u16",    "CMYK u16");
  check_identity_transform (variant, builtin, "CMYK u16",    "CMYK u16");
  check_identity_transform (builtin, variant, "CMYKA float", "CMYKA u16");
  check_identity_transform (builtin, builtin, "CMYKA u16",   "CMYKA u16");
  check_identity_transform (builtin, variant, "CMYKA u8",    "CMYKA double");
}

static void
f9_transform_identity (void)
{
  GimpColorProfile   *builtin;
  GimpColorProfile   *variant;
  GimpColorProfile   *other;
  GimpColorProfile   *input_only;
  GimpColorProfile   *srgb;
  GimpColorTransform *transform;
  gchar              *saved_env;

  builtin    = gimp_color_profile_new_cmyk_builtin ();
  variant    = gimp_test_utils_cmyk_header_variant (builtin);
  other      = other_cmyk_profile ();
  input_only = gimp_test_utils_cmyk_input_only_profile ();
  srgb       = gimp_color_profile_new_rgb_srgb ();

  g_assert_true (gimp_color_profile_is_equal (builtin, variant));

  f9_check_identity (builtin, variant);

  saved_env = g_strdup (g_getenv ("GIMP_COLOR_TRANSFORM_DISABLE_BABL"));
  g_setenv ("GIMP_COLOR_TRANSFORM_DISABLE_BABL", "1", TRUE);

  f9_check_identity (builtin, variant);

  if (saved_env)
    g_setenv ("GIMP_COLOR_TRANSFORM_DISABLE_BABL", saved_env, TRUE);
  else
    g_unsetenv ("GIMP_COLOR_TRANSFORM_DISABLE_BABL");
  g_free (saved_env);

  /* can_gegl_copy */
  g_assert_true  (gimp_color_transform_can_gegl_copy (builtin, variant));
  g_assert_true  (gimp_color_transform_can_gegl_copy (builtin, builtin));
  g_assert_false (gimp_color_transform_can_gegl_copy (builtin, other));
  g_assert_false (gimp_color_transform_can_gegl_copy (other,   builtin));
  g_assert_false (gimp_color_transform_can_gegl_copy (builtin, srgb));
  g_assert_false (gimp_color_transform_can_gegl_copy (srgb,    builtin));
  g_assert_false (gimp_color_transform_can_gegl_copy (builtin, input_only));

  /* different CMYK profiles: an lcms transform */
  transform = gimp_color_transform_new (builtin, babl_format ("CMYKA u16"),
                                        other,   babl_format ("CMYKA u16"),
                                        GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                        GIMP_COLOR_TRANSFORM_FLAGS_BLACK_POINT_COMPENSATION);
  g_assert_nonnull (transform);
  g_object_unref (transform);

  /* an input-only CMYK target fails cleanly, also from RGB */
  transform = gimp_color_transform_new (builtin,    babl_format ("CMYKA u16"),
                                        input_only, babl_format ("CMYKA u16"),
                                        GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                        0);
  g_assert_null (transform);

  transform = gimp_color_transform_new (srgb,       babl_format ("R'G'B'A float"),
                                        input_only, babl_format ("CMYKA float"),
                                        GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                        0);
  g_assert_null (transform);

  /* but it works as a source */
  transform = gimp_color_transform_new (input_only, babl_format ("CMYKA float"),
                                        srgb,       babl_format ("R'G'B'A float"),
                                        GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                        0);
  g_assert_nonnull (transform);
  g_object_unref (transform);

  g_object_unref (srgb);
  g_object_unref (input_only);
  g_object_unref (other);
  g_object_unref (variant);
  g_object_unref (builtin);
}


/*  F9b: CMYK -> CMYK convert / assign in the core  */

static void
f9b_convert_assign (void)
{
  static const gdouble  patches[][5] = { { 0.0, 0.0, 0.0, 1.0, 1.0 },
                                         { 0.1, 0.2, 0.3, 0.4, 1.0 },
                                         { 1.0, 0.0, 0.0, 0.0, 1.0 },
                                         { 0.3, 0.6, 0.9, 0.0, 0.5 } };
  const gint            n = G_N_ELEMENTS (patches);
  GimpColorProfile     *builtin;
  GimpColorProfile     *variant;
  GimpColorProfile     *other;
  GimpImage            *image;
  GimpLayer            *layer;
  GeglBuffer           *buffer;
  const Babl           *format;
  guint16              *before;
  guint16              *after;
  guint16               expected[G_N_ELEMENTS (patches) * 5];
  GError               *error = NULL;
  gint                  i;

  builtin = gimp_color_profile_new_cmyk_builtin ();
  variant = gimp_test_utils_cmyk_header_variant (builtin);
  other   = other_cmyk_profile ();

  image = gimp_test_utils_create_cmyk_image (gimp, n, 1,
                                             GIMP_PRECISION_U16_NON_LINEAR,
                                             builtin);
  layer = gimp_test_utils_add_cmyk_layer (image, "patches", TRUE, NULL);
  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));

  for (i = 0; i < n; i++)
    gimp_test_utils_write_cmyk_patch (buffer, GEGL_RECTANGLE (i, 0, 1, 1),
                                      patches[i]);

  format = gimp_image_get_layer_format (image, TRUE);
  before = gimp_test_utils_read_patch (buffer, GEGL_RECTANGLE (0, 0, n, 1),
                                       format);

  /* equal profile: no-op */
  g_assert_true (gimp_image_convert_color_profile (image, variant,
                                                   GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                                   TRUE, NULL, &error));
  g_assert_no_error (error);
  g_assert_true (gimp_color_profile_is_equal (gimp_image_get_color_profile (image),
                                              builtin));

  /* different profile: lcms with intent and BPC */
  {
    cmsHTRANSFORM ref;

    ref = cmsCreateTransform (gimp_color_profile_get_lcms_profile (builtin),
                              TYPE_CMYKA_16,
                              gimp_color_profile_get_lcms_profile (other),
                              TYPE_CMYKA_16,
                              INTENT_RELATIVE_COLORIMETRIC,
                              cmsFLAGS_BLACKPOINTCOMPENSATION |
                              cmsFLAGS_NOOPTIMIZE             |
                              cmsFLAGS_COPY_ALPHA);
    g_assert_nonnull (ref);
    cmsDoTransform (ref, before, expected, n);
    cmsDeleteTransform (ref);
  }

  g_assert_true (gimp_image_convert_color_profile (image, other,
                                                   GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                                   TRUE, NULL, &error));
  g_assert_no_error (error);
  g_assert_true (gimp_color_profile_is_equal (gimp_image_get_color_profile (image),
                                              other));

  layer  = GIMP_LAYER (gimp_image_get_layer_iter (image)->data);
  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));
  format = gimp_drawable_get_format (GIMP_DRAWABLE (layer));

  g_assert_true (babl_format_get_space (format) ==
                 gimp_color_profile_get_space (other,
                                               GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                               NULL));

  after = gimp_test_utils_read_patch (buffer, GEGL_RECTANGLE (0, 0, n, 1),
                                      format);
  for (i = 0; i < n * 5; i++)
    g_assert_cmpint (ABS ((gint) after[i] - (gint) expected[i]), <=, 1);
  g_free (after);

  /* undo restores profile and pixels */
  gimp_image_undo (image);
  g_assert_true (gimp_color_profile_is_equal (gimp_image_get_color_profile (image),
                                              builtin));
  layer  = GIMP_LAYER (gimp_image_get_layer_iter (image)->data);
  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));
  after  = gimp_test_utils_read_patch (buffer, GEGL_RECTANGLE (0, 0, n, 1),
                                       gimp_drawable_get_format (GIMP_DRAWABLE (layer)));
  g_assert_true (memcmp (after, before, n * 5 * sizeof (guint16)) == 0);
  g_free (after);

  /* assign: relabel, the ink values stay */
  g_assert_true (gimp_image_assign_color_profile (image, other, NULL, &error));
  g_assert_no_error (error);
  g_assert_true (gimp_color_profile_is_equal (gimp_image_get_color_profile (image),
                                              other));
  layer  = GIMP_LAYER (gimp_image_get_layer_iter (image)->data);
  format = gimp_drawable_get_format (GIMP_DRAWABLE (layer));
  g_assert_true (babl_format_get_space (format) ==
                 gimp_color_profile_get_space (other,
                                               GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                               NULL));
  after = gimp_test_utils_read_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                      GEGL_RECTANGLE (0, 0, n, 1), format);
  g_assert_true (memcmp (after, before, n * 5 * sizeof (guint16)) == 0);
  g_free (after);

  /* assign NULL: the default CMYK profile (I1) */
  g_assert_true (gimp_image_assign_color_profile (image, NULL, NULL, &error));
  g_assert_no_error (error);
  g_assert_true (gimp_color_profile_is_cmyk_bidirectional (gimp_image_get_color_profile (image)));

  /* an RGB profile is refused */
  {
    GimpColorProfile *srgb = gimp_color_profile_new_rgb_srgb ();

    g_assert_false (gimp_image_convert_color_profile (image, srgb,
                                                      GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                                      TRUE, NULL, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_object_unref (srgb);
  }

  /* sRGB transforms of the image */
  {
    GimpColorTransform *transform;
    guint8              rgba[4];
    const guint16       k100[5] = { 0, 0, 0, 65535, 65535 };

    g_assert_nonnull (gimp_image_get_color_transform_to_srgb_double (image));
    g_assert_nonnull (gimp_image_get_color_transform_from_srgb_double (image));

    transform = gimp_image_get_color_transform_to_srgb_u8 (image);
    g_assert_nonnull (transform);

    gimp_color_transform_process_pixels (transform,
                                         babl_format ("CMYKA u16"), k100,
                                         babl_format ("R'G'B'A u8"), rgba,
                                         1);
    /* perceptual + BPC on a v4 profile: lcms maps K100 to the v4
     * perceptual reference black (L* ~ 3, about 12/255), neutral and no
     * lighter than the relcol black point (0.08)
     */
    g_test_message ("K100 -> sRGB u8 (perceptual, BPC): %d %d %d",
                    rgba[0], rgba[1], rgba[2]);
    g_assert_cmpint (rgba[0], <=, 0.09 * 255);
    g_assert_cmpint (ABS (rgba[0] - rgba[1]), <=, 2);
    g_assert_cmpint (ABS (rgba[0] - rgba[2]), <=, 2);
    g_assert_cmpint (rgba[3], ==, 255);
  }

  g_free (before);
  g_object_unref (image);
  g_object_unref (other);
  g_object_unref (variant);
  g_object_unref (builtin);
}


/*  F10: babl contract (I5)  */

static void
f10_babl_contract (void)
{
  GimpColorProfile *builtin;
  const Babl       *space;
  GRand            *rand;
  const gint        n = 1000;
  guint16          *pixels;
  guint16          *result;
  gfloat           *rgba;
  gfloat           *rgba_result;
  gint              i;

  builtin = gimp_color_profile_new_cmyk_builtin ();
  space   = gimp_color_profile_get_space (builtin,
                                          GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                          NULL);
  g_assert_nonnull (space);
  g_assert_true (babl_space_is_cmyk (space));

  rand = g_rand_new_with_seed (4711);

  pixels      = g_new (guint16, n * 5);
  result      = g_new (guint16, n * 5);
  rgba        = g_new (gfloat,  n * 4);
  rgba_result = g_new (gfloat,  n * 4);

  for (i = 0; i < n * 5; i++)
    pixels[i] = g_rand_int_range (rand, 0, 65536);

  for (i = 0; i < n * 4; i++)
    rgba[i] = g_rand_double (rand);

  /* CMYKA u16 without space <-> with the CMYK space: identity */
  babl_process (babl_fish (babl_format ("CMYKA u16"),
                           babl_format_with_space ("CMYKA u16", space)),
                pixels, result, n);
  g_assert_true (memcmp (pixels, result, n * 5 * sizeof (guint16)) == 0);

  babl_process (babl_fish (babl_format_with_space ("CMYKA u16", space),
                           babl_format ("CMYKA u16")),
                pixels, result, n);
  g_assert_true (memcmp (pixels, result, n * 5 * sizeof (guint16)) == 0);

  /* R'G'B'A float tagged with a CMYK space means sRGB */
  babl_process (babl_fish (babl_format_with_space ("R'G'B'A float", space),
                           babl_format ("R'G'B'A float")),
                rgba, rgba_result, n);
  for (i = 0; i < n * 4; i++)
    g_assert_cmpfloat_with_epsilon (rgba_result[i], rgba[i], 1e-6);

  babl_process (babl_fish (babl_format ("R'G'B'A float"),
                           babl_format_with_space ("R'G'B'A float", space)),
                rgba, rgba_result, n);
  for (i = 0; i < n * 4; i++)
    g_assert_cmpfloat_with_epsilon (rgba_result[i], rgba[i], 1e-6);

  g_free (rgba_result);
  g_free (rgba);
  g_free (result);
  g_free (pixels);
  g_rand_free (rand);
  g_object_unref (builtin);
}


/*  F11: lcms format mapping  */

static void
f11_check_format (GimpColorProfile *profile,
                  cmsHTRANSFORM     reference,
                  const gchar      *encoding)
{
  static const gdouble  patches[][5] = { { 0.0,  0.0,  0.0, 0.0, 1.0 },
                                         { 0.0,  0.0,  0.0, 1.0, 1.0 },
                                         { 1.0,  0.0,  0.0, 0.0, 1.0 },
                                         { 0.1,  0.2,  0.3, 0.4, 1.0 },
                                         { 0.5,  0.5,  0.5, 0.5, 0.5 },
                                         { 0.25, 0.75, 0.0, 0.1, 0.25 } };
  const gint            n = G_N_ELEMENTS (patches);
  GimpColorProfile     *srgb;
  GimpColorTransform   *transform;
  const Babl           *format;
  const Babl           *lcms_format;
  guint32               lcms_type = 0;
  guint8                pixels[G_N_ELEMENTS (patches) * 5 * 8];
  gdouble               quantized[G_N_ELEMENTS (patches)][5];
  gdouble               rgba[G_N_ELEMENTS (patches) * 4];
  const Babl           *rgb_format;
  gint                  rgb_n;
  gboolean              has_alpha;
  gint                  i, j;

  format    = babl_format (encoding);
  has_alpha = babl_format_has_alpha (format);

  /* lcms only copies alpha if both sides have it */
  rgb_format = babl_format (has_alpha ? "R'G'B'A double" : "R'G'B' double");
  rgb_n      = has_alpha ? 4 : 3;

  /* the mapping itself */
  lcms_format = gimp_color_profile_get_lcms_format (format, &lcms_type);
  g_assert_nonnull (lcms_format);
  g_assert_cmpint (T_COLORSPACE (lcms_type), ==, PT_CMYK);
  g_assert_cmpint (T_CHANNELS (lcms_type),   ==, 4);
  g_assert_cmpint (T_EXTRA (lcms_type),      ==, has_alpha ? 1 : 0);
  g_assert_cmpint (T_FLOAT (lcms_type),      ==, 0);
  g_assert_true (babl_format_get_model (lcms_format) ==
                 babl_model (has_alpha ? "CMYKA" : "CMYK"));

  /* the pixels, as they are stored in the format */
  babl_process (babl_fish (babl_format ("CMYKA double"), format),
                patches, pixels, n);
  babl_process (babl_fish (format, babl_format ("CMYKA double")),
                pixels, quantized, n);

  srgb = gimp_color_profile_new_rgb_srgb ();

  transform = gimp_color_transform_new (profile, format,
                                        srgb,    rgb_format,
                                        GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                        GIMP_COLOR_TRANSFORM_FLAGS_NOOPTIMIZE);
  g_assert_nonnull (transform);

  gimp_color_transform_process_pixels (transform,
                                       format, pixels,
                                       rgb_format, rgba,
                                       n);

  for (i = 0; i < n; i++)
    {
      gdouble ink[4];
      gdouble ref[3];

      for (j = 0; j < 4; j++)
        ink[j] = quantized[i][j] * 100.0;  /* lcms float CMYK is 0..100 */

      cmsDoTransform (reference, ink, ref, 1);

      for (j = 0; j < 3; j++)
        {
          if (fabs (rgba[i * rgb_n + j] - ref[j]) > 1.0 / 255.0)
            g_error ("F11 %s: patch %d channel %d: %f, reference %f",
                     encoding, i, j, rgba[i * rgb_n + j], ref[j]);
        }

      if (has_alpha)
        g_assert_cmpfloat_with_epsilon (rgba[i * rgb_n + 3],
                                        quantized[i][4],
                                        1.0 / 255.0);
    }

  g_object_unref (transform);
  g_object_unref (srgb);
}

static void
f11_lcms_formats (void)
{
  static const gchar *types[] = { "u8", "u16", "u32", "half", "float", "double" };
  GimpColorProfile   *builtin;
  cmsHPROFILE         srgb;
  cmsHTRANSFORM       reference;
  gint                i;

  builtin = gimp_color_profile_new_cmyk_builtin ();
  srgb    = cmsCreate_sRGBProfile ();

  reference = cmsCreateTransform (gimp_color_profile_get_lcms_profile (builtin),
                                  TYPE_CMYK_DBL,
                                  srgb, TYPE_RGB_DBL,
                                  INTENT_RELATIVE_COLORIMETRIC,
                                  cmsFLAGS_NOOPTIMIZE);
  g_assert_nonnull (reference);

  for (i = 0; i < G_N_ELEMENTS (types); i++)
    {
      gchar *cmyk  = g_strdup_printf ("CMYK %s",  types[i]);
      gchar *cmyka = g_strdup_printf ("CMYKA %s", types[i]);

      f11_check_format (builtin, reference, cmyk);
      f11_check_format (builtin, reference, cmyka);

      g_free (cmyk);
      g_free (cmyka);
    }

  f11_check_format (builtin, reference, "cmyk u8");
  f11_check_format (builtin, reference, "cmykA u8");
  f11_check_format (builtin, reference, "cmykA float");
  f11_check_format (builtin, reference, "CaMaYaKaA float");
  f11_check_format (builtin, reference, "camayakaA float");

  cmsDeleteTransform (reference);
  cmsCloseProfile (srgb);
  g_object_unref (builtin);
}


/*  F12: component masks (I7)  */

static void
f12_masks (void)
{
  static const struct
  {
    GimpChannelType   channel;
    GimpComponentMask bit;
  }
  cmyk_bits[] =
  {
    { GIMP_CHANNEL_CYAN,    GIMP_COMPONENT_MASK_CYAN    },
    { GIMP_CHANNEL_MAGENTA, GIMP_COMPONENT_MASK_MAGENTA },
    { GIMP_CHANNEL_YELLOW,  GIMP_COMPONENT_MASK_YELLOW  },
    { GIMP_CHANNEL_BLACK,   GIMP_COMPONENT_MASK_BLACK   },
    { GIMP_CHANNEL_ALPHA,   GIMP_COMPONENT_MASK_ALPHA   }
  };

  GimpImage *image;
  gint       i;

  g_assert_cmpint (GIMP_COMPONENT_MASK_ALL, ==, 0x1f);
  g_assert_cmpint (GIMP_COMPONENT_MASK_CYAN, ==, 1 << 0);
  g_assert_cmpint (GIMP_COMPONENT_MASK_MAGENTA, ==, 1 << 1);
  g_assert_cmpint (GIMP_COMPONENT_MASK_YELLOW, ==, 1 << 2);
  g_assert_cmpint (GIMP_COMPONENT_MASK_ALPHA, ==, 1 << 3);
  g_assert_cmpint (GIMP_COMPONENT_MASK_BLACK, ==, 1 << 4);

  /* RGB: the BLACK bit is always set */
  image = gimp_image_new (gimp, 8, 8, GIMP_RGB, GIMP_PRECISION_U8_NON_LINEAR);
  g_assert_cmpint (gimp_image_get_active_mask (image), ==,
                   GIMP_COMPONENT_MASK_ALL);
  g_assert_cmpint (gimp_image_get_visible_mask (image), ==,
                   GIMP_COMPONENT_MASK_ALL);
  gimp_image_set_component_active (image, GIMP_CHANNEL_RED, FALSE);
  g_assert_cmpint (gimp_image_get_active_mask (image), ==,
                   GIMP_COMPONENT_MASK_ALL & ~GIMP_COMPONENT_MASK_RED);
  g_object_unref (image);

  /* GRAY: BLACK set, visible alpha is ALPHA_G */
  image = gimp_image_new (gimp, 8, 8, GIMP_GRAY, GIMP_PRECISION_U8_NON_LINEAR);
  g_assert_cmpint (gimp_image_get_active_mask (image), ==,
                   GIMP_COMPONENT_MASK_ALL);
  g_assert_cmpint (gimp_image_get_visible_mask (image), ==,
                   GIMP_COMPONENT_MASK_ALL);
  gimp_image_set_component_visible (image, GIMP_CHANNEL_ALPHA, FALSE);
  g_assert_cmpint (gimp_image_get_visible_mask (image), ==,
                   GIMP_COMPONENT_MASK_ALL & ~GIMP_COMPONENT_MASK_ALPHA);
  gimp_image_set_component_visible (image, GIMP_CHANNEL_ALPHA, TRUE);
  g_assert_cmpint (gimp_image_get_visible_mask (image), ==,
                   GIMP_COMPONENT_MASK_ALL);
  g_object_unref (image);

  /* CMYK: one bit per component */
  image = gimp_image_new (gimp, 8, 8, GIMP_CMYK, GIMP_PRECISION_U8_NON_LINEAR);
  g_assert_cmpint (gimp_image_get_active_mask (image), ==,
                   GIMP_COMPONENT_MASK_ALL);
  g_assert_cmpint (gimp_image_get_visible_mask (image), ==,
                   GIMP_COMPONENT_MASK_ALL);

  for (i = 0; i < G_N_ELEMENTS (cmyk_bits); i++)
    {
      gimp_image_set_component_active (image, cmyk_bits[i].channel, FALSE);
      g_assert_false (gimp_image_get_component_active (image,
                                                       cmyk_bits[i].channel));
      g_assert_cmpint (gimp_image_get_active_mask (image), ==,
                       GIMP_COMPONENT_MASK_ALL & ~cmyk_bits[i].bit);
      gimp_image_set_component_active (image, cmyk_bits[i].channel, TRUE);

      gimp_image_set_component_visible (image, cmyk_bits[i].channel, FALSE);
      g_assert_false (gimp_image_get_component_visible (image,
                                                        cmyk_bits[i].channel));
      g_assert_cmpint (gimp_image_get_visible_mask (image), ==,
                       GIMP_COMPONENT_MASK_ALL & ~cmyk_bits[i].bit);
      gimp_image_set_component_visible (image, cmyk_bits[i].channel, TRUE);
    }

  g_assert_cmpint (gimp_image_get_component_index (image, GIMP_CHANNEL_CYAN),
                   ==, CYAN);
  g_assert_cmpint (gimp_image_get_component_index (image, GIMP_CHANNEL_BLACK),
                   ==, BLACK);
  g_assert_cmpint (gimp_image_get_component_index (image, GIMP_CHANNEL_ALPHA),
                   ==, ALPHA_C);

  for (i = 0; i < G_N_ELEMENTS (cmyk_bits); i++)
    {
      const Babl *format = gimp_image_get_component_format (image,
                                                            cmyk_bits[i].channel);

      g_assert_nonnull (format);
      g_assert_true (babl_format_get_space (format) ==
                     gimp_image_get_layer_space (image));
    }

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

  g_test_add_func ("/cmyk-foundation/F1-formats",            f1_formats);
  g_test_add_func ("/cmyk-foundation/F2-cmyk-support",       f2_cmyk_support);
  g_test_add_func ("/cmyk-foundation/F3-builtin-profile",    f3_builtin_profile);
  g_test_add_func ("/cmyk-foundation/F4-input-only",         f4_input_only);
  g_test_add_func ("/cmyk-foundation/F5-new-image",          f5_new_image);
  g_test_add_func ("/cmyk-foundation/F6-default-profile",    f6_default_profile);
  g_test_add_func ("/cmyk-foundation/F7-xcf-version",        f7_xcf_version);
  g_test_add_func ("/cmyk-foundation/F8-canonical-space",    f8_canonical_space);
  g_test_add_func ("/cmyk-foundation/F9-transform-identity", f9_transform_identity);
  g_test_add_func ("/cmyk-foundation/F9b-convert-assign",    f9b_convert_assign);
  g_test_add_func ("/cmyk-foundation/F10-babl-contract",     f10_babl_contract);
  g_test_add_func ("/cmyk-foundation/F11-lcms-formats",      f11_lcms_formats);
  g_test_add_func ("/cmyk-foundation/F12-masks",             f12_masks);

  result = g_test_run ();

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_BUILDDIR",
                                       "app/tests/gimpdir-output");

  gimp_exit (gimp, TRUE);

  return result;
}

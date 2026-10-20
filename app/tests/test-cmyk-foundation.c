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


/*  SHA-256 of the builtin CMYK profile bytes.  WS01a: the 3^4/3^3 stub;
 *  WS01b updates it once for the final profile.
 */
#define BUILTIN_CMYK_SHA256 \
  "f900f7abb4d0cf692efe659836644d6f6221b0998d641ff93c175ae5e4116263"


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


/*  F3: builtin profile (stub checks; WS01b adds the babl round trip)  */

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


/*  F9-F11: WS01b  */

static void
f9_transform_identity (void)
{
  g_test_skip ("F9 transform identity: WS01b");
}

static void
f10_babl_contract (void)
{
  g_test_skip ("F10 babl contract: WS01b");
}

static void
f11_lcms_formats (void)
{
  g_test_skip ("F11 lcms format mapping: WS01b");
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
  g_test_add_func ("/cmyk-foundation/F10-babl-contract",     f10_babl_contract);
  g_test_add_func ("/cmyk-foundation/F11-lcms-formats",      f11_lcms_formats);
  g_test_add_func ("/cmyk-foundation/F12-masks",             f12_masks);

  result = g_test_run ();

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_BUILDDIR",
                                       "app/tests/gimpdir-output");

  gimp_exit (gimp, TRUE);

  return result;
}

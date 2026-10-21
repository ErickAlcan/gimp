/* LIBGIMP - The GIMP Library
 * Copyright (C) 1995-2003 Peter Mattis and Spencer Kimball
 *
 * gimpimagecolorprofile.c
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gimp.h"


/**
 * gimp_image_get_color_profile:
 * @image: The image.
 *
 * This procedure returns the image's color profile, or %NULL if the
 * image uses the built-in color profile.
 *
 * See also [method@Gimp.Image.get_effective_color_profile] if you want
 * the effective color profile and don't want to have to deal with %NULL
 * return values.
 *
 * Returns: (transfer full) (nullable): The image's color profile. The
 *          returned value must be freed with [method@GObject.Object.unref].
 *
 * Since: 2.10
 **/
GimpColorProfile *
gimp_image_get_color_profile (GimpImage *image)
{
  GBytes *data;

  data = _gimp_image_get_color_profile (image);
  if (data)
    {
      GimpColorProfile *profile;

      profile = gimp_color_profile_new_from_icc_profile (g_bytes_get_data (data, NULL),
                                                         g_bytes_get_size (data),
                                                         NULL);
      g_bytes_unref (data);

      return profile;
    }

  return NULL;
}

/**
 * gimp_image_set_color_profile:
 * @image:   The image.
 * @profile: (nullable): A #GimpColorProfile, or %NULL.
 *
 * This procedure sets the image's color profile.
 *
 * If %NULL is passed as @profile, then the built-in profile for
 * @image's color model is set.
 *
 * Returns: %TRUE on success.
 *
 * Since: 2.10
 **/
gboolean
gimp_image_set_color_profile (GimpImage        *image,
                              GimpColorProfile *profile)
{
  const guint8 *data   = NULL;
  GBytes       *bytes  = NULL;
  gboolean      ret;

  g_return_val_if_fail (profile == NULL || GIMP_IS_COLOR_PROFILE (profile),
                        FALSE);

  if (profile)
    {
      gsize l;

      data = gimp_color_profile_get_icc_profile (profile, &l);
      bytes = g_bytes_new_static (data, l);
    }

  ret = _gimp_image_set_color_profile (image, bytes);

  g_bytes_unref (bytes);
  return ret;
}

/**
 * gimp_image_get_simulation_profile:
 * @image: The image.
 *
 * This procedure returns the image's simulation color profile, or %NULL if
 * the image has no simulation color profile assigned.
 *
 * Returns: (transfer full) (nullable): The image's simulation color profile. The
 *          returned value must be freed with [method@GObject.Object.unref].
 *
 * Since: 3.0
 **/
GimpColorProfile *
gimp_image_get_simulation_profile (GimpImage *image)
{
  GBytes *data;

  data = _gimp_image_get_simulation_profile (image);
  if (data)
    {
      GimpColorProfile *profile;

      profile = gimp_color_profile_new_from_icc_profile (g_bytes_get_data (data, NULL),
                                                         g_bytes_get_size (data),
                                                         NULL);
      g_bytes_unref (data);

      return profile;
    }

  return NULL;
}

/**
 * gimp_image_set_simulation_profile:
 * @image:   The image.
 * @profile: (nullable): A #GimpColorProfile, or %NULL.
 *
 * This procedure sets the image's simulation color profile.
 *
 * If %NULL is passed as @profile, then the simulation profile is unset.
 *
 * Returns: %TRUE on success.
 *
 * Since: 3.0
 **/
gboolean
gimp_image_set_simulation_profile (GimpImage        *image,
                                   GimpColorProfile *profile)
{
  const guint8 *data   = NULL;
  GBytes       *bytes  = NULL;
  gboolean      ret;

  g_return_val_if_fail (profile == NULL || GIMP_IS_COLOR_PROFILE (profile),
                        FALSE);

  if (profile)
    {
      gsize l;

      data = gimp_color_profile_get_icc_profile (profile, &l);
      bytes = g_bytes_new_static (data, l);
    }

  ret = _gimp_image_set_simulation_profile (image, bytes);
  g_bytes_unref (bytes);
  return ret;
}

/**
 * gimp_image_get_effective_color_profile:
 * @image: The image.
 *
 * This procedure returns the color profile that is actually used for
 * this image, which is the profile returned by
 * [method@Gimp.Image.get_color_profile] if the image has a profile
 * assigned, or a built-in profile for the given color space otherwise.
 *
 * Returns: (transfer full): The color profile. The returned value must
 *          be freed with [method@GObject.Object.unref].
 *
 * Since: 2.10
 **/
GimpColorProfile *
gimp_image_get_effective_color_profile (GimpImage *image)
{
  GBytes *data;

  data = _gimp_image_get_effective_color_profile (image);

  if (data)
    {
      GimpColorProfile *profile;

      profile = gimp_color_profile_new_from_icc_profile (g_bytes_get_data (data, NULL),
                                                         g_bytes_get_size (data),
                                                         NULL);
      g_bytes_unref (data);

      return profile;
    }

  return NULL;
}

/**
 * gimp_image_convert_color_profile:
 * @image:   The image.
 * @profile: (nullable): The color profile to convert to.
 * @intent:  Rendering intent.
 * @bpc:     Black point compensation.
 *
 * This procedure converts from the image's color profile (or the
 * built-in profile if none is set) to the given color profile.
 *
 * Make sure you use a valid color profile, according to the image's
 * type (e.g. RGB or grayscale color profiles respectively for RGB or
 * grayscale images).
 *
 * Returns: %TRUE on success.
 *
 * Since: 2.10
 **/
gboolean
gimp_image_convert_color_profile (GimpImage                 *image,
                                  GimpColorProfile          *profile,
                                  GimpColorRenderingIntent   intent,
                                  gboolean                   bpc)
{
  const guint8 *data   = NULL;
  GBytes       *bytes  = NULL;
  gboolean      ret;

  g_return_val_if_fail (profile == NULL || GIMP_IS_COLOR_PROFILE (profile),
                        FALSE);

  if (profile)
    {
      gsize l;

      data = gimp_color_profile_get_icc_profile (profile, &l);
      bytes = g_bytes_new_static (data, l);
    }

  ret = _gimp_image_convert_color_profile (image, bytes, intent, bpc);
  g_bytes_unref (bytes);
  return ret;
}

/**
 * gimp_image_convert_cmyk:
 * @image:   The image.
 * @profile: (nullable): The CMYK color profile of the converted image.
 * @intent:  Rendering intent.
 * @bpc:     Black point compensation.
 *
 * Converts an RGB, grayscale or indexed image to CMYK, using @profile
 * as the CMYK profile of the result, or the default CMYK profile (the
 * CMYK profile of the color management preferences, else GIMP's
 * built-in CMYK profile) if @profile is %NULL.
 *
 * @profile must be a CMYK profile usable in both directions, see
 * gimp_color_profile_is_cmyk_bidirectional(). The image's precision
 * becomes non-linear if it was linear, and legacy layer modes are
 * replaced by their non-legacy equivalent.
 *
 * Plug-ins that create CMYK images directly should rather use
 * gimp_image_new_with_precision() with %GIMP_CMYK, then immediately
 * gimp_image_set_color_profile() with a bidirectional CMYK profile,
 * then gimp_layer_new() with %GIMP_CMYK_IMAGE or %GIMP_CMYKA_IMAGE,
 * and write pixels in the format returned by gimp_drawable_get_format()
 * (or an encoding-compatible CMYK format with the same babl space).
 * Thumbnails and previews of CMYK drawables are sRGB.
 *
 * Returns: %TRUE on success.
 *
 * Since: 3.4
 **/
gboolean
gimp_image_convert_cmyk (GimpImage                *image,
                         GimpColorProfile         *profile,
                         GimpColorRenderingIntent  intent,
                         gboolean                  bpc)
{
  GBytes   *bytes = NULL;
  gboolean  ret;

  g_return_val_if_fail (GIMP_IS_IMAGE (image), FALSE);
  g_return_val_if_fail (profile == NULL || GIMP_IS_COLOR_PROFILE (profile),
                        FALSE);

  if (profile)
    {
      const guint8 *data;
      gsize         length;

      data  = gimp_color_profile_get_icc_profile (profile, &length);
      bytes = g_bytes_new_static (data, length);
    }

  ret = _gimp_image_convert_cmyk (image, bytes, intent, bpc);

  if (bytes)
    g_bytes_unref (bytes);

  return ret;
}

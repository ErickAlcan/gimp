/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
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

#include "config.h"

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gegl.h>

#include "libgimpconfig/gimpconfig.h"
#include "libgimpcolor/gimpcolor.h"

#include "core-types.h"

#include "config/gimpdialogconfig.h"

#include "gegl/gimp-babl.h"

#include "gimp.h"
#include "gimpdrawable.h"
#include "gimperror.h"
#include "gimpimage.h"
#include "gimpimage-color-profile.h"
#include "gimpimage-colormap.h"
#include "gimpimage-convert-type.h"
#include "gimpimage-undo.h"
#include "gimpimage-undo-push.h"
#include "gimplayer.h"
#include "gimpobjectqueue.h"
#include "gimpprogress.h"

#include "gimp-intl.h"


static gboolean gimp_image_convert_type_rgb_gray (GimpImage                *image,
                                                  GimpImageBaseType         new_type,
                                                  GimpColorProfile         *dest_profile,
                                                  GimpProgress             *progress,
                                                  GError                  **error);
static gboolean gimp_image_convert_type_cmyk     (GimpImage                *image,
                                                  GimpImageBaseType         new_type,
                                                  GimpColorProfile         *dest_profile,
                                                  GimpColorRenderingIntent  intent,
                                                  gboolean                  bpc,
                                                  GimpProgress             *progress,
                                                  GError                  **error);


/**
 * gimp_image_convert_type:
 * @image:        a #GimpImage
 * @new_type:     the new base type
 * @dest_profile: (nullable): the profile to convert to
 * @progress:     (nullable): a #GimpProgress
 * @error:        return location for a #GError
 *
 * Same as gimp_image_convert_type_full() with
 * %GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL and black point compensation.
 *
 * Returns: %TRUE on success.
 **/
gboolean
gimp_image_convert_type (GimpImage          *image,
                         GimpImageBaseType   new_type,
                         GimpColorProfile   *dest_profile,
                         GimpProgress       *progress,
                         GError            **error)
{
  return gimp_image_convert_type_full (image, new_type, dest_profile,
                                       GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                       TRUE, progress, error);
}

/**
 * gimp_image_convert_type_full:
 * @image:        a #GimpImage
 * @new_type:     the new base type
 * @dest_profile: (nullable): the profile to convert to, or %NULL for the
 *                default profile of @new_type
 * @intent:       the rendering intent of the conversion
 * @bpc:          whether to use black point compensation
 * @progress:     (nullable): a #GimpProgress
 * @error:        return location for a #GError
 *
 * Converts @image to @new_type.  Allowed are RGB/GRAY/INDEXED -> CMYK,
 * CMYK -> RGB/GRAY and RGB <-> GRAY; conversions to indexed go through
 * gimp_image_convert_indexed() (which refuses CMYK images).
 *
 * For conversions to CMYK, a %NULL @dest_profile means
 * gimp_get_default_cmyk_profile() with the image's simulation profile
 * as preferred profile; the precision is moved to its non-linear
 * variant in the same undo group.  For CMYK -> RGB/GRAY, a %NULL
 * @dest_profile means the preferred RGB/grayscale profile of the color
 * configuration, else the builtin profile.
 *
 * @intent and @bpc are honoured for conversions involving CMYK.
 * Conversions between RGB and grayscale keep their historic behaviour.
 *
 * Returns: %TRUE on success.
 **/
gboolean
gimp_image_convert_type_full (GimpImage                *image,
                              GimpImageBaseType         new_type,
                              GimpColorProfile         *dest_profile,
                              GimpColorRenderingIntent  intent,
                              gboolean                  bpc,
                              GimpProgress             *progress,
                              GError                  **error)
{
  g_return_val_if_fail (GIMP_IS_IMAGE (image), FALSE);
  g_return_val_if_fail (new_type != gimp_image_get_base_type (image), FALSE);
  g_return_val_if_fail (new_type != GIMP_INDEXED, FALSE);
  g_return_val_if_fail (dest_profile == NULL || GIMP_IS_COLOR_PROFILE (dest_profile),
                        FALSE);
  g_return_val_if_fail (progress == NULL || GIMP_IS_PROGRESS (progress), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (new_type == GIMP_CMYK ||
      gimp_image_get_base_type (image) == GIMP_CMYK)
    {
      return gimp_image_convert_type_cmyk (image, new_type, dest_profile,
                                           intent, bpc, progress, error);
    }

  g_return_val_if_fail (gimp_babl_is_valid (new_type,
                                            gimp_image_get_precision (image)),
                        FALSE);

  return gimp_image_convert_type_rgb_gray (image, new_type, dest_profile,
                                           progress, error);
}


/*  private functions  */

/*  the historic RGB <-> GRAY (and INDEXED -> RGB/GRAY) conversion,
 *  unchanged
 */
static gboolean
gimp_image_convert_type_rgb_gray (GimpImage          *image,
                                  GimpImageBaseType   new_type,
                                  GimpColorProfile   *dest_profile,
                                  GimpProgress       *progress,
                                  GError            **error)
{
  GimpColorProfile  *src_profile;
  GimpImageBaseType  old_type;
  const Babl        *new_layer_format;
  GimpObjectQueue   *queue;
  GList             *all_layers;
  GimpDrawable      *drawable;
  const gchar       *undo_desc = NULL;

  new_layer_format = gimp_babl_format (new_type,
                                       gimp_image_get_precision (image),
                                       TRUE,
                                       gimp_image_get_layer_space (image));

  if (dest_profile &&
      ! gimp_image_validate_color_profile_by_format (new_layer_format,
                                                     dest_profile,
                                                     NULL, error))
    {
      return FALSE;
    }

  switch (new_type)
    {
    case GIMP_RGB:
      undo_desc = C_("undo-type", "Convert Image to RGB");
      break;

    case GIMP_GRAY:
      undo_desc = C_("undo-type", "Convert Image to Grayscale");
      break;

    default:
      g_return_val_if_reached (FALSE);
    }

  gimp_set_busy (image->gimp);

  queue    = gimp_object_queue_new (progress);
  progress = GIMP_PROGRESS (queue);

  all_layers = gimp_image_get_layer_list (image);
  gimp_object_queue_push_list (queue, all_layers);
  g_list_free (all_layers);

  g_object_freeze_notify (G_OBJECT (image));

  gimp_image_undo_group_start (image, GIMP_UNDO_GROUP_IMAGE_CONVERT,
                               undo_desc);

  src_profile = gimp_color_managed_get_color_profile (GIMP_COLOR_MANAGED (image));

  /*  Push the image type to the stack  */
  gimp_image_undo_push_image_type (image, NULL);

  /*  Set the new base type  */
  old_type = gimp_image_get_base_type (image);

  g_object_set (image, "base-type", new_type, NULL);

  /*  When converting to/from GRAY, always convert to the new type's
   *  builtin profile as a fallback, we need one for convert_type on
   *  the same image
   */
  if (old_type == GIMP_GRAY ||
      new_type == GIMP_GRAY)
    {
      if (! dest_profile)
        dest_profile = gimp_image_get_builtin_color_profile (image);
    }

  while ((drawable = gimp_object_queue_pop (queue)))
    {
      gimp_drawable_convert_type (drawable, image,
                                  new_type,
                                  gimp_drawable_get_precision (drawable),
                                  gimp_drawable_has_alpha (drawable),
                                  src_profile,
                                  dest_profile,
                                  GEGL_DITHER_NONE, GEGL_DITHER_NONE,
                                  TRUE, progress);
    }

  if (old_type == GIMP_INDEXED)
    gimp_image_unset_colormap (image, TRUE);

  /*  When converting to/from GRAY, set the new profile.
   */
  if (old_type == GIMP_GRAY ||
      new_type == GIMP_GRAY)
    {
      gimp_image_set_color_profile (image, dest_profile, NULL);
    }

  gimp_image_undo_group_end (image);

  gimp_image_mode_changed (image);
  g_object_thaw_notify (G_OBJECT (image));

  g_object_unref (queue);

  gimp_unset_busy (image->gimp);

  return TRUE;
}

/*  returns a new reference to the profile a CMYK conversion converts
 *  to, or NULL with @error set if @dest_profile can't be used
 */
static GimpColorProfile *
gimp_image_convert_type_get_dest_profile (GimpImage          *image,
                                          GimpImageBaseType   new_type,
                                          GimpColorProfile   *dest_profile,
                                          GError            **error)
{
  GimpColorConfig  *config = image->gimp->config->color_management;
  GimpColorProfile *profile;
  const Babl       *format;

  format = gimp_babl_format (new_type, GIMP_PRECISION_U16_NON_LINEAR,
                             TRUE, NULL);

  if (dest_profile)
    {
      if (! gimp_image_validate_color_profile_by_format (format, dest_profile,
                                                         NULL, error))
        return NULL;

      return g_object_ref (dest_profile);
    }

  switch (new_type)
    {
    case GIMP_CMYK:
      {
        GimpColorProfile *simulation;

        /*  D3: the image's simulation profile is the preferred target
         *  if it is usable for CMYK images
         */
        simulation = gimp_image_get_simulation_profile (image);

        if (simulation && ! gimp_color_profile_is_cmyk (simulation))
          simulation = NULL;

        return gimp_get_default_cmyk_profile (image->gimp, simulation, NULL);
      }

    case GIMP_RGB:
      profile = gimp_color_config_get_rgb_color_profile (config, NULL);
      break;

    case GIMP_GRAY:
      profile = gimp_color_config_get_gray_color_profile (config, NULL);
      break;

    default:
      g_return_val_if_reached (NULL);
    }

  if (profile &&
      ! gimp_image_validate_color_profile_by_format (format, profile,
                                                     NULL, NULL))
    {
      g_clear_object (&profile);
    }

  if (! profile)
    profile = g_object_ref (gimp_babl_get_builtin_color_profile (new_type,
                                                                 GIMP_TRC_NON_LINEAR));

  return profile;
}

static GimpPrecision
gimp_image_convert_type_non_linear (GimpPrecision precision)
{
  return gimp_babl_precision (gimp_babl_component_type (precision),
                              GIMP_TRC_NON_LINEAR);
}

/*  INTERFACES §6 step 6; same as gimp_image_fix_layer_format_spaces()
 *  in gimpimage-color-profile.c, which is static
 */
static void
gimp_image_convert_type_fix_layer_spaces (GimpImage *image)
{
  GList *layers;
  GList *list;

  layers = gimp_image_get_layer_list (image);

  for (list = layers; list; list = g_list_next (list))
    {
      if (! gimp_viewable_get_children (list->data))
        gimp_layer_fix_format_space (list->data, TRUE, TRUE);
    }

  g_list_free (layers);
}

/*  conversions to and from CMYK, undo push order per INTERFACES §6  */
static gboolean
gimp_image_convert_type_cmyk (GimpImage                *image,
                              GimpImageBaseType         new_type,
                              GimpColorProfile         *dest_profile,
                              GimpColorRenderingIntent  intent,
                              gboolean                  bpc,
                              GimpProgress             *progress,
                              GError                  **error)
{
  GimpColorProfile  *src_profile;
  GimpColorProfile  *target_profile;
  GimpImageBaseType  old_type;
  GimpPrecision      old_precision;
  GimpPrecision      new_precision;
  GimpObjectQueue   *queue;
  GList             *all_layers;
  GList             *list;
  GimpDrawable      *drawable;
  const gchar       *undo_desc = NULL;

  old_type = gimp_image_get_base_type (image);

  if (new_type == GIMP_INDEXED)
    {
      /*  never reached through the API above, kept for clarity  */
      g_set_error_literal (error, GIMP_ERROR, GIMP_FAILED,
                           _("Cannot convert a CMYK image to indexed. "
                             "Convert the image to RGB first."));
      return FALSE;
    }

  if (new_type == GIMP_CMYK && ! gimp_babl_has_cmyk_support ())
    {
      g_set_error_literal (error, GIMP_ERROR, GIMP_FAILED,
                           _("CMYK images are not supported: babl was "
                             "built without LCMS support"));
      return FALSE;
    }

  target_profile = gimp_image_convert_type_get_dest_profile (image, new_type,
                                                             dest_profile,
                                                             error);
  if (! target_profile)
    return FALSE;

  switch (new_type)
    {
    case GIMP_RGB:
      undo_desc = C_("undo-type", "Convert Image to RGB");
      break;

    case GIMP_GRAY:
      undo_desc = C_("undo-type", "Convert Image to Grayscale");
      break;

    case GIMP_CMYK:
      undo_desc = C_("undo-type", "Convert Image to CMYK");
      break;

    default:
      g_object_unref (target_profile);
      g_return_val_if_reached (FALSE);
    }

  old_precision = gimp_image_get_precision (image);
  new_precision = old_precision;

  /*  I2: CMYK images only use non-linear precisions  */
  if (new_type == GIMP_CMYK)
    new_precision = gimp_image_convert_type_non_linear (old_precision);

  gimp_set_busy (image->gimp);

  g_object_freeze_notify (G_OBJECT (image));

  gimp_image_undo_group_start (image, GIMP_UNDO_GROUP_IMAGE_CONVERT,
                               undo_desc);

  /*  Indexed -> CMYK: indexed -> RGB first, within the same group  */
  if (old_type == GIMP_INDEXED)
    {
      gimp_image_convert_type_rgb_gray (image, GIMP_RGB, NULL, NULL, NULL);

      old_type = GIMP_RGB;
    }

  /*  step 1: no layer of a CMYK image has a legacy mode (I9), map them
   *  while the image still has its old base type
   */
  if (new_type == GIMP_CMYK)
    {
      all_layers = gimp_image_get_layer_list (image);

      for (list = all_layers; list; list = g_list_next (list))
        gimp_layer_map_legacy_mode (list->data, TRUE);

      g_list_free (all_layers);
    }

  /*  the source profile, captured before the base type changes  */
  src_profile = gimp_color_managed_get_color_profile (GIMP_COLOR_MANAGED (image));
  src_profile = g_object_ref (src_profile);

  /*  step 2: precision  */
  if (new_precision != old_precision)
    {
      gimp_image_undo_push_image_precision (image, NULL);

      g_object_set (image, "precision", new_precision, NULL);
    }

  /*  step 3: base type  */
  gimp_image_undo_push_image_type (image, NULL);

  g_object_set (image, "base-type", new_type, NULL);

  /*  step 4: the new profile, validated against the new base type  */
  if (! gimp_image_set_color_profile (image, target_profile, NULL))
    g_warning ("%s: setting the target profile failed", G_STRFUNC);

  /*  step 5: convert all drawables with explicit profiles  */
  _gimp_image_set_conversion_intent (image, intent, bpc);

  queue    = gimp_object_queue_new (progress);
  progress = GIMP_PROGRESS (queue);

  all_layers = gimp_image_get_layer_list (image);
  gimp_object_queue_push_list (queue, all_layers);
  g_list_free (all_layers);

  while ((drawable = gimp_object_queue_pop (queue)))
    {
      gimp_drawable_convert_type (drawable, image,
                                  new_type,
                                  new_precision,
                                  gimp_drawable_has_alpha (drawable),
                                  src_profile,
                                  target_profile,
                                  GEGL_DITHER_NONE, GEGL_DITHER_NONE,
                                  TRUE, progress);
    }

  g_object_unref (queue);

  /*  channels only change if the precision change changed their format
   *  (U8 linear -> U8 non-linear)
   */
  if (new_precision != old_precision)
    {
      GList *channels = gimp_image_get_channel_list (image);

      for (list = channels; list; list = g_list_next (list))
        {
          GimpDrawable *channel = list->data;

          if (gimp_drawable_get_format (channel) !=
              gimp_image_get_channel_format (image))
            {
              gimp_drawable_convert_type (channel, image,
                                          GIMP_GRAY, new_precision,
                                          FALSE, NULL, NULL,
                                          GEGL_DITHER_NONE, GEGL_DITHER_NONE,
                                          TRUE, NULL);
            }
        }

      g_list_free (channels);
    }

  _gimp_image_set_conversion_intent (image,
                                     GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                     TRUE);

  /*  step 6: safety net  */
  gimp_image_convert_type_fix_layer_spaces (image);

  gimp_image_undo_group_end (image);

  if (new_precision != old_precision)
    gimp_image_precision_changed (image);

  gimp_image_mode_changed (image);
  g_object_thaw_notify (G_OBJECT (image));

  g_object_unref (src_profile);
  g_object_unref (target_profile);

  gimp_unset_busy (image->gimp);

  return TRUE;
}

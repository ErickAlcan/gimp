/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimppickable.c
 * Copyright (C) 2004  Michael Natterer <mitch@gimp.org>
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

/* This file contains an interface for pixel objects that their color at
 * a given position can be picked. Also included is a utility for
 * sampling an average area (which uses the implemented picking
 * functions).
 */

#include "config.h"

#include <string.h>

#include <cairo.h>
#include <gegl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"
#include "libgimpmath/gimpmath.h"

#include "core-types.h"

#include "gegl/gimp-babl.h"

#include "gimpobject.h"
#include "gimpimage.h"
#include "gimppickable.h"


/*  local function prototypes  */

static void   gimp_pickable_real_get_pixel_average (GimpPickable          *pickable,
                                                    const GeglRectangle   *rect,
                                                    const Babl            *format,
                                                    gpointer               pixel);


G_DEFINE_INTERFACE (GimpPickable, gimp_pickable, GIMP_TYPE_OBJECT)


/*  private functions  */


static void
gimp_pickable_default_init (GimpPickableInterface *iface)
{
  iface->get_pixel_average = gimp_pickable_real_get_pixel_average;

  g_object_interface_install_property (iface,
                                       g_param_spec_object ("buffer",
                                                            NULL, NULL,
                                                            GEGL_TYPE_BUFFER,
                                                            GIMP_PARAM_READABLE));
}

static void
gimp_pickable_real_get_pixel_average (GimpPickable        *pickable,
                                      const GeglRectangle *rect,
                                      const Babl          *format,
                                      gpointer             pixel)
{
  const Babl *average_format;
  gdouble     average[MAX_CHANNELS] = { 0, };
  gint        n_components;
  gint        n          = 0;
  gint        x;
  gint        y;
  gint        c;

  /*  average premultiplied, in the native color model: CMYK pixels are
   *  averaged as inks (m12), not through RGB
   */
  if (gimp_babl_format_is_cmyk (format))
    average_format = babl_format_with_space ("CaMaYaKaA double", format);
  else
    average_format = babl_format_with_space ("RaGaBaA double",
                                             babl_format_get_space (format));

  n_components = babl_format_get_n_components (average_format);

  for (y = rect->y; y < rect->y + rect->height; y++)
    {
      for (x = rect->x; x < rect->x + rect->width; x++)
        {
          gdouble sample[MAX_CHANNELS];

          if (gimp_pickable_get_pixel_at (pickable,
                                          x, y, average_format, sample))
            {
              for (c = 0; c < n_components; c++)
                average[c] += sample[c];

              n++;
            }
        }
    }

  if (n > 0)
    {
      for (c = 0; c < n_components; c++)
        average[c] /= n;
    }

  babl_process (babl_fish (average_format, format), average, pixel, 1);
}


/*  public functions  */


void
gimp_pickable_flush (GimpPickable *pickable)
{
  GimpPickableInterface *pickable_iface;

  g_return_if_fail (GIMP_IS_PICKABLE (pickable));

  pickable_iface = GIMP_PICKABLE_GET_IFACE (pickable);

  if (pickable_iface->flush)
    pickable_iface->flush (pickable);
}

GimpImage *
gimp_pickable_get_image (GimpPickable *pickable)
{
  GimpPickableInterface *pickable_iface;

  g_return_val_if_fail (GIMP_IS_PICKABLE (pickable), NULL);

  pickable_iface = GIMP_PICKABLE_GET_IFACE (pickable);

  if (pickable_iface->get_image)
    return pickable_iface->get_image (pickable);

  return NULL;
}

const Babl *
gimp_pickable_get_format (GimpPickable *pickable)
{
  GimpPickableInterface *pickable_iface;

  g_return_val_if_fail (GIMP_IS_PICKABLE (pickable), NULL);

  pickable_iface = GIMP_PICKABLE_GET_IFACE (pickable);

  if (pickable_iface->get_format)
    return pickable_iface->get_format (pickable);

  return NULL;
}

const Babl *
gimp_pickable_get_format_with_alpha (GimpPickable *pickable)
{
  GimpPickableInterface *pickable_iface;

  g_return_val_if_fail (GIMP_IS_PICKABLE (pickable), NULL);

  pickable_iface = GIMP_PICKABLE_GET_IFACE (pickable);

  if (pickable_iface->get_format_with_alpha)
    return pickable_iface->get_format_with_alpha (pickable);

  return NULL;
}

GeglBuffer *
gimp_pickable_get_buffer (GimpPickable *pickable)
{
  GimpPickableInterface *pickable_iface;

  g_return_val_if_fail (GIMP_IS_PICKABLE (pickable), NULL);

  pickable_iface = GIMP_PICKABLE_GET_IFACE (pickable);

  if (pickable_iface->get_buffer)
    return pickable_iface->get_buffer (pickable);

  return NULL;
}

GeglBuffer *
gimp_pickable_get_buffer_with_effects (GimpPickable *pickable)
{
  GimpPickableInterface *pickable_iface;

  g_return_val_if_fail (GIMP_IS_PICKABLE (pickable), NULL);

  pickable_iface = GIMP_PICKABLE_GET_IFACE (pickable);

  if (pickable_iface->get_buffer_with_effects)
    return pickable_iface->get_buffer_with_effects (pickable);
  else if (pickable_iface->get_buffer)
    return g_object_ref (pickable_iface->get_buffer (pickable));

  return NULL;
}

gboolean
gimp_pickable_get_pixel_at (GimpPickable *pickable,
                            gint          x,
                            gint          y,
                            const Babl   *format,
                            gpointer      pixel)
{
  GimpPickableInterface *pickable_iface;

  g_return_val_if_fail (GIMP_IS_PICKABLE (pickable), FALSE);
  g_return_val_if_fail (pixel != NULL, FALSE);

  if (! format)
    format = gimp_pickable_get_format (pickable);

  pickable_iface = GIMP_PICKABLE_GET_IFACE (pickable);

  if (pickable_iface->get_pixel_at)
    return pickable_iface->get_pixel_at (pickable, x, y, format, pixel);

  return FALSE;
}

void
gimp_pickable_get_pixel_average (GimpPickable        *pickable,
                                 const GeglRectangle *rect,
                                 const Babl          *format,
                                 gpointer             pixel)
{
  GimpPickableInterface *pickable_iface;

  g_return_if_fail (GIMP_IS_PICKABLE (pickable));
  g_return_if_fail (rect != NULL);
  g_return_if_fail (pixel != NULL);

  if (! format)
    format = gimp_pickable_get_format (pickable);

  pickable_iface = GIMP_PICKABLE_GET_IFACE (pickable);

  /*  m12: CMYK is averaged as inks.  The pickables' get_pixel_average()
   *  implementations use gimp_gegl_average_color(), which averages in
   *  RGB, so CMYK is handled here, on the pickable's buffer.
   */
  if (gimp_babl_format_is_cmyk (format) && gimp_pickable_get_buffer (pickable))
    gimp_pickable_average_color_native (gimp_pickable_get_buffer (pickable),
                                        rect, TRUE, format, pixel);
  else if (pickable_iface->get_pixel_average)
    pickable_iface->get_pixel_average (pickable, rect, format, pixel);
  else
    memset (pixel, 0, babl_format_get_bytes_per_pixel (format));
}

/**
 * gimp_pickable_average_color_native:
 * @buffer:         a #GeglBuffer with a CMYK (or any) format
 * @rect:           the area to average
 * @clip_to_buffer: whether to clip @rect to @buffer's extent; if %FALSE
 *                  pixels outside count as transparent
 * @format:         the format of @pixel
 * @pixel:          (out caller-allocates): the average, in @format
 *
 * Averages @rect premultiplied in the native color model of @format
 * ("CaMaYaKaA double" in @format's space for CMYK, "RaGaBaA double"
 * otherwise), without a detour through another color model.
 **/
void
gimp_pickable_average_color_native (GeglBuffer          *buffer,
                                    const GeglRectangle *rect,
                                    gboolean             clip_to_buffer,
                                    const Babl          *format,
                                    gpointer             pixel)
{
  const Babl         *average_format;
  GeglBufferIterator *iter;
  GeglRectangle       roi;
  gdouble             sum[MAX_CHANNELS] = { 0, };
  gint64              n = 0;
  gint                n_components;
  gint                c;

  g_return_if_fail (GEGL_IS_BUFFER (buffer));
  g_return_if_fail (rect != NULL);
  g_return_if_fail (format != NULL);
  g_return_if_fail (pixel != NULL);

  if (gimp_babl_format_is_cmyk (format))
    average_format = babl_format_with_space ("CaMaYaKaA double", format);
  else
    average_format = babl_format_with_space ("RaGaBaA double",
                                             babl_format_get_space (format));

  n_components = babl_format_get_n_components (average_format);

  if (clip_to_buffer)
    gegl_rectangle_intersect (&roi, rect, gegl_buffer_get_extent (buffer));
  else
    roi = *rect;

  if (roi.width > 0 && roi.height > 0)
    {
      iter = gegl_buffer_iterator_new (buffer, &roi, 0, average_format,
                                       GEGL_ACCESS_READ, GEGL_ABYSS_NONE, 1);

      while (gegl_buffer_iterator_next (iter))
        {
          const gdouble *p = iter->items[0].data;
          gint           i;

          for (i = 0; i < iter->length; i++)
            {
              for (c = 0; c < n_components; c++)
                sum[c] += p[c];

              p += n_components;
            }

          n += iter->length;
        }
    }

  if (n > 0)
    {
      for (c = 0; c < n_components; c++)
        sum[c] /= n;
    }

  babl_process (babl_fish (average_format, format), sum, pixel, 1);
}

GeglColor *
gimp_pickable_get_color_at (GimpPickable *pickable,
                            gint          x,
                            gint          y)
{
  GeglColor *color = NULL;
  gdouble    pixel[MAX_CHANNELS]; /* I10: native format, up to CMYKA double */

  g_return_val_if_fail (GIMP_IS_PICKABLE (pickable), FALSE);

  if (gimp_pickable_get_pixel_at (pickable, x, y, NULL, pixel))
    {
      color = gegl_color_new ("black");
      gegl_color_set_pixel (color, gimp_pickable_get_format (pickable), pixel);
    }

  return color;
}

gdouble
gimp_pickable_get_opacity_at (GimpPickable *pickable,
                              gint          x,
                              gint          y)
{
  GimpPickableInterface *pickable_iface;

  g_return_val_if_fail (GIMP_IS_PICKABLE (pickable), GIMP_OPACITY_TRANSPARENT);

  pickable_iface = GIMP_PICKABLE_GET_IFACE (pickable);

  if (pickable_iface->get_opacity_at)
    return pickable_iface->get_opacity_at (pickable, x, y);

  return GIMP_OPACITY_TRANSPARENT;
}

/*  @pixel (nullable) receives the pixel in gimp_pickable_get_format();
 *  it must have room for that format, i.e. gdouble[MAX_CHANNELS] (I10).
 *  With @sample_average, @color is the average over the radius,
 *  computed in the pickable's color model (CMYK inks for CMYK).
 */
gboolean
gimp_pickable_pick_color (GimpPickable *pickable,
                          gint          x,
                          gint          y,
                          gboolean      sample_average,
                          gdouble       average_radius,
                          gpointer      pixel,
                          GeglColor   **color)
{
  const Babl *format;
  gdouble     sample[MAX_CHANNELS]; /* I10: native format, up to CMYKA double */

  g_return_val_if_fail (GIMP_IS_PICKABLE (pickable), FALSE);
  g_return_val_if_fail (color != NULL && GEGL_IS_COLOR (*color), FALSE);

  format = gimp_pickable_get_format (pickable);

  if (! gimp_pickable_get_pixel_at (pickable, x, y, format, sample))
    return FALSE;

  if (pixel)
    memcpy (pixel, sample, babl_format_get_bytes_per_pixel (format));

  if (sample_average)
    {
      gint radius = floor (average_radius);

      gimp_pickable_get_pixel_average (pickable,
                                       GEGL_RECTANGLE (x - radius,
                                                       y - radius,
                                                       2 * radius + 1,
                                                       2 * radius + 1),
                                       format, sample);
    }

  gegl_color_set_pixel (*color, format, sample);

  return TRUE;
}

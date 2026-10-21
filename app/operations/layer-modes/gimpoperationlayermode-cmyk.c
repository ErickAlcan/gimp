/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimpoperationlayermode-cmyk.c
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

/* The CMYK <-> blend space converter used by GimpOperationLayerMode when
 * it composites in CMYK (devel-docs/cmyk-mode/INTERFACES.md §8.5).
 *
 * Blend modes blend in an RGB/Lab space: the backdrop and the layer are
 * converted from "CMYKA float" (image space) to the op's blend format
 * (sRGB based, NULL babl space, I5), blended with 4 components, and the
 * result is converted back.  The color conversion runs through two
 * GimpColorTransforms per CMYK space, "CMYKA u16" <-> "R'G'B'A u16" sRGB,
 * relative colorimetric with black point compensation.  With lcms (the
 * GimpColorTransform CMYK path of WS01b) these are precomputed 16 bit
 * device links, an order of magnitude faster than babl's reference fish.
 * If the transforms cannot be created, babl fishes are used directly.
 */

#include "config.h"

#include <gio/gio.h>
#include <gegl.h>
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"

#include "../operations-types.h"

#include "gimpoperationlayermode-cmyk.h"


/* pixels converted per step, bounds the stack buffers below */
#define CHUNK 512

#define N_BLEND_FORMATS 4


struct _GimpLayerModeCmykConverter
{
  const Babl         *space;

  const Babl         *cmyka_float;   /* CMYKA float, space */
  const Babl         *cmyka_u16;     /* CMYKA u16,   space */
  const Babl         *rgba_u16;      /* R'G'B'A u16, sRGB  */

  GimpColorTransform *to_rgb;
  GimpColorTransform *from_rgb;


  const Babl         *blend_formats[N_BLEND_FORMATS];

  /* indexed like blend_formats[] */
  const Babl         *u16_to_blend[N_BLEND_FORMATS];
  const Babl         *blend_to_u16[N_BLEND_FORMATS];

  /* fallback (no transforms): direct babl fishes */
  const Babl         *cmyk_to_blend[N_BLEND_FORMATS];
  const Babl         *blend_to_cmyk[N_BLEND_FORMATS];
};


static const gchar *blend_format_names[N_BLEND_FORMATS] =
{
  "RGBA float",
  "R'G'B'A float",
  "R~G~B~A float",
  "CIE Lab alpha float"
};

static GHashTable *converters       = NULL;
static GMutex      converters_mutex;


static void
converter_free (GimpLayerModeCmykConverter *conv)
{
  g_clear_object (&conv->to_rgb);
  g_clear_object (&conv->from_rgb);

  g_free (conv);
}

static GimpLayerModeCmykConverter *
converter_new (const Babl *space)
{
  GimpLayerModeCmykConverter *conv;
  GimpColorProfile           *cmyk_profile = NULL;
  GimpColorProfile           *srgb_profile;
  const char                 *icc;
  int                         icc_length   = 0;
  gint                        i;

  conv = g_new0 (GimpLayerModeCmykConverter, 1);

  conv->space       = space;
  conv->cmyka_float = babl_format_with_space ("CMYKA float", space);
  conv->cmyka_u16   = babl_format_with_space ("CMYKA u16",   space);
  conv->rgba_u16    = babl_format ("R'G'B'A u16");


  for (i = 0; i < N_BLEND_FORMATS; i++)
    {
      const Babl *blend = babl_format (blend_format_names[i]);

      conv->blend_formats[i] = blend;

      conv->u16_to_blend[i]  = babl_fish (conv->rgba_u16, blend);
      conv->blend_to_u16[i]  = babl_fish (blend, conv->rgba_u16);
      conv->cmyk_to_blend[i] = babl_fish (conv->cmyka_float, blend);
      conv->blend_to_cmyk[i] = babl_fish (blend, conv->cmyka_float);
    }

  icc = babl_space_get_icc (space, &icc_length);

  if (icc && icc_length > 0)
    {
      GError *error = NULL;

      cmyk_profile = gimp_color_profile_new_from_icc_profile ((const guint8 *) icc,
                                                              icc_length,
                                                              &error);
      if (! cmyk_profile)
        {
          g_printerr ("%s: %s\n", G_STRFUNC, error->message);
          g_clear_error (&error);
        }
    }

  srgb_profile = gimp_color_profile_new_rgb_srgb ();

  if (cmyk_profile && gimp_color_profile_is_cmyk (cmyk_profile))
    {
      GimpColorTransformFlags flags;

      flags = GIMP_COLOR_TRANSFORM_FLAGS_BLACK_POINT_COMPENSATION;

      conv->to_rgb =
        gimp_color_transform_new (cmyk_profile, conv->cmyka_u16,
                                  srgb_profile, conv->rgba_u16,
                                  GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                  flags);

      conv->from_rgb =
        gimp_color_transform_new (srgb_profile, conv->rgba_u16,
                                  cmyk_profile, conv->cmyka_u16,
                                  GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                  flags);
    }

  if (! conv->to_rgb || ! conv->from_rgb)
    {
      g_clear_object (&conv->to_rgb);
      g_clear_object (&conv->from_rgb);
    }

  g_clear_object (&cmyk_profile);
  g_object_unref (srgb_profile);

  return conv;
}

/* CMYKA float <-> CMYKA u16 with babl's rounding.  babl has no fast path
 * for CMYK formats with a space; its reference fish is serialized by a
 * global mutex, which made the converter not scale with threads.
 */
static inline void
cmyka_float_to_u16 (const gfloat *src,
                    guint16      *dest,
                    gint          n)
{
  n *= 5;

  while (n--)
    {
      gdouble d = *src++;

      if (d < 0.0)
        *dest++ = 0;
      else if (d > 1.0)
        *dest++ = 65535;
      else
        *dest++ = d * 65535.0 + 0.5;
    }
}

static inline void
cmyka_u16_to_float (const guint16 *src,
                    gfloat        *dest,
                    gint           n)
{
  n *= 5;

  while (n--)
    *dest++ = (gfloat) ((gdouble) *src++ / 65535.0);
}

static inline void
copy_alpha (const gfloat *src,
            gint          src_n,
            gfloat       *dest,
            gint          dest_n,
            gint          n)
{
  src  += src_n  - 1;
  dest += dest_n - 1;

  while (n--)
    {
      *dest = *src;

      src  += src_n;
      dest += dest_n;
    }
}

static gint
blend_format_index (const GimpLayerModeCmykConverter *conv,
                    const Babl                       *blend_format)
{
  gint i;

  for (i = 0; i < N_BLEND_FORMATS; i++)
    {
      if (blend_format == conv->blend_formats[i])
        return i;
    }

  g_return_val_if_reached (1);
}


/*  public functions  */

/**
 * gimp_layer_mode_cmyk_converter_get:
 * @cmyk_space: a CMYK babl space
 *
 * Returns: the converter for @cmyk_space, from a process-wide cache.
 *          Never %NULL for a CMYK space.
 **/
const GimpLayerModeCmykConverter *
gimp_layer_mode_cmyk_converter_get (const Babl *cmyk_space)
{
  GimpLayerModeCmykConverter *conv;

  g_return_val_if_fail (cmyk_space != NULL, NULL);

  g_mutex_lock (&converters_mutex);

  if (! converters)
    converters = g_hash_table_new_full (NULL, NULL, NULL,
                                        (GDestroyNotify) converter_free);

  conv = g_hash_table_lookup (converters, cmyk_space);

  if (! conv)
    {
      conv = converter_new (cmyk_space);

      g_hash_table_insert (converters, (gpointer) cmyk_space, conv);
    }

  g_mutex_unlock (&converters_mutex);

  return conv;
}

/**
 * gimp_layer_mode_cmyk_converter_to_blend:
 * @conv:         a converter
 * @blend_format: the (sRGB based) blend format
 * @cmyka:        @n pixels of "CMYKA float" in the converter's space
 * @blend:        (out caller-allocates): @n pixels in @blend_format
 * @n:            the number of pixels
 *
 * Converts CMYKA pixels to the blend format.  Alpha is preserved.
 **/
void
gimp_layer_mode_cmyk_converter_to_blend (const GimpLayerModeCmykConverter *conv,
                                         const Babl                       *blend_format,
                                         const gfloat                     *cmyka,
                                         gfloat                           *blend,
                                         gint                              n)
{
  gint index = blend_format_index (conv, blend_format);

  if (! conv->to_rgb)
    {
      babl_process (conv->cmyk_to_blend[index], cmyka, blend, n);
      return;
    }

  while (n > 0)
    {
      guint16 cmyk16[CHUNK * 5];
      guint16 rgb16[CHUNK * 4];
      gint    k = MIN (n, CHUNK);

      cmyka_float_to_u16 (cmyka, cmyk16, k);

      gimp_color_transform_process_pixels (conv->to_rgb,
                                           conv->cmyka_u16, cmyk16,
                                           conv->rgba_u16,  rgb16,
                                           k);

      babl_process (conv->u16_to_blend[index], rgb16, blend, k);

      /* keep alpha exact, not quantized to 16 bits */
      copy_alpha (cmyka, 5, blend, 4, k);

      cmyka += 5 * k;
      blend += 4 * k;
      n     -= k;
    }
}

/**
 * gimp_layer_mode_cmyk_converter_from_blend:
 * @conv:         a converter
 * @blend_format: the (sRGB based) blend format
 * @blend:        @n pixels in @blend_format
 * @cmyka:        (out caller-allocates): @n pixels of "CMYKA float" in the
 *                converter's space
 * @n:            the number of pixels
 *
 * Converts blend results back to CMYKA.  Alpha is preserved.
 **/
void
gimp_layer_mode_cmyk_converter_from_blend (const GimpLayerModeCmykConverter *conv,
                                           const Babl                       *blend_format,
                                           const gfloat                     *blend,
                                           gfloat                           *cmyka,
                                           gint                              n)
{
  gint index = blend_format_index (conv, blend_format);

  if (! conv->from_rgb)
    {
      babl_process (conv->blend_to_cmyk[index], blend, cmyka, n);
      return;
    }

  while (n > 0)
    {
      guint16 cmyk16[CHUNK * 5];
      guint16 rgb16[CHUNK * 4];
      gint    k = MIN (n, CHUNK);

      babl_process (conv->blend_to_u16[index], blend, rgb16, k);

      gimp_color_transform_process_pixels (conv->from_rgb,
                                           conv->rgba_u16,  rgb16,
                                           conv->cmyka_u16, cmyk16,
                                           k);

      cmyka_u16_to_float (cmyk16, cmyka, k);

      copy_alpha (blend, 4, cmyka, 5, k);

      blend += 4 * k;
      cmyka += 5 * k;
      n     -= k;
    }
}

/**
 * gimp_layer_mode_cmyk_converter_uses_transforms:
 * @conv: a converter
 *
 * Returns: %TRUE if @conv converts through #GimpColorTransforms, %FALSE
 *          if it fell back to babl fishes.
 **/
gboolean
gimp_layer_mode_cmyk_converter_uses_transforms (const GimpLayerModeCmykConverter *conv)
{
  g_return_val_if_fail (conv != NULL, FALSE);

  return conv->to_rgb != NULL;
}

void
gimp_layer_mode_cmyk_converters_exit (void)
{
  g_mutex_lock (&converters_mutex);
  g_clear_pointer (&converters, g_hash_table_unref);
  g_mutex_unlock (&converters_mutex);
}

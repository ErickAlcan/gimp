/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimpoperationlayermode-cmyk.h
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

#pragma once


/*  CMYK <-> blend space converter of the layer-mode op
 *  (devel-docs/cmyk-mode/INTERFACES.md §8.5)
 */

typedef struct _GimpLayerModeCmykConverter GimpLayerModeCmykConverter;


const GimpLayerModeCmykConverter *
     gimp_layer_mode_cmyk_converter_get        (const Babl                       *cmyk_space);

void gimp_layer_mode_cmyk_converter_to_blend   (const GimpLayerModeCmykConverter *conv,
                                                const Babl                       *blend_format,
                                                const gfloat                     *cmyka,
                                                gfloat                           *blend,
                                                gint                              n);
void gimp_layer_mode_cmyk_converter_from_blend (const GimpLayerModeCmykConverter *conv,
                                                const Babl                       *blend_format,
                                                const gfloat                     *blend,
                                                gfloat                           *cmyka,
                                                gint                              n);

gboolean gimp_layer_mode_cmyk_converter_uses_transforms
                                               (const GimpLayerModeCmykConverter *conv);

void gimp_layer_mode_cmyk_converters_exit      (void);

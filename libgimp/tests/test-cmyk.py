#!/usr/bin/env python3

# libgimp CMYK tests L (devel-docs/cmyk-mode/TESTING.md), owner WS05.
#
# Covers the PDB / libgimp surface of CMYK images: enum values in the
# GIR, image and layer creation rules, CMYK queries, component and
# histogram channel validation, thumbnails, color profile rules,
# gimp_image_convert_cmyk() argument validation, conversions and export
# options.
#
# Every check is strict: the pending-integration gates on WS03 were
# removed by WS09 after the integration of all workstreams.

def new_or_none(ctor, *args):
  # PyGObject raises TypeError when a constructor returns NULL.
  try:
    return ctor(*args)
  except TypeError:
    return None

pdb = Gimp.get_pdb()

# 1. Enums (GIR) ----------------------------------------------------------

gimp_assert('Gimp.ImageBaseType.CMYK == 3', int(Gimp.ImageBaseType.CMYK) == 3)
gimp_assert('Gimp.ImageType.CMYK_IMAGE == 6', int(Gimp.ImageType.CMYK_IMAGE) == 6)
gimp_assert('Gimp.ImageType.CMYKA_IMAGE == 7', int(Gimp.ImageType.CMYKA_IMAGE) == 7)
gimp_assert('Gimp.ChannelType.CYAN == 6', int(Gimp.ChannelType.CYAN) == 6)
gimp_assert('Gimp.ChannelType.MAGENTA == 7', int(Gimp.ChannelType.MAGENTA) == 7)
gimp_assert('Gimp.ChannelType.YELLOW == 8', int(Gimp.ChannelType.YELLOW) == 8)
gimp_assert('Gimp.ChannelType.BLACK == 9', int(Gimp.ChannelType.BLACK) == 9)
gimp_assert('Gimp.ExportCapabilities.CAN_HANDLE_CMYK == 1 << 11',
            int(Gimp.ExportCapabilities.CAN_HANDLE_CMYK) == 1 << 11)
gimp_assert('Gimp.HistogramChannel.CYAN == 7', int(Gimp.HistogramChannel.CYAN) == 7)
gimp_assert('Gimp.HistogramChannel.BLACK == 10', int(Gimp.HistogramChannel.BLACK) == 10)

# 2. New procedures and libgimp API ---------------------------------------

# gimp-image-convert-cmyk is private (wrapped by Gimp.Image.convert_cmyk),
# so the PDB does not report it as existing.
gimp_assert('gimp-image-convert-cmyk is private',
            not pdb.procedure_exists('gimp-image-convert-cmyk'))
gimp_assert('gimp-drawable-is-cmyk exists',
            pdb.procedure_exists('gimp-drawable-is-cmyk'))
gimp_assert('Gimp.Image.convert_cmyk', hasattr(Gimp.Image, 'convert_cmyk'))
gimp_assert('Gimp.Drawable.is_cmyk', hasattr(Gimp.Drawable, 'is_cmyk'))

# 3. Image creation -------------------------------------------------------

W, H = 32, 24

image = new_or_none(Gimp.Image.new, W, H, Gimp.ImageBaseType.CMYK)

# This suite runs with CMYK support (lcms in babl); the no-support path
# is covered by app/tests/test-cmyk-nosupport.c.
gimp_assert('Gimp.Image.new(CMYK) succeeds', image is not None)
gimp_assert('Gimp.Image.new(CMYK) base type',
            image.get_base_type() == Gimp.ImageBaseType.CMYK)
gimp_assert('Gimp.Image.new(CMYK) precision is U8 non-linear',
            image.get_precision() == Gimp.Precision.U8_NON_LINEAR)

profile = image.get_color_profile()
gimp_assert('new CMYK image has a color profile', profile is not None)
gimp_assert('new CMYK image profile is CMYK', profile.is_cmyk())
gimp_assert('new CMYK image profile is bidirectional',
            profile.is_cmyk_bidirectional())

gimp_assert('Gimp.Image.new_with_precision(CMYK, U16 linear) fails',
            new_or_none(Gimp.Image.new_with_precision, W, H, Gimp.ImageBaseType.CMYK,
                        Gimp.Precision.U16_LINEAR) is None)
gimp_assert('Gimp.Image.new_with_precision(CMYK, float linear) fails',
            new_or_none(Gimp.Image.new_with_precision, W, H, Gimp.ImageBaseType.CMYK,
                        Gimp.Precision.FLOAT_LINEAR) is None)

image16 = Gimp.Image.new_with_precision(W, H, Gimp.ImageBaseType.CMYK,
                                        Gimp.Precision.U16_NON_LINEAR)
gimp_assert('Gimp.Image.new_with_precision(CMYK, U16 non-linear)',
            image16 is not None and
            image16.get_base_type() == Gimp.ImageBaseType.CMYK and
            image16.get_precision() == Gimp.Precision.U16_NON_LINEAR)

# 4. Layers ---------------------------------------------------------------

layer = Gimp.Layer.new(image, "cmyka", W, H, Gimp.ImageType.CMYKA_IMAGE,
                       100.0, Gimp.LayerMode.NORMAL)
gimp_assert('Gimp.Layer.new(CMYKA) on a CMYK image', layer is not None)
gimp_assert('insert CMYKA layer', image.insert_layer(layer, None, 0))
gimp_assert('CMYKA layer is_cmyk()', layer.is_cmyk())
gimp_assert('CMYKA layer not is_rgb()', not layer.is_rgb())
gimp_assert('CMYKA layer not is_gray()', not layer.is_gray())
gimp_assert('CMYKA layer has alpha', layer.has_alpha())
gimp_assert('CMYKA layer type', layer.type() == Gimp.ImageType.CMYKA_IMAGE)
gimp_assert('CMYKA layer type_with_alpha',
            layer.type_with_alpha() == Gimp.ImageType.CMYKA_IMAGE)
gimp_assert('CMYKA u8 layer bpp == 5', layer.get_bpp() == 5)

layer2 = Gimp.Layer.new(image, "cmyk", W, H, Gimp.ImageType.CMYK_IMAGE,
                        100.0, Gimp.LayerMode.NORMAL)
gimp_assert('Gimp.Layer.new(CMYK) on a CMYK image',
            layer2 is not None and layer2.is_cmyk() and
            not layer2.has_alpha() and
            layer2.type() == Gimp.ImageType.CMYK_IMAGE and
            layer2.type_with_alpha() == Gimp.ImageType.CMYKA_IMAGE)
image.insert_layer(layer2, None, 0)

layer16 = Gimp.Layer.new(image16, "cmyka16", W, H, Gimp.ImageType.CMYKA_IMAGE,
                         100.0, Gimp.LayerMode.NORMAL)
image16.insert_layer(layer16, None, 0)
gimp_assert('CMYKA u16 layer bpp == 10',
            layer16 is not None and layer16.get_bpp() == 10)

gimp_assert('Gimp.Layer.new(RGBA) on a CMYK image fails',
            new_or_none(Gimp.Layer.new, image, "rgba", W, H, Gimp.ImageType.RGBA_IMAGE,
                           100.0, Gimp.LayerMode.NORMAL) is None)
gimp_assert('Gimp.Layer.new(INDEXED) on a CMYK image fails',
            new_or_none(Gimp.Layer.new, image, "idx", W, H, Gimp.ImageType.INDEXED_IMAGE,
                           100.0, Gimp.LayerMode.NORMAL) is None)

rgb_image = Gimp.Image.new(W, H, Gimp.ImageBaseType.RGB)
gimp_assert('Gimp.Layer.new(CMYKA) on an RGB image fails',
            new_or_none(Gimp.Layer.new, rgb_image, "cmyka", W, H, Gimp.ImageType.CMYKA_IMAGE,
                           100.0, Gimp.LayerMode.NORMAL) is None)

rgb_layer = Gimp.Layer.new(rgb_image, "rgba", W, H, Gimp.ImageType.RGBA_IMAGE,
                           100.0, Gimp.LayerMode.NORMAL)
gimp_assert('Gimp.Layer.new(RGBA) on an RGB image still works',
            rgb_layer is not None)
rgb_image.insert_layer(rgb_layer, None, 0)
gimp_assert('RGB layer is not CMYK', not rgb_layer.is_cmyk())

# 5. Components -----------------------------------------------------------

for c in (Gimp.ChannelType.CYAN, Gimp.ChannelType.MAGENTA,
          Gimp.ChannelType.YELLOW, Gimp.ChannelType.BLACK,
          Gimp.ChannelType.ALPHA):
  gimp_assert('CMYK image component {} visible'.format(c),
              image.get_component_visible(c))
  gimp_assert('CMYK image component {} active'.format(c),
              image.get_component_active(c))

gimp_assert('set CMYK component BLACK invisible',
            image.set_component_visible(Gimp.ChannelType.BLACK, False))
gimp_assert('CMYK component BLACK is invisible',
            not image.get_component_visible(Gimp.ChannelType.BLACK))
gimp_assert('CMYK component CYAN still visible',
            image.get_component_visible(Gimp.ChannelType.CYAN))
image.set_component_visible(Gimp.ChannelType.BLACK, True)

gimp_assert('set CMYK component RED fails',
            not image.set_component_active(Gimp.ChannelType.RED, False))
gimp_assert('set RGB image component CYAN fails',
            not rgb_image.set_component_active(Gimp.ChannelType.CYAN, False))
gimp_assert('RGB image component RED still works',
            rgb_image.get_component_active(Gimp.ChannelType.RED))

# 6. Histogram channels (model validation only; WS08 owns the values) ----

ret = layer.histogram(Gimp.HistogramChannel.RED, 0.0, 1.0)
gimp_assert('histogram RED on a CMYK drawable fails', not ret[0])
ret = rgb_layer.histogram(Gimp.HistogramChannel.CYAN, 0.0, 1.0)
gimp_assert('histogram CYAN on an RGB drawable fails', not ret[0])
ret = rgb_layer.histogram(Gimp.HistogramChannel.RED, 0.0, 1.0)
gimp_assert('histogram RED on an RGB drawable still works', ret[0])

# 7. Thumbnails are sRGB --------------------------------------------------

data, tw, th, bpp = layer.get_thumbnail_data(16, 16)
gimp_assert('CMYKA layer thumbnail bpp == 4', data is not None and bpp == 4)
gimp_assert('CMYKA layer thumbnail size',
            data.get_size() == tw * th * bpp)

image2 = Gimp.Image.new(W, H, Gimp.ImageBaseType.CMYK)
bg = Gimp.Layer.new(image2, "bg", W, H, Gimp.ImageType.CMYK_IMAGE,
                    100.0, Gimp.LayerMode.NORMAL)
image2.insert_layer(bg, None, 0)
data, tw, th, bpp = bg.get_thumbnail_data(16, 16)
gimp_assert('CMYK layer thumbnail bpp == 3', data is not None and bpp == 3)
data, tw, th, bpp = bg.get_sub_thumbnail_data(0, 0, 8, 8, 8, 8)
gimp_assert('CMYK layer sub-thumbnail bpp == 3', data is not None and bpp == 3)

data, tw, th, bpp = image.get_thumbnail_data(16, 16)
gimp_assert('CMYK image thumbnail bpp is 3 or 4',
            data is not None and bpp in (3, 4))
gimp_assert('CMYK image thumbnail pixbuf',
            image.get_thumbnail(16, 16, Gimp.PixbufTransparency.KEEP_ALPHA)
            is not None)

# 8. Color profile rules --------------------------------------------------

gimp_assert('detaching "icc-profile" of a CMYK image fails',
            not image.detach_parasite('icc-profile'))
gimp_assert('CMYK image still has its profile after detach attempt',
            image.get_color_profile() is not None)

srgb = Gimp.ColorProfile.new_rgb_srgb()
gimp_assert('assigning an RGB profile to a CMYK image fails',
            not image.set_color_profile(srgb))

gimp_assert('reassigning the CMYK profile works',
            image.set_color_profile(profile))
gimp_assert('unsetting the profile of a CMYK image keeps a CMYK profile',
            image.set_color_profile(None) and
            image.get_color_profile() is not None and
            image.get_color_profile().is_cmyk())
gimp_assert('effective profile of a CMYK image is CMYK',
            image.get_effective_color_profile().is_cmyk())

gimp_assert('converting a CMYK image to an RGB profile fails',
            not image.convert_color_profile(srgb,
                                            Gimp.ColorRenderingIntent.PERCEPTUAL,
                                            True))

# 9. gimp_image_convert_cmyk() argument validation -----------------------

gimp_assert('convert_cmyk on a CMYK image fails',
            not image.convert_cmyk(None,
                                   Gimp.ColorRenderingIntent.RELATIVE_COLORIMETRIC,
                                   True))
gimp_assert('convert_cmyk with an RGB profile fails',
            not rgb_image.convert_cmyk(srgb,
                                       Gimp.ColorRenderingIntent.RELATIVE_COLORIMETRIC,
                                       True))
gimp_assert('failed convert_cmyk leaves the image RGB',
            rgb_image.get_base_type() == Gimp.ImageBaseType.RGB)

gimp_assert('convert_indexed on a CMYK image fails',
            not image.convert_indexed(Gimp.ConvertDitherType.NONE,
                                      Gimp.ConvertPaletteType.GENERATE,
                                      255, False, False, ""))
gimp_assert('convert_precision of a CMYK image to linear fails',
            not image.convert_precision(Gimp.Precision.U16_LINEAR))

# 10. Conversions ----------------------------------------------------------

conv = rgb_image.duplicate()
gimp_assert('convert_cmyk(None) succeeds',
            conv.convert_cmyk(None, Gimp.ColorRenderingIntent.RELATIVE_COLORIMETRIC,
                              True))
gimp_assert('convert_cmyk(None) gives a CMYK image',
            conv.get_base_type() == Gimp.ImageBaseType.CMYK)
gimp_assert('convert_cmyk(None) attaches a CMYK profile',
            conv.get_color_profile() is not None and
            conv.get_color_profile().is_cmyk())
gimp_assert('converted layers are CMYK',
            all(l.is_cmyk() for l in conv.get_layers()))

conv2 = rgb_image.duplicate()
gimp_assert('convert_cmyk(profile, PERCEPTUAL, False)',
            conv2.convert_cmyk(profile,
                               Gimp.ColorRenderingIntent.PERCEPTUAL,
                               False) and
            conv2.get_base_type() == Gimp.ImageBaseType.CMYK)

lin = Gimp.Image.new_with_precision(W, H, Gimp.ImageBaseType.RGB,
                                    Gimp.Precision.FLOAT_LINEAR)
gimp_assert('convert_cmyk of a linear image gives a non-linear precision',
            lin.convert_cmyk(None,
                             Gimp.ColorRenderingIntent.RELATIVE_COLORIMETRIC,
                             True) and
            lin.get_precision() == Gimp.Precision.FLOAT_NON_LINEAR)

gimp_assert('convert_rgb of a CMYK image',
            conv.convert_rgb() and
            conv.get_base_type() == Gimp.ImageBaseType.RGB)
gimp_assert('convert_grayscale of a CMYK image',
            conv2.convert_grayscale() and
            conv2.get_base_type() == Gimp.ImageBaseType.GRAY)

# 11. Export options ------------------------------------------------------

exp_image = Gimp.Image.new(W, H, Gimp.ImageBaseType.CMYK)
exp_layer = Gimp.Layer.new(exp_image, "bg", W, H, Gimp.ImageType.CMYK_IMAGE,
                           100.0, Gimp.LayerMode.NORMAL)
exp_image.insert_layer(exp_layer, None, 0)

options = Gimp.ExportOptions()
options.set_property("capabilities",
                     Gimp.ExportCapabilities.CAN_HANDLE_RGB |
                     Gimp.ExportCapabilities.CAN_HANDLE_GRAY |
                     Gimp.ExportCapabilities.CAN_HANDLE_ALPHA |
                     Gimp.ExportCapabilities.CAN_HANDLE_LAYER_EFFECTS |
                     Gimp.ExportCapabilities.CAN_HANDLE_CMYK)
ret, out = options.get_image(exp_image)
gimp_assert('CMYK-capable export options keep the CMYK image',
            ret == Gimp.ExportReturn.IGNORE and out == exp_image)

n_images = len(Gimp.get_images())
options = Gimp.ExportOptions()
options.set_property("capabilities",
                     Gimp.ExportCapabilities.CAN_HANDLE_RGB |
                     Gimp.ExportCapabilities.CAN_HANDLE_ALPHA)
ret, out = options.get_image(exp_image)
gimp_assert('RGB-only export options export a copy of a CMYK image',
            ret == Gimp.ExportReturn.EXPORT and out != exp_image and
            len(Gimp.get_images()) == n_images + 1)
gimp_assert('the original CMYK image is unchanged',
            exp_image.get_base_type() == Gimp.ImageBaseType.CMYK)
gimp_assert('the exported copy is an RGB image',
            out.get_base_type() == Gimp.ImageBaseType.RGB)
gimp_assert('the exported copy has RGB layers',
            all(l.is_rgb() for l in out.get_layers()))
out.delete()

options = Gimp.ExportOptions()
options.set_property("capabilities",
                     Gimp.ExportCapabilities.CAN_HANDLE_GRAY)
ret, out = options.get_image(exp_image)
gimp_assert('gray-only export options export a copy of a CMYK image',
            ret == Gimp.ExportReturn.EXPORT and out != exp_image)
gimp_assert('the exported copy is a grayscale image',
            out.get_base_type() == Gimp.ImageBaseType.GRAY)
out.delete()

# A single layer smaller than the canvas is merged with a new CMYKA layer.
multi = Gimp.Image.new(W, H, Gimp.ImageBaseType.CMYK)
small = Gimp.Layer.new(multi, "small", W // 2, H // 2, Gimp.ImageType.CMYKA_IMAGE,
                       100.0, Gimp.LayerMode.NORMAL)
multi.insert_layer(small, None, 0)
options = Gimp.ExportOptions()
options.set_property("capabilities",
                     Gimp.ExportCapabilities.CAN_HANDLE_CMYK |
                     Gimp.ExportCapabilities.CAN_HANDLE_ALPHA)
ret, out = options.get_image(multi)
out_layers = out.get_layers()
gimp_assert('CMYK-only exporter: single small layer is merged to image size',
            ret == Gimp.ExportReturn.EXPORT and
            out.get_base_type() == Gimp.ImageBaseType.CMYK and
            len(out_layers) == 1 and
            out_layers[0].get_width() == W and
            out_layers[0].get_height() == H and
            out_layers[0].is_cmyk())
out.delete()

# 12. Legacy layer modes are mapped on CMYK images (I9, core rule of WS03)

legacy = Gimp.Layer.new(image, "legacy", W, H, Gimp.ImageType.CMYKA_IMAGE,
                        100.0, Gimp.LayerMode.MULTIPLY_LEGACY)
image.insert_layer(legacy, None, 0)
gimp_assert('legacy mode of a new CMYK layer is mapped',
            legacy.get_mode() == Gimp.LayerMode.MULTIPLY)
legacy.set_mode(Gimp.LayerMode.SCREEN_LEGACY)
gimp_assert('set_mode(legacy) on a CMYK layer is mapped',
            legacy.get_mode() == Gimp.LayerMode.SCREEN)

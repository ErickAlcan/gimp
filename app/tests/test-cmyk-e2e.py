#!/usr/bin/env python3

# CMYK end-to-end test Z (devel-docs/cmyk-mode/TESTING.md).  Owner: WS09.
#
# One headless scenario through the public API:
#
#   1. new CMYK u16 image (with the first GIMP_TESTING_CMYK_ICC profile if
#      set, else the builtin), background with K-only, C-only, rich black
#      and 50 % patches over paper;
#   2. paint with a K-only foreground (image space): a paintbrush stroke
#      and a bucket fill inside a selection;
#   3. a Multiply layer over the top-right quarter, a K100 text layer;
#   4. K-only invariants on the layers and on the flattened projection;
#   5. save XCF -> reload: base type, precision, profile bytes, layers,
#      modes, offsets, every layer's pixels and the projection
#      bit-identical;
#   6. native TIFF export -> reload: CMYK, profile, one page per layer
#      with bit-identical pixels and offsets; TIFF of a flattened
#      duplicate == projection;
#      native PSD export -> reload: CMYK, profile, per-layer pixels,
#      modes, offsets and the projection bit-identical;
#   7. convert a duplicate to RGB (gimp-image-convert-rgb: perceptual +
#      BPC to the builtin sRGB): every non-text layer equals a direct lcms
#      transform (ctypes) within 1/255;
#   8. convert it back to CMYK with Gimp.Image.convert_cmyk (same
#      profile, relative colorimetric + BPC): CMYK, precision kept, pixels
#      equal the lcms chain within 2/255, and K-only pixels appear exactly
#      where the lcms chain gives K-only (the RGB round trip re-separates
#      K100 into rich black: a documented limitation);
#   9. copy the original K-only patch and paste it into the converted
#      image: K-only again there, bit-identical.
#
# Pixel data is read and written with babl format *names* only
# ("CMYKA u16"), relying on INTERFACES I5.  Every check is a gimp_assert:
# there are no skips.

import array
import ctypes
import ctypes.util
import os
import struct

from gi.repository import Babl
from gi.repository import Gegl

pdb    = Gimp.get_pdb()
tmpdir = Gimp.temp_directory()

W, H = 64, 64
MAXV = 65535

PAPER = (0.0, 0.0, 0.0, 0.0)
K100  = (0.0, 0.0, 0.0, 1.0)
C100  = (1.0, 0.0, 0.0, 0.0)
RICH  = (0.6, 0.4, 0.4, 1.0)
HALF  = (0.5, 0.5, 0.5, 0.5)

# rectangles: (x, y, w, h)
R_K      = (0,  0, 16, 32)     # K-only patch
R_C      = (16, 0, 16, 32)     # C-only patch
R_RICH   = (32, 0, 16, 32)     # rich black   (under the Multiply layer)
R_HALF   = (48, 0, 16, 32)     # 50 % patch   (under the Multiply layer)
R_STROKE = (0, 32, 64, 16)     # paper, paintbrush stroke along y = 40
R_BUCKET = (0, 48, 32, 16)     # paper, bucket-filled with K100
R_TEXT   = (32, 48, 32, 16)    # paper, under the text layer
R_MULT   = (32, 0, 32, 32)     # Multiply layer

################################################################################
# Pixel helpers

def u16(v):
  return int(round(max(0.0, min(1.0, v)) * MAXV))

def rect_pixels(rect):
  x0, y0, w, h = rect
  return [(x, y) for y in range(y0, y0 + h) for x in range(x0, x0 + w)]

def read(drawable, fmt, rect=None):
  '''Pixels of @drawable (drawable coordinates) as an array of uint16.'''
  if rect is None:
    rect = (0, 0, drawable.get_width(), drawable.get_height())
  data = drawable.get_buffer().get(Gegl.Rectangle.new(*rect), 1.0, fmt,
                                   Gegl.AbyssPolicy.NONE)
  a = array.array('H')
  a.frombytes(bytes(data))
  return a

def write(drawable, fmt, rect, values):
  buf = drawable.get_buffer()
  buf.set(Gegl.Rectangle.new(*rect), fmt, array.array('H', values).tobytes())
  buf.flush()

def pixel(a, n, w, x, y):
  i = (y * w + x) * n
  return tuple(a[i:i + n])

def is_k_only(p):
  return p[0] == 0 and p[1] == 0 and p[2] == 0

def profile_bytes(image):
  return bytes(image.get_color_profile().get_icc_profile())

def cmyk_color(image, inks):
  '''A GeglColor in the image's CMYK space (exact inks).'''
  layer = image.get_layers()[-1]
  fmt   = Babl.format_with_space("CMYK double", layer.get_format())
  color = Gegl.Color.new("white")
  color.set_bytes(fmt, GLib.Bytes.new(struct.pack('<4d', *inks)))
  return color

def flatten_copy(image):
  '''The projection, as the bytes of the flattened layer of a duplicate.'''
  dup   = image.duplicate()
  layer = dup.flatten()
  gimp_assert("flatten", layer is not None)
  gimp_assert("flattened image is CMYK",
              dup.get_base_type() == Gimp.ImageBaseType.CMYK)
  gimp_assert("flattened layer has no alpha", not layer.has_alpha())
  pixels = read(layer, "CMYK u16")
  dup.delete()
  return pixels

def export(proc_name, image, path, **props):
  proc   = pdb.lookup_procedure(proc_name)
  gimp_assert(f"{proc_name} exists", proc is not None)
  config = proc.create_config()
  config.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
  config.set_property('image', image)
  config.set_property('file', Gio.File.new_for_path(path))
  config.set_property('options', None)
  for key, value in props.items():
    config.set_property(key.replace('_', '-'), value)
  result = proc.run(config)
  return result.index(0) == Gimp.PDBStatusType.SUCCESS

def load(path):
  image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(path))
  gimp_assert(f"load {os.path.basename(path)}", image is not None)
  return image

def count_diffs(a, b, label):
  if len(a) != len(b):
    print(f"{label}: length {len(a)} != {len(b)}")
    return -1
  diffs = [i for i in range(len(a)) if a[i] != b[i]]
  if diffs:
    print(f"{label}: {len(diffs)} differing samples, first at "
          f"{[(i, a[i], b[i]) for i in diffs[:6]]}")
  return len(diffs)

def max_diff(a, b):
  return max(abs(x - y) for x, y in zip(a, b))

################################################################################
# lcms (ctypes) reference transforms

class Lcms:
  TYPE_CMYK_16 = (6 << 16) | (4 << 3) | 2
  TYPE_RGB_16  = (4 << 16) | (3 << 3) | 2
  BPC          = 0x2000
  NOOPTIMIZE   = 0x0100
  INTENT = { 'perceptual': 0, 'relative': 1 }

  def __init__(self):
    name = ctypes.util.find_library('lcms2')
    gimp_assert("liblcms2 available through ctypes", name is not None)
    self.lib = lib = ctypes.CDLL(name)
    lib.cmsOpenProfileFromMem.restype  = ctypes.c_void_p
    lib.cmsOpenProfileFromMem.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
    lib.cmsCreateTransform.restype     = ctypes.c_void_p
    lib.cmsCreateTransform.argtypes    = [ctypes.c_void_p, ctypes.c_uint32,
                                          ctypes.c_void_p, ctypes.c_uint32,
                                          ctypes.c_uint32, ctypes.c_uint32]
    lib.cmsDoTransform.argtypes        = [ctypes.c_void_p, ctypes.c_void_p,
                                          ctypes.c_void_p, ctypes.c_uint32]
    lib.cmsDeleteTransform.argtypes    = [ctypes.c_void_p]
    lib.cmsCloseProfile.argtypes       = [ctypes.c_void_p]

  def transform(self, src_icc, src_type, src_n, dst_icc, dst_type, dst_n,
                intent, values):
    lib = self.lib
    hs  = lib.cmsOpenProfileFromMem(src_icc, len(src_icc))
    hd  = lib.cmsOpenProfileFromMem(dst_icc, len(dst_icc))
    gimp_assert("lcms opens the profiles", hs and hd)
    t = lib.cmsCreateTransform(hs, src_type, hd, dst_type,
                               self.INTENT[intent], self.BPC | self.NOOPTIMIZE)
    gimp_assert("lcms transform", t)
    n   = len(values) // src_n
    src = array.array('H', values)
    dst = array.array('H', [0] * (n * dst_n))
    lib.cmsDoTransform(t, src.buffer_info()[0], dst.buffer_info()[0], n)
    lib.cmsDeleteTransform(t)
    lib.cmsCloseProfile(hs)
    lib.cmsCloseProfile(hd)
    return dst

def drop_alpha(a, n):
  return array.array('H', [a[i] for i in range(len(a)) if i % n != n - 1])

################################################################################
# 1. The image

icc_env = os.environ.get('GIMP_TESTING_CMYK_ICC')
profile = None
if icc_env:
  path = icc_env.split(':')[0]
  profile = Gimp.ColorProfile.new_from_file(Gio.File.new_for_path(path))
  gimp_assert(f"GIMP_TESTING_CMYK_ICC profile {path} loads", profile is not None)
  gimp_assert("GIMP_TESTING_CMYK_ICC profile is bidirectional CMYK",
              profile.is_cmyk_bidirectional())
  print(f"e2e: using {path}")
else:
  print("e2e: using the builtin CMYK profile")

image = Gimp.Image.new_with_precision(W, H, Gimp.ImageBaseType.CMYK,
                                      Gimp.Precision.U16_NON_LINEAR)
gimp_assert("new CMYK u16 image", image is not None and
            image.get_base_type() == Gimp.ImageBaseType.CMYK)
if profile is not None:
  gimp_assert("set the CMYK profile before adding layers",
              image.set_color_profile(profile))
profile_icc = profile_bytes(image)
gimp_assert("image profile is CMYK", image.get_color_profile().is_cmyk())

bg = Gimp.Layer.new(image, "Background", W, H, Gimp.ImageType.CMYK_IMAGE,
                    100.0, Gimp.LayerMode.NORMAL)
gimp_assert("CMYK background layer", bg is not None and bg.is_cmyk() and
            not bg.has_alpha())
image.insert_layer(bg, None, 0)

values = []
for y in range(H):
  for x in range(W):
    if   x < 16 and y < 32: inks = K100
    elif x < 32 and y < 32: inks = C100
    elif x < 48 and y < 32: inks = RICH
    elif y < 32:            inks = HALF
    else:                   inks = PAPER
    values += [u16(v) for v in inks]
write(bg, "CMYK u16", (0, 0, W, H), values)
bg_before_paint = read(bg, "CMYK u16")

################################################################################
# 2. Painting with a K-only foreground

k100 = cmyk_color(image, K100)
Gimp.context_push()
Gimp.context_set_defaults()
gimp_assert("set FG", Gimp.context_set_foreground(k100))
fg = Gimp.context_get_foreground()
fg_inks = struct.unpack('<4d', bytes(fg.get_bytes(
            Babl.format_with_space("CMYK double", bg.get_format())).get_data()))
gimp_assert(f"context FG keeps the K-only inks {fg_inks}",
            fg_inks == (0.0, 0.0, 0.0, 1.0))

Gimp.context_set_opacity(100.0)
Gimp.context_set_paint_mode(Gimp.LayerMode.NORMAL)
# A test session has no brush files (only the clipboard brushes), and
# creating a generated brush criticals (no writable data folder in the
# search path): paint with the "Clipboard Mask" brush made from a 7x7
# antialiased black disk on white.  "Dynamics Off" is not installed
# either; "Pressure Size" is (pressure is 1.0 for PDB strokes).
disk = Gimp.Image.new(7, 7, Gimp.ImageBaseType.GRAY)
disk_layer = Gimp.Layer.new(disk, "disk", 7, 7, Gimp.ImageType.GRAY_IMAGE,
                            100.0, Gimp.LayerMode.NORMAL)
disk.insert_layer(disk_layer, None, 0)
disk_layer.get_buffer().set(Gegl.Rectangle.new(0, 0, 7, 7), "Y' u8",
                            bytes([0 if (x - 3) ** 2 + (y - 3) ** 2 <= 10 else 255
                                   for y in range(7) for x in range(7)]))
gimp_assert("copy the brush disk", Gimp.edit_copy([disk_layer]))
disk.delete()
brush = Gimp.Brush.get_by_name("Clipboard Mask")
gimp_assert("'Clipboard Mask' brush", brush is not None)
gimp_assert("set brush", Gimp.context_set_brush(brush))
gimp_assert("set dynamics", Gimp.context_set_dynamics_name("Pressure Size"))
Gimp.context_set_brush_default_size()

gimp_assert("paintbrush stroke",
            Gimp.paintbrush_default(bg, [4.0, 40.0, 30.0, 40.0, 60.0, 40.0]))

gimp_assert("select the bucket rectangle",
            image.select_rectangle(Gimp.ChannelOps.REPLACE, *R_BUCKET))
Gimp.context_set_sample_threshold(0.0)
Gimp.context_set_antialias(False)
gimp_assert("bucket fill with FG",
            bg.edit_bucket_fill(Gimp.FillType.FOREGROUND, 8.0, 56.0))
gimp_assert("select none", Gimp.Selection.none(image))

painted = read(bg, "CMYK u16")

n_k_partial = 0
for (x, y) in rect_pixels(R_STROKE):
  p = pixel(painted, 4, W, x, y)
  gimp_assert(f"stroke pixel ({x},{y}) {p} is K-only", is_k_only(p))
  if 0 < p[3] < MAXV:
    n_k_partial += 1
gimp_assert("stroke centre is K100 exactly",
            pixel(painted, 4, W, 30, 40) == (0, 0, 0, MAXV))
gimp_assert("stroke left paper untouched far from it",
            pixel(painted, 4, W, 30, 33) == (0, 0, 0, 0))
print(f"e2e: stroke has {n_k_partial} partially covered (antialiased) pixels")

for (x, y) in rect_pixels(R_BUCKET):
  gimp_assert(f"bucket pixel ({x},{y}) is K100",
              pixel(painted, 4, W, x, y) == (0, 0, 0, MAXV))
for (x, y) in rect_pixels(R_TEXT) + rect_pixels(R_K) + rect_pixels(R_C) + \
              rect_pixels(R_RICH) + rect_pixels(R_HALF):
  gimp_assert(f"pixel ({x},{y}) outside stroke and fill unchanged",
              pixel(painted, 4, W, x, y) == pixel(bg_before_paint, 4, W, x, y))

################################################################################
# 3. Multiply layer and text layer

mult = Gimp.Layer.new(image, "Multiply", R_MULT[2], R_MULT[3],
                      Gimp.ImageType.CMYKA_IMAGE, 100.0, Gimp.LayerMode.MULTIPLY)
gimp_assert("CMYKA Multiply layer", mult is not None and mult.has_alpha())
image.insert_layer(mult, None, 0)
mult.set_offsets(R_MULT[0], R_MULT[1])
values = []
for y in range(R_MULT[3]):
  for x in range(R_MULT[2]):
    values += [u16(0.3), u16(x / (R_MULT[2] - 1)), 0, u16(0.1),
               MAXV if y < 24 else u16(0.5)]
write(mult, "CMYKA u16", (0, 0, R_MULT[2], R_MULT[3]), values)
gimp_assert("Multiply mode", mult.get_mode() == Gimp.LayerMode.MULTIPLY)

font = Gimp.context_get_font()
gimp_assert("a font is available", font is not None)
text = Gimp.TextLayer.new(image, "Km", font, 14.0, Gimp.Unit.pixel())
gimp_assert("text layer", text is not None)
image.insert_layer(text, None, 0)
gimp_assert("text color K100", text.set_color(k100))
text.set_offsets(R_TEXT[0] + 2, R_TEXT[1])
gimp_assert("text layer is CMYK", text.is_cmyk())

tw, th = text.get_width(), text.get_height()
tpix   = read(text, "CMYKA u16")
covered = 0
for y in range(th):
  for x in range(tw):
    p = pixel(tpix, 5, tw, x, y)
    if p[4] > 0:
      covered += 1
      gimp_assert(f"text pixel ({x},{y}) {p} is K100", p[:4] == (0, 0, 0, MAXV))
gimp_assert(f"text has coverage ({covered} pixels)", covered > 10)
Gimp.context_pop()

################################################################################
# 4. Projection invariants

proj = flatten_copy(image)
for (x, y) in rect_pixels(R_K):
  gimp_assert(f"projection K patch ({x},{y}) bit-identical K100",
              pixel(proj, 4, W, x, y) == (0, 0, 0, MAXV))
for (x, y) in rect_pixels(R_C):
  gimp_assert(f"projection C patch ({x},{y}) bit-identical C100",
              pixel(proj, 4, W, x, y) == (MAXV, 0, 0, 0))
for (x, y) in rect_pixels(R_STROKE) + rect_pixels(R_BUCKET):
  gimp_assert(f"projection ({x},{y}) equals the painted background",
              pixel(proj, 4, W, x, y) == pixel(painted, 4, W, x, y))
for (x, y) in rect_pixels(R_TEXT):
  gimp_assert(f"projection text area ({x},{y}) is K-only",
              is_k_only(pixel(proj, 4, W, x, y)))
changed = sum(1 for (x, y) in rect_pixels(R_MULT)
              if pixel(proj, 4, W, x, y) != pixel(painted, 4, W, x, y))
gimp_assert(f"Multiply changes the covered quarter ({changed} pixels)",
            changed > len(rect_pixels(R_MULT)) // 2)

def snapshot(image):
  layers = image.get_layers()
  return [(l.get_name(), l.get_mode(), l.get_opacity(), l.get_offsets()[1:],
           l.get_width(), l.get_height(), l.has_alpha(),
           read(l, "CMYKA u16" if l.has_alpha() else "CMYK u16"))
          for l in layers]

def compare_layers(label, ref, got, names=True):
  gimp_assert(f"{label}: {len(ref)} layers", len(got) == len(ref))
  for r, g in zip(ref, got):
    name = r[0]
    if names:
      gimp_assert(f"{label}: layer name {name}", g[0] == name)
    if r[1] is not None:
      gimp_assert(f"{label}: {name} mode", g[1] == r[1])
      gimp_assert(f"{label}: {name} opacity", abs(g[2] - r[2]) < 1e-3)
    gimp_assert(f"{label}: {name} offsets {g[3]} == {r[3]}", g[3] == r[3])
    gimp_assert(f"{label}: {name} size", g[4:6] == r[4:6])
    if r[6] is not None:
      gimp_assert(f"{label}: {name} alpha", g[6] == r[6])
    gimp_assert(f"{label}: {name} pixels bit-identical",
                count_diffs(r[7], g[7], f"{label}/{name}") == 0)

orig = snapshot(image)

################################################################################
# 5. XCF round trip

xcf_path = os.path.join(tmpdir, 'cmyk-e2e.xcf')
gimp_assert("save XCF",
            Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image,
                           Gio.File.new_for_path(xcf_path), None))
with open(xcf_path, 'rb') as f:
  header = f.read(14)
gimp_assert(f"XCF version 28 ({header!r})", header == b'gimp xcf v028\0')

xcf = load(xcf_path)
gimp_assert("XCF: base type CMYK", xcf.get_base_type() == Gimp.ImageBaseType.CMYK)
gimp_assert("XCF: precision U16 non-linear",
            xcf.get_precision() == Gimp.Precision.U16_NON_LINEAR)
gimp_assert("XCF: profile bytes identical", profile_bytes(xcf) == profile_icc)
compare_layers("XCF", orig, snapshot(xcf))
xl = xcf.get_layers()
gimp_assert("XCF: text layer is still a text layer", xl[0].is_text_layer())
gimp_assert("XCF: projection bit-identical",
            count_diffs(proj, flatten_copy(xcf), "XCF/projection") == 0)
xcf.delete()

################################################################################
# 6. Native TIFF and PSD

def check_tiff_header(label, tif):
  gimp_assert(f"{label}: base type CMYK", tif.get_base_type() == Gimp.ImageBaseType.CMYK)
  gimp_assert(f"{label}: precision U16 non-linear",
              tif.get_precision() == Gimp.Precision.U16_NON_LINEAR)
  gimp_assert(f"{label}: profile bytes identical", profile_bytes(tif) == profile_icc)

# 6a. multi-page TIFF (one page per layer; TIFF has no layer modes)
tif_path = os.path.join(tmpdir, 'cmyk-e2e.tif')
gimp_assert("TIFF export", export('file-tiff-export', image, tif_path))
tif = load(tif_path)
check_tiff_header("TIFF", tif)
pages = [(l.get_name(), l.get_offsets()[1:], l.get_width(), l.get_height(),
          read(l, "CMYKA u16" if l.has_alpha() else "CMYK u16"))
         for l in tif.get_layers()]
# Known upstream behaviour (RGB too, not CMYK-specific): a text layer
# comes back twice from a multi-page TIFF ("Km #1" and "Km"), identical.
text_ref = orig[0]
dups = [p for p in pages if p[0] == text_ref[0] + " #1"]
gimp_assert("TIFF: at most one duplicated text page", len(dups) <= 1)
for d in dups:
  gimp_assert("TIFF: the duplicated text page is identical to the text layer",
              d[4] == text_ref[7] and d[1] == text_ref[3])
pages = [p for p in pages if p not in dups]
compare_layers("TIFF pages",
               [(r[0], None, None, r[3], r[4], r[5], None, r[7]) for r in orig],
               [(p[0], None, None, p[1], p[2], p[3], None, p[4]) for p in pages])
tif.delete()

# 6b. the composite: TIFF of a flattened duplicate == the projection
flat = image.duplicate()
flat.flatten()
tif_path2 = os.path.join(tmpdir, 'cmyk-e2e-flat.tif')
gimp_assert("TIFF export (flattened)", export('file-tiff-export', flat, tif_path2))
flat.delete()
tif = load(tif_path2)
check_tiff_header("TIFF (flattened)", tif)
tl = tif.get_layers()
gimp_assert("TIFF (flattened): one layer", len(tl) == 1)
gimp_assert("TIFF (flattened): full size",
            (tl[0].get_width(), tl[0].get_height()) == (W, H))
tif_pix = read(tl[0], "CMYKA u16")
gimp_assert("TIFF (flattened): opaque",
            all(tif_pix[i] == MAXV for i in range(4, len(tif_pix), 5)))
gimp_assert("TIFF (flattened): pixels == projection (bit-identical)",
            count_diffs(proj, drop_alpha(tif_pix, 5), "TIFF") == 0)
tif.delete()

psd_path = os.path.join(tmpdir, 'cmyk-e2e.psd')
gimp_assert("PSD export", export('file-psd-export', image, psd_path))
psd = load(psd_path)
gimp_assert("PSD: base type CMYK", psd.get_base_type() == Gimp.ImageBaseType.CMYK)
gimp_assert("PSD: precision U16 non-linear",
            psd.get_precision() == Gimp.Precision.U16_NON_LINEAR)
gimp_assert("PSD: profile bytes identical", profile_bytes(psd) == profile_icc)
compare_layers("PSD", orig, snapshot(psd))
gimp_assert("PSD: projection bit-identical",
            count_diffs(proj, flatten_copy(psd), "PSD/projection") == 0)
psd.delete()

################################################################################
# 7. CMYK -> RGB

lcms = Lcms()
srgb_icc = bytes(Gimp.ColorProfile.new_rgb_srgb().get_icc_profile())

rgb = image.duplicate()
gimp_assert("convert_rgb", rgb.convert_rgb())
gimp_assert("RGB: base type", rgb.get_base_type() == Gimp.ImageBaseType.RGB)
gimp_assert("RGB: precision U16 non-linear",
            rgb.get_precision() == Gimp.Precision.U16_NON_LINEAR)
gimp_assert("RGB: sRGB profile",
            rgb.get_effective_color_profile().is_rgb())

rl = rgb.get_layers()
gimp_assert("RGB: same layers", len(rl) == len(orig))
for ref, layer in zip(orig, rl):
  name = ref[0]
  gimp_assert(f"RGB: {name} is RGB", layer.is_rgb())
  gimp_assert(f"RGB: {name} mode kept", layer.get_mode() == ref[1])
  if layer.is_text_layer():
    # text layers re-render from their (converted) text color
    alpha = read(layer, "R'G'B'A u16")[3::4]
    gimp_assert(f"RGB: {name} coverage kept",
                list(alpha) == list(ref[7][4::5]))
    continue
  n    = 5 if ref[6] else 4
  inks = drop_alpha(ref[7], 5) if ref[6] else ref[7]
  exp  = lcms.transform(profile_icc, Lcms.TYPE_CMYK_16, 4,
                        srgb_icc, Lcms.TYPE_RGB_16, 3, 'perceptual', inks)
  got  = read(layer, "R'G'B' u16")
  d    = max_diff(exp, got)
  print(f"e2e: RGB {name}: max diff vs lcms {d}/65535")
  gimp_assert(f"RGB: {name} == lcms perceptual+BPC within 1/255 (max {d})",
              d <= 257)
  if ref[6]:
    gimp_assert(f"RGB: {name} alpha kept",
                list(read(layer, "R'G'B'A u16")[3::4]) == list(ref[7][4::5]))

################################################################################
# 8. RGB -> CMYK

gimp_assert("convert_cmyk back",
            rgb.convert_cmyk(image.get_color_profile(),
                             Gimp.ColorRenderingIntent.RELATIVE_COLORIMETRIC, True))
back = rgb
gimp_assert("back: base type CMYK", back.get_base_type() == Gimp.ImageBaseType.CMYK)
gimp_assert("back: precision U16 non-linear",
            back.get_precision() == Gimp.Precision.U16_NON_LINEAR)
gimp_assert("back: profile bytes identical", profile_bytes(back) == profile_icc)

back_bg  = back.get_layers()[-1]
gimp_assert("back: background is CMYK", back_bg.is_cmyk())
srgb_bg  = lcms.transform(profile_icc, Lcms.TYPE_CMYK_16, 4,
                          srgb_icc, Lcms.TYPE_RGB_16, 3, 'perceptual', painted)
chain    = lcms.transform(srgb_icc, Lcms.TYPE_RGB_16, 3,
                          profile_icc, Lcms.TYPE_CMYK_16, 4, 'relative', srgb_bg)
got      = read(back_bg, "CMYK u16")
d        = max_diff(chain, got)
print(f"e2e: CMYK -> RGB -> CMYK background: max diff vs lcms chain {d}/65535")
gimp_assert(f"back: background == lcms chain within 2/255 (max {d})", d <= 514)

expected_k_only = set()
got_k_only      = set()
for y in range(H):
  for x in range(W):
    e = pixel(chain, 4, W, x, y)
    g = pixel(got, 4, W, x, y)
    if is_k_only(e) and e[3] > 0:
      expected_k_only.add((x, y))
    if is_k_only(g) and g[3] > 0:
      got_k_only.add((x, y))
k_after = pixel(got, 4, W, 4, 4)
print(f"e2e: K100 after the RGB round trip: {k_after}; "
      f"{len(got_k_only)} K-only pixels (lcms chain: {len(expected_k_only)})")
gimp_assert("back: K-only pixels exactly where the lcms chain has them",
            got_k_only == expected_k_only)
gimp_assert("back: the RGB round trip does not keep K100 K-only "
            "(documented limitation; K is re-separated)",
            not is_k_only(k_after))
gimp_assert("back: K100 stays dark (K >= 50 %)", k_after[3] >= MAXV // 2)
paper = pixel(got, 4, W, 60, 62)
gimp_assert(f"back: paper stays (almost) paper {paper}", max(paper) <= 514)

################################################################################
# 9. copy/paste of the original K-only patch: K-only again

gimp_assert("select the K patch",
            image.select_rectangle(Gimp.ChannelOps.REPLACE, *R_K))
gimp_assert("copy the K patch", Gimp.edit_copy([bg]))
Gimp.Selection.none(image)

pasted = Gimp.edit_paste(back_bg, False)
gimp_assert("paste", pasted is not None and len(pasted) == 1)
floating = pasted[0]
gimp_assert("floating selection is CMYK", floating.is_cmyk())
floating.set_offsets(R_K[0], R_K[1])
gimp_assert("anchor", Gimp.floating_sel_anchor(floating))

final = read(back_bg, "CMYK u16")
for (x, y) in rect_pixels(R_K):
  gimp_assert(f"pasted K patch ({x},{y}) is K100 again, bit-identical",
              pixel(final, 4, W, x, y) == (0, 0, 0, MAXV))
outside = [(x, y) for y in range(H) for x in range(W)
           if not (x < R_K[0] + R_K[2] and y < R_K[1] + R_K[3])]
gimp_assert("paste left everything else bit-identical",
            all(pixel(final, 4, W, x, y) == pixel(got, 4, W, x, y)
                for (x, y) in outside))
got_k_only = set((x, y) for y in range(H) for x in range(W)
                 if is_k_only(pixel(final, 4, W, x, y)) and
                    pixel(final, 4, W, x, y)[3] > 0)
gimp_assert("K-only pixels: the lcms chain's plus the pasted patch",
            got_k_only == expected_k_only | set(rect_pixels(R_K)))

back.delete()
image.delete()
for p in (xcf_path, tif_path, tif_path2, psd_path):
  os.remove(p)
print("e2e: all checks passed")

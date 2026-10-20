#!/usr/bin/env python3

# CMYK file format tests E1-E7 (devel-docs/cmyk-mode/TESTING.md).
# Owner: WS06.
#
# Runs inside the in-build GIMP (python-fu-eval), so pixel data is passed
# with babl format *names* only ("CMYKA u16", no space): INTERFACES I5
# makes space-less CMYK <-> image-space CMYK an identity.
#
# Sub-tests whose prerequisite belongs to another, not yet merged,
# workstream print "PENDING INTEGRATION (<ws>): ..." and do not fail;
# they become strict as soon as the prerequisite is detected:
#   * WS05 (PDB/libgimp): detected by Gimp.Image.convert_cmyk().  Needed
#     for E6 (export options "convert to RGB" for RGB-only exporters).
#     Until then gimp-layer-new ignores the alpha of GIMP_CMYKA_IMAGE, so
#     the test adds the alpha channel itself.

import ctypes
import ctypes.util
import struct

from gi.repository import Gegl

HAVE_WS05 = hasattr(Gimp.Image, 'convert_cmyk')

tmpdir = Gimp.temp_directory()
pdb    = Gimp.get_pdb()


def pending(ws, what):
  print(f"PENDING INTEGRATION ({ws}): {what}")


################################################################################
# Pixel helpers

def enc(value, bpc):
  maxval = 255 if bpc == 1 else 65535
  return int(round(max(0.0, min(1.0, value)) * maxval))

def pack(values, bpc):
  return struct.pack('<%d%s' % (len(values), 'B' if bpc == 1 else 'H'), *values)

def unpack(data, bpc):
  n = len(data) // bpc
  return list(struct.unpack('<%d%s' % (n, 'B' if bpc == 1 else 'H'), bytes(data)))

def fmt_name(alpha, bpc):
  return ('CMYKA ' if alpha else 'CMYK ') + ('u8' if bpc == 1 else 'u16')

def patch_inks(x, y, w, h):
  '''Four quadrants: K-only, C-only, rich black, a gradient.'''
  qx = 0 if x < w // 2 else 1
  qy = 0 if y < h // 2 else 1
  q  = qx + 2 * qy
  if q == 0:
    return (0.0, 0.0, 0.0, 1.0)
  elif q == 1:
    return (1.0, 0.0, 0.0, 0.0)
  elif q == 2:
    return (0.6, 0.4, 0.4, 1.0)
  else:
    return (x / (w - 1), y / (h - 1), (x + y) / (w + h - 2), 0.25)

def patch_alpha(x, y, w, h):
  if x < w // 2 and y < h // 2:
    return 1.0
  elif y < h // 2:
    return 0.5
  elif x < w // 2:
    return 0.0          # fully transparent: color values must survive too
  else:
    return x / (w - 1)

def make_pixels(w, h, alpha, bpc):
  values = []
  for y in range(h):
    for x in range(w):
      inks = patch_inks(x, y, w, h)
      values += [enc(v, bpc) for v in inks]
      if alpha:
        values.append(enc(patch_alpha(x, y, w, h), bpc))
  return pack(values, bpc)

def precision_for(bpc):
  return (Gimp.Precision.U8_NON_LINEAR if bpc == 1 else
          Gimp.Precision.U16_NON_LINEAR)

def new_cmyk_image(w, h, bpc, alpha, with_mask=False):
  image = Gimp.Image.new_with_precision(w, h, Gimp.ImageBaseType.CMYK,
                                        precision_for(bpc))
  if image is None:
    return None, None

  layer = Gimp.Layer.new(image, "CMYK layer", w, h,
                         Gimp.ImageType.CMYKA_IMAGE if alpha else
                         Gimp.ImageType.CMYK_IMAGE,
                         100.0, Gimp.LayerMode.NORMAL)
  if alpha and not layer.has_alpha():
    if HAVE_WS05:
      gimp_assert("gimp-layer-new honours GIMP_CMYKA_IMAGE (WS05)", False)
    layer.add_alpha()
  image.insert_layer(layer, None, 0)

  buf = layer.get_buffer()
  buf.set(Gegl.Rectangle.new(0, 0, w, h), fmt_name(alpha, bpc),
          make_pixels(w, h, alpha, bpc))
  buf.flush()

  if with_mask:
    mask = layer.create_mask(Gimp.AddMaskType.WHITE)
    layer.add_mask(mask)
    mbuf = mask.get_buffer()
    mbuf.set(Gegl.Rectangle.new(0, 0, w, h), 'Y u8',
             bytes([(x * 37 + y * 11) % 256 for y in range(h) for x in range(w)]))
    mbuf.flush()

  return image, layer

def read_pixels(drawable, format_name):
  buf = drawable.get_buffer()
  w   = drawable.get_width()
  h   = drawable.get_height()
  return bytes(buf.get(Gegl.Rectangle.new(0, 0, w, h), 1.0, format_name,
                       Gegl.AbyssPolicy.NONE))

def profile_bytes(profile):
  if profile is None:
    return None
  return bytes(profile.get_icc_profile())


################################################################################
# Export / load helpers

def export(proc_name, image, path, **props):
  proc   = pdb.lookup_procedure(proc_name)
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
  return Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(path))


################################################################################
# Minimal TIFF writer / reader (baseline, little endian, one strip)

def write_cmyk_tiff(path, w, h, pixels, icc=None):
  SHORT, LONG, UNDEFINED = 3, 4, 7
  entries = []        # (tag, type, count, value bytes)

  def short(tag, *vals):
    entries.append((tag, SHORT, len(vals), struct.pack('<%dH' % len(vals), *vals)))
  def long_(tag, *vals):
    entries.append((tag, LONG, len(vals), struct.pack('<%dI' % len(vals), *vals)))

  short(256, w)
  short(257, h)
  short(258, 8, 8, 8, 8)
  short(259, 1)                 # no compression
  short(262, 5)                 # separated
  long_(273, 0)                 # strip offset, patched below
  short(277, 4)
  short(278, h)
  long_(279, len(pixels))
  short(284, 1)                 # contiguous
  short(332, 1)                 # InkSet CMYK
  if icc is not None:
    entries.append((34675, UNDEFINED, len(icc), bytes(icc)))
  entries.sort()

  ifd_offset = 8
  data_off   = ifd_offset + 2 + 12 * len(entries) + 4
  blobs      = b''
  ifd        = struct.pack('<H', len(entries))
  strip_idx  = [i for i, e in enumerate(entries) if e[0] == 273][0]

  for tag, typ, count, value in entries:
    if len(value) <= 4:
      ifd += struct.pack('<HHI', tag, typ, count) + value.ljust(4, b'\0')
    else:
      ifd += struct.pack('<HHII', tag, typ, count, data_off + len(blobs))
      blobs += value + (b'\0' if len(value) % 2 else b'')
  ifd += struct.pack('<I', 0)

  pos = 2 + 12 * strip_idx + 8
  ifd = ifd[:pos] + struct.pack('<I', data_off + len(blobs)) + ifd[pos + 4:]

  with open(path, 'wb') as f:
    f.write(b'II*\0' + struct.pack('<I', ifd_offset))
    f.write(ifd)
    f.write(blobs)
    f.write(pixels)

def read_tiff_tags(path):
  '''Tags of the first IFD: {tag: (type, count, inline value or offset)}.'''
  with open(path, 'rb') as f:
    data = f.read()
  bo  = '<' if data[:2] == b'II' else '>'
  off = struct.unpack(bo + 'I', data[4:8])[0]
  n   = struct.unpack(bo + 'H', data[off:off + 2])[0]
  tags = {}
  for i in range(n):
    e = data[off + 2 + 12 * i: off + 14 + 12 * i]
    tag, typ, count = struct.unpack(bo + 'HHI', e[:8])
    if typ == 3 and count == 1:
      value = struct.unpack(bo + 'H', e[8:10])[0]
    else:
      value = struct.unpack(bo + 'I', e[8:12])[0]
    tags[tag] = (typ, count, value)
  return tags


################################################################################
# lcms (ctypes): input-only derivative of a CMYK profile (TESTING.md)

def input_only_profile(icc):
  name = ctypes.util.find_library('lcms2')
  if name is None:
    return None
  lcms = ctypes.CDLL(name)
  lcms.cmsOpenProfileFromMem.restype  = ctypes.c_void_p
  lcms.cmsOpenProfileFromMem.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
  lcms.cmsIsTag.argtypes              = [ctypes.c_void_p, ctypes.c_uint32]
  lcms.cmsWriteTag.argtypes           = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p]
  lcms.cmsSaveProfileToMem.argtypes   = [ctypes.c_void_p, ctypes.c_void_p,
                                         ctypes.POINTER(ctypes.c_uint32)]
  lcms.cmsIsIntentSupported.argtypes  = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32]
  lcms.cmsCloseProfile.argtypes       = [ctypes.c_void_p]

  def sig(s):
    return struct.unpack('>I', s.encode())[0]

  h = lcms.cmsOpenProfileFromMem(icc, len(icc))
  if not h:
    return None
  for t in ('B2A0', 'B2A1', 'B2A2', 'B2D0', 'B2D1', 'B2D2'):
    if lcms.cmsIsTag(h, sig(t)):
      lcms.cmsWriteTag(h, sig(t), None)
  length = ctypes.c_uint32(0)
  lcms.cmsSaveProfileToMem(h, None, ctypes.byref(length))
  out = ctypes.create_string_buffer(length.value)
  lcms.cmsSaveProfileToMem(h, out, ctypes.byref(length))
  lcms.cmsCloseProfile(h)

  data = out.raw[:length.value]
  h = lcms.cmsOpenProfileFromMem(data, len(data))
  # INTENT_RELATIVE_COLORIMETRIC = 1, LCMS_USED_AS_OUTPUT = 1
  output_ok = lcms.cmsIsIntentSupported(h, 1, 1)
  lcms.cmsCloseProfile(h)
  gimp_assert("ctypes input-only derivative is not usable as output", not output_ok)
  return data


################################################################################
# Round trips

def check_roundtrip(label, proc, ext, bpc, alpha, with_mask=False, **props):
  w, h = 8, 8
  image, layer = new_cmyk_image(w, h, bpc, alpha, with_mask)
  src_profile  = profile_bytes(image.get_color_profile())
  src_pixels   = read_pixels(layer, fmt_name(alpha, bpc))
  src_mask     = read_pixels(layer.get_mask(), 'Y u8') if with_mask else None

  path = os.path.join(tmpdir, f'cmyk-{label}.{ext}')
  gimp_assert(f"{label}: export", export(proc, image, path, **props))
  image.delete()

  loaded = load(path)
  gimp_assert(f"{label}: load", loaded is not None)
  gimp_assert(f"{label}: base type CMYK",
              loaded.get_base_type() == Gimp.ImageBaseType.CMYK)
  gimp_assert(f"{label}: same profile bytes",
              profile_bytes(loaded.get_color_profile()) == src_profile)

  layers = loaded.get_layers()
  gimp_assert(f"{label}: one layer", len(layers) == 1)
  dst = layers[0]
  gimp_assert(f"{label}: alpha preserved", dst.has_alpha() == alpha)
  dst_pixels = read_pixels(dst, fmt_name(alpha, bpc))
  if dst_pixels != src_pixels:
    a = unpack(src_pixels, bpc)
    b = unpack(dst_pixels, bpc)
    diffs = [(i, a[i], b[i]) for i in range(len(a)) if a[i] != b[i]]
    print(f"{label}: {len(diffs)} differing samples, first: {diffs[:8]}")
  gimp_assert(f"{label}: pixels exact (K-only patch included)",
              dst_pixels == src_pixels)

  if with_mask:
    gimp_assert(f"{label}: mask preserved", dst.get_mask() is not None)
    gimp_assert(f"{label}: mask pixels exact",
                read_pixels(dst.get_mask(), 'Y u8') == src_mask)

  loaded.delete()
  return path


def run_tests():
  probe = Gimp.Image.new_with_precision(4, 4, Gimp.ImageBaseType.CMYK,
                                        Gimp.Precision.U16_NON_LINEAR)
  if probe is None:
    pending("WS05", "gimp-image-new-with-precision refuses GIMP_CMYK; "
            "all E tests skipped")
    return
  probe.delete()

  builtin = Gimp.ColorProfile.new_cmyk_builtin()

  # E1: TIFF export -> load, exact
  for bpc in (1, 2):
    for alpha in (False, True):
      label = f"E1-tiff-u{8 * bpc}{'-alpha' if alpha else ''}"
      path  = check_roundtrip(label, 'file-tiff-export', 'tif', bpc, alpha,
                              compression='none',
                              save_transparent_pixels=True)
      tags = read_tiff_tags(path)
      gimp_assert(f"{label}: Photometric = separated", tags[262][2] == 5)
      gimp_assert(f"{label}: InkSet = CMYK", 332 in tags and tags[332][2] == 1)
      gimp_assert(f"{label}: ICC profile tag", 34675 in tags)
      gimp_assert(f"{label}: samples per pixel",
                  tags[277][2] == (5 if alpha else 4))

  # E2: PSD export -> load, exact, masks preserved
  for bpc in (1, 2):
    for alpha in (False, True):
      label = f"E2-psd-u{8 * bpc}{'-alpha' if alpha else ''}"
      check_roundtrip(label, 'file-psd-export', 'psd', bpc, alpha,
                      with_mask=True)

  # E3: JPEG q=100, K-only stays K-only (Adobe inversion handled)
  w, h  = 16, 16
  image, layer = new_cmyk_image(w, h, 1, False)
  src   = unpack(read_pixels(layer, 'CMYK u8'), 1)
  path  = os.path.join(tmpdir, 'cmyk-E3.jpg')
  gimp_assert("E3: JPEG export", export('file-jpeg-export', image, path,
                                         quality=1.0))
  image.delete()
  loaded = load(path)
  gimp_assert("E3: load", loaded is not None)
  gimp_assert("E3: base type CMYK",
              loaded.get_base_type() == Gimp.ImageBaseType.CMYK)
  gimp_assert("E3: profile embedded and restored",
              profile_bytes(loaded.get_color_profile()) ==
              profile_bytes(builtin))
  dst = unpack(read_pixels(loaded.get_layers()[0], 'CMYK u8'), 1)
  maxdiff = max(abs(a - b) for a, b in zip(src, dst))
  print(f"E3: max abs diff {maxdiff}/255")
  gimp_assert("E3: max abs diff <= 3/255", maxdiff <= 3)
  for y in range(h // 2):
    for x in range(w // 2):
      i = 4 * (y * w + x)
      gimp_assert(f"E3: K-only patch C,M,Y <= 1/255 at ({x},{y})",
                  max(dst[i:i + 3]) <= 1 and dst[i + 3] >= 252)
  loaded.delete()

  # E4: CMYK TIFF without profile -> CMYK, default profile, raw values kept
  # (written by the script: export always embeds the profile in batch mode)
  w, h   = 4, 4
  pixels = make_pixels(w, h, False, 1)
  path   = os.path.join(tmpdir, 'cmyk-E4-noprofile.tif')
  write_cmyk_tiff(path, w, h, pixels)
  loaded = load(path)
  gimp_assert("E4: load", loaded is not None)
  gimp_assert("E4: base type CMYK",
              loaded.get_base_type() == Gimp.ImageBaseType.CMYK)
  profile = loaded.get_color_profile()
  gimp_assert("E4: a CMYK profile is attached",
              profile is not None and profile.is_cmyk())
  gimp_assert("E4: raw values kept",
              read_pixels(loaded.get_layers()[0], 'CMYK u8') == pixels)
  loaded.delete()

  # E5: RGB image, "export as CMYK" (late binding) still works
  for proc, ext in (('file-tiff-export', 'tif'),
                    ('file-jpeg-export', 'jpg'),
                    ('file-psd-export',  'psd')):
    label = f"E5-{ext}"
    w, h  = 16, 16
    image = Gimp.Image.new(w, h, Gimp.ImageBaseType.RGB)
    layer = Gimp.Layer.new(image, "RGB", w, h, Gimp.ImageType.RGB_IMAGE,
                           100.0, Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    rgb = bytes([255 if x < w // 2 else 0 for y in range(h) for x in range(w)
                 for c in range(3)])
    layer.get_buffer().set(Gegl.Rectangle.new(0, 0, w, h), "R'G'B' u8", rgb)
    image.set_simulation_profile(builtin)
    path  = os.path.join(tmpdir, f'cmyk-{label}.{ext}')
    props = {'cmyk': True}
    if ext == 'jpg':
      props['quality'] = 1.0
    gimp_assert(f"{label}: export as CMYK", export(proc, image, path, **props))
    image.delete()

    loaded = load(path)
    gimp_assert(f"{label}: load", loaded is not None)
    gimp_assert(f"{label}: loads as CMYK",
                loaded.get_base_type() == Gimp.ImageBaseType.CMYK)
    gimp_assert(f"{label}: soft-proofing profile embedded",
                profile_bytes(loaded.get_color_profile()) ==
                profile_bytes(builtin))
    inks  = unpack(read_pixels(loaded.get_layers()[0], 'CMYK u8'), 1)
    white = inks[4 * (h // 2 * w + 1):     4 * (h // 2 * w + 1) + 4]
    black = inks[4 * (h // 2 * w + w - 2): 4 * (h // 2 * w + w - 2) + 4]
    print(f"{label}: white -> {white}, black -> {black}")
    gimp_assert(f"{label}: white has (almost) no ink", max(white) <= 8)
    gimp_assert(f"{label}: black has K ink", black[3] >= 128)
    loaded.delete()

  # E6: CMYK image exported by an RGB-only exporter (PNG)
  image, layer = new_cmyk_image(8, 8, 2, False)
  path = os.path.join(tmpdir, 'cmyk-E6.png')
  ok = export('file-png-export', image, path)
  image.delete()
  if not HAVE_WS05:
    pending("WS05", f"E6 (PNG export of a CMYK image through the export "
            f"options' convert-to-RGB step): export returned {ok}, not checked")
  else:
    gimp_assert("E6: PNG export of a CMYK image", ok)
    loaded = load(path)
    gimp_assert("E6: PNG loads as RGB",
                loaded.get_base_type() == Gimp.ImageBaseType.RGB)
    loaded.delete()

  # E7: input-only CMYK profile -> RGB import, warning, no crash
  icc = input_only_profile(profile_bytes(builtin))
  if icc is None:
    print("SKIP: E7 needs liblcms2 through ctypes")
  else:
    w, h   = 4, 4
    pixels = make_pixels(w, h, False, 1)
    path   = os.path.join(tmpdir, 'cmyk-E7-input-only.tif')
    write_cmyk_tiff(path, w, h, pixels, icc)
    loaded = load(path)
    gimp_assert("E7: load", loaded is not None)
    gimp_assert("E7: loads as RGB",
                loaded.get_base_type() == Gimp.ImageBaseType.RGB)
    sim = loaded.get_simulation_profile()
    gimp_assert("E7: input-only profile not used as soft-proofing profile",
                sim is None or sim.is_cmyk_bidirectional())
    rgb = read_pixels(loaded.get_layers()[0], "R'G'B' u8")
    gimp_assert("E7: K-only pixel is dark", max(rgb[0:3]) < 100)
    loaded.delete()


run_tests()

#!/usr/bin/env python3

# Tier A babl probe for the CMYK work (devel-docs/cmyk-mode/TESTING.md,
# Tier A #4): read-only ctypes checks of the babl facts the CMYK mode is
# built on.  Run with: python3 -I -u app/tests/cmyk-babl-probe.py
#
# Owner: WS09.
#
# Checks
#   1. "CMYK <t>" / "CMYKA <t>" exist for u8, u16, u32, half, float and
#      double, with 4/5 components, a CMYK model and the right size; the
#      cmyk/cmykA/CaMaYaKaA/camayakaA models are CMYK models too;
#   2. "CaMaYaKaA double" exists; whether "CaMaYaKaA u32" exists is
#      reported (babl 0.1.129 lacks it; gimp_babl_init() registers it
#      only when missing, so both answers are fine);
#   3. a CMYK babl space is created from the builtin CMYK profile
#      (libgimpcolor's gimp_color_profile_new_cmyk_builtin());
#   4. I5: "CMYK(A) <t>" without space <-> with the builtin space is an
#      identity; "R'G'B'A float" / "Y' float" tagged with the CMYK space
#      mean sRGB;
#   5. the lcms-support discriminator of gimp_babl_has_cmyk_support()
#      (INTERFACES section 2): CMYK float (0.5,0,0,0) -> R'G'B' float
#      differs by more than 0.1 between the space-tagged and the naive
#      (space-less) conversion; K100 survives an sRGB round trip;
#   6. the facts behind I8: the same ICC bytes give the same babl space;
#      a header variant (bytes 24-35 changed) gives a *different* space
#      with the same colorimetry, so GIMP must canonicalize spaces
#      itself (gimp_color_profile_get_space()).
#
# Safety: no input-only (one-way) CMYK profile is ever created here, so
# no RGB -> CMYK fish can be fed one (babl segfaults on those).
#
# Exit status: 0 pass, 1 failure, 77 skipped (libraries not found).

import ctypes
import ctypes.util
import os
import random
import struct
import sys

SKIP = 77

failures = []

def check(name, cond, detail=''):
  status = 'ok' if cond else 'FAIL'
  print(f"{status:4} {name}{(': ' + detail) if detail else ''}")
  if not cond:
    failures.append(name)

def load_library(names, extra_dirs=()):
  for d in extra_dirs:
    for n in names:
      path = os.path.join(d, n)
      if os.path.exists(path):
        return ctypes.CDLL(path, mode=ctypes.RTLD_GLOBAL)
  for n in names:
    try:
      return ctypes.CDLL(n, mode=ctypes.RTLD_GLOBAL)
    except OSError:
      pass
  return None

################################################################################
# Libraries

babl = load_library(['libbabl-0.1.so.0', 'libbabl-0.1.so'])
if babl is None:
  print("SKIP: libbabl-0.1 not found")
  sys.exit(SKIP)

build_root = (os.environ.get('GIMP_GLOBAL_BUILD_ROOT') or
              os.environ.get('GIMP_TESTING_ABS_TOP_BUILDDIR'))
if not build_root:
  # <srcdir>/app/tests/cmyk-babl-probe.py -> <srcdir>/_build
  build_root = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
                 os.path.realpath(__file__)))), '_build')

gimpcolor = load_library(['libgimpcolor-3.0.so.0', 'libgimpcolor-3.0.so'],
                         [os.path.join(build_root, 'libgimpcolor')])
gobject   = load_library(['libgobject-2.0.so.0'])
if gimpcolor is None or gobject is None:
  print(f"SKIP: libgimpcolor-3.0 (build root {build_root}) or libgobject not found")
  sys.exit(SKIP)

vp = ctypes.c_void_p
cp = ctypes.c_char_p

def proto(lib, name, restype, argtypes):
  f = getattr(lib, name)
  f.restype  = restype
  f.argtypes = argtypes

proto(babl, 'babl_init',                       None,          [])
proto(babl, 'babl_format',                     vp,            [cp])
proto(babl, 'babl_format_exists',              ctypes.c_int,  [cp])
proto(babl, 'babl_format_with_space',          vp,            [cp, vp])
proto(babl, 'babl_format_get_n_components',    ctypes.c_int,  [vp])
proto(babl, 'babl_format_get_bytes_per_pixel', ctypes.c_int,  [vp])
proto(babl, 'babl_format_get_model',           vp,            [vp])
proto(babl, 'babl_format_get_space',           vp,            [vp])
proto(babl, 'babl_format_has_alpha',           ctypes.c_int,  [vp])
proto(babl, 'babl_get_model_flags',            ctypes.c_int,  [vp])
proto(babl, 'babl_space',                      vp,            [cp])
proto(babl, 'babl_space_from_icc',             vp,            [cp, ctypes.c_int,
                                                               ctypes.c_int,
                                                               ctypes.POINTER(cp)])
proto(babl, 'babl_space_is_cmyk',              ctypes.c_int,  [vp])
proto(babl, 'babl_fish',                       vp,            [vp, vp])
proto(babl, 'babl_process',                    ctypes.c_long, [vp, vp, vp, ctypes.c_long])

proto(gimpcolor, 'gimp_color_profile_new_cmyk_builtin', vp, [])
proto(gimpcolor, 'gimp_color_profile_get_icc_profile',  vp,
      [vp, ctypes.POINTER(ctypes.c_size_t)])
proto(gobject,   'g_object_unref',                      None, [vp])

BABL_MODEL_FLAG_CMYK                  = 1 << 24   # babl.h
BABL_ICC_INTENT_RELATIVE_COLORIMETRIC = 1

babl.babl_init()

################################################################################
# Helpers

def fmt(name, space=None):
  if space:
    return babl.babl_format_with_space(name.encode(), space)
  return babl.babl_format(name.encode())

def is_cmyk_format(f):
  return bool(babl.babl_get_model_flags(babl.babl_format_get_model(f)) &
              BABL_MODEL_FLAG_CMYK)

def process(src_fmt, dst_fmt, data, n):
  dst_bpp = babl.babl_format_get_bytes_per_pixel(dst_fmt)
  src     = ctypes.create_string_buffer(bytes(data), len(data))
  out     = ctypes.create_string_buffer(n * dst_bpp)
  babl.babl_process(babl.babl_fish(src_fmt, dst_fmt), src, out, n)
  return out.raw

def floats(values):
  return struct.pack(f'<{len(values)}f', *values)

def unfloats(data):
  return struct.unpack(f'<{len(data) // 4}f', data)

def space_from_icc(icc):
  error = cp()
  space = babl.babl_space_from_icc(icc, len(icc),
                                   BABL_ICC_INTENT_RELATIVE_COLORIMETRIC,
                                   ctypes.byref(error))
  return space, (error.value.decode() if error.value else '')

################################################################################
# 1. CMYK(A) formats

TYPES = {'u8': 1, 'u16': 2, 'u32': 4, 'half': 2, 'float': 4, 'double': 8}

for t, bpc in TYPES.items():
  for alpha in (False, True):
    name = ('CMYKA ' if alpha else 'CMYK ') + t
    exists = bool(babl.babl_format_exists(name.encode()))
    check(f"format '{name}' exists", exists)
    if not exists:
      continue
    f   = fmt(name)
    n   = babl.babl_format_get_n_components(f)
    bpp = babl.babl_format_get_bytes_per_pixel(f)
    check(f"'{name}': {5 if alpha else 4} components", n == (5 if alpha else 4), str(n))
    check(f"'{name}': {n * bpc} bytes per pixel", bpp == n * bpc, str(bpp))
    check(f"'{name}': CMYK model", is_cmyk_format(f))
    check(f"'{name}': alpha {alpha}", bool(babl.babl_format_has_alpha(f)) == alpha)

for name in ('cmyk u8', 'cmykA u8', 'cmykA float', 'CaMaYaKaA float',
             'camayakaA float'):
  exists = bool(babl.babl_format_exists(name.encode()))
  check(f"format '{name}' exists", exists)
  if exists:
    check(f"'{name}': CMYK model", is_cmyk_format(fmt(name)))

check("'R'G'B'A float': not a CMYK model", not is_cmyk_format(fmt("R'G'B'A float")))
check("'Y' u8': not a CMYK model", not is_cmyk_format(fmt("Y' u8")))

################################################################################
# 2. premultiplied CMYK

check("format 'CaMaYaKaA double' exists",
      bool(babl.babl_format_exists(b'CaMaYaKaA double')))
u32 = bool(babl.babl_format_exists(b'CaMaYaKaA u32'))
print(f"info 'CaMaYaKaA u32' {'exists' if u32 else 'is missing'} in babl "
      f"(gimp_babl_init() registers it only when missing)")

################################################################################
# 3. CMYK space from the builtin profile

profile = gimpcolor.gimp_color_profile_new_cmyk_builtin()
check("gimp_color_profile_new_cmyk_builtin()", bool(profile))
if not profile:
  sys.exit(1)

length = ctypes.c_size_t(0)
data   = gimpcolor.gimp_color_profile_get_icc_profile(profile, ctypes.byref(length))
icc    = ctypes.string_at(data, length.value)
gobject.g_object_unref(profile)

check("builtin ICC data is a CMYK profile",
      len(icc) > 128 and icc[16:20] == b'CMYK',
      f"{len(icc)} bytes, color space {icc[16:20]!r}")

space, error = space_from_icc(icc)
check("babl space from the builtin ICC", bool(space), error)
if not space:
  sys.exit(1)
check("babl_space_is_cmyk (builtin)", bool(babl.babl_space_is_cmyk(space)))
check("babl_space_is_cmyk (sRGB) is FALSE",
      not babl.babl_space_is_cmyk(babl.babl_space(b'sRGB')))

for name in ('CMYK u8', 'CMYKA u16', 'CMYK float', 'CMYKA double'):
  check(f"'{name}' tagged with the builtin space",
        babl.babl_format_get_space(fmt(name, space)) == space)

################################################################################
# 4. I5 identities

rng = random.Random(4711)
N   = 1000

pixels = struct.pack(f'<{N * 5}H', *[rng.randrange(65536) for _ in range(N * 5)])
check("I5: CMYKA u16 (no space) -> CMYKA u16 (builtin space) is the identity",
      process(fmt('CMYKA u16'), fmt('CMYKA u16', space), pixels, N) == pixels)
check("I5: CMYKA u16 (builtin space) -> CMYKA u16 (no space) is the identity",
      process(fmt('CMYKA u16', space), fmt('CMYKA u16'), pixels, N) == pixels)

pixels = bytes(rng.randrange(256) for _ in range(N * 4))
check("I5: CMYK u8 (no space) <-> CMYK u8 (builtin space) is the identity",
      process(fmt('CMYK u8'), fmt('CMYK u8', space), pixels, N) == pixels and
      process(fmt('CMYK u8', space), fmt('CMYK u8'), pixels, N) == pixels)

pixels = floats([rng.random() for _ in range(N * 5)])
check("I5: CMYKA float (no space) <-> CMYKA float (builtin space) is the identity",
      process(fmt('CMYKA float'), fmt('CMYKA float', space), pixels, N) == pixels and
      process(fmt('CMYKA float', space), fmt('CMYKA float'), pixels, N) == pixels)

def max_float_diff(a, b):
  return max(abs(x - y) for x, y in zip(unfloats(a), unfloats(b)))

# not bit-exact: babl's float path goes through linear light and back
EPS = 1e-6

pixels = floats([rng.random() for _ in range(N * 4)])
d1 = max_float_diff(process(fmt("R'G'B'A float", space), fmt("R'G'B'A float"), pixels, N), pixels)
d2 = max_float_diff(process(fmt("R'G'B'A float"), fmt("R'G'B'A float", space), pixels, N), pixels)
check("I5: R'G'B'A float tagged with the CMYK space is sRGB (both directions)",
      d1 <= EPS and d2 <= EPS, f"max diff {max(d1, d2):.2e}")

pixels = floats([rng.random() for _ in range(N)])
d1 = max_float_diff(process(fmt("Y' float", space), fmt("Y' float"), pixels, N), pixels)
check("I5: Y' float tagged with the CMYK space is sRGB gray",
      d1 <= EPS, f"max diff {d1:.2e}")

################################################################################
# 5. lcms-support discriminator (gimp_babl_has_cmyk_support())

c50   = floats([0.5, 0.0, 0.0, 0.0])
rgb_s = unfloats(process(fmt('CMYK float', space), fmt("R'G'B' float"), c50, 1))
rgb_n = unfloats(process(fmt('CMYK float'),        fmt("R'G'B' float"), c50, 1))
check("lcms discriminator: |R'(space) - R'(naive)| > 0.1 for C50",
      abs(rgb_s[0] - rgb_n[0]) > 0.1,
      f"R' {rgb_s[0]:.3f} (space) vs {rgb_n[0]:.3f} (naive)")

k100 = floats([0.0, 0.0, 0.0, 1.0])
srgb = process(fmt('CMYK float', space), fmt("R'G'B' float"), k100, 1)
back = unfloats(process(fmt("R'G'B' float"), fmt('CMYK float', space), srgb, 1))
check("K100 -> sRGB -> CMYK (builtin space): K >= 0.98, C/M/Y <= 0.05",
      back[3] >= 0.98 and max(back[0:3]) <= 0.05,
      "(%.4f, %.4f, %.4f, %.4f)" % back)

################################################################################
# 6. the facts behind I8

again, error = space_from_icc(bytes(icc))
check("same ICC bytes -> the same babl space", again == space, error)

variant = bytearray(icc)
new_date = bytes([0x07, 0xE9, 0x0C, 0x1F, 0x17, 0x3B, 0x3B, 0, 0, 0, 0, 0])
if bytes(variant[24:36]) == new_date:
  new_date = bytes([0x07, 0xE8, 0x01, 0x02, 0x03, 0x04, 0x05, 0, 0, 0, 0, 0])
variant[24:36] = new_date
variant = bytes(variant)
check("header variant differs exactly in bytes 24-35",
      variant != icc and variant[:24] == icc[:24] and variant[36:] == icc[36:])

vspace, error = space_from_icc(variant)
check("header variant -> a CMYK babl space",
      bool(vspace) and bool(babl.babl_space_is_cmyk(vspace)), error)
check("header variant -> a different babl space (so GIMP canonicalizes, I8)",
      vspace != space)

if vspace:
  rgb_v = unfloats(process(fmt('CMYK float', vspace), fmt("R'G'B' float"), c50, 1))
  check("header variant space converts like the builtin space",
        max(abs(a - b) for a, b in zip(rgb_s, rgb_v)) < 1e-6)

################################################################################

if failures:
  print(f"\n{len(failures)} check(s) failed:")
  for f in failures:
    print(f"  {f}")
  sys.exit(1)

print("\nall babl CMYK checks passed")
sys.exit(0)

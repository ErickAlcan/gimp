# WS08 — Channels dialog, histogram, color tools, pickers, selectors, text

**Goal**: C/M/Y/K/Alpha components in the UI and the core histogram; color tools usable on
CMYK (RGB round trip, documented); pickers report native CMYK values without overflow; FG/BG
colors live in the image's CMYK space; select-by-color works; K-only text; MyPaint insensitive.

**Owned files** (PLAN §4.1): `app/core/gimphistogram.{c,h}`, `gimpdrawable-histogram.{c,h}`,
`gimpdrawable-levels.c`, `gimpdrawable-equalize.c`, `gimppickable.{c,h}`,
`gimpimage-pick-color.{c,h}`, `gimppickable-contiguous-region.cc`, `gimpchannel-select.c`;
color-tool ops/configs in `app/operations` (`gimp{brightnesscontrast,colorbalance,curves,
huesaturation,levels}config.c`, `gimpoperation{brightnesscontrast,colorbalance,colorize,curves,
desaturate,equalize,histogramsink,huesaturation,levels,pointfilter,posterize,threshold}.c`);
`app/tools/*`; `app/text/*`; `app/actions/channels-actions.c`, `channels-commands.c`;
`app/widgets/gimp{componenteditor,histogrameditor,histogramview,histogrambox,colordialog,
colorframe,colorhistory,fgbgeditor,samplepointeditor,colorbar}.c`;
`modules/color-selector-cmyk.c`; `libgimpwidgets/*` except 05's and 07's files;
`app/tests/test-cmyk-histogram.c`.

**Dependencies**: starts at the 01a merge. Acceptance needs 02 (mask-components for the eye /
affect toggles) and 05. Merges seventh.

**Steps**
1. Pickers (M5/m12, PLAN §5): `gimppickable.c:92,260,300`, `gimpimage-pick-color.c:158`,
   `gimpcolortool.c:633`, `gimpsamplepointeditor.c:557` → `MAX_CHANNELS` buffers; average in
   the native format for CMYK; `gimppickable-contiguous-region.cc` passes the buffer space;
   `gimpchannel-select.c` select-by-color on CMYK.
2. `gimpcomponenteditor.c`: Cyan, Magenta, Yellow, Black, Alpha for CMYK; per-component
   previews via space-tagged component formats (ink amount = white, D8); eye/affect toggles.
   `channels-actions/commands`: "channel from component" for CMYK components.
3. Histogram core (INTERFACES §9): native CMYKA float, VALUE = max ink, slot map,
   `has_channel`; `gimp_drawable_calculate_histogram_rgb()`; editor/view/box channel menus per
   base type.
4. Color tools (levels, curves, threshold, brightness-contrast, color balance, hue-saturation,
   colorize, desaturate, posterize, equalize, levels-stretch): on CMYK drawables use the RGB
   histogram and show "Operates on an RGB conversion of the CMYK data"; check
   `gimpcurvestool.c:809,1142`, `gimpwarptool.c`, `gimplevelstool.c`, `gimpthresholdtool.c`.
5. Color frame / pointer: CMYK mode default for CMYK images; values from the native pixel.
6. FG/BG in the image space (INTERFACES §11): `gimpcolordialog.c`, `gimpfgbgeditor.c`,
   `gimpcolorhistory.c`, `modules/color-selector-cmyk.c` and libgimpwidgets selectors use the
   **active image's** CMYK profile/space (not the simulation profile) when the image is CMYK.
7. Text (`app/text/*`): native solid-color path (INTERFACES §11: A8 coverage + native CMYK
   fill) when the image is CMYK, the text has one color, no markup colors, no outline;
   otherwise RGB render + conversion; `gimptextundo.c:253` base type.
8. MyPaint tool insensitive on CMYK drawables with the core's message (02 implements the core
   refusal).
9. Tests H1–H6 in `test-cmyk-histogram.c`; Tier A (#2b especially) on owned files.

**Acceptance**: H1–H6 pass; manual checklist items Channels dialog, pickers, histogram,
MyPaint insensitive; G1–G3 green.

**Risks**: histogram slot mapping bugs (RGB layout must not change); dependence on 02's
mask-components; text layout code paths with markup; selector code shared with RGB images.


## Status

Branch `ws/08`, 7 commits on `cmyk-01a-merged` (29a24b9..4b0f284). Build warning-free in
owned files; `meson test`: 27 OK, 3 SKIP (compositing/image skeletons, babl probe), 1 FAIL
(`appdata_file`, network = baseline). **G1-G3 green.**

**Landed**
* Histogram (INTERFACES §9): CMYK(A) float in the buffer space; slots VALUE 0, C..K 1..4,
  alpha 5 (CMYKA only); VALUE = max ink; `has_channel()` per color model; nested statistics
  use the unmapped channel (RGB results unchanged). New `gimp_histogram_is_cmyk()`,
  `gimp_histogram_clear_values_for_format()`, `gimp_drawable_calculate_histogram_rgb[_async]()`.
  Deviation: a CMYK drawable *without* alpha gives n_components 4 (C,M,Y,K) and no ALPHA
  channel, matching RGB; with alpha 5.
* Pickers (PLAN §5): `gimppickable.c` `pixel[]`/`sample[]` → `MAX_CHANNELS`;
  `gimpcolortool.c:633`, `gimpsamplepointeditor.c:557` → `MAX_CHANNELS`; CMYK averages are
  premultiplied ink averages (`CaMaYaKaA double`, image space) via the new
  `gimp_pickable_average_color_native()` (used by `gimp_pickable_get_pixel_average()` for
  CMYK formats and by the show-all pick), because every pickable's `get_pixel_average()`
  calls `gimp_gegl_average_color()` (RGB, owner 02). Show-all pick no longer dereferences a
  NULL `pixel`/`sample_format`. Contiguous region / select-by-color: composite criterion uses
  the buffer space for CMYK (RGB/GRAY unchanged: sRGB).
* Color frame: pixel mode shows C/M/Y/K(/A) natively; CMYK mode uses the color's own space
  for CMYK colors.
* Color tools: curves/levels/threshold use the RGB-conversion histogram and offer R,G,B on
  CMYK; levels-stretch and equalize too; filter tools show "Operates on an RGB conversion of
  the CMYK data" for point operations on CMYK drawables. Levels config ignores channels
  outside VALUE..ALPHA (previously an array overrun for LUMINANCE/RGB too).
* -Wswitch resolved: gimphistogram.c, gimplevelsconfig.c, gimpoperationthreshold.c,
  gimpcurvestool.c (2), gimplevelstool.c (2), gimpthresholdtool.c, gimpcolorbar.c,
  gimpcomponenteditor.c.
* Component editor: Cyan, Magenta, Yellow, Black (+Alpha). Channels actions/commands need no
  change (generic over `gimp_channel_new_from_component()`).
* CMYK selector module: uses the active CMYK image's profile (from `set_format`) before the
  simulation/preference profile → colors are in the image space.
* Text: solid-color, no markup colors, no outline on CMYK → A8 coverage + native fill
  (K-only stays K-only); otherwise RGB render + babl conversion (rich black, documented).
  `gimptextundo.c:253` verified (base type from format, CMYK via 01a).
* MyPaint tool: bad cursor + refusal message on CMYK drawables.

**Acceptance**
| id | result |
|---|---|
| H1 histogram (+ no-alpha, RGB regression) | pass (u8/u16/half/float/double) |
| H2 pickers, CMYKA double, canary after 40 bytes | pass |
| H3 select by color (K100 vs rich black) | pass |
| H4 K-only text (+ two-color markup fallback) | pass (fonts loaded by the test's own init) |
| H5 CMYK selector → image space, buffer fill exact | pass; FG-via-context fill **TODO** (see requests) |
| H6 Levels on CMYK, outside selection bit-identical | pass |
| Manual checklist (Channels dialog, pickers, histogram, MyPaint) | not run (no UI session) |

Tier A #2b on owned files: fixed as above; remaining hits are explicit RGB formats
(`gimpcurvestool.c:810,1158`, `gimplevelsconfig.c:625,698`, `gimpcolorframe.c:910`,
`gimpcolorbar.c:255`, text `color[3]`), the selector's `cmyk_values[4]` (CMYK float, no
alpha), iscissors (non-pixel).

**Pending integration / requests for other owners**
* libgimpcolor `gimpcolor.c` `gimp_color_set_alpha()` (unowned, ask coordinator/01b):
  round-trips through RGBA, so `gimp_context_set_foreground()` turns an image-space CMYK FG
  into a converted RGB color (K100 → rich black). Fix: for CMYK formats get/set
  `CMYKA double` in the same space and change only alpha. H5 marks this `g_test_incomplete`.
* 02: `gimp_gegl_average_color()` should average CMYK natively (PLAN §5); then the
  workaround in `gimp_pickable_get_pixel_average()` can go. `gimp-gegl-loops.cc` `total[4]`
  (convolve) is 02's.
* 07: `gimpcursorview.c:763` `pixel[4]` → `MAX_CHANNELS`; `gimpviewrendererimage.c`
  component previews index the sRGB preview buffer by component index (CMYK K would show
  alpha) — render CMYK component previews from `gimp_image_get_component_format()`;
  channel icons for CMYK components; `gimpcoloreditor.c` already passes the image format
  (the selector fix covers it).
* 03: `gimpdrawable.c`/`gimpimage.c`/`gimpprojection.c` `get_pixel_average` (see 02 note).

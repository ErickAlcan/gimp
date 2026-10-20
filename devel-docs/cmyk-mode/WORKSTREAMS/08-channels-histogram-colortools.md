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

**Status**: (agent fills in)

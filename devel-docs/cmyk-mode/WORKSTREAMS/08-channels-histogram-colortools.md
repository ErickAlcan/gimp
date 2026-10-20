# WS08 — Channels dialog, histogram, color tools, color selectors

Goal: C/M/Y/K/Alpha component handling in the UI and core histogram; color tools usable on CMYK
drawables (RGB round trip); color pickers/selectors show CMYK values.

Owned files: PLAN §3 (08).

Steps
1. gimpcomponenteditor.c: list CYAN, MAGENTA, YELLOW, BLACK, ALPHA for CMYK; previews per
   component (space-tagged component formats); eye/affect toggles.
2. channels-actions/commands: "channel from component" for CMYK components.
3. gimphistogram.c / gimpdrawable-histogram.c: native CMYKA float histogram (INTERFACES §9),
   VALUE = max ink; `gimp_drawable_calculate_histogram_rgb()`.
4. Histogram editor/view/box: channel menus per base type; ink-colored channel display.
5. Tools (levels, curves, threshold, color balance etc.): for CMYK drawables use RGB histogram,
   show info line; color picker tool and pointer/color frames report CMYK (GimpColorFrame CMYK
   mode default for CMYK images).
6. gimpcolordialog/fgbg editor/color history/modules color-selector-cmyk.c: for CMYK images use
   the image profile (not the simulation profile) as CMYK space; keep GeglColor native CMYK.
7. Tier A audits; T16, T17 (core part), T21 locally.

Acceptance: T21 passes; manual B5 items 3, 4, 5.

Risks: depends on 02's mask-components for visibility/affect; histogram index mapping bugs.

Status: (to be filled)

# REVIEW-02: final code review of the CMYK-mode branch

Reviewer: independent senior review (did not take part in the implementation).
Scope: `git diff 5a569ea..cmyk-09-merged` (da9e756; 108 commits + 13 merges, 221 files,
+23 009 / −778), read directory by directory against PLAN.md rev 2, INTERFACES.md,
REVIEW-01.md and every `WORKSTREAMS/*.md` Status section.

What was run (all on this box; the probe scripts `craft.py`, `load-hostile.py`, `affect-none.py`, `leak-rgb.py`, `overlay-legacy.py`, `i8-header.py` live in the reviewer scratchpad `/tmp/claude-0/-home-claude/7c01abe6-401a-55a5-863e-17260773fab0/scratchpad/craft/` and are not part of the branch):

* Full suite on `/root/gimp/_build` (`meson test -C _build --no-rebuild --num-processes 1`):
  **32 tests, 31 OK, 1 FAIL (`gimp:desktop / appdata_file`, network)**: STATUS.md §2 is
  confirmed. Inside the TAP suites there are exactly two skips: P1/P2 (`GIMP_TESTING_PERF`)
  and I1's optional real-profile case.
* ASan (`/root/wt/09/_build-asan`, same tree as `cmyk-09-merged`): core, layer-modes-golden,
  cmyk-foundation, cmyk-compositing, cmyk-image, cmyk-histogram, cmyk-nosupport, cmyk-ui,
  xcf at the default precision, and compositing/image/histogram/xcf with
  `GIMP_TESTING_CMYK_PRECISION=double`: **all OK, no AddressSanitizer report**.
  LeakSanitizer (`detect_leaks=1`) on foundation, compositing and image: no leak with a CMYK
  frame; what is reported is babl's fish cache, a `GeglColor` in `_gimp_paintbrush_motion`
  and `xcf_load_parasite` (all upstream code, see m14).
* A baseline build of 5a569ea (`/root/wt/review-base/_build`) to decide what is pre-existing.
* Hostile inputs (13 crafted TIFF/PSD/JPEG files, loaded headless through the python runner,
  the TIFF plug-in also under valgrind): see M2.
* A trial build of the M1 fix (`/root/wt/review/_build`), see M1.

Severity scale as in REVIEW-01: **Blocker** = wrong pixels, crash or data loss in a core use
case, or a broken public contract; **Major** = significant defect or regression that must be
fixed before upstream submission; **Minor** = accuracy, hygiene, small gaps.

---------------------------------------------------------------------------------------------

## Verdict (short)

**No blocker.** The architecture of rev 2 is implemented as designed and the core invariants
hold in code, not only in documents (REVIEW-01 resolution check below). Same-profile copies,
paint, flatten, conversions with undo, XCF, TIFF/PSD/JPEG round trips are exact, and the
tests really assert it. **Merge readiness for the `cmyk-mode` integration branch: yes, after
M1** (a small RGB regression, one-hunk fix, trial-tested). **Upstream readiness: not yet**;
M2–M4 and the hygiene items in "Remaining work for upstream" are needed first (estimate at the
end).

---------------------------------------------------------------------------------------------

## REVIEW-01 items: verified in code

| Item | Verdict | Evidence |
|---|---|---|
| B1 same-profile re-separation | Resolved | I8 cache in `gimp_color_profile_get_space()` (`libgimpcolor/gimpcolorprofile.c:1802-1866`, SHA-256 of bytes after the header + length, under a mutex held across `babl_space_from_icc`); equal-CMYK re-encoding path in `gimp_color_transform_new()` (`gimpcolortransform.c:226-264`, also with `GIMP_COLOR_TRANSFORM_DISABLE_BABL`); `can_gegl_copy()` FALSE for unequal CMYK (`:694-696`); fixed header date / zero ID of the builtin (`gimpcolorprofile.c:1692-1703`); equality short-circuit in `gimp_layer_new_convert_buffer()` (`app/core/gimplayer-new.c:249`); `gimpsavable-load.c:2231-2238` goes through `get_space`. Tests F8/F9/F9b, I9/I10, X3, Z (paste K100 bit-identical). Residual: m4. |
| B2 paint core op | Resolved | `gimp_layer_mode_get_operation_for_format()` (`gimp-layer-modes.c:1473-1527`) + `gimp_operation_layer_mode_set_fixed_format()`; `DoLayerBlend` (`gimppaintcore-loops.cc:1905-1930`). G2, C9. Residual: m7. |
| B3 composite-space contract | Resolved | `cmyk-native` property (`gimpoperationlayermode.c:217-222`), `set_mode_full()` (`gimp-gegl-nodes.c:228-263`), `gimp_layer_update_mode_node()` passes the stored space (`gimplayer.c:746-762`), `create_flatten_node (bg, format, space)` (`gimp-gegl-nodes.c:51-112`). C1, C8, C13. |
| M1 two-stride `real_process` | Resolved | CMYK branch `gimpoperationlayermode.c:727-809` (sample-indexed runs, 4-stride scratch, 5-stride `blend_out`); RGB branch textually unchanged; 33 blend functions on `n_components = 4` with `gimp_operation_layer_mode_get_blend_format()` (`gimpoperationlayermode-blend.c:90…`). Golden G1–G3 bit-identical. |
| M2 performance | Partly | lcms u16 device-link converter (`gimpoperationlayermode-cmyk.c`), P1 met; **P2 missed** (M3). |
| M3 libgimpcolor holes | Resolved | `get_lcms_format()` CMYK branch first (`gimpcolorprofile.c:1963-1996`), u32/half/float/double/inverted/premultiplied → u16; `TYPE_CMYKA_8` defined. F11. K-preserving intents deferred (documented). |
| M4 builtin profile | Resolved | 9⁴/17³ linked LUTs, 79 432 bytes, deterministic (F3). Serialization size deferred (M4 below). |
| M5 stack buffers | Resolved | `gimppickable.c:103,355,400`, `gimpcolortool.c:633`, `gimpsamplepointeditor.c:557`, `gimpcursorview.c:763`, `gimp-gegl-loops.cc:305,356` all `MAX_CHANNELS`; ASan at DOUBLE clean (H2 has a canary after 40 bytes). A tree-wide grep of `[3]`/`[4]` pixel buffers found only explicit-RGB-format users. |
| M6 ownership | n/a (process) | Respected, with documented cross-owner edits (db1645f, 1756b4d). |
| M7 previews/thumbnails | Resolved | `gimpdrawable-preview.c:182`, `gimpimage-preview.c:54`, `gimpbuffer.c:250`, renderer pairing `gimpviewrenderer.c:1170-1180`; L checks bpp 3/4. |
| M8 legacy modes | Resolved, with a leftover | I9 enforced in convert step 1, `gimp_layer_set_mode()`, paint/fill, XCF load, PSD load; safety net in the op (`gimpoperationlayermode.c:387-406`). Leftover private table: m1. |
| M9 text | Resolved | `gimp_text_layer_render_layout_cmyk()` (`app/text/gimptextlayer.c:1164-1334`), H4, Z. |
| M10 tests | Resolved | Golden test landed first (2f8ed5d); tests assert (no print-only checks found); DOUBLE and ASan runs done. Residual: m10. |
| M11 undo / I1 transient | Resolved | Push order `gimpimage-convert-type.c:447-537` matches INTERFACES §6; no I1 enforcement in parasite/undo code (only `gimpimageundo.c:342-343` emits profile-changed); transient absorbed in `gimp_image_get_layer_space()` (`gimpimage.c:2490-2518`) and `gimp_image_create_color_transforms()`; I2 cycles undo/redo ×3 with bytes compared. |
| M12 WS split | n/a (process) | Done. |
| M13 MyPaint | Resolved | `gimpmybrushcore.c:161`, `gimpmybrushtool.c:165`. |

---------------------------------------------------------------------------------------------

## Blockers

None.

---------------------------------------------------------------------------------------------

## Major

### M1. RGB/GRAY regression: an empty component mask is no longer empty (I7)

**Evidence.** I7 sets `GIMP_COMPONENT_MASK_BLACK` for every non-CMYK image
(`app/core/gimpimage.c:4115, 4124, 4209, 4218`). `gimp_drawable_get_active_mask()`
(`app/core/gimpdrawable.c:1742-1766`) only normalises the alpha bit, so when a user switches
off every component of an RGB image (or R, G, B of an image without alpha), the mask is
`BLACK|ALPHA` (0x18) instead of 0. Every "nothing to do" test that relied on 0 now fails:
`gimp_paint_core_paste()` / `_replace()` (`app/paint/gimppaintcore.c:1290, 1508`,
`if (! affect) return;`) and the `mask == 0` fast paths of `gimp:mask-components`
(`gimpoperationmaskcomponents.cc:595, 624`).
**Measured** (script `affect-none.py`: RGB image, R/G/B inactive, `clean_all()`, one
`Gimp.pencil` stroke): baseline 5a569ea → image **not dirty**; `cmyk-09-merged` → image
**dirty** (an undo step is pushed and the whole stroke is processed, pixels unchanged).
The golden test cannot see it (it never deactivates components).

**Fix** (trial-built in `/root/wt/review`, see "Trial fix" below): treat BLACK like alpha in
`gimp_drawable_get_active_mask()` for non-CMYK drawables, before the alpha normalisation:

```c
if (! gimp_drawable_is_cmyk (drawable))
  {
    if (mask & ~(GIMP_COMPONENT_MASK_ALPHA | GIMP_COMPONENT_MASK_BLACK))
      mask |= GIMP_COMPONENT_MASK_BLACK;
    else
      mask &= ~GIMP_COMPONENT_MASK_BLACK;
  }
```

`GIMP_COMPONENT_MASK_ALL` still means "all" (any active colour component keeps BLACK set),
and an all-inactive mask is 0 again. Add a golden/regression check: RGB, all components
inactive, paint → image clean.

### M2. File loaders: three memory-safety bugs on the CMYK import paths (pre-existing, now on the native path)

All three reproduce on the baseline build too (same counts), so they are **not introduced**
by the branch; they are listed as Major because the branch made these the default code paths
for CMYK files (TIFF `PHOTOMETRIC_SEPARATED` was rewritten in `c1d426e`, the JPEG export now
relies on multi-marker APP13 joining, `44b8606`) and REVIEW-02 was asked to cover them.

| File (crafted) | Result | Where |
|---|---|---|
| TIFF, Photometric SEPARATED, InkSet CMYK, **SamplesPerPixel 2** (or 3) | Loads as a *native CMYK* image; valgrind: **1144 invalid reads** in `load_contiguous` (`plug-ins/file-tiff/file-tiff-load.c:2382`); "Suspicious number of extra channels: 65534" | `get_extra_channels_count()` returns `spp - 4 - alpha` as `gushort` (`:276-277`) and the SEPARATED case (`:1108-1215`) never checks `spp >= 4 (+ alpha)`; the CMYK base format (4/5 components) is then read from a 2-sample strip. |
| PSD, CMYK mode, one layer with channels C, M and alpha only | **file-psd segfault** | `add_layers()`: `channel_idx[]` (`psd-load.c:2786`) is filled for `layer_channels` (2) entries, the copy loop runs `cidx < base_channels` (5) and dereferences `lyr_chn[channel_idx[cidx]]` with uninitialised indices (`:3414-3432`). |
| JPEG CMYK with a 4-byte APP13 marker | **file-jpeg aborts** (g_realloc of ~2⁶⁴ bytes) | `jpeg-load.c:382-392`: `len - sizeof (JPEG_APP_HEADER_PS)` on a `gsize` without checking `len` or the "Photoshop 3.0" header; this is the joining code 44b8606 now depends on. |

The other ten crafted files fail or degrade cleanly: 1-byte ICC tag/resource → default
profile + message; truncated/garbage "CMYK" ICC → refused as one-way, RGB import; ICC resource
longer than the resource block → ignored; PSD with 3 channels in CMYK mode → "Not enough
channels"; short TIFF strip → libtiff error, no crash.

**Fix.** TIFF: in `case PHOTOMETRIC_SEPARATED`, refuse (or fall back to the worst-case RGBA
path) when `spp < 4 + (alpha ? 1 : 0)`, and make `get_extra_channels_count()` return 0 instead
of underflowing. PSD: after building `channel_idx[]`, fail the layer ("not enough channels")
when `layer_channels < base_channels` (before `base_channels++` for alpha), and initialise
`channel_idx[]`. JPEG: skip APP13 markers with `len <= sizeof (JPEG_APP_HEADER_PS)` or without
the header string. Submit these three as separate upstream commits (they also affect the RGB
import of CMYK files on current master); add the crafted files as regression fixtures (they are
tiny and generated by a 150-line script, `craft.py`).

Also (same area, Minor): `peek_icc_profile()` does `g_malloc (res_a.data_len)` with a length
only bounded by the resource-block length read from the file (`psd-load.c:1801`); use
`g_try_malloc` or bound it by the file size, like the TIFF/JPEG ICC readers do via libtiff/libjpeg.

### M3. Performance: P2 missed (≈4× RGB with two threads) and the contention is unexplained

**Evidence.** STATUS §2: CMYK Normal projection 0.61–0.67 s vs RGB 0.13–0.17 s with two GEGL
threads, 1.90× pinned to one core; the target was 1.5×. The single-thread part is babl's
missing CMYK u16 ↔ CMYKA float fast path (measured by 02/09). The two-thread slowdown is
specific to CMYK and nobody found its cause. Candidates visible in the code:
`gimp_operation_layer_mode_cache_fishes()` now calls
`gimp_operation_layer_mode_get_cmyk_format()` → two `gegl_operation_get_source_format()`
lookups on **every** `real_process()` call (`gimpoperationlayermode.c:1100`, this also adds
work for RGB blend modes, m8), `format_ops_mutex` (paint only) and babl's internal fish cache
lookups per chunk. For an upstream feature whose main promise is "native CMYK editing", a
4× slower projection of the commonest mode will be noticed on real (12–50 Mpx) print files.

**Fix.** Before submission: profile the two-thread case (perf/`GEGL_THREADS=2`) and remove
per-chunk lookups from the hot path (resolve the CMYK format once in `prepare()`; cache
`cmyk`/`blend_format` per op and read them without re-querying the graph); register babl
conversions for `CMYK(A) u8/u16 ↔ CMYKA float` per CMYK space (or land the babl fast path
upstream, PLAN R2). Make P2 an asserted test once it passes, still gated on
`GIMP_TESTING_PERF` so it can't flake in CI.

### M4. Serialized CMYK colours embed the full ICC profile (~106 KB each)

**Evidence.** Accepted for the MVP (D3/M4), but it is an upstream acceptance risk rather than
a cosmetic one: every `GeglColor` in a CMYK space is written with its space's ICC in base64 —
FG/BG in `contextrc`, each entry of the colour history (`colorrc`, up to 12 colours), every
tool-options file that stores a colour, palettes, every serialized colour in XCF (text, fill,
stroke, effects), every plug-in wire round trip and DnD. A user who picks a dozen CMYK colours
gets megabytes of config that is parsed at every start-up, and each XCF colour property costs
~80 KB. Upstream reviewers will ask for the named-reference scheme.

**Fix.** Implement the deferred "named builtin" serialization (a profile-ID/hash reference
plus the bytes only when the profile is not the builtin or the image profile), or at minimum
deduplicate per file (XCF: reference the image's `icc-profile` parasite). Needs the
coordinated change listed in PLAN §7 (libgimpconfig, xcf, gpparams, palettes).

---------------------------------------------------------------------------------------------

## Minor

**m1. Leftover private legacy-mode table, inconsistent with the official one.**
`app/core/gimplayer.c:2242-2276` (`gimp_layer_get_non_legacy_mode()`, "TODO: replace by
gimp_layer_mode_get_non_legacy() … once that is merged") is still used by
`gimp_layer_set_mode()` (`:3088`) and `gimp_layer_map_legacy_mode()` (`:3174`), i.e. by the
image conversion, XCF load and `set_mode` paths. It maps `OVERLAY_LEGACY → OVERLAY`, while
02's `gimp_layer_mode_get_non_legacy()` (`gimp-layer-modes.c:1262`) maps it to `SOFTLIGHT`
(the legacy op is soft light). Today the difference is latent because the PDB and the XCF
loader turn OVERLAY_LEGACY into SOFTLIGHT_LEGACY first (checked: `convert_cmyk` of an
"Overlay (legacy)" layer yields `softlight`), but any core caller passing OVERLAY_LEGACY gets
a different look. Fix: delete the private table and call `gimp_layer_mode_get_non_legacy()`.

**m2. Private copy of `gimp_image_fix_layer_format_spaces()`.**
`gimp_image_convert_type_fix_layer_spaces()` (`gimpimage-convert-type.c:344`) duplicates the
static function of `gimpimage-color-profile.c` (03's documented deviation). Export the
original (`gimpimage-color-profile.h`) and drop the copy.

**m3. Stale integration workarounds in the loaders.** `file-tiff-load.c:1832-1837` and
`psd_ensure_layer_alpha()` (`psd-load.c:1844-1853`, three call sites) add alpha "until
gimp-layer-new honours GIMP_CMYKA_IMAGE (WS05)"; WS05 merged that (layer.pdb `layer_new`
CMYKA → `has_alpha = TRUE`). Remove them.

**m4. I8 ignores colorimetrically relevant header fields.** The I8 key (and
`gimp_color_profile_is_equal()`) skip the whole 128-byte header, including version (8-11),
class (12-15), PCS (20-23) and rendering intent / illuminant (64-79). **Measured**
(`i8-header.py`): the builtin and a copy with header version 2.1 are `is_equal`, get the
**same babl space**, but lcms perceptual+BPC maps K100 to sRGB16 (3103, 3136, 3122) for the
builtin and (0, 0, 0) for the variant (v4 vs v2 perceptual black point). So two images "in
the same colour space" convert differently to RGB, and the babl/display path uses whichever
header was seen first in the process. Practical risk is low (date-only differences are the
real case), but the fix is cheap: put bytes 8-23 and 64-79 into the I8 key (keep excluding
date, creator, flags, ID), i.e. an `is_equal`-for-colorimetry helper used by the I8 cache and
by the equal-CMYK branches of `GimpColorTransform`/`gimp_layer_new_convert_buffer()`. Header
variants that differ only in the date (F8/F9/X3) keep sharing a space. SHA-256 collisions are
not a practical concern; the key also includes the length.

**m5. Hot-path cost of the I8 key and of profile objects.** `gimp_color_profile_get_space()`
hashes the whole profile (79 KB SHA-256 for the builtin) on every call before the lookup
(`gimpcolorprofile.c:1816-1830`), and `gimp_babl_format_get_color_profile()` parses the ICC
with lcms on every call for CMYK formats (`gimp-babl.c:561-568`; called per pointer motion by
`gimpcolorframe.c:790, 886, 959`). Cache the key on the `GimpColorProfile` (private field or
qdata) and the profile per babl space.

**m6. `g_printerr` diagnostics in library and plug-in code.** `gimpcolortransform.c:261`,
`gimpoperationlayermode-cmyk.c:146`, `gimp-gegl-loops.cc:1179`, `file-tiff-load.c`
(three), `jpeg-load.c`, `psd-load.c`: use `g_warning`/`g_debug`, or propagate a GError
(`gimp_image_set_color_profile()` failures in the loaders should be load errors or messages).

**m7. `DoLayerBlend` lost its format check for CMYK.** In the CMYK branch
`iterator_format = paint_format` (`gimppaintcore-loops.cc:1917-1921`), so the
`g_return_if_fail (… == iterator_format)` at `:1930` is a tautology. Assert
`paint_format == babl_format_with_space ("CMYKA float", paint_format)`: a CMYK paint buffer in
any other encoding would be processed as floats with the wrong stride.

**m8. Extra per-chunk work for RGB blend modes.** See M3: `get_cmyk_format()` runs for every
non-legacy op with `cmyk-native` TRUE (the default) on each `real_process()`, RGB included.
The golden test proves bit-identity, not cost; measure RGB Multiply projection vs baseline and
move the resolution to `prepare()`.

**m9. Converter lifetime at exit.** `gimp_layer_mode_cmyk_converters_exit()` frees converters
that graph ops still point to (`op->cmyk_converter`); harmless only because nothing composites
after `gimp_layer_modes_exit()`. Document it, or clear the cached pointers via the existing
`cached_fish_format` invalidation.

**m10. Test portability and tolerances.**
* `test-cmyk-e2e.py:165-167` asserts `ctypes.util.find_library ('lcms2')`; on the Windows and
  macOS CI jobs that lookup fails or finds a different lcms than babl's. Skip (exit 77) instead
  of failing, or get the library path from the build (`GIMP_TESTING_ABS_TOP_BUILDDIR`/meson).
  `test-cmyk-file-formats.py:211` already degrades; `cmyk-babl-probe.py` hard-codes
  `libbabl-0.1.so.0` (Linux only; fine if it skips elsewhere — check it does on MSYS2).
* I3 compares CMYK → RGB/GRAY against lcms with a tolerance of 257 (1/255 in u16,
  `test-cmyk-image.c:926, 938`) while Z shows the same path is exact (0/65535); tighten to 1–2.
* The python runner's new crash detection (05) is good: verified that the old runner hid the
  "GeglBuffers leaked" line that the new one shows (see m14).

**m11. libgimp enum docs.** `GIMP_HISTOGRAM_CYAN … BLACK` in the generated
`libgimp/gimpenums.h` have no "Since: 3.4" (the libgimpbase values do). Add `(Since: 3.4)` to
the `desc` comments in `app/core/core-enums.h` so pdbgen/mkenums carry it.

**m12. Unrelated RGB/GRAY bug fixes bundled in CMYK commits.** `fee156d` changes
`gimp_image_parasite_attach()` so that a *simulation* profile equal to the builtin no longer
detaches the image profile (`gimpimage.c:4876-4881`), and fixes GRAY `visible[ALPHA]` →
`visible[ALPHA_G]` (`:4217`). Both are correct and change RGB/GRAY behaviour; submit them as
separate upstream commits with their own tests.

**m13. Export options accept CMYK-only exporters but don't convert RGB for them.**
`gimp_export_options_get_image()` now accepts a capability set with only `CAN_HANDLE_CMYK`
(`libgimp/gimpexportoptions.c:624-629`), but the RGB/GRAY/INDEXED cases have no "convert to
CMYK" action, so such an exporter receives RGB drawables. Either refuse CMYK-only sets (keep
the old `g_return_val_if_fail`) or document that a CMYK-only exporter must handle that itself.

**m14. Upstream leaks seen by the new runner (not CMYK).** "2 GeglBuffers leaked" after a
libgimp copy/paste: reproduced on the **baseline** binary with the same script once it runs
under the branch's runner (`pygimp/run.py`); the old runner just did not show it, so STATUS's
"pre-existing" is right. `xcf_load_parasite()` leaks every parasite's string
(`g_string_free (parasite_data, FALSE)`, `app/xcf/xcf-load.c:5688`), which CMYK makes ~80 KB per
XCF load. Report/fix both upstream separately.

**m15. Production comments reference the work orders.** ~90 added lines in non-test code cite
`INTERFACES §…`, `WS0x`, `I1…I10`, `REVIEW-01`, `D16`; `devel-docs/cmyk-mode/` is not an
upstream document. Before submission: rewrite these comments to say *what* the invariant is
(e.g. "a CMYK image always has a CMYK profile") and drop the `devel-docs/cmyk-mode` tree from
the submission branch (or condense it into one `devel-docs/CMYK.md` design note).

**m16. Small behaviour notes.** FG/BG keyboard actions (`context-commands.c:135-430`) edit
the colour in sRGB and thus re-separate an image-space CMYK FG (undocumented); templates with
`base-type cmyk` make `templaterc` unreadable for older GIMP versions (downgrade only); the
RGB→GRAY convert dialog now receives the configured intent/BPC instead of 0/FALSE
(`image-commands.c:325-326`, not used for RGB, harmless).

**m17. One-way CMYK profiles still reach babl RGB→CMYK in two widgets (pre-existing).**
The loaders no longer attach input-only profiles as soft-proofing profiles (06), but the CMYK
selector (`modules/color-selector-cmyk.c`, `colorsel_cmyk_get_space()`) and the colour frame's
CMYK mode for RGB images (`gimpcolorframe.c:1165-1175`) still build a babl space from the
soft-proofing or *preference* CMYK profile without `gimp_color_profile_is_cmyk_bidirectional()`
and convert RGB → CMYK through it — the babl crash of REVIEW-01 m3 when a user selects an
input-only profile in Preferences or `gimp-image-set-simulation-profile`. Add the
bidirectional check there (fall back to "no profile") and to
`gimp_image_set_simulation_profile()` / the preferences validation.

---------------------------------------------------------------------------------------------

## Checked and found OK (selection)

* **ABI/API**: only appended enum values (`GIMP_CMYK` 3, `CMYK(A)_IMAGE` 6/7, channel types
  6–9, `CAN_HANDLE_CMYK` 1<<11, histogram 7–10 with the documented gap at 6) and new symbols;
  `gimp.def` (+2) and `gimpcolor.def` (+2) match the new public functions; no signature changes
  (the `gimpimageconvert_pdb.h` diff is re-alignment plus the `G_GNUC_INTERNAL`
  `_gimp_image_convert_cmyk`); `Since: 3.4` on all new libgimp/libgimpcolor/libgimpbase symbols
  (except m11). GIR/typelib build and contain the new members (`Gimp-3.0.gir`); Script-Fu
  constants come from the GIR (checked by 05).
* **Generated files** only in the two `regen:` commits (703b75b, c6405d9); the tree regenerates
  to the committed state (`ninja -n` has nothing to do, `git status` clean).
* **Thread safety**: the I8 cache and the converter cache are mutex-protected and never free
  entries during runtime; lcms transforms shared across GEGL threads are safe (lcms copies its
  1-pixel cache to the stack per call); `GOnce` for the builtin and the support probe.
* **Memory**: converters hold their two `GimpColorTransform`s for the process lifetime (one per
  CMYK space, freed at exit); per-format paint ops are freed in `gimp_layer_modes_exit()`; no
  per-call `cmsHTRANSFORM`; `real_process` scratch is `gegl_scratch` (bounded by
  `GIMP_COMPOSITE_BLEND_MAX_SAMPLES`), no stride overflow (`i * n_components` ≤ 5 × max
  samples); LSan shows no CMYK frame.
* **Histogram slot map**: VALUE/C/M/Y/K/A → 0..5 only when `priv->cmyk`; RGB keeps
  enum = slot; CMYK channels rejected for RGB histograms and vice versa (`gimphistogram.c`
  `map_channel`, `has_channel`); mean/median/std-dev use the unmapped channel for nested calls.
* **Component masks**: CMYK bit map C/M/Y = R/G/B bits, A = bit 3, K = bit 4 used consistently
  (`gimpimage.c`, `gimpoperationmaskcomponents.cc` `cmyka_bits[]`); 5/10/20/40-bpp variants;
  `ALL == 0x1f` passthrough preserved for RGB (apart from M1's empty mask).
* **M11 undo order** and **XCF version selection** (RGB/GRAY unchanged; CMYK → 28; header and
  layer types gated on version 28; no-support error is `G_FILE_ERROR_NOSYS` without the
  "corrupt" prefix).
* **PDB validation** does not reject previously valid RGB/GRAY calls: the new helpers only
  reject CMYK types on non-CMYK images and non-CMYK/GRAY types on CMYK images
  (`gimppdb-utils.c:648-857`).
* **APP13 splitting** (`jpeg-export.c:726-746`): chunks ≤ 65533 − header, each with the header,
  re-joined by the loader (subject to M2's loader fix).

---------------------------------------------------------------------------------------------

## Trial fix for M1

The hunk shown in M1 was applied (uncommitted) in `/root/wt/review` on top of
`cmyk-09-merged` and built in `/root/wt/review/_build`:

* `affect-none.py`: image **not dirty** after the stroke (as on 5a569ea).
* Full suite: 32 tests, 31 OK, 1 FAIL (`appdata_file`, network) — unchanged.
* `GIMP_TESTING_CMYK_PRECISION=double`: layer-modes-golden, cmyk-compositing, cmyk-image,
  cmyk-histogram OK.

The fix is not committed on `review/final` (this branch only carries the review); the owner
of `app/core/gimpdrawable.c` (01a) should land it with a regression test.

---------------------------------------------------------------------------------------------

## Remaining work for upstream submission (estimate)

| Work | Estimate |
|---|---|
| M1 fix + regression test | 0.5 day |
| M2 loader fixes (TIFF spp, PSD channel count, JPEG APP13, PSD ICC alloc) + fixtures, as separate upstream MRs | 1–1.5 days |
| M3 P2: profile the two-thread case, move CMYK resolution to `prepare()`, babl fast path or registration (+ babl MR) | 3–5 days (babl review cycle on top) |
| M4 named-reference colour serialization (config, XCF, wire, palettes, DnD) | 4–6 days incl. compatibility tests |
| m1–m3, m6, m7 cleanups (leftover tables/copies/workarounds, diagnostics) | 0.5–1 day |
| m15 comment rewrite, drop `devel-docs/cmyk-mode`, split bundled RGB fixes (m12), squash fix-ups into reviewable topic commits (enums+regen, libgimpcolor, compositing, core, xcf, pdb/libgimp, plug-ins, UI, tests) | 2–3 days |
| Upstream coordination: XCF 28 number (R10), `GIMP_CMYK` enum values in libgimpbase, babl one-way-profile NULL check | calendar time, ~1 day of work |
| Manual UI checklist on a real display (TESTING.md), C/M/Y/K channel icons in gimp-data | 1 day |

Total: roughly **2.5–3.5 weeks of focused work** plus upstream review latency; without M3/M4
(if upstream accepts them as documented limitations) about **1 week**.

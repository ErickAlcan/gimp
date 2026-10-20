# REVIEW-01 — independent review of the CMYK-mode plan

Status: DRAFT (in progress). Findings are appended as they are verified against code.

## Early notes (verified, to be folded into the final structure)

* babl CMYK->CMYK between two *different* Babl spaces re-separates (K100 -> 0.68/0.67/0.65/0.74),
  and babl keys spaces on the full ICC bytes including the header date. The lcms-generated builtin
  carries the creation time in the header -> new babl space every session.
* babl one-way profile segfault confirmed (CGATS001Compat-v2-micro.icc, RGB->CMYK fish).
* get_lcms_format: CMYKA missing, float -> TYPE_CMYK_FLT (0..100), u32 falls back to RGB float.
* SSE2/SSE4 normal + SSE2 clip-to-backdrop already fall back for n_components != 4.
* Blend functions take n_components from the op's *input* format -> break with a 5-comp input.

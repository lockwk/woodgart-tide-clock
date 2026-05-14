# Tide Clock E-Paper Dithering — Test Plan

**Display:** Waveshare 7.3" ACeP 6-color e-paper (`epd7in3e`)  
**Native resolution:** 800 × 480  
**Render pipeline:** HTML → Playwright/Chromium → PNG → PIL → `epd.getbuffer()` → display

---

## Root Cause Hypotheses

The dithering and illegible text most likely stem from one or more of these causes, listed in order of suspected impact:

**H1 — ACeP palette dithering (highest likelihood)**  
The `epd7in3e` display is a 7-color ACeP panel (black, white, red, green, blue, yellow, orange). When `epd.getbuffer(image)` converts the full-color PIL image to the display palette, the Waveshare driver applies Floyd-Steinberg dithering to approximate every color that isn't in those 7. The dark navy/black background and white text require color mixing across nearly the entire frame, resulting in the dense stipple pattern visible across all content areas.

**H2 — Downscale blurs text before dithering (high likelihood)**  
The HTML renders at 1280 × 720 and is LANCZOS-scaled to 800 × 480. LANCZOS produces soft anti-aliased edges on all text and chart lines. These sub-pixel gray values have no clean mapping to the 7-color palette, so the dithering algorithm spreads error across adjacent pixels — turning every glyph edge into a fringe of colored dots.

**H3 — Design not optimized for e-paper constraints (medium likelihood)**  
The current design uses: dark background (#000), thin weight fonts (Inter 300), a dense stipple/dot texture in the chart area, and smooth gradient curves. None of these translate well to a display with only 7 discrete colors and no true grayscale.

**H4 — Font rendering at small sizes (medium likelihood)**  
Chromium sub-pixel text rendering at the small sizes used in the header, labels, and tide time readouts produces anti-aliased glyph strokes ~1–2 px wide. At 800 × 480, these strokes may only be 1–2 px wide, making dithering artifacts consume the entire glyph.

---

## Test 1 — Capture the intermediate PNG

**Goal:** Isolate whether the problem is in the render stage or the display driver stage.

**Steps:**
1. Run the current `tide_display.py` but **comment out** the `push_to_display()` call so only `render_to_image()` executes.
2. Save the PIL image (after `resize()`) to `/tmp/tide_clock_800x480.png`.
3. View the PNG on a normal monitor.

**Expected outcomes:**
- If the PNG looks sharp and correct → the problem is entirely in `epd.getbuffer()` (H1)
- If the PNG already looks blurry/dithered → the problem is upstream in the render or scale step (H2, H3, H4)

**Code change to add to `render_to_image()`:**
```python
image = Image.open(SCREENSHOT_PATH).resize(
    (DISPLAY_WIDTH, DISPLAY_HEIGHT),
    Image.LANCZOS,
)
image.save('/tmp/tide_clock_800x480.png')   # ADD THIS
return image
```

---

## Test 2 — Eliminate the scaling step

**Goal:** Test whether rendering directly at 800 × 480 (native display resolution) sharpens text.

**Hypothesis tested:** H2

**Steps:**
1. Change `RENDER_WIDTH = 800` and `RENDER_HEIGHT = 480` in `tide_display.py`.
2. Remove (or make a no-op) the `.resize()` call.
3. Save the intermediate PNG and compare with Test 1's PNG.
4. If it looks better, push to the display.

**What to look for:** Are text edges sharper? Are the tide graph lines crisper?

**Note:** The HTML file references `width: 1280px` in the viewport meta tag and CSS. You'll need a separate layout (`tide_clock_800x480.html`) that is laid out natively at 800 × 480, **or** you can set the Chromium viewport to 1280 × 720 and use CSS `transform: scale(0.625)` at the root, then screenshot at 800 × 480. Either approach eliminates the PIL downscale step.

---

## Test 3 — Quantize to the ACeP palette without dithering

**Goal:** See if turning off dithering in the PIL → palette conversion produces more readable output at the cost of color accuracy.

**Hypothesis tested:** H1

**Steps:**  
Add explicit quantization before passing to `getbuffer()`:

```python
from PIL import Image

# Define the 7 ACeP colors
ACE_PALETTE = [
    0,   0,   0,    # Black
    255, 255, 255,  # White
    0,   255, 0,    # Green
    0,   0,   255,  # Blue
    255, 0,   0,    # Red
    255, 255, 0,    # Yellow
    255, 128, 0,    # Orange
]
# Pad to 256 colors
palette_colors = ACE_PALETTE + [0, 0, 0] * (256 - len(ACE_PALETTE) // 3)

palette_img = Image.new('P', (1, 1))
palette_img.putpalette(palette_colors)

# Quantize WITHOUT dithering (dither=0)
quantized = image.quantize(palette=palette_img, dither=0)
quantized_rgb = quantized.convert('RGB')

epd.display(epd.getbuffer(quantized_rgb))
```

**What to look for:** Does text become legible (solid white/black pixels) even if background color areas look posterized? This establishes whether the dithering is the primary legibility problem.

---

## Test 4 — Quantize with ordered (Bayer) dithering instead of Floyd-Steinberg

**Goal:** Floyd-Steinberg dithering propagates error in a way that clusters visually — it looks like noise. Ordered (Bayer matrix) dithering produces a regular halftone pattern that can look cleaner on e-paper.

**Hypothesis tested:** H1

**Steps:**  
PIL's `quantize()` with `dither=Image.Dither.FLOYDSTEINBERG` is the default. Unfortunately PIL doesn't natively support ordered dithering for palette conversion. Test with a simple nearest-neighbor threshold approach via numpy:

```python
import numpy as np

def nearest_acep(pixel):
    """Snap each pixel to closest ACeP color by Euclidean distance."""
    colors = np.array([
        [0,0,0], [255,255,255], [0,255,0],
        [0,0,255], [255,0,0], [255,255,0], [255,128,0]
    ])
    dists = np.sum((colors - np.array(pixel))**2, axis=1)
    return tuple(colors[np.argmin(dists)])

arr = np.array(image)
for y in range(arr.shape[0]):
    for x in range(arr.shape[1]):
        arr[y, x] = nearest_acep(arr[y, x])

snapped = Image.fromarray(arr.astype(np.uint8))
epd.display(epd.getbuffer(snapped))
```

Note: this loop is slow — use `cdist` from scipy or vectorized numpy for a production version.

---

## Test 5 — Invert the color scheme (light background)

**Goal:** Determine if the dark background is making the dithering dramatically worse than a light design would.

**Hypothesis tested:** H1, H3

**Background:** ACeP e-paper renders white areas as true white (no dithering needed). Dark areas must be approximated using the black + blue + green palette entries with dithering. A design with a white background and dark text uses the easiest color combination for this display.

**Steps:**
1. Create `tide_clock_inverted.html` — swap the background from `#000`/`#0a1628` to `#ffffff` and text/elements from white to `#000000` or `#001133`.
2. Run the full pipeline and compare the display output with the current dark version.

**What to look for:** Is text in the inverted version sharp and dithering-free? If yes, the dark background is a root cause and the design needs to be reworked for e-paper.

---

## Test 6 — Increase font weight and minimum size

**Goal:** Determine the minimum font size and weight that survives the full render-and-dither pipeline.

**Hypothesis tested:** H4

**Steps:**
1. On a copy of the HTML, temporarily set all fonts to `font-weight: 900` (black) and `font-size: max(16px, <current size>)`.
2. Run the full pipeline.
3. Compare legibility of labels, header, and tide readouts.

**What to look for:** If bold/large text is legible but the original thin/small text is not, then font sizing and weight need to be updated in the design.

---

## Test 7 — Black-and-white only mode

**Goal:** Confirm whether using the display in strict 1-bit (black/white) mode eliminates all dithering issues, as a baseline for what the "best possible" output looks like.

**Hypothesis tested:** H1 baseline

**Steps:**
```python
# Convert to pure 1-bit black/white before sending
bw_image = image.convert('L')             # grayscale
threshold_image = bw_image.point(
    lambda p: 255 if p > 128 else 0, '1' # hard threshold
)
bw_rgb = threshold_image.convert('RGB')
epd.display(epd.getbuffer(bw_rgb))
```

**What to look for:** Does text become perfectly sharp? Does the tide chart read cleanly? If yes, this becomes the target baseline and the color design should be reconsidered for this display type.

---

## Recommended Test Order

| Priority | Test | Time on Pi | Expected Signal |
|----------|------|-----------|-----------------|
| 1 | Test 1 — Capture intermediate PNG | 5 min | Isolates render vs. driver |
| 2 | Test 7 — B&W mode | 10 min | Baseline for "what's possible" |
| 3 | Test 3 — No dithering | 10 min | Is dithering the core issue? |
| 4 | Test 2 — Native 800×480 render | 20 min | Does scaling hurt text? |
| 5 | Test 5 — Light background | 20 min | Is dark bg the root cause? |
| 6 | Test 6 — Heavier fonts | 15 min | What sizes/weights survive? |
| 7 | Test 4 — Ordered dithering | 30 min | Alternative dither quality |

---

## Decision Tree After Testing

```
Is the intermediate PNG (Test 1) already blurry or dithered?
├── YES → Render pipeline problem. Run Test 2 (native resolution).
│         └── Still dithered? → H3/H4: design/font issue. Run Tests 5 & 6.
└── NO  → Driver conversion problem (H1). Run Test 3.
          ├── No-dither result legible? → Floyd-Steinberg is the problem. 
          │   Fix: snap to palette without dithering, accept posterization.
          └── Still poor? → Fundamental mismatch between design + ACeP palette.
              Fix: redesign for e-paper constraints (Test 5 + 6).
```

---

## Likely Fix (if all hypotheses confirmed)

Based on what's visible in the photo, the most likely remediation is a combination of:

1. **Render at 800 × 480 natively** — skip the LANCZOS downscale entirely.
2. **Redesign for light background** — white or very light background with dark elements. E-paper's native white requires zero dithering; the current dark navy requires the most.
3. **Increase font weight** — use Inter 600 or 700 minimum, never 300, at the small sizes used in this layout.
4. **Snap to palette without dithering** — or use a very low-intensity dithering so chart gradients soften gently rather than producing a stipple field.
5. **Simplify the dot/stipple chart texture** — replace the dotted background in the tide graph with a clean line, since dots + e-paper dithering compound each other.

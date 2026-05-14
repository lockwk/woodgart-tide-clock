#!/usr/bin/env python3
"""
tide_display.py — Tide Clock E-Paper Renderer

Renders tide_clock.html via headless Chromium (Playwright) at the
display's native 800×480 resolution and pushes it to the Waveshare
7.3" 6-color e-paper display (epd7in3e). Refreshes every hour.

Setup (run once on the Pi):
  pip3 install playwright --break-system-packages
  playwright install chromium
"""

import logging
import math
import time
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageEnhance, ImageFilter
from playwright.sync_api import sync_playwright
from waveshare_epd import epd7in3e

# ── Config ────────────────────────────────────────────────────────────────────

# The six colors the epd7in3e can physically display (R, G, B).
# Adjust these if your display's actual output looks off — ACeP panels
# vary slightly from unit to unit.
EPAPER_PALETTE = [
    (  0,   0,   0),  # Black
    (255, 255, 255),  # White
    (255,   0,   0),  # Red
    (  0, 255,   0),  # Green
    (  0,   0, 255),  # Blue
    (255, 255,   0),  # Yellow
]

HTML_PATH            = Path('/home/pi/tide_clock.html')
RAW_EXPORT_PATH      = Path('/home/pi/last-raw-image.png')
DITHERED_EXPORT_PATH = Path('/home/pi/last-dithered-image.png')
DITHERING_ENABLED    = False   # set to True to re-enable Floyd-Steinberg dithering

DISPLAY_WIDTH        = 800
DISPLAY_HEIGHT       = 480
DISPLAY_DIAGONAL_IN  = 7.3    # physical diagonal of the Waveshare panel (inches)

# Derive device scale factor from the panel's actual pixel density.
# Reference: 96 PPI is the standard CSS/web baseline (DPR = 1).
_display_ppi         = math.sqrt(DISPLAY_WIDTH**2 + DISPLAY_HEIGHT**2) / DISPLAY_DIAGONAL_IN
DEVICE_SCALE_FACTOR  = 1.0   # override: set to (_display_ppi / 96) to restore dynamic (~1.33)

REFRESH_INTERVAL = 3600   # seconds (1 hour)

# Post-processing — tune these to taste.
# Unsharp mask recovers edge sharpness lost during the supersampled downscale.
#   radius:    neighbourhood size in pixels (1–3 is typical)
#   percent:   sharpening strength (100 = subtle, 200 = aggressive)
#   threshold: minimum brightness difference to sharpen (avoids noise in flat areas)
USM_RADIUS    = 1
USM_PERCENT   = 150
USM_THRESHOLD = 3
# Contrast: 1.0 = unchanged, 1.2 = subtle lift, 1.5 = noticeable boost
CONTRAST      = 1.2

# How long to wait (ms) after network is idle before screenshotting.
# Gives JS/animations time to settle.
SETTLE_MS        = 3000

# ── Logging ───────────────────────────────────────────────────────────────────

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s  %(levelname)-8s  %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S',
)
log = logging.getLogger(__name__)

# ── Dithering ─────────────────────────────────────────────────────────────────

def dither_image(image: Image.Image) -> Image.Image:
    """
    Apply Floyd-Steinberg dithering to map a full-color image down to the
    six colors the e-paper display can physically render.
    """
    # Build a palette image PIL can use as a quantization target.
    palette_img = Image.new('P', (1, 1))
    flat = [channel for color in EPAPER_PALETTE for channel in color]
    flat += [0] * (768 - len(flat))   # PIL palette must be 256 × 3 bytes
    palette_img.putpalette(flat)

    dithered = image.convert('RGB').quantize(
        palette=palette_img,
        dither=Image.Dither.FLOYDSTEINBERG,
    )
    return dithered.convert('RGB')

# ── Rendering ─────────────────────────────────────────────────────────────────

def render_to_image() -> Image.Image:
    """
    Launch headless Chromium, load the tide clock page, wait for tide
    data to finish loading, then return a PIL Image at display resolution.
    Also saves a raw screenshot for comparison/debugging.
    """
    log.info('Rendering %s ...', HTML_PATH)
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(
            viewport={'width': DISPLAY_WIDTH, 'height': DISPLAY_HEIGHT},
            device_scale_factor=DEVICE_SCALE_FACTOR,
        )
        page.goto(
            f'file://{HTML_PATH.resolve()}',
            wait_until='networkidle',
            timeout=30_000,
        )
        page.wait_for_timeout(SETTLE_MS)
        screenshot_bytes = page.screenshot()   # captures at 1600×960
        browser.close()

    log.info('Render complete.')
    image = Image.open(BytesIO(screenshot_bytes))
    image.save(RAW_EXPORT_PATH)
    log.info('Raw screenshot saved → %s', RAW_EXPORT_PATH)

    # Scale back to display resolution using high-quality downsampling.
    image = image.resize((DISPLAY_WIDTH, DISPLAY_HEIGHT), Image.LANCZOS)

    # Sharpen edges lost during downscale.
    image = image.filter(ImageFilter.UnsharpMask(
        radius=USM_RADIUS, percent=USM_PERCENT, threshold=USM_THRESHOLD,
    ))

    # Slight contrast boost to compensate for e-paper's flatter appearance.
    image = ImageEnhance.Contrast(image).enhance(CONTRAST)

    return image

# ── Display ───────────────────────────────────────────────────────────────────

def push_to_display(image: Image.Image) -> None:
    """Send a PIL Image to the e-paper display, then put it to sleep."""
    log.info('Initializing display ...')
    epd = epd7in3e.EPD()
    epd.init()

    log.info('Pushing image to display (this takes ~35 s) ...')
    epd.display(epd.getbuffer(image))

    epd.sleep()
    log.info('Display updated and sleeping.')

# ── Main loop ─────────────────────────────────────────────────────────────────

def update() -> None:
    image = render_to_image()
    if DITHERING_ENABLED:
        image = dither_image(image)
        log.info('Dithering applied.')
    else:
        log.info('Dithering skipped (DITHERING_ENABLED = False).')
    image.save(DITHERED_EXPORT_PATH)
    log.info('Output image saved → %s', DITHERED_EXPORT_PATH)
    push_to_display(image)

if __name__ == '__main__':
    log.info('Tide clock display started — refreshing every %d s.', REFRESH_INTERVAL)
    while True:
        try:
            update()
        except Exception:
            log.exception('Update failed — will retry next cycle.')
        log.info('Sleeping for %d s ...', REFRESH_INTERVAL)
        time.sleep(REFRESH_INTERVAL)

# ── 13.3inch e-Paper HAT+ (E) — Display Configuration ───────────────────────
# Waveshare 13.3" 6-color ACeP display
# Product: https://www.waveshare.com/wiki/13.3inch_e-Paper_HAT%2B_(E)

WIDTH  = 1600
HEIGHT = 1200
DIAGONAL_IN = 13.3

DRIVER = "epd13in3e"  # waveshare_epd module name — confirm once installed

# The six colors this panel can physically display (R, G, B)
# Note: verify these against actual output — ACeP panels vary unit to unit
PALETTE = [
    (  0,   0,   0),  # Black
    (255, 255, 255),  # White
    (255,   0,   0),  # Red
    (  0, 255,   0),  # Green
    (  0,   0, 255),  # Blue
    (255, 255,   0),  # Yellow
]

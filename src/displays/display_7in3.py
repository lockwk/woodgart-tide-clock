# ── 7.3inch e-Paper HAT (E) — Display Configuration ─────────────────────────
# Waveshare 7.3" 6-color ACeP display
# Product: https://www.waveshare.com/wiki/7.3inch_e-Paper_HAT_(E)

WIDTH  = 800
HEIGHT = 480
DIAGONAL_IN = 7.3

DRIVER = "epd7in3e"  # waveshare_epd module name

# The six colors this panel can physically display (R, G, B)
PALETTE = [
    (  0,   0,   0),  # Black
    (255, 255, 255),  # White
    (255,   0,   0),  # Red
    (  0, 255,   0),  # Green
    (  0,   0, 255),  # Blue
    (255, 255,   0),  # Yellow
]

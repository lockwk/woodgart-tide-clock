# ── 13.3inch e-Paper HAT (K) — Display Configuration ────────────────────────
# Waveshare 13.3" black & white E-Ink display
# Product: https://www.waveshare.com/wiki/13.3inch_e-Paper_HAT_(K)_Manual
#
# Note: This is the HAT (K) model — 960×680, B&W only, SPI interface.
# NOT the HAT+ (E) model (which is 6-color ACeP).

WIDTH  = 960
HEIGHT = 680
DIAGONAL_IN = 13.3

DRIVER = "epd13in3k"  # waveshare_epd module name — confirm once installed

# Black & white only — 2-16 grey scales supported
PALETTE = [
    (  0,   0,   0),  # Black
    (255, 255, 255),  # White
]

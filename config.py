# ── Woodgart Tide Clock — Configuration ──────────────────────────────────────
#
# Edit this file to switch displays, themes, or locations.
# No other files need to change.

# Which display is physically connected?
# Options: "7in3" | "13in3"
DISPLAY = "7in3"

# Light or dark mode?
# Options: "light" | "dark"
THEME = "light"

# How often to refresh the display (seconds)
# 3600 = once per hour
REFRESH_INTERVAL = 3600

# ── Location ──────────────────────────────────────────────────────────────────
#
# Set your NOAA tide station and coordinates here.
# Station finder: https://tidesandcurrents.noaa.gov/stations.html

NOAA_STATION_ID = "9413745"          # Santa Cruz, CA
LATITUDE        = 36.9741
LONGITUDE       = -122.0308          # negative = West
TIMEZONE        = "America/Los_Angeles"

#!/usr/bin/env python3
"""
generate_bitmap.py — Pure-PIL Tide Clock Bitmap Generator

Fetches live data from all configured APIs (data_sources.md), then renders
an 800×480 RGB bitmap matching the Figma e-paper design (node 141-338).
No browser required — pure Python + PIL.

Usage:
    python3 generate_bitmap.py [output.png]          # live data
    python3 generate_bitmap.py --demo [output.png]   # sample data, no network needed
"""

import io
import math
import sys
import datetime
import urllib.request
from pathlib import Path

# cairosvg may be installed in the user site-packages (pip --user)
sys.path.insert(0, str(Path.home() / ".local/lib/python3.10/site-packages"))
import cairosvg

import requests
from PIL import Image, ImageDraw, ImageFont
import pytz

# ── Config ─────────────────────────────────────────────────────────────────────
WIDTH, HEIGHT  = 800, 480
EXPORTS_DIR    = Path(__file__).parent / "_figmaExports_800x480_bw"
NOAA_STATION   = "9413745"
LAT, LON       = 36.9741, -122.0308
TZ             = pytz.timezone("America/Los_Angeles")

BLACK  = (0,   0,   0)
WHITE  = (255, 255, 255)
ORANGE = (255, 128,  0)    # "now" bar — matches the Figma/HTML orange

# ── Layout constants (px) — mirrored from tide_clock.html / Figma 141:338 ────
HEADER_TOP    = 16
DIVIDER_1_Y   = 48
CURVE_TOP     = 118
CURVE_BOTTOM  = 221
HOUR_Y        = 254        # vertical centre of hour labels
DIVIDER_2_Y   = 276
DOT_RADIUS    = 5

PANEL_TOP     = 276        # bottom panels start here
PANEL_W       = 200
PANEL_H       = 204

# Absolute y-centres for bottom-panel elements (panel CSS positions + PANEL_TOP)
MOON_IMG_CY   = int(PANEL_TOP + 63.5)
MOON_AGE_CY   = PANEL_TOP + 137
MOON_LBL_CY   = PANEL_TOP + 167

TIDE_ICON_CY  = int(PANEL_TOP + 62.5)
TIDE_CODE_CY  = PANEL_TOP + 137
TIDE_LBL_CY   = PANEL_TOP + 167

NEXT_HT_TOP   = PANEL_TOP + 31
NEXT_ICON_CY_HI  = int(PANEL_TOP + 93.5)
NEXT_ICON_CY_LO  = int(PANEL_TOP + 87)
NEXT_TIME_CY  = int(PANEL_TOP + 136.5)
NEXT_LBL_CY   = int(PANEL_TOP + 165.5)

SUN_PAD_TOP   = PANEL_TOP + 34
SUN_GAP       = 40

# ── Font resolution ────────────────────────────────────────────────────────────
# Priority order for each weight:
#   1. _fonts/ subfolder next to this script  ← drop files here to override
#   2. macOS system / user font directories   ← picked up automatically if installed
#   3. Linux system font directories          ← Raspberry Pi / Debian
#   4. Auto-download from GitHub              ← first-run on the Pi
#   5. DejaVu Sans fallback                   ← last resort

_FONT_DIR = Path(__file__).parent / "_fonts"
_FONT_DIR.mkdir(exist_ok=True)

# Candidate paths searched in order for each weight.
# The script drops downloaded copies into _FONT_DIR so they survive reboots.
_HOME = Path.home()
_INTER_LIGHT_CANDIDATES = [
    _FONT_DIR / "Inter-Light.ttf",
    # macOS — user fonts (Figma / manual install)
    _HOME / "Library/Fonts/Inter-Light.ttf",
    _HOME / "Library/Fonts/Inter Light.ttf",
    # macOS — system-wide
    Path("/Library/Fonts/Inter-Light.ttf"),
    Path("/Library/Fonts/Inter Light.ttf"),
    # Linux (apt install fonts-inter or similar)
    Path("/usr/share/fonts/truetype/inter/Inter-Light.ttf"),
    Path("/usr/local/share/fonts/Inter-Light.ttf"),
]
_INTER_SEMI_CANDIDATES = [
    _FONT_DIR / "Inter-SemiBold.ttf",
    _HOME / "Library/Fonts/Inter-SemiBold.ttf",
    _HOME / "Library/Fonts/Inter SemiBold.ttf",
    Path("/Library/Fonts/Inter-SemiBold.ttf"),
    Path("/Library/Fonts/Inter SemiBold.ttf"),
    Path("/usr/share/fonts/truetype/inter/Inter-SemiBold.ttf"),
    Path("/usr/local/share/fonts/Inter-SemiBold.ttf"),
]

def _find_font(candidates: list[Path]) -> Path | None:
    for p in candidates:
        if p.exists():
            return p
    return None

def _dl_font(name: str, url: str, dest: Path) -> bool:
    """Download a font to dest. Returns True on success."""
    if dest.exists():
        return True
    print(f"  Downloading {name} from GitHub …")
    try:
        urllib.request.urlretrieve(url, dest)
        print(f"  ✓ Saved to {dest}")
        return True
    except Exception as exc:
        print(f"  WARNING: could not download {name}: {exc}")
        return False

_INTER_BASE = "https://raw.githubusercontent.com/rsms/inter/master/docs/font-files/"

def _resolve_inter(candidates: list[Path], filename: str, label: str) -> Path | None:
    """Return an existing Inter TTF path, downloading to _fonts/ if needed."""
    found = _find_font(candidates)
    if found:
        return found
    dest = _FONT_DIR / filename
    if _dl_font(label, _INTER_BASE + filename, dest):
        return dest
    return None

_inter_light_path = _resolve_inter(_INTER_LIGHT_CANDIDATES, "Inter-Light.ttf",    "Inter Light")
_inter_semi_path  = _resolve_inter(_INTER_SEMI_CANDIDATES,  "Inter-SemiBold.ttf", "Inter SemiBold")

# Fallback system fonts (Raspberry Pi / Debian)
_DEJAVU           = Path("/usr/share/fonts/truetype/dejavu/")
_FALLBACK_REGULAR = str(_DEJAVU / "DejaVuSans.ttf")
_FALLBACK_BOLD    = str(_DEJAVU / "DejaVuSans-Bold.ttf")

def _load_font(preferred: Path | None, fallback: str, size: int) -> ImageFont.FreeTypeFont:
    if preferred and preferred.exists():
        print(f"  Font: {preferred.name} @ {size}px")
        return ImageFont.truetype(str(preferred), size)
    if Path(fallback).exists():
        print(f"  Font: {Path(fallback).name} @ {size}px  (Inter not found — see _fonts/ README)")
        return ImageFont.truetype(fallback, size)
    return ImageFont.load_default()

FONT_LABEL = _load_font(_inter_semi_path,  _FALLBACK_BOLD,    12)
FONT_DATA  = _load_font(_inter_light_path, _FALLBACK_REGULAR, 32)
FONT_SMALL = _load_font(_inter_semi_path,  _FALLBACK_BOLD,    10)


# ── Text utilities ─────────────────────────────────────────────────────────────

def _text_size(draw: ImageDraw.ImageDraw, text: str, font) -> tuple[int, int]:
    bb = draw.textbbox((0, 0), text, font=font)
    return bb[2] - bb[0], bb[3] - bb[1]

def draw_text(draw, text, xy, font, fill=BLACK, anchor="lt"):
    """
    Draw text with anchor:
      'lt'  = left / top
      'mm'  = centre / middle
      'mt'  = centre / top
      'rm'  = right  / middle
    """
    x, y = xy
    w, h = _text_size(draw, text, font)
    if anchor == "mm":
        x -= w // 2;  y -= h // 2
    elif anchor == "mt":
        x -= w // 2
    elif anchor == "rm":
        x -= w;        y -= h // 2
    elif anchor == "rt":
        x -= w
    draw.text((x, y), text, font=font, fill=fill)
    return w  # useful for chaining

def draw_spaced(draw, text, xy, font, spacing=5, fill=BLACK, anchor="lt"):
    """
    Draw text with manual inter-character spacing (approximates CSS letter-spacing).
    Returns total rendered width.
    """
    x0, y = xy
    total_w = 0
    widths = []
    for ch in text:
        w, _ = _text_size(draw, ch, font)
        widths.append(w)
        total_w += w + spacing
    total_w -= spacing  # no trailing gap

    if anchor in ("mm", "mt"):
        x0 -= total_w // 2
    elif anchor in ("rm", "rt"):
        x0 -= total_w
    if anchor in ("mm", "rm"):
        _, h = _text_size(draw, text[0], font)
        y -= h // 2

    x = x0
    for ch, w in zip(text, widths):
        draw.text((x, y), ch, font=font, fill=fill)
        x += w + spacing
    return total_w


# ── SVG icon loading ───────────────────────────────────────────────────────────
# All icons come from _figmaExports_800x480_bw/ as exact Figma SVG exports.

_svg_cache: dict[str, Image.Image] = {}

def _load_svg(filename: str, scale: float = 1.0) -> Image.Image:
    """
    Render a Figma-exported SVG from EXPORTS_DIR into a PIL RGBA image.
    Results are cached so each file is only decoded once.
    """
    key = f"{filename}@{scale}"
    if key not in _svg_cache:
        path = EXPORTS_DIR / filename
        png  = cairosvg.svg2png(url=str(path), scale=scale)
        _svg_cache[key] = Image.open(io.BytesIO(png)).convert("RGBA")
    return _svg_cache[key]

def paste_svg(canvas: Image.Image, filename: str, cx: int, cy: int,
              scale: float = 1.0) -> None:
    """
    Paste a Figma SVG export centred at pixel (cx, cy) on canvas,
    using the icon's alpha channel as a compositing mask.
    """
    icon = _load_svg(filename, scale)
    w, h = icon.size
    x = cx - w // 2
    y = cy - h // 2
    canvas.paste(icon, (x, y), mask=icon.split()[3])

# Convenience wrappers (keep the same call signature as the old hand-drawn fns)

def draw_rain_drop(canvas, cx, cy):
    paste_svg(canvas, "rain.svg", cx, cy)

def draw_wind_lines(canvas, cx, cy):
    paste_svg(canvas, "wind lines.svg", cx, cy)

def draw_waves(canvas, cx, cy):
    paste_svg(canvas, "waves.svg", cx, cy)

def draw_sunrise_icon(canvas, cx, cy):
    paste_svg(canvas, "sunrise.svg", cx, cy)

def draw_sunset_icon(canvas, cx, cy):
    paste_svg(canvas, "sunset.svg", cx, cy)

def draw_high_tide_arrows(canvas, cx, cy):
    paste_svg(canvas, "high tide.svg", cx, cy)

def draw_low_tide_arrows(canvas, cx, cy):
    paste_svg(canvas, "low tide.svg", cx, cy)

# Moon phase name → SVG filename
_MOON_SVG = {
    "New Moon":        "Moon Phase New.svg",
    "Waxing Crescent": "Moon Phase Waxing Crescent.svg",
    "First Quarter":   "Moon Phase First Quarter.svg",
    "Waxing Gibbous":  "Moon Phase Waxing Gibbous.svg",
    "Full Moon":       "Moon Phase Full Moon.svg",
    "Waning Gibbous":  "Moon Phase Waning Gibbous.svg",
    "Last Quarter":    "Moon Phase Last Quarter.svg",
    "Waning Crescent": "Moon Phase Waning Crescent.svg",
}

def draw_moon_phase(canvas, cx, cy, phase_name: str):
    """Paste the correct Figma moon-phase SVG centred at (cx, cy)."""
    fname = _MOON_SVG.get(phase_name, "Moon Phase New.svg")
    paste_svg(canvas, fname, cx, cy)

# Tide cycle type → SVG filename
_TIDE_CYCLE_SVG = {
    "Spring Tide": "spring tide curve.svg",
    "Half Tide":   "half tide curve.svg",
    "Neap Tide":   "neap tide curve.svg",
}

def draw_tide_cycle_icon(canvas, cx, cy, tide_type: str):
    """Paste the correct tide-cycle curve SVG centred at (cx, cy)."""
    fname = _TIDE_CYCLE_SVG.get(tide_type, "half tide curve.svg")
    paste_svg(canvas, fname, cx, cy)


# ── Data helpers ───────────────────────────────────────────────────────────────

def _wind_cardinal(deg: float) -> str:
    dirs = ["N","NNE","NE","ENE","E","ESE","SE","SSE",
            "S","SSW","SW","WSW","W","WNW","NW","NNW"]
    return dirs[round(deg / 22.5) % 16]

def _hours_since_rain(hourly: dict, now: datetime.datetime) -> int | None:
    times  = hourly.get("time", [])
    precip = hourly.get("precipitation", [])
    for t_str, p in zip(reversed(times), reversed(precip)):
        if p is None:
            continue
        t = datetime.datetime.fromisoformat(t_str).replace(tzinfo=TZ)
        if p > 0.0:
            hrs = int((now - t).total_seconds() / 3600)
            return hrs
    return None

def _moon_phase(now: datetime.datetime) -> tuple[float, str]:
    """Return (age_days, phase_name). Known new moon: 2000-01-06."""
    known_new = datetime.datetime(2000, 1, 6, 18, 14, 0,
                                  tzinfo=datetime.timezone.utc)
    delta = now.astimezone(datetime.timezone.utc) - known_new
    age = delta.total_seconds() / 86400 % 29.53058770576
    if   age <  1.85 or age >= 27.68: phase = "New Moon"
    elif age <  7.38:                  phase = "Waxing Crescent"
    elif age <  9.22:                  phase = "First Quarter"
    elif age < 14.77:                  phase = "Waxing Gibbous"
    elif age < 16.61:                  phase = "Full Moon"
    elif age < 22.15:                  phase = "Waning Gibbous"
    elif age < 23.99:                  phase = "Last Quarter"
    else:                              phase = "Waning Crescent"
    return age, phase

def _tide_cycle(moon_age: float) -> str:
    a = moon_age % 29.53
    if a < 3 or a > 26.5 or 12 < a < 17:
        return "Spring Tide"
    if 6 < a < 10 or 21 < a < 25:
        return "Neap Tide"
    return "Half Tide"

def _fmt_height(h: float) -> str:
    neg = h < 0
    a   = abs(h)
    ft  = int(a)
    inch = round((a - ft) * 12)
    if inch == 12:
        ft += 1; inch = 0
    return f"{'-' if neg else ''}{ft}'{inch}\""

def _fmt_time_short(dt: datetime.datetime) -> str:
    h = dt.hour % 12 or 12
    return f"{h}:{dt.minute:02d}"

def _fmt_time_full(dt: datetime.datetime) -> str:
    h    = dt.hour % 12 or 12
    ampm = "PM" if dt.hour >= 12 else "AM"
    return f"{h}:{dt.minute:02d} {ampm}"

def _parse_noaa_time(t_str: str) -> datetime.datetime:
    """'2026-05-08 06:23' → aware datetime in local TZ."""
    dt = datetime.datetime.strptime(t_str, "%Y-%m-%d %H:%M")
    return TZ.localize(dt)

def _cosine_interp(h1, h2, t):
    return h1 + (h2 - h1) * (1 - math.cos(t * math.pi)) / 2

def _height_at(ms: float, pts: list) -> float:
    """Cosine interpolation between hi/lo tide points (same as JS version)."""
    for k in range(len(pts) - 1):
        if pts[k]["ms"] <= ms <= pts[k+1]["ms"]:
            frac = (ms - pts[k]["ms"]) / (pts[k+1]["ms"] - pts[k]["ms"])
            return _cosine_interp(pts[k]["h"], pts[k+1]["h"], frac)
    if ms < pts[0]["ms"]:
        frac = (ms - pts[0]["ms"]) / (pts[1]["ms"] - pts[0]["ms"])
        return _cosine_interp(pts[0]["h"], pts[1]["h"], max(0.0, frac))
    n = len(pts)
    frac = (ms - pts[n-2]["ms"]) / (pts[n-1]["ms"] - pts[n-2]["ms"])
    return _cosine_interp(pts[n-2]["h"], pts[n-1]["h"], min(1.0, frac))


# ── API fetches ────────────────────────────────────────────────────────────────

def _get(url, timeout=10):
    return requests.get(url, timeout=timeout).json()

def fetch_tides():
    now  = datetime.datetime.now(TZ)
    fmt  = lambda d: d.strftime("%Y%m%d")
    yest = now - datetime.timedelta(days=1)
    tom  = now + datetime.timedelta(days=1)
    url  = (
        "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter"
        f"?begin_date={fmt(yest)}&end_date={fmt(tom)}"
        f"&station={NOAA_STATION}&product=predictions&datum=MLLW"
        "&time_zone=lst_ldt&interval=hilo&units=english"
        "&application=tide_clock&format=json"
    )
    data = _get(url)
    raw  = data.get("predictions", [])
    return [{"iso": p["t"].replace(" ", "T"),
             "ms":  _parse_noaa_time(p["t"]).timestamp() * 1000,
             "h":   float(p["v"]),
             "type": p["type"]} for p in raw]

def fetch_weather():
    url = (
        f"https://api.open-meteo.com/v1/forecast"
        f"?latitude={LAT}&longitude={LON}"
        "&current=wind_speed_10m,wind_direction_10m"
        "&wind_speed_unit=mph"
    )
    return _get(url).get("current", {})

def fetch_rain_hourly():
    url = (
        f"https://api.open-meteo.com/v1/forecast"
        f"?latitude={LAT}&longitude={LON}"
        "&hourly=precipitation"
        "&precipitation_unit=inch"
        "&timezone=America%2FLos_Angeles"
        "&past_days=5"
    )
    return _get(url).get("hourly", {})

def fetch_sea_temp_f():
    url = (
        f"https://marine-api.open-meteo.com/v1/marine"
        f"?latitude={LAT}&longitude={LON}"
        "&current=sea_surface_temperature"
    )
    c = _get(url).get("current", {})
    t = c.get("sea_surface_temperature")
    return round(t * 9 / 5 + 32, 1) if t is not None else None

def fetch_sun_times(now: datetime.datetime) -> tuple[datetime.datetime, datetime.datetime]:
    date_str = now.strftime("%Y-%m-%d")
    url = f"https://api.sunrise-sunset.org/json?lat={LAT}&lng={LON}&formatted=0&date={date_str}"
    res = _get(url).get("results", {})

    def parse_utc(s):
        dt = datetime.datetime.fromisoformat(s.replace("Z", "+00:00"))
        return dt.astimezone(TZ)

    return parse_utc(res["sunrise"]), parse_utc(res["sunset"])


# ── Main render ────────────────────────────────────────────────────────────────

def render(output_path: Path, _now_override: datetime.datetime | None = None):
    now = _now_override if _now_override is not None else datetime.datetime.now(TZ)
    print("Fetching tides …")
    tides = fetch_tides()
    print("Fetching weather …")
    weather = fetch_weather()
    print("Fetching rain …")
    rain_hourly = fetch_rain_hourly()
    print("Fetching sea temp …")
    sea_temp_f = fetch_sea_temp_f()
    print("Fetching sun times …")
    sunrise, sunset = fetch_sun_times(now)

    # ── Derived values ──────────────────────────────────────────────────────────
    wind_spd  = weather.get("wind_speed_10m", 0)
    wind_dir  = weather.get("wind_direction_10m", 0)
    wind_str  = f"{_wind_cardinal(wind_dir)} {round(wind_spd)}MPH"

    rain_hrs  = _hours_since_rain(rain_hourly, now)
    rain_str  = f"{rain_hrs} HRS" if rain_hrs is not None else None

    moon_age, moon_phase = _moon_phase(now)
    tide_cycle = _tide_cycle(moon_age)

    # Next upcoming tide
    now_ms = now.timestamp() * 1000
    upcoming = [t for t in tides if t["ms"] > now_ms]
    next_tide = min(upcoming, key=lambda t: t["ms"]) if upcoming else None

    # Tide graph helpers
    t_start = sunrise.timestamp() * 1000
    t_end   = sunset.timestamp()  * 1000

    all_h = [t["h"] for t in tides]
    min_h = min(all_h) - 0.5
    max_h = max(all_h) + 0.5

    def to_x(ms):
        return ((ms - t_start) / (t_end - t_start)) * WIDTH

    def to_y(h):
        norm = (h - min_h) / (max_h - min_h)
        return CURVE_TOP + (CURVE_BOTTOM - CURVE_TOP) * (1 - norm)

    # Tide hour labels — first odd hour after sunrise, every 2 hours
    sr_hour = sunrise.hour + sunrise.minute / 60
    ss_hour = sunset.hour  + sunset.minute  / 60
    h_cursor = math.ceil(sr_hour)
    if h_cursor % 2 == 0:
        h_cursor += 1
    hour_labels = []
    while h_cursor < ss_hour:
        d = now.replace(hour=h_cursor, minute=0, second=0, microsecond=0)
        label = f"{h_cursor}A" if h_cursor < 12 else ("12P" if h_cursor == 12 else f"{h_cursor-12}P")
        hour_labels.append((d.timestamp() * 1000, label))
        h_cursor += 2

    # Peaks inside graph window
    peaks = [t for t in tides
             if t_start < t["ms"] < t_end]

    # ── Create canvas ───────────────────────────────────────────────────────────
    img  = Image.new("RGB", (WIDTH, HEIGHT), WHITE)
    draw = ImageDraw.Draw(img)

    # ── HEADER ──────────────────────────────────────────────────────────────────
    # Left: time + date
    time_str = _fmt_time_full(now)
    date_str = now.strftime("%a %b %-d %Y").upper()

    x = 20
    w = draw_spaced(draw, time_str, (x, HEADER_TOP), FONT_LABEL, spacing=5)
    x += w + 24
    draw_spaced(draw, date_str, (x, HEADER_TOP), FONT_LABEL, spacing=5)

    # Right: rain / wind / water temp (right-aligned, rightmost first)
    x = WIDTH - 20

    # Water temp
    if sea_temp_f is not None:
        temp_str = f"{sea_temp_f}°F"
        tw = draw_spaced(draw, temp_str, (x, HEADER_TOP), FONT_LABEL,
                         spacing=5, anchor="rt")
        x -= tw + 12          # 12px gap between text left and icon centre search point
        draw_waves(img, x - 15, HEADER_TOP + 7)   # icon right edge → 12px gap to text
        x -= 30 + 24          # icon full width (30) + 24px inter-group gap

    # Wind
    ww = draw_spaced(draw, wind_str, (x, HEADER_TOP), FONT_LABEL,
                     spacing=5, anchor="rt")
    x -= ww + 12          # 12px gap between text left and icon centre search point
    draw_wind_lines(img, x - 15, HEADER_TOP + 8)   # icon right edge → 12px gap to text
    x -= 30 + 24          # icon full width (30) + 24px inter-group gap

    # Rain (only show if we have data)
    if rain_str:
        rw = draw_spaced(draw, rain_str, (x, HEADER_TOP), FONT_LABEL,
                         spacing=5, anchor="rt")
        x -= rw + 10
        draw_rain_drop(img, x - 2, HEADER_TOP + 8)

    # ── DIVIDER 1 ───────────────────────────────────────────────────────────────
    draw.line([0, DIVIDER_1_Y, WIDTH, DIVIDER_1_Y], fill=BLACK, width=1)

    # ── NOW BAR ─────────────────────────────────────────────────────────────────
    now_x = to_x(now_ms)
    if 0 <= now_x <= WIDTH:
        nx = int(now_x)
        draw.rectangle([nx - 2, DIVIDER_1_Y, nx + 2, DIVIDER_2_Y], fill=ORANGE)

    # ── TIDE CURVE ──────────────────────────────────────────────────────────────
    SAMPLES = 900
    curve = []
    for i in range(SAMPLES + 1):
        ms = t_start + (i / SAMPLES) * (t_end - t_start)
        x  = int(to_x(ms))
        y  = int(to_y(_height_at(ms, tides)))
        curve.append((x, y))
    draw.line(curve, fill=BLACK, width=3)

    # ── PEAK DOTS ───────────────────────────────────────────────────────────────
    for tide in peaks:
        px = int(to_x(tide["ms"]))
        py = int(to_y(tide["h"]))
        draw.ellipse([px - DOT_RADIUS, py - DOT_RADIUS,
                      px + DOT_RADIUS, py + DOT_RADIUS], fill=BLACK)

    # ── PEAK LABELS ─────────────────────────────────────────────────────────────
    # HTML uses `600 12px Inter` for peak labels — use FONT_LABEL (12px SemiBold)
    for tide in peaks:
        px = int(to_x(tide["ms"]))
        py = int(to_y(tide["h"]))
        dt = datetime.datetime.fromtimestamp(tide["ms"] / 1000, TZ)
        lx = px + 6
        draw_spaced(draw, tide["type"],           (lx, py - 43), FONT_LABEL, spacing=5)
        draw_spaced(draw, _fmt_height(tide["h"]), (lx, py - 29), FONT_LABEL, spacing=5)
        draw_spaced(draw, _fmt_time_short(dt),    (lx, py - 15), FONT_LABEL, spacing=5)

    # ── HOUR LABELS ─────────────────────────────────────────────────────────────
    for h_ms, label in hour_labels:
        lx = int(to_x(h_ms))
        draw_spaced(draw, label, (lx, HOUR_Y), FONT_LABEL, spacing=5, anchor="mt")

    # ── DIVIDER 2 ───────────────────────────────────────────────────────────────
    draw.line([0, DIVIDER_2_Y, WIDTH, DIVIDER_2_Y], fill=BLACK, width=1)

    # Panel vertical dividers
    for i in range(1, 4):
        draw.line([i * PANEL_W, PANEL_TOP, i * PANEL_W, HEIGHT], fill=BLACK, width=1)

    # ── PANEL 0 — NEXT TIDE ─────────────────────────────────────────────────────
    cx0 = PANEL_W // 2  # 100

    if next_tide:
        is_high = next_tide["type"] == "H"
        is_neg  = next_tide["h"] < 0
        abs_h   = abs(next_tide["h"])
        ft      = int(abs_h)
        inch    = round((abs_h - ft) * 12)
        if inch == 12:
            ft += 1; inch = 0

        dt_next = datetime.datetime.fromtimestamp(next_tide["ms"] / 1000, TZ)

        # Height row: "N FT  M IN"
        ht_y = NEXT_HT_TOP
        # Draw ft number + "FT" label, then in number + "IN" label
        # Measure widths to centre the group
        ft_w,   data_h = _text_size(draw, str(ft),   FONT_DATA)
        in_w,        _ = _text_size(draw, str(inch),  FONT_DATA)
        lbl_w,  lbl_h  = _text_size(draw, "FT",       FONT_LABEL)
        lbl2_w,      _ = _text_size(draw, "IN",        FONT_LABEL)
        GAP    = 11      # Figma CSS: gap: 11px between number and unit label
        SPACER = 16      # horizontal gap between ft group and in group
        total = ft_w + GAP + lbl_w + SPACER + in_w + GAP + lbl2_w
        rx = cx0 - total // 2

        # Vertically centre unit labels alongside the 32px numbers:
        # data center = ht_y + data_h/2 → label top = data_center - lbl_h/2
        lbl_y = ht_y + data_h // 2 - lbl_h // 2

        if is_neg:
            neg_w, _ = _text_size(draw, "-", FONT_DATA)
            draw.text((rx, ht_y), "-", font=FONT_DATA, fill=BLACK)
            rx += neg_w + 4

        draw.text((rx, ht_y), str(ft), font=FONT_DATA, fill=BLACK)
        rx += ft_w + GAP
        draw_spaced(draw, "FT", (rx, lbl_y), FONT_LABEL, spacing=4)
        rx += lbl_w + SPACER
        draw.text((rx, ht_y), str(inch), font=FONT_DATA, fill=BLACK)
        rx += in_w + GAP
        draw_spaced(draw, "IN", (rx, lbl_y), FONT_LABEL, spacing=4)

        # Direction icon
        icon_cy = NEXT_ICON_CY_HI if is_high else NEXT_ICON_CY_LO
        if is_high or is_neg:
            draw_high_tide_arrows(img, cx0, icon_cy)
        else:
            draw_low_tide_arrows(img, cx0, icon_cy)

        # Time
        time_next = _fmt_time_full(dt_next)
        draw_text(draw, time_next, (cx0, NEXT_TIME_CY), FONT_DATA, anchor="mm")

        # Label
        lbl = "HIGH TIDE" if is_high else "LOW TIDE"
        draw_spaced(draw, lbl, (cx0, NEXT_LBL_CY), FONT_LABEL, spacing=5, anchor="mm")

    # ── PANEL 1 — TIDE CYCLE ────────────────────────────────────────────────────
    cx1 = PANEL_W + PANEL_W // 2  # 300

    draw_tide_cycle_icon(img, cx1, TIDE_ICON_CY, tide_cycle)

    # Code letter: Spring→L, Half→M, Neap→S  (from HTML source)
    code = {"Spring Tide": "L", "Half Tide": "M", "Neap Tide": "S"}.get(tide_cycle, "?")
    draw_text(draw, code, (cx1, TIDE_CODE_CY), FONT_DATA, anchor="mm")
    draw_spaced(draw, tide_cycle.upper(), (cx1, TIDE_LBL_CY), FONT_LABEL,
                spacing=5, anchor="mm")

    # ── PANEL 2 — SUNRISE / SUNSET ──────────────────────────────────────────────
    cx2 = 2 * PANEL_W + PANEL_W // 2  # 500

    # Sunrise row — vertically centred around SUN_PAD_TOP
    sr_str = _fmt_time_full(sunrise)
    ss_str = _fmt_time_full(sunset)

    # Row 1: icon + time, then label below (gap=6)
    sr_y = SUN_PAD_TOP  # top of first row

    # Measure the row elements using actual font metrics
    sr_time_w, data_h = _text_size(draw, sr_str, FONT_DATA)
    _, lbl_h = _text_size(draw, "SUNRISE", FONT_LABEL)
    icon_w  = 16
    row_gap = 8
    row_total = icon_w + row_gap + sr_time_w
    icon_x1 = cx2 - row_total // 2
    time_x1 = icon_x1 + icon_w + row_gap

    draw_sunrise_icon(img, icon_x1 + 8, sr_y + data_h // 2)
    draw.text((time_x1, sr_y), sr_str, font=FONT_DATA, fill=BLACK)
    draw_spaced(draw, "SUNRISE", (cx2, sr_y + data_h + 6), FONT_LABEL,
                spacing=5, anchor="mt")

    # Row 2 (sunset): shifted down by data_h + label gap(6) + label_h + SUN_GAP
    # Using measured heights (data_h=24, lbl_h=9) for pixel-exact spacing
    ss_y = sr_y + data_h + 6 + lbl_h + SUN_GAP
    ss_time_w, _ = _text_size(draw, ss_str, FONT_DATA)
    row_total2 = icon_w + row_gap + ss_time_w
    icon_x2 = cx2 - row_total2 // 2
    time_x2 = icon_x2 + icon_w + row_gap

    draw_sunset_icon(img, icon_x2 + 8, ss_y + data_h // 2)
    draw.text((time_x2, ss_y), ss_str, font=FONT_DATA, fill=BLACK)
    draw_spaced(draw, "SUNSET", (cx2, ss_y + data_h + 6), FONT_LABEL,
                spacing=5, anchor="mt")

    # ── PANEL 3 — MOON PHASE ────────────────────────────────────────────────────
    cx3 = 3 * PANEL_W + PANEL_W // 2  # 700

    draw_moon_phase(img, cx3, MOON_IMG_CY, moon_phase)

    age_str = f"{moon_age:.1f}"
    draw_text(draw, age_str, (cx3, MOON_AGE_CY), FONT_DATA, anchor="mm")
    draw_spaced(draw, moon_phase.upper(), (cx3, MOON_LBL_CY), FONT_LABEL,
                spacing=5, anchor="mm")

    # ── Save ────────────────────────────────────────────────────────────────────
    img.save(output_path)
    print(f"✓ Bitmap saved → {output_path}")


def render_demo(output_path: Path):
    """
    Render with the same sample data used in tide_clock.html DEMO_MODE,
    so the output can be verified without any network access.
    """
    # IMPORTANT: use TZ.localize(), NOT datetime(..., tzinfo=TZ) — pytz requires
    # localize() to pick the correct UTC offset (PDT vs LMT).
    now_demo = TZ.localize(datetime.datetime(2026, 4, 4, 21, 35, 0))

    # Tide predictions spanning yesterday → tomorrow (same pattern as DEMO_MODE)
    def _demo_tide(day_offset, hour, minute, height, ttype):
        naive = datetime.datetime(2026, 4, 4, hour, minute, 0)
        naive += datetime.timedelta(days=day_offset)
        dt = TZ.localize(naive)
        return {"iso": dt.isoformat(), "ms": dt.timestamp() * 1000,
                "h": float(height), "type": ttype}

    demo_tides = [
        _demo_tide(-1,  1, 47, 1.0, "L"),
        _demo_tide(-1,  8, 12, 4.0, "H"),
        _demo_tide(-1, 14, 38, 1.1, "L"),
        _demo_tide(-1, 21,  3, 4.0, "H"),
        _demo_tide( 0,  7, 53, -0.4, "L"),   # low at 7:53 AM → left dot
        _demo_tide( 0, 13, 17,  3.4, "H"),   # high at 1:17 PM → center dot
        _demo_tide( 0, 18,  0,  2.1, "L"),   # low at 6:00 PM → right dot
        _demo_tide( 0, 22, 14,  4.0, "H"),
        _demo_tide( 1,  3, 18,  1.0, "L"),
        _demo_tide( 1, 10, 25,  4.0, "H"),
    ]

    sunrise_demo = TZ.localize(datetime.datetime(2026, 4, 4,  6,  8, 0))
    sunset_demo  = TZ.localize(datetime.datetime(2026, 4, 4, 20,  4, 0))

    # Monkey-patch: override fetch functions for this call
    import unittest.mock as mock
    with (
        mock.patch("__main__.fetch_tides",      return_value=demo_tides),
        mock.patch("__main__.fetch_weather",    return_value={
            "wind_speed_10m": 3.0, "wind_direction_10m": 180.0}),
        # Rain 56 hours ago: index 200-56=144 in ascending list
        mock.patch("__main__.fetch_rain_hourly", return_value={
            "time": [TZ.localize(datetime.datetime(2026, 4, 4, 21, 35, 0) -
                                 datetime.timedelta(hours=(200 - h))).isoformat()
                     for h in range(201)],
            "precipitation": [0.1 if h == 144 else 0.0 for h in range(201)]}),
        mock.patch("__main__.fetch_sea_temp_f", return_value=58.8),
        mock.patch("__main__.fetch_sun_times",  return_value=(sunrise_demo, sunset_demo)),
    ):
        render(output_path, _now_override=now_demo)


if __name__ == "__main__":
    args = sys.argv[1:]
    demo_mode = "--demo" in args
    args = [a for a in args if a != "--demo"]
    out = Path(args[0]) if args else Path(__file__).parent / "tide_clock_bitmap.png"

    if demo_mode:
        print("Running in DEMO MODE (no network requests)")
        render_demo(out)
    else:
        render(out)

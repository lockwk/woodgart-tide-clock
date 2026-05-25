#!/usr/bin/env python3
"""
fetcher_13in3.py — Data Fetcher for 13.3" Tide Clock (Woodgart)

Fetches all data from external APIs and writes /tmp/tide_data.json
for the C renderer to consume.

Dependencies (install once on the Pi):
    pip3 install requests --break-system-packages

Run standalone:
    python3 src/fetch/fetcher_13in3.py
    cat /tmp/tide_data.json
"""

import json
import logging
import sys
from datetime import datetime, timezone, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

import requests

# ── Config import ─────────────────────────────────────────────────────────────
# Walk up from src/fetch/ → src/ → repo root to find config.py
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from config import NOAA_STATION_ID, LATITUDE, LONGITUDE, TIMEZONE

# ── Output path ───────────────────────────────────────────────────────────────
OUTPUT_PATH = Path('/tmp/tide_data.json')

# ── Rain settings ─────────────────────────────────────────────────────────────
# Minimum hourly precipitation (inches) that counts as "rain"
RAIN_THRESHOLD = 0.1
# If the last qualifying rain was more than this many hours ago, treat as no rain
RAIN_MAX_HOURS = 120

# ── Lunar reference ───────────────────────────────────────────────────────────
LUNAR_CYCLE_DAYS = 29.53058867
LUNAR_REF_UTC    = datetime(2000, 1, 6, 6, 44, 0, tzinfo=timezone.utc)

# ── Logging ───────────────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s  %(levelname)-8s  %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S',
)
log = logging.getLogger(__name__)


# ─────────────────────────────────────────────────────────────────────────────
#  Helpers
# ─────────────────────────────────────────────────────────────────────────────

def fmt_time_12h(dt):
    """Format a datetime as '9:35 AM'."""
    h, m = dt.hour, dt.minute
    ampm = 'PM' if h >= 12 else 'AM'
    h12  = h % 12 or 12
    return f'{h12}:{m:02d} {ampm}'


def fmt_date_upper(dt):
    """Format a datetime as 'SAT MAY 21 2026'."""
    return dt.strftime('%a %b %d %Y').upper()


def degrees_to_cardinal(deg):
    """Convert a wind bearing (degrees) to a 16-point cardinal label."""
    dirs = ['N','NNE','NE','ENE','E','ESE','SE','SSE',
            'S','SSW','SW','WSW','W','WNW','NW','NNW']
    return dirs[round(deg / 22.5) % 16]


def height_to_whole_rem(height_ft):
    """
    Split a float tide height into whole feet and remaining inches.
    Works correctly for negative heights (e.g. -0.04 ft → 0 ft 0 in).
    Returns (whole_ft, rem_in) as integers.
    """
    whole = int(height_ft)          # truncates toward zero
    rem   = round(abs(height_ft - whole) * 12)
    return whole, rem


# ─────────────────────────────────────────────────────────────────────────────
#  Moon phase & tide cycle  (computed locally — no API call)
# ─────────────────────────────────────────────────────────────────────────────

def compute_moon_age(now):
    """Return the moon's age in days (0.0 = new moon)."""
    days_since = (now.astimezone(timezone.utc) - LUNAR_REF_UTC).total_seconds() / 86400.0
    return ((days_since % LUNAR_CYCLE_DAYS) + LUNAR_CYCLE_DAYS) % LUNAR_CYCLE_DAYS


def moon_phase_key(age):
    """
    Map moon age (days) to a short phase key that matches the C icon names.
    Boundaries ported directly from the existing HTML/JS implementation.
    """
    if   age <  1.85: return 'NEW'
    elif age <  7.38: return 'WAXING_CRESCENT'
    elif age <  9.22: return 'FIRST_QUARTER'
    elif age < 14.77: return 'WAXING_GIBBOUS'
    elif age < 16.61: return 'FULL'
    elif age < 22.15: return 'WANING_GIBBOUS'
    elif age < 23.99: return 'LAST_QUARTER'
    else:             return 'WANING_CRESCENT'


def tide_cycle_key(moon_age):
    """
    Map moon age (days) to a tide cycle key: SPRING, NEAP, or HALF.
    Ported from the existing HTML/JS implementation.
    """
    pos = moon_age % 14.765
    if pos <  4.5: return 'SPRING'
    if pos <  6.5: return 'HALF'
    if pos < 10.5: return 'NEAP'
    if pos < 12.5: return 'HALF'
    return 'SPRING'


# ─────────────────────────────────────────────────────────────────────────────
#  NOAA tides
# ─────────────────────────────────────────────────────────────────────────────

def fetch_tides(tz, now):
    """
    Fetch hi/lo tide predictions from NOAA for a 3-day window
    (yesterday → tomorrow) so we catch tides near midnight.

    Returns:
        tides_today  — list of all hi/lo events for today
        next_tide    — dict for the next upcoming tide, or None
    """
    today     = now.date()
    yesterday = today - timedelta(days=1)
    tomorrow  = today + timedelta(days=1)

    url = (
        'https://api.tidesandcurrents.noaa.gov/api/prod/datagetter'
        f'?begin_date={yesterday.strftime("%Y%m%d")}'
        f'&end_date={tomorrow.strftime("%Y%m%d")}'
        f'&station={NOAA_STATION_ID}'
        f'&product=predictions&datum=MLLW'
        f'&time_zone=lst_ldt&interval=hilo&units=english&format=json'
    )

    try:
        resp = requests.get(url, timeout=15)
        resp.raise_for_status()
        data = resp.json()

        if 'predictions' not in data:
            msg = data.get('error', {}).get('message', 'No predictions in response')
            raise ValueError(msg)

        all_tides = []
        for p in data['predictions']:
            # NOAA returns local time via lst_ldt, format: "2026-05-21 07:53"
            t = datetime.strptime(p['t'], '%Y-%m-%d %H:%M').replace(tzinfo=tz)
            all_tides.append({
                '_dt':       t,                         # internal — stripped before output
                'time_str':  fmt_time_12h(t),
                'hour':      t.hour,
                'minute':    t.minute,
                'height_ft': round(float(p['v']), 2),
                'type':      p['type'],                  # 'H' or 'L'
            })

        # Today's tides only (for the graph/tide array)
        tides_today = [t for t in all_tides if t['_dt'].date() == today]

        # Next tide = first event still in the future (from full 3-day window)
        future    = [t for t in all_tides if t['_dt'] > now]
        next_raw  = future[0] if future else None

        next_tide = None
        if next_raw:
            whole, rem = height_to_whole_rem(next_raw['height_ft'])
            next_tide = {
                'time_str':       next_raw['time_str'],
                'height_whole_ft': whole,
                'height_rem_in':  rem,
                'type':           next_raw['type'],
            }

        # Boundary tides for spline continuity across the full display width.
        # Find the tide immediately BEFORE today's first and immediately AFTER
        # today's last by chronological position in the full 3-day list.
        # (Using date-based filtering gave the wrong tides when today's first/last
        # tide falls very near midnight — e.g. a 12:13 AM tide is today's first,
        # not tomorrow's, and a 10:45 PM tide is today's last, not yesterday's.)
        #
        # t_min = minutes from today's midnight; computed directly from the
        # datetime delta so it's correct regardless of day boundary.
        midnight_today = datetime(today.year, today.month, today.day,
                                  0, 0, 0, tzinfo=tz)

        today_indices = [i for i, t in enumerate(all_tides)
                         if t['_dt'].date() == today]

        if today_indices:
            prev_idx  = today_indices[0]  - 1
            after_idx = today_indices[-1] + 1
            prev_raw  = all_tides[prev_idx]  if prev_idx  >= 0               else None
            after_raw = all_tides[after_idx] if after_idx < len(all_tides)   else None
        else:
            prev_raw = after_raw = None

        def _boundary_dict(raw):
            t_min = (raw['_dt'] - midnight_today).total_seconds() / 60.0
            return {
                'time_str':  raw['time_str'],
                'hour':      raw['hour'],
                'minute':    raw['minute'],
                'height_ft': raw['height_ft'],
                'type':      raw['type'],
                't_min':     round(t_min, 1),
            }

        prev_tide_out  = _boundary_dict(prev_raw)  if prev_raw  else None
        next_after_out = _boundary_dict(after_raw) if after_raw else None

        log.info('✓ Tides: %d today, prev → %s, next_after → %s',
                 len(tides_today), prev_tide_out, next_after_out)

        # Strip the internal _dt key before returning
        tides_out = [{k: v for k, v in t.items() if k != '_dt'} for t in tides_today]
        return tides_out, next_tide, prev_tide_out, next_after_out

    except Exception as e:
        log.warning('Tides fetch failed: %s', e)
        return [], None


# ─────────────────────────────────────────────────────────────────────────────
#  Open-Meteo: wind + water temperature
# ─────────────────────────────────────────────────────────────────────────────

def fetch_weather():
    """
    Fetch current wind speed/direction and sea surface temperature.
    Returns (wind_speed_mph, wind_direction, water_temp_f).
    Any value may be None if its fetch fails.
    """
    wind_mph, wind_dir, water_temp = None, None, None

    # Wind (Open-Meteo forecast)
    try:
        r = requests.get(
            'https://api.open-meteo.com/v1/forecast',
            params={
                'latitude':        LATITUDE,
                'longitude':       LONGITUDE,
                'current':         'wind_speed_10m,wind_direction_10m',
                'wind_speed_unit': 'mph',
            },
            timeout=15,
        )
        r.raise_for_status()
        current  = r.json().get('current', {})
        wind_mph = round(current['wind_speed_10m'])
        wind_dir = degrees_to_cardinal(current['wind_direction_10m'])
        log.info('✓ Wind: %s %d mph', wind_dir, wind_mph)
    except Exception as e:
        log.warning('Wind fetch failed: %s', e)

    # Water temperature (Open-Meteo marine)
    try:
        r = requests.get(
            'https://marine-api.open-meteo.com/v1/marine',
            params={
                'latitude':  LATITUDE,
                'longitude': LONGITUDE,
                'current':   'sea_surface_temperature',
            },
            timeout=15,
        )
        r.raise_for_status()
        current    = r.json().get('current', {})
        temp_c     = current['sea_surface_temperature']
        water_temp = round(temp_c * 9.0 / 5.0 + 32.0, 1)
        log.info('✓ Water temp: %.1f °F', water_temp)
    except Exception as e:
        log.warning('Water temp fetch failed: %s', e)

    return wind_mph, wind_dir, water_temp


# ─────────────────────────────────────────────────────────────────────────────
#  Open-Meteo: hours since last rain
# ─────────────────────────────────────────────────────────────────────────────

def fetch_rain(tz, now):
    """
    Scan the last 5 days of hourly precipitation data and return the number
    of hours since the last qualifying rain event (>= RAIN_THRESHOLD inches).
    Returns None if no qualifying rain found within RAIN_MAX_HOURS.
    """
    try:
        r = requests.get(
            'https://api.open-meteo.com/v1/forecast',
            params={
                'latitude':           LATITUDE,
                'longitude':          LONGITUDE,
                'hourly':             'precipitation',
                'past_days':          5,
                'timezone':           TIMEZONE,
                'precipitation_unit': 'inch',
            },
            timeout=15,
        )
        r.raise_for_status()
        hourly = r.json().get('hourly', {})
        times  = hourly.get('time', [])
        precip = hourly.get('precipitation', [])

        if not times or not precip:
            raise ValueError('Unexpected response shape from Open-Meteo rain endpoint')

        # Locate the current hour in the time array
        current_hour_str = now.strftime('%Y-%m-%dT%H:00')
        if current_hour_str in times:
            current_idx = times.index(current_hour_str)
        else:
            # Fall back: find the latest entry that's <= the current hour
            current_idx = 0
            for i in range(len(times) - 1, -1, -1):
                if times[i] <= current_hour_str:
                    current_idx = i
                    break

        # Scan backwards for the most recent qualifying rain event
        last_rain_idx = -1
        for i in range(current_idx, -1, -1):
            if precip[i] is not None and precip[i] >= RAIN_THRESHOLD:
                last_rain_idx = i
                break

        if last_rain_idx == -1:
            log.info('✓ Rain: no qualifying rain in the past %d h', RAIN_MAX_HOURS)
            return None

        hours_since = current_idx - last_rain_idx
        if hours_since > RAIN_MAX_HOURS:
            log.info('✓ Rain: last rain was %d h ago — beyond cap, returning None', hours_since)
            return None

        log.info('✓ Rain: last rain %d h ago (%.2f in)', hours_since, precip[last_rain_idx])
        return hours_since

    except Exception as e:
        log.warning('Rain fetch failed: %s', e)
        return None


# ─────────────────────────────────────────────────────────────────────────────
#  Sunrise / Sunset
# ─────────────────────────────────────────────────────────────────────────────

def fetch_sunrise_sunset(tz):
    """
    Fetch today's sunrise and sunset times from sunrise-sunset.org.
    The API returns UTC ISO strings; we convert to local time.

    Returns (sunrise_hour, sunrise_minute, sunrise_str,
             sunset_hour,  sunset_minute,  sunset_str)
    All values are None on failure.
    """
    try:
        r = requests.get(
            'https://api.sunrise-sunset.org/json',
            params={'lat': LATITUDE, 'lng': LONGITUDE, 'formatted': 0},
            timeout=15,
        )
        r.raise_for_status()
        data = r.json()

        if data.get('status') != 'OK':
            raise ValueError(f'API returned status: {data.get("status")}')

        def parse_utc(iso_str):
            dt_local = datetime.fromisoformat(iso_str).astimezone(tz)
            return dt_local.hour, dt_local.minute, fmt_time_12h(dt_local)

        sr_h, sr_m, sr_str = parse_utc(data['results']['sunrise'])
        ss_h, ss_m, ss_str = parse_utc(data['results']['sunset'])
        log.info('✓ Sunrise: %s  Sunset: %s', sr_str, ss_str)
        return sr_h, sr_m, sr_str, ss_h, ss_m, ss_str

    except Exception as e:
        log.warning('Sunrise/sunset fetch failed: %s', e)
        return None, None, None, None, None, None


# ─────────────────────────────────────────────────────────────────────────────
#  Main
# ─────────────────────────────────────────────────────────────────────────────

def main():
    tz  = ZoneInfo(TIMEZONE)
    now = datetime.now(tz)

    log.info('Fetching data for %s ...', now.strftime('%Y-%m-%d %H:%M %Z'))

    tides, next_tide, prev_tide, next_tide_after = fetch_tides(tz, now)
    wind_mph, wind_dir, water_temp          = fetch_weather()
    rain_hours                              = fetch_rain(tz, now)
    sr_h, sr_m, sr_str, ss_h, ss_m, ss_str = fetch_sunrise_sunset(tz)

    moon_age   = compute_moon_age(now)
    moon_phase = moon_phase_key(moon_age)
    tide_cycle = tide_cycle_key(moon_age)

    payload = {
        'generated_at':     now.strftime('%Y-%m-%dT%H:%M:%S'),
        'current_time_str': fmt_time_12h(now),
        'current_date_str': fmt_date_upper(now),
        'current_hour':     now.hour,
        'current_minute':   now.minute,

        'tides':           tides,
        'next_tide':       next_tide,
        'prev_tide':       prev_tide,
        'next_tide_after': next_tide_after,

        'tide_cycle': tide_cycle,

        'wind_speed_mph':   wind_mph,
        'wind_direction':   wind_dir,
        'rain_hours_since': rain_hours,
        'water_temp_f':     water_temp,

        'sunrise_hour':   sr_h,
        'sunrise_minute': sr_m,
        'sunrise_str':    sr_str,
        'sunset_hour':    ss_h,
        'sunset_minute':  ss_m,
        'sunset_str':     ss_str,

        'moon_phase': moon_phase,
        'moon_age':   round(moon_age, 1),
    }

    tmp = OUTPUT_PATH.with_suffix('.tmp')
    tmp.write_text(json.dumps(payload, indent=2))
    tmp.replace(OUTPUT_PATH)
    log.info('✓ Written → %s', OUTPUT_PATH)


if __name__ == '__main__':
    main()

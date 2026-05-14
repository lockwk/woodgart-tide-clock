# Tide Clock — Data Sources & Attributes

## NOAA Tides & Currents API
`https://api.tidesandcurrents.noaa.gov/api/prod/datagetter`

Station 9413745 (Santa Cruz, CA). Fetches hi/lo predictions for a 3-day window (yesterday → tomorrow), MLLW datum, English units, local time zone.

| Attribute | Description |
|-----------|-------------|
| `t` | Tide time → stored as ISO timestamp |
| `v` | Tide height in feet |
| `type` | `H` (high) or `L` (low) |

---

## Open-Meteo Forecast API — Current Weather
`https://api.open-meteo.com/v1/forecast`

Location: 36.9741°N, 122.0308°W (Santa Cruz, CA)

| Attribute | Description |
|-----------|-------------|
| `current.wind_speed_10m` | Wind speed in mph |
| `current.wind_direction_10m` | Wind direction in degrees → converted to cardinal (N, NNE, etc.) |

---

## Open-Meteo Forecast API — Hourly Rain
`https://api.open-meteo.com/v1/forecast`

Location: 36.9741°N, 122.0308°W. Fetches past 5 days of hourly data, timezone: America/Los_Angeles, precipitation unit: inches.

| Attribute | Description |
|-----------|-------------|
| `hourly.time` | Array of hourly timestamps |
| `hourly.precipitation` | Precipitation in inches per hour → used to calculate hours since last rain and last rain amount |

---

## Open-Meteo Marine API
`https://marine-api.open-meteo.com/v1/marine`

Location: 36.9741°N, 122.0308°W

| Attribute | Description |
|-----------|-------------|
| `current.sea_surface_temperature` | Sea surface temperature in °C → converted to °F for display |

---

## Sunrise-Sunset.org API
`https://api.sunrise-sunset.org/json`

Location: 36.9741°N, 122.0308°W

| Attribute | Description |
|-----------|-------------|
| `results.sunrise` | Sunrise time (UTC ISO string) → converted to local time for display |
| `results.sunset` | Sunset time (UTC ISO string) → converted to local time for display |

---

## Moon Phase — Computed Locally
No API call. Moon age is calculated client-side from the current date using a standard lunar cycle formula.

| Derived Value | Description |
|---------------|-------------|
| Moon age (days) | Used to determine the current tide cycle state |
| Tide cycle state | `Spring Tide`, `Half Tide`, or `Neap Tide` based on moon age position |

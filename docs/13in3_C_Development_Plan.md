# 13.3" Tide Clock — Native C Development Plan

**Display:** 13.3inch e-Paper HAT (K) — 960×680px, 4-level grayscale  
**Language:** C, using Waveshare's existing `EPD_13in3k` driver + `GUI_Paint` library  
**Data layer:** Python fetcher writes JSON → C renderer reads it  
**Fonts:** Inter-SemiBold/Light converted to C bitmap arrays  

---

## Architecture Overview

```
[Hourly cron/timer]
       │
       ▼
src/fetch/fetcher.py   ← fetches all APIs, writes /tmp/tide_data.json
       │
       ▼
/tmp/tide_data.json    ← shared data contract between Python and C
       │
       ▼
src/c/tide_clock.c     ← reads JSON, renders frame buffer, pushes to display
```

The Python fetcher reuses the existing API knowledge from `config.py` and `data_sources.md`. The C renderer owns everything from JSON → pixels → display.

---

## Display Layout (960×680)

```
┌──────────────────────────────────────────────────────────────────┐
│  9:35 AM  SAT APR 4 2026    ☁ 56 HRS   〜 S 3MPH   ≈≈ 58.8°F   │  ← Status bar (~35px)
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│         H 3'5" 1:17                                              │
│              •                        L 2'1" 6:00               │
│  H -'04  ___/_______________           •___                      │  ← Tide graph
│  7:53 •/                   \         /                           │    (~405px tall)
│  ▓▓▓▓▓│   9 AM             \       /                            │
│  ▓▓▓▓▓│   (current hour)    \     /                             │
│        │                     \___/                               │
│                                                                  │
├────────────┬────────────┬────────────┬────────────┤  ← divider line
│  1 ft 3 in │    ∿       │ ☀ 00:00 AM │     ○      │
│     ▲▲     │    L       │   SUNRISE  │    0.0     │  ← Bottom panels
│  00:00 AM  │SPRING TIDE │ ☽ 00:00 PM │    NEW     │    (~240px tall)
│  HIGH TIDE │            │   SUNSET   │            │
└────────────┴────────────┴────────────┴────────────┘
```

**4-gray palette mapping:**
- `GRAY1` (blackest `#000000`) — text, curves, lines
- `GRAY2` (`#555555`) — tide graph nighttime fill, current-hour bar fill
- `GRAY3` (`#AAAAAA`) — (reserved for anti-aliased curve edges if needed)
- `GRAY4` (white `#FFFFFF`) — background

---

## JSON Data Contract

`/tmp/tide_data.json` — written by Python fetcher, read by C renderer.

```json
{
  "generated_at": "2026-05-19T09:00:00",
  "current_time_str": "9:35 AM",
  "current_date_str": "SAT MAY 19 2026",
  "current_hour": 9,
  "current_minute": 35,

  "tides": [
    { "time_str": "7:53 AM",  "hour": 7,  "minute": 53, "height_ft": -0.04, "type": "H" },
    { "time_str": "1:17 PM",  "hour": 13, "minute": 17, "height_ft": 3.42,  "type": "H" },
    { "time_str": "6:00 PM",  "hour": 18, "minute": 0,  "height_ft": 2.08,  "type": "L" }
  ],

  "next_tide": {
    "time_str": "1:17 PM",
    "height_whole_ft": 1,
    "height_rem_in": 3,
    "type": "H"
  },

  "tide_cycle": "SPRING",

  "wind_speed_mph": 3,
  "wind_direction": "S",
  "rain_hours_since": 56,
  "water_temp_f": 58.8,

  "sunrise_hour": 6,
  "sunrise_minute": 15,
  "sunrise_str": "6:15 AM",
  "sunset_hour": 20,
  "sunset_minute": 4,
  "sunset_str": "8:04 PM",

  "moon_phase": "NEW",
  "moon_age": 0.0
}
```

---

## Phases

### Phase 0 — Repo & Directory Structure
**Goal:** create the file skeleton before writing any code.

```
src/
  fetch/
    fetcher_13in3.py     ← new Python fetcher (or extend existing)
  c/
    tide_clock.c         ← main program
    json_reader.h/.c     ← minimal JSON field extractor (no external lib)
    tide_curve.h/.c      ← spline interpolation + graph rendering
    layout.h/.c          ← section drawing functions
    fonts/
      inter_lt_48.h      ← Inter Light, 48px (tide height, spring tide letter)
      inter_b_14.h       ← Inter Bold, 14px (all labels, times, status bar)
    icons/
      icon_rain.h
      icon_wind.h
      icon_watertemp.h
      icon_sunrise.h
      icon_sunset.h
      icon_moon_new.h
      icon_moon_full.h
      icon_moon_waxing_crescent.h
      icon_moon_waxing_gibbous.h
      icon_moon_first_quarter.h
      icon_moon_waning_gibbous.h
      icon_moon_last_quarter.h
      icon_moon_waning_crescent.h
      icon_tide_spring.h
      icon_tide_neap.h
      icon_tide_half.h
      icon_arrow_up.h
      icon_arrow_down.h
    Makefile
services/
  tide-clock-13in3.service
  tide-clock-13in3.timer
```

**You and I do together:** agree on the final directory layout, create empty files, confirm it fits within the existing repo structure.

---

### Phase 1 — Python Fetcher
**Goal:** `fetcher_13in3.py` fetches all APIs and writes `tide_data.json`.

This is mostly a refactor/extension of the existing display logic in `tide_display.py`, but decoupled from any rendering:

- NOAA tides API — same station 9413745, pull today's hi/lo, compute `next_tide`
- Open-Meteo forecast — wind speed + direction
- Open-Meteo hourly — compute hours since last rain ≥ 0.25 in
- Open-Meteo marine — water surface temp → °F
- Sunrise-Sunset.org — sunrise/sunset times
- Moon phase — local formula (already exists in some form)
- Tide cycle — Spring/Neap/Half from moon age
- Write to `/tmp/tide_data.json`

**Deliverable:** `src/fetch/fetcher_13in3.py` that runs standalone and writes the JSON file.  
**Test:** Run on Pi, inspect the JSON, confirm all fields are present and correct.

---

### Phase 2 — Font Conversion Tool
**Goal:** convert `assets/fonts/Inter-SemiBold.ttf` and `Inter-Light.ttf` into C header files containing 2-bit-per-pixel bitmap glyph arrays.

**Approach:** write `tools/font_to_c.py` using Pillow's `ImageFont.truetype()`:
1. Render each printable ASCII character at the target point size
2. Quantize to 4 gray levels (GRAY1–GRAY4)
3. Emit a `.h` file in the same format as Waveshare's `Font24` — a `sFONT` struct with a `uint8_t table[]` and `Width`/`Height`.

**Sizes to generate** (confirmed against design in Phase 3.5):
| Header | Font | Target px height | Used for |
|--------|------|-----------------|---------|
| `inter_lt_48.h` | Light | 48 | Tide height large number (e.g. "1 FT 3 IN"), spring tide cycle letter |
| `inter_b_14.h` | Bold | 14 | Everything else — labels, times, status bar, panel text |

**Deliverable:** `tools/font_to_c.py` + all five `.h` files in `src/c/fonts/`.  
**Test:** Write a minimal C test program that renders "Hello 123" to a BMP on the Pi using the new fonts; visually compare against the Figma design.

---

### Phase 3 — Icon Bitmap Conversion
**Goal:** convert SVG icons in `assets/icons/1280x720_dark/` to 2-bit C arrays.

**Approach:** write `tools/svg_to_c.py`:
1. Use Inkscape CLI (`inkscape --export-png`) or `cairosvg` to render SVG → PNG at target size
2. Convert PNG → 2-bit (4-gray) array using Pillow
3. Emit as C header with a struct matching the glyph format

**Icons to convert** (with approximate target sizes for the 960×680 display):
| Icon | Source SVG | Target size |
|------|-----------|------------|
| Rain | `rain.svg` | 20×20 |
| Wind | `wind lines.svg` | 20×20 |
| Water temp | `waves.svg` | 20×20 |
| Sunrise | `sunrise.svg` | 28×28 |
| Sunset | `sunset.svg` | 28×28 |
| Moon phases (×8) | `Moon Phase *.svg` | 80×80 |
| Tide spring | `spring tide curve.svg` | 70×50 |
| Tide neap | `neap tide curve.svg` | 70×50 |
| Tide half | `half tide curve.svg` | 70×50 |
| Arrow up | — | 16×16 (draw programmatically) |
| Arrow down | — | 16×16 (draw programmatically) |

**Deliverable:** `tools/svg_to_c.py` + all icon `.h` files in `src/c/icons/`.  
**Test:** Render each icon into a test BMP on the Pi; spot-check against Figma.

---

### Phase 3.5 — Visual Fidelity Test
**Goal:** push a static "golden frame" to the actual display before building the full renderer, so you can judge whether the visual quality meets the design standard. Go/no-go checkpoint.

**Why here:** the font conversion tool exists (Phase 2) and a basic tide curve can be hardcoded, but we haven't invested in the full JSON reader or layout system yet. Cheap moment to course-correct if something looks wrong.

**What to render:** a single hardcoded C program (`src/c/fidelity_test.c`) that exercises the highest-risk visual elements:

1. **Large Inter numbers** — e.g., "1 ft 3 in" at the sizes chosen for the Next Tide panel. This is the biggest unknown: does Inter SemiBold at ~96px look right on the 4-gray display, or does it need size/weight adjustments?
2. **Small label text** — "HIGH TIDE", "SPRING TIDE", "SUNRISE" at ~16px Inter Light. Small text on e-paper is notoriously tricky.
3. **Tide curve** — spline through 4–5 hardcoded tide points across the full 960px width, rendered in 4-gray mode.
4. **One icon** — e.g., the sunrise icon at its target size.
5. **Divider lines and panel grid** — just to confirm stroke weights look right.

**What it is not:** no JSON, no API data, no full layout. Just a static test frame.

**How to evaluate:**
- Photograph the display
- Compare against a screenshot of the Figma design at the same proportions
- Check: Are the large numbers legible and well-shaped? Is the small text readable? Does the curve look smooth?

**Possible outcomes:**
- ✅ Looks great → proceed to Phase 4 as planned
- ⚠️ Font sizes need adjustment → re-run `font_to_c.py` at revised sizes, retest
- ⚠️ Small text is too thin/broken → switch Inter Light to Inter SemiBold at that size, or bump up minimum size
- ⚠️ Curve looks jagged → try increasing line width from 1px to 2px, or add intermediate interpolation points

**Deliverable:** `src/c/fidelity_test.c` + a photo of the display render + a decision to proceed (or adjust and retest).

---

### Phase 4 — C Scaffolding & JSON Reader
**Goal:** get a compilable C program that reads the JSON file and prints all fields.

**Sub-tasks:**

1. **Makefile** — modeled on the Waveshare demo Makefile but pointing to `src/c/`. Links against `lgpio` and the Waveshare `EPD_13in3k.c` + `GUI_Paint.c` sources.

2. **`json_reader.h/.c`** — a minimal, dependency-free JSON field extractor. No full JSON library needed — just functions like:
   ```c
   const char* json_get_string(const char* json, const char* key);
   double      json_get_double(const char* json, const char* key);
   int         json_get_int(const char* json, const char* key);
   // For the tides array, a simple indexed accessor
   int         json_get_tide(const char* json, int index, TidePoint* out);
   ```
   Implemented with `strstr` + careful pointer arithmetic — no malloc beyond reading the whole file.

3. **`tide_clock.c` skeleton:**
   ```c
   int main(void) {
       signal(SIGINT, Handler);
       
       // 1. Read JSON
       char* json = read_file("/tmp/tide_data.json");
       ClockData data;
       parse_clock_data(json, &data);
       
       // 2. Init display
       DEV_Module_Init();
       EPD_13IN3K_Init_4GRAY();
       
       // 3. Allocate 4-gray frame buffer
       // Buffer: (960/4) * 680 = 163,200 bytes
       UBYTE* image = malloc(IMAGE_SIZE_4GRAY);
       Paint_NewImage(image, EPD_13IN3K_WIDTH, EPD_13IN3K_HEIGHT, 0, GRAY4);
       Paint_SetScale(4);
       Paint_Clear(GRAY4);
       
       // 4. Render sections (stubs for now)
       render_status_bar(&data);
       render_tide_graph(&data);
       render_bottom_panels(&data);
       
       // 5. Push to display
       EPD_13IN3K_4GrayDisplay(image);
       EPD_13IN3K_Sleep();
       DEV_Module_Exit();
       free(image);
       return 0;
   }
   ```

**Deliverable:** compiles and runs on Pi without crashing, fills display white, prints parsed data to stdout.  
**Build command:** `sudo make clean && make EPD=epd13in3k RPI`

---

### Phase 5 — Status Bar
**Goal:** render the top strip — time, date, rain, wind, water temp.

Layout (y=0 to y=34, full 960px width):
```
│ 9:35 AM │ SAT MAY 19 2026 │ ☁ 56 HRS │ 〜 S 3MPH │ ≈≈ 58.8°F │
```

**Implementation:**
- Use `inter_sb_20.h` for time and date strings
- Use `inter_lt_16.h` for weather values
- Inline the three weather icons (rain, wind, water temp) at 20×20
- Draw a thin horizontal divider line at y=35

**Deliverable:** status bar renders correctly; all fields populated from JSON.

---

### Phase 6 — Tide Graph: Frame & Shading
**Goal:** draw the graph area background — night shading, current-hour bar, axis.

**Coordinate mapping:**
- X axis: sunrise hour → x=0, sunset hour → x=960 (roughly; exact formula TBD based on Figma proportions)
- Actually: graph spans from ~1 hour before sunrise to ~1 hour after sunset
- Y axis: min tide height → near bottom, max tide height → near top (with padding)

**Steps:**
1. Compute pixel X for sunrise and sunset from the data
2. Fill left region (before sunrise) with GRAY2 — the dark "nighttime" fill
3. Fill right region (after sunset) with GRAY2
4. Draw current-hour vertical bar: a solid GRAY1 rectangle ~16px wide at current-time X
5. Label the bar with current hour (e.g., "9 AM") in `inter_lt_16.h` at the bar's bottom
6. Draw the thin horizontal divider between graph area and bottom panels

---

### Phase 7 — Tide Graph: Curve & Labels
**Goal:** draw the smooth tide curve and label each peak/trough.

**Spline interpolation:**
- Input: N tide hi/lo points (time in minutes since midnight, height in feet)
- Algorithm: natural cubic spline through those N points
- Evaluate at every pixel column x across the graph
- Convert each evaluated height → pixel Y
- Draw with `Paint_DrawLine` between adjacent (x, y) pairs using 2px width

**Implementation of `tide_curve.h/.c`:**
```c
typedef struct { float t; float h; } TidePoint;
void compute_spline_coefficients(TidePoint* pts, int n, SplineCoeff* out);
float eval_spline(SplineCoeff* coeffs, int n, float t);
void render_tide_curve(UBYTE* image, TidePoint* pts, int n,
                       float t_start, float t_end,
                       int x0, int x1, int y_min_px, int y_max_px,
                       float h_min, float h_max);
```

**Peak/trough markers and labels:**
- For each tide point in the day's data: draw a 16px filled circle at the (x, y) pixel
- Above each circle: label with type (H or L), height (e.g., `3'5"`), and time (e.g., `1:17`)
- Use `inter_sb_20.h` for labels; position above/below curve so they don't overlap

---

### Phase 8 — Bottom Panels
**Goal:** render the four equal panels below the divider line.

Each panel is 240×240px (960/4 columns, from y=440 to y=680). Draw thin vertical dividers between them.

**Panel 1 — Next Tide:**
- Tide height: large number in `inter_sb_96.h` + `inter_sb_56.h` (e.g., "1 ft 3 in")
- Direction arrows: programmatically drawn triangles (▲▲ for rising, ▼▼ for falling)
- Time string in `inter_sb_28.h`
- Label "HIGH TIDE" or "LOW TIDE" in `inter_lt_16.h`

**Panel 2 — Tide Cycle:**
- Tide cycle icon (spring/neap/half) from `icon_tide_*.h`
- Cycle letter ("S" or "N") in large font
- Label "SPRING TIDE" / "NEAP TIDE" in `inter_lt_16.h`

**Panel 3 — Sunrise & Sunset:**
- Sunrise icon + time in `inter_sb_28.h` + "SUNRISE" in `inter_lt_16.h`
- Sunset icon + time + "SUNSET"
- Vertical layout with the two rows centered

**Panel 4 — Moon Phase:**
- Moon phase icon (one of 8 bitmaps) centered
- Moon age (e.g., "0.0") in `inter_sb_56.h`
- Phase name ("NEW", "FULL", etc.) in `inter_lt_16.h`

---

### Phase 9 — Integration & First Render on Pi
**Goal:** run the full pipeline end to end on the Pi for the first time.

Steps:
1. Run `fetcher_13in3.py` → confirm `/tmp/tide_data.json` is correct
2. Build the C program: `sudo make clean && make EPD=epd13in3k RPI`
3. Run: `sudo ./epd`
4. Photograph the display; compare against Figma design
5. Tune font sizes, pixel positions, curve scaling, icon sizes as needed

This phase is iterative — expect multiple build/test cycles to dial in the layout.

---

### Phase 10 — Partial Refresh for Current Hour
**Goal:** after the initial full 4-gray render, update only the current-hour bar region hourly without a full display refresh.

**Approach:**
- After the full render, call `EPD_13IN3K_Init_Part()` to switch to partial-refresh mode
- Each hour: re-render only the narrow vertical strip containing the previous + new hour bar positions
- Use `EPD_13IN3K_Display_Part()` with the minimal bounding box

**Note:** partial refresh only supports 1-bit (not 4-gray), so the bar area will refresh in B&W. This is acceptable — the current-hour bar is solid black anyway.

---

### Phase 11 — systemd Service
**Goal:** run the tide clock automatically on boot, fetching data and refreshing hourly.

Two units:
1. **`tide-clock-13in3-fetch.service`** + **`tide-clock-13in3-fetch.timer`** — runs Python fetcher every hour
2. **`tide-clock-13in3.service`** — runs C renderer after each fetch

OR: combine into a single shell script that runs fetcher then renderer, called by one timer.

Update `deploy.sh` to handle the 13in3 service alongside the existing 7in3 service.

---

## Key Technical Notes

### 4-Gray Buffer Math
```c
// 2 bits per pixel → 4 pixels per byte
#define IMAGE_SIZE_4GRAY  ((EPD_13IN3K_WIDTH / 4) * EPD_13IN3K_HEIGHT)
// = (960 / 4) * 680 = 163,200 bytes
```

### Build Command
```bash
# On the Pi, in the c/ directory:
sudo make clean && make EPD=epd13in3k RPI
sudo ./epd
```

### Makefile additions needed
The existing Waveshare Makefile needs to also compile our new source files (`json_reader.c`, `tide_curve.c`, `layout.c`). We'll write a custom Makefile rather than modifying the Waveshare one.

### Partial Refresh Constraint
`EPD_13IN3K_Display_Part()` requires x coordinates to be byte-aligned (multiples of 8). The current-hour bar x position must be rounded to the nearest multiple of 8.

### No External JSON Library
To keep the build simple and avoid installing dependencies, the JSON reader is hand-rolled using `strstr`. The JSON schema is fixed and well-known, so this is safe.

---

## Development Session Sequence

| Session | Focus | Deliverable |
|---------|-------|-------------|
| 1 | Repo structure + JSON schema + Python fetcher | `fetcher_13in3.py` running on Pi |
| 2 | Font conversion tool + 5 Inter header files | `font_to_c.py` + `.h` files |
| 3 | Icon conversion tool + all icon headers | `svg_to_c.py` + `.h` files |
| 3.5 | **Visual fidelity test** | Static test frame on display; go/no-go decision |
| 4 | Makefile + JSON reader + C scaffolding | Program compiles, clears display |
| 5 | Status bar | Top bar renders correctly |
| 6 | Tide graph shading + current hour bar | Night fill + hour bar visible |
| 7 | Tide curve + peak labels | Smooth curve with H/L labels |
| 8 | Bottom panels | All 4 panels render |
| 9 | Integration + on-Pi photo review | First full render, tuning round |
| 10 | Partial refresh + systemd service | Clock auto-runs, updates hourly |

---

## Open Questions (to revisit before Session 1)

1. **NOAA station** — Are we sticking with 9413745 (Santa Cruz)? Or is this going to be configurable?
2. **Existing `display_13in3.py`** — That file currently only has display constants, no rendering logic. The Python fetcher is a new file (`fetcher_13in3.py`), not an extension of it.
3. **Repo branch** — All C work goes on the `dev` branch, per existing convention.
4. **Pi cleanup** — We should do the "clone repo into `/home/pi/tide-clock/`" cleanup step before starting Session 1, per the existing Next Steps in the project notes.

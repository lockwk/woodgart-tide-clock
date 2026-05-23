/*
 * fidelity_test.c — Phase 3.5 Visual Fidelity Test
 *
 * Renders a single static frame to the 13.3" e-paper display so you can
 * judge visual quality BEFORE building the full renderer.  No JSON, no
 * live data — everything is hardcoded.
 *
 * Elements under test (from the Phase 3.5 spec):
 *   1. Large Inter numbers   — "1 FT 3 IN" in inter_lt_48 (48px Light) in Panel 1
 *   2. Small label text      — inter_b_14 (14px Bold) throughout
 *   3. Tide curve            — natural cubic spline through 5 hardcoded points
 *   4. One icon              — icon_sunrise in the status bar (28×28)
 *   5. Divider lines & panel grid
 *
 * After photographing the result, decide:
 *   ✅ Looks great  → proceed to Phase 4 (Makefile + JSON reader + scaffolding)
 *   ⚠️ Font too small/large  → re-run font_to_c.py at revised pt size, retest
 *   ⚠️ Small text broken     → adjust weight or size in font_to_c.py, retest
 *   ⚠️ Curve jagged          → increase line thickness from 2px to 3px
 *
 * -----------------------------------------------------------------------
 * Build (on the Pi, from ~/tide-clock-dev/src/c/):
 *
 *   gcc fidelity_test.c \
 *       inter_font.c \
 *       fonts/inter_lt_48.c fonts/inter_b_14.c \
 *       /home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_13in3k.c \
 *       /home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib/Config/DEV_Config.c \
 *       /home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib/Config/dev_hardware_SPI.c \
 *       -I. \
 *       -I/home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib \
 *       -I/home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib/e-Paper \
 *       -I/home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib/Config \
 *       -D USE_LGPIO_LIB -D RPI \
 *       -llgpio -lm \
 *       -o fidelity_test
 *
 * Run:
 *   sudo systemctl stop tide-clock     # stop 7.3" clock if running
 *   sudo ./fidelity_test
 *   Press Ctrl+C to sleep the display and exit cleanly.
 * -----------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>

/* Waveshare driver headers (resolved via -I flags) */
#include "DEV_Config.h"
#include "EPD_13in3k.h"

/* Font renderer + two Inter sizes */
#include "inter_font.h"
#include "fonts/inter_lt_48.h"   /* Light 48px — tide height, spring tide letter */
#include "fonts/inter_b_14.h"    /* Bold  14px — all labels, times, status bar   */

/* Icon types + icons used in this test */
#include "icons/icon_types.h"
#include "icons/icon_sunrise.h"
#include "icons/icon_rain.h"
#include "icons/icon_wind.h"
#include "icons/icon_watertemp.h"
#include "icons/icon_moon_new.h"
#include "icons/icon_tide_spring.h"
#include "icons/icon_high_tide.h"

/* ==========================================================================
 * Display geometry
 * ======================================================================= */

#define DISP_W        960
#define DISP_H        680
#define DISP_STRIDE   (DISP_W / 4)            /* bytes per row = 240       */
#define DISP_BUFSIZE  (DISP_STRIDE * DISP_H)  /* 163,200 bytes             */

/* Layout ----------------------------------------------------------------- */
#define STATUS_H      35    /* status bar height (y = 0 .. 34)              */
#define DIVIDER_Y1    35    /* thin line: status bar → graph                */
#define GRAPH_TOP     36    /* graph area top (inclusive)                   */
#define GRAPH_BOT     439   /* graph area bottom (inclusive)                */
#define DIVIDER_Y2    440   /* thin line: graph → bottom panels             */
#define PANEL_TOP     441   /* bottom panels top                            */
#define PANEL_BOT     679   /* bottom panels bottom                         */
#define PANEL_W       240   /* each of the four 240-px panels               */

/* Tide curve pixel bounds (within GRAPH_TOP..GRAPH_BOT)
 * Leaves 34 px above for peak labels, 19 px below for baseline. */
#define CURVE_TOP_Y   70
#define CURVE_BOT_Y   420

/* Gray level encoding (Waveshare 4-gray frame buffer)
 * 2 bits per pixel, 4 pixels per byte, MSB-first.
 * 0x00 = black (#000000)    — text, curve, dividers
 * 0x01 = dark gray (#555555) — nighttime fill, current-hour bar
 * 0x02 = light gray (#AAAAAA)
 * 0x03 = white (#FFFFFF)    — background (memset 0xFF = four 0x03 pixels)  */
#define GRAY1  0x00
#define GRAY2  0x01
#define GRAY3  0x02
#define GRAY4  0x03

/* Hardcoded demo time ---------------------------------------------------- */
/* "9:35 AM, SAT MAY 22 2026" */
#define CUR_HOUR    9
#define CUR_MIN     35
#define CUR_T       (CUR_HOUR * 60 + CUR_MIN)   /* = 575 minutes */

/* Sunrise 6:15 AM, sunset 8:04 PM */
#define SUNRISE_T   (6 * 60 + 15)               /* = 375 */
#define SUNSET_T    (20 * 60 + 4)               /* = 1204 */

/* Tide graph time span: 5:00 AM (300) → 9:00 PM (1260) */
#define T_START     300
#define T_END       1260

/* Tide height axis: -0.5 ft → 4.5 ft */
#define H_MIN       (-0.5f)
#define H_MAX       4.5f

/* ==========================================================================
 * Signal handler — sleep display on Ctrl+C
 * ======================================================================= */

static volatile int g_exit = 0;

static void handler(int sig)
{
    (void)sig;
    g_exit = 1;
}

/* ==========================================================================
 * Low-level pixel operations
 * ======================================================================= */

/* Write one pixel into the 4-gray frame buffer.
 * gv must be GRAY1 / GRAY2 / GRAY3 / GRAY4 (0x00 – 0x03). */
static void set_pixel(uint8_t *buf, int x, int y, uint8_t gv)
{
    if (x < 0 || x >= DISP_W || y < 0 || y >= DISP_H) return;
    int addr  = y * DISP_STRIDE + x / 4;
    int shift = 6 - (x % 4) * 2;
    buf[addr] = (uint8_t)((buf[addr] & ~(0x03 << shift)) | ((gv & 0x03) << shift));
}

/* Bresenham line — 1px thick. */
static void draw_line(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t gv)
{
    int dx =  abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        set_pixel(buf, x0, y0, gv);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { if (x0 == x1) break; err += dy; x0 += sx; }
        if (e2 <= dx) { if (y0 == y1) break; err += dx; y0 += sy; }
    }
}

/* 2-px thick Bresenham line (draws at y and y+1) — used for tide curve. */
static void draw_line2(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t gv)
{
    draw_line(buf, x0, y0,   x1, y1,   gv);
    draw_line(buf, x0, y0+1, x1, y1+1, gv);
}

/* Fill an axis-aligned rectangle. */
static void fill_rect(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t gv)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            set_pixel(buf, x, y, gv);
}

/* Draw a full-width horizontal rule. */
static void draw_hline_full(uint8_t *buf, int y, uint8_t gv)
{
    fill_rect(buf, 0, y, DISP_W - 1, y, gv);
}

/* Draw a full-height vertical rule. */
static void draw_vline_full(uint8_t *buf, int x, uint8_t gv)
{
    fill_rect(buf, x, PANEL_TOP, x, PANEL_BOT, gv);
}

/* Filled circle (for tide peak/trough dots). */
static void draw_circle(uint8_t *buf, int cx, int cy, int r, uint8_t gv)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                set_pixel(buf, cx + dx, cy + dy, gv);
}

/* Upward-pointing filled triangle — used for "rising tide" arrows.
 * Apex at (cx, top_y), base at y = top_y + h - 1, width = 2*h-1 px. */
static void draw_arrow_up(uint8_t *buf, int cx, int top_y, int h, uint8_t gv)
{
    for (int dy = 0; dy < h; dy++) {
        int half = dy;                         /* 0 at apex, h-1 at base */
        for (int dx = -half; dx <= half; dx++)
            set_pixel(buf, cx + dx, top_y + dy, gv);
    }
}

/* ==========================================================================
 * Icon blitter
 * ======================================================================= */

/*
 * Icon files use "natural" encoding: 0x03 = black stroke, 0x00 = white bg.
 * The display uses the inverse: 0x00 = black, 0x03 = white.
 * We invert at blit time: display_gv = (~gv) & 0x03
 *   icon 0x03 (stroke) → display 0x00 (black)  ✓
 *   icon 0x00 (bg)     → display 0x03 (white)  → treated as transparent
 */
static void draw_icon(uint8_t *buf, const sICON *icon, int dst_x, int dst_y)
{
    int stride = ((int)icon->Width + 3) / 4;
    for (int row = 0; row < (int)icon->Height; row++) {
        for (int col = 0; col < (int)icon->Width; col++) {
            int byte_idx   = row * stride + col / 4;
            int shift      = 6 - (col % 4) * 2;
            int gv         = (icon->table[byte_idx] >> shift) & 0x03;
            int display_gv = (~gv) & 0x03;
            if (display_gv == GRAY4) continue;   /* transparent */
            set_pixel(buf, dst_x + col, dst_y + row, (uint8_t)display_gv);
        }
    }
}

/* ==========================================================================
 * Text helpers
 * ======================================================================= */

/* Draw string left-aligned at (x, baseline_y) in black. */
static int draw_str(uint8_t *buf, int x, int baseline_y,
                    const char *str, const InterFont *font)
{
    return inter_draw_string_4gray(buf, DISP_W, DISP_H,
                                   x, baseline_y, GRAY1, str, font);
}

/* Draw string centred horizontally around center_x. */
static void draw_str_c(uint8_t *buf, int center_x, int baseline_y,
                        const char *str, const InterFont *font)
{
    int w = inter_measure_string(font, str);
    draw_str(buf, center_x - w / 2, baseline_y, str, font);
}

/* Draw string right-aligned: rightmost pixel at x_right. */
static void draw_str_r(uint8_t *buf, int x_right, int baseline_y,
                        const char *str, const InterFont *font)
{
    int w = inter_measure_string(font, str);
    draw_str(buf, x_right - w, baseline_y, str, font);
}

/* ==========================================================================
 * Coordinate helpers
 * ======================================================================= */

/* Map tide time (minutes since midnight) to display pixel x. */
static int t_to_x(float t)
{
    float x = (t - T_START) * (float)(DISP_W - 1) / (float)(T_END - T_START);
    if (x < 0)           x = 0;
    if (x > DISP_W - 1)  x = DISP_W - 1;
    return (int)(x + 0.5f);
}

/* Map tide height (feet) to display pixel y (higher water = smaller y). */
static int h_to_y(float h)
{
    float y = CURVE_BOT_Y - (h - H_MIN)
                * (float)(CURVE_BOT_Y - CURVE_TOP_Y) / (float)(H_MAX - H_MIN);
    if (y < CURVE_TOP_Y)  y = (float)CURVE_TOP_Y;
    if (y > CURVE_BOT_Y)  y = (float)CURVE_BOT_Y;
    return (int)(y + 0.5f);
}

/* ==========================================================================
 * Natural cubic spline
 * ======================================================================= */

#define MAX_PTS  10

typedef struct { float a, b, c, d, t0; } SplineSeg;

/*
 * compute_spline — natural cubic spline through n control points.
 * pts_t[], pts_h[] — monotonically increasing t values and heights.
 * segs[]           — output array of n-1 segments.
 */
static void compute_spline(const float *pts_t, const float *pts_h, int n,
                            SplineSeg *segs)
{
    int n1 = n - 1;   /* number of segments / intervals */

    float dt[MAX_PTS], dh[MAX_PTS];
    float alpha[MAX_PTS];
    float l[MAX_PTS], mu[MAX_PTS], z[MAX_PTS];
    float c[MAX_PTS], b[MAX_PTS], d[MAX_PTS];

    for (int i = 0; i < n1; i++) {
        dt[i] = pts_t[i+1] - pts_t[i];
        dh[i] = pts_h[i+1] - pts_h[i];
    }

    /* alpha[i] = RHS of the tridiagonal system (i = 1..n1-1) */
    for (int i = 1; i < n1; i++)
        alpha[i] = 3.0f * (dh[i] / dt[i] - dh[i-1] / dt[i-1]);

    /* Forward sweep — Thomas algorithm */
    l[0] = 1.0f; mu[0] = 0.0f; z[0] = 0.0f;
    for (int i = 1; i < n1; i++) {
        l[i]  = 2.0f * (pts_t[i+1] - pts_t[i-1]) - dt[i-1] * mu[i-1];
        mu[i] = dt[i] / l[i];
        z[i]  = (alpha[i] - dt[i-1] * z[i-1]) / l[i];
    }
    l[n1] = 1.0f; z[n1] = 0.0f; c[n1] = 0.0f;

    /* Back substitution */
    for (int j = n1 - 1; j >= 0; j--) {
        c[j] = z[j] - mu[j] * c[j+1];
        b[j] = dh[j] / dt[j] - dt[j] * (c[j+1] + 2.0f * c[j]) / 3.0f;
        d[j] = (c[j+1] - c[j]) / (3.0f * dt[j]);

        segs[j].a  = pts_h[j];
        segs[j].b  = b[j];
        segs[j].c  = c[j];
        segs[j].d  = d[j];
        segs[j].t0 = pts_t[j];
    }
}

/*
 * eval_spline — evaluate the spline at time t.
 * n_segs = n - 1 (number of segments).
 */
static float eval_spline(const SplineSeg *segs, int n_segs, float t)
{
    /* Clamp to valid range */
    if (t <= segs[0].t0)
        t = segs[0].t0;
    if (t >= segs[n_segs - 1].t0 + (segs[n_segs-1].t0 - segs[n_segs-2].t0))
        t = segs[n_segs - 1].t0;  /* extrapolation guard */

    /* Find segment: largest i where segs[i].t0 <= t */
    int i = 0;
    for (int k = 1; k < n_segs; k++) {
        if (segs[k].t0 <= t) i = k;
        else break;
    }
    float dt = t - segs[i].t0;
    return segs[i].a + dt * (segs[i].b + dt * (segs[i].c + dt * segs[i].d));
}

/* ==========================================================================
 * Hardcoded tide data
 *
 * Five control points spanning midnight to midnight (full day) so the spline
 * has natural boundary conditions at both ends.  The graph shows only the
 * T_START..T_END window (5 AM – 9 PM), so the curve enters and exits smoothly.
 *
 * Labelled peaks/troughs visible in the graph window:
 *   L at 6:00 AM (t=360), h ≈ -0.2 ft
 *   H at 1:00 PM (t=780), h ≈ 3.7 ft
 *   L at 6:00 PM (t=1080), h ≈ 1.1 ft
 * ======================================================================= */

#define N_TIDE_PTS  5

static const float tide_t[N_TIDE_PTS] = {    0,  360,  780, 1080, 1440 };
static const float tide_h[N_TIDE_PTS] = { 1.8f, -0.2f, 3.7f, 1.1f, 4.3f };

/* Peaks/troughs to annotate on the graph (within T_START..T_END) */
typedef struct {
    float t;      /* time in minutes */
    float h;      /* height in feet  */
    char  type;   /* 'H' or 'L'      */
    const char *label;    /* e.g. "H 3'8\"" */
    const char *time_str; /* e.g. "1:00"   */
} TideLabel;

static const TideLabel tide_labels[] = {
    { 360,  -0.2f, 'L', "L -0'2\"", "6:00" },
    { 780,   3.7f, 'H', "H 3'8\"",  "1:00" },
    { 1080,  1.1f, 'L', "L 1'1\"",  "6:00" },
};
#define N_LABELS  3

/* ==========================================================================
 * Grayscale legend
 *
 * Four filled boxes — one per gray level — so you can confirm all four shades
 * are rendering correctly on the display before judging font/curve quality.
 *
 * Placement: upper-left corner of the graph area, just inside the daylight
 * region (after the left night fill ends at ~x=75, before the current-hour
 * bar at ~x=267).  Well above the tide curve in that x range.
 *
 *   x = 84 .. 263   y = 44 .. 83   (40 × 40 px each, 8 px gap)
 *
 * Each box has a 1 px black border (so the white box is still visible against
 * the white background) and a short label centred below it.
 * ======================================================================= */

static void render_grayscale_legend(uint8_t *buf)
{
    static const uint8_t levels[4]         = { GRAY1,   GRAY2,  GRAY3,   GRAY4   };
    static const char *const labels[4]     = { "BLACK", "DARK", "LIGHT", "WHITE" };

    const int box_w  = 40;
    const int box_h  = 40;
    const int gap    = 8;
    const int orig_x = 84;   /* left edge of first box */
    const int orig_y = 44;   /* top edge of all boxes  */

    for (int i = 0; i < 4; i++) {
        int bx = orig_x + i * (box_w + gap);
        int by = orig_y;

        /* Filled box */
        fill_rect(buf, bx, by, bx + box_w - 1, by + box_h - 1, levels[i]);

        /* 1 px black border on all four sides */
        draw_line(buf, bx - 1,      by - 1,      bx + box_w,  by - 1,      GRAY1); /* top    */
        draw_line(buf, bx - 1,      by + box_h,  bx + box_w,  by + box_h,  GRAY1); /* bottom */
        draw_line(buf, bx - 1,      by - 1,      bx - 1,      by + box_h,  GRAY1); /* left   */
        draw_line(buf, bx + box_w,  by - 1,      bx + box_w,  by + box_h,  GRAY1); /* right  */

        /* Label centred below the box */
        int lbl_y = by + box_h + 4 + inter_b_14.ascent;
        draw_str_c(buf, bx + box_w / 2, lbl_y, labels[i], &inter_b_14);
    }
}

/* ==========================================================================
 * Rendering
 * ======================================================================= */

static void render(uint8_t *buf)
{
    /* --- White background: 0xFF per byte = four 0x03 pixels = white ----- */
    memset(buf, 0xFF, DISP_BUFSIZE);

    /* ------------------------------------------------------------------
     * 1.  STATUS BAR  (y = 0 .. STATUS_H-1)
     * ---------------------------------------------------------------- */
    {
        /* Time + date, left-aligned with a small margin */
        int baseline = STATUS_H - 7;   /* 28 px fonts fit in 35 px row */
        int x = draw_str(buf, 14, baseline, "9:35 AM", &inter_b_14);
        x += 16;
        x = draw_str(buf, x, baseline, "SAT MAY 22 2026", &inter_b_14);

        /* Weather icons + values, right-aligned in the remaining space.
         * Draw from right edge inward: water temp → wind → rain. */
        int iy = (STATUS_H - (int)icon_watertemp.Height) / 2;
        int xr = DISP_W - 14;

        /* Water temp */
        int tw = inter_measure_string(&inter_b_14, "58.8F");
        xr -= tw;
        draw_str(buf, xr, baseline, "58.8F", &inter_b_14);
        xr -= (int)icon_watertemp.Width + 4;
        draw_icon(buf, &icon_watertemp, xr, iy);
        xr -= 24;

        /* Wind */
        tw = inter_measure_string(&inter_b_14, "S 3MPH");
        xr -= tw;
        draw_str(buf, xr, baseline, "S 3MPH", &inter_b_14);
        xr -= (int)icon_wind.Width + 4;
        draw_icon(buf, &icon_wind, xr, iy);
        xr -= 24;

        /* Rain (hours since last rain) */
        tw = inter_measure_string(&inter_b_14, "56 HRS");
        xr -= tw;
        draw_str(buf, xr, baseline, "56 HRS", &inter_b_14);
        xr -= (int)icon_rain.Width + 4;
        draw_icon(buf, &icon_rain, xr, iy);

        /* Sunrise icon — the one icon explicitly called out in Phase 3.5 spec */
        draw_icon(buf, &icon_sunrise, xr - (int)icon_sunrise.Width - 24,
                  (STATUS_H - (int)icon_sunrise.Height) / 2);
    }

    /* Divider: status bar → graph */
    draw_hline_full(buf, DIVIDER_Y1, GRAY1);

    /* ------------------------------------------------------------------
     * 2.  GRAPH AREA  (y = GRAPH_TOP .. GRAPH_BOT)
     * ---------------------------------------------------------------- */

    /* --- 2a. Compute sunrise/sunset/current pixel positions ----------- */
    int x_sunrise = t_to_x((float)SUNRISE_T);
    int x_sunset  = t_to_x((float)SUNSET_T);
    int x_current = t_to_x((float)CUR_T);

    /* --- 2b. Nighttime fill (left + right of sunrise/sunset) ---------- */
    fill_rect(buf, 0,          GRAPH_TOP, x_sunrise,  GRAPH_BOT, GRAY2);
    fill_rect(buf, x_sunset,   GRAPH_TOP, DISP_W - 1, GRAPH_BOT, GRAY2);

    /* --- 2c. Current-hour bar ---------------------------------------- */
    /* 16 px wide bar; if it sits in the night region, still draw it black
     * so it stands out against the dark fill. */
    int bar_x0 = x_current - 8;
    int bar_x1 = x_current + 7;
    fill_rect(buf, bar_x0, GRAPH_TOP, bar_x1, GRAPH_BOT, GRAY1);

    /* Hour label below the bar — use light text colour to be visible on black.
     * Position: just below GRAPH_BOT on the white area is inside the bar,
     * so we place the label over the bar itself at its lower end. */
    {
        int label_y = GRAPH_BOT - 4;  /* baseline near bottom of bar */
        /* In GRAY4 (white) so it shows on the black bar */
        inter_draw_string_4gray(buf, DISP_W, DISP_H,
                                x_current - inter_measure_string(&inter_b_14, "9 AM") / 2,
                                label_y, GRAY4, "9 AM", &inter_b_14);
    }

    /* --- 2d. Tide curve (natural cubic spline) ------------------------ */
    {
        SplineSeg segs[N_TIDE_PTS - 1];
        compute_spline(tide_t, tide_h, N_TIDE_PTS, segs);

        int prev_x = -1, prev_y = -1;
        for (int px = 0; px < DISP_W; px++) {
            float t_val = T_START + (float)px * (float)(T_END - T_START)
                          / (float)(DISP_W - 1);
            float h_val = eval_spline(segs, N_TIDE_PTS - 1, t_val);
            int   cur_y = h_to_y(h_val);

            if (prev_x >= 0)
                draw_line2(buf, prev_x, prev_y, px, cur_y, GRAY1);

            prev_x = px;
            prev_y = cur_y;
        }
    }

    /* --- 2e. Peak/trough dots and labels ------------------------------ */
    for (int i = 0; i < N_LABELS; i++) {
        const TideLabel *tl = &tide_labels[i];
        int dot_x = t_to_x(tl->t);
        int dot_y = h_to_y(tl->h);

        /* 6 px radius filled circle */
        draw_circle(buf, dot_x, dot_y, 6, GRAY1);

        /* Label position: above curve for H, below for L */
        int lbl_y, time_y;
        if (tl->type == 'H') {
            lbl_y  = dot_y - 22;   /* height string above dot */
            time_y = dot_y - 8;    /* time string just above dot */
        } else {
            lbl_y  = dot_y + 8 + inter_b_14.ascent;  /* below dot */
            time_y = dot_y + 8 + inter_b_14.line_height + 2 + inter_b_14.ascent;
        }

        /* Clamp labels inside the graph area */
        if (lbl_y < CURVE_TOP_Y + inter_b_14.ascent)
            lbl_y = CURVE_TOP_Y + inter_b_14.ascent;

        draw_str_c(buf, dot_x, lbl_y,  tl->label,    &inter_b_14);
        draw_str_c(buf, dot_x, time_y, tl->time_str, &inter_b_14);
    }

    /* ------------------------------------------------------------------
     * 3.  DIVIDERS
     * ---------------------------------------------------------------- */

    /* Graph → bottom panels */
    draw_hline_full(buf, DIVIDER_Y2, GRAY1);

    /* Vertical panel dividers */
    draw_vline_full(buf,   PANEL_W,       GRAY1);
    draw_vline_full(buf, 2*PANEL_W,       GRAY1);
    draw_vline_full(buf, 3*PANEL_W,       GRAY1);

    /* ------------------------------------------------------------------
     * 4.  BOTTOM PANELS
     *
     * Panel geometry:
     *   Panel 1: x = 0   ..  239  — Next Tide
     *   Panel 2: x = 240 ..  479  — Tide Cycle
     *   Panel 3: x = 480 ..  719  — Sunrise / Sunset
     *   Panel 4: x = 720 ..  959  — Moon Phase
     *
     * Each panel is 239 px tall (441..679).  All text centered within
     * the panel's column.
     * ---------------------------------------------------------------- */

    int panel_h  = PANEL_BOT - PANEL_TOP + 1;   /* 239 px */
    int mid_y    = PANEL_TOP + panel_h / 2;      /* ≈ 560 */

    /* ---- Panel 1 — Next Tide ---------------------------------------- */
    {
        int cx = PANEL_W / 2;   /* 120 */

        /* Large tide height — the primary fidelity target for 48px Light.
         * "1 FT 3 IN" fits on one line (~178px measured, panel is 240px). */
        int base48 = PANEL_TOP + 20 + inter_lt_48.ascent;
        draw_str_c(buf, cx, base48, "1 FT 3 IN", &inter_lt_48);

        /* Rising-tide arrows (▲▲) — two small triangles side by side */
        {
            int arrow_y = base48 + inter_lt_48.line_height - inter_lt_48.ascent + 10;
            int arrow_h = 12;
            draw_arrow_up(buf, cx - 8, arrow_y, arrow_h, GRAY1);
            draw_arrow_up(buf, cx + 8, arrow_y, arrow_h, GRAY1);
        }

        /* Time string */
        int time_y = base48 + inter_lt_48.line_height - inter_lt_48.ascent
                     + 30 + inter_b_14.ascent;
        draw_str_c(buf, cx, time_y, "1:17 PM", &inter_b_14);

        /* Label */
        int lbl_y = time_y + inter_b_14.line_height - inter_b_14.ascent
                    + 4 + inter_b_14.ascent;
        draw_str_c(buf, cx, lbl_y, "HIGH TIDE", &inter_b_14);
    }

    /* ---- Panel 2 — Tide Cycle --------------------------------------- */
    {
        int cx = PANEL_W + PANEL_W / 2;   /* 360 */

        /* Spring tide icon centred near top of panel */
        int icon_top = PANEL_TOP + 20;
        int icon_x   = cx - (int)icon_tide_spring.Width  / 2;
        draw_icon(buf, &icon_tide_spring, icon_x, icon_top);

        /* Large cycle letter "S" in inter_lt_48 — the spring tide size indicator */
        int letter_y = icon_top + (int)icon_tide_spring.Height + 10
                       + inter_lt_48.ascent;
        draw_str_c(buf, cx, letter_y, "S", &inter_lt_48);

        /* "SPRING TIDE" label below letter */
        int lbl_y = letter_y + inter_lt_48.line_height - inter_lt_48.ascent
                    + 6 + inter_b_14.ascent;
        draw_str_c(buf, cx, lbl_y, "SPRING TIDE", &inter_b_14);
    }

    /* ---- Panel 3 — Sunrise / Sunset -------------------------------- */
    {
        int cx = 2 * PANEL_W + PANEL_W / 2;   /* 600 */

        /* Layout: sunrise row, gap, sunset row, centred as a group */
        int row_h = (int)icon_sunrise.Height + 6 + inter_b_14.ascent;
        int gap   = 16;
        int total_h = row_h * 2 + gap;
        int top_y   = mid_y - total_h / 2;

        /* Sunrise row */
        {
            int icon_x  = cx - (int)icon_sunrise.Width - 8;
            int icon_y  = top_y;
            int text_x  = cx + 8;
            int base    = top_y + (int)icon_sunrise.Height;  /* baseline ≈ bottom of icon */

            draw_icon(buf, &icon_sunrise, icon_x, icon_y);
            draw_str(buf, text_x, base, "6:15 AM", &inter_b_14);

            int sub_y = base + 4 + inter_b_14.ascent;
            draw_str_c(buf, cx, sub_y, "SUNRISE", &inter_b_14);
        }

        /* Sunset row — reuse icon_sunrise for visual fidelity (sunset icon
         * is also available via icon_sunset.h; the fidelity test just
         * needs to see a 28×28 icon at both positions). */
        {
            int sy      = top_y + row_h + gap;
            int icon_x  = cx - (int)icon_sunrise.Width - 8;
            int icon_y  = sy;
            int text_x  = cx + 8;
            int base    = sy + (int)icon_sunrise.Height;

            /* Include icon_sunset.h at top of file if you want the correct
             * icon here — for now, icon_sunrise is a size stand-in.       */
            draw_icon(buf, &icon_sunrise, icon_x, icon_y);
            draw_str(buf, text_x, base, "8:04 PM", &inter_b_14);

            int sub_y = base + 4 + inter_b_14.ascent;
            draw_str_c(buf, cx, sub_y, "SUNSET", &inter_b_14);
        }
    }

    /* ---- Panel 4 — Moon Phase -------------------------------------- */
    {
        int cx = 3 * PANEL_W + PANEL_W / 2;   /* 840 */

        /* Moon icon (80×80) — centred near top of panel */
        int icon_top = PANEL_TOP + 16;
        int icon_x   = cx - (int)icon_moon_new.Width  / 2;
        draw_icon(buf, &icon_moon_new, icon_x, icon_top);

        /* Moon age "0.0" and phase name — both in inter_b_14 */
        int age_y = icon_top + (int)icon_moon_new.Height + 12
                    + inter_b_14.ascent;
        draw_str_c(buf, cx, age_y, "0.0", &inter_b_14);

        int lbl_y = age_y + inter_b_14.line_height - inter_b_14.ascent
                    + 4 + inter_b_14.ascent;
        draw_str_c(buf, cx, lbl_y, "NEW", &inter_b_14);
    }

    /* ------------------------------------------------------------------
     * 5.  GRAYSCALE LEGEND
     * ---------------------------------------------------------------- */
    render_grayscale_legend(buf);
}

/* ==========================================================================
 * main
 * ======================================================================= */

int main(void)
{
    signal(SIGINT,  handler);
    signal(SIGTERM, handler);

    printf("fidelity_test: initialising display...\n");

    if (DEV_Module_Init() != 0) {
        fprintf(stderr, "ERROR: DEV_Module_Init() failed\n");
        return 1;
    }

    /* Standard init + clear (required before switching to 4-gray mode) */
    EPD_13IN3K_Init();
    EPD_13IN3K_Clear();
    DEV_Delay_ms(500);

    /* Switch to 4-gray mode */
    EPD_13IN3K_Init_4GRAY();

    /* Allocate 4-gray frame buffer */
    uint8_t *buf = (uint8_t *)malloc(DISP_BUFSIZE);
    if (!buf) {
        fprintf(stderr, "ERROR: out of memory\n");
        DEV_Module_Exit();
        return 1;
    }

    printf("Rendering frame...\n");
    render(buf);

    printf("Pushing to display...\n");
    EPD_13IN3K_4GrayDisplay(buf);

    printf("Done.  Display showing fidelity test frame.\n"
           "Photograph it and compare against the Figma design.\n"
           "Press Ctrl+C to sleep the display and exit.\n");

    /* Hold until Ctrl+C */
    while (!g_exit)
        DEV_Delay_ms(200);

    /* Re-init to 1-bit mode before sleeping (matches Waveshare demo sequence) */
    printf("\nSleeping display...\n");
    EPD_13IN3K_Init();
    EPD_13IN3K_Clear();
    EPD_13IN3K_Sleep();

    free(buf);
    DEV_Module_Exit();
    return 0;
}

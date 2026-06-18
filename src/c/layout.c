/*
 * layout.c — Drawing primitives and high-level render functions.
 *
 * Phases 5-8 fill in render_status_bar, render_tide_graph, and
 * render_bottom_panels.  For Phase 4 those are empty stubs.
 *
 * The low-level pixel helpers (set_pixel, draw_line, fill_rect, etc.)
 * are copied verbatim from the patterns proven in fidelity_test.c.
 */

#include "layout.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>   /* roundf */

/* Phase 5: status bar */
#include "fonts/inter_b_14.h"
#include "icons/icon_rain.h"
#include "icons/icon_wind.h"
#include "icons/icon_watertemp.h"

/* Phase 6/7: tide graph */
#include "tide_curve.h"

/* Phase 8: bottom panels */
#include "fonts/inter_lt_40.h"
#include "icons/icon_high_tide.h"
#include "icons/icon_low_tide.h"
#include "icons/icon_tide_spring.h"
#include "icons/icon_tide_neap.h"
#include "icons/icon_tide_half.h"
#include "icons/icon_sunrise.h"
#include "icons/icon_sunset.h"
#include "icons/icon_moon_new.h"
#include "icons/icon_moon_full.h"
#include "icons/icon_moon_waxing_crescent.h"
#include "icons/icon_moon_waxing_gibbous.h"
#include "icons/icon_moon_first_quarter.h"
#include "icons/icon_moon_waning_gibbous.h"
#include "icons/icon_moon_last_quarter.h"
#include "icons/icon_moon_waning_crescent.h"

/* ==========================================================================
 * Low-level drawing primitives
 * ======================================================================= */

void set_pixel(uint8_t *buf, int x, int y, uint8_t gv)
{
    if (x < 0 || x >= DISP_W || y < 0 || y >= DISP_H) return;
    int addr  = y * DISP_STRIDE + x / 4;
    int shift = 6 - (x % 4) * 2;
    buf[addr] = (uint8_t)((buf[addr] & ~(0x03 << shift)) | ((gv & 0x03) << shift));
}

void draw_line(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t gv)
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

void draw_line2(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t gv)
{
    draw_line(buf, x0, y0,   x1, y1,   gv);
    draw_line(buf, x0, y0+1, x1, y1+1, gv);
}

void fill_rect(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t gv)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            set_pixel(buf, x, y, gv);
}

void draw_hline_full(uint8_t *buf, int y, uint8_t gv)
{
    fill_rect(buf, 0, y, DISP_W - 1, y, gv);
}

void draw_vline_full(uint8_t *buf, int x, uint8_t gv)
{
    fill_rect(buf, x, PANEL_TOP, x, PANEL_BOT, gv);
}

void draw_circle(uint8_t *buf, int cx, int cy, int r, uint8_t gv)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                set_pixel(buf, cx + dx, cy + dy, gv);
}

void draw_arrow_up(uint8_t *buf, int cx, int top_y, int h, uint8_t gv)
{
    for (int dy = 0; dy < h; dy++) {
        for (int dx = -dy; dx <= dy; dx++)
            set_pixel(buf, cx + dx, top_y + dy, gv);
    }
}

void draw_arrow_down(uint8_t *buf, int cx, int bot_y, int h, uint8_t gv)
{
    for (int dy = 0; dy < h; dy++) {
        int half = (h - 1) - dy;
        for (int dx = -half; dx <= half; dx++)
            set_pixel(buf, cx + dx, bot_y - dy, gv);
    }
}

/* ==========================================================================
 * Icon blitter
 * ======================================================================= */

void draw_icon(uint8_t *buf, const sICON *icon, int dst_x, int dst_y)
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

int draw_str(uint8_t *buf, int x, int baseline_y,
             const char *str, const InterFont *font)
{
    return inter_draw_string_4gray(buf, DISP_W, DISP_H,
                                   x, baseline_y, GRAY1, str, font);
}

void draw_str_c(uint8_t *buf, int center_x, int baseline_y,
                const char *str, const InterFont *font)
{
    int w = inter_measure_string(font, str);
    draw_str(buf, center_x - w / 2, baseline_y, str, font);
}

void draw_str_r(uint8_t *buf, int x_right, int baseline_y,
                const char *str, const InterFont *font)
{
    int w = inter_measure_string(font, str);
    draw_str(buf, x_right - w, baseline_y, str, font);
}

/* ---- Tracked variants ---- */

int draw_str_t(uint8_t *buf, int x, int baseline_y,
               const char *str, const InterFont *font, int sp)
{
    return inter_draw_string_4gray_tracked(buf, DISP_W, DISP_H,
                                           x, baseline_y, GRAY1, str, font, sp);
}

void draw_str_tc(uint8_t *buf, int center_x, int baseline_y,
                 const char *str, const InterFont *font, int sp)
{
    int w = inter_measure_string_tracked(font, str, sp);
    draw_str_t(buf, center_x - w / 2, baseline_y, str, font, sp);
}

void draw_str_tr(uint8_t *buf, int x_right, int baseline_y,
                 const char *str, const InterFont *font, int sp)
{
    int w = inter_measure_string_tracked(font, str, sp);
    draw_str_t(buf, x_right - w, baseline_y, str, font, sp);
}

/* ==========================================================================
 * High-level render stubs (filled in Phases 5-8)
 * ======================================================================= */

void render_status_bar(uint8_t *buf, const ClockData *data)
{
    /*
     * Status bar: y = 0..63, full 960px width.
     * Figma: Header frame at x=24, y=24, w=912, h=16.
     * Font: inter_b_14, tracking = STATUS_TRACKING (6px).
     * Baseline: vertically centred in the 64px bar.
     *   baseline_y = STATUS_H/2 + inter_b_14.ascent/2
     */
    const InterFont *font = &inter_b_14;
    /* Centre text vertically in the 64px bar */
    int baseline_y = STATUS_H / 2 + font->ascent / 2;

    int x_left  = 24;          /* left margin (matches Figma header x=24) */
    int x_right = 24 + 912;    /* right edge of header frame = 936         */
    int gap     = 32;          /* Figma gap between weather items           */
    int icon_gap = 12;         /* Figma gap between icon and text label     */

    /* ---- Left: date only (time removed — e-paper refreshes hourly) ---- */
    draw_str_t(buf, x_left, baseline_y,
               data->current_date_str, font, STATUS_TRACKING);

    /* ---- Right: weather items, right-aligned as a group ---- */
    /*
     * Build right-to-left so each item's right edge butts against the
     * previous one.  Order (right to left): water temp, wind, rain.
     * rain is conditional — only shown when 0 <= rain_hours_since <= 72.
     */

    int show_rain = (data->rain_hours_since >= 0 &&
                     data->rain_hours_since <= 72);

    /* water temp — always shown, rightmost */
    char water_buf[24];
    /* Format as "XX.X°F" — degree sign is U+00B0 but we only have ASCII.
     * Use a simple "F" suffix since the degree glyph isn't in our charset. */
    snprintf(water_buf, sizeof(water_buf), "%.1fF", (double)data->water_temp_f);
    int w_water_text = inter_measure_string_tracked(font, water_buf, STATUS_TRACKING);

    /* wind — always shown */
    char wind_buf[24];
    snprintf(wind_buf, sizeof(wind_buf), "%s %dMPH",
             data->wind_direction, data->wind_speed_mph);
    int w_wind_text = inter_measure_string_tracked(font, wind_buf, STATUS_TRACKING);

    /* since rain — conditional */
    char rain_buf[24];
    int w_rain = 0;
    if (show_rain) {
        if (data->rain_hours_since == 0)
            snprintf(rain_buf, sizeof(rain_buf), "< 1 HR");
        else
            snprintf(rain_buf, sizeof(rain_buf), "%d HRS", data->rain_hours_since);
        int w_rain_text = inter_measure_string_tracked(font, rain_buf, STATUS_TRACKING);
        w_rain = icon_rain.Width + icon_gap + w_rain_text;
    }

    /* Lay out right-to-left */
    int rx = x_right;

    /* Water temp */
    rx -= w_water_text;
    draw_str_t(buf, rx, baseline_y, water_buf, font, STATUS_TRACKING);
    rx -= icon_gap;
    draw_icon(buf, &icon_watertemp, rx - (int)icon_watertemp.Width,
              STATUS_H / 2 - (int)icon_watertemp.Height / 2);
    rx -= (int)icon_watertemp.Width;

    /* Wind */
    rx -= gap;
    rx -= w_wind_text;
    draw_str_t(buf, rx, baseline_y, wind_buf, font, STATUS_TRACKING);
    rx -= icon_gap;
    draw_icon(buf, &icon_wind, rx - (int)icon_wind.Width,
              STATUS_H / 2 - (int)icon_wind.Height / 2);
    rx -= (int)icon_wind.Width;

    /* Rain (conditional) */
    if (show_rain) {
        rx -= gap;
        rx -= w_rain - (int)icon_rain.Width - icon_gap;  /* text width */
        draw_str_t(buf, rx, baseline_y, rain_buf, font, STATUS_TRACKING);
        rx -= icon_gap;
        draw_icon(buf, &icon_rain, rx - (int)icon_rain.Width,
                  STATUS_H / 2 - (int)icon_rain.Height / 2);
    }

    /* Divider line across full width — 2px */
    draw_hline_full(buf, DIVIDER_Y1,     GRAY1);
    draw_hline_full(buf, DIVIDER_Y1 + 1, GRAY1);
}

/* --------------------------------------------------------------------------
 * Graph coordinate helpers — used only inside render_tide_graph.
 *
 * graph_t_to_x: minutes-since-midnight → pixel column (0..DISP_W-1)
 * graph_h_to_y: tide height (feet) → pixel row (clamped to CURVE_TOP/BOT_Y)
 *
 * Fixed Y scale: y = CURVE_BOT_Y − (h − h_min) × (CURVE_BOT_Y − CURVE_TOP_Y) / h_range
 *   h_min = −2.0 ft, h_max = 6.0 ft, h_range = 8.0 ft
 *   → 28 px/ft, 0 ft anchor at y = 318
 * ---------------------------------------------------------------------- */

static int graph_t_to_x(float t, float t_start, float t_range)
{
    float x = (t - t_start) * (float)(DISP_W - 1) / t_range;
    if (x < 0.0f)                x = 0.0f;
    if (x > (float)(DISP_W - 1)) x = (float)(DISP_W - 1);
    return (int)(x + 0.5f);
}

static int graph_h_to_y(float h, float h_min, float h_range)
{
    float y = (float)CURVE_BOT_Y
              - (h - h_min) * (float)(CURVE_BOT_Y - CURVE_TOP_Y) / h_range;
    if (y < (float)CURVE_TOP_Y) y = (float)CURVE_TOP_Y;
    if (y > (float)CURVE_BOT_Y) y = (float)CURVE_BOT_Y;
    return (int)(y + 0.5f);
}

void render_tide_graph(uint8_t *buf, const ClockData *data)
{
    /* ----------------------------------------------------------------
     * 0.  Always draw the bottom divider line.
     * ------------------------------------------------------------ */
    draw_hline_full(buf, DIVIDER_Y2,     GRAY1);   /* 2px bottom divider */
    draw_hline_full(buf, DIVIDER_Y2 + 1, GRAY1);

    /* Need at least two control points to build a spline */
    if (data->n_tides < 2)
        return;

    /* ----------------------------------------------------------------
     * 1.  Time axis: column-based, floor(sunrise_hour) to floor(sunset_hour)
     *     inclusive.  Each of the num_hours columns is col_width_f px wide.
     *     t_start/t_end map to pixel 0 / pixel DISP_W-1 for the spline.
     * ------------------------------------------------------------ */
    int start_hour  = data->sunrise_hour;
    int end_hour    = data->sunset_hour;
    int num_hours   = end_hour - start_hour + 1;
    if (num_hours < 2 || start_hour < 0 || end_hour < 0 || end_hour <= start_hour) return;

    float col_width_f = (float)DISP_W / (float)num_hours;
    float t_start     = (float)(start_hour * 60);
    float t_end       = (float)((start_hour + num_hours) * 60);
    float t_range     = t_end - t_start;   /* = num_hours × 60.0 */

    /* ----------------------------------------------------------------
     * 2.  Height axis: fixed scale, independent of daily tide range.
     *       -2.0 ft → CURVE_BOT_Y (374)
     *        6.0 ft → CURVE_TOP_Y (150)
     *       28 px/ft, 0 ft anchor at y = 318
     * ------------------------------------------------------------ */
    float h_min   = -2.0f;
    float h_max   =  6.0f;
    float h_range =  8.0f;

    /* ----------------------------------------------------------------
     * 3.  Control point arrays for the spline.
     *
     *     Prepend yesterday's last tide and append tomorrow's first
     *     tide when available.  t values are minutes from today's
     *     midnight (negative for yesterday, > 1440 for tomorrow).
     * ------------------------------------------------------------ */
    float tide_t[MAX_TIDES + 2], tide_h[MAX_TIDES + 2];
    int n_pts = 0;

    if (data->has_prev_tide) {
        tide_t[n_pts] = data->prev_tide_t_min;
        tide_h[n_pts] = data->prev_tide_height_ft;
        n_pts++;
    }
    for (int i = 0; i < data->n_tides; i++) {
        tide_t[n_pts] = (float)(data->tides[i].hour * 60 + data->tides[i].minute);
        tide_h[n_pts] = data->tides[i].height_ft;
        n_pts++;
    }
    if (data->has_next_tide_after) {
        tide_t[n_pts] = data->next_tide_after_t_min;
        tide_h[n_pts] = data->next_tide_after_height_ft;
        n_pts++;
    }

    SplineSeg segs[MAX_TIDES + 1];
    compute_spline(tide_t, tide_h, n_pts, segs);
    int n_segs = n_pts - 1;

    /* ----------------------------------------------------------------
     * 4.  Current-hour bar coordinates (full column width).
     *     Clamped to valid column range so an out-of-window current
     *     hour doesn't crash (e.g. on startup before 5 AM).
     * ------------------------------------------------------------ */
    int current_col = data->current_hour - start_hour;
    if (current_col < 0)          current_col = 0;
    if (current_col >= num_hours)  current_col = num_hours - 1;

    int x_bar_left  = (int)(current_col       * col_width_f + 0.5f);
    int x_bar_right = (int)((current_col + 1) * col_width_f + 0.5f) - 1;
    int bar_cx      = (x_bar_left + x_bar_right) / 2;

    /* ----------------------------------------------------------------
     * 5.  Current-hour bar (GRAY1) — top edge follows the spline,
     *     bottom extends to GRAPH_BOT.  Drawn column by column.
     * ------------------------------------------------------------ */
    for (int px = x_bar_left; px <= x_bar_right; px++) {
        float t      = t_start + (float)px * t_range / (float)(DISP_W - 1);
        float t_eval = t;
        if (t_eval < tide_t[0])          t_eval = tide_t[0];
        if (t_eval > tide_t[n_pts - 1])  t_eval = tide_t[n_pts - 1];
        float h    = eval_spline(segs, n_segs, t_eval);
        int   y_top = graph_h_to_y(h, h_min, h_range);
        fill_rect(buf, px, y_top, px, GRAPH_BOT, GRAY1);
    }

    /* ----------------------------------------------------------------
     * 6.  Hourly dividers — 1px GRAY3 at the right edge of every column
     *     except the last.  Top of each divider meets the spline;
     *     bottom extends to GRAPH_BOT.  Drawn before the curve so the
     *     2px spline line renders on top.
     * ------------------------------------------------------------ */
    for (int col = 0; col < num_hours - 1; col++) {
        int   div_x  = (int)((col + 1) * col_width_f + 0.5f) - 1;
        float t_div  = t_start + (float)div_x * t_range / (float)(DISP_W - 1);
        float t_eval = t_div;
        if (t_eval < tide_t[0])          t_eval = tide_t[0];
        if (t_eval > tide_t[n_pts - 1])  t_eval = tide_t[n_pts - 1];
        float h_div  = eval_spline(segs, n_segs, t_eval);
        int   y_div  = graph_h_to_y(h_div, h_min, h_range);
        fill_rect(buf, div_x, y_div, div_x, GRAPH_BOT, GRAY3);
    }

    /* ----------------------------------------------------------------
     * 7.  Tide curve — 2px spline line, full display width.
     *     Boundary tides anchor the spline on both sides so the curve
     *     is accurate all the way to the edges.
     * ------------------------------------------------------------ */
    render_tide_curve(buf, tide_t, tide_h, n_pts,
                      t_start, t_end,
                      0, DISP_W - 1,
                      CURVE_TOP_Y, CURVE_BOT_Y,
                      h_min, h_max);

    /* ----------------------------------------------------------------
     * 8.  Peak/trough dots and 3-line labels.
     *
     *     Figma layout (all tide types):
     *       [line 1: "H" or "L"]
     *       [line 2: height        ]   (2px gap between lines)
     *       [line 3: time (H:MM)   ]
     *       ↕ 8px gap
     *       ● 16px dot (radius 8)
     *
     *     Text left-aligned at dot_center_x − dot_radius + 4px.
     * ------------------------------------------------------------ */
    {
        const InterFont *font = &inter_b_14;
        int line_gap    = 2;
        int dot_gap     = 8;
        int dot_radius  = 8;
        int line_height = font->ascent + line_gap;

        for (int i = 0; i < data->n_tides; i++) {
            const TidePoint *tp = &data->tides[i];
            float t_dot = (float)(tp->hour * 60 + tp->minute);

            /* Only annotate tides within the graph window */
            if (t_dot < t_start || t_dot > t_end)
                continue;

            int dot_x = graph_t_to_x(t_dot, t_start, t_range);
            int dot_y = graph_h_to_y(tp->height_ft, h_min, h_range);

            /* Line 1: type character */
            char type_str[4];
            type_str[0] = tp->type;
            type_str[1] = '\0';

            /* Line 2: height in feet+inches.
             * Positive:        "{ft}'{in}\""    e.g. "3'5\""
             * Negative, ft=0:  "-{in}\""        e.g. "-4\""
             * Negative, ft>0:  "-{ft}'{in}\""   e.g. "-1'2\""  */
            char height_str[32];
            float h_abs = fabsf(tp->height_ft);
            int   ft    = (int)h_abs;
            int   in_v  = (int)((h_abs - (float)ft) * 12.0f + 0.5f);
            if (in_v >= 12) { ft++; in_v = 0; }

            if (tp->height_ft < 0.0f) {
                if (ft == 0)
                    snprintf(height_str, sizeof(height_str), "-%d\"", in_v);
                else
                    snprintf(height_str, sizeof(height_str), "-%d'%d\"", ft, in_v);
            } else {
                snprintf(height_str, sizeof(height_str), "%d'%d\"", ft, in_v);
            }

            /* Line 3: H:MM (no AM/PM) */
            char time_str[8];
            int h12 = tp->hour % 12;
            if (h12 == 0) h12 = 12;
            snprintf(time_str, sizeof(time_str), "%d:%02d", h12, tp->minute);

            /* Label block sits above the dot.
             * line3 baseline = dot top − dot_gap
             * line2 baseline = line3 − line_height
             * line1 baseline = line2 − line_height
             * Clamp so line1 stays inside the graph area.          */
            int line3_baseline = dot_y - dot_radius - dot_gap;
            int line2_baseline = line3_baseline - line_height;
            int line1_baseline = line2_baseline - line_height;

            int min_baseline = GRAPH_TOP + font->ascent + 2;
            if (line1_baseline < min_baseline) {
                int shift = min_baseline - line1_baseline;
                line1_baseline += shift;
                line2_baseline += shift;
                line3_baseline += shift;
            }

            int lbl_x = dot_x - dot_radius + 4;

            draw_str_t(buf, lbl_x, line1_baseline, type_str,   font, STATUS_TRACKING);
            draw_str_t(buf, lbl_x, line2_baseline, height_str, font, STATUS_TRACKING);
            draw_str_t(buf, lbl_x, line3_baseline, time_str,   font, STATUS_TRACKING);

            draw_circle(buf, dot_x, dot_y, dot_radius, GRAY1);
        }
    }

    /* ----------------------------------------------------------------
     * 9.  Hour label in current-hour bar — white text, centered.
     *     inter_b_14, STATUS_TRACKING, GRAY4, baseline at y = 404.
     * ------------------------------------------------------------ */
    {
        int h12 = data->current_hour % 12;
        if (h12 == 0) h12 = 12;
        const char *ampm = (data->current_hour >= 12) ? "PM" : "AM";
        char hour_label[16];
        snprintf(hour_label, sizeof(hour_label), "%d%s", h12, ampm);

        int label_baseline = DIVIDER_Y2 - 16;   /* y = 404 per Figma */
        int lbl_w = inter_measure_string_tracked(&inter_b_14, hour_label,
                                                  STATUS_TRACKING);
        int lbl_x = bar_cx - lbl_w / 2;

        /* draw_str_t is hardwired to GRAY1; call the underlying function
         * directly to render in GRAY4 (white) over the black bar.       */
        inter_draw_string_4gray_tracked(buf, DISP_W, DISP_H,
                                        lbl_x, label_baseline, GRAY4,
                                        hour_label, &inter_b_14,
                                        STATUS_TRACKING);
    }
}

void render_bottom_panels(uint8_t *buf, const ClockData *data)
{
    /*
     * Four 240×260 tiles, left to right:
     *   [0,239]   Tile 1 — Next Tide
     *   [240,479] Tile 2 — Tide Cycle
     *   [480,719] Tile 3 — Sunrise & Sunset
     *   [720,959] Tile 4 — Moon Phase
     *
     * Top border (DIVIDER_Y2) already drawn by render_tide_graph.
     * Vertical dividers drawn here at x=240, 480, 720.
     *
     * Font metrics used below:
     *   inter_lt_40: ascent=39, line_height=49  (descent = 10)
     *   inter_b_14:  ascent=14, line_height=18  (descent =  4)
     */

    const InterFont *f40 = &inter_lt_40;
    const InterFont *f14 = &inter_b_14;

    /* Tile center x-coordinates */
    const int cx1 = 120;
    const int cx2 = 360;
    const int cx3 = 600;
    const int cx4 = 840;

    /* Vertical tile dividers — 2px each */
    draw_vline_full(buf, 240, GRAY1);   draw_vline_full(buf, 241, GRAY1);
    draw_vline_full(buf, 480, GRAY1);   draw_vline_full(buf, 481, GRAY1);
    draw_vline_full(buf, 720, GRAY1);   draw_vline_full(buf, 721, GRAY1);

    /* =========================================================
     * Tile 1 — Next Tide
     *
     * Figma y offsets from PANEL_TOP (420):
     *   46  — height row  "[X] FT  [Y] IN"  (or "[Y] IN" if feet=0)
     *   123 — tide direction icon (42×18 or 43×18)
     *   150 — next tide time string
     *   150+descent+8 — "HIGH TIDE" / "LOW TIDE" sub-label
     * ========================================================= */
    {
        int whole = data->next_tide_height_whole_ft;
        int rem   = data->next_tide_height_rem_in;
        char type = data->next_tide_type;   /* 'H' or 'L' */

        /* ---- Height row ---- */
        /* Numbers (f40) and unit labels (f14) use separate baselines per QA. */
        int num_baseline  = PANEL_TOP + 46 + f40->ascent;  /* big numbers:  2px up from previous */
        int unit_baseline = PANEL_TOP + 36 + f40->ascent;  /* FT/IN labels: 12px up from previous */

        if (whole != 0) {
            char num1_buf[16], num2_buf[16];
            snprintf(num1_buf, sizeof(num1_buf), "%d", abs(whole));
            snprintf(num2_buf, sizeof(num2_buf), "%d", rem);

            int w_neg  = (whole < 0) ? inter_measure_string(f40, "-") : 0;
            int w_num1 = inter_measure_string(f40, num1_buf);
            int w_ft   = inter_measure_string_tracked(f14, "FT", STATUS_TRACKING);
            int w_num2 = inter_measure_string(f40, num2_buf);
            int w_in   = inter_measure_string_tracked(f14, "IN", STATUS_TRACKING);

            /* Figma gaps: 11px (number→FT), 16px (FT-group→IN-group), 8px (number→IN) */
            int total_w = w_neg + w_num1 + 11 + w_ft + 16 + w_num2 + 8 + w_in;
            int x = cx1 - total_w / 2;

            if (whole < 0) {
                draw_str(buf, x, num_baseline, "-", f40);
                x += w_neg;
            }
            draw_str(buf, x, num_baseline, num1_buf, f40);   x += w_num1 + 11;
            draw_str_t(buf, x, unit_baseline, "FT", f14, STATUS_TRACKING);
            x += w_ft + 16;
            draw_str(buf, x, num_baseline, num2_buf, f40);   x += w_num2 + 8;
            draw_str_t(buf, x, unit_baseline, "IN", f14, STATUS_TRACKING);
        } else {
            /* feet=0: show "0 FT [Y] IN" (or "-0 FT [Y] IN" if negative) */
            int is_neg = data->next_tide_height_negative;
            char num2_buf[16];
            snprintf(num2_buf, sizeof(num2_buf), "%d", rem);

            int w_neg  = is_neg ? inter_measure_string(f40, "-") : 0;
            int w_num1 = inter_measure_string(f40, "0");
            int w_ft   = inter_measure_string_tracked(f14, "FT", STATUS_TRACKING);
            int w_num2 = inter_measure_string(f40, num2_buf);
            int w_in   = inter_measure_string_tracked(f14, "IN", STATUS_TRACKING);

            int total_w = w_neg + w_num1 + 11 + w_ft + 16 + w_num2 + 8 + w_in;
            int x = cx1 - total_w / 2;

            if (is_neg) {
                draw_str(buf, x, num_baseline, "-", f40);
                x += w_neg;
            }
            draw_str(buf, x, num_baseline, "0", f40);        x += w_num1 + 11;
            draw_str_t(buf, x, unit_baseline, "FT", f14, STATUS_TRACKING);
            x += w_ft + 16;
            draw_str(buf, x, num_baseline, num2_buf, f40);   x += w_num2 + 8;
            draw_str_t(buf, x, unit_baseline, "IN", f14, STATUS_TRACKING);
        }

        /* ---- Tide direction icon ---- */
        int icon_y = PANEL_TOP + 112;   /* 3px up per QA */
        if (type == 'H') {
            draw_icon(buf, &icon_high_tide, cx1 - (int)icon_high_tide.Width / 2, icon_y);
        } else {
            draw_icon(buf, &icon_low_tide,  cx1 - (int)icon_low_tide.Width  / 2, icon_y);
        }

        /* ---- Time string ---- */
        int time_baseline = PANEL_TOP + 147 + f40->ascent;  /* 3px up */
        draw_str_c(buf, cx1, time_baseline, data->next_tide_time_str, f40);

        /* ---- Sub-label ("HIGH TIDE" / "LOW TIDE") ---- */
        int lbl_baseline = time_baseline + (f40->line_height - f40->ascent) + 2 + f14->ascent;  /* gap 8→2, net −9px */
        const char *tide_lbl = (type == 'H') ? "HIGH TIDE" : "LOW TIDE";
        draw_str_tc(buf, cx1, lbl_baseline, tide_lbl, f14, STATUS_TRACKING);
    }

    /* =========================================================
     * Tile 2 — Tide Cycle
     *
     * tide_cycle maps to:   SPRING→L, HALF→M, NEAP→S
     *
     * Figma y offsets from PANEL_TOP:
     *   78  — tide cycle icon (~92×92)
     *   148 — cycle size letter (L / M / S)
     *   148+descent+8 — cycle name label
     * ========================================================= */
    {
        const sICON *cycle_icon;
        const char  *cycle_letter;
        const char  *cycle_label;
        int          cycle_icon_y;

        if (strncmp(data->tide_cycle, "NEAP", 4) == 0) {
            cycle_icon   = &icon_tide_neap;
            cycle_letter = "S";
            cycle_label  = "NEAP TIDE";
            cycle_icon_y = PANEL_TOP + 45;
        } else if (strncmp(data->tide_cycle, "HALF", 4) == 0) {
            cycle_icon   = &icon_tide_half;
            cycle_letter = "M";
            cycle_label  = "HALF TIDE";
            cycle_icon_y = PANEL_TOP + 29;   /* 16px up per QA */
        } else {
            /* Default: SPRING */
            cycle_icon   = &icon_tide_spring;
            cycle_letter = "L";
            cycle_label  = "SPRING TIDE";
            cycle_icon_y = PANEL_TOP + 33;   /* 12px up per QA */
        }

        draw_icon(buf, cycle_icon,
                  cx2 - (int)cycle_icon->Width / 2,
                  cycle_icon_y);

        int letter_baseline = PANEL_TOP + 148 + f40->ascent;  /* y=607 — unchanged */
        draw_str_c(buf, cx2, letter_baseline, cycle_letter, f40);

        int lbl_baseline = letter_baseline + (f40->line_height - f40->ascent) + 1 + f14->ascent;  /* gap 8→1, −7px */
        draw_str_tc(buf, cx2, lbl_baseline, cycle_label, f14, STATUS_TRACKING);
    }

    /* =========================================================
     * Tile 3 — Sunrise & Sunset
     *
     * Two groups centered vertically in the tile (32px top/bottom
     * padding, 40px gap between groups).  Each group:
     *   row:   [16×16 icon] [8px] [time string]  — all baseline-aligned
     *   label: 8px below row, centered
     * ========================================================= */
    {
        /* Figma uses leading-none (line-height = font-size) for vertical centering,
         * not the font's full line_height. Use visual heights to match exactly. */
        int row_h   = 40;               /* Inter Light 40 with leading-none    */
        int lbl_h   = f14->ascent;      /* 14 — Inter Bold 14 with leading-none */
        int group_h = row_h + 8 + lbl_h; /* 62 */
        int total_h = 2 * group_h + 40;  /* 164 */
        int avail   = PANEL_H - 64;      /* 196 */
        int y0      = PANEL_TOP + 32 + (avail - total_h) / 2 - 3;  /* −3px per QA: Y=465 */

        const char  *times[2]  = { data->sunrise_str,  data->sunset_str  };
        const char  *labels[2] = { "SUNRISE",          "SUNSET"          };
        const sICON *icons[2]  = { &icon_sunrise,      &icon_sunset      };

        for (int i = 0; i < 2; i++) {
            int y_row = y0 + i * (group_h + 40);

            /* [icon][8px][time] row centered at cx3 */
            int icon_w  = (int)icons[i]->Width;   /* 16 */
            int icon_h  = (int)icons[i]->Height;  /* 16 */
            int time_w  = inter_measure_string(f40, times[i]);
            int row_w   = icon_w + 8 + time_w;
            int row_x   = cx3 - row_w / 2;

            /* Icon: vertically centered in the 40px text row */
            draw_icon(buf, icons[i], row_x, y_row + (row_h - icon_h) / 2);

            /* Time string */
            int time_baseline = y_row + f40->ascent;
            draw_str(buf, row_x + icon_w + 8, time_baseline, times[i], f40);

            /* Label — 3px lower per QA */
            int lbl_baseline = y_row + row_h + 11 + f14->ascent;
            draw_str_tc(buf, cx3, lbl_baseline, labels[i], f14, STATUS_TRACKING);
        }
    }

    /* =========================================================
     * Tile 4 — Moon Phase
     *
     * Figma y offsets from PANEL_TOP:
     *   75  — moon phase icon (88×88)
     *   148 — moon age float ("0.0" … "29.5")
     *   148+descent+8 — phase name ("NEW", "WAXING CRESCENT", …)
     * ========================================================= */
    {
        /* Select icon from moon_phase string */
        const sICON *moon_icon = &icon_moon_new;  /* fallback */
        if      (strcmp(data->moon_phase, "FULL")            == 0) moon_icon = &icon_moon_full;
        else if (strcmp(data->moon_phase, "WAXING_CRESCENT") == 0) moon_icon = &icon_moon_waxing_crescent;
        else if (strcmp(data->moon_phase, "WAXING_GIBBOUS")  == 0) moon_icon = &icon_moon_waxing_gibbous;
        else if (strcmp(data->moon_phase, "FIRST_QUARTER")   == 0) moon_icon = &icon_moon_first_quarter;
        else if (strcmp(data->moon_phase, "WANING_GIBBOUS")  == 0) moon_icon = &icon_moon_waning_gibbous;
        else if (strcmp(data->moon_phase, "LAST_QUARTER")    == 0) moon_icon = &icon_moon_last_quarter;
        else if (strcmp(data->moon_phase, "WANING_CRESCENT") == 0) moon_icon = &icon_moon_waning_crescent;

        draw_icon(buf, moon_icon,
                  cx4 - (int)moon_icon->Width / 2,
                  PANEL_TOP + 32);   /* y=452, top of moon graphic per Figma spec */

        /* Moon age */
        char age_buf[8];
        snprintf(age_buf, sizeof(age_buf), "%.1f", (double)data->moon_age);
        int age_baseline = PANEL_TOP + 147 + f40->ascent;  /* 1px up */
        draw_str_c(buf, cx4, age_baseline, age_buf, f40);

        /* Phase label: underscores → spaces */
        char phase_label[32];
        snprintf(phase_label, sizeof(phase_label), "%s", data->moon_phase);
        for (char *p = phase_label; *p; p++)
            if (*p == '_') *p = ' ';
        int lbl_baseline = age_baseline + (f40->line_height - f40->ascent) + 2 + f14->ascent;  /* gap 8→2, −7px */
        draw_str_tc(buf, cx4, lbl_baseline, phase_label, f14, STATUS_TRACKING);
    }
}

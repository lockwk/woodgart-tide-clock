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

/* Phase 8: bottom panels (uncomment when implementing)
 * #include "fonts/inter_lt_48.h"
 * #include "fonts/inter_lt_16.h"
 * #include "fonts/inter_sb_28.h"
 * #include "fonts/inter_sb_56.h"
 * #include "fonts/inter_sb_96.h"
 */

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
     * rain is conditional — only shown when 1 <= rain_hours_since <= 72.
     */

    int show_rain = (data->rain_hours_since >= 1 &&
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

    /* Divider line across full width */
    draw_hline_full(buf, DIVIDER_Y1, GRAY1);
}

/* --------------------------------------------------------------------------
 * Graph coordinate helpers — used only inside render_tide_graph.
 *
 * graph_t_to_x: minutes-since-midnight → pixel column (0..DISP_W-1)
 * graph_h_to_y: tide height (feet) → pixel row (clamped to CURVE_TOP/BOT_Y)
 * ---------------------------------------------------------------------- */

static int graph_t_to_x(float t, float t_start, float t_range)
{
    float x = (t - t_start) * (float)(DISP_W - 1) / t_range;
    if (x < 0.0f)              x = 0.0f;
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
    draw_hline_full(buf, DIVIDER_Y2, GRAY1);

    /* Need at least two control points to build a spline */
    if (data->n_tides < 2)
        return;

    /* ----------------------------------------------------------------
     * 1.  Time axis: (sunrise – 1 hr) to (sunset + 1 hr).
     * ------------------------------------------------------------ */
    float t_start = (float)(data->sunrise_hour * 60 + data->sunrise_minute)
                    - 60.0f;
    float t_end   = (float)(data->sunset_hour  * 60 + data->sunset_minute)
                    + 60.0f;
    if (t_start < 0.0f)    t_start = 0.0f;
    if (t_end   > 1440.0f) t_end   = 1440.0f;

    float t_range = t_end - t_start;
    if (t_range < 1.0f) return;   /* degenerate: sunrise/sunset missing */

    /* ----------------------------------------------------------------
     * 2.  Height axis: range of today's tide events + 0.5 ft margins.
     * ------------------------------------------------------------ */
    float h_min = data->tides[0].height_ft;
    float h_max = data->tides[0].height_ft;
    for (int i = 1; i < data->n_tides; i++) {
        float h = data->tides[i].height_ft;
        if (h < h_min) h_min = h;
        if (h > h_max) h_max = h;
    }
    h_min -= 0.5f;
    h_max += 0.5f;
    float h_range = h_max - h_min;
    if (h_range < 0.1f) return;   /* degenerate: all tides the same height */

    /* ----------------------------------------------------------------
     * 3.  Control point arrays for the spline.
     * ------------------------------------------------------------ */
    float tide_t[MAX_TIDES], tide_h[MAX_TIDES];
    for (int i = 0; i < data->n_tides; i++) {
        tide_t[i] = (float)(data->tides[i].hour * 60 + data->tides[i].minute);
        tide_h[i] = data->tides[i].height_ft;
    }
    SplineSeg segs[MAX_TIDES - 1];
    compute_spline(tide_t, tide_h, data->n_tides, segs);
    int n_segs = data->n_tides - 1;

    /* ----------------------------------------------------------------
     * 4.  Pixel positions for sunrise, sunset, and current time.
     * ------------------------------------------------------------ */
    float t_sunrise = (float)(data->sunrise_hour * 60 + data->sunrise_minute);
    float t_sunset  = (float)(data->sunset_hour  * 60 + data->sunset_minute);
    float t_cur     = (float)(data->current_hour * 60 + data->current_minute);

    int x_sunrise = graph_t_to_x(t_sunrise, t_start, t_range);
    int x_sunset  = graph_t_to_x(t_sunset,  t_start, t_range);

    /* Bar spans ±30 min around current time (= 1 hour wide) */
    int x_bar_left  = graph_t_to_x(t_cur - 30.0f, t_start, t_range);
    int x_bar_right = graph_t_to_x(t_cur + 30.0f, t_start, t_range);
    int bar_cx      = (x_bar_left + x_bar_right) / 2;

    /* ----------------------------------------------------------------
     * 5.  Phase 6: Night fills (GRAY2) — before sunrise and after sunset.
     * ------------------------------------------------------------ */
    fill_rect(buf,  0,        GRAPH_TOP, x_sunrise,   GRAPH_BOT, GRAY3);
    fill_rect(buf,  x_sunset, GRAPH_TOP, DISP_W - 1,  GRAPH_BOT, GRAY3);

    /* ----------------------------------------------------------------
     * 6.  Phase 6: Current-hour bar (GRAY1) — top edge follows spline.
     *     Column by column: fill from the spline height down to GRAPH_BOT.
     * ------------------------------------------------------------ */
    for (int px = x_bar_left; px <= x_bar_right; px++) {
        float t      = t_start + (float)px * t_range / (float)(DISP_W - 1);
        float t_eval = t;
        if (t_eval < tide_t[0])                  t_eval = tide_t[0];
        if (t_eval > tide_t[data->n_tides - 1])  t_eval = tide_t[data->n_tides - 1];
        float h    = eval_spline(segs, n_segs, t_eval);
        int   y_top = graph_h_to_y(h, h_min, h_range);
        fill_rect(buf, px, y_top, px, GRAPH_BOT, GRAY1);
    }

    /* ----------------------------------------------------------------
     * 7.  Phase 7: Tide curve — 2px spline line on top of the bar.
     * ------------------------------------------------------------ */
    render_tide_curve(buf, tide_t, tide_h, data->n_tides,
                      t_start, t_end,
                      0, DISP_W - 1,
                      CURVE_TOP_Y, CURVE_BOT_Y,
                      h_min, h_max);

    /* ----------------------------------------------------------------
     * 8.  Phase 7: Peak/trough dots and 3-line labels.
     *
     *     Figma layout (all tide types):
     *       [line 1: "H" or "L"]   ← label text
     *       [line 2: height        ]   (2px gap between lines)
     *       [line 3: time (H:MM)   ]
     *       ↕ 8px gap
     *       ● 16px dot (radius 8)
     *
     *     Text is left-aligned starting 4px right of the dot's left edge,
     *     i.e. x = dot_center_x – 4.
     * ------------------------------------------------------------ */
    {
        const InterFont *font = &inter_b_14;
        int line_gap    = 2;                     /* px between text lines */
        int dot_gap     = 8;                     /* px between text block and dot top */
        int dot_radius  = 8;                     /* 16 px diameter per Figma */
        int line_height = font->ascent + line_gap;  /* advance per text line */

        for (int i = 0; i < data->n_tides; i++) {
            const TidePoint *tp = &data->tides[i];
            float t_dot = (float)(tp->hour * 60 + tp->minute);

            /* Only annotate tides visible in the graph window */
            if (t_dot < t_start || t_dot > t_end)
                continue;

            int dot_x = graph_t_to_x(t_dot, t_start, t_range);
            int dot_y = graph_h_to_y(tp->height_ft, h_min, h_range);

            /* ---- Build label strings ---- */

            /* Line 1: type character */
            char type_str[4];
            type_str[0] = tp->type;
            type_str[1] = '\0';

            /* Line 2: height in feet+inches.
             * Positive:  "{ft}'{in}\""     e.g. "3'5\""
             * Negative feet=0: "-{in}\""   e.g. "-4\""
             * Negative feet>0: "-{ft}'{in}\"" e.g. "-1'2\""          */
            char height_str[32];  /* 32 silences GCC truncation warning */
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

            /* Line 3: time as H:MM (no AM/PM, matching Figma style) */
            char time_str[8];
            int h12 = tp->hour % 12;
            if (h12 == 0) h12 = 12;
            snprintf(time_str, sizeof(time_str), "%d:%02d", h12, tp->minute);

            /* ---- Label placement: label block is ABOVE the dot ----
             *
             * line3 baseline is placed dot_gap above the dot's top edge.
             * dot top = dot_y - dot_radius
             * line3 baseline = dot_y - dot_radius - dot_gap
             * line2 baseline = line3 - line_height
             * line1 baseline = line2 - line_height
             *
             * Clamp so line1 stays within the graph area.          */
            int line3_baseline = dot_y - dot_radius - dot_gap;
            int line2_baseline = line3_baseline - line_height;
            int line1_baseline = line2_baseline - line_height;

            /* Clamp: push down if label would bleed above graph top */
            int min_baseline = GRAPH_TOP + font->ascent + 2;
            if (line1_baseline < min_baseline) {
                int shift = min_baseline - line1_baseline;
                line1_baseline += shift;
                line2_baseline += shift;
                line3_baseline += shift;
            }

            /* Text left-aligned at dot_left + 4px (Figma pl-[4px]) */
            int lbl_x = dot_x - dot_radius + 4;

            draw_str_t(buf, lbl_x, line1_baseline, type_str,   font, STATUS_TRACKING);
            draw_str_t(buf, lbl_x, line2_baseline, height_str, font, STATUS_TRACKING);
            draw_str_t(buf, lbl_x, line3_baseline, time_str,   font, STATUS_TRACKING);

            /* ---- Dot (filled circle, radius 8, GRAY1) ---- */
            draw_circle(buf, dot_x, dot_y, dot_radius, GRAY1);
        }
    }

    /* ----------------------------------------------------------------
     * 9.  Phase 6: Hour label in bar — white text, centered, near bottom.
     *     Figma: inter_b_14, tracking 5.6px, GRAY4, baseline at y = 404.
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
    /* Phase 8 */
    (void)buf; (void)data;
}

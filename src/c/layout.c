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

/* Phase 6/7: tide graph (uncomment when implementing)
 * #include "fonts/inter_sb_20.h"
 */

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

void render_tide_graph(uint8_t *buf, const ClockData *data)
{
    /* Phase 6 (background + hour bar) and Phase 7 (curve + labels) */
    (void)buf; (void)data;
}

void render_bottom_panels(uint8_t *buf, const ClockData *data)
{
    /* Phase 8 */
    (void)buf; (void)data;
}

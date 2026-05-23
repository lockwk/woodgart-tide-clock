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

/* ==========================================================================
 * High-level render stubs (filled in Phases 5-8)
 * ======================================================================= */

void render_status_bar(uint8_t *buf, const ClockData *data)
{
    /* Phase 5 */
    (void)buf; (void)data;
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

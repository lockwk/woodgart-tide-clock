/*
 * icon_test.c — Phase 3 icon test for the 13.3" tide clock.
 *
 * Renders all 18 icons at their native sizes into a 960×680 grayscale BMP,
 * matching the actual display resolution and 4-gray palette.  No labels —
 * just the icons with thin divider lines between groups.
 *
 * Build (from src/c/):
 *   gcc icon_test.c -I. -o icon_test
 *
 * Run:
 *   ./icon_test
 *   # outputs icon_test.bmp
 *
 * Groups (top to bottom):
 *   Row 1  — status bar icons: rain, wind, watertemp, sunrise, sunset
 *   Row 2  — moon phases (waxing): new, wax crescent, 1st quarter, wax gibbous
 *   Row 3  — moon phases (waning): full, wan gibbous, last quarter, wan crescent
 *   Row 4  — tide cycles: spring, neap, half
 *   Row 5  — tide direction: high tide, low tide
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- icon types + all icons ---- */
#include "icons/icon_types.h"

/* status bar */
#include "icons/icon_rain.h"
#include "icons/icon_wind.h"
#include "icons/icon_watertemp.h"
#include "icons/icon_sunrise.h"
#include "icons/icon_sunset.h"

/* moon phases */
#include "icons/icon_moon_new.h"
#include "icons/icon_moon_full.h"
#include "icons/icon_moon_waxing_crescent.h"
#include "icons/icon_moon_waxing_gibbous.h"
#include "icons/icon_moon_first_quarter.h"
#include "icons/icon_moon_waning_gibbous.h"
#include "icons/icon_moon_last_quarter.h"
#include "icons/icon_moon_waning_crescent.h"

/* tide cycles */
#include "icons/icon_tide_spring.h"
#include "icons/icon_tide_neap.h"
#include "icons/icon_tide_half.h"

/* tide direction */
#include "icons/icon_high_tide.h"
#include "icons/icon_low_tide.h"

/* -------------------------------------------------------------------------
 * Canvas
 * ---------------------------------------------------------------------- */

#define CANVAS_W  960
#define CANVAS_H  680

#define LUM_BLACK      0x00
#define LUM_DARK_GRAY  0x55
#define LUM_LIGHT_GRAY 0xAA
#define LUM_WHITE      0xFF

/* -------------------------------------------------------------------------
 * BMP writer (24-bit RGB, top-down)
 * ---------------------------------------------------------------------- */

static void write_u16(FILE *f, uint16_t v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}
static void write_u32(FILE *f, uint32_t v)
{
    fputc( v        & 0xFF, f);
    fputc((v >>  8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 24) & 0xFF, f);
}

static int write_bmp(const char *path, const uint8_t *pixels, int w, int h)
{
    int row_bytes  = (w * 3 + 3) & ~3;
    int pixel_size = row_bytes * h;
    int file_size  = 54 + pixel_size;

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", path); return -1; }

    fwrite("BM", 1, 2, f);
    write_u32(f, (uint32_t)file_size);
    write_u32(f, 0);
    write_u32(f, 54);
    write_u32(f, 40);
    write_u32(f, (uint32_t)w);
    write_u32(f, (uint32_t)(-h));
    write_u16(f, 1);
    write_u16(f, 24);
    write_u32(f, 0);
    write_u32(f, (uint32_t)pixel_size);
    write_u32(f, 3780);
    write_u32(f, 3780);
    write_u32(f, 0);
    write_u32(f, 0);

    uint8_t pad[3] = {0, 0, 0};
    int pad_bytes  = row_bytes - w * 3;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t v = pixels[y * w + x];
            fputc(v, f); fputc(v, f); fputc(v, f);
        }
        fwrite(pad, 1, pad_bytes, f);
    }
    fclose(f);
    return 0;
}

/* -------------------------------------------------------------------------
 * Drawing helpers
 * ---------------------------------------------------------------------- */

static void draw_hline(uint8_t *buf, int y, int x0, int x1, uint8_t lum)
{
    if (y < 0 || y >= CANVAS_H) return;
    for (int x = x0; x < x1 && x < CANVAS_W; x++)
        if (x >= 0) buf[y * CANVAS_W + x] = lum;
}

/* Unpack 2-bit icon and blit to canvas. GRAY4 (0x00) = transparent. */
static void draw_icon(uint8_t *buf, const sICON *icon, int dst_x, int dst_y)
{
    int stride = ((int)icon->Width + 3) / 4;
    for (int row = 0; row < (int)icon->Height; row++) {
        for (int col = 0; col < (int)icon->Width; col++) {
            int px = dst_x + col;
            int py = dst_y + row;
            if (px < 0 || px >= CANVAS_W || py < 0 || py >= CANVAS_H)
                continue;
            int byte_idx = row * stride + col / 4;
            int shift    = 6 - (col % 4) * 2;
            int gv       = (icon->table[byte_idx] >> shift) & 0x03;
            uint8_t lum;
            switch (gv) {
                case 0x03: lum = LUM_BLACK;      break;
                case 0x02: lum = LUM_DARK_GRAY;  break;
                case 0x01: lum = LUM_LIGHT_GRAY; break;
                default:   continue;   /* GRAY4 = transparent */
            }
            buf[py * CANVAS_W + px] = lum;
        }
    }
}

/* Centre an icon horizontally within a cell. */
static int cell_x(int cell_left, int cell_w, int icon_w)
{
    return cell_left + (cell_w - icon_w) / 2;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void)
{
    uint8_t *buf = malloc(CANVAS_W * CANVAS_H);
    if (!buf) { fprintf(stderr, "out of memory\n"); return 1; }
    memset(buf, LUM_WHITE, CANVAS_W * CANVAS_H);

    int y;   /* top of current icon row */

    /* ================================================================
     * ROW 1 — Status bar & sun icons (small: 10–30px wide, ≤18px tall)
     * Spread evenly across the full width with generous spacing.
     * ============================================================== */
    y = 30;

    /* 5 icons; place them in equal cells across 960px */
    #define STATUS_CELLS 5
    int status_cell = CANVAS_W / STATUS_CELLS;   /* 192px each */

    const sICON *status_icons[STATUS_CELLS] = {
        &icon_rain, &icon_wind, &icon_watertemp, &icon_sunrise, &icon_sunset
    };
    /* tallest is 18px (wind); vertically centre each in an 18px row */
    int row1_h = 18;
    for (int i = 0; i < STATUS_CELLS; i++) {
        int ix = cell_x(i * status_cell, status_cell, status_icons[i]->Width);
        int iy = y + (row1_h - (int)status_icons[i]->Height) / 2;
        draw_icon(buf, status_icons[i], ix, iy);
    }

    y += row1_h + 30;
    draw_hline(buf, y, 0, CANVAS_W, LUM_LIGHT_GRAY);
    y += 20;

    /* ================================================================
     * ROWS 2 & 3 — Moon phases (88×88, 4 per row)
     * ============================================================== */
    #define MOON_CELL  240   /* 960 / 4 */
    #define MOON_SIZE   88

    /* Waxing row */
    const sICON *moon_wax[4] = {
        &icon_moon_new, &icon_moon_waxing_crescent,
        &icon_moon_first_quarter, &icon_moon_waxing_gibbous
    };
    for (int i = 0; i < 4; i++)
        draw_icon(buf, moon_wax[i], cell_x(i * MOON_CELL, MOON_CELL, MOON_SIZE), y);

    y += MOON_SIZE + 20;
    draw_hline(buf, y, 0, CANVAS_W, LUM_LIGHT_GRAY);
    y += 20;

    /* Waning row */
    const sICON *moon_wan[4] = {
        &icon_moon_full, &icon_moon_waning_gibbous,
        &icon_moon_last_quarter, &icon_moon_waning_crescent
    };
    for (int i = 0; i < 4; i++)
        draw_icon(buf, moon_wan[i], cell_x(i * MOON_CELL, MOON_CELL, MOON_SIZE), y);

    y += MOON_SIZE + 20;
    draw_hline(buf, y, 0, CANVAS_W, LUM_LIGHT_GRAY);
    y += 20;

    /* ================================================================
     * ROW 4 — Tide cycles (~92×92, 3 per row)
     * ============================================================== */
    #define TIDE_CELL  320   /* 960 / 3 */

    const sICON *tide_icons[3] = {
        &icon_tide_spring, &icon_tide_neap, &icon_tide_half
    };
    int tide_h = 0;
    for (int i = 0; i < 3; i++) {
        if ((int)tide_icons[i]->Height > tide_h) tide_h = tide_icons[i]->Height;
    }
    for (int i = 0; i < 3; i++) {
        int ix = cell_x(i * TIDE_CELL, TIDE_CELL, tide_icons[i]->Width);
        int iy = y + (tide_h - (int)tide_icons[i]->Height) / 2;
        draw_icon(buf, tide_icons[i], ix, iy);
    }

    y += tide_h + 20;
    draw_hline(buf, y, 0, CANVAS_W, LUM_LIGHT_GRAY);
    y += 20;

    /* ================================================================
     * ROW 5 — Tide direction (42×18 and 43×18)
     * Centre the pair in the canvas.
     * ============================================================== */
    int dir_gap  = 80;
    int pair_w   = icon_high_tide.Width + dir_gap + icon_low_tide.Width;
    int pair_x   = (CANVAS_W - pair_w) / 2;

    draw_icon(buf, &icon_high_tide, pair_x, y + 4);
    draw_icon(buf, &icon_low_tide,  pair_x + icon_high_tide.Width + dir_gap, y + 4);

    /* Write BMP */
    const char *out = "icon_test.bmp";
    if (write_bmp(out, buf, CANVAS_W, CANVAS_H) == 0)
        printf("Written: %s  (%dx%d)\n", out, CANVAS_W, CANVAS_H);

    free(buf);
    return 0;
}

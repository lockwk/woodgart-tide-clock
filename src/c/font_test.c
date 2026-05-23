/*
 * font_test.c — Phase 2 font test for the 13.3" tide clock.
 *
 * Renders sample text at all five Inter font sizes into a grayscale BMP.
 * No display hardware required — just gcc + the generated font files.
 *
 * Build (from src/c/):
 *   gcc font_test.c inter_font.c \
 *       fonts/inter_sb_96.c fonts/inter_sb_56.c fonts/inter_sb_28.c \
 *       fonts/inter_sb_20.c fonts/inter_lt_16.c \
 *       -I. -o font_test
 *
 * Run:
 *   ./font_test
 *   # outputs font_test.bmp — open with any image viewer
 *   # also prints pixel widths to stdout for Figma comparison
 *
 * What to check:
 *   • Large numbers (96px, 56px) look clean and proportional
 *   • Small labels (16px) are readable — not broken or jagged
 *   • Letter spacing feels right compared with the Figma design
 *   • No garbled glyphs or obvious rendering artifacts
 *   • stdout widths match what Figma reports for the same strings
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "inter_font.h"
#include "fonts/inter_sb_96.h"
#include "fonts/inter_sb_56.h"
#include "fonts/inter_sb_28.h"
#include "fonts/inter_sb_20.h"
#include "fonts/inter_lt_16.h"

/* -------------------------------------------------------------------------
 * Canvas dimensions
 * ---------------------------------------------------------------------- */

#define CANVAS_W  960
#define CANVAS_H  620
#define MARGIN_L   40   /* left margin for all text */

/* -------------------------------------------------------------------------
 * Minimal BMP writer — 24-bit RGB, top-down storage
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

static int write_bmp(const char *path, const uint8_t *pixels,
                     int w, int h)
{
    /* Row size must be padded to a 4-byte boundary. */
    int row_bytes  = (w * 3 + 3) & ~3;
    int pixel_size = row_bytes * h;
    int file_size  = 54 + pixel_size;

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open %s for writing\n", path);
        return -1;
    }

    /* ---- BMP file header (14 bytes) ---- */
    fwrite("BM", 1, 2, f);
    write_u32(f, (uint32_t)file_size);
    write_u32(f, 0);        /* reserved */
    write_u32(f, 54);       /* pixel data offset */

    /* ---- DIB header / BITMAPINFOHEADER (40 bytes) ---- */
    write_u32(f, 40);       /* header size */
    write_u32(f, (uint32_t)w);
    write_u32(f, (uint32_t)(-h));   /* negative → top-down row order */
    write_u16(f, 1);        /* colour planes */
    write_u16(f, 24);       /* bits per pixel */
    write_u32(f, 0);        /* compression: none */
    write_u32(f, (uint32_t)pixel_size);
    write_u32(f, 3780);     /* x pixels per metre (~96 dpi) */
    write_u32(f, 3780);
    write_u32(f, 0);        /* colours in table */
    write_u32(f, 0);        /* important colours */

    /* ---- Pixel data (BGR order for BMP) ---- */
    uint8_t pad[4] = {0, 0, 0, 0};
    int pad_bytes = row_bytes - w * 3;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t v = pixels[y * w + x];
            fputc(v, f);   /* B */
            fputc(v, f);   /* G */
            fputc(v, f);   /* R */
        }
        fwrite(pad, 1, pad_bytes, f);
    }

    fclose(f);
    return 0;
}

/* -------------------------------------------------------------------------
 * Drawing helpers
 * ---------------------------------------------------------------------- */

static void draw_rule(uint8_t *buf, int y, int x0, int x1)
{
    if (y < 0 || y >= CANVAS_H) return;
    for (int x = x0; x < x1 && x < CANVAS_W; x++)
        buf[y * CANVAS_W + x] = 0xAA;   /* mid-gray */
}

/*
 * Draw a vertical tick mark at pixel x spanning y0..y1 (inclusive).
 * Used to show the measured end-of-string position for Figma comparison.
 */
static void draw_tick(uint8_t *buf, int x, int y0, int y1)
{
    if (x < 0 || x >= CANVAS_W) return;
    for (int y = y0; y <= y1 && y < CANVAS_H; y++) {
        if (y >= 0)
            buf[y * CANVAS_W + x] = 0x55;   /* dark gray tick */
    }
}

/* -------------------------------------------------------------------------
 * Render one section: draw the string, print its pixel width, draw tick.
 *
 *   buf        — canvas
 *   baseline_y — text baseline
 *   str        — string to render
 *   font       — font to use
 *   label      — printed to stdout (e.g. "96px")
 * ---------------------------------------------------------------------- */
static int render_section(uint8_t *buf, int baseline_y,
                           const char *str, const InterFont *font,
                           const char *label)
{
    int measured = inter_measure_string(font, str);
    int end_x    = inter_draw_string(buf, CANVAS_W, CANVAS_H,
                                     MARGIN_L, baseline_y, str, font);

    /* Tick mark spanning roughly the cap-height of this font */
    int tick_top    = baseline_y - font->ascent;
    int tick_bottom = baseline_y + (font->line_height - font->ascent);
    draw_tick(buf, MARGIN_L + measured, tick_top, tick_bottom);

    printf("  %-6s  \"%s\"\n"
           "           measured=%dpx  end_x=%dpx\n"
           "           → In Figma, select this text and check W in the "
           "right-panel; it should read %dpx\n",
           label, str, measured, end_x - MARGIN_L, measured);

    return end_x;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void)
{
    /* Allocate white canvas */
    uint8_t *buf = malloc(CANVAS_W * CANVAS_H);
    if (!buf) {
        fprintf(stderr, "ERROR: out of memory\n");
        return 1;
    }
    memset(buf, 255, CANVAS_W * CANVAS_H);

    int y = 0;   /* tracks current baseline as we move down the canvas */

    printf("font_test — pixel width diagnostics\n");
    printf("(dark tick at the right edge of each string = measured width)\n\n");

    /* ------------------------------------------------------------------
     * Section 1 — 96px SemiBold  (large tide height display)
     * ---------------------------------------------------------------- */
    y += inter_sb_96.ascent + 10;
    render_section(buf, y, "1 FT 3 IN", &inter_sb_96, "96px");
    draw_rule(buf, y + inter_sb_96.line_height - inter_sb_96.ascent + 4,
              MARGIN_L, CANVAS_W - MARGIN_L);

    /* ------------------------------------------------------------------
     * Section 2 — 56px SemiBold  (secondary numbers)
     * ---------------------------------------------------------------- */
    y += inter_sb_96.line_height + 16;
    render_section(buf, y, "3'5\" HIGH", &inter_sb_56, "56px");
    draw_rule(buf, y + inter_sb_56.line_height - inter_sb_56.ascent + 4,
              MARGIN_L, CANVAS_W - MARGIN_L);

    /* ------------------------------------------------------------------
     * Section 3 — 28px SemiBold  (time strings, graph labels)
     * ---------------------------------------------------------------- */
    y += inter_sb_56.line_height + 14;
    render_section(buf, y, "1:17 PM  SAT MAY 22 2026", &inter_sb_28, "28px");
    draw_rule(buf, y + inter_sb_28.line_height - inter_sb_28.ascent + 4,
              MARGIN_L, CANVAS_W - MARGIN_L);

    /* ------------------------------------------------------------------
     * Section 4 — 20px SemiBold  (tide labels on graph)
     * ---------------------------------------------------------------- */
    y += inter_sb_28.line_height + 12;
    render_section(buf, y,
                   "H 3'5\"  1:17     L 2'1\"  6:00", &inter_sb_20, "20px");
    draw_rule(buf, y + inter_sb_20.line_height - inter_sb_20.ascent + 4,
              MARGIN_L, CANVAS_W - MARGIN_L);

    /* ------------------------------------------------------------------
     * Section 5 — 16px Light  (sub-labels)
     * ---------------------------------------------------------------- */
    y += inter_sb_20.line_height + 12;
    render_section(buf, y,
                   "HIGH TIDE    SPRING TIDE    SUNRISE    SUNSET",
                   &inter_lt_16, "16px");

    /* ------------------------------------------------------------------
     * Section 6 — digits stress test (all ten at 96px)
     * ---------------------------------------------------------------- */
    y += inter_lt_16.line_height + 20;
    y += inter_sb_96.ascent;
    render_section(buf, y, "0123456789", &inter_sb_96, "96px-d");

    printf("\nDone. Compare the numbers above against Figma's W value\n"
           "for each text element at the matching font size.\n");

    /* Write BMP */
    const char *out = "font_test.bmp";
    if (write_bmp(out, buf, CANVAS_W, CANVAS_H) == 0)
        printf("Written: %s  (%dx%d)\n", out, CANVAS_W, CANVAS_H);

    free(buf);
    return 0;
}

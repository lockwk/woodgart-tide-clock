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
 *       -o font_test
 *
 * Run:
 *   ./font_test
 *   # outputs font_test.bmp — open with any image viewer
 *
 * What to check:
 *   • Large numbers (96px, 56px) look clean and proportional
 *   • Small labels (16px) are readable — not broken or jagged
 *   • Letter spacing feels right compared with the Figma design
 *   • No garbled glyphs or obvious rendering artifacts
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
#define CANVAS_H  520
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
 * Thin horizontal rule
 * ---------------------------------------------------------------------- */

static void draw_rule(uint8_t *buf, int y, int x0, int x1)
{
    if (y < 0 || y >= CANVAS_H) return;
    for (int x = x0; x < x1 && x < CANVAS_W; x++)
        buf[y * CANVAS_W + x] = 0xAA;   /* mid-gray */
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

    /* ------------------------------------------------------------------
     * Section 1 — 96px SemiBold  (large tide height display)
     *   e.g. "1 FT 3 IN" in the Next Tide panel
     * ---------------------------------------------------------------- */
    y += inter_sb_96.ascent + 10;
    inter_draw_string(buf, CANVAS_W, CANVAS_H,
                      MARGIN_L, y, "1 FT 3 IN", &inter_sb_96);
    draw_rule(buf, y + inter_sb_96.line_height - inter_sb_96.ascent + 4,
              MARGIN_L, CANVAS_W - MARGIN_L);

    /* ------------------------------------------------------------------
     * Section 2 — 56px SemiBold  (secondary numbers)
     * ---------------------------------------------------------------- */
    y += inter_sb_96.line_height + 16;
    inter_draw_string(buf, CANVAS_W, CANVAS_H,
                      MARGIN_L, y, "3'5\" HIGH", &inter_sb_56);
    draw_rule(buf, y + inter_sb_56.line_height - inter_sb_56.ascent + 4,
              MARGIN_L, CANVAS_W - MARGIN_L);

    /* ------------------------------------------------------------------
     * Section 3 — 28px SemiBold  (time strings, graph labels)
     * ---------------------------------------------------------------- */
    y += inter_sb_56.line_height + 14;
    inter_draw_string(buf, CANVAS_W, CANVAS_H,
                      MARGIN_L, y, "1:17 PM  SAT MAY 22 2026", &inter_sb_28);
    draw_rule(buf, y + inter_sb_28.line_height - inter_sb_28.ascent + 4,
              MARGIN_L, CANVAS_W - MARGIN_L);

    /* ------------------------------------------------------------------
     * Section 4 — 20px SemiBold  (tide labels on graph: "H 3'5\"")
     * ---------------------------------------------------------------- */
    y += inter_sb_28.line_height + 12;
    inter_draw_string(buf, CANVAS_W, CANVAS_H,
                      MARGIN_L, y,
                      "H 3'5\"  1:17     L 2'1\"  6:00", &inter_sb_20);
    draw_rule(buf, y + inter_sb_20.line_height - inter_sb_20.ascent + 4,
              MARGIN_L, CANVAS_W - MARGIN_L);

    /* ------------------------------------------------------------------
     * Section 5 — 16px Light  (sub-labels: HIGH TIDE, SPRING TIDE …)
     * ---------------------------------------------------------------- */
    y += inter_sb_20.line_height + 12;
    inter_draw_string(buf, CANVAS_W, CANVAS_H,
                      MARGIN_L, y,
                      "HIGH TIDE    SPRING TIDE    SUNRISE    SUNSET",
                      &inter_lt_16);

    /* ------------------------------------------------------------------
     * Section 6 — digits only stress test (all ten digits at 96px)
     * ---------------------------------------------------------------- */
    y += inter_lt_16.line_height + 20;
    y += inter_sb_96.ascent;
    inter_draw_string(buf, CANVAS_W, CANVAS_H,
                      MARGIN_L, y, "0123456789", &inter_sb_96);

    /* Write BMP */
    const char *out = "font_test.bmp";
    if (write_bmp(out, buf, CANVAS_W, CANVAS_H) == 0)
        printf("Written: %s  (%dx%d)\n", out, CANVAS_W, CANVAS_H);

    free(buf);
    return 0;
}

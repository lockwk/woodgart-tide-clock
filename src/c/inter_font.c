/*
 * inter_font.c — Lightweight renderer for tide-clock bitmap fonts.
 * See inter_font.h for documentation.
 */

#include "inter_font.h"
#include <stddef.h>   /* NULL */
#include <string.h>

/* -------------------------------------------------------------------------
 * Glyph lookup
 * ---------------------------------------------------------------------- */

const InterGlyph *inter_find_glyph(const InterFont *font, char ch)
{
    for (int i = 0; i < font->num_glyphs; i++) {
        if (font->glyphs[i].ch == ch)
            return &font->glyphs[i];
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Measurement
 * ---------------------------------------------------------------------- */

int inter_measure_string(const InterFont *font, const char *str)
{
    int width = 0;
    while (*str) {
        const InterGlyph *g = inter_find_glyph(font, *str++);
        if (g) width += g->adv_w;
    }
    return width;
}

/* -------------------------------------------------------------------------
 * 8-bit grayscale renderer
 * ---------------------------------------------------------------------- */

/*
 * Blend a 4bpp alpha value onto an 8-bit grayscale buffer pixel.
 *
 *   alpha4  — 0 (transparent) … 15 (fully opaque)
 *   fg8     — foreground intensity in 0–255 (0 = black)
 *   bg8     — current pixel value (background)
 *
 * Returns the blended pixel value.
 */
static inline uint8_t blend8(uint8_t alpha4, uint8_t fg8, uint8_t bg8)
{
    /* Expand alpha to 0–255 range */
    uint16_t a = (uint16_t)alpha4 * 17;   /* 15→255, 8→136, 0→0 */
    return (uint8_t)(((uint16_t)fg8 * a + (uint16_t)bg8 * (255 - a)) / 255);
}

int inter_draw_string(uint8_t *buf, int buf_w, int buf_h,
                      int x, int baseline_y,
                      const char *str, const InterFont *font)
{
    while (*str) {
        const InterGlyph *g = inter_find_glyph(font, *str++);
        if (!g) continue;

        if (g->width > 0 && g->height > 0) {
            /* Top-left corner of this glyph in screen coordinates.
             *
             * baseline_y is the y position of the text baseline.
             * ofs_y is the signed distance from baseline to the bbox bottom:
             *   screen_bottom = baseline_y - ofs_y
             *   screen_top    = screen_bottom - g->height
             */
            int draw_x = x + g->ofs_x;
            int draw_y = baseline_y - g->ofs_y - (int)g->height;

            const uint8_t *data   = font->bitmaps + g->data_offset;
            int            stride = (g->width + 1) / 2;  /* bytes per row */

            for (int row = 0; row < g->height; row++) {
                int sy = draw_y + row;
                if (sy < 0 || sy >= buf_h) continue;

                for (int col = 0; col < g->width; col++) {
                    int sx = draw_x + col;
                    if (sx < 0 || sx >= buf_w) continue;

                    /* Unpack 4bpp nibble */
                    uint8_t byte   = data[row * stride + col / 2];
                    uint8_t alpha4 = (col % 2 == 0) ? (byte >> 4) : (byte & 0x0F);

                    if (alpha4 == 0) continue;  /* fully transparent */

                    buf[sy * buf_w + sx] = blend8(alpha4, 0, buf[sy * buf_w + sx]);
                }
            }
        }

        x += g->adv_w;
    }
    return x;
}

/* -------------------------------------------------------------------------
 * Waveshare 4-gray frame buffer renderer
 *
 * Frame buffer layout:
 *   Each byte holds 4 pixels (2 bits each).
 *   Pixel 0 → bits [7:6], pixel 1 → bits [5:4], etc.
 *   Gray values: 0x03 = white, 0x02 = light gray,
 *                0x01 = dark gray, 0x00 = black.
 *   Byte address of pixel (px, py):
 *     byte  = py * (buf_w / 4) + px / 4
 *     shift = 6 - (px % 4) * 2
 * ---------------------------------------------------------------------- */

static inline void set_4gray_pixel(uint8_t *buf, int buf_w,
                                   int px, int py, uint8_t gray2)
{
    int byte_idx = py * (buf_w / 4) + px / 4;
    int shift    = 6 - (px % 4) * 2;
    buf[byte_idx] = (buf[byte_idx] & ~(0x03 << shift)) | ((gray2 & 0x03) << shift);
}

static inline uint8_t get_4gray_pixel(const uint8_t *buf, int buf_w,
                                      int px, int py)
{
    int byte_idx = py * (buf_w / 4) + px / 4;
    int shift    = 6 - (px % 4) * 2;
    return (buf[byte_idx] >> shift) & 0x03;
}

/*
 * Blend a 4bpp alpha onto a 2-bit (0–3) gray pixel.
 * fg and bg are both in 0–3 range (0=black, 3=white).
 */
static inline uint8_t blend4gray(uint8_t alpha4, uint8_t fg2, uint8_t bg2)
{
    /* Convert to 0–255 space, blend, convert back */
    uint16_t fg8 = (uint16_t)fg2 * 85;   /* 0→0, 1→85, 2→170, 3→255 */
    uint16_t bg8 = (uint16_t)bg2 * 85;
    uint16_t a   = (uint16_t)alpha4 * 17;
    uint16_t out = (fg8 * a + bg8 * (255 - a)) / 255;
    return (uint8_t)(out / 64);           /* quantise back to 0–3 */
}

int inter_draw_string_4gray(uint8_t *buf, int buf_w, int buf_h,
                             int x, int baseline_y,
                             uint8_t fg,
                             const char *str, const InterFont *font)
{
    while (*str) {
        const InterGlyph *g = inter_find_glyph(font, *str++);
        if (!g) continue;

        if (g->width > 0 && g->height > 0) {
            int draw_x = x + g->ofs_x;
            int draw_y = baseline_y - g->ofs_y - (int)g->height;

            const uint8_t *data   = font->bitmaps + g->data_offset;
            int            stride = (g->width + 1) / 2;

            for (int row = 0; row < g->height; row++) {
                int sy = draw_y + row;
                if (sy < 0 || sy >= buf_h) continue;

                for (int col = 0; col < g->width; col++) {
                    int sx = draw_x + col;
                    if (sx < 0 || sx >= buf_w) continue;

                    uint8_t byte   = data[row * stride + col / 2];
                    uint8_t alpha4 = (col % 2 == 0) ? (byte >> 4) : (byte & 0x0F);

                    if (alpha4 == 0) continue;

                    uint8_t bg  = get_4gray_pixel(buf, buf_w, sx, sy);
                    uint8_t out = blend4gray(alpha4, fg, bg);
                    set_4gray_pixel(buf, buf_w, sx, sy, out);
                }
            }
        }

        x += g->adv_w;
    }
    return x;
}

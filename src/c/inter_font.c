/*
 * inter_font.c — Lightweight renderer for tide-clock bitmap fonts.
 * See inter_font.h for documentation.
 *
 * adv_w is stored as 26.6 fixed-point (pixels * 64) so that sub-pixel
 * advance widths accumulate without per-glyph rounding error.
 * The kern table stays in whole pixels (adjustments are small integers).
 */

#include "inter_font.h"
#include <stddef.h>   /* NULL */
#include <string.h>

/* -------------------------------------------------------------------------
 * Glyph lookup — returns index (for kern table) or -1 if not found.
 * ---------------------------------------------------------------------- */

static int inter_find_glyph_idx(const InterFont *font, char ch)
{
    for (int i = 0; i < font->num_glyphs; i++) {
        if (font->glyphs[i].ch == ch)
            return i;
    }
    return -1;
}

const InterGlyph *inter_find_glyph(const InterFont *font, char ch)
{
    int idx = inter_find_glyph_idx(font, ch);
    return (idx >= 0) ? &font->glyphs[idx] : NULL;
}

/* -------------------------------------------------------------------------
 * Kern lookup — whole-pixel adjustment after glyph a, before glyph b.
 * Returns 0 if either character is not in the font or there is no kern table.
 * ---------------------------------------------------------------------- */

static int inter_kern(const InterFont *font, int idx_a, int idx_b)
{
    if (!font->kern_table || idx_a < 0 || idx_b < 0)
        return 0;
    return (int)font->kern_table[idx_a * font->num_glyphs + idx_b];
}

/* -------------------------------------------------------------------------
 * Measurement
 *
 * Accumulates advance widths in 26.6 fixed-point to avoid rounding drift,
 * then returns rounded integer pixels.
 * ---------------------------------------------------------------------- */

int inter_measure_string(const InterFont *font, const char *str)
{
    int width_fp  = 0;   /* 26.6 fixed-point accumulator */
    int prev_idx  = -1;
    while (*str) {
        int idx = inter_find_glyph_idx(font, *str++);
        if (idx < 0) continue;
        width_fp += inter_kern(font, prev_idx, idx) << 6;  /* px → 26.6 */
        width_fp += font->glyphs[idx].adv_w;               /* already 26.6 */
        prev_idx = idx;
    }
    return (width_fp + 32) >> 6;  /* round to nearest integer pixel */
}

int inter_measure_string_tracked(const InterFont *font, const char *str,
                                  int letter_spacing_px)
{
    if (letter_spacing_px == 0)
        return inter_measure_string(font, str);

    int width_fp = 0;
    int prev_idx = -1;
    int n_glyphs = 0;
    while (*str) {
        int idx = inter_find_glyph_idx(font, *str++);
        if (idx < 0) continue;
        width_fp += inter_kern(font, prev_idx, idx) << 6;
        width_fp += font->glyphs[idx].adv_w;
        prev_idx = idx;
        n_glyphs++;
    }
    /* letter_spacing is added after each glyph except the last */
    if (n_glyphs > 1)
        width_fp += (n_glyphs - 1) * (letter_spacing_px << 6);
    return (width_fp + 32) >> 6;
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
    int x_fp     = x << 6;   /* 26.6 fixed-point pen position */
    int prev_idx = -1;
    while (*str) {
        int idx = inter_find_glyph_idx(font, *str++);
        if (idx < 0) continue;

        /* Apply kern adjustment from previous glyph (whole pixels → 26.6) */
        x_fp += inter_kern(font, prev_idx, idx) << 6;
        prev_idx = idx;

        const InterGlyph *g = &font->glyphs[idx];
        if (g->width > 0 && g->height > 0) {
            /* Integer pixel position for this glyph */
            int pen_x  = x_fp >> 6;

            /* Top-left corner of this glyph in screen coordinates.
             *
             * baseline_y is the y position of the text baseline.
             * ofs_y is the signed distance from baseline to the bbox bottom:
             *   screen_bottom = baseline_y - ofs_y
             *   screen_top    = screen_bottom - g->height
             */
            int draw_x = pen_x + g->ofs_x;
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

        x_fp += g->adv_w;   /* adv_w is already 26.6 */
    }
    return x_fp >> 6;   /* return integer pixel position */
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
    int x_fp     = x << 6;   /* 26.6 fixed-point pen position */
    int prev_idx = -1;
    while (*str) {
        int idx = inter_find_glyph_idx(font, *str++);
        if (idx < 0) continue;

        x_fp += inter_kern(font, prev_idx, idx) << 6;
        prev_idx = idx;

        const InterGlyph *g = &font->glyphs[idx];
        if (g->width > 0 && g->height > 0) {
            int pen_x  = x_fp >> 6;
            int draw_x = pen_x + g->ofs_x;
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

        x_fp += g->adv_w;
    }
    return x_fp >> 6;
}

int inter_draw_string_4gray_tracked(uint8_t *buf, int buf_w, int buf_h,
                                     int x, int baseline_y,
                                     uint8_t fg,
                                     const char *str, const InterFont *font,
                                     int letter_spacing_px)
{
    if (letter_spacing_px == 0)
        return inter_draw_string_4gray(buf, buf_w, buf_h, x, baseline_y,
                                       fg, str, font);

    int x_fp     = x << 6;
    int prev_idx = -1;
    while (*str) {
        int idx = inter_find_glyph_idx(font, *str++);
        if (idx < 0) continue;

        x_fp += inter_kern(font, prev_idx, idx) << 6;
        prev_idx = idx;

        const InterGlyph *g = &font->glyphs[idx];
        if (g->width > 0 && g->height > 0) {
            int pen_x  = x_fp >> 6;
            int draw_x = pen_x + g->ofs_x;
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

        x_fp += g->adv_w;
        /* Add letter-spacing after every glyph (CSS / Figma behaviour) */
        if (*str) x_fp += letter_spacing_px << 6;
    }
    return x_fp >> 6;
}

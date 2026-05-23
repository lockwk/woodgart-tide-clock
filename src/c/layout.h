/*
 * layout.h — Display constants, drawing primitives, and rendering API for
 *            the 13.3" tide clock.
 *
 * This header is included by tide_clock.c, layout.c, and tide_curve.c so
 * they all share the same display geometry, gray-level constants, and
 * low-level pixel helpers.
 *
 * Drawing primitives are declared here and defined in layout.c.
 * High-level render functions (render_status_bar, etc.) are implemented
 * section-by-section across Phases 5-8.
 */

#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdint.h>
#include "json_reader.h"

/* ==========================================================================
 * Display geometry -- 13.3" e-Paper HAT (K), 960x680 px, 4-level grayscale
 * ======================================================================= */

#define DISP_W        960
#define DISP_H        680
#define DISP_STRIDE   (DISP_W / 4)            /* bytes per row = 240          */
#define DISP_BUFSIZE  (DISP_STRIDE * DISP_H)  /* total bytes = 163200         */

/*
 * 4-gray pixel encoding
 * 2 bits per pixel, 4 pixels per byte, MSB-first (px0 in bits[7:6]).
 * memset(buf, 0xFF, DISP_BUFSIZE) -> all white (four 0x03 pixels/byte).
 */
#define GRAY1  0x00   /* #000000 - black  : text, curve, dividers        */
#define GRAY2  0x01   /* #555555 - dark   : night fill, current-hour bar */
#define GRAY3  0x02   /* #AAAAAA - light  : (reserved / anti-alias)      */
#define GRAY4  0x03   /* #FFFFFF - white  : background                   */

/*
 * Layout regions (pixel rows) — measured from Figma "Tide Clock e-Paper 13.3":
 *
 *   y =   0 ..  63   status bar   (STATUS_H = 64 px)
 *   y =  64           divider line  (9.41% of 680)
 *   y =  65 .. 419   tide graph   (GRAPH_H  = 355 px)
 *   y = 420           bottom panels start (panels include 2px top border)
 *   y = 420 .. 679   bottom panels (PANEL_H = 260 px)
 */
#define STATUS_H      64
#define DIVIDER_Y1    64    /* status bar -> graph divider    */
#define GRAPH_TOP     65
#define GRAPH_BOT     419
#define DIVIDER_Y2    420   /* graph -> bottom panels divider */
#define PANEL_TOP     420
#define PANEL_BOT     679
#define PANEL_W       240   /* each of the four equal bottom panels */
#define PANEL_H       260

/* Tide curve drawing bounds within the graph area.
 * Labels (70 px tall) sit above each peak circle — CURVE_TOP_Y must leave
 * enough room above for the topmost label to stay inside the graph area.
 * CURVE_BOT_Y leaves a small gap above the panel divider for the hour label. */
#define CURVE_TOP_Y   150   /* y of the highest tide circle center */
#define CURVE_BOT_Y   400   /* y of the lowest tide circle center  */

/* ==========================================================================
 * Low-level drawing primitives (implemented in layout.c)
 * ======================================================================= */

void set_pixel(uint8_t *buf, int x, int y, uint8_t gv);
void draw_line(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t gv);
void draw_line2(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t gv);
void fill_rect(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t gv);
void draw_hline_full(uint8_t *buf, int y, uint8_t gv);
void draw_vline_full(uint8_t *buf, int x, uint8_t gv);
void draw_circle(uint8_t *buf, int cx, int cy, int r, uint8_t gv);
void draw_arrow_up(uint8_t *buf, int cx, int top_y, int h, uint8_t gv);
void draw_arrow_down(uint8_t *buf, int cx, int bot_y, int h, uint8_t gv);

/* ==========================================================================
 * Icon blitter
 * ======================================================================= */

#include "icons/icon_types.h"

void draw_icon(uint8_t *buf, const sICON *icon, int dst_x, int dst_y);

/* ==========================================================================
 * Text helpers
 * ======================================================================= */

#include "inter_font.h"

/* Standard (no extra letter spacing) */
int  draw_str  (uint8_t *buf, int x, int baseline_y, const char *str, const InterFont *font);
void draw_str_c(uint8_t *buf, int center_x, int baseline_y, const char *str, const InterFont *font);
void draw_str_r(uint8_t *buf, int x_right,  int baseline_y, const char *str, const InterFont *font);

/* Tracked — adds letter_spacing_px between every glyph (Figma "tracking") */
int  draw_str_t (uint8_t *buf, int x, int baseline_y, const char *str, const InterFont *font, int sp);
void draw_str_tc(uint8_t *buf, int center_x, int baseline_y, const char *str, const InterFont *font, int sp);
void draw_str_tr(uint8_t *buf, int x_right,  int baseline_y, const char *str, const InterFont *font, int sp);

/* Status bar letter spacing (5.6 px, rounded to 6) */
#define STATUS_TRACKING  6

/* ==========================================================================
 * High-level render functions (Phases 5-8)
 * ======================================================================= */

void render_status_bar   (uint8_t *buf, const ClockData *data);   /* Phase 5 */
void render_tide_graph   (uint8_t *buf, const ClockData *data);   /* Phase 6/7 */
void render_bottom_panels(uint8_t *buf, const ClockData *data);   /* Phase 8 */

#endif /* LAYOUT_H */

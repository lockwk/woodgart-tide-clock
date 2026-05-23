/*
 * icon_test_epd.c — Phase 3 icon test, Pi display version.
 *
 * Same layout as icon_test.c (Mac BMP version) but renders directly into
 * the Waveshare 4-gray frame buffer and pushes it to the 13.3" e-paper
 * display.  No GUI_Paint dependency — pixels are written directly.
 *
 * Buffer format (Waveshare 4-gray):
 *   Size  : (960 / 4) * 680 = 163,200 bytes
 *   Layout: 2 bits per pixel, 4 pixels per byte, MSB-first
 *   Values: GRAY1 = 0x03 (black)   GRAY2 = 0x02
 *           GRAY3 = 0x01            GRAY4 = 0x00 (white)
 *
 * Build (on the Pi, from ~/tide-clock-dev/src/c/):
 *
 *   gcc icon_test_epd.c \
 *       /home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_13in3k.c \
 *       /home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib/Config/DEV_Config.c \
 *       -I. \
 *       -I/home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib \
 *       -I/home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib/e-Paper \
 *       -I/home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib/Config \
 *       -lgpiod -lm \
 *       -o icon_test_epd
 *
 * Run:
 *   sudo ./icon_test_epd
 *   Press Ctrl+C to sleep the display and exit cleanly.
 *
 * Note — if the build fails with "lgpiod not found", try -lgpio instead.
 * Check the Waveshare demo Makefile for the exact flag used on your Pi.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* Waveshare driver headers — resolved via -I flags above */
#include "DEV_Config.h"
#include "EPD_13in3k.h"

/* Icon types + all icons */
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
 * Display geometry
 * ---------------------------------------------------------------------- */

#define DISP_W       960
#define DISP_H       680
#define DISP_STRIDE  (DISP_W / 4)                     /* bytes per row = 240 */
#define DISP_BUFSIZE (DISP_STRIDE * DISP_H)            /* 163,200 bytes      */

/* Gray values — Waveshare EPD_13IN3K display encoding (confirmed from driver source).
 * NOTE: This is the INVERSE of the icon file encoding in the .h headers.
 *       Icons store 0x03=black, 0x00=white (natural/BMP convention).
 *       The display driver maps 0x00→black, 0x03→white (inverted).
 *       draw_icon() handles the inversion at blit time. */
#define GRAY_BLACK  0x00   /* darkest  — displays black  */
#define GRAY_DARK   0x02   /* dark gray                  */
#define GRAY_LIGHT  0x01   /* light gray                 */
#define GRAY_WHITE  0x03   /* lightest — displays white  */

/* -------------------------------------------------------------------------
 * Signal handler — sleep display on Ctrl+C
 * ---------------------------------------------------------------------- */

static volatile int g_exit = 0;

static void handler(int sig)
{
    (void)sig;
    g_exit = 1;
}

/* -------------------------------------------------------------------------
 * Drawing into the Waveshare 4-gray buffer
 * ---------------------------------------------------------------------- */

/*
 * set_pixel — write a single pixel into the Waveshare 4-gray buffer.
 * gv must be GRAY_BLACK / GRAY_DARK / GRAY_LIGHT / GRAY_WHITE (0x03–0x00).
 */
static void set_pixel(uint8_t *buf, int x, int y, int gv)
{
    if (x < 0 || x >= DISP_W || y < 0 || y >= DISP_H) return;
    int addr  = y * DISP_STRIDE + x / 4;
    int shift = 6 - (x % 4) * 2;
    buf[addr] = (uint8_t)((buf[addr] & ~(0x03 << shift)) | ((gv & 0x03) << shift));
}

/*
 * draw_hline — draw a horizontal rule in GRAY_LIGHT across the full width.
 */
static void draw_hline(uint8_t *buf, int y)
{
    for (int x = 0; x < DISP_W; x++)
        set_pixel(buf, x, y, GRAY_LIGHT);
}

/*
 * draw_icon — unpack a 2-bit sICON and blit it into the display buffer.
 *
 * Icon files use the natural/BMP encoding: 0x03 = black stroke, 0x00 = white bg.
 * The display uses the inverse:            0x00 = black,        0x03 = white.
 * We invert each pixel value at blit time: display_gv = (~gv) & 0x03
 *   icon 0x03 (black stroke) → display 0x00 (black)  ✓
 *   icon 0x00 (white bg)     → display 0x03 (white)  → skip (transparent)
 */
static void draw_icon(uint8_t *buf, const sICON *icon, int dst_x, int dst_y)
{
    int stride = ((int)icon->Width + 3) / 4;
    for (int row = 0; row < (int)icon->Height; row++) {
        for (int col = 0; col < (int)icon->Width; col++) {
            int byte_idx  = row * stride + col / 4;
            int shift     = 6 - (col % 4) * 2;
            int gv        = (icon->table[byte_idx] >> shift) & 0x03;
            int display_gv = (~gv) & 0x03;   /* invert: natural → display encoding */
            if (display_gv == GRAY_WHITE) continue;   /* transparent */
            set_pixel(buf, dst_x + col, dst_y + row, display_gv);
        }
    }
}

/* Centre an icon horizontally within a cell. */
static int cell_x(int cell_left, int cell_w, int icon_w)
{
    return cell_left + (cell_w - icon_w) / 2;
}

/* -------------------------------------------------------------------------
 * Render — identical layout to icon_test.c
 * ---------------------------------------------------------------------- */

static void render(uint8_t *buf)
{
    /* White background: 0xFF per byte = four pixels of 0x03 each = white on display. */
    memset(buf, 0xFF, DISP_BUFSIZE);

    int y;

    /* -- ROW 1: status bar & sun icons ---------------------------------- */
    y = 30;
    {
        #define N_STATUS 5
        const sICON *icons[N_STATUS] = {
            &icon_rain, &icon_wind, &icon_watertemp, &icon_sunrise, &icon_sunset
        };
        int cell = DISP_W / N_STATUS;   /* 192px each */
        int row_h = 18;
        for (int i = 0; i < N_STATUS; i++) {
            int ix = cell_x(i * cell, cell, icons[i]->Width);
            int iy = y + (row_h - (int)icons[i]->Height) / 2;
            draw_icon(buf, icons[i], ix, iy);
        }
        y += row_h + 30;
    }

    draw_hline(buf, y);
    y += 20;

    /* -- ROWS 2 & 3: moon phases (88×88, 4 per row) --------------------- */
    #define MOON_CELL  240
    #define MOON_SIZE   88

    {
        const sICON *wax[4] = {
            &icon_moon_new, &icon_moon_waxing_crescent,
            &icon_moon_first_quarter, &icon_moon_waxing_gibbous
        };
        for (int i = 0; i < 4; i++)
            draw_icon(buf, wax[i], cell_x(i * MOON_CELL, MOON_CELL, MOON_SIZE), y);
        y += MOON_SIZE + 20;
    }

    draw_hline(buf, y);
    y += 20;

    {
        const sICON *wan[4] = {
            &icon_moon_full, &icon_moon_waning_gibbous,
            &icon_moon_last_quarter, &icon_moon_waning_crescent
        };
        for (int i = 0; i < 4; i++)
            draw_icon(buf, wan[i], cell_x(i * MOON_CELL, MOON_CELL, MOON_SIZE), y);
        y += MOON_SIZE + 20;
    }

    draw_hline(buf, y);
    y += 20;

    /* -- ROW 4: tide cycles (~92×92, 3 per row) ------------------------- */
    #define TIDE_CELL  320

    {
        const sICON *tides[3] = {
            &icon_tide_spring, &icon_tide_neap, &icon_tide_half
        };
        int max_h = 0;
        for (int i = 0; i < 3; i++)
            if ((int)tides[i]->Height > max_h) max_h = tides[i]->Height;
        for (int i = 0; i < 3; i++) {
            int ix = cell_x(i * TIDE_CELL, TIDE_CELL, tides[i]->Width);
            int iy = y + (max_h - (int)tides[i]->Height) / 2;
            draw_icon(buf, tides[i], ix, iy);
        }
        y += max_h + 20;
    }

    draw_hline(buf, y);
    y += 20;

    /* -- ROW 5: tide direction (centred pair) ---------------------------- */
    {
        int gap    = 80;
        int pair_w = icon_high_tide.Width + gap + icon_low_tide.Width;
        int pair_x = (DISP_W - pair_w) / 2;
        draw_icon(buf, &icon_high_tide, pair_x, y + 4);
        draw_icon(buf, &icon_low_tide,  pair_x + icon_high_tide.Width + gap, y + 4);
    }
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void)
{
    signal(SIGINT,  handler);
    signal(SIGTERM, handler);

    printf("icon_test_epd: initialising display...\n");

    if (DEV_Module_Init() != 0) {
        fprintf(stderr, "ERROR: DEV_Module_Init() failed\n");
        return 1;
    }

    /* Step 1: standard init + clear (required before switching to 4-gray) */
    EPD_13IN3K_Init();
    EPD_13IN3K_Clear();
    DEV_Delay_ms(500);

    /* Step 2: switch to 4-gray mode */
    EPD_13IN3K_Init_4GRAY();

    /* Allocate frame buffer (163,200 bytes) */
    uint8_t *buf = (uint8_t *)malloc(DISP_BUFSIZE);
    if (!buf) {
        fprintf(stderr, "ERROR: out of memory\n");
        DEV_Module_Exit();
        return 1;
    }

    printf("Rendering icons...\n");
    render(buf);

    printf("Pushing to display...\n");
    EPD_13IN3K_4GrayDisplay(buf);

    printf("Done. Display showing icon test. Press Ctrl+C to sleep and exit.\n");

    /* Wait until Ctrl+C */
    while (!g_exit)
        DEV_Delay_ms(200);

    /* Re-init to 1-bit mode before sleeping (matches Waveshare demo sequence) */
    printf("\nSleeping display...\n");
    EPD_13IN3K_Init();
    EPD_13IN3K_Clear();
    EPD_13IN3K_Sleep();
    free(buf);
    DEV_Module_Exit();

    return 0;
}

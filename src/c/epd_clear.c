/*
 * epd_clear.c -- Burn-in clearing utility for 13.3" e-Paper HAT (K).
 *
 * Continuously cycles the display through all 4 grayscale levels
 * (black → dark gray → light gray → white) to help clear ghosting
 * and burn-in artifacts.
 *
 * Build (on the Pi, from ~/tide-clock-dev/src/c/):
 *   make epd_clear
 *   sudo ./epd_clear
 *
 * Press Ctrl+C to stop. The display will be left white and put to sleep.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "DEV_Config.h"
#include "EPD_13in3k.h"

/* Display geometry (must match layout.h) */
#define DISP_W        960
#define DISP_H        680
#define DISP_STRIDE   (DISP_W / 4)
#define DISP_BUFSIZE  (DISP_STRIDE * DISP_H)

/*
 * 4-gray: 2 bits per pixel, 4 pixels per byte.
 * To fill every pixel with one gray level, pack 4 identical 2-bit values:
 *   GRAY1 (0x00 black)      -> byte 0x00  (00 00 00 00)
 *   GRAY2 (0x01 dark gray)  -> byte 0x55  (01 01 01 01)
 *   GRAY3 (0x02 light gray) -> byte 0xAA  (10 10 10 10)
 *   GRAY4 (0x03 white)      -> byte 0xFF  (11 11 11 11)
 */
static const struct {
    const char *name;
    uint8_t     fill;
} colors[] = {
    { "BLACK",      0x00 },
    { "DARK GRAY",  0x55 },
    { "LIGHT GRAY", 0xAA },
    { "WHITE",      0xFF },
};
#define NUM_COLORS  (sizeof(colors) / sizeof(colors[0]))

static volatile int g_exit = 0;

static void handler(int sig)
{
    (void)sig;
    g_exit = 1;
}

int main(void)
{
    signal(SIGINT,  handler);
    signal(SIGTERM, handler);

    printf("epd_clear: initialising display...\n");
    if (DEV_Module_Init() != 0) {
        fprintf(stderr, "ERROR: DEV_Module_Init() failed\n");
        return 1;
    }

    /* Full B/W init + clear, then switch to 4-gray (matches tide_clock.c) */
    EPD_13IN3K_Init();
    EPD_13IN3K_Clear();
    DEV_Delay_ms(500);
    EPD_13IN3K_Init_4GRAY();

    uint8_t *buf = (uint8_t *)malloc(DISP_BUFSIZE);
    if (!buf) {
        fprintf(stderr, "ERROR: out of memory\n");
        DEV_Module_Exit();
        return 1;
    }

    int cycle = 0;

    while (!g_exit) {
        cycle++;
        for (int c = 0; c < (int)NUM_COLORS && !g_exit; c++) {
            printf("  cycle %d: %s...\n", cycle, colors[c].name);
            memset(buf, colors[c].fill, DISP_BUFSIZE);
            EPD_13IN3K_4GrayDisplay(buf);
            if (g_exit) break;
            DEV_Delay_ms(2000);
        }
    }

    /* Leave display white and sleep */
    printf("\nepd_clear: cleaning up (leaving display white)...\n");
    memset(buf, 0xFF, DISP_BUFSIZE);
    EPD_13IN3K_4GrayDisplay(buf);
    EPD_13IN3K_Sleep();

    free(buf);
    DEV_Module_Exit();

    printf("epd_clear: done after %d cycles.\n", cycle);
    return 0;
}

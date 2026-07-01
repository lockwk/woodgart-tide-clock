/*
 * epd_clear.c -- Burn-in clearing utility for 13.3" e-Paper HAT (K).
 *
 * Continuously cycles the display between full black and full white
 * to help clear ghosting / burn-in artifacts.
 *
 * Build (on the Pi, from ~/tide-clock-dev/src/c/):
 *   make epd_clear
 *   sudo ./epd_clear
 *
 * Press Ctrl+C to stop. The display will be left white and put to sleep.
 */

#include <stdio.h>
#include <signal.h>

#include "DEV_Config.h"
#include "EPD_13in3k.h"

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

    EPD_13IN3K_Init();

    int cycle = 0;

    while (!g_exit) {
        cycle++;

        /* Black */
        printf("  cycle %d: BLACK...\n", cycle);
        EPD_13IN3K_color_Base(0x00);
        if (g_exit) break;
        DEV_Delay_ms(2000);
        if (g_exit) break;

        /* White */
        printf("  cycle %d: WHITE...\n", cycle);
        EPD_13IN3K_color_Base(0xFF);
        if (g_exit) break;
        DEV_Delay_ms(2000);
    }

    /* Leave display white and sleep */
    printf("\nepd_clear: cleaning up (leaving display white)...\n");
    EPD_13IN3K_color_Base(0xFF);
    EPD_13IN3K_Sleep();
    DEV_Module_Exit();

    printf("epd_clear: done after %d cycles.\n", cycle);
    return 0;
}

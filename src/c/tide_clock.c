/*
 * tide_clock.c -- 13.3" e-paper tide clock, main program.
 *
 * Pipeline:
 *   1. Read /tmp/tide_data.json (written by src/fetch/fetcher_13in3.py)
 *   2. Parse into ClockData
 *   3. Print all fields to stdout (diagnostic)
 *   4. Initialise the display
 *   5. Allocate 4-gray frame buffer; clear to white
 *   6. Call render stubs (filled in Phases 5-8)
 *   7. Push frame buffer to display
 *   8. Sleep display; clean exit
 *
 * -----------------------------------------------------------------------
 * Build (on the Pi, from ~/tide-clock-dev/src/c/):
 *
 *   make clean && make RPI
 *   sudo ./epd
 *
 * See Makefile for the full compiler invocation.
 * -----------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* Waveshare driver headers (resolved via -I flags in Makefile) */
#include "DEV_Config.h"
#include "EPD_13in3k.h"

/* Project headers */
#include "json_reader.h"
#include "layout.h"
#include "tide_curve.h"

/* Font sizes used by the renderer (included here so the translation
 * unit pulls in the bitmap data; individual render functions in layout.c
 * also include what they need). */
#include "fonts/inter_lt_48.h"
#include "fonts/inter_b_14.h"

/* ==========================================================================
 * Signal handler -- sleep display on Ctrl+C or SIGTERM
 * ======================================================================= */

static volatile int g_exit = 0;

static void handler(int sig)
{
    (void)sig;
    g_exit = 1;
}

/* ==========================================================================
 * main
 * ======================================================================= */

int main(void)
{
    signal(SIGINT,  handler);
    signal(SIGTERM, handler);

    /* ------------------------------------------------------------------
     * 1. Read and parse JSON
     * ---------------------------------------------------------------- */
    const char *json_path = "/tmp/tide_data.json";

    printf("tide_clock: reading %s ...\n", json_path);
    char *json = read_file(json_path);
    if (!json) {
        fprintf(stderr, "ERROR: could not read '%s'\n"
                        "       Run fetcher_13in3.py first.\n", json_path);
        return 1;
    }

    ClockData data;
    if (!parse_clock_data(json, &data)) {
        fprintf(stderr, "ERROR: failed to parse JSON from '%s'\n", json_path);
        free(json);
        return 1;
    }
    free(json);

    /* ------------------------------------------------------------------
     * 2. Print parsed data to stdout (diagnostic -- Phase 4 deliverable)
     * ---------------------------------------------------------------- */
    print_clock_data(&data);

    /* ------------------------------------------------------------------
     * 3. Initialise display
     * ---------------------------------------------------------------- */
    printf("tide_clock: initialising display...\n");
    if (DEV_Module_Init() != 0) {
        fprintf(stderr, "ERROR: DEV_Module_Init() failed\n");
        return 1;
    }

    /* Full init + clear required before switching to 4-gray mode */
    EPD_13IN3K_Init();
    EPD_13IN3K_Clear();
    DEV_Delay_ms(500);

    /* Switch to 4-gray mode */
    EPD_13IN3K_Init_4GRAY();

    /* ------------------------------------------------------------------
     * 4. Allocate frame buffer and clear to white
     * ---------------------------------------------------------------- */
    uint8_t *buf = (uint8_t *)malloc(DISP_BUFSIZE);
    if (!buf) {
        fprintf(stderr, "ERROR: out of memory allocating frame buffer\n");
        DEV_Module_Exit();
        return 1;
    }

    /* memset 0xFF -> four 0x03 (white) pixels per byte */
    memset(buf, 0xFF, DISP_BUFSIZE);

    /* ------------------------------------------------------------------
     * 5. Render (stubs for Phase 4 -- filled in Phases 5-8)
     * ---------------------------------------------------------------- */
    printf("tide_clock: rendering...\n");
    render_status_bar   (buf, &data);
    render_tide_graph   (buf, &data);
    render_bottom_panels(buf, &data);

    /* ------------------------------------------------------------------
     * 6. Push frame buffer to display
     * ---------------------------------------------------------------- */
    printf("tide_clock: pushing to display...\n");
    EPD_13IN3K_4GrayDisplay(buf);

    printf("tide_clock: done.  Press Ctrl+C to sleep display and exit.\n");

    /* Hold until signalled */
    while (!g_exit)
        DEV_Delay_ms(200);

    /* ------------------------------------------------------------------
     * 7. Sleep display and clean up
     * ---------------------------------------------------------------- */
    printf("\ntide_clock: sleeping display...\n");
    EPD_13IN3K_Init();
    EPD_13IN3K_Clear();
    EPD_13IN3K_Sleep();

    free(buf);
    DEV_Module_Exit();
    return 0;
}

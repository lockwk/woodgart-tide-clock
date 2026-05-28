/*
 * tide_clock.c -- 13.3" e-paper tide clock, main program.
 *
 * Pipeline:
 *   1. Initialise the display (once).
 *   2. Run the Python fetcher, read /tmp/tide_data.json, render, push.
 *   3. Sleep until the next H:00:05 trigger.
 *   4. If the current hour is within REFRESH_HOUR_MIN..REFRESH_HOUR_MAX,
 *      repeat from step 2.  Otherwise skip and sleep again.
 *   5. On SIGINT / SIGTERM: sleep the display and exit cleanly.
 *
 * Refresh window: 5:00:05 AM through 9:00:05 PM (hours 5–21 inclusive).
 * On startup the fetcher always runs immediately regardless of the time.
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
#include <time.h>
#include <unistd.h>

/* Waveshare driver headers (resolved via -I flags in Makefile) */
#include "DEV_Config.h"
#include "EPD_13in3k.h"

/* Project headers */
#include "json_reader.h"
#include "layout.h"
#include "tide_curve.h"

/* Font sizes used by the renderer */
#include "fonts/inter_lt_48.h"
#include "fonts/inter_b_14.h"

/* ==========================================================================
 * Configuration
 * ======================================================================= */

#define JSON_PATH  "/tmp/tide_data.json"

/* Hourly refresh fires at H:00:05 for hours in [REFRESH_HOUR_MIN, REFRESH_HOUR_MAX] */
#define REFRESH_HOUR_MIN  5   /*  5 AM */
#define REFRESH_HOUR_MAX  21  /*  9 PM */

/* ==========================================================================
 * Signal handler
 * ======================================================================= */

static volatile int g_exit = 0;

static void handler(int sig)
{
    (void)sig;
    g_exit = 1;
}

/* ==========================================================================
 * Helpers
 * ======================================================================= */

/*
 * sleep_interruptible -- sleep for `seconds` seconds in 200 ms chunks,
 * returning early if g_exit is set.
 */
static void sleep_interruptible(int seconds)
{
    for (int s = 0; s < seconds && !g_exit; s++) {
        /* 5 × 200 ms = 1 s, checking g_exit between each chunk */
        for (int ms = 0; ms < 5 && !g_exit; ms++)
            DEV_Delay_ms(200);
    }
}

/*
 * secs_to_next_trigger -- seconds from now until the next H:00:05 mark.
 *
 * If we are already in the first 5 seconds of the current hour (e.g. just
 * crossed the hour boundary), we wait to :05 of this hour.  Otherwise we
 * wait to :05 of the next hour.
 */
static int secs_to_next_trigger(void)
{
    time_t     now        = time(NULL);
    struct tm *lt         = localtime(&now);
    int        secs_in_hr = lt->tm_min * 60 + lt->tm_sec;

    if (secs_in_hr < 5)
        return 5 - secs_in_hr;          /* wait to :05 of current hour */

    return 3600 - secs_in_hr + 5;       /* wait to :05 of next hour    */
}

/*
 * load_data -- read JSON_PATH, parse it into *data, then overwrite the
 * time fields with the live system clock.
 *
 * Returns 1 on success, 0 on failure.
 */
static int load_data(ClockData *data)
{
    char *json = read_file(JSON_PATH);
    if (!json) {
        fprintf(stderr, "ERROR: could not read '%s'\n"
                        "       Run fetcher first.\n", JSON_PATH);
        return 0;
    }
    if (!parse_clock_data(json, data)) {
        fprintf(stderr, "ERROR: failed to parse JSON from '%s'\n", JSON_PATH);
        free(json);
        return 0;
    }
    free(json);

    /* Overwrite time fields with the live system clock so the displayed
     * time always reflects when the renderer actually runs, not when the
     * fetcher last wrote the file.                                       */
    {
        time_t     now    = time(NULL);
        struct tm *lt     = localtime(&now);
        int        hour24 = lt->tm_hour;
        int        min    = lt->tm_min;
        int        h12    = hour24 % 12;
        if (h12 == 0) h12 = 12;
        const char *ampm  = (hour24 >= 12) ? "PM" : "AM";

        snprintf(data->current_time_str, sizeof(data->current_time_str),
                 "%d:%02d %s", h12, min, ampm);
        strftime(data->current_date_str, sizeof(data->current_date_str),
                 "%a %b %d %Y", lt);
        /* strftime gives mixed case; upcase to match design */
        for (char *p = data->current_date_str; *p; p++)
            if (*p >= 'a' && *p <= 'z') *p -= 32;

        data->current_hour   = hour24;
        data->current_minute = min;
    }

    return 1;
}

/* ==========================================================================
 * main
 * ======================================================================= */

int main(void)
{
    signal(SIGINT,  handler);
    signal(SIGTERM, handler);

    /* ------------------------------------------------------------------
     * Initialise display (once at startup).
     * ---------------------------------------------------------------- */
    printf("tide_clock: initialising display...\n");
    if (DEV_Module_Init() != 0) {
        fprintf(stderr, "ERROR: DEV_Module_Init() failed\n");
        return 1;
    }

    /* Full B/W init + clear before switching to 4-gray mode */
    EPD_13IN3K_Init();
    EPD_13IN3K_Clear();
    DEV_Delay_ms(500);
    EPD_13IN3K_Init_4GRAY();

    /* ------------------------------------------------------------------
     * Allocate frame buffer (cleared to white = 0xFF per byte).
     * ---------------------------------------------------------------- */
    uint8_t *buf = (uint8_t *)malloc(DISP_BUFSIZE);
    if (!buf) {
        fprintf(stderr, "ERROR: out of memory allocating frame buffer\n");
        DEV_Module_Exit();
        return 1;
    }

    /* ------------------------------------------------------------------
     * Fetch-render-display loop.
     *
     * first_run = 1: always fetch + render immediately on startup.
     * first_run = 0: sleep to the next :05 trigger, then check the
     *                refresh window before fetching.
     * ---------------------------------------------------------------- */
    int first_run = 1;

    while (!g_exit) {

        if (!first_run) {
            /* Sleep until the next H:00:05 mark */
            int wait = secs_to_next_trigger();
            printf("tide_clock: sleeping %d s to next trigger...\n", wait);
            sleep_interruptible(wait);
            if (g_exit) break;

            /* Skip render outside the active refresh window */
            time_t     now  = time(NULL);
            struct tm *lt   = localtime(&now);
            int        hour = lt->tm_hour;
            if (hour < REFRESH_HOUR_MIN || hour > REFRESH_HOUR_MAX) {
                printf("tide_clock: outside refresh window (hour %02d), "
                       "skipping.\n", hour);
                continue;
            }
        }

        first_run = 0;

        /* ---- Run fetcher ------------------------------------------ */
        /* Derive the fetcher path from the binary's own location so this
         * works in any branch (main, dev, playground) without recompiling. */
        char fetcher_cmd[512];
        {
            char exe[256];
            ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
            if (n < 0) {
                fprintf(stderr, "ERROR: readlink /proc/self/exe failed\n");
                break;
            }
            exe[n] = '\0';
            /* exe = <repo>/src/c/epd — strip three path components */
            char *p;
            p = strrchr(exe, '/'); if (p) *p = '\0'; /* strip /epd  */
            p = strrchr(exe, '/'); if (p) *p = '\0'; /* strip /c    */
            p = strrchr(exe, '/'); if (p) *p = '\0'; /* strip /src  */
            snprintf(fetcher_cmd, sizeof(fetcher_cmd),
                     "python3 %s/src/fetch/fetcher_13in3.py", exe);
        }
        printf("tide_clock: running fetcher: %s\n", fetcher_cmd);
        int fetch_ret = system(fetcher_cmd);
        if (fetch_ret != 0)
            fprintf(stderr, "WARNING: fetcher exited with status %d\n",
                    fetch_ret);

        /* ---- Load and parse JSON ---------------------------------- */
        ClockData data;
        if (!load_data(&data)) {
            fprintf(stderr, "ERROR: skipping render this cycle\n");
            continue;
        }
        print_clock_data(&data);

        /* ---- Render into frame buffer ----------------------------- */
        memset(buf, 0xFF, DISP_BUFSIZE);   /* clear to white */
        render_status_bar   (buf, &data);
        render_tide_graph   (buf, &data);
        render_bottom_panels(buf, &data);

        /* ---- Push to display -------------------------------------- */
        printf("tide_clock: pushing to display...\n");
        EPD_13IN3K_Init_4GRAY();           /* re-init before each 4-gray push */
        EPD_13IN3K_4GrayDisplay(buf);
        EPD_13IN3K_Sleep();                /* sleep display between refreshes  */
        printf("tide_clock: display updated and sleeping.\n");
    }

    /* ------------------------------------------------------------------
     * Clean shutdown: sleep display and release resources.
     * ---------------------------------------------------------------- */
    printf("\ntide_clock: sleeping display...\n");
    EPD_13IN3K_Init();
    EPD_13IN3K_Clear();
    EPD_13IN3K_Sleep();

    free(buf);
    DEV_Module_Exit();
    return 0;
}

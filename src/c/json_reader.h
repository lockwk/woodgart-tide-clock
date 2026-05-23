/*
 * json_reader.h — JSON data contract for the 13.3" tide clock.
 *
 * The Python fetcher writes /tmp/tide_data.json; this module reads it and
 * populates a ClockData struct.  No external JSON library is used — parsing
 * is done with strstr + pointer arithmetic since the schema is fixed.
 *
 * See docs/13in3_C_Development_Plan.md § "JSON Data Contract" for the
 * full field list and example values.
 */

#ifndef JSON_READER_H
#define JSON_READER_H

#include <stdint.h>

/* Maximum hi/lo tide points returned by NOAA for a single day (typically 3–4). */
#define MAX_TIDES 8

/* --------------------------------------------------------------------------
 * Data structures
 * ---------------------------------------------------------------------- */

/*
 * TidePoint — one hi or lo tide event.
 * Matches each element of the "tides" array in the JSON.
 */
typedef struct {
    char  time_str[16];   /* "7:53 AM"                               */
    int   hour;           /* 0–23                                    */
    int   minute;         /* 0–59                                    */
    float height_ft;      /* signed, e.g. -0.04 or 3.42             */
    char  type;           /* 'H' (high) or 'L' (low)                */
} TidePoint;

/*
 * ClockData — everything the C renderer needs, parsed from tide_data.json.
 */
typedef struct {

    /* ---- Current time ---- */
    char  current_time_str[16];   /* "9:35 AM"                      */
    char  current_date_str[24];   /* "SAT MAY 19 2026"              */
    int   current_hour;           /* 0–23                           */
    int   current_minute;         /* 0–59                           */

    /* ---- Today's hi/lo tide events ---- */
    TidePoint tides[MAX_TIDES];
    int       n_tides;            /* number of valid entries        */

    /* ---- Next tide panel ---- */
    char  next_tide_time_str[16]; /* "1:17 PM"                      */
    int   next_tide_height_whole_ft; /* integer feet part, e.g. 1  */
    int   next_tide_height_rem_in;   /* remaining inches, e.g. 3   */
    char  next_tide_type;         /* 'H' or 'L'                     */

    /* ---- Tide cycle ---- */
    char  tide_cycle[16];         /* "SPRING", "NEAP", or "HALF"    */

    /* ---- Weather ---- */
    int   wind_speed_mph;         /* e.g. 3                         */
    char  wind_direction[8];      /* "N", "SW", "NNE", etc.         */
    int   rain_hours_since;       /* hours since last ≥0.25" rain   */
    float water_temp_f;           /* sea surface temp in °F         */

    /* ---- Sun ---- */
    int   sunrise_hour;           /* 0–23                           */
    int   sunrise_minute;         /* 0–59                           */
    char  sunrise_str[16];        /* "6:15 AM"                      */
    int   sunset_hour;            /* 0–23                           */
    int   sunset_minute;          /* 0–59                           */
    char  sunset_str[16];         /* "8:04 PM"                      */

    /* ---- Moon ---- */
    char  moon_phase[24];         /* "NEW", "FULL", "WAXING_CRESCENT", etc. */
    float moon_age;               /* days into current cycle, 0.0–29.5      */

} ClockData;

/* --------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/*
 * read_file — read the entire file at `path` into a malloc'd, NUL-terminated
 *             buffer.  The caller must free() the returned pointer.
 *             Returns NULL on error (file not found, out of memory, etc.)
 *             and prints the error to stderr.
 */
char *read_file(const char *path);

/*
 * parse_clock_data — populate *out from the JSON string `json`.
 *
 * All recognised fields are extracted; unrecognised or missing optional
 * fields are silently skipped (a warning is printed for required fields).
 *
 * Returns 1 on success (all required fields found), 0 on failure.
 */
int parse_clock_data(const char *json, ClockData *out);

/*
 * print_clock_data — dump all fields of *data to stdout (for debugging).
 */
void print_clock_data(const ClockData *data);

#endif /* JSON_READER_H */

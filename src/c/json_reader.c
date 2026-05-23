/*
 * json_reader.c — Minimal JSON field extractor for tide_data.json.
 *
 * No external JSON library is used.  The schema is fixed and well-known,
 * so we parse with strstr + careful pointer arithmetic.  The only heap
 * allocation is in read_file() to load the whole file at once.
 *
 * Parsing strategy
 * ----------------
 *   find_value(json, "key") searches for the literal string "key" and
 *   returns a pointer to the first non-whitespace character of its value.
 *   Type-specific helpers (extract_string, extract_int, extract_double,
 *   extract_char_from_string) then consume that pointer.
 *
 *   For the "tides" array we find the '[' and then walk object-by-object
 *   with find_array_element().
 *
 *   For the "next_tide" nested object we find its '{' and pass that
 *   sub-string to the same helpers.
 */

#include "json_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Internal helpers
 * ======================================================================= */

/*
 * skip_ws — advance past ASCII whitespace.
 */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

/*
 * find_value — locate the value of a JSON key in the given (sub-)string.
 *
 * Searches for the exact token "key" (with surrounding double-quotes), then
 * skips optional whitespace and the colon separator.
 *
 * Returns a pointer to the first character of the value (which may be '"',
 * '[', '{', a digit, '-', 't', 'f', or 'n'), or NULL if the key is absent.
 *
 * NOTE: if the same key appears more than once, the first occurrence wins.
 */
static const char *find_value(const char *json, const char *key)
{
    /* Build the search token: "key" */
    char token[80];
    int  klen = (int)strlen(key);
    if (klen + 3 >= (int)sizeof(token))
        return NULL;   /* key too long — programmer error */

    token[0] = '"';
    memcpy(token + 1, key, (size_t)klen);
    token[klen + 1] = '"';
    token[klen + 2] = '\0';

    const char *p = strstr(json, token);
    if (!p)
        return NULL;

    p += klen + 2;               /* skip past closing '"' of the key */
    p  = skip_ws(p);
    if (*p != ':')
        return NULL;             /* malformed JSON */
    p++;
    p = skip_ws(p);
    return p;                    /* points to first char of value    */
}

/*
 * extract_string — copy a JSON string value (without surrounding quotes)
 *                  into out[0..out_size-1].
 *
 * `val` must point to the opening '"' of the value.
 * Returns 1 on success, 0 on failure.
 */
static int extract_string(const char *val, char *out, size_t out_size)
{
    if (!val || *val != '"')
        return 0;

    val++;   /* skip opening '"' */
    size_t i = 0;
    while (*val && *val != '"' && i < out_size - 1)
        out[i++] = *val++;
    out[i] = '\0';
    return 1;
}

/*
 * extract_double — parse a JSON number value.
 * `val` must point to the first character of the number (digit or '-').
 */
static double extract_double(const char *val)
{
    if (!val)
        return 0.0;
    return strtod(val, NULL);
}

/*
 * extract_int — parse a JSON integer value (truncates doubles).
 */
static int extract_int(const char *val)
{
    if (!val)
        return 0;
    return (int)strtol(val, NULL, 10);
}

/*
 * extract_char_from_string — return the first character inside a JSON
 *                            string value, e.g. "H" → 'H'.
 * Returns '\0' on failure.
 */
static char extract_char_from_string(const char *val)
{
    if (!val || *val != '"' || val[1] == '\0')
        return '\0';
    return val[1];
}

/* --------------------------------------------------------------------------
 * Array helpers
 * ---------------------------------------------------------------------- */

/*
 * find_array_start — find the '[' for a named array key.
 * Returns a pointer to '[', or NULL.
 */
static const char *find_array_start(const char *json, const char *key)
{
    const char *v = find_value(json, key);
    if (!v || *v != '[')
        return NULL;
    return v;
}

/*
 * find_array_element — return a pointer to the opening '{' of the n-th
 *                      object in a JSON array (0-indexed).
 *
 * `arr` must point to '['.
 * Returns NULL if the index is out of bounds.
 */
static const char *find_array_element(const char *arr, int index)
{
    if (!arr || *arr != '[')
        return NULL;

    const char *p   = arr + 1;
    int         count = 0;

    while (*p) {
        p = skip_ws(p);

        if (*p == ']')
            break;

        if (*p == ',') {
            p++;
            continue;
        }

        if (*p == '{') {
            if (count == index)
                return p;   /* found the requested element */

            count++;

            /* Skip over this object (track brace depth) */
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if      (*p == '{') depth++;
                else if (*p == '}') depth--;
                p++;
            }
        } else {
            p++;
        }
    }

    return NULL;   /* index out of range */
}

/* --------------------------------------------------------------------------
 * Count array elements
 * ---------------------------------------------------------------------- */

static int count_array_elements(const char *arr)
{
    if (!arr || *arr != '[')
        return 0;

    const char *p     = arr + 1;
    int         count = 0;

    while (*p) {
        p = skip_ws(p);
        if (*p == ']') break;
        if (*p == ',') { p++; continue; }
        if (*p == '{') {
            count++;
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if      (*p == '{') depth++;
                else if (*p == '}') depth--;
                p++;
            }
        } else {
            p++;
        }
    }
    return count;
}

/* --------------------------------------------------------------------------
 * Find a nested object by key (returns pointer to '{')
 * ---------------------------------------------------------------------- */

static const char *find_object(const char *json, const char *key)
{
    const char *v = find_value(json, key);
    if (!v || *v != '{')
        return NULL;
    return v;
}

/* ==========================================================================
 * Public API
 * ======================================================================= */

char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "json_reader: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fprintf(stderr, "json_reader: '%s' is empty\n", path);
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)(size + 1));
    if (!buf) {
        fprintf(stderr, "json_reader: out of memory reading '%s'\n", path);
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

int parse_clock_data(const char *json, ClockData *out)
{
    if (!json || !out)
        return 0;

    memset(out, 0, sizeof(*out));

    const char *v;

    /* ---- Current time ---- */
    v = find_value(json, "current_time_str");
    extract_string(v, out->current_time_str, sizeof(out->current_time_str));

    v = find_value(json, "current_date_str");
    extract_string(v, out->current_date_str, sizeof(out->current_date_str));

    v = find_value(json, "current_hour");
    out->current_hour   = extract_int(v);

    v = find_value(json, "current_minute");
    out->current_minute = extract_int(v);

    /* ---- Tides array ---- */
    {
        const char *arr = find_array_start(json, "tides");
        int n = arr ? count_array_elements(arr) : 0;
        if (n > MAX_TIDES) n = MAX_TIDES;
        out->n_tides = n;

        for (int i = 0; i < n; i++) {
            const char *elem = find_array_element(arr, i);
            if (!elem) break;

            TidePoint *tp = &out->tides[i];

            v = find_value(elem, "time_str");
            extract_string(v, tp->time_str, sizeof(tp->time_str));

            v = find_value(elem, "hour");
            tp->hour = extract_int(v);

            v = find_value(elem, "minute");
            tp->minute = extract_int(v);

            v = find_value(elem, "height_ft");
            tp->height_ft = (float)extract_double(v);

            v = find_value(elem, "type");
            tp->type = extract_char_from_string(v);
        }
    }

    /* ---- Next tide (nested object) ---- */
    {
        const char *obj = find_object(json, "next_tide");
        if (obj) {
            v = find_value(obj, "time_str");
            extract_string(v, out->next_tide_time_str,
                           sizeof(out->next_tide_time_str));

            v = find_value(obj, "height_whole_ft");
            out->next_tide_height_whole_ft = extract_int(v);

            v = find_value(obj, "height_rem_in");
            out->next_tide_height_rem_in = extract_int(v);

            v = find_value(obj, "type");
            out->next_tide_type = extract_char_from_string(v);
        }
    }

    /* ---- Tide cycle ---- */
    v = find_value(json, "tide_cycle");
    extract_string(v, out->tide_cycle, sizeof(out->tide_cycle));

    /* ---- Weather ---- */
    v = find_value(json, "wind_speed_mph");
    out->wind_speed_mph = extract_int(v);

    v = find_value(json, "wind_direction");
    extract_string(v, out->wind_direction, sizeof(out->wind_direction));

    v = find_value(json, "rain_hours_since");
    out->rain_hours_since = (v && strncmp(v, "null", 4) == 0) ? -1 : extract_int(v);

    v = find_value(json, "water_temp_f");
    out->water_temp_f = (float)extract_double(v);

    /* ---- Sun ---- */
    v = find_value(json, "sunrise_hour");
    out->sunrise_hour   = extract_int(v);

    v = find_value(json, "sunrise_minute");
    out->sunrise_minute = extract_int(v);

    v = find_value(json, "sunrise_str");
    extract_string(v, out->sunrise_str, sizeof(out->sunrise_str));

    v = find_value(json, "sunset_hour");
    out->sunset_hour   = extract_int(v);

    v = find_value(json, "sunset_minute");
    out->sunset_minute = extract_int(v);

    v = find_value(json, "sunset_str");
    extract_string(v, out->sunset_str, sizeof(out->sunset_str));

    /* ---- Moon ---- */
    v = find_value(json, "moon_phase");
    extract_string(v, out->moon_phase, sizeof(out->moon_phase));

    v = find_value(json, "moon_age");
    out->moon_age = (float)extract_double(v);

    /* Required field check — current_time_str is a good sentinel */
    if (out->current_time_str[0] == '\0') {
        fprintf(stderr, "json_reader: required field 'current_time_str' missing\n");
        return 0;
    }

    return 1;
}

void print_clock_data(const ClockData *data)
{
    if (!data) return;

    printf("=== ClockData ===\n");
    printf("  time         : %s\n",  data->current_time_str);
    printf("  date         : %s\n",  data->current_date_str);
    printf("  hour/min     : %02d:%02d\n", data->current_hour, data->current_minute);
    printf("\n");

    printf("  tides (%d):\n", data->n_tides);
    for (int i = 0; i < data->n_tides; i++) {
        const TidePoint *tp = &data->tides[i];
        printf("    [%d] %s  %s  %.2f ft\n",
               i, tp->time_str,
               tp->type == 'H' ? "HIGH" : "LOW",
               (double)tp->height_ft);
    }
    printf("\n");

    printf("  next_tide    : %c  %d ft %d in  @ %s\n",
           data->next_tide_type,
           data->next_tide_height_whole_ft,
           data->next_tide_height_rem_in,
           data->next_tide_time_str);
    printf("  tide_cycle   : %s\n", data->tide_cycle);
    printf("\n");

    printf("  wind         : %s %d mph\n",
           data->wind_direction, data->wind_speed_mph);
    if (data->rain_hours_since < 0)
        printf("  rain hrs     : (null)\n");
    else
        printf("  rain hrs     : %d\n", data->rain_hours_since);
    printf("  water temp   : %.1f F\n", (double)data->water_temp_f);
    printf("\n");

    printf("  sunrise      : %s  (%02d:%02d)\n",
           data->sunrise_str, data->sunrise_hour, data->sunrise_minute);
    printf("  sunset       : %s  (%02d:%02d)\n",
           data->sunset_str, data->sunset_hour, data->sunset_minute);
    printf("\n");

    printf("  moon_phase   : %s\n",  data->moon_phase);
    printf("  moon_age     : %.1f days\n", (double)data->moon_age);
    printf("=================\n");
}

/*
 * tide_curve.c — Hermite cubic spline and tide graph renderer.
 *
 * Every control point is a tidal extremum (alternating high/low), so the
 * physically correct slope at every point is zero.  We use a Hermite cubic
 * spline with prescribed zero slopes, which gives a smoothstep curve between
 * each pair of adjacent tides.  This guarantees that the mathematical peak
 * of the spline coincides exactly with the reported tide time — the dot and
 * the curve peak land on the same pixel.
 *
 * The per-segment coefficients are derived from the Hermite basis with m=0:
 *
 *   S_i(t) = h_i  +  3·Δh/Δt²·dt²  −  2·Δh/Δt³·dt³
 *
 * where dt = t − t_i, Δh = h_{i+1} − h_i, Δt = t_{i+1} − t_i.
 * Each segment is computed independently — no global solve required.
 */

#include "tide_curve.h"
#include "layout.h"   /* draw_line2, GRAY1, CURVE_TOP_Y, CURVE_BOT_Y */

/* Maximum control points: today's tides + optional prev + optional next */
#define SPLINE_MAX_N  (MAX_TIDES + 3)

/* --------------------------------------------------------------------------
 * compute_spline — Hermite cubic spline with zero slopes at all control
 * points.
 *
 * pts_t must be strictly increasing.  n must be >= 2.
 * segs must point to a caller-allocated array of (n-1) SplineSeg structs.
 *
 * Because every tide control point is an extremum, slope = 0 everywhere.
 * Each segment reduces to:
 *
 *   b = 0
 *   c = 3·Δh / Δt²
 *   d = −2·Δh / Δt³
 *
 * Segments are independent — no global solve needed.
 * ---------------------------------------------------------------------- */
void compute_spline(const float *pts_t, const float *pts_h, int n,
                    SplineSeg *segs)
{
    for (int i = 0; i < n - 1; i++) {
        float seg_dt = pts_t[i + 1] - pts_t[i];
        float seg_dh = pts_h[i + 1] - pts_h[i];
        segs[i].t0 = pts_t[i];
        segs[i].a  = pts_h[i];
        segs[i].b  = 0.0f;
        segs[i].c  =  3.0f * seg_dh / (seg_dt * seg_dt);
        segs[i].d  = -2.0f * seg_dh / (seg_dt * seg_dt * seg_dt);
    }
}

/* --------------------------------------------------------------------------
 * eval_spline — evaluate the spline at time t (minutes since midnight).
 *
 * If t is before the first control point, returns the first point's height.
 * If t is past the last segment's start, evaluates (and lightly extrapolates)
 * the last segment — acceptable because the graph window is bounded by
 * sunrise-1hr / sunset+1hr, which stay within a reasonable tidal range.
 * ---------------------------------------------------------------------- */
float eval_spline(const SplineSeg *segs, int n_segs, float t)
{
    /* Clamp below: hold the first control point's value */
    if (t <= segs[0].t0)
        return segs[0].a;

    /* Find segment: the largest i such that segs[i].t0 <= t */
    int i = 0;
    for (int k = 1; k < n_segs; k++) {
        if (segs[k].t0 <= t) i = k;
        else break;
    }

    float dt = t - segs[i].t0;
    return segs[i].a + dt * (segs[i].b + dt * (segs[i].c + dt * segs[i].d));
}

/* --------------------------------------------------------------------------
 * render_tide_curve — draw the smooth 2px tide curve into the frame buffer.
 *
 * Iterates every pixel column x from x0 to x1, maps it to a time value in
 * [t_start, t_end], evaluates the spline, maps the height to a y pixel, and
 * connects adjacent samples with draw_line2 (2-pixel-thick Bresenham line).
 * ---------------------------------------------------------------------- */
void render_tide_curve(uint8_t *buf,
                       const float *pts_t, const float *pts_h, int n,
                       float t_start, float t_end,
                       int x0, int x1,
                       int y_top_px, int y_bot_px,
                       float h_min, float h_max)
{
    SplineSeg segs[SPLINE_MAX_N - 1];
    compute_spline(pts_t, pts_h, n, segs);
    int n_segs = n - 1;

    float x_span = (float)(x1 - x0);
    float y_span = (float)(y_bot_px - y_top_px);
    float h_span = h_max - h_min;

    /* Clamp bounds: never extrapolate beyond the last tide data point */
    float t_data_start = pts_t[0];
    float t_data_end   = pts_t[n - 1];

    int prev_x = -1, prev_y = -1;

    for (int px = x0; px <= x1; px++) {
        /* Map pixel column to time */
        float t = t_start + (float)(px - x0) * (t_end - t_start) / x_span;

        /* Clamp to data range to avoid cubic extrapolation artifacts */
        float t_eval = t;
        if (t_eval < t_data_start) t_eval = t_data_start;
        if (t_eval > t_data_end)   t_eval = t_data_end;

        /* Evaluate spline */
        float h = eval_spline(segs, n_segs, t_eval);

        /* Map height to y pixel (higher h → smaller y) */
        float fy = (float)y_bot_px - (h - h_min) * y_span / h_span;
        if (fy < (float)y_top_px) fy = (float)y_top_px;
        if (fy > (float)y_bot_px) fy = (float)y_bot_px;
        int cur_y = (int)(fy + 0.5f);

        if (prev_x >= 0)
            draw_line2(buf, prev_x, prev_y, px, cur_y, GRAY1);

        prev_x = px;
        prev_y = cur_y;
    }
}

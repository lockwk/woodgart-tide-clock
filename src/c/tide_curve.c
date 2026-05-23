/*
 * tide_curve.c — Natural cubic spline and tide graph renderer.
 *
 * The spline algorithm is the standard natural cubic spline (Burden & Faires,
 * "Numerical Analysis", Algorithm 3.4).  The working implementation was
 * proven in fidelity_test.c and promoted here in Phase 6.
 */

#include "tide_curve.h"
#include "layout.h"   /* draw_line2, GRAY1, CURVE_TOP_Y, CURVE_BOT_Y */

/*
 * SPLINE_MAX_N — maximum number of control points we support on the stack.
 * Must be > MAX_TIDES (from json_reader.h, via layout.h) so all daily tide
 * events fit.  The solver needs arrays of size n (not n-1), so +1.
 */
#define SPLINE_MAX_N  (MAX_TIDES + 1)

/* --------------------------------------------------------------------------
 * compute_spline — natural cubic spline through n control points.
 *
 * pts_t must be strictly increasing (consecutive values must differ by at
 * least 1 minute; typical NOAA data has gaps of 100–400 minutes).
 * n must satisfy 2 <= n <= SPLINE_MAX_N.
 * segs must point to a caller-allocated array of (n-1) SplineSeg structs.
 * ---------------------------------------------------------------------- */
void compute_spline(const float *pts_t, const float *pts_h, int n,
                    SplineSeg *segs)
{
    int n1 = n - 1;   /* number of segments */
    int i;

    float dt[SPLINE_MAX_N], dh[SPLINE_MAX_N];
    float alpha[SPLINE_MAX_N];
    float l[SPLINE_MAX_N], mu[SPLINE_MAX_N], z[SPLINE_MAX_N];
    float c[SPLINE_MAX_N], b[SPLINE_MAX_N], d_arr[SPLINE_MAX_N];

    for (i = 0; i < n1; i++) {
        dt[i] = pts_t[i + 1] - pts_t[i];
        dh[i] = pts_h[i + 1] - pts_h[i];
    }

    /* Build the tridiagonal RHS */
    for (i = 1; i < n1; i++)
        alpha[i] = 3.0f * (dh[i] / dt[i] - dh[i - 1] / dt[i - 1]);

    /* Forward sweep — Thomas algorithm */
    l[0] = 1.0f;  mu[0] = 0.0f;  z[0] = 0.0f;
    for (i = 1; i < n1; i++) {
        l[i]  = 2.0f * (pts_t[i + 1] - pts_t[i - 1]) - dt[i - 1] * mu[i - 1];
        mu[i] = dt[i] / l[i];
        z[i]  = (alpha[i] - dt[i - 1] * z[i - 1]) / l[i];
    }
    l[n1] = 1.0f;  z[n1] = 0.0f;  c[n1] = 0.0f;

    /* Back substitution */
    for (int j = n1 - 1; j >= 0; j--) {
        c[j]      = z[j] - mu[j] * c[j + 1];
        b[j]      = dh[j] / dt[j] - dt[j] * (c[j + 1] + 2.0f * c[j]) / 3.0f;
        d_arr[j]  = (c[j + 1] - c[j]) / (3.0f * dt[j]);

        segs[j].a  = pts_h[j];
        segs[j].b  = b[j];
        segs[j].c  = c[j];
        segs[j].d  = d_arr[j];
        segs[j].t0 = pts_t[j];
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

    int prev_x = -1, prev_y = -1;

    for (int px = x0; px <= x1; px++) {
        /* Map pixel column to time */
        float t = t_start + (float)(px - x0) * (t_end - t_start) / x_span;

        /* Evaluate spline */
        float h = eval_spline(segs, n_segs, t);

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

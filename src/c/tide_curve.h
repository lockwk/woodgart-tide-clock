/*
 * tide_curve.h — Hermite cubic spline and tide graph renderer.
 *
 * compute_spline uses a Hermite cubic with zero slopes at all control
 * points.  Because every tide data point is an extremum, slope = 0 is
 * physically correct and guarantees the spline peak coincides exactly
 * with the reported tide time.
 */

#ifndef TIDE_CURVE_H
#define TIDE_CURVE_H

#include <stdint.h>

/* One segment of the Hermite cubic spline: S_i(t) = a + b*dt + c*dt^2 + d*dt^3
 * where dt = t - t0.  b is always 0 (zero slope at each tide extremum). */
typedef struct {
    float a, b, c, d;
    float t0;   /* time (minutes since midnight) at the start of this segment */
} SplineSeg;

/*
 * compute_spline -- compute Hermite cubic spline through n control points,
 * with zero slope prescribed at every point.
 *
 *   pts_t  -- monotonically increasing time values (minutes since midnight)
 *   pts_h  -- tide heights (feet) at each control point
 *   n      -- number of control points (>= 2)
 *   segs   -- output array of (n-1) SplineSeg structs (caller-allocated)
 */
void compute_spline(const float *pts_t, const float *pts_h, int n,
                    SplineSeg *segs);

/*
 * eval_spline -- evaluate the spline at time t.
 *
 *   segs    -- spline segments (from compute_spline)
 *   n_segs  -- number of segments (= n - 1 from compute_spline call)
 *   t       -- time in minutes since midnight
 *
 * Returns the interpolated tide height in feet.
 * Clamps to the first/last segment if t is out of range.
 */
float eval_spline(const SplineSeg *segs, int n_segs, float t);

/*
 * render_tide_curve -- draw the smooth tide curve into the 4-gray frame buffer.
 *
 * This is called by render_tide_graph() in Phase 7.  Parameters:
 *
 *   buf        -- 4-gray frame buffer ((DISP_W/4) * DISP_H bytes)
 *   pts_t      -- control point times (minutes since midnight)
 *   pts_h      -- control point heights (feet)
 *   n          -- number of control points
 *   t_start    -- leftmost time shown on the graph (maps to x = x0)
 *   t_end      -- rightmost time shown on the graph (maps to x = x1)
 *   x0, x1     -- pixel column range for the graph area
 *   y_top_px   -- pixel row for the maximum height
 *   y_bot_px   -- pixel row for the minimum height
 *   h_min      -- height that maps to y_bot_px
 *   h_max      -- height that maps to y_top_px
 */
void render_tide_curve(uint8_t *buf,
                       const float *pts_t, const float *pts_h, int n,
                       float t_start, float t_end,
                       int x0, int x1,
                       int y_top_px, int y_bot_px,
                       float h_min, float h_max);

#endif /* TIDE_CURVE_H */

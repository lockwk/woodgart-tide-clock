/*
 * tide_curve.c -- Natural cubic spline and tide graph renderer.
 *
 * Phase 7 will replace these stubs with a full implementation.
 * (The working spline code already exists in fidelity_test.c and will
 *  be moved here.)
 */

#include "tide_curve.h"

void compute_spline(const float *pts_t, const float *pts_h, int n,
                    SplineSeg *segs)
{
    /* Phase 7 */
    (void)pts_t; (void)pts_h; (void)n; (void)segs;
}

float eval_spline(const SplineSeg *segs, int n_segs, float t)
{
    /* Phase 7 */
    (void)segs; (void)n_segs; (void)t;
    return 0.0f;
}

void render_tide_curve(uint8_t *buf,
                       const float *pts_t, const float *pts_h, int n,
                       float t_start, float t_end,
                       int x0, int x1,
                       int y_top_px, int y_bot_px,
                       float h_min, float h_max)
{
    /* Phase 7 */
    (void)buf;
    (void)pts_t; (void)pts_h; (void)n;
    (void)t_start; (void)t_end;
    (void)x0; (void)x1;
    (void)y_top_px; (void)y_bot_px;
    (void)h_min; (void)h_max;
}

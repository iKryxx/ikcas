//
// Created by ikryxxdev on 2/10/26.
//

#include "ui/plot.h"
#include "core/core.h"
#include "core/eval.h"
#include "core/node.h"
#include <float.h>
#include <math.h>

typedef struct {
    bool ok;
    double y;
} plot_sample_t;

static void plot__line(unsigned char *rgba, int W, int H, int x0, int y0,
                       int x1, int y1, unsigned char r, unsigned char g,
                       unsigned char b, unsigned char a) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        if ((unsigned)x0 < (unsigned)W && (unsigned)y0 < (unsigned)H) {
            unsigned char *p = rgba + (y0 * W + x0) * 4;
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = a;
        }
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void plot_callable(ui_t *ui, const char *name, double from, double to) {
    if (from >= to) {
        ui_print(ui, "Please input a correct range.");
        return;
    }

    ui_viz_clear(ui);

    const int W = 640;
    const int H = 640;

    const int stride = W * 4;

    unsigned char rgba[stride * H];
    memset(rgba, 0, stride * H);

    arena_t func_mem = {0};
    arena_init(&func_mem);

    node_t xr = {.kind = NODE_REAL, .real = 0.0};
    node_t *vals[1] = {&xr};

    int prev_x = 0, prev_y = 0;
    bool has_prev = false;

    node_t *call = node_call(&func_mem, name, (node_t **)vals, 1);
    plot_sample_t samples[W];
    double y_min = DBL_MAX;
    double y_max = -DBL_MAX;
    for (int x = 0; x < W; ++x) {
        double t = (double)x / (double)(W - 1); // 0..1
        double xx = from + t * (to - from);     // "World" x
        vals[0]->real = xx;

        eval_result_t res = node_eval_approx(&func_mem, call, core_get_g_env());
        samples[x] =
            (plot_sample_t){.ok = res.ok, .y = (res.ok ? res.out->real : 0.0)};

        if (samples[x].y < y_min)
            y_min = samples[x].y;
        if (samples[x].y > y_max)
            y_max = samples[x].y;
    }

    double margin = (y_max - y_min) * 0.05;
    y_max += margin;
    y_min -= margin;
    if (y_max == INFINITY)
        y_max = DBL_MAX;
    if (y_min == -INFINITY)
        y_min = -DBL_MAX;
    if (y_max - y_min <= 1.0) {
        y_max += 2.5;
        y_min -= 2.5;
    }
    // Draw Axes
    int x_0 = (-from / (to - from)) * (double)(W - 1);
    double v_0 = (-y_min) / (y_max - y_min);
    int y_0 = lround((1.0 - v_0) * (double)(H - 1));

    plot__line(rgba, W, H, 0, y_0, W, y_0, 200, 200, 200, 255);
    plot__line(rgba, W, H, x_0, 0, x_0, H, 200, 200, 200, 255);

    for (int x = 0; x < W; ++x) {
        double yy = samples[x].y;

        double v = (yy - y_min) / (y_max - y_min);
        int y = lround((1.0 - v) * (double)(H - 1));

        // int y = (int)lround(yy * H);
        if (!has_prev) {
            prev_x = x;
            prev_y = y;
            has_prev = true;
            continue;
        }

        plot__line(rgba, W, H, prev_x, prev_y, x, y, 255, 0, 0, 255);
        prev_x = x;
        prev_y = y;
    }
    struct ncvisual *ncv = ncvisual_from_rgba(rgba, H, stride, W);
    if (!ncv) {
        ui_print(ui, "Error: OOM allocating plot visual.");
        return;
    }

    struct ncvisual_options vopts = {0};
    vopts.n = ui->viz;
    vopts.y = 1;
    vopts.x = 1;
    vopts.scaling = NCSCALE_SCALE;
    vopts.flags |= NCVISUAL_OPTION_CHILDPLANE;
    vopts.blitter = ui->has_bitmap ? NCBLIT_PIXEL : NCBLIT_8x1;

    ui->viz_obj = ncvisual_blit(ui->nc, ncv, &vopts);
    ncvisual_destroy(ncv);

    if (!ui->viz_obj) {
        ui_print(ui, "Error: OOM allocating plot visual.");
        return;
    }

    arena_destroy(&func_mem);
}

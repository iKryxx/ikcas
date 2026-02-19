//
// Created by ikryxxdev on 2/10/26.
//

#include "ui/plot.h"

#include <math.h>

static inline void plot__putpx(
    unsigned char* rgba, int w, int x, int y,
    unsigned char r, unsigned char g, unsigned char b, unsigned char a
) {
    unsigned char *p = rgba + (y * w + x) * 4;
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

static inline void plot__line(
    unsigned char* rgba, int W, int H,
    int x0, int y0, int x1, int y1,
    unsigned char r, unsigned char g, unsigned char b, unsigned char a
) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        if ((unsigned)x0 < (unsigned)W && (unsigned)y0 < (unsigned)H) {
            unsigned char *p = rgba + (y0 * W + x0) * 4;
            p[0] = r; p[1] = g; p[2] = b; p[3] = a;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void plot_demo(ui_t *ui) {
    ui_viz_clear(ui);
    ui_viz_title(ui, "Plot: y = sin(x)");

    const int W = 640;
    const int H = 320;
    const int stride = W * 4;

    unsigned char *rgba = (unsigned char*)malloc((size_t)stride * (size_t)H);
    if (!rgba) {
        ui_print(ui, "Error: OOM allocating plot buffer.");
        return;
    }
    memset(rgba, 0, (size_t)stride * (size_t)H);

    // axes
    int midx = W / 2;
    int midy = H / 2;

    for (int x = 0; x < W; ++x) { plot__putpx(rgba, W, x, midy, 200, 200, 200, 255); }
    for (int y = 0; y < H; ++y) { plot__putpx(rgba, W, midx, y, 200, 200, 200, 255); }

    int prev_x = 0, prev_y = 0;
    bool has_prev = false;

    for (int x = 0; x < W; ++x) {
        double t = (double)x / (double)(W -1);
        double xx = -M_PI + t * (2.0 * M_PI);
        double yy = sin(xx);
        int y = (int)lround((0.5 - 0.45 * yy) * (double)(H - 1));

        if (!has_prev) {
            prev_x = x; prev_y = y; has_prev = true; continue;
        }

        plot__line(rgba, W, H, prev_x, prev_y, x, y, 255, 0, 0, 255);
        prev_x = x; prev_y = y;
    }

    struct ncvisual* ncv = ncvisual_from_rgba(rgba, H, stride, W);
    free(rgba);
    if (!ncv) {
        ui_print(ui, "Error: OOM allocating plot visual.");
        return;
    }

    struct ncvisual_options vopts = { 0 };
    vopts.n = ui->viz;
    vopts.y = 1; vopts.x = 1;
    vopts.scaling = NCSCALE_SCALE;
    vopts.flags |= NCVISUAL_OPTION_CHILDPLANE;
    vopts.blitter = ui->has_bitmap ? NCBLIT_PIXEL : NCBLIT_8x1;

    ui->viz_obj = ncvisual_blit(ui->nc, ncv, &vopts);
    ncvisual_destroy(ncv);

    if (!ui->viz_obj) {
        ui_print(ui, "Error: OOM allocating plot visual.");
        return;
    }
}
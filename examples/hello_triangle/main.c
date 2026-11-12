// Hello world for chao: a spinning, vertex-colored triangle.
#include "../../chao/chao.h"

#define WIDTH  320
#define HEIGHT 240

static float angle = 0.0f;

static void init(void) {
}

static void update(float dt) {
    angle += dt * 1.5f; // radians per second

    fill(chao_canvas, 0x202030FF);

    // Canvas size can change when the window is resized, so recompute the center every frame.
    float cx = screen_size.x * 0.5f;
    float cy = screen_size.y * 0.5f;
    float radius = min(screen_size.x, screen_size.y) * 0.4f;

    int xs[3], ys[3];
    for (int i = 0; i < 3; ++i) {
        float a = angle + i * (2.0f * (float)M_PI / 3.0f);
        xs[i] = (int)(cx + cosf(a) * radius);
        ys[i] = (int)(cy + sinf(a) * radius);
    }

    fill_triangle_3c(chao_canvas,
        xs[0], ys[0],
        xs[1], ys[1],
        xs[2], ys[2],
        COLOR_RED, COLOR_GREEN, COLOR_BLUE);
}

int main(void) {
    chao_run("Hello Triangle", WIDTH, HEIGHT, &init, &update);
    return 0;
}

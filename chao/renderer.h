#ifndef CHAO_RENDERER_H
#define CHAO_RENDERER_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include "types.h"
#include "utils.h"
#define STB_IMAGE_IMPLEMENTATION
#include "3rd_party/stb_image.h"

// classic BGI palette ftw
#define COLOR_BLACK 0x000000FF
#define COLOR_BLUE 0x0000FFFF
#define COLOR_GREEN 0x00FF00FF
#define COLOR_CYAN 0x00FFFFFF
#define COLOR_RED 0xFF0000FF
#define COLOR_MAGENTA 0xFF00FFFF
#define COLOR_BROWN 0xA0522DFF
#define COLOR_LIGHTGRAY 0xC0C0C0FF
#define COLOR_DARKGRAY 0x808080FF
#define COLOR_LIGHTBLUE 0xADD8E6FF
#define COLOR_LIGHTGREEN 0x90EE90FF
#define COLOR_LIGHTCYAN 0xE0FFFFff
#define COLOR_LIGHTRED 0xFFB6C1FF
#define COLOR_LIGHTMAGENTA 0xFF00FFFF
#define COLOR_YELLOW 0xFFFF00FF
#define COLOR_WHITE 0xFFFFFFFF

#define COLOR_TRANSPARENT 0xFFFFFF00

#define RGBA(r, g, b, a) ((r << 24) | (g << 16) | (b << 8) | a)
#define FRGBA(r, g, b, a) (((r*256) << 24) | ((g*255) << 16) | ((b*255) << 8) | (a*255))
#define SET_ALPHA(color, alpha) ((color & 0xFFFFFF00) | ((int)((alpha)) & 0xFF))
#define SET_ALPHAF(color, alpha) (SET_ALPHA(color, alpha * 255))
#define screen_size chao_canvas->size
#define BMP_RECT(bitmap) (RectInt){0,0,bitmap->size.x,bitmap->size.y}

const uint32_t PIXEL_HARD_MASK = RGBA(00, 00, 00, 128);

Bitmap* chao_canvas;
BlendMode chao_blend_mode;
bool chao_hard_tint = false;

struct {
	bool enabled;
	uint32_t color;
	float start;
	float end;
} chao_3d_fog;

struct {
	bool enabled;
	Vector3 direction;
	float ambient;
} chao_3d_directional_light;

Bitmap* create_bitmap(int w, int h){
    Bitmap* bitmap = (Bitmap*)malloc(sizeof(Bitmap));
    bitmap->size.x = w;
    bitmap->size.y = h;
    bitmap->pixels = (uint32_t*)malloc(w * h * sizeof(uint32_t));
    bitmap->z_buffer = NULL;
    return bitmap;
}

Bitmap* create_bitmap_filled(int w, int h, uint32_t fill_color) {
    Bitmap* bitmap = create_bitmap(w, h);
	for (int i = 0; i < bitmap->size.x * bitmap->size.y; ++i) {
		bitmap->pixels[i] = fill_color;
	}
    return bitmap;
}

void create_bitmap_z_buffer(Bitmap* bitmap) {
	if (bitmap->z_buffer != NULL) {
		free(bitmap->z_buffer);
	}
	bitmap->z_buffer = (float*)malloc(sizeof(float) * bitmap->size.x * bitmap->size.y);
	for (int i = 0; i < bitmap->size.x * bitmap->size.y; ++i) {
		bitmap->z_buffer[i] = INFINITY;  // empty: everything is closer
	}
}

void resize_bitmap(Bitmap* bitmap, int w, int h) {
    if (bitmap->size.x == w && bitmap->size.y == h) {
        return;
    }
    if (bitmap->pixels) {
        free(bitmap->pixels);
    }
    bitmap->size.x = w;
    bitmap->size.y = h;
    bitmap->pixels = (uint32_t*)malloc(w * h * sizeof(uint32_t));
    if (bitmap->z_buffer) {
		create_bitmap_z_buffer(bitmap);
	}
}

void free_bitmap(Bitmap* bitmap) {
    if (bitmap) {
        if (bitmap->pixels) {
            free(bitmap->pixels);
            bitmap->pixels = NULL;
        }
        if (bitmap->z_buffer) {
        	free(bitmap->z_buffer);
        	bitmap->z_buffer = NULL;
		}
        free(bitmap);
    }
}


///////////////////////////////
//// PIXELS
///////////////////////////////
static inline uint32_t blend_pixel_alpha(uint32_t src, uint32_t dst) {
    uint32_t a1 = src & 0xFF;
    uint32_t inv = 255 - a1;
    uint32_t r = ((((src >> 24) & 0xFF) * a1) + (((dst >> 24) & 0xFF) * inv)) / 255;
    uint32_t g = ((((src >> 16) & 0xFF) * a1) + (((dst >> 16) & 0xFF) * inv)) / 255;
    uint32_t b = ((((src >>  8) & 0xFF) * a1) + (((dst >>  8) & 0xFF) * inv)) / 255;
    uint32_t a =  a1 + (((( dst & 0xFF) * inv)) / 255);
    return RGBA(r, g, b, a);
}

static inline uint32_t blend_pixel_additive(uint32_t src, uint32_t dst) {
    uint32_t r1 = (src >> 24) & 0xFF;
    uint32_t g1 = (src >> 16) & 0xFF;
    uint32_t b1 = (src >>  8) & 0xFF;
    uint32_t a1 =  src & 0xFF;
    uint32_t r2 = (dst >> 24) & 0xFF;
    uint32_t g2 = (dst >> 16) & 0xFF;
    uint32_t b2 = (dst >>  8) & 0xFF;
    uint32_t a2 =  dst & 0xFF;
    uint32_t r = r1 + r2; if (r > 255) r = 255;
    uint32_t g = g1 + g2; if (g > 255) g = 255;
    uint32_t b = b1 + b2; if (b > 255) b = 255;
    uint32_t a = a1 + a2; if (a > 255) a = 255;
    return (r << 24) | (g << 16) | (b << 8) | a;
}

static inline uint32_t blend_pixel_multiply(uint32_t src, uint32_t dst) {
    uint32_t rs = (src >> 24) & 0xFF;
    uint32_t gs = (src >> 16) & 0xFF;
    uint32_t bs = (src >>  8) & 0xFF;
    uint32_t as = src & 0xFF;
    uint32_t rd = (dst >> 24) & 0xFF;
    uint32_t gd = (dst >> 16) & 0xFF;
    uint32_t bd = (dst >>  8) & 0xFF;
    uint32_t ad = dst & 0xFF;
    uint32_t rm = (rd * rs) / 255;
    uint32_t gm = (gd * gs) / 255;
    uint32_t bm = (bd * bs) / 255;

    uint32_t r = rd + ((int)(rm - rd) * as) / 255;
    uint32_t g = gd + ((int)(gm - gd) * as) / 255;
    uint32_t b = bd + ((int)(bm - bd) * as) / 255;

    uint32_t a = as + ((ad * (255 - as)) / 255);

    return (r << 24) | (g << 16) | (b << 8) | a;
}

static inline uint32_t blend_pixel(uint32_t src, uint32_t dst, BlendMode blend_mode) {
    switch (blend_mode) {
        default:
        case BLEND_NONE: return src;
        case BLEND_ALPHA: return blend_pixel_alpha(src, dst);
        case BLEND_ADDITIVE: return blend_pixel_additive(src, dst);
        case BLEND_MULTIPLY: return blend_pixel_multiply(src, dst);
    }
}

// hard tint, tints everything including dark pixels
uint32_t tint_pixel(uint32_t base, uint32_t tint) {
    uint32_t alpha = tint & 0xFF;
    uint32_t inv_alpha = 255 - alpha;

    uint32_t rb = ((base & 0xFF00FF00) >> 8);
    uint32_t trb = ((tint & 0xFF00FF00) >> 8);

    uint32_t g = ((base & 0x00FF0000) >> 16);
    uint32_t tg = ((tint & 0x00FF0000) >> 16);

    uint32_t r_out = (((((base >> 24) & 0xFF) * inv_alpha) + (((tint >> 24) & 0xFF) * alpha)) >> 8);
    uint32_t g_out = (((g * inv_alpha) + (tg * alpha)) >> 8);
    uint32_t b_out = (((((base >> 8) & 0xFF) * inv_alpha) + (((tint >> 8) & 0xFF) * alpha)) >> 8);

    uint32_t a_out = base & 0xFF;

    return (r_out << 24) | (g_out << 16) | (b_out << 8) | a_out;
}

// this is a kinda soft tint, so black stays black and brighter a pixel is the more tint it gets
uint32_t modulate_pixel(uint32_t base, uint32_t modulate) {
    uint32_t r_base = (base >> 24) & 0xFF;
    uint32_t g_base = (base >> 16) & 0xFF;
    uint32_t b_base = (base >> 8)  & 0xFF;
    uint32_t a_base = base & 0xFF;
    
    uint32_t r_mod = (modulate >> 24) & 0xFF;
    uint32_t g_mod = (modulate >> 16) & 0xFF;
    uint32_t b_mod = (modulate >> 8)  & 0xFF;
    uint32_t a_mod = modulate & 0xFF;

    uint32_t r_out = (r_base * r_mod) / 255;
    uint32_t g_out = (g_base * g_mod) / 255;
    uint32_t b_out = (b_base * b_mod) / 255;
    
    uint32_t a_out = a_base;

    return (r_out << 24) | (g_out << 16) | (b_out << 8) | a_out;
}

uint32_t brighten_pixel(uint32_t base, float factor) {
    uint32_t r = (base >> 24) & 0xFF;
    uint32_t g = (base >> 16) & 0xFF;
    uint32_t b = (base >> 8)  & 0xFF;
    uint32_t a = base & 0xFF;
    
    r = r + (uint32_t)((255 - r) * factor);
    g = g + (uint32_t)((255 - g) * factor);
    b = b + (uint32_t)((255 - b) * factor);
    
    return (r << 24) | (g << 16) | (b << 8) | a;
}

void put_pixel(Bitmap* bitmap, int x, int y, uint32_t color) {
    if (x >= 0 && x < bitmap->size.x && y >= 0 && y < bitmap->size.y) {
        uint32_t* px = &bitmap->pixels[y * bitmap->size.x + x];
        *px = blend_pixel(color, *px, chao_blend_mode);
    }
}

uint32_t get_pixel(Bitmap* bitmap, int x, int y) {
    if (bitmap == NULL || x < 0 || x >= bitmap->size.x || y < 0 || y >= bitmap->size.y) {
        return 0xFF00FF00;
    }
    return bitmap->pixels[y * bitmap->size.x + x];
}

uint32_t get_pixel_clamped(Bitmap* bitmap, int x, int y) {
	x = clamp(x, 0, bitmap->size.x-1);
	y = clamp(y, 0, bitmap->size.y-1);
	return bitmap->pixels[y * bitmap->size.x + x];
}

uint32_t get_pixel_wrapped(Bitmap* bitmap, int x, int y) {
	x = wrap_int(x, 0, bitmap->size.x);
	y = wrap_int(y, 0, bitmap->size.y);
	return get_pixel(bitmap, x, y);
}

uint32_t get_pixel_nearest(Bitmap* bitmap, float x, float y) {
	x = round(x * (bitmap->size.x - 1));
	y = round(y * (bitmap->size.y - 1));
	return get_pixel(bitmap, x, y);
}

uint32_t get_pixel_bilinear(Bitmap* bitmap, float x, float y) {
	return 0x00000000;
}

///////////////////////////////
//// PRIMITIVES
///////////////////////////////

void fill(Bitmap* dst, uint32_t color) {
	if (dst->z_buffer != NULL){
		for (int i = 0; i < dst->size.x * dst->size.y; ++i) {
			dst->pixels[i] = color;
			dst->z_buffer[i] = INFINITY;
		}
	} else {
		for (int i = 0; i < dst->size.x * dst->size.y; ++i) {
			dst->pixels[i] = color;
		}
	}
}

void fill_rect(Bitmap* dst, int x, int y, int w, int h, uint32_t color) {
    for (int i = x; i < x + w; i++) {
        for (int j = y; j < y + h; j++) {
            put_pixel(dst, i, j, color);
        }
    }
}

void draw_line(Bitmap* dst, int x1, int y1, int x2, int y2, uint32_t color) {
	bool steep = abs(x1-x2) < abs(y1-y2);
	if (steep) {
		swap(int, x1, y1);
		swap(int, x2, y2);
	}
	if (x1 > x2) {
		swap(int, x1, x2);
		swap(int, y1, y2);
	}
	int y = y1;
	int ierror = 0;
	for (int x = x1; x <= x2; ++x) {
		if (steep) {
			put_pixel(dst, y, x, color);
		} else {
			put_pixel(dst, x, y, color);
		}
		ierror += 2 * abs(y2 - y1);
		y += (y2 > y1 ? 1 : -1) * (ierror > x2 - x1);
        ierror -= 2 * (x2-x1) * (ierror > x2 - x1);
	}
}

void draw_line_thick(Bitmap* dst, int x1, int y1, int x2, int y2, int thickness, uint32_t color) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len == 0) return;

    float nx = -dy / len;
    float ny = dx / len;
    float half = thickness / 2.0f;

    float ox = nx * half;
    float oy = ny * half;

    float vx[4] = {
        x1 + ox, x1 - ox,
        x2 - ox, x2 + ox
    };
    float vy[4] = {
        y1 + oy, y1 - oy,
        y2 - oy, y2 + oy
    };

    int minx = (int)fminf(fminf(vx[0], vx[1]), fminf(vx[2], vx[3]));
    int maxx = (int)fmaxf(fmaxf(vx[0], vx[1]), fmaxf(vx[2], vx[3]));
    int miny = (int)fminf(fminf(vy[0], vy[1]), fminf(vy[2], vy[3]));
    int maxy = (int)fmaxf(fmaxf(vy[0], vy[1]), fmaxf(vy[2], vy[3]));

    for (int y = miny; y <= maxy; ++y) {
        if (y < 0 || y >= dst->size.y) continue;
        for (int x = minx; x <= maxx; ++x) {
            if (x < 0 || x >= dst->size.x) continue;

            float w0 = (vx[1] - vx[0]) * (y - vy[0]) - (vy[1] - vy[0]) * (x - vx[0]);
            float w1 = (vx[2] - vx[1]) * (y - vy[1]) - (vy[2] - vy[1]) * (x - vx[1]);
            float w2 = (vx[3] - vx[2]) * (y - vy[2]) - (vy[3] - vy[2]) * (x - vx[2]);
            float w3 = (vx[0] - vx[3]) * (y - vy[3]) - (vy[0] - vy[3]) * (x - vx[3]);

            if ((w0 <= 0 && w1 <= 0 && w2 <= 0 && w3 <= 0) ||
                (w0 >= 0 && w1 >= 0 && w2 >= 0 && w3 >= 0)) {
                put_pixel(dst, x, y, color);
            }
        }
    }
}

void draw_rect(Bitmap* dst, int x, int y, int w, int h, int thickness, uint32_t color) {
    // top
    fill_rect(dst, x, y, w, thickness, color);
    // bottom
    fill_rect(dst, x, y+(h-thickness), w, thickness, color);
    // left
    fill_rect(dst, x, y+thickness, thickness, h-thickness*2, color);
    // right
    fill_rect(dst, x+(w-thickness), y+thickness, thickness, h-thickness*2, color);
}

void fill_circle(Bitmap* dst, int x, int y, int r, uint32_t color) {
	int r2 = r * r;
	for (int dy = -r; dy <= r; ++dy) {
		int y2 = dy * dy;
		if (y + dy >= 0 && y + dy < dst->size.y) {
			int x_offset = sqrt(r2 - y2);
			for (int dx = -x_offset; dx <= x_offset; ++dx) {
				put_pixel(dst, x+dx, y+dy, color);
			}
		}
	}
}

void fill_arc(Bitmap* dst, int cx, int cy, int r, int start_angle, int end_angle, uint32_t color) {
    while (start_angle < 0) start_angle += 360;
    while (end_angle < 0) end_angle += 360;
	start_angle %= 360;
    end_angle %= 360;

    int r2 = r * r;

    for (int dy = -r; dy <= r; ++dy) {
        int y = cy + dy;
        if (y < 0 || y >= dst->size.y) continue;

        for (int dx = -r; dx <= r; ++dx) {
            int x = cx + dx;
            if (x < 0 || x >= dst->size.x) continue;

            int d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;

            float angle = atan2f((float)dy, (float)dx) * 180.0f / M_PI;
            if (angle < 0) angle += 360.0f;

            if (start_angle < end_angle) {
                if (angle >= start_angle && angle <= end_angle) {
                    put_pixel(dst, x, y, color);
                }
            } else {
                if (angle >= start_angle || angle <= end_angle) {
                    put_pixel(dst, x, y, color);
                }
            }
        }
    }
}


///////////////////////////////
//// TRIANGLES
///////////////////////////////

// Triangles are rasterized with edge functions on a sub-pixel grid:
//  - vertices snap to 1/16 of a pixel instead of whole pixels, so shapes don't wobble
//  - a pixel is covered when its centre is inside the triangle
//  - the top-left fill rule decides pixels exactly on an edge, so triangles that share
//    an edge never leave gaps between them and never draw the same pixel twice
//  - edge functions are 64-bit, so large coordinates can't overflow
#define CHAO_SUBPIXEL_BITS 4
#define CHAO_SUBPIXEL_ONE (1 << CHAO_SUBPIXEL_BITS)
#define CHAO_SUBPIXEL_HALF (CHAO_SUBPIXEL_ONE / 2)

typedef struct {
    int64_t x[3], y[3];  // fixed-point vertices
    int64_t area;        // twice the signed area, in fixed-point units squared
} RasterTri;

typedef struct {
    int min_x, min_y, max_x, max_y;  // pixel bounds, clipped to the target
    int64_t a[3], b[3], c[3];        // edge i (opposite vertex i): e(x, y) = a*x + b*y + c
} RasterSetup;

static inline int64_t raster_fixed(float v) {
    return (int64_t)llroundf(v * (float)CHAO_SUBPIXEL_ONE);
}

// Snaps a triangle to the sub-pixel grid and computes its signed area.
// area > 0: clockwise on screen (y down), area < 0: counter-clockwise, 0: degenerate.
static inline RasterTri raster_tri(float x0, float y0, float x1, float y1, float x2, float y2) {
    RasterTri t;
    t.x[0] = raster_fixed(x0); t.y[0] = raster_fixed(y0);
    t.x[1] = raster_fixed(x1); t.y[1] = raster_fixed(y1);
    t.x[2] = raster_fixed(x2); t.y[2] = raster_fixed(y2);
    t.area = (t.x[1] - t.x[0]) * (t.y[2] - t.y[0]) - (t.y[1] - t.y[0]) * (t.x[2] - t.x[0]);
    return t;
}

// Prepares edge functions for a triangle with area > 0. Returns false if it covers no pixels of dst.
static bool raster_setup(const Bitmap* dst, const RasterTri* t, RasterSetup* s) {
    int64_t min_x = t->x[0], max_x = t->x[0], min_y = t->y[0], max_y = t->y[0];
    for (int i = 1; i < 3; ++i) {
        if (t->x[i] < min_x) min_x = t->x[i];
        if (t->x[i] > max_x) max_x = t->x[i];
        if (t->y[i] < min_y) min_y = t->y[i];
        if (t->y[i] > max_y) max_y = t->y[i];
    }
    // floor() to whole pixels; slightly conservative, the edge test does the exact work
    min_x >>= CHAO_SUBPIXEL_BITS; max_x >>= CHAO_SUBPIXEL_BITS;
    min_y >>= CHAO_SUBPIXEL_BITS; max_y >>= CHAO_SUBPIXEL_BITS;
    if (max_x < 0 || max_y < 0 || min_x >= dst->size.x || min_y >= dst->size.y) return false;
    s->min_x = min_x < 0 ? 0 : (int)min_x;
    s->min_y = min_y < 0 ? 0 : (int)min_y;
    s->max_x = max_x >= dst->size.x ? dst->size.x - 1 : (int)max_x;
    s->max_y = max_y >= dst->size.y ? dst->size.y - 1 : (int)max_y;

    for (int i = 0; i < 3; ++i) {
        int j = (i + 1) % 3, k = (i + 2) % 3;  // edge j -> k is opposite vertex i
        int64_t ax = t->x[j], ay = t->y[j], bx = t->x[k], by = t->y[k];
        s->a[i] = ay - by;
        s->b[i] = bx - ax;
        s->c[i] = (by - ay) * ax - (bx - ax) * ay;
        // Top-left rule (clockwise on a y-down screen): a top edge runs left to right,
        // a left edge runs upwards. Pixels exactly on any other edge belong to the neighbour.
        bool top = ay == by && bx > ax;
        bool left = by < ay;
        if (!top && !left) s->c[i] -= 1;
    }
    return true;
}

// Loops over every pixel whose centre is covered. In the body: px, py are the pixel and
// e0, e1, e2 the (integer) barycentric weights of vertices 0, 1, 2; they sum to roughly t.area.
#define CHAO_RASTER_LOOP(s, ...)                                                         \
    do {                                                                                  \
        const int64_t step = CHAO_SUBPIXEL_ONE;                                           \
        int64_t sx = (int64_t)(s).min_x * step + CHAO_SUBPIXEL_HALF;                      \
        int64_t sy = (int64_t)(s).min_y * step + CHAO_SUBPIXEL_HALF;                      \
        int64_t r0 = (s).a[0] * sx + (s).b[0] * sy + (s).c[0];                            \
        int64_t r1 = (s).a[1] * sx + (s).b[1] * sy + (s).c[1];                            \
        int64_t r2 = (s).a[2] * sx + (s).b[2] * sy + (s).c[2];                            \
        for (int py = (s).min_y; py <= (s).max_y; ++py) {                                 \
            int64_t e0 = r0, e1 = r1, e2 = r2;                                            \
            const int64_t dx0 = (s).a[0] * step, dx1 = (s).a[1] * step, dx2 = (s).a[2] * step; \
            /* increments live in the for header so the body may use `continue` */            \
            for (int px = (s).min_x; px <= (s).max_x; ++px, e0 += dx0, e1 += dx1, e2 += dx2) { \
                if ((e0 | e1 | e2) < 0) continue;                                         \
                __VA_ARGS__                                                               \
            }                                                                             \
            r0 += (s).b[0] * step; r1 += (s).b[1] * step; r2 += (s).b[2] * step;          \
        }                                                                                 \
    } while (0)

float signed_triangle_area(int x1, int y1, int x2, int y2, int x3, int y3) {
	return ((y2-y1)*(x2+x1) + (y3-y2)*(x3+x2) + (y1-y3)*(x1+x3)) * 0.5f;
}

RectInt get_tri_bounds(int x1, int y1, int x2, int y2, int x3, int y3) {
	return (RectInt) {
		min(min(x1, x2), x3),
		min(min(y1, y2), y3),
		max(max(x1, x2), x3),
		max(max(y1, y2), y3)
	};
}

// Integer pixel coordinates refer to pixel centres, so fill_triangle(.., 0,0, 10,0, 0,10, ..)
// covers the same pixels as the corresponding float triangle through (0.5,0.5), (10.5,0.5), (0.5,10.5).
void fill_triangle(Bitmap* dst, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color) {
    RasterTri t = raster_tri(x1 + 0.5f, y1 + 0.5f, x2 + 0.5f, y2 + 0.5f, x3 + 0.5f, y3 + 0.5f);
    if (t.area == 0) return;
    if (t.area < 0) {  // either winding is fine in 2D
        RasterTri f = { {t.x[0], t.x[2], t.x[1]}, {t.y[0], t.y[2], t.y[1]}, -t.area };
        t = f;
    }
    RasterSetup s;
    if (!raster_setup(dst, &t, &s)) return;
    CHAO_RASTER_LOOP(s, {
        uint32_t* p = &dst->pixels[py * dst->size.x + px];
        *p = blend_pixel(color, *p, chao_blend_mode);
    });
}

// Gouraud-shaded triangle: each vertex has its own colour.
void fill_triangle_3c(Bitmap* dst, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t c1, uint32_t c2, uint32_t c3) {
    RasterTri t = raster_tri(x1 + 0.5f, y1 + 0.5f, x2 + 0.5f, y2 + 0.5f, x3 + 0.5f, y3 + 0.5f);
    if (t.area == 0) return;
    if (t.area < 0) {
        RasterTri f = { {t.x[0], t.x[2], t.x[1]}, {t.y[0], t.y[2], t.y[1]}, -t.area };
        t = f;
        swap(uint32_t, c2, c3);
    }
    RasterSetup s;
    if (!raster_setup(dst, &t, &s)) return;

    float ch[3][4];
    uint32_t cs[3] = { c1, c2, c3 };
    for (int i = 0; i < 3; ++i) {
        ch[i][0] = (float)((cs[i] >> 24) & 0xFF);
        ch[i][1] = (float)((cs[i] >> 16) & 0xFF);
        ch[i][2] = (float)((cs[i] >> 8) & 0xFF);
        ch[i][3] = (float)(cs[i] & 0xFF);
    }
    float inv_area = 1.0f / (float)t.area;
    CHAO_RASTER_LOOP(s, {
        float l0 = (float)e0 * inv_area, l1 = (float)e1 * inv_area, l2 = 1.0f - l0 - l1;
        uint32_t out = 0;
        for (int k = 0; k < 4; ++k) {
            float v = ch[0][k] * l0 + ch[1][k] * l1 + ch[2][k] * l2 + 0.5f;
            uint32_t b = v <= 0.0f ? 0 : v >= 255.0f ? 255 : (uint32_t)v;
            out |= b << (24 - 8 * k);
        }
        uint32_t* p = &dst->pixels[py * dst->size.x + px];
        *p = blend_pixel(out, *p, chao_blend_mode);
    });
}

void fill_triangle_scanline(Bitmap* dst, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color) {
    if (y1 > y2) { swap(int, x1, x2); swap(int, y1, y2); }
    if (y1 > y3) { swap(int, x1, x3); swap(int, y1, y3); }
    if (y2 > y3) { swap(int, x2, x3); swap(int, y2, y3); }
    
    int total_height = y3 - y1;
    if (total_height == 0) return;
    
    y1 = ceil(y1);
    y3 = ceil(y3);

    for (int y = y1; y <= y3; y++) {
        bool second_half = y > y2 || y2 == y1;
        int segment_height = second_half ? (y3 - y2) : (y2 - y1);
        if (segment_height == 0) continue;

        float step_a = (float)(y - y1) / total_height;
        float step_b = (float)(y - (second_half ? y2 : y1)) / segment_height;

        int start_x = x1 + (x3 - x1) * step_a;
        int end_x = second_half
            ? x2 + (x3 - x2) * step_b
            : x1 + (x2 - x1) * step_b;

        if (start_x > end_x) { swap(int, start_x, end_x); }

        for (int x = ceil(start_x); x <= ceil(end_x); x++) {
            put_pixel(dst, x, y, color);
        }
    }
}

///////////////////////////////
//// 2D BLITTING
///////////////////////////////

void maskedcpy(uint32_t* dst, uint32_t* src, size_t pixels, bool flip) {
	register uint32_t *srcptr = src, *dstptr = dst;

	if (flip) {
    	while (pixels--) {
    		if (!(*srcptr&PIXEL_HARD_MASK)) {
    			dstptr ++;
    			srcptr --;
    			continue;
    		}
            *dstptr++ = *srcptr--;
    	}
	} else {
	   while (pixels--) {
    		if (!(*srcptr&PIXEL_HARD_MASK)) {
    			dstptr ++;
    			srcptr ++;
    			continue;
    		}
            *dstptr++ = *srcptr++;
    	}
    }
}

void blit_ex(Bitmap* dst, Bitmap* src, RectInt rect, Vector2Int pos, bool flip_h, bool flip_v, uint32_t modulate, float alpha) {
	if (dst == NULL || src == NULL) {
		printf("Trying to blit onto or a NULL here?\n");
		return;
	}
    rect.w = rect.w > 0 ? rect.w : src->size.x;
    rect.h = rect.h > 0 ? rect.h : src->size.y;

    if (flip_h) {
        rect.x += rect.w-1;
    }
    signed char src_v_dir = 1;
    if (flip_v) {
        src_v_dir = -1;
        rect.y += rect.h-1;
    }
    
    if (pos.x < 0) {
        if (flip_h){
            rect.x += pos.x;
        } else {
            rect.x -= pos.x;
        }
        rect.w += pos.x;
        pos.x = 0;
    }
    if (pos.y < 0) {
        if (flip_v){
            rect.y += pos.y;
        } else {
            rect.y -= pos.y;
        }
        rect.h += pos.y;
        pos.y = 0;
    }
    if (pos.x + rect.w >= dst->size.x) {
        rect.w = dst->size.x - pos.x;
    }
    if (pos.y + rect.h >= dst->size.y) {
        rect.h = dst->size.y - pos.y;
    }

    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    uint32_t* src_ptr = src->pixels + (rect.y * src->size.x) + rect.x;
    uint32_t* ptr = dst->pixels + (pos.y * dst->size.x) + pos.x;

	alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    uint8_t alpha_byte = (uint8_t)(alpha * 255.0f);
    
    if ((modulate == 0xffffffff  || (chao_hard_tint && (modulate&0xFF) == 0)) && chao_blend_mode == BLEND_NONE && alpha_byte == 255) {
    	// fast, no modulate and no blend path
        for (int y = 0; y < rect.h; ++y) {
            maskedcpy(ptr, src_ptr, rect.w, flip_h);
            src_ptr += src->size.x * src_v_dir;
            ptr += dst->size.x;
        }
    } else {
    	// slightly slower path with tints and blending
        signed char src_h_dir = flip_h ? -1 : 1;
        for (int y = 0; y < rect.h; ++y) {
            for (int x = 0; x < rect.w; ++x) {
            	int xi = flip_h ? -x : x;
				uint32_t src_pix = chao_hard_tint ? tint_pixel(*(src_ptr+xi), modulate) : modulate_pixel(*(src_ptr+xi), modulate);
            	if (chao_blend_mode == BLEND_NONE) {
					if (src_pix&PIXEL_HARD_MASK) {
						*(ptr+x) = src_pix;
					}
				} else {
                    src_pix = SET_ALPHA(src_pix, alpha * (src_pix & 0xFF));
                    *(ptr+x) = blend_pixel(src_pix, *(ptr+x), chao_blend_mode);
				}
            }
            src_ptr += src->size.x * src_v_dir;
            ptr += dst->size.x;
        }
    }
}


void blit_rect(Bitmap* dst, Bitmap* src, RectInt rect, Vector2Int pos, uint32_t modulate) {
	blit_ex(dst, src, rect, pos, false, false,  modulate, 1.0f);
}

void blit_tile(Bitmap* dst, Bitmap* src, int tile_size, int tile_index, int x, int y, bool flip_h, bool flip_v, uint32_t modulate) {
	int h_tiles = src->size.x / tile_size;
	int tile_x = (tile_index % h_tiles) * tile_size;
	int tile_y = (tile_index / h_tiles) * tile_size;
	blit_ex(dst, src, (RectInt){tile_x, tile_y, tile_size, tile_size}, (Vector2Int){x, y}, flip_h, flip_v, modulate, 1.0f);
}

void blit(Bitmap* dst, Bitmap* src, int x, int y, uint32_t modulate) {
	if (src == NULL) {
		printf("passed a NULL as src to blit dude!\n");
		return;
	}
	blit_ex(dst, src, (RectInt){0, 0, src->size.x, src->size.y}, (Vector2Int){x, y}, false, false, modulate, 1.0f);
}

void blit_transformed(Bitmap* dst, Bitmap* src, RectInt rect, Vector2Int position, Vector2 scale, float angle, Vector2 pivot, uint32_t modulate, float alpha) {
	rect.w = rect.w > 0 ? rect.w : src->size.x;
	rect.h = rect.h > 0 ? rect.h : src->size.y;
	pivot.x *= src->size.x;
	pivot.y *= src->size.y;

	alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);

	float c = cosf(angle);
	float s = sinf(angle);
	bool flip_x = scale.x < 0;
	bool flip_y = scale.y < 0;
	float sx = fabsf(scale.x);
	float sy = fabsf(scale.y);

	Vector2 corners[4] = {
		{0, 0},
		{(float)rect.w, 0},
		{(float)rect.w, (float)rect.h},
		{0, (float)rect.h}
	};

	float min_x =  1e9f, min_y =  1e9f;
	float max_x = -1e9f, max_y = -1e9f;

	for (int i = 0; i < 4; i++) {
		float x = corners[i].x - pivot.x;
		float y = corners[i].y - pivot.y;
		x *= sx;
		y *= sy;
		float tx = x * c - y * s + position.x;
		float ty = x * s + y * c + position.y;
		if (tx < min_x) min_x = tx;
		if (ty < min_y) min_y = ty;
		if (tx > max_x) max_x = tx;
		if (ty > max_y) max_y = ty;
	}

	int start_x = (int)floorf(min_x);
	int end_x = (int)ceilf(max_x);
	int start_y = (int)floorf(min_y);
	int end_y = (int)ceilf(max_y);

	start_x = max(start_x, 0);
	start_y = max(start_y, 0);
	end_x = min(end_x, dst->size.x - 1);
	end_y = min(end_y, dst->size.y - 1);

	for (int dy = start_y; dy <= end_y; dy++) {
		for (int dx = start_x; dx <= end_x; dx++) {
			float lx = dx - position.x;
			float ly = dy - position.y;

			float rx =  lx * c + ly * s;
			float ry = -lx * s + ly * c;

			rx /= sx;
			ry /= sy;

			rx += pivot.x;
			ry += pivot.y;

			if (rx < 0 || ry < 0 || rx >= rect.w || ry >= rect.h)
				continue;

			int sx_i = (int)(flip_x ? (rect.x + rect.w - 1 - (int)rx) : (rect.x + (int)rx));
			int sy_i = (int)(flip_y ? (rect.y + rect.h - 1 - (int)ry) : (rect.y + (int)ry));

			uint32_t src_pix = get_pixel(src, sx_i, sy_i);

			if (!(src_pix & PIXEL_HARD_MASK))
				continue;

			if (chao_hard_tint)
				src_pix = tint_pixel(src_pix, modulate);
			else
				src_pix = modulate_pixel(src_pix, modulate);

			if (alpha < 1.0f) {
				uint32_t a = src_pix & 0xFF;
				a = (uint32_t)(a * alpha);
				src_pix = (src_pix & 0xFFFFFF00) | a;
			}

			uint32_t* dst_ptr = &dst->pixels[dy * dst->size.x + dx];
			*dst_ptr = blend_pixel(src_pix, *dst_ptr, chao_blend_mode);
		}
	}
}

///////////////////////////////
//// 3D
///////////////////////////////

// Triangles are clipped against the near plane, so geometry crossing it gets cut instead of
// dropped. They are also clipped against a guard band this many viewports wide, so vertices
// far off-screen can't produce huge screen coordinates. Real screen edges are handled by the
// rasterizer's bounds.
#define CHAO_GUARD_BAND 16.0f
#define CHAO_CLIP_MAX_VERTS 9  // a triangle clipped by 5 planes has at most 3 + 5 vertices

typedef struct {
    Vector4 pos;  // clip space
    float u, v;
    Vector3 n;    // world-space normal
} ClipVertex;

typedef struct {
    float x, y;    // screen position in pixels
    float inv_w;   // 1 / view depth, interpolated linearly across the screen
    float u_w, v_w; // attributes pre-divided by depth for perspective-correct interpolation
    Vector3 n_w;
} ScreenVertex;

typedef struct {
    Bitmap* texture;
    bool lit;
    Vector3 light;  // normalized, pointing toward the light
    float ambient;
    bool fog;
    float fog_rgb[3];
    float fog_start, fog_inv_range;
} Shade3D;

// Plane 0 is the near plane, 1-4 the guard band. Inside when >= 0.
static inline float clip_distance(Vector4 p, int plane) {
    switch (plane) {
        case 0: return p.z + p.w;
        case 1: return CHAO_GUARD_BAND * p.w - p.x;
        case 2: return CHAO_GUARD_BAND * p.w + p.x;
        case 3: return CHAO_GUARD_BAND * p.w - p.y;
        default: return CHAO_GUARD_BAND * p.w + p.y;
    }
}

static inline ClipVertex clip_vertex_lerp(ClipVertex a, ClipVertex b, float t) {
    ClipVertex r;
    r.pos = vector4_lerp(a.pos, b.pos, t);
    r.u = lerp(a.u, b.u, t);
    r.v = lerp(a.v, b.v, t);
    r.n = (Vector3){ lerp(a.n.x, b.n.x, t), lerp(a.n.y, b.n.y, t), lerp(a.n.z, b.n.z, t) };
    return r;
}

// Sutherland-Hodgman against one plane. Returns the new vertex count.
static int clip_polygon(const ClipVertex* in, int count, ClipVertex* out, int plane) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const ClipVertex* a = &in[i];
        const ClipVertex* b = &in[(i + 1) % count];
        float da = clip_distance(a->pos, plane);
        float db = clip_distance(b->pos, plane);
        if (da >= 0.0f) out[n++] = *a;
        if ((da >= 0.0f) != (db >= 0.0f)) {
            // Always interpolate from the inside vertex toward the outside one, so two triangles
            // sharing this edge compute bit-identical points and stay watertight.
            if (da >= 0.0f) out[n++] = clip_vertex_lerp(*a, *b, da / (da - db));
            else            out[n++] = clip_vertex_lerp(*b, *a, db / (db - da));
        }
    }
    return n;
}

static inline ScreenVertex clip_to_screen(const Bitmap* dst, const ClipVertex* c) {
    float inv_w = 1.0f / c->pos.w;
    ScreenVertex s;
    s.x = (c->pos.x * inv_w * 0.5f + 0.5f) * (float)dst->size.x;
    s.y = (0.5f - c->pos.y * inv_w * 0.5f) * (float)dst->size.y;
    s.inv_w = inv_w;
    s.u_w = c->u * inv_w;
    s.v_w = c->v * inv_w;
    s.n_w = vector3_scale(c->n, inv_w);
    return s;
}

static void raster_triangle_3d(Bitmap* dst, const ScreenVertex* v0, const ScreenVertex* v1, const ScreenVertex* v2, const Shade3D* sh) {
    RasterTri t = raster_tri(v0->x, v0->y, v1->x, v1->y, v2->x, v2->y);
    // Front faces are counter-clockwise on screen (negative area here). Culling uses the same
    // snapped coordinates the rasterizer uses, so the two can never disagree.
    if (t.area >= 0) return;
    const ScreenVertex* v[3] = { v0, v2, v1 };  // swap to clockwise for the rasterizer
    RasterTri tc = { {t.x[0], t.x[2], t.x[1]}, {t.y[0], t.y[2], t.y[1]}, -t.area };
    RasterSetup s;
    if (!raster_setup(dst, &tc, &s)) return;

    float inv_area = 1.0f / (float)tc.area;
    float* zbuf = dst->z_buffer;
    Bitmap* tex = sh->texture;

    CHAO_RASTER_LOOP(s, {
        float l0 = (float)e0 * inv_area, l1 = (float)e1 * inv_area, l2 = 1.0f - l0 - l1;
        float inv_w = v[0]->inv_w * l0 + v[1]->inv_w * l1 + v[2]->inv_w * l2;
        float depth = 1.0f / inv_w;
        int index = py * dst->size.x + px;
        if (zbuf) {
            if (depth >= zbuf[index]) continue;
            zbuf[index] = depth;
        }

        uint32_t sample = 0xFF00FFFF;
        if (tex) {
            float u = (v[0]->u_w * l0 + v[1]->u_w * l1 + v[2]->u_w * l2) * depth;
            float w = (v[0]->v_w * l0 + v[1]->v_w * l1 + v[2]->v_w * l2) * depth;
            int tx = (int)(u * tex->size.x);
            int ty = (int)(w * tex->size.y);
            tx = tx < 0 ? 0 : tx >= tex->size.x ? tex->size.x - 1 : tx;
            ty = ty < 0 ? 0 : ty >= tex->size.y ? tex->size.y - 1 : ty;
            sample = tex->pixels[ty * tex->size.x + tx];
        }

        if (sh->lit || sh->fog) {
            float r = (float)((sample >> 24) & 0xFF);
            float g = (float)((sample >> 16) & 0xFF);
            float b = (float)((sample >> 8) & 0xFF);
            if (sh->lit) {
                Vector3 n = {
                    (v[0]->n_w.x * l0 + v[1]->n_w.x * l1 + v[2]->n_w.x * l2),
                    (v[0]->n_w.y * l0 + v[1]->n_w.y * l1 + v[2]->n_w.y * l2),
                    (v[0]->n_w.z * l0 + v[1]->n_w.z * l1 + v[2]->n_w.z * l2),
                };
                float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
                float ndotl = len > 0.0f ? vector3_dot(n, sh->light) / len : 0.0f;
                float light = sh->ambient + fmaxf(ndotl, 0.0f) * (1.0f - sh->ambient);
                r *= light; g *= light; b *= light;
            }
            if (sh->fog) {
                float f = clamp01((depth - sh->fog_start) * sh->fog_inv_range);
                r += (sh->fog_rgb[0] - r) * f;
                g += (sh->fog_rgb[1] - g) * f;
                b += (sh->fog_rgb[2] - b) * f;
            }
            sample = ((uint32_t)clamp(r, 0.0f, 255.0f) << 24) |
                     ((uint32_t)clamp(g, 0.0f, 255.0f) << 16) |
                     ((uint32_t)clamp(b, 0.0f, 255.0f) << 8) |
                     (sample & 0xFF);
        }

        dst->pixels[index] = blend_pixel(sample, dst->pixels[index], chao_blend_mode);
    });
}

void draw_model3d_m(Bitmap* dst, Camera3D* cam, Model3D* model, Matrix4 model_matrix) {
    Matrix4 mvp;
    MATRIX4_COPY(mvp, cam->projection);
    matrix4_mul(mvp, cam->view);
    matrix4_mul(mvp, model_matrix);

    Matrix4 normal_matrix;
    MATRIX4_COPY(normal_matrix, model_matrix);
    matrix4_fast_inverse_transpose_3x3(normal_matrix);

    Shade3D sh = {0};
    sh.texture = model->texture;
    sh.lit = chao_3d_directional_light.enabled;
    sh.light = vector3_get_normalized(chao_3d_directional_light.direction);
    sh.ambient = chao_3d_directional_light.ambient;
    sh.fog = chao_3d_fog.enabled;
    sh.fog_rgb[0] = (float)((chao_3d_fog.color >> 24) & 0xFF);
    sh.fog_rgb[1] = (float)((chao_3d_fog.color >> 16) & 0xFF);
    sh.fog_rgb[2] = (float)((chao_3d_fog.color >> 8) & 0xFF);
    sh.fog_start = chao_3d_fog.start;
    float fog_range = chao_3d_fog.end - chao_3d_fog.start;
    sh.fog_inv_range = fog_range > 0.0f ? 1.0f / fog_range : 0.0f;

    for (int i = 0; i < model->tris_count; i++) {
        int vi[3] = { model->tris[i].x, model->tris[i].y, model->tris[i].z };
        int ti[3] = { model->tri_uvs[i].x, model->tri_uvs[i].y, model->tri_uvs[i].z };
        int ni[3] = { model->tri_normals[i].x, model->tri_normals[i].y, model->tri_normals[i].z };

        ClipVertex cv[3];
        int outside_mask[5] = {0};
        for (int k = 0; k < 3; ++k) {
            Vector3 p = model->vertices[vi[k]];
            cv[k].pos = vector4_get_transformed((Vector4){ p.x, p.y, p.z, 1.0f }, mvp);
            Vector2 uv = ti[k] >= 0 ? model->uvs[ti[k]] : VECTOR2_ZERO;
            cv[k].u = uv.x;
            cv[k].v = uv.y;
            cv[k].n = VECTOR3_ZERO;
        }

        // Trivial reject when all three vertices are outside the same frustum plane.
        {
            int out_l = 0, out_r = 0, out_b = 0, out_t = 0, out_n = 0;
            for (int k = 0; k < 3; ++k) {
                Vector4 p = cv[k].pos;
                out_l += p.x < -p.w;
                out_r += p.x >  p.w;
                out_b += p.y < -p.w;
                out_t += p.y >  p.w;
                out_n += p.z < -p.w;
            }
            if (out_l == 3 || out_r == 3 || out_b == 3 || out_t == 3 || out_n == 3) continue;
        }

        if (sh.lit) {
            for (int k = 0; k < 3; ++k) {
                Vector3 n;
                if (ni[k] >= 0) {
                    n = model->normals[ni[k]];
                } else {  // no normals in the model: use the face normal
                    Vector3 a = model->vertices[vi[0]], b = model->vertices[vi[1]], c = model->vertices[vi[2]];
                    n = vector3_cross(vector3_subtract(b, a), vector3_subtract(c, a));
                }
                Vector4 t = vector4_get_transformed((Vector4){ n.x, n.y, n.z, 0.0f }, normal_matrix);
                cv[k].n = vector3_get_normalized((Vector3){ t.x, t.y, t.z });
            }
        }

        // Clip only when some vertex is behind the near plane or outside the guard band.
        ClipVertex buf_a[CHAO_CLIP_MAX_VERTS], buf_b[CHAO_CLIP_MAX_VERTS];
        ClipVertex* poly = cv;
        int count = 3;
        for (int plane = 0; plane < 5; ++plane) {
            bool needs_clip = false;
            for (int k = 0; k < count; ++k) {
                if (clip_distance(poly[k].pos, plane) < 0.0f) { needs_clip = true; break; }
            }
            if (!needs_clip) continue;
            ClipVertex* out = poly == buf_a ? buf_b : buf_a;
            count = clip_polygon(poly, count, out, plane);
            poly = out;
            if (count < 3) break;
        }
        if (count < 3) continue;

        ScreenVertex sv[CHAO_CLIP_MAX_VERTS];
        for (int k = 0; k < count; ++k) sv[k] = clip_to_screen(dst, &poly[k]);
        for (int k = 1; k + 1 < count; ++k) {
            raster_triangle_3d(dst, &sv[0], &sv[k], &sv[k + 1], &sh);
        }
    }
}

void draw_model3d(Bitmap* dst, Camera3D* camera, Model3D* model, Vector3 position, Vector3 rotation) {
	Matrix4 model_matrix;
    matrix4_identity(model_matrix);

    Matrix4 trans;
    matrix4_set_translation(trans, position.x, position.y, position.z);
    matrix4_mul(model_matrix, trans);

    Matrix4 rot;
    matrix4_set_euler(rot, rotation.x, rotation.y, rotation.z);
    matrix4_mul(model_matrix, rot);

	draw_model3d_m(dst, camera, model, model_matrix);
}

#endif // CHAO_RENDERER_H

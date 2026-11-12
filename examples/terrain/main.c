// Procedural terrain: a heightmap mesh and its texture are generated from noise at startup,
// then rendered with chao's software 3D pipeline (directional light + fog).
//
// Controls:
//   click          capture the mouse (mouse look)
//   Esc            release the mouse
//   W A S D        move
//   Space / E      up
//   Q / Ctrl       down
//   Shift          move faster
//   arrow keys     look around without the mouse
//   R              generate a new terrain
#include "../../chao/chao.h"

#define CANVAS_W 320
#define CANVAS_H 240
#define WINDOW_SCALE 3

#define GRID 128            // cells per side, the mesh has (GRID+1)^2 vertices
#define CELL_SIZE 1.0f      // world units per cell
#define TERRAIN_SIZE (GRID * CELL_SIZE)
#define HEIGHT_SCALE 18.0f
#define WATER_LEVEL -3.0f
#define TEXTURE_SIZE 384

#define SKY_COLOR 0x9CC3E6FF

#define MOVE_SPEED 12.0f
#define FAST_MULTIPLIER 4.0f
#define MOUSE_SENSITIVITY 0.0025f
#define KEY_LOOK_SPEED 1.8f
#define MIN_HEIGHT_ABOVE_GROUND 1.5f

typedef struct {
    Vector3 position;
    float yaw;   // radians, 0 looks toward -Z
    float pitch; // radians, positive looks up
} FreeCamera;

static Camera3D camera;
static FreeCamera fly;
static Model3D* terrain;
static Vector2 noise_offset;

///////////////////////////////
//// TERRAIN GENERATION
///////////////////////////////

static uint32_t rgb(float r, float g, float b) {
    return ((uint32_t)(clamp01(r) * 255.0f) << 24) |
           ((uint32_t)(clamp01(g) * 255.0f) << 16) |
           ((uint32_t)(clamp01(b) * 255.0f) << 8) | 0xFF;
}

static Vector3 mix3(Vector3 a, Vector3 b, float t) {
    t = clamp01(t);
    return (Vector3){ lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t) };
}

static float smoothstep(float e0, float e1, float x) {
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

// Raw terrain height at a world position, before the water plane flattens it.
static float ground_height(float x, float z) {
    float nx = x * 0.018f + noise_offset.x;
    float nz = z * 0.018f + noise_offset.y;
    float h = noise_fbm(nx, nz, 0.5f, 5, 2.0f, 0.5f);
    // Push the high parts up more than the low parts: wide valleys, sharper peaks.
    h = h > 0.0f ? h * (1.0f + h * 0.8f) : h * 0.8f;
    return h * HEIGHT_SCALE;
}

static float terrain_height(float x, float z) {
    return fmaxf(ground_height(x, z), WATER_LEVEL);
}

// Colour by height and slope, with a little high-frequency noise to break up the bands.
static uint32_t terrain_color(float x, float z) {
    float h = ground_height(x, z);
    float e = 0.5f;
    float slope = fabsf(ground_height(x + e, z) - ground_height(x - e, z)) +
                  fabsf(ground_height(x, z + e) - ground_height(x, z - e));
    float detail = noise_perlin(x * 0.6f, z * 0.6f, 3.7f) * 0.5f + 0.5f;

    const Vector3 deep    = {0.08f, 0.22f, 0.45f};
    const Vector3 shallow = {0.18f, 0.48f, 0.65f};
    const Vector3 sand    = {0.82f, 0.76f, 0.55f};
    const Vector3 grass1  = {0.30f, 0.55f, 0.22f};
    const Vector3 grass2  = {0.20f, 0.42f, 0.16f};
    const Vector3 rock    = {0.45f, 0.42f, 0.40f};
    const Vector3 snow    = {0.95f, 0.96f, 0.98f};

    Vector3 c;
    if (h < WATER_LEVEL) {
        c = mix3(deep, shallow, 1.0f + (h - WATER_LEVEL) / 6.0f);
    } else {
        c = mix3(grass1, grass2, detail);
        c = mix3(sand, c, smoothstep(WATER_LEVEL, WATER_LEVEL + 1.2f, h));
        c = mix3(c, rock, smoothstep(1.3f, 2.2f, slope));
        float snow_line = HEIGHT_SCALE * 0.6f + (detail - 0.5f) * 3.0f;
        c = mix3(c, snow, smoothstep(snow_line, snow_line + 1.5f, h) * (1.0f - smoothstep(2.0f, 3.0f, slope)));
        float shade = 0.9f + detail * 0.2f;
        c = vector3_multiply_scalar(c, shade);
    }
    return rgb(c.x, c.y, c.z);
}

static Model3D* terrain_generate(void) {
    const int side = GRID + 1;
    const int verts = side * side;
    const int tris = GRID * GRID * 2;
    const float half = TERRAIN_SIZE * 0.5f;

    Model3D* m = (Model3D*)malloc(sizeof(Model3D));
    m->vertices    = (Vector3*)malloc(sizeof(Vector3) * verts);
    m->normals     = (Vector3*)malloc(sizeof(Vector3) * verts);
    m->uvs         = (Vector2*)malloc(sizeof(Vector2) * verts);
    m->tris        = (Vector3Int*)malloc(sizeof(Vector3Int) * tris);
    m->tri_uvs     = (Vector3Int*)malloc(sizeof(Vector3Int) * tris);
    m->tri_normals = (Vector3Int*)malloc(sizeof(Vector3Int) * tris);
    m->tris_count  = tris;

    // Vertices on a grid centred on the origin; UVs span the whole texture.
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            float x = i * CELL_SIZE - half;
            float z = j * CELL_SIZE - half;
            int idx = j * side + i;
            m->vertices[idx] = (Vector3){ x, terrain_height(x, z), z };
            m->uvs[idx] = (Vector2){ (float)i / GRID, (float)j / GRID };
        }
    }

    // Normals from central differences of the height field.
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            float x = i * CELL_SIZE - half;
            float z = j * CELL_SIZE - half;
            float dx = terrain_height(x + CELL_SIZE, z) - terrain_height(x - CELL_SIZE, z);
            float dz = terrain_height(x, z + CELL_SIZE) - terrain_height(x, z - CELL_SIZE);
            m->normals[j * side + i] = vector3_get_normalized((Vector3){ -dx, 2.0f * CELL_SIZE, -dz });
        }
    }

    // Two counter-clockwise (seen from above) triangles per cell.
    int t = 0;
    for (int j = 0; j < GRID; ++j) {
        for (int i = 0; i < GRID; ++i) {
            int a = j * side + i;     // (i,   j)
            int b = a + 1;            // (i+1, j)
            int c = a + side;         // (i,   j+1)
            int d = c + 1;            // (i+1, j+1)
            Vector3Int t0 = { a, c, b };
            Vector3Int t1 = { b, c, d };
            m->tris[t] = m->tri_uvs[t] = m->tri_normals[t] = t0; ++t;
            m->tris[t] = m->tri_uvs[t] = m->tri_normals[t] = t1; ++t;
        }
    }

    // Texture sampled straight from the noise, so it has more detail than the mesh.
    m->texture = create_bitmap(TEXTURE_SIZE, TEXTURE_SIZE);
    for (int y = 0; y < TEXTURE_SIZE; ++y) {
        for (int x = 0; x < TEXTURE_SIZE; ++x) {
            float wx = ((float)x / (TEXTURE_SIZE - 1)) * TERRAIN_SIZE - half;
            float wz = ((float)y / (TEXTURE_SIZE - 1)) * TERRAIN_SIZE - half;
            m->texture->pixels[y * TEXTURE_SIZE + x] = terrain_color(wx, wz);
        }
    }

    return m;
}

static void terrain_free(Model3D* m) {
    if (!m) return;
    free_bitmap(m->texture);
    model3d_free(m);
    free(m);
}

static void regenerate(void) {
    noise_offset = (Vector2){ (float)(rand() % 1000) + 0.37f, (float)(rand() % 1000) + 0.71f };
    terrain_free(terrain);
    terrain = terrain_generate();

    // Start above the edge of the map, looking across it.
    float z = TERRAIN_SIZE * 0.5f - 4.0f;
    fly.position = (Vector3){ 0.0f, fmaxf(HEIGHT_SCALE * 1.2f, terrain_height(0.0f, z) + 4.0f), z };
    fly.yaw = 0.0f;
    fly.pitch = -0.35f;
}

///////////////////////////////
//// CAMERA
///////////////////////////////

static Vector3 camera_forward(const FreeCamera* c) {
    return (Vector3){
        cosf(c->pitch) * sinf(c->yaw),
        sinf(c->pitch),
        -cosf(c->pitch) * cosf(c->yaw),
    };
}

static void camera_update(float dt) {
    if (input.mouse.just_pressed && !platform_is_mouse_locked()) {
        platform_set_mouse_locked(true);
    } else if (input.just_pressed[KEY_ESC]) {
        platform_set_mouse_locked(false);
    }

    // Look
    if (platform_is_mouse_locked()) {
        fly.yaw   += input.mouse.dx * MOUSE_SENSITIVITY;
        fly.pitch -= input.mouse.dy * MOUSE_SENSITIVITY;
    }
    if (input.pressed[KEY_LEFT])  fly.yaw   -= KEY_LOOK_SPEED * dt;
    if (input.pressed[KEY_RIGHT]) fly.yaw   += KEY_LOOK_SPEED * dt;
    if (input.pressed[KEY_UP])    fly.pitch += KEY_LOOK_SPEED * dt;
    if (input.pressed[KEY_DOWN])  fly.pitch -= KEY_LOOK_SPEED * dt;
    fly.pitch = clamp(fly.pitch, -1.5f, 1.5f);

    // Move along the view direction (free flight), up/down along world Y.
    Vector3 forward = camera_forward(&fly);
    Vector3 right = vector3_get_normalized(vector3_cross(forward, VECTOR3_UP));
    Vector3 move = VECTOR3_ZERO;
    if (input.pressed[KEY_W]) move = vector3_add(move, forward);
    if (input.pressed[KEY_S]) move = vector3_subtract(move, forward);
    if (input.pressed[KEY_D]) move = vector3_add(move, right);
    if (input.pressed[KEY_A]) move = vector3_subtract(move, right);
    if (input.pressed[KEY_SPACE] || input.pressed[KEY_E]) move.y += 1.0f;
    if (input.pressed[KEY_Q] || input.pressed[KEY_LCONTROL]) move.y -= 1.0f;

    if (vector3_length(move) > 0.0f) {
        float speed = MOVE_SPEED * (input.pressed[KEY_LSHIFT] ? FAST_MULTIPLIER : 1.0f);
        move = vector3_scale(vector3_get_normalized(move), speed * dt);
        fly.position = vector3_add(fly.position, move);
    }

    // Stay above the ground: the renderer drops triangles that cross the near plane,
    // so getting too close would punch holes in the terrain.
    float ground = terrain_height(fly.position.x, fly.position.z);
    fly.position.y = fmaxf(fly.position.y, ground + MIN_HEIGHT_ABOVE_GROUND);

    camera.position = fly.position;
    camera.target = vector3_add(fly.position, forward);
    camera.up = VECTOR3_UP;
    camera3d_update(&camera);
    camera3d_set_perspective(camera.projection, deg_to_rad(70.0f),
                             (float)screen_size.x / (float)screen_size.y, 0.1f, 200.0f);
}

///////////////////////////////
//// APP
///////////////////////////////

static void init(void) {
    platform_set_window_size(CANVAS_W * WINDOW_SCALE, CANVAS_H * WINDOW_SCALE);

    chao_3d_directional_light.enabled = true;
    chao_3d_directional_light.direction = vector3_get_normalized((Vector3){ -0.5f, 0.8f, -0.3f }); // toward the sun
    chao_3d_directional_light.ambient = 0.45f;

    chao_3d_fog.enabled = true;
    chao_3d_fog.color = SKY_COLOR;
    chao_3d_fog.start = 30.0f;
    chao_3d_fog.end = 100.0f;

    regenerate();

    printf("Click to capture the mouse, Esc to release.\n"
           "WASD move, Space/E up, Q/Ctrl down, Shift faster, arrows look, R new terrain.\n");
}

static void update(float dt) {
    if (input.just_pressed[KEY_R]) regenerate();
    camera_update(dt);

    fill(chao_canvas, SKY_COLOR); // also clears the depth buffer
    draw_model3d(chao_canvas, &camera, terrain, VECTOR3_ZERO, VECTOR3_ZERO);
}

int main(void) {
    chao_run("Terrain", CANVAS_W, CANVAS_H, &init, &update);
    terrain_free(terrain);
    return 0;
}

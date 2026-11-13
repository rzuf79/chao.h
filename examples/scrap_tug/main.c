// Scrap Tug: an entire little salvage game in one translation unit.
// All artwork is drawn in code; the only asset is chao's bundled bitmap font.
//
// W / Up       thrust             A D / Left Right   turn
// S / Down     reverse            Shift              boost
// Space        brake              E                  attach / release cable
// Q            reel cable in      1 / 2 / 3           buy upgrades at the dock
// Esc          pause              R                  retry after a shift
//
// Tow three wrecks into the refinery. Cargo has mass: brake early, swing it
// around asteroids, or swing it INTO pirates. The refinery is a safe zone.
#include "resources.h"
#include "../../chao/chao.h"

#define VIEW_W 640
#define VIEW_H 360
#define TAU 6.28318530718f
#define FIXED_DT (1.0f / 120.0f)
#define WORLD_X 1350.0f
#define WORLD_Y 1000.0f
#define CARGO_COUNT 9
#define ROCK_COUNT 32
#define PIRATE_COUNT 5
#define SHOT_COUNT 48
#define FX_COUNT 640
#define TEXT_COUNT 14
#define QUOTA 3
#define DOCK_RADIUS 94.0f
#define SAFE_RADIUS 235.0f
#define HOOK_RANGE 128.0f

#define INK    0x080F1BFF
#define PANEL  0x101D2CFF
#define EDGE   0x2A4054FF
#define MUTED  0x7D9AAFFF
#define WHITE  0xDDEAF1FF
#define TEAL   0x70F2CAFF
#define GOLD   0xFFCB70FF
#define RED    0xFF6B78FF
#define BLUE   0x73CDEFFF

typedef enum { TITLE, PLAY, PAUSE, WON, LOST } State;
typedef struct { Vector2 p, v; float r, mass, angle, spin, hp; bool active; } Body;
typedef struct { Body b; int value, kind; float max_hp; } Cargo;
typedef struct { Vector2 p; float r, shape[9], angle; } Rock;
typedef struct { Body b; float cooldown, flash; } Pirate;
typedef struct { Vector2 p, v; float life; } Shot;
typedef struct { Vector2 p, v; float life, max_life, size; uint32_t color; } Fx;
typedef struct { Vector2 p; float life; uint32_t color; char text[64]; } FloatText;

static State state = TITLE;
static Body tug;
static Cargo cargo[CARGO_COUNT];
static Rock rocks[ROCK_COUNT];
static Pirate pirates[PIRATE_COUNT];
static Shot shots[SHOT_COUNT];
static Fx effects[FX_COUNT];
static FloatText float_text[TEXT_COUNT];
static AsciiFont *font, *small_font;
static Bitmap *nebula, *lettering;
static Vector2 camera, camera_draw;
static float clock_time, elapsed, fuel, hull, invulnerable, shake;
static float accumulator, next_pirate, rope_length, rope_stress, tension;
static float toast_time, delivery_flash, thrust_visual;
static int hooked = -1, delivered, credits, engines, armor, kills, best;
static int fx_cursor, text_cursor;
static bool boosting, braking;
static char toast[128], loss_reason[96];
static uint32_t rng = 1;

// Keeping simulation randomness here makes a shift reproducible with a seed.
static uint32_t random_u32(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
static float random_float(float lo, float hi) {
    return lo + (hi - lo) * (random_u32() & 0xFFFFFF) / 16777216.0f;
}
static Vector2 v2(float x, float y) { return (Vector2){x, y}; }
static Vector2 add(Vector2 a, Vector2 b) { return vector2_add(a, b); }
static Vector2 sub(Vector2 a, Vector2 b) { return vector2_subtract(a, b); }
static Vector2 mul(Vector2 a, float s) { return vector2_scale(a, s); }
static float length(Vector2 a) { return vector2_length(a); }
static float distance(Vector2 a, Vector2 b) { return length(sub(a, b)); }
static Vector2 direction(float a) { return v2(cosf(a), sinf(a)); }
static Vector2 unit(Vector2 a) {
    float d = length(a);
    return d > 0.0001f ? mul(a, 1.0f / d) : v2(1, 0);
}
static Vector2 rotate(Vector2 p, float a) {
    return v2(p.x * cosf(a) - p.y * sinf(a), p.x * sinf(a) + p.y * cosf(a));
}
static float angle_difference(float a, float b) { return atan2f(sinf(a - b), cosf(a - b)); }
static uint32_t fade_color(uint32_t c, float f) {
    f = clamp01(f);
    return RGBA((int)((c >> 24) * f), (int)(((c >> 16) & 255) * f),
                (int)(((c >> 8) & 255) * f), 255);
}
static Vector2 screen(Vector2 p) {
    return v2(p.x - camera_draw.x + screen_size.x * 0.5f,
              p.y - camera_draw.y + screen_size.y * 0.5f);
}
static bool visible(Vector2 p, float r) {
    Vector2 s = screen(p);
    return s.x > -r && s.y > -r && s.x < screen_size.x + r && s.y < screen_size.y + r;
}
static void line(Vector2 a, Vector2 b, uint32_t c) {
    draw_line(chao_canvas, (int)a.x, (int)a.y, (int)b.x, (int)b.y, c);
}
static void world_line(Vector2 a, Vector2 b, uint32_t c) { line(screen(a), screen(b), c); }
static void ring(Vector2 p, float r, uint32_t c, int segments) {
    Vector2 prev = add(p, v2(r, 0));
    for (int i = 1; i <= segments; ++i) {
        Vector2 next = add(p, mul(direction(TAU * i / segments), r));
        line(prev, next, c); prev = next;
    }
}
static void text(int x, int y, uint32_t c, const char* fmt, ...) {
    char buffer[256];
    va_list args; va_start(args, fmt); vsnprintf(buffer, sizeof(buffer), fmt, args); va_end(args);
    ascii_font_draw(chao_canvas, small_font, x, y, c, "%s", buffer);
}
static void centered(int x, int y, uint32_t c, const char* msg) {
    Vector2Int size = ascii_font_get_size(small_font, msg, -1);
    text(x - size.x / 2, y, c, "%s", msg);
}
static void large_text(int x, int y, int scale, uint32_t c, const char* msg) {
    fill(lettering, 0);
    ascii_font_draw(lettering, font, 0, 0, c, "%s", msg);
    RectInt rect = {0, 0, min(320, ascii_font_get_size(font, msg, -1).x), 8};
    blit_transformed(chao_canvas, lettering, rect, (Vector2Int){x, y},
                     v2(scale, scale), 0, v2(0, 0), COLOR_WHITE, 1);
}
static void panel(int x, int y, int w, int h) {
    fill_rect(chao_canvas, x + 3, y + 4, w, h, INK);
    fill_rect(chao_canvas, x, y, w, h, PANEL);
    draw_rect(chao_canvas, x, y, w, h, 1, EDGE);
}
static void notify(const char* msg) {
    snprintf(toast, sizeof(toast), "%s", msg); toast_time = 4.0f;
}
static void floating(Vector2 p, uint32_t color, const char* fmt, ...) {
    FloatText* t = &float_text[text_cursor++ % TEXT_COUNT];
    t->p = p; t->life = 2.5f; t->color = color;
    va_list args; va_start(args, fmt); vsnprintf(t->text, sizeof(t->text), fmt, args); va_end(args);
}
static void particle(Vector2 p, Vector2 v, float life, float size, uint32_t c) {
    effects[fx_cursor++ % FX_COUNT] = (Fx){p, v, life, life, size, c};
}
static void burst(Vector2 p, Vector2 velocity, int count, uint32_t c) {
    for (int i = 0; i < count; ++i)
        particle(p, add(mul(velocity, 0.2f), mul(direction(random_float(0, TAU)), random_float(15, 130))),
                 random_float(0.25f, 0.9f), random_float(1, 3), c);
}
static void hurt_tug(float amount) {
    if (invulnerable > 0 || state != PLAY || amount <= 0) return;
    hull = fmaxf(0, hull - amount * (armor ? 0.60f : 1.0f));
    invulnerable = 0.75f; shake = 5;
    burst(tug.p, tug.v, 18, RED);
}
static void release_cable(const char* msg) {
    if (hooked < 0) return;
    hooked = -1; tension = rope_stress = 0;
    if (msg) notify(msg);
}
static int nearest_cargo(float range) {
    int found = -1;
    for (int i = 0; i < CARGO_COUNT; ++i) {
        float d = distance(tug.p, cargo[i].b.p);
        if (cargo[i].b.active && d < range) { range = d; found = i; }
    }
    return found;
}
static void toggle_hook(void) {
    if (hooked >= 0) { release_cable("CABLE RELEASED"); return; }
    int i = nearest_cargo(HOOK_RANGE);
    if (i < 0) { notify("NO WRECK IN RANGE  /  GET CLOSER THAN 128m"); return; }
    hooked = i;
    rope_length = clamp(distance(tug.p, cargo[i].b.p), 56.0f, 108.0f);
    rope_stress = tension = 0;
    burst(cargo[i].b.p, cargo[i].b.v, 10, TEAL);
    notify("CABLE LOCKED  /  Q REELS IN  /  SPACE BRAKES");
}

static void spawn_pirate(void) {
    for (int i = 0; i < PIRATE_COUNT; ++i) {
        if (pirates[i].b.active) continue;
        Vector2 p = v2(0, 0);
        for (int tries = 0; tries < 20; ++tries) {
            p = add(tug.p, mul(direction(random_float(0, TAU)), 570));
            p.x = clamp(p.x, -WORLD_X + 60, WORLD_X - 60);
            p.y = clamp(p.y, -WORLD_Y + 60, WORLD_Y - 60);
            if (length(p) > SAFE_RADIUS + 100) break;
        }
        pirates[i] = (Pirate){.b = {.p = p, .r = 13, .mass = 1.7f,
            .hp = 52, .active = true}, .cooldown = 2.0f};
        notify("PIRATE INBOUND  /  YOUR CARGO HITS HARDER THAN YOU DO");
        return;
    }
}
static void kill_pirate(Pirate* p, bool cargo_hit) {
    if (!p->b.active) return;
    p->b.active = false; ++kills;
    credits += cargo_hit ? 75 : 30;
    burst(p->b.p, p->b.v, 48, RED); burst(p->b.p, p->b.v, 20, GOLD);
    floating(p->b.p, GOLD, cargo_hit ? "WRECKING BALL! +$75" : "PIRATE DOWN +$30");
    shake = fmaxf(shake, 3);
    if (cargo_hit) notify("WRECKING BALL!  /  THAT'S ONE WAY TO TAKE OUT THE TRASH");
}
static void deliver(int i) {
    Cargo* c = &cargo[i];
    int payout = (int)(c->value * (0.5f + 0.5f * clamp01(c->b.hp / c->max_hp)));
    if (hooked == i) release_cable(NULL);
    c->b.active = false;
    credits += payout; ++delivered;
    fuel = fminf(100, fuel + 12); hull = fminf(100, hull + 20);
    delivery_flash = 1;
    burst(c->b.p, v2(0, 0), 60, TEAL);
    floating(c->b.p, TEAL, "SALVAGED +$%d", payout);
    notify("DELIVERY ACCEPTED  /  +12 FUEL  +20 HULL  /  DOCK SHOP: 1 2 3");
    if (delivered == 1) next_pirate = fminf(next_pirate, elapsed + 7);
    if (delivered >= QUOTA) {
        state = WON;
        if (credits > best) best = credits;
    }
}
static void buy_upgrade(int item) {
    if (length(tug.p) > 155 || length(tug.v) > 45) {
        notify("SHOP OFFLINE  /  STOP NEAR THE REFINERY TO BUY"); return;
    }
    int cost = item == 1 ? 90 : item == 2 ? 70 : 35;
    if ((item == 1 && engines) || (item == 2 && armor)) { notify("UPGRADE ALREADY FITTED"); return; }
    if (item == 3 && fuel >= 99 && hull >= 99) { notify("FUEL AND HULL ALREADY FULL"); return; }
    if (credits < cost) { notify("NOT ENOUGH CREDITS  /  SALVAGE ANOTHER WRECK"); return; }
    credits -= cost;
    if (item == 1) { engines = 1; notify("HOT ENGINE FITTED  /  +30 PERCENT THRUST"); }
    if (item == 2) { armor = 1; hull = 100; notify("ARMOR FITTED  /  40 PERCENT LESS DAMAGE"); }
    if (item == 3) { fuel = hull = 100; notify("REFUELED AND REPAIRED  /  BACK TO WORK"); }
    burst(tug.p, v2(0, 0), 25, TEAL);
}

static void reset_shift(uint32_t seed) {
    rng = seed ? seed : 1;
    memset(cargo, 0, sizeof(cargo)); memset(pirates, 0, sizeof(pirates));
    memset(shots, 0, sizeof(shots)); memset(effects, 0, sizeof(effects));
    memset(float_text, 0, sizeof(float_text));
    tug = (Body){.p = {0, 166}, .r = 11, .mass = 1.4f, .angle = -TAU / 4, .hp = 100, .active = true};
    camera = tug.p; camera_draw = camera;
    fuel = hull = 100; elapsed = accumulator = invulnerable = shake = 0;
    hooked = -1; delivered = credits = engines = armor = kills = 0;
    fx_cursor = text_cursor = 0;
    rope_length = 90; rope_stress = tension = delivery_flash = thrust_visual = 0;
    boosting = braking = false; next_pirate = 30;
    loss_reason[0] = 0;
    // The first light wreck teaches towing; the rest form a scattered salvage field.
    const Vector2 positions[CARGO_COUNT] = {
        {230, 180}, {-440, -390}, {600, -430}, {-650, 400}, {850, 420},
        {80, -870}, {-1030, -600}, {1110, -130}, {-120, 900}
    };
    for (int i = 0; i < CARGO_COUNT; ++i) {
        int kind = i % 3;
        float hp = 65 + kind * 30;
        cargo[i] = (Cargo){.b = {.p = positions[i], .r = 16 + kind * 6,
            .mass = 2.2f + kind * 1.8f, .angle = random_float(0, TAU),
            .spin = random_float(-0.15f, 0.15f), .hp = hp, .active = true},
            .value = 140 + kind * 85, .kind = kind, .max_hp = hp};
    }
    for (int i = 0; i < ROCK_COUNT; ++i) {
        Rock* r = &rocks[i];
        r->r = random_float(20, 49); r->angle = random_float(0, TAU);
        for (int k = 0; k < 9; ++k) r->shape[k] = random_float(0.72f, 1.0f);
        for (int tries = 0; tries < 500; ++tries) {
            r->p = v2(random_float(-WORLD_X + 70, WORLD_X - 70), random_float(-WORLD_Y + 70, WORLD_Y - 70));
            bool clear = length(r->p) > 280;
            for (int j = 0; j < CARGO_COUNT; ++j)
                if (distance(r->p, cargo[j].b.p) < r->r + 135) clear = false;
            for (int j = 0; j < i; ++j)
                if (distance(r->p, rocks[j].p) < r->r + rocks[j].r + 35) clear = false;
            if (clear) break;
        }
    }
    notify("SHIFT START  /  E TO HOOK THE NEARBY WRECK  /  DELIVER 3 TO GET PAID");
}

static void title_scene(void) {
    state = TITLE; camera = camera_draw = v2(-165, 0);
    tug.p = v2(20, 95); tug.angle = -2.95f;
    cargo[0].b.p = v2(112, 108); hooked = 0; rope_length = 94;
}

// Circle collisions use inverse mass impulses; a loaded tug feels very different.
// The return value is impact speed, independent of mass, for damage calculations.
static float collide(Body* a, Body* b) {
    Vector2 delta = sub(b->p, a->p);
    float d = length(delta), overlap = a->r + b->r - d;
    if (overlap <= 0) return 0;
    Vector2 n = d > 0.001f ? mul(delta, 1 / d) : v2(1, 0);
    float ia = 1 / a->mass, ib = b->mass > 0 ? 1 / b->mass : 0;
    a->p = sub(a->p, mul(n, overlap * ia / (ia + ib)));
    b->p = add(b->p, mul(n, overlap * ib / (ia + ib)));
    float relative = vector2_dot(sub(b->v, a->v), n);
    if (relative >= 0) return 0;
    float impulse = -1.4f * relative / (ia + ib);
    a->v = sub(a->v, mul(n, impulse * ia));
    b->v = add(b->v, mul(n, impulse * ib));
    return -relative;
}
static void move_body(Body* b, float dt, float drag, float max_speed) {
    b->v = mul(b->v, expf(-drag * dt));
    float speed = length(b->v);
    if (speed > max_speed) b->v = mul(b->v, max_speed / speed);
    b->p = add(b->p, mul(b->v, dt)); b->angle += b->spin * dt;
    if (fabsf(b->p.x) > WORLD_X - b->r) {
        b->p.x = clamp(b->p.x, -WORLD_X + b->r, WORLD_X - b->r); b->v.x *= -0.5f;
    }
    if (fabsf(b->p.y) > WORLD_Y - b->r) {
        b->p.y = clamp(b->p.y, -WORLD_Y + b->r, WORLD_Y - b->r); b->v.y *= -0.5f;
    }
}
static void update_effects(float dt) {
    for (int i = 0; i < FX_COUNT; ++i) if (effects[i].life > 0) {
        Fx* f = &effects[i]; f->life -= dt;
        f->p = add(f->p, mul(f->v, dt)); f->v = mul(f->v, expf(-1.8f * dt));
    }
    for (int i = 0; i < TEXT_COUNT; ++i) if (float_text[i].life > 0) {
        float_text[i].life -= dt; float_text[i].p.y -= dt * 15;
    }
}
static void physics(float dt) {
    elapsed += dt;
    invulnerable = fmaxf(0, invulnerable - dt);
    toast_time = fmaxf(0, toast_time - dt);
    delivery_flash = fmaxf(0, delivery_flash - dt);
    shake = fmaxf(0, shake - dt * 10);
    update_effects(dt);

    float turn = (input.pressed[KEY_D] || input.pressed[KEY_RIGHT]) -
                 (input.pressed[KEY_A] || input.pressed[KEY_LEFT]);
    float thrust = (input.pressed[KEY_W] || input.pressed[KEY_UP]) ? 1.0f : 0.0f;
    if (input.pressed[KEY_S] || input.pressed[KEY_DOWN]) thrust -= 0.55f;
    tug.angle += turn * 3.25f * dt;
    boosting = input.pressed[KEY_LSHIFT] && thrust > 0 && fuel > 0;
    braking = input.pressed[KEY_SPACE];
    thrust_visual = fuel > 0 ? thrust : 0;
    float power = 190 * (engines ? 1.3f : 1.0f) * (boosting ? 1.85f : 1.0f);
    if (fuel > 0) tug.v = add(tug.v, mul(direction(tug.angle), thrust * power * dt));
    fuel = fmaxf(0, fuel - dt * (0.18f + fabsf(thrust_visual) * (boosting ? 2.1f : 0.85f)));

    if (hooked >= 0) {
        Cargo* c = &cargo[hooked];
        if (input.pressed[KEY_Q]) rope_length = fmaxf(45, rope_length - dt * 35);
        Vector2 anchor = sub(tug.p, mul(direction(tug.angle), 10));
        Vector2 delta = sub(c->b.p, anchor);
        float d = length(delta);
        Vector2 n = unit(delta);
        float separating = vector2_dot(sub(c->b.v, tug.v), n);
        // A unilateral damped spring: a cable pulls but cannot push. Substeps
        // keep the heavy wrecks stable even on a slow frame.
        float force = d > rope_length ? clamp((d - rope_length) * 23 + separating * 12, 0, 1100) : 0;
        tension = force / 1100;
        tug.v = add(tug.v, mul(n, force * dt / tug.mass));
        c->b.v = sub(c->b.v, mul(n, force * dt / c->b.mass));
        c->b.spin += clamp(angle_difference(atan2f(-n.y, -n.x), c->b.angle), -1, 1) * tension * dt;
        c->b.spin *= expf(-dt * 0.6f);
        rope_stress = clamp(rope_stress + (d > rope_length + 70 ? dt : -dt * 2), 0, 2);
        if (rope_stress > 1.15f || d > 255) release_cable("CABLE SNAPPED!  /  E TO REATTACH  /  EASE OFF THE BOOST");
    }

    move_body(&tug, dt, braking ? 3.4f : 0.33f, boosting ? 290 : 220);
    if (fabsf(thrust_visual) > 0 && random_float(0, 1) < dt * 100) {
        float side = random_float(-5, 5);
        Vector2 p = add(tug.p, rotate(v2(-13, side), tug.angle));
        particle(p, sub(mul(tug.v, 0.4f), mul(direction(tug.angle), random_float(40, boosting ? 210 : 120))),
                 random_float(0.15f, 0.45f), boosting ? 3 : 2, boosting ? GOLD : BLUE);
    }
    if (braking && fuel > 0 && length(tug.v) > 25 && random_float(0, 1) < dt * 35)
        particle(add(tug.p, mul(direction(random_float(0, TAU)), 9)), mul(tug.v, -0.1f), 0.2f, 2, TEAL);

    for (int i = 0; i < CARGO_COUNT; ++i) if (cargo[i].b.active) {
        Cargo* c = &cargo[i]; move_body(&c->b, dt, 0.16f, 340);
        float impact = collide(&tug, &c->b);
        if (impact > 85) hurt_tug((impact - 70) * 0.12f);
        for (int j = 0; j < i; ++j) if (cargo[j].b.active) collide(&c->b, &cargo[j].b);
        for (int j = 0; j < ROCK_COUNT; ++j) {
            Body rock = {.p = rocks[j].p, .r = rocks[j].r * 0.88f};
            impact = collide(&c->b, &rock);
            if (impact > 60) {
                c->b.hp -= (impact - 50) * 0.14f;
                c->b.spin += random_float(-1, 1);
                burst(c->b.p, c->b.v, 8, GOLD);
            }
        }
        if (c->b.hp <= 0) {
            c->b.active = false; burst(c->b.p, c->b.v, 60, GOLD);
            if (hooked == i) release_cable(NULL);
            floating(c->b.p, RED, "CARGO DESTROYED");
            notify("CARGO LOST  /  THERE ARE MORE WRECKS ON THE RADAR");
        } else if (length(c->b.p) < DOCK_RADIUS && length(c->b.v) < 100) {
            deliver(i);
            if (state != PLAY) return;
        }
    }
    for (int j = 0; j < ROCK_COUNT; ++j) {
        Body rock = {.p = rocks[j].p, .r = rocks[j].r * 0.88f};
        float impact = collide(&tug, &rock);
        if (impact > 55) hurt_tug((impact - 40) * 0.22f);
    }

    if (elapsed >= next_pirate) { spawn_pirate(); next_pirate = elapsed + fmaxf(15, 34 - delivered * 7); }
    for (int i = 0; i < PIRATE_COUNT; ++i) if (pirates[i].b.active) {
        Pirate* p = &pirates[i];
        p->cooldown -= dt; p->flash = fmaxf(0, p->flash - dt);
        float d = distance(p->b.p, tug.p);
        Vector2 target = add(tug.p, mul(tug.v, 0.5f));
        if (length(tug.p) < SAFE_RADIUS) {
            float a = atan2f(p->b.p.y, p->b.p.x) + dt * 0.5f;
            target = mul(direction(a), SAFE_RADIUS + 90);
        } else if (d < 155) {
            target = add(tug.p, mul(rotate(unit(sub(p->b.p, tug.p)), 0.65f), 190));
        }
        Vector2 desired = mul(unit(sub(target, p->b.p)), length(tug.p) < SAFE_RADIUS ? 70 : 112 + delivered * 8);
        p->b.v = add(p->b.v, mul(sub(desired, p->b.v), dt * 1.4f));
        p->b.angle = atan2f(tug.p.y - p->b.p.y, tug.p.x - p->b.p.x);
        move_body(&p->b, dt, 0.1f, 155);
        // The refinery repels pirates and intercepts their shots.
        if (length(p->b.p) < SAFE_RADIUS + 15) {
            Vector2 outward = unit(p->b.p);
            p->b.p = mul(outward, SAFE_RADIUS + 15);
            p->b.v = add(p->b.v, mul(outward, dt * 180));
        }
        if (p->cooldown <= 0 && d < 510 && length(tug.p) > SAFE_RADIUS + 20) {
            for (int j = 0; j < SHOT_COUNT; ++j) if (shots[j].life <= 0) {
                Vector2 aim = unit(sub(add(tug.p, mul(tug.v, d / 340)), p->b.p));
                shots[j] = (Shot){add(p->b.p, mul(aim, 18)), mul(aim, 260), 2.3f};
                p->cooldown = random_float(1.6f, 2.5f); break;
            }
        }
        float impact = collide(&tug, &p->b);
        if (impact > 35) { hurt_tug(13); p->b.hp -= impact * 0.45f; p->flash = 0.2f; }
        for (int j = 0; j < CARGO_COUNT; ++j) if (cargo[j].b.active) {
            impact = collide(&cargo[j].b, &p->b);
            if (impact > 30) {
                p->b.hp -= impact * cargo[j].b.mass * 0.65f;
                p->flash = 0.2f; cargo[j].b.hp -= impact * 0.025f;
                if (p->b.hp <= 0) { kill_pirate(p, true); break; }
            }
        }
        if (!p->b.active) continue;
        for (int j = 0; j < ROCK_COUNT; ++j) {
            Body rock = {.p = rocks[j].p, .r = rocks[j].r * 0.88f};
            impact = collide(&p->b, &rock);
            if (impact > 45) p->b.hp -= impact * 0.3f;
        }
        for (int j = 0; j < i; ++j) if (pirates[j].b.active) collide(&p->b, &pirates[j].b);
        if (p->b.hp <= 0) kill_pirate(p, false);
    }
    for (int i = 0; i < SHOT_COUNT; ++i) if (shots[i].life > 0) {
        Shot* s = &shots[i]; s->life -= dt; s->p = add(s->p, mul(s->v, dt));
        if (length(s->p) < SAFE_RADIUS) { s->life = 0; burst(s->p, v2(0, 0), 4, TEAL); continue; }
        if (distance(s->p, tug.p) < tug.r + 3) { s->life = 0; hurt_tug(12); continue; }
        for (int j = 0; j < CARGO_COUNT; ++j)
            if (cargo[j].b.active && distance(s->p, cargo[j].b.p) < cargo[j].b.r) {
                cargo[j].b.hp -= 8; s->life = 0; burst(s->p, cargo[j].b.v, 6, GOLD); break;
            }
        for (int j = 0; j < ROCK_COUNT && s->life > 0; ++j)
            if (distance(s->p, rocks[j].p) < rocks[j].r * 0.88f) { s->life = 0; burst(s->p, v2(0, 0), 5, GOLD); }
    }

    // A small solar trickle at the dock prevents a stranded, nearly empty tug
    // from getting stuck in a menu. Open-space fuel exhaustion ends the shift.
    if (length(tug.p) < 155) fuel = fminf(100, fuel + dt * 1.8f);
    int remaining = 0;
    for (int i = 0; i < CARGO_COUNT; ++i) remaining += cargo[i].b.active;
    if (hull <= 0 || (fuel <= 0 && length(tug.p) >= 155) || remaining + delivered < QUOTA) {
        state = LOST;
        snprintf(loss_reason, sizeof(loss_reason), "%s", hull <= 0 ? "YOUR TUG IS NOW SOMEBODY ELSE'S SALVAGE." :
                 fuel <= 0 ? "DRY TANKS. A VERY LONG WALK HOME." : "NOT ENOUGH WRECKS LEFT TO FILL THE CONTRACT.");
        if (hull <= 0) burst(tug.p, tug.v, 100, GOLD);
        if (credits > best) best = credits;
    }
}

// Convex local polygons are triangulated into chao's ordinary 2D triangles.
static void polygon(Vector2 center, const Vector2* vertices, int n, float a, float scale,
                    uint32_t face, uint32_t outline) {
    Vector2 points[16];
    for (int i = 0; i < n; ++i) points[i] = add(center, mul(rotate(vertices[i], a), scale));
    for (int i = 1; i + 1 < n; ++i)
        fill_triangle(chao_canvas, points[0].x, points[0].y, points[i].x, points[i].y,
                      points[i + 1].x, points[i + 1].y, face);
    for (int i = 0; i < n; ++i) line(points[i], points[(i + 1) % n], outline);
}
static void local_line(Vector2 center, Vector2 a, Vector2 b, float angle, float scale, uint32_t c) {
    line(add(center, mul(rotate(a, angle), scale)), add(center, mul(rotate(b, angle), scale)), c);
}
static void draw_background(void) {
    int ox = (int)(camera_draw.x * 0.045f), oy = (int)(camera_draw.y * 0.045f);
    for (int y = 0; y < screen_size.y; ++y) for (int x = 0; x < screen_size.x; ++x)
        chao_canvas->pixels[y * screen_size.x + x] = nebula->pixels[((y + oy) & 511) * 512 + ((x + ox) & 511)];
    // A stable hash gives each star a location without consuming simulation RNG.
    for (int layer = 0; layer < 3; ++layer) {
        float parallax = 0.12f + layer * 0.19f;
        int cell = 74 - layer * 12;
        int left = (int)floorf((camera_draw.x * parallax - screen_size.x * 0.5f) / cell);
        int top = (int)floorf((camera_draw.y * parallax - screen_size.y * 0.5f) / cell);
        for (int cy = top; cy <= top + screen_size.y / cell + 2; ++cy)
            for (int cx = left; cx <= left + screen_size.x / cell + 2; ++cx) {
                uint32_t h = (uint32_t)cx * 374761393u + (uint32_t)cy * 668265263u + layer * 1274126177u;
                h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
                int x = (int)(cx * cell + (int)(h & 31) - camera_draw.x * parallax + screen_size.x * 0.5f);
                int y = (int)(cy * cell + (int)((h >> 8) & 31) - camera_draw.y * parallax + screen_size.y * 0.5f);
                float light = 0.40f + layer * 0.15f + sinf(clock_time * 1.2f + (h & 255)) * 0.08f;
                uint32_t c = fade_color((h & 32) ? BLUE : WHITE, light);
                put_pixel(chao_canvas, x, y, c);
                if ((h & 127) == 0) {
                    put_pixel(chao_canvas, x + 1, y, fade_color(c, 0.4f));
                    put_pixel(chao_canvas, x - 1, y, fade_color(c, 0.4f));
                    put_pixel(chao_canvas, x, y + 1, fade_color(c, 0.4f));
                    put_pixel(chao_canvas, x, y - 1, fade_color(c, 0.4f));
                }
            }
    }
    // Faint navigation crosses live in world space and make drift readable.
    int gx = (int)floorf((camera_draw.x - screen_size.x * 0.5f) / 160);
    int gy = (int)floorf((camera_draw.y - screen_size.y * 0.5f) / 160);
    for (int y = gy; y <= gy + screen_size.y / 160 + 2; ++y)
        for (int x = gx; x <= gx + screen_size.x / 160 + 2; ++x) {
            Vector2 p = screen(v2(x * 160, y * 160));
            line(add(p, v2(-3, 0)), add(p, v2(3, 0)), 0x182938FF);
            line(add(p, v2(0, -3)), add(p, v2(0, 3)), 0x182938FF);
        }
    world_line(v2(-WORLD_X, -WORLD_Y), v2(WORLD_X, -WORLD_Y), EDGE);
    world_line(v2(-WORLD_X, WORLD_Y), v2(WORLD_X, WORLD_Y), EDGE);
    world_line(v2(-WORLD_X, -WORLD_Y), v2(-WORLD_X, WORLD_Y), EDGE);
    world_line(v2(WORLD_X, -WORLD_Y), v2(WORLD_X, WORLD_Y), EDGE);
}
static void draw_refinery(void) {
    if (!visible(v2(0, 0), SAFE_RADIUS + 15)) return;
    Vector2 p = screen(v2(0, 0));
    for (int i = 0; i < 64; ++i) {
        float a = TAU * i / 64;
        line(add(p, mul(direction(a), SAFE_RADIUS)), add(p, mul(direction(a + 0.028f), SAFE_RADIUS)), 0x23473FFF);
    }
    fill_circle(chao_canvas, p.x, p.y, DOCK_RADIUS, 0x0C242CFF);
    ring(p, DOCK_RADIUS + 3, 0x163B40FF, 64);
    ring(p, DOCK_RADIUS, TEAL, 64); ring(p, DOCK_RADIUS - 7, 0x2D6660FF, 64);
    for (int i = 0; i < 12; ++i) {
        float a = TAU * i / 12 + clock_time * 0.04f;
        Vector2 tip = add(p, mul(direction(a), 72));
        line(tip, add(tip, rotate(v2(7, -4), a)), fade_color(TEAL, 0.6f));
        line(tip, add(tip, rotate(v2(7, 4), a)), fade_color(TEAL, 0.6f));
    }
    // Solar arrays and a broad, shaded central hub.
    for (int side = -1; side <= 1; side += 2) {
        int x = (int)p.x + (side < 0 ? -68 : 29);
        fill_rect(chao_canvas, x + 2, p.y - 23, 39, 51, INK);
        fill_rect(chao_canvas, x, p.y - 26, 39, 50, 0x1B354CFF);
        draw_rect(chao_canvas, x, p.y - 26, 39, 50, 1, 0x5D7A8AFF);
        for (int i = 1; i < 5; ++i) draw_line(chao_canvas, x, p.y - 26 + i * 10, x + 38, p.y - 26 + i * 10, 0x34516AFF);
        for (int i = 1; i < 4; ++i) draw_line(chao_canvas, x + i * 10, p.y - 26, x + i * 10, p.y + 23, 0x34516AFF);
        line(add(p, v2(side * 18, 0)), add(p, v2(side * 35, 0)), MUTED);
    }
    const Vector2 hex[] = {{-27,-14},{0,-29},{27,-14},{27,14},{0,29},{-27,14}};
    polygon(add(p, v2(0, 5)), hex, 6, 0, 1, INK, INK);
    polygon(p, hex, 6, 0, 1, 0x526B75FF, 0x91B3B9FF);
    polygon(add(p, v2(0, -3)), hex, 6, 0, 0.70f, 0x233D49FF, 0x345666FF);
    fill_circle(chao_canvas, p.x, p.y - 3, 8, fade_color(TEAL, 0.65f + sinf(clock_time * 2) * 0.2f));
    ring(add(p, v2(0, -3)), 12, 0x496E73FF, 20);
    centered(p.x, p.y + 39, TEAL, "REFINERY 07");
    centered(p.x, p.y + 51, MUTED, "TOW CARGO INSIDE THE RING");
    centered(p.x, p.y + SAFE_RADIUS + 8, 0x50847CFF, "PIRATE EXCLUSION ZONE");
    if (delivery_flash > 0) ring(p, DOCK_RADIUS + (1 - delivery_flash) * 80, fade_color(TEAL, delivery_flash), 64);
}
static void draw_rock(const Rock* r) {
    if (!visible(r->p, r->r + 8)) return;
    Vector2 vertices[9];
    for (int i = 0; i < 9; ++i) vertices[i] = mul(direction(TAU * i / 9), r->shape[i] * r->r);
    Vector2 p = screen(r->p);
    polygon(add(p, v2(4, 6)), vertices, 9, r->angle, 1, INK, INK);
    polygon(p, vertices, 9, r->angle, 1, 0x2F3B49FF, 0x526170FF);
    for (int i = 0; i < 9; ++i) {
        Vector2 a = add(p, rotate(vertices[i], r->angle));
        Vector2 b = add(p, rotate(vertices[(i + 1) % 9], r->angle));
        uint32_t shade = i < 4 ? 0x354350FF : 0x26323FFF;
        fill_triangle(chao_canvas, p.x - 4, p.y - 5, a.x, a.y, b.x, b.y, shade);
        line(a, b, i > 3 ? 0x5A6873FF : 0x3D4A58FF);
    }
    Vector2 crater = add(p, rotate(v2(r->r * 0.22f, -r->r * 0.15f), r->angle));
    fill_circle(chao_canvas, crater.x, crater.y, r->r * 0.22f, 0x202C3AFF);
    ring(crater, r->r * 0.22f, 0x3E4E5BFF, 12);
    line(add(p, v2(-r->r * 0.4f, r->r * 0.1f)), add(p, v2(-r->r * 0.15f, r->r * 0.3f)), 0x53616CFF);
}
static void draw_cargo(int index, bool candidate) {
    Cargo* c = &cargo[index]; Body* b = &c->b;
    if (!b->active || !visible(b->p, b->r + 45)) return;
    Vector2 p = screen(b->p);
    const Vector2 shapes[3][8] = {
        {{-1,-.65f},{.65f,-.65f},{1,-.3f},{1,.3f},{.65f,.65f},{-1,.65f},{-.8f,0},{-1,-.3f}},
        {{-.9f,-.65f},{.65f,-.65f},{1,-.3f},{1,.35f},{.65f,.65f},{-.9f,.65f},{-1,.3f},{-1,-.3f}},
        {{-1,-.7f},{.1f,-.7f},{1,-.25f},{1,.25f},{.1f,.7f},{-1,.7f},{-.7f,.25f},{-.7f,-.25f}}
    };
    const uint32_t faces[] = {0x786758FF, 0x596C77FF, 0x646478FF};
    polygon(add(p, v2(3, 4)), shapes[c->kind], 8, b->angle, b->r, INK, INK);
    polygon(p, shapes[c->kind], 8, b->angle, b->r, faces[c->kind], 0xB3A693FF);
    for (int side = -1; side <= 1; side += 2) {
        local_line(p, v2(-.6f, side * .42f), v2(.45f, side * .42f), b->angle, b->r, 0x303944FF);
        local_line(p, v2(-.4f, side * .38f), v2(.3f, side * .38f), b->angle, b->r, 0xB1956DFF);
    }
    local_line(p, v2(.4f, -.45f), v2(.4f, .45f), b->angle, b->r, GOLD);
    local_line(p, v2(.5f, -.4f), v2(.5f, .4f), b->angle, b->r, GOLD);
    local_line(p, v2(-.55f, -.2f), v2(-.05f, -.2f), b->angle, b->r, 0x263B49FF);
    local_line(p, v2(-.55f, .0f), v2(-.05f, .0f), b->angle, b->r, BLUE);
    if (hooked == index || candidate) {
        int r = b->r + 7;
        uint32_t color = hooked == index ? TEAL : GOLD;
        for (int x = -1; x <= 1; x += 2) for (int y = -1; y <= 1; y += 2) {
            line(add(p, v2(x * r, y * r)), add(p, v2(x * (r - 6), y * r)), color);
            line(add(p, v2(x * r, y * r)), add(p, v2(x * r, y * (r - 6))), color);
        }
        centered(p.x, p.y - r - 13, color, hooked == index ? "CARGO LOCKED" : "[E] ATTACH");
        int value = (int)(c->value * (0.5f + 0.5f * clamp01(b->hp / c->max_hp)));
        text(p.x - 22, p.y + r + 5, GOLD, "$%d / %.1ft", value, b->mass);
        fill_rect(chao_canvas, p.x - 20, p.y + r + 14, 40, 2, EDGE);
        fill_rect(chao_canvas, p.x - 20, p.y + r + 14, 40 * clamp01(b->hp / c->max_hp), 2, color);
    } else {
        centered(p.x, p.y + b->r + 8, 0xA8977CFF, "SALVAGE");
    }
}
static void draw_cable(void) {
    if (hooked < 0) return;
    Vector2 start = sub(tug.p, mul(direction(tug.angle), 10));
    Vector2 end = cargo[hooked].b.p, delta = sub(end, start);
    float sag = fmaxf(0, rope_length - length(delta)) * 0.35f;
    Vector2 perpendicular = rotate(unit(delta), TAU / 4);
    uint32_t c = rope_stress > 0.25f ? RED : tension > 0.55f ? GOLD : TEAL;
    Vector2 prev = start;
    for (int i = 1; i <= 20; ++i) {
        float t = i / 20.0f;
        Vector2 p = add(add(start, mul(delta, t)), mul(perpendicular, sinf(t * TAU / 2) * (sag + 1.5f)));
        world_line(add(prev, v2(0, 2)), add(p, v2(0, 2)), INK);
        world_line(prev, p, i % 3 ? c : fade_color(c, 0.55f)); prev = p;
    }
    Vector2 a = screen(start), b = screen(end);
    fill_circle(chao_canvas, a.x, a.y, 2, WHITE); fill_circle(chao_canvas, b.x, b.y, 3, TEAL);
}
static void draw_ship(const Body* b, bool pirate, bool flash) {
    if (!visible(b->p, 35)) return;
    Vector2 p = screen(b->p);
    const Vector2 hull_shape[] = {{18,0},{4,7},{-11,8},{-9,0},{-11,-8},{4,-7}};
    const Vector2 wing[] = {{5,-6},{-5,-12},{-13,-12},{-10,-5}};
    const Vector2 pod[] = {{5,6},{4,11},{-12,11},{-12,6}};
    const Vector2 enemy[] = {{17,0},{-9,14},{-5,3},{-13,0},{-5,-3},{-9,-14}};
    if (pirate) {
        polygon(add(p, v2(2, 4)), enemy, 6, b->angle, 1, INK, INK);
        polygon(p, enemy, 6, b->angle, 1, flash ? WHITE : 0x7B364FFF, RED);
        local_line(p, v2(2,-3), v2(9,0), b->angle, 1, GOLD);
        local_line(p, v2(9,0), v2(2,3), b->angle, 1, GOLD);
        Vector2 exhaust = add(p, rotate(v2(-13, 0), b->angle));
        fill_circle(chao_canvas, exhaust.x, exhaust.y, 2, RED);
        return;
    }
    if (hull <= 0 && state == LOST) return;
    uint32_t face = flash ? WHITE : 0xA9BAC4FF;
    polygon(add(p, v2(2, 4)), hull_shape, 6, b->angle, 1, INK, INK);
    polygon(p, wing, 4, b->angle, 1, 0x536775FF, 0x96A9B6FF);
    polygon(p, pod, 4, b->angle, 1, 0x536775FF, 0x96A9B6FF);
    polygon(p, hull_shape, 6, b->angle, 1, face, WHITE);
    local_line(p, v2(-3,-5), v2(7,-3), b->angle, 1, 0x374E60FF);
    local_line(p, v2(-3,5), v2(7,3), b->angle, 1, 0x374E60FF);
    local_line(p, v2(3,-3), v2(10,0), b->angle, 1, BLUE);
    local_line(p, v2(10,0), v2(3,3), b->angle, 1, BLUE);
    local_line(p, v2(-6,-7), v2(-6,7), b->angle, 1, GOLD);
    if (armor) ring(p, 19, 0x42636FFF, 12);
    if (fabsf(thrust_visual) > 0) for (int side = -1; side <= 1; side += 2) {
        Vector2 a = add(p, rotate(v2(-12, side * 8), b->angle));
        Vector2 tip = add(p, rotate(v2(-19 - (boosting ? 10 : 0) - sinf(clock_time * 73) * 3, side * 8), b->angle));
        Vector2 up = add(a, rotate(v2(0, -2), b->angle)), down = add(a, rotate(v2(0, 2), b->angle));
        fill_triangle(chao_canvas, up.x, up.y, down.x, down.y, tip.x, tip.y, boosting ? GOLD : BLUE);
        fill_circle(chao_canvas, a.x, a.y, 1, WHITE);
    }
}
static void draw_effects(void) {
    for (int i = 0; i < FX_COUNT; ++i) if (effects[i].life > 0) {
        Fx* f = &effects[i]; if (!visible(f->p, 4)) continue;
        Vector2 p = screen(f->p);
        float t = clamp01(f->life / f->max_life);
        int r = fmaxf(1, f->size * t);
        fill_rect(chao_canvas, p.x - r / 2, p.y - r / 2, r, r, fade_color(f->color, t));
    }
    for (int i = 0; i < SHOT_COUNT; ++i) if (shots[i].life > 0) {
        Vector2 p = screen(shots[i].p), tail = sub(p, mul(unit(shots[i].v), 9));
        line(p, tail, RED); put_pixel(chao_canvas, p.x, p.y, WHITE);
    }
    for (int i = 0; i < TEXT_COUNT; ++i) if (float_text[i].life > 0) {
        FloatText* t = &float_text[i]; Vector2 p = screen(t->p);
        centered(p.x + 1, p.y + 1, INK, t->text);
        centered(p.x, p.y, fade_color(t->color, fminf(1, t->life)), t->text);
    }
}
static void draw_radar(void) {
    int x = screen_size.x - 119, y = screen_size.y - 128;
    panel(x, y, 107, 91);
    text(x + 8, y + 7, MUTED, "SECTOR / 07");
    int cx = x + 53, cy = y + 50;
    draw_line(chao_canvas, cx - 46, cy, cx + 46, cy, 0x1B3040FF);
    draw_line(chao_canvas, cx, cy - 30, cx, cy + 30, 0x1B3040FF);
    float scale = 0.032f;
    ring(v2(cx, cy), SAFE_RADIUS * scale, 0x27534CFF, 20);
    for (int i = 0; i < ROCK_COUNT; ++i)
        put_pixel(chao_canvas, cx + rocks[i].p.x * scale, cy + rocks[i].p.y * scale, 0x3B4A58FF);
    for (int i = 0; i < CARGO_COUNT; ++i) if (cargo[i].b.active)
        fill_rect(chao_canvas, cx + cargo[i].b.p.x * scale - 1, cy + cargo[i].b.p.y * scale - 1, 3, 3, hooked == i ? TEAL : GOLD);
    for (int i = 0; i < PIRATE_COUNT; ++i) if (pirates[i].b.active)
        fill_rect(chao_canvas, cx + pirates[i].b.p.x * scale - 1, cy + pirates[i].b.p.y * scale - 1, 3, 3, RED);
    fill_rect(chao_canvas, cx - 2, cy - 2, 4, 4, TEAL);
    Vector2 p = v2(cx + tug.p.x * scale, cy + tug.p.y * scale);
    fill_circle(chao_canvas, p.x, p.y, 2, WHITE);
    line(p, add(p, mul(direction(tug.angle), 5)), WHITE);
    text(x + 8, y + 79, GOLD, "CARGO"); text(x + 57, y + 79, RED, "PIRATES");
}
static void draw_waypoint(void) {
    int target_index = nearest_cargo(10000);
    bool home = hooked >= 0;
    if (target_index < 0 && !home) return;
    Vector2 target = home ? v2(0, 0) : cargo[target_index].b.p;
    Vector2 p = screen(target);
    int w = screen_size.x, h = screen_size.y;
    if (p.x > 55 && p.x < w - 55 && p.y > 75 && p.y < h - 70) return;
    Vector2 center = v2(w * 0.5f, h * 0.5f);
    Vector2 delta = sub(p, center), n = unit(delta);
    float t = 1;
    if (fabsf(delta.x) > 0.1f) t = fminf(t, (w * 0.5f - 57) / fabsf(delta.x));
    if (fabsf(delta.y) > 0.1f) t = fminf(t, (h * 0.5f - 80) / fabsf(delta.y));
    p = add(center, mul(delta, t));
    uint32_t c = home ? TEAL : GOLD;
    Vector2 tip = add(p, mul(n, 9)), a = add(p, rotate(mul(n, 6), 2.3f)), b = add(p, rotate(mul(n, 6), -2.3f));
    fill_triangle(chao_canvas, tip.x, tip.y, a.x, a.y, b.x, b.y, c);
    centered(p.x, p.y + 15, c, home ? "REFINERY" : "SALVAGE");
    char label[32]; snprintf(label, sizeof(label), "%dm", (int)distance(tug.p, target));
    centered(p.x, p.y + 24, MUTED, label);
}
static void meter(int x, int y, int w, float value, uint32_t c) {
    fill_rect(chao_canvas, x, y, w, 5, EDGE);
    fill_rect(chao_canvas, x, y, (int)(w * clamp01(value / 100)), 5, c);
    for (int i = 1; i < 5; ++i) fill_rect(chao_canvas, x + w * i / 5, y, 1, 5, PANEL);
}
static void draw_hud(void) {
    int w = screen_size.x, h = screen_size.y;
    panel(12, 12, 168, 45);
    text(22, 20, MUTED, "HULL"); meter(56, 20, 89, hull, hull < 30 ? RED : TEAL);
    text(150, 20, hull < 30 ? RED : WHITE, "%d", (int)ceilf(hull));
    text(22, 36, MUTED, "FUEL"); meter(56, 36, 89, fuel, fuel < 25 ? RED : GOLD);
    text(150, 36, fuel < 25 ? RED : WHITE, "%d", (int)ceilf(fuel));
    panel(w - 186, 12, 174, 45);
    text(w - 174, 20, MUTED, "CONTRACT");
    text(w - 100, 20, TEAL, "%d / %d WRECKS", delivered, QUOTA);
    text(w - 174, 36, GOLD, "$%04d", credits);
    text(w - 100, 36, MUTED, "%02d:%02d  SHIFT", (int)elapsed / 60, (int)elapsed % 60);
    centered(w / 2, 18, WHITE, "SCRAP TUG");
    centered(w / 2, 32, MUTED, "ORBITAL SALVAGE DIVISION");
    if (toast_time > 0) {
        int tw = ascii_font_get_size(small_font, toast, -1).x;
        fill_rect(chao_canvas, (w - tw) / 2 - 9, 65, tw + 18, 17, PANEL);
        centered(w / 2, 70, toast_time < 0.5f ? MUTED : GOLD, toast);
    }
    draw_radar(); draw_waypoint();
    if (hooked >= 0) {
        panel(12, h - 84, 174, 40);
        text(21, h - 76, TEAL, "CABLE  %dm  /  %.1ft", (int)rope_length, cargo[hooked].b.mass);
        text(21, h - 61, MUTED, "LOAD");
        meter(53, h - 61, 90, tension * 100, rope_stress > 0.25f ? RED : GOLD);
        text(150, h - 61, WHITE, "%d%%", (int)(tension * 100));
    } else {
        text(15, h - 55, MUTED, "SPEED %03d m/s", (int)length(tug.v));
    }
    bool shop = length(tug.p) < 155 && length(tug.v) < 45;
    if (shop) {
        panel((w - 398) / 2, h - 120, 398, 31);
        centered(w / 2, h - 113, TEAL, "REFINERY SHOP / SOLAR REFUEL ACTIVE");
        char offers[128];
        snprintf(offers, sizeof(offers), "1 ENGINE %s     2 ARMOR %s     3 SERVICE $35",
                 engines ? "FITTED" : "$90", armor ? "FITTED" : "$70");
        centered(w / 2, h - 101, WHITE, offers);
    }
    fill_rect(chao_canvas, 0, h - 29, w, 29, PANEL);
    draw_line(chao_canvas, 0, h - 29, w, h - 29, EDGE);
    centered(w / 2, h - 21, WHITE, "W THRUST   A/D TURN   S REVERSE   SPACE BRAKE   SHIFT BOOST");
    centered(w / 2, h - 10, MUTED, "E HOOK / RELEASE     Q REEL IN     ESC PAUSE");
    if (fuel < 25 && ((int)(clock_time * 3) & 1)) centered(w / 2, h - 48, RED, "LOW FUEL / DELIVER CARGO OR RETURN FOR SERVICE");
}
static void dim_screen(void) {
    for (int i = 0; i < screen_size.x * screen_size.y; ++i)
        chao_canvas->pixels[i] = blend_pixel_alpha(0x030912A0, chao_canvas->pixels[i]);
}
static void draw_title(void) {
    int x = max(22, screen_size.x / 2 - 292), y = max(15, screen_size.y / 2 - 160);
    panel(x, y, 318, 320);
    fill_rect(chao_canvas, x, y, 3, 320, GOLD);
    text(x + 22, y + 20, GOLD, "INDEPENDENT CONTRACTOR / SECTOR 07");
    large_text(x + 20, y + 42, 4, WHITE, "SCRAP");
    large_text(x + 20, y + 81, 4, GOLD, "TUG");
    text(x + 23, y + 124, TEAL, "SOMEBODY'S TRASH. YOUR PAYDAY.");
    draw_line(chao_canvas, x + 22, y + 144, x + 294, y + 144, EDGE);
    text(x + 22, y + 157, WHITE, "RECOVER 3 WRECKS. BRING THEM HOME.");
    text(x + 22, y + 171, MUTED, "Heavy cargo swings. Pirates shoot. Swing back.");
    text(x + 22, y + 193, GOLD, "W / UP");   text(x + 106, y + 193, WHITE, "THRUST     A/D TURN");
    text(x + 22, y + 208, GOLD, "E");        text(x + 106, y + 208, WHITE, "HOOK / RELEASE CARGO");
    text(x + 22, y + 223, GOLD, "SPACE / Q");text(x + 106, y + 223, WHITE, "BRAKE / REEL CABLE");
    text(x + 22, y + 238, GOLD, "SHIFT / S");text(x + 106, y + 238, WHITE, "BOOST / REVERSE");
    fill_rect(chao_canvas, x + 22, y + 263, 274, 27, TEAL);
    centered(x + 159, y + 273, INK, "PRESS ENTER TO CLOCK IN");
    text(x + 22, y + 302, MUTED, "NO GUNS. JUST A TUG AND A QUESTIONABLE PLAN.");
    text(screen_size.x - 173, screen_size.y - 18, MUTED, "CHAO / SOFTWARE RENDERED");
}
static void draw_end(void) {
    dim_screen();
    int w = 420, x = (screen_size.x - w) / 2, y = (screen_size.y - 240) / 2;
    panel(x, y, w, 240);
    fill_rect(chao_canvas, x, y, w, 3, state == WON ? TEAL : RED);
    text(x + 24, y + 23, state == WON ? TEAL : RED, "ORBITAL SALVAGE DIVISION / SHIFT REPORT");
    large_text(x + 24, y + 46, 2, WHITE, state == WON ? "CONTRACT COMPLETE" : "SHIFT TERMINATED");
    text(x + 24, y + 79, MUTED, "%s", state == WON ? "THREE WRECKS RECOVERED. YOU EARNED YOUR WAY HOME." : loss_reason);
    draw_line(chao_canvas, x + 24, y + 102, x + w - 24, y + 102, EDGE);
    text(x + 24, y + 118, MUTED, "TAKE-HOME PAY"); text(x + 236, y + 118, GOLD, "$%04d", credits);
    text(x + 24, y + 136, MUTED, "WRECKS / PIRATES"); text(x + 236, y + 136, WHITE, "%d RECOVERED / %d DOWN", delivered, kills);
    text(x + 24, y + 154, MUTED, "SHIFT / BEST PAY");
    text(x + 236, y + 154, WHITE, "%02d:%02d / $%04d", (int)elapsed / 60, (int)elapsed % 60, best);
    fill_rect(chao_canvas, x + 24, y + 184, w - 48, 28, state == WON ? TEAL : GOLD);
    centered(screen_size.x / 2, y + 194, INK, "R / ENTER : TAKE ANOTHER CONTRACT");
    centered(screen_size.x / 2, y + 224, MUTED, "ESC : BACK TO TITLE");
}
static void render(void) {
    draw_background(); draw_refinery();
    for (int i = 0; i < ROCK_COUNT; ++i) draw_rock(&rocks[i]);
    draw_cable();
    int candidate = hooked < 0 ? nearest_cargo(HOOK_RANGE) : -1;
    for (int i = 0; i < CARGO_COUNT; ++i) draw_cargo(i, candidate == i && state == PLAY);
    for (int i = 0; i < PIRATE_COUNT; ++i)
        if (pirates[i].b.active) draw_ship(&pirates[i].b, true, pirates[i].flash > 0);
    draw_ship(&tug, false, invulnerable > 0 && ((int)(clock_time * 16) & 1));
    draw_effects();
    if (state == TITLE) { draw_title(); return; }
    draw_hud();
    if (state == WON || state == LOST) draw_end();
    if (state == PAUSE) {
        dim_screen(); int x = screen_size.x / 2 - 154, y = screen_size.y / 2 - 57;
        panel(x, y, 308, 114);
        large_text(x + 24, y + 19, 2, WHITE, "COFFEE BREAK");
        centered(screen_size.x / 2, y + 58, GOLD, "ESC / ENTER TO RESUME");
        centered(screen_size.x / 2, y + 79, MUTED, "YOUR FUEL AND CARGO CAN WAIT.");
        centered(screen_size.x / 2, y + 98, MUTED, "R : RESTART SHIFT");
    }
}
static void init(void) {
    font = ascii_font_create(assets_load_bitmap("scrap_font", "assets/fonts/cp437_8x8.png"), true);
    small_font = ascii_font_create(assets_load_bitmap("scrap_small_font", "assets/fonts/cp437_5x5.png"), true);
    lettering = create_bitmap(320, 16);
    nebula = create_bitmap(512, 512);
    for (int y = 0; y < 512; ++y) for (int x = 0; x < 512; ++x) {
        // Periodic trig clouds tile without seams and need no image assets.
        float a = x * TAU / 512, b = y * TAU / 512;
        float n = clamp01((sinf(a + sinf(b)) + cosf(b * 2 + cosf(a * 2))) * 0.24f + 0.3f);
        float m = clamp01((cosf(a * 2 - b + sinf(b * 3)) + 0.5f) * 0.4f);
        nebula->pixels[y * 512 + x] = RGBA(7 + (int)(n * 10 + m * 5), 12 + (int)(n * 12), 23 + (int)(n * 18 + m * 10), 255);
    }
    platform_set_window_size(VIEW_W * 2, VIEW_H * 2);
    reset_shift((uint32_t)rand() | 1u); title_scene();
}
static void update(float dt) {
    clock_time += dt;
    bool start = input.just_pressed[KEY_ENTER];
    bool esc = input.just_pressed[KEY_ESC];
    bool restart = input.just_pressed[KEY_R];
    if (state == TITLE && start) {
        reset_shift((uint32_t)rand() | 1u); state = PLAY;
    } else if (state == PLAY && esc) {
        state = PAUSE; accumulator = 0;
    } else if (state == PAUSE && (esc || start)) {
        state = PLAY;
    } else if ((state == WON || state == LOST || state == PAUSE) && (restart || start)) {
        reset_shift((uint32_t)rand() | 1u); state = PLAY;
    } else if ((state == WON || state == LOST) && esc) {
        reset_shift((uint32_t)rand() | 1u); title_scene();
    }
    if (state == PLAY) {
        if (input.just_pressed[KEY_E]) toggle_hook();
        if (input.just_pressed[KEY_1]) buy_upgrade(1);
        if (input.just_pressed[KEY_2]) buy_upgrade(2);
        if (input.just_pressed[KEY_3]) buy_upgrade(3);
        accumulator += fminf(dt, MAX_DELTA_TIME);
        while (accumulator >= FIXED_DT && state == PLAY) { physics(FIXED_DT); accumulator -= FIXED_DT; }
        Vector2 target = add(tug.p, mul(tug.v, 0.42f));
        camera = add(camera, mul(sub(target, camera), 1 - expf(-dt * 5)));
    } else if (state == WON || state == LOST) {
        update_effects(dt); shake = fmaxf(0, shake - dt * 10);
    }
    camera_draw = add(camera, v2(sinf(clock_time * 81) * shake, cosf(clock_time * 67) * shake));
    render();
}

int main(void) {
    chao_run("Scrap Tug", VIEW_W, VIEW_H, &init, &update);
    ascii_font_free(font); ascii_font_free(small_font);
    free_bitmap(lettering); free_bitmap(nebula);
    return 0;
}

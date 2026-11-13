// Bonefield. All geometry, textures and game logic live in this file.
// WASD / arrows move, Space dodges, 1 / 2 / 3 spend a level's upgrade point.
// Esc pauses. R restarts after death or completion. Survive 3 minutes and kill
// the large enemy that arrives at 2:30. Weapons aim and fire automatically.
#include <assert.h>
#include "resources.h"
#include "../../chao/chao.h"

#define VIEW_W 480
#define VIEW_H 300
#define TAU 6.28318530718f
#define STEP (1.0f / 60.0f)
#define RUN_SECONDS 180.0f
#define LIMIT 28.0f
#define ENEMIES 256
#define BOLTS 128
#define PICKUPS 640
#define SPARKS 512
#define HOSTILE_SHOTS 80
#define PROPS 48
#define POSES 8
#define GRID_SIDE 32
#define GRID_CELL 3.0f

#define INK   0x10171CFF
#define PAPER 0xE0D9C5FF
#define DIM   0x86918EFF
#define GOLD  0xEFB65AFF
#define BLOOD 0xBA594BFF
#define FOG   0x313F46FF

typedef enum { PLAYING, PAUSED, DEAD, CLEARED } State;
typedef enum { SKELETON, HOUND, ACOLYTE, BRUTE, KEEPER, KINDS } Kind;
typedef struct {
    Vector2 p, velocity, push;
    float hp, max_hp, radius, speed, yaw, phase, flash, blade_wait, attack;
    int kind;
    unsigned pulse_hit;
    bool active;
} Enemy;
typedef struct {
    Vector2 p, previous, velocity;
    float life, damage;
    int pierce;
    uint64_t hits[4];
} Bolt;
typedef struct { Vector2 p; float phase, speed; int value; bool active, pulled; } Pickup;
typedef struct { Vector3 p, velocity; float life, total, size; uint32_t color; } Spark;
typedef struct { Vector2 p, velocity; float life; } HostileShot;
typedef struct { Vector2 p; float yaw, scale, radius; int kind; } Prop;
typedef struct {
    Vector2 p, move, facing;
    float hp, invulnerable, dodge, dodge_wait, gait, fire_wait, pulse_wait;
    int level, xp, points, rank[3], kills;
} Player;

static State state;
static Player player;
static Enemy enemies[ENEMIES];
static Bolt bolts[BOLTS];
static Pickup pickups[PICKUPS];
static Spark sparks[SPARKS];
static HostileShot hostile_shots[HOSTILE_SHOTS];
static Prop props[PROPS];
static int grid[GRID_SIDE * GRID_SIDE], grid_next[ENEMIES];
static Camera3D camera;
static Vector2 camera_p;
static Model3D *actors[KINDS + 1][POSES], *scenery[4];
static Model3D *ground, *blade, *gem, *shadow, *ring_mesh;
static Bitmap *palette, *soil;
static AsciiFont *font, *small_font;
static float elapsed, accumulator, spawn_wait, shake, pulse_radius, pulse_max;
static float pulse_life, orbit_angle;
static Vector2 pulse_origin;
static int spark_cursor, pickup_cursor, boss_index;
static unsigned pulse_id;
static bool boss_spawned, boss_killed;
static uint32_t rng = 0xA341316Cu;
static const int rank_cap[3] = {6,5,5};

static Vector2 v2(float x, float y) { return (Vector2){x, y}; }
static Vector3 v3(float x, float y, float z) { return (Vector3){x, y, z}; }
static Vector2 add(Vector2 a, Vector2 b) { return vector2_add(a, b); }
static Vector2 sub(Vector2 a, Vector2 b) { return vector2_subtract(a, b); }
static Vector2 mul(Vector2 a, float n) { return vector2_scale(a, n); }
static float len2(Vector2 p) { return p.x * p.x + p.y * p.y; }
static float dist2(Vector2 a, Vector2 b) { return len2(sub(a, b)); }
static Vector2 unit(Vector2 p) {
    float d = sqrtf(len2(p));
    return d > 0.00001f ? mul(p, 1.0f / d) : v2(0, -1);
}
static Vector2 direction(float a) { return v2(sinf(a), -cosf(a)); }
static Vector3 world(Vector2 p, float y) { return v3(p.x, y, p.y); }
static uint32_t random_u32(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
static float rnd(float a, float b) { return a + (b - a) * (random_u32() & 0xFFFFFF) / 16777216.0f; }
static int xp_needed(void) { return 6 + player.level * 3; }

// A tiny mesh builder: each face samples one swatch of a shared colour texture.
// The eight walking poses are built once, so crowds need one draw call per body.
typedef struct { Vector3 p[3072]; int color[1024], count; } Builder;
static Builder builder;
static const uint32_t swatches[] = {
    0xC9C0A7FF, 0x736E61FF, 0x292F32FF, 0x171F24FF,
    0x967B59FF, 0xBA594BFF, 0xEFB65AFF, 0x59666AFF,
    0x444F53FF, 0x778180FF, 0x6A4350FF, 0x37303FFF,
    0xB2BCC0FF, 0xE5DDC8FF, 0x322D2BFF, 0xB77B4AFF,
};
static void tri(Vector3 a, Vector3 b, Vector3 c, int color) {
    int n = builder.count++;
    assert(n < 1024);
    builder.p[n * 3] = a; builder.p[n * 3 + 1] = b; builder.p[n * 3 + 2] = c;
    builder.color[n] = color;
}
static Vector3 pitch_point(Vector3 p, Vector3 center, float pitch) {
    return v3(p.x + center.x, p.y * cosf(pitch) - p.z * sinf(pitch) + center.y,
              p.y * sinf(pitch) + p.z * cosf(pitch) + center.z);
}
static void box(Vector3 center, Vector3 size, float pitch, int color) {
    Vector3 p[8];
    for (int i = 0; i < 8; ++i)
        p[i] = pitch_point(v3((i & 1 ? 1 : -1) * size.x * .5f,
                              (i & 2 ? 1 : -1) * size.y * .5f,
                              (i & 4 ? 1 : -1) * size.z * .5f), center, pitch);
    const int faces[6][4] = {{4,5,7,6}, {1,0,2,3}, {5,1,3,7},
                             {0,4,6,2}, {6,7,3,2}, {0,1,5,4}};
    for (int i = 0; i < 6; ++i) {
        const int* q = faces[i];
        tri(p[q[0]], p[q[1]], p[q[2]], color);
        tri(p[q[0]], p[q[2]], p[q[3]], color);
    }
}
static void taper(float y, float height, float bottom, float top, int color) {
    Vector3 b[4], t[4];
    for (int i = 0; i < 4; ++i) {
        float a = TAU * (i + .5f) / 4;
        b[i] = v3(cosf(a) * bottom, y, sinf(a) * bottom);
        t[i] = v3(cosf(a) * top, y + height, sinf(a) * top);
    }
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        tri(b[i], t[j], b[j], color); tri(b[i], t[i], t[j], color);
    }
    tri(t[0], t[2], t[1], color); tri(t[0], t[3], t[2], color);
}
static Model3D* finish_mesh(void) {
    Model3D* m = model3d_create_from_triangles(builder.p, builder.count);
    free(m->uvs);
    m->uvs = malloc(sizeof(Vector2) * 16);
    for (int i = 0; i < 16; ++i) m->uvs[i] = (Vector2){(i + .5f) / 16, .5f};
    for (int i = 0; i < builder.count; ++i) {
        int c = builder.color[i];
        m->tri_uvs[i] = (Vector3Int){c, c, c};
    }
    m->texture = palette; builder.count = 0;
    return m;
}
static Model3D* make_actor(int kind, int pose) {
    float step = sinf(TAU * pose / POSES) * .26f;
    if (kind == KINDS) {
        // Hood, coat, boots, scarf and a crossbow; forward is -Z.
        box(v3(-.20f,.29f,step), v3(.21f,.56f,.27f), step, 3);
        box(v3( .20f,.29f,-step), v3(.21f,.56f,.27f), -step, 3);
        taper(.48f,.66f,.53f,.34f,15);
        box(v3(0,1.36f,0), v3(.56f,.51f,.52f),0,2);
        box(v3(0,1.33f,-.27f), v3(.32f,.26f,.03f),0,0);
        box(v3(0,1.30f,-.30f), v3(.25f,.065f,.03f),0,3);
        box(v3(0,1.12f,-.04f), v3(.68f,.13f,.53f),0,5);
        box(v3(.31f,.90f,-.30f), v3(.17f,.20f,.50f),-.2f,15);
        box(v3(-.31f,.90f,-.26f), v3(.17f,.20f,.42f),.2f,15);
        box(v3(0,.89f,-.55f), v3(.13f,.13f,.66f),0,14);
        box(v3(0,.91f,-.64f), v3(.92f,.09f,.10f),0,12);
    } else if (kind == HOUND) {
        box(v3(0,.49f,0), v3(.53f,.44f,.92f),step*.2f,14);
        box(v3(0,.61f,-.53f), v3(.39f,.38f,.44f),-.25f,0);
        box(v3(0,.59f,-.78f), v3(.27f,.14f,.14f),0,3);
        for (int i = 0; i < 4; ++i)
            box(v3(i & 1 ? .23f : -.23f,.23f,(i & 2 ? .33f : -.33f) + (i & 1 ? step : -step)),
                v3(.12f,.42f,.14f),i & 1 ? step : -step,1);
        box(v3(-.17f,.88f,-.43f), v3(.10f,.25f,.13f),-.3f,14);
        box(v3( .17f,.88f,-.43f), v3(.10f,.25f,.13f),-.3f,14);
    } else if (kind == ACOLYTE) {
        taper(.15f,1.22f,.58f,.25f,11);
        box(v3(0,1.52f,0),v3(.46f,.51f,.46f),0,10);
        box(v3(0,1.49f,-.24f),v3(.26f,.17f,.02f),0,3);
        box(v3(0,1.48f,-.26f),v3(.18f,.045f,.02f),0,6);
        box(v3(.50f,.80f,-.13f),v3(.08f,1.52f,.08f),0,14);
        box(v3(.50f,1.62f,-.13f),v3(.24f,.23f,.24f),.7f,5);
        box(v3(.32f,1.03f,-.12f),v3(.34f,.19f,.23f),0,10);
    } else {
        bool big = kind == BRUTE || kind == KEEPER;
        float w = big ? .74f : .48f;
        int bone = kind == KEEPER ? 12 : 0;
        box(v3(-w*.35f,.32f,step),v3(big?.23f:.12f,.63f,.17f),step,bone);
        box(v3( w*.35f,.32f,-step),v3(big?.23f:.12f,.63f,.17f),-step,bone);
        box(v3(0,.67f,0),v3(w,.16f,.29f),0,1);
        box(v3(0,1.00f,0),v3(.11f,.58f,.15f),0,1);
        for (int i = 0; i < 3; ++i)
            box(v3(0,.87f+i*.15f,-.025f),v3(w-.06f*i,.08f,.30f),0,bone);
        box(v3(0,1.51f,0),v3(big?.62f:.44f,.47f,.40f),0,bone);
        box(v3(0,1.44f,-.211f),v3(big?.46f:.32f,.14f,.02f),0,3);
        box(v3(-.105f,1.47f,-.23f),v3(.07f,.045f,.02f),0,5);
        box(v3( .105f,1.47f,-.23f),v3(.07f,.045f,.02f),0,5);
        box(v3(-w*.65f,1.01f,-.16f-step),v3(.12f,.61f,.15f),.4f+step,bone);
        box(v3( w*.65f,1.01f,-.16f+step),v3(.12f,.61f,.15f),.4f-step,bone);
        if (big) {
            box(v3(0,1.21f,.10f),v3(w*1.5f,.23f,.48f),0,kind==KEEPER?10:14);
            box(v3(w*.73f,.80f,-.3f),v3(.10f,1.5f,.10f),-.3f,14);
            box(v3(w*.73f,1.52f,-.52f),v3(.64f,.38f,.18f),-.3f,12);
        }
        if (kind == KEEPER) {
            for (int i = -1; i <= 1; ++i)
                box(v3(i*.23f,1.88f,0),v3(.12f,.43f,.18f),0,6);
        }
    }
    return finish_mesh();
}
static float flat_ground(float x, float z, void* user) { (void)x; (void)z; (void)user; return -.035f; }
static void make_models(void) {
    palette = create_bitmap(16,1);
    memcpy(palette->pixels,swatches,sizeof(swatches));
    for (int k = 0; k <= KINDS; ++k)
        for (int p = 0; p < POSES; ++p) actors[k][p] = make_actor(k,p);

    box(v3(0,.13f,0),v3(1.05f,.26f,.70f),0,8);
    box(v3(0,.65f,0),v3(.66f,1.05f,.27f),0,9);
    box(v3(0,1.19f,0),v3(.48f,.17f,.31f),0,9);
    box(v3(0,.79f,-.143f),v3(.34f,.055f,.02f),0,8);
    box(v3(0,.79f,-.143f),v3(.055f,.35f,.02f),0,8);
    scenery[0] = finish_mesh();
    box(v3(0,.16f,0),v3(1.2f,.32f,1.2f),0,8);
    taper(.32f,1.55f,.58f,.35f,7); taper(1.87f,.7f,.35f,0,9);
    scenery[1] = finish_mesh();
    box(v3(0,1.05f,0),v3(.28f,2.1f,.29f),.07f,14);
    box(v3(-.39f,1.65f,0),v3(.69f,.16f,.20f),-.4f,14);
    box(v3(.42f,2.0f,0),v3(.72f,.14f,.18f),.7f,14);
    box(v3(-.7f,1.97f,.06f),v3(.12f,.73f,.13f),-.55f,14);
    box(v3(.70f,2.31f,-.1f),v3(.13f,.68f,.13f),.3f,14);
    scenery[2] = finish_mesh();
    box(v3(0,.20f,0),v3(.78f,.40f,.78f),0,8);
    taper(.4f,1.0f,.32f,.24f,7);
    box(v3(0,1.46f,0),v3(.62f,.14f,.62f),0,14);
    taper(1.53f,.38f,.27f,.07f,6);
    scenery[3] = finish_mesh();

    box(v3(0,0,0),v3(.12f,.11f,.75f),0,12);
    box(v3(.21f,0,-.27f),v3(.55f,.08f,.15f),0,13);
    blade = finish_mesh();
    for (int i = 0; i < 4; ++i) {
        float a = TAU*i/4, b = TAU*(i+1)/4;
        Vector3 p = v3(cosf(a)*.16f,0,sinf(a)*.16f);
        Vector3 q = v3(cosf(b)*.16f,0,sinf(b)*.16f);
        tri(p,v3(0,.29f,0),q,6); tri(p,q,v3(0,-.15f,0),4);
    }
    gem = finish_mesh();
    Vector3 disk[16*3];
    for (int i = 0; i < 16; ++i) {
        disk[i*3] = v3(0,0,0);
        disk[i*3+1] = v3(cosf(TAU*(i+1)/16),0,sinf(TAU*(i+1)/16));
        disk[i*3+2] = v3(cosf(TAU*i/16),0,sinf(TAU*i/16));
    }
    shadow = model3d_create_from_triangles(disk,16);
    for (int i = 0; i < 64; ++i) {
        float a = TAU*i/64, b = TAU*(i+1)/64;
        Vector3 p = v3(cosf(a),0,sinf(a)), q = v3(cosf(b),0,sinf(b));
        tri(p,vector3_scale(q,.965f),q,6);
        tri(p,vector3_scale(p,.965f),vector3_scale(q,.965f),6);
    }
    ring_mesh = finish_mesh();
    // Ground has its own texture, with worn paths and sparse small stones.
    soil = create_bitmap(384,384);
    for (int z = 0; z < 384; ++z) for (int x = 0; x < 384; ++x) {
        uint32_t h = (uint32_t)x*374761393u + (uint32_t)z*668265263u;
        h = (h ^ (h >> 13))*1274126177u; h ^= h >> 16;
        float wx = (x/383.0f-.5f)*84, wz = (z/383.0f-.5f)*84;
        float r = sqrtf(wx*wx+wz*wz);
        bool path = fabsf(wx)<1.6f || fabsf(wz)<1.6f || fabsf(r-11)<.75f;
        int n = (int)(h%13)-6;
        int c = path ? 66 : 49;
        if (h%89 == 0) n += 19;
        soil->pixels[z*384+x] = RGBA(c+n,c+n+5,c+n+5,255);
    }
    ground = model3d_create_heightmap(12,7,flat_ground,NULL);
    ground->texture = soil;
}

static void draw_at(Model3D* model, Vector3 p, float yaw, Vector3 scale, uint32_t color) {
    Matrix4 m;
    float c = cosf(yaw), s = sinf(yaw);
    MATRIX4_SET(m, c*scale.x,0,s*scale.z,p.x,
                  0,scale.y,0,p.y,
                  -s*scale.x,0,c*scale.z,p.z,
                  0,0,0,1);
    model->color = color;
    draw_model3d_m(chao_canvas,&camera,model,m);
}
static void ground_ring(Vector2 p, float radius, uint32_t color) {
    bool lit = chao_3d_directional_light.enabled;
    chao_3d_directional_light.enabled = false;
    draw_at(ring_mesh,world(p,.025f),0,v3(radius,1,radius),color);
    chao_3d_directional_light.enabled = lit;
}
static void cast_shadow(Vector2 p, float radius) {
    draw_at(shadow,world(p,-.018f),0,v3(radius,1,radius*.7f),0x222B2EFF);
}
static bool on_screen(Vector2 p, float margin) {
    Vector3 s;
    return camera3d_project(chao_canvas,&camera,world(p,.7f),&s) &&
           s.x > -margin && s.x < screen_size.x+margin && s.y > -margin && s.y < screen_size.y+margin;
}

static void particles(Vector2 p, float y, int count, uint32_t color, float force) {
    for (int i = 0; i < count; ++i) {
        Spark* s = &sparks[spark_cursor++ % SPARKS];
        s->p = world(p,y); s->velocity = v3(rnd(-force,force),rnd(.6f,force),rnd(-force,force));
        s->life = s->total = rnd(.18f,.6f); s->size = rnd(.04f,.11f); s->color = color;
    }
}
static void drop_pickup(Vector2 p, int value) {
    for (int n = 0; n < PICKUPS; ++n) {
        int i = (pickup_cursor+n)%PICKUPS;
        if (pickups[i].active) continue;
        pickups[i] = (Pickup){.p=p,.phase=rnd(0,TAU),.value=value,.active=true};
        pickup_cursor = (i+1)%PICKUPS; return;
    }
    // Pool full: consolidate XP into a nearby gem instead of losing it.
    if (value <= 0) return;
    int best = -1; float d = INFINITY;
    for (int i = 0; i < PICKUPS; ++i) if (pickups[i].value > 0) {
        float ds = dist2(p,pickups[i].p);
        if (ds < d) { best=i; d=ds; }
    }
    if (best >= 0) pickups[best].value += value;
}
static bool hurt_enemy(int i, float damage, Vector2 push) {
    Enemy* e = &enemies[i];
    if (!e->active) return false;
    e->hp -= damage; e->flash = .10f;
    e->push = add(e->push,mul(push,e->kind==KEEPER?.05f:e->kind==BRUTE?.3f:1));
    particles(e->p,.8f,2,PAPER,1.8f);
    if (e->hp > 0) return false;
    e->active = false; player.kills++;
    int value = e->kind==KEEPER?35:e->kind==BRUTE?8:e->kind==ACOLYTE?3:1;
    drop_pickup(e->p,value);
    if (e->kind==BRUTE || random_u32()%34==0) drop_pickup(add(e->p,v2(.5f,0)),0);
    particles(e->p,.7f,e->kind==KEEPER?80:9,e->kind==ACOLYTE?BLOOD:PAPER,3);
    if (e->kind==KEEPER) { boss_killed=true; shake=.22f; }
    return true;
}
static void hurt_player(float damage, Vector2 from) {
    if (player.invulnerable>0 || state!=PLAYING) return;
    player.hp = fmaxf(0,player.hp-damage);
    player.invulnerable=.65f; shake=.13f;
    particles(player.p,.9f,16,BLOOD,3.0f);
    player.p=add(player.p,mul(unit(sub(player.p,from)),.35f));
    if (player.hp<=0) { state=DEAD; player.dodge=0; }
}
static Vector2 slide_props(Vector2 p, float radius) {
    for (int i = 0; i < PROPS; ++i) {
        Prop* prop = &props[i];
        Vector2 d = sub(p,prop->p);
        float r = radius+prop->radius;
        if (len2(d)<r*r) p = add(prop->p,mul(unit(d),r));
    }
    p.x=clamp(p.x,-LIMIT,LIMIT); p.y=clamp(p.y,-LIMIT,LIMIT);
    return p;
}
static int grid_coord(float p) { return clamp((int)floorf(p/GRID_CELL)+GRID_SIDE/2,0,GRID_SIDE-1); }
static void rebuild_grid(void) {
    for (int i = 0; i < GRID_SIDE*GRID_SIDE; ++i) grid[i]=-1;
    for (int i = 0; i < ENEMIES; ++i) if (enemies[i].active) {
        int cell = grid_coord(enemies[i].p.y)*GRID_SIDE+grid_coord(enemies[i].p.x);
        grid_next[i]=grid[cell]; grid[cell]=i;
    }
}
static int spawn_enemy(int kind, Vector2 p) {
    int slot = -1;
    for (int i = 0; i < ENEMIES; ++i) if (!enemies[i].active) { slot=i; break; }
    if (slot<0 && kind==KEEPER) {
        float far = -1;
        for (int i = 0; i < ENEMIES; ++i) {
            float d=dist2(player.p,enemies[i].p);
            if (d>far) { far=d; slot=i; }
        }
    }
    if (slot<0) return -1;
    static const float hp[KINDS] = {22,16,42,120,1800};
    static const float speed[KINDS] = {1.45f,3.15f,1.25f,1.05f,1.35f};
    static const float radius[KINDS] = {.37f,.32f,.40f,.64f,1.3f};
    Enemy* e = &enemies[slot];
    *e = (Enemy){.active=true,.kind=kind,.p=p,.hp=hp[kind]+elapsed*(kind==KEEPER?1:.12f),
        .speed=speed[kind]+(kind==SKELETON?elapsed*.0025f:0),.radius=radius[kind],
        .phase=rnd(0,TAU),.attack=rnd(1.5f,3.0f)};
    e->max_hp=e->hp;
    return slot;
}
static Vector2 spawn_position(float radius) {
    Vector2 p=v2(0,0);
    for (int tries=0; tries<40; ++tries) {
        p=add(player.p,mul(direction(rnd(0,TAU)),radius));
        p.x=clamp(p.x,-LIMIT-.8f,LIMIT+.8f); p.y=clamp(p.y,-LIMIT-.8f,LIMIT+.8f);
        if (dist2(p,player.p)>14*14) break;
    }
    return p;
}
static void spawn_wave(float dt) {
    spawn_wait-=dt;
    if (spawn_wait<=0) {
        spawn_wait+=fmaxf(.16f,.9f-elapsed*.0041f);
        int count=1+(elapsed>45)+(elapsed>105);
        for (int i=0; i<count; ++i) {
            int roll=(int)(random_u32()%100), kind=SKELETON;
            if (elapsed>15 && roll<23) kind=HOUND;
            if (elapsed>35 && roll>=82) kind=ACOLYTE;
            if (elapsed>65 && roll>=95) kind=BRUTE;
            spawn_enemy(kind,spawn_position(rnd(21,25)));
        }
    }
    if (elapsed>=150 && !boss_spawned) {
        boss_index=spawn_enemy(KEEPER,spawn_position(16));
        if (boss_index>=0) boss_spawned=true;
    }
}
static int nearest_enemy(Vector2 p, float range) {
    int best=-1; float d=range*range;
    for (int i=0; i<ENEMIES; ++i) if (enemies[i].active) {
        float ds=dist2(enemies[i].p,p);
        if (ds<d) { best=i; d=ds; }
    }
    return best;
}
static void fire_crossbow(void) {
    int target=nearest_enemy(player.p,17);
    if (target<0) return;
    Enemy* e=&enemies[target];
    float travel=sqrtf(dist2(player.p,e->p))/24;
    Vector2 aim=unit(sub(add(e->p,mul(e->velocity,travel)),player.p));
    player.facing=aim;
    float yaw=atan2f(aim.x,-aim.y);
    int rank=player.rank[0], count=1+(rank-1)/2;
    for (int b=0; b<count; ++b) for (int i=0; i<BOLTS; ++i) {
        if (bolts[i].life>0) continue;
        Vector2 d=direction(yaw+(b-(count-1)*.5f)*.13f);
        Vector2 p=add(player.p,mul(d,.7f));
        bolts[i]=(Bolt){.p=p,.previous=p,.velocity=mul(d,24),.life=.85f,
            .damage=16+rank*5+player.level*.65f,.pierce=1+(rank>=4)+(rank>=6)};
        break;
    }
    particles(add(player.p,mul(aim,.8f)),.9f,3,GOLD,1.2f);
    player.fire_wait=.68f-rank*.075f;
}
static Vector2 blade_position(int blade_index) {
    float a=orbit_angle+TAU*blade_index/(player.rank[1]+1);
    return add(player.p,mul(direction(a),2.0f+player.rank[1]*.18f));
}
static void tick_weapons(float dt) {
    player.fire_wait-=dt;
    if (player.fire_wait<=0) fire_crossbow();
    orbit_angle+=dt*(2.4f+player.rank[1]*.15f);
    int rank=player.rank[1];
    if (rank) for (int i=0; i<ENEMIES; ++i) {
        Enemy* e=&enemies[i];
        if (!e->active || e->blade_wait>0) continue;
        for (int b=0; b<rank+1; ++b) {
            Vector2 p=blade_position(b);
            if (dist2(p,e->p)<(e->radius+.46f)*(e->radius+.46f)) {
                e->blade_wait=.32f;
                hurt_enemy(i,9+rank*5,mul(unit(sub(e->p,player.p)),3)); break;
            }
        }
    }
    player.pulse_wait-=dt;
    rank=player.rank[2];
    if (rank && player.pulse_wait<=0) {
        pulse_origin=player.p; pulse_radius=.1f; pulse_max=3.4f+rank*.55f;
        pulse_life=.52f; ++pulse_id;
        player.pulse_wait=6.0f-rank*.6f;
        particles(player.p,.2f,22,GOLD,3.4f);
    }
    if (pulse_life>0) {
        pulse_life-=dt;
        pulse_radius=fminf(pulse_max,pulse_radius+dt*pulse_max/.48f);
        for (int i=0; i<ENEMIES; ++i) {
            Enemy* e=&enemies[i];
            if (e->active && e->pulse_hit!=pulse_id && dist2(e->p,pulse_origin)<pulse_radius*pulse_radius) {
                e->pulse_hit=pulse_id;
                hurt_enemy(i,18+rank*12,mul(unit(sub(e->p,pulse_origin)),7));
            }
        }
    }
}
static float segment_distance2(Vector2 p, Vector2 a, Vector2 b) {
    Vector2 d=sub(b,a), q=sub(p,a);
    float t=clamp01((q.x*d.x+q.y*d.y)/fmaxf(.00001f,len2(d)));
    return dist2(p,add(a,mul(d,t)));
}
static void tick_bolts(float dt) {
    for (int i=0; i<BOLTS; ++i) {
        Bolt* b=&bolts[i];
        if (b->life<=0) continue;
        b->life-=dt; b->previous=b->p; b->p=add(b->p,mul(b->velocity,dt));
        for (int n=0; n<ENEMIES; ++n) {
            Enemy* e=&enemies[n];
            uint64_t mask=(uint64_t)1<<(n%64);
            if (!e->active || (b->hits[n/64]&mask)) continue;
            float r=e->radius+.12f;
            if (segment_distance2(e->p,b->previous,b->p)>r*r) continue;
            b->hits[n/64]|=mask;
            hurt_enemy(n,b->damage,mul(unit(b->velocity),2.4f));
            if (--b->pierce<=0) { b->life=0; break; }
        }
    }
}
static void enemy_shoot(Enemy* e) {
    for (int i=0; i<HOSTILE_SHOTS; ++i) if (hostile_shots[i].life<=0) {
        hostile_shots[i]=(HostileShot){.p=e->p,.velocity=mul(unit(sub(player.p,e->p)),5.2f),.life=4};
        break;
    }
}
static void tick_enemies(float dt) {
    rebuild_grid();
    for (int i=0; i<ENEMIES; ++i) {
        Enemy* e=&enemies[i]; if (!e->active) continue;
        Vector2 to=sub(player.p,e->p), motion=mul(unit(to),e->speed);
        float distance=sqrtf(len2(to));
        e->flash=fmaxf(0,e->flash-dt); e->blade_wait=fmaxf(0,e->blade_wait-dt);
        e->attack-=dt;
        if (e->kind==ACOLYTE) {
            if (distance<7.5f) motion=mul(motion,-.65f);
            else if (distance<10) motion=mul(v2(-motion.y,motion.x),.55f);
            if (e->attack<=0 && distance<18) { enemy_shoot(e); e->attack=3.1f; }
        }
        if (e->kind==KEEPER) {
            if (e->attack<.95f) motion=v2(0,0);
            if (e->attack<=0) {
                if (distance<4.5f) hurt_player(28,e->p);
                particles(e->p,.2f,50,BLOOD,5); e->attack=4.8f;
            }
        }
        // Local separation keeps a crowd from collapsing into a single stack.
        int gx=grid_coord(e->p.x), gy=grid_coord(e->p.y);
        Vector2 separation=v2(0,0);
        for (int y=max(0,gy-1); y<=min(GRID_SIDE-1,gy+1); ++y)
            for (int x=max(0,gx-1); x<=min(GRID_SIDE-1,gx+1); ++x)
                for (int j=grid[y*GRID_SIDE+x]; j>=0; j=grid_next[j]) {
                    if (j==i || !enemies[j].active) continue;
                    Vector2 d=sub(e->p,enemies[j].p); float ds=len2(d);
                    float r=e->radius+enemies[j].radius;
                    if (ds<r*r && ds>.00001f) {
                        float l=sqrtf(ds);
                        separation=add(separation,mul(d,(r-l)/(l*r)));
                    }
                }
        float strength=e->kind==KEEPER?.2f:2.3f;
        e->velocity=add(motion,mul(separation,strength));
        e->p=add(e->p,mul(add(e->velocity,e->push),dt));
        e->push=mul(e->push,fmaxf(0,1-8*dt));
        e->p=slide_props(e->p,e->radius);
        e->yaw=atan2f(to.x,-to.y);
        e->phase+=dt*e->speed*(e->kind==HOUND?5:3.5f);
        float r=e->radius+.34f;
        if (dist2(e->p,player.p)<r*r) hurt_player(e->kind==KEEPER?25:e->kind==BRUTE?18:10,e->p);
    }
    for (int i=0; i<HOSTILE_SHOTS; ++i) {
        HostileShot* s=&hostile_shots[i]; if (s->life<=0) continue;
        s->life-=dt; Vector2 prev=s->p; s->p=add(s->p,mul(s->velocity,dt));
        if (segment_distance2(player.p,prev,s->p)<.47f*.47f) {
            hurt_player(12,s->p); s->life=0; particles(s->p,.7f,5,BLOOD,2);
        }
    }
}
static void tick_pickups(float dt) {
    for (int i=0; i<PICKUPS; ++i) {
        Pickup* p=&pickups[i]; if (!p->active) continue;
        p->phase+=dt*2;
        float ds=dist2(p->p,player.p);
        float magnet=2.6f+player.level*.055f;
        if (ds<magnet*magnet) p->pulled=true;
        if (p->pulled) {
            p->speed=fminf(17,p->speed+dt*26);
            float move=p->speed*dt;
            if (ds>move*move) p->p=add(p->p,mul(unit(sub(player.p,p->p)),move));
            else p->p=player.p;
        }
        if (dist2(p->p,player.p)>.65f*.65f) continue;
        p->active=false;
        if (p->value==0) { player.hp=fminf(100,player.hp+18); particles(player.p,.7f,7,BLOOD,1.5f); }
        else player.xp+=p->value;
    }
    while (player.xp>=xp_needed()) {
        player.xp-=xp_needed(); player.level++;
        int remaining=0;
        for (int i=0; i<3; ++i) remaining+=rank_cap[i]-player.rank[i];
        if (player.points<remaining) player.points++;
        player.hp=fminf(100,player.hp+8); particles(player.p,.6f,18,GOLD,2.5f);
    }
}
static void tick_sparks(float dt) {
    for (int i=0; i<SPARKS; ++i) {
        Spark* s=&sparks[i]; if (s->life<=0) continue;
        s->life-=dt; s->velocity.y-=dt*6;
        s->p=vector3_add(s->p,vector3_scale(s->velocity,dt));
        if (s->p.y<.04f) { s->p.y=.04f; s->velocity.y=fabsf(s->velocity.y)*.25f; }
    }
}
static void tick_player(float dt) {
    Vector2 move=v2((input.pressed[KEY_D]||input.pressed[KEY_RIGHT])-(input.pressed[KEY_A]||input.pressed[KEY_LEFT]),
                     (input.pressed[KEY_S]||input.pressed[KEY_DOWN])-(input.pressed[KEY_W]||input.pressed[KEY_UP]));
    if (len2(move)>0) move=unit(move);
    player.invulnerable=fmaxf(0,player.invulnerable-dt);
    player.dodge_wait=fmaxf(0,player.dodge_wait-dt);
    float speed=4.9f;
    if (player.dodge>0) {
        player.dodge-=dt; move=player.move; speed=17;
        if (random_u32()%3==0) particles(player.p,.15f,1,PAPER,1);
    } else if (len2(move)>0) {
        player.move=move;
        if (player.fire_wait<.1f) player.facing=move;
    }
    player.p=slide_props(add(player.p,mul(move,dt*speed)),.31f);
    if (len2(move)>0) player.gait+=dt*speed*2.8f;
    else player.gait=0;
}
static void simulate(float dt) {
    elapsed+=dt; shake=fmaxf(0,shake-dt);
    tick_player(dt); spawn_wave(dt); tick_enemies(dt);
    if (state!=PLAYING) return;
    tick_weapons(dt); tick_bolts(dt); tick_pickups(dt); tick_sparks(dt);
    if (elapsed>=RUN_SECONDS && boss_killed) state=CLEARED;
}
static void reset_game(void) {
    memset(enemies,0,sizeof(enemies)); memset(bolts,0,sizeof(bolts));
    memset(pickups,0,sizeof(pickups)); memset(sparks,0,sizeof(sparks));
    memset(hostile_shots,0,sizeof(hostile_shots));
    player=(Player){.hp=100,.level=1,.rank={1,0,0},.move={0,-1},.facing={0,-1}};
    elapsed=accumulator=shake=pulse_life=orbit_angle=0;
    spawn_wait=.3f; spark_cursor=pickup_cursor=0; pulse_id=0;
    boss_spawned=boss_killed=false; boss_index=-1; camera_p=v2(0,0); state=PLAYING;
    // Fixed scenery layout; retries keep the same routes through the graves.
    rng=0xA341316Cu;
    for (int i=0; i<PROPS; ++i) {
        int col=i%8, row=i/8;
        Vector2 p=v2((col-3.5f)*6.2f+rnd(-1,1),(row-2.5f)*8+rnd(-1,1));
        if (fabsf(p.x)<4.4f && fabsf(p.y)<5) p.x+=p.x<0?-4:4;
        int kind=i%13==0?3:i%11==0?2:i%9==0?1:0;
        float scale=rnd(.8f,1.2f);
        props[i]=(Prop){.p=p,.yaw=rnd(-.27f,.27f),.scale=scale,.kind=kind,
            .radius=(kind==2?.24f:kind==1?.6f:.42f)*scale};
    }
    for (int i=0; i<5; ++i) spawn_enemy(SKELETON,mul(direction(TAU*i/5),rnd(13,17)));
}

static void update_camera(float dt) {
    camera_p=add(camera_p,mul(sub(player.p,camera_p),1-expf(-dt*9)));
    float jx=shake>0?sinf(elapsed*83)*shake*.9f:0;
    float jz=shake>0?cosf(elapsed*71)*shake*.6f:0;
    camera.position=v3(camera_p.x+jx,24,camera_p.y+20+jz);
    camera.target=world(camera_p,0); camera.up=VECTOR3_UP;
    camera3d_update(&camera);
    camera3d_set_perspective(camera.projection,deg_to_rad(48),(float)screen_size.x/screen_size.y,.1f,85);
}
static void draw_world(void) {
    fill(chao_canvas,FOG);
    chao_blend_mode=BLEND_NONE;
    chao_3d_directional_light.enabled=true;
    draw_model3d(chao_canvas,&camera,ground,VECTOR3_ZERO,VECTOR3_ZERO);
    // Low boundary stones are visible well before movement reaches the fence.
    for (int side=0; side<4; ++side) for (int i=-5; i<=5; ++i) {
        Vector2 p=side<2?v2(i*5.6f,side?30:-30):v2(side==2?-30:30,i*5.6f);
        if (on_screen(p,40)) draw_at(scenery[0],world(p,0),side<2?0:TAU*.25f,v3(.9f,.45f,.8f),0xA2ADAAFF);
    }
    chao_3d_directional_light.enabled=false;
    for (int i=0; i<PROPS; ++i) if (on_screen(props[i].p,40)) cast_shadow(props[i].p,props[i].scale*.9f);
    for (int i=0; i<ENEMIES; ++i) if (enemies[i].active && on_screen(enemies[i].p,35))
        cast_shadow(enemies[i].p,enemies[i].radius*1.6f);
    cast_shadow(player.p,.67f);
    ground_ring(player.p,.59f,0xB9AD8CFF);
    if (pulse_life>0) ground_ring(pulse_origin,pulse_radius,GOLD);
    if (boss_spawned && !boss_killed) {
        Enemy* e=&enemies[boss_index];
        if (e->attack<.95f) {
            ground_ring(e->p,4.5f,BLOOD);
            ground_ring(e->p,4.5f*(1-e->attack/.95f),BLOOD);
        }
    }
    chao_3d_directional_light.enabled=true;
    for (int i=0; i<PROPS; ++i) {
        Prop* p=&props[i]; if (!on_screen(p->p,50)) continue;
        draw_at(scenery[p->kind],world(p->p,0),p->yaw,v3(p->scale,p->scale,p->scale),COLOR_WHITE);
    }
    for (int i=0; i<PICKUPS; ++i) {
        Pickup* p=&pickups[i]; if (!p->active || !on_screen(p->p,12)) continue;
        float size=p->value>4?1.5f:1;
        if (p->value==0) size=1.5f;
        draw_at(gem,world(p->p,.22f+sinf(p->phase)*.06f),p->phase,v3(size,size,size),p->value?COLOR_WHITE:0xF07365FF);
    }
    for (int i=0; i<ENEMIES; ++i) {
        Enemy* e=&enemies[i]; if (!e->active || !on_screen(e->p,60)) continue;
        float scale=e->kind==KEEPER?2.25f:e->kind==BRUTE?1.45f:1;
        int pose=((int)(e->phase*POSES/TAU))%POSES;
        float y=e->kind==ACOLYTE?.13f+sinf(e->phase)*.08f:0;
        Model3D* model=actors[e->kind][pose];
        if (e->flash>0) model->texture=NULL;
        draw_at(model,world(e->p,y),e->yaw,v3(scale,scale,scale),e->flash>0?PAPER:COLOR_WHITE);
        model->texture=palette;
    }
    int pose=((int)(player.gait*POSES/TAU))%POSES;
    float yaw=atan2f(player.facing.x,-player.facing.y);
    draw_at(actors[KINDS][pose],world(player.p,player.dodge>0?.12f:0),yaw,v3(1,1,1),
            player.invulnerable>0 && ((int)(elapsed*18)&1)?0xF9DC99FF:COLOR_WHITE);
    if (player.rank[1]) for (int i=0; i<player.rank[1]+1; ++i)
        draw_at(blade,world(blade_position(i),.85f),-orbit_angle+TAU*i/(player.rank[1]+1),v3(1,1,1),COLOR_WHITE);
    chao_3d_directional_light.enabled=false;
    for (int i=0; i<BOLTS; ++i) if (bolts[i].life>0) {
        Vector2 d=mul(unit(bolts[i].velocity),.36f);
        draw_point3d(chao_canvas,&camera,world(bolts[i].p,.9f),.13f,PAPER);
        draw_point3d(chao_canvas,&camera,world(sub(bolts[i].p,d),.9f),.075f,GOLD);
    }
    for (int i=0; i<HOSTILE_SHOTS; ++i) if (hostile_shots[i].life>0) {
        draw_point3d(chao_canvas,&camera,world(hostile_shots[i].p,.8f),.29f,BLOOD);
        draw_point3d(chao_canvas,&camera,world(hostile_shots[i].p,.82f),.10f,PAPER);
    }
    chao_blend_mode=BLEND_ALPHA;
    for (int i=0; i<PROPS; ++i) if (props[i].kind==3) {
        Vector3 p=world(props[i].p,1.85f*props[i].scale);
        draw_point3d(chao_canvas,&camera,p,.5f,0xEFB65A30);
        draw_point3d(chao_canvas,&camera,p,.16f+sinf(elapsed*11+i)*.03f,GOLD);
    }
    for (int i=0; i<SPARKS; ++i) if (sparks[i].life>0) {
        Spark* s=&sparks[i];
        draw_point3d(chao_canvas,&camera,s->p,s->size,SET_ALPHAF(s->color,clamp01(s->life/s->total)));
    }
    chao_blend_mode=BLEND_NONE;
}
static void text(int x, int y, uint32_t color, const char* fmt, ...) {
    char buf[160]; va_list args;
    va_start(args,fmt); vsnprintf(buf,sizeof(buf),fmt,args); va_end(args);
    ascii_font_draw(chao_canvas,small_font,x+1,y+1,INK,"%s",buf);
    ascii_font_draw(chao_canvas,small_font,x,y,color,"%s",buf);
}
static void centered(int x, int y, AsciiFont* f, uint32_t color, const char* msg) {
    Vector2Int size=ascii_font_get_size(f,msg,-1);
    ascii_font_draw(chao_canvas,f,x-size.x/2+1,y+1,INK,"%s",msg);
    ascii_font_draw(chao_canvas,f,x-size.x/2,y,color,"%s",msg);
}
static void draw_hud(void) {
    int w=screen_size.x, h=screen_size.y;
    chao_blend_mode=BLEND_ALPHA;
    fill_rect(chao_canvas,0,0,w,35,0x10171CEB);
    fill_rect(chao_canvas,0,h-38,w,38,0x10171CF0);
    chao_blend_mode=BLEND_NONE;
    text(9,7,PAPER,"HP");
    fill_rect(chao_canvas,25,7,96,5,0x3F3836FF);
    fill_rect(chao_canvas,25,7,(int)(96*player.hp/100),5,BLOOD);
    text(9,20,DIM,"LV %02d",player.level);
    fill_rect(chao_canvas,43,21,78,3,0x343F43FF);
    fill_rect(chao_canvas,43,21,78*player.xp/xp_needed(),3,GOLD);
    int t=(int)elapsed;
    char clock_text[40]; snprintf(clock_text,sizeof(clock_text),"%02d:%02d",t/60,t%60);
    centered(w/2,6,font,PAPER,clock_text);
    centered(w/2,21,small_font,DIM,"/ 03:00");
    text(w-88,7,PAPER,"%d KILLS",player.kills);
    text(w-88,20,player.dodge_wait<=0?PAPER:DIM,"DODGE");
    fill_rect(chao_canvas,w-54,21,44,3,0x343F43FF);
    fill_rect(chao_canvas,w-54,21,(int)(44*(1-clamp01(player.dodge_wait/2.4f))),3,DIM);
    text(w-88,29,DIM,"FPS: %d",chao_fps);
    if (boss_spawned && !boss_killed) {
        Enemy* e=&enemies[boss_index];
        fill_rect(chao_canvas,w/2-70,41,140,3,INK);
        fill_rect(chao_canvas,w/2-70,41,(int)(140*clamp01(e->hp/e->max_hp)),3,BLOOD);
        centered(w/2,48,small_font,PAPER,"KEEPER");
    }
    if (state==PLAYING || state==PAUSED) {
        static const char* names[3]={"CROSSBOW","BLADES","PULSE"};
        int col=w/3;
        for (int i=0; i<3; ++i) {
            int x=i*col+9;
            bool can=player.points>0 && player.rank[i]<rank_cap[i];
            text(x,h-31,can?GOLD:DIM,"%d  %s",i+1,names[i]);
            for (int n=0; n<rank_cap[i]; ++n)
                fill_rect(chao_canvas,x+13+n*9,h-20,6,3,n<player.rank[i]?PAPER:0x3C484BFF);
            if (can) text(x+77,h-31,GOLD,"+");
        }
        if (player.points>0) text(9,h-48,GOLD,"%d UPGRADE%s  [1-3]",player.points,player.points==1?"":"S");
        centered(w/2,h-9,small_font,DIM,"WASD / ARROWS   SPACE DODGE   ESC PAUSE");
        if (state==PAUSED) centered(w/2,74,font,PAPER,"Paused");
    } else {
        centered(w/2,h-31,font,PAPER,state==DEAD?"Dead":"Cleared");
        centered(w/2,h-13,small_font,DIM,"R  RESTART");
    }
}
static void init(void) {
    platform_set_window_size(VIEW_W*3,VIEW_H*3);
    font=ascii_font_create(assets_load_bitmap("bonefield_font","assets/fonts/cp437_8x8.png"),true);
    small_font=ascii_font_create(assets_load_bitmap("bonefield_small","assets/fonts/cp437_5x5.png"),true);
    chao_3d_directional_light.direction=vector3_get_normalized(v3(-.45f,.85f,.25f));
    chao_3d_directional_light.ambient=.43f;
    chao_3d_fog.enabled=true; chao_3d_fog.color=FOG;
    chao_3d_fog.start=33; chao_3d_fog.end=66;
    make_models(); reset_game(); update_camera(1);
}
static void update(float dt) {
    dt=clamp(dt,0,.05f);
    if (input.just_pressed[KEY_ESC] && (state==PLAYING || state==PAUSED)) {
        state=state==PLAYING?PAUSED:PLAYING; accumulator=0;
    }
    if ((state==DEAD || state==CLEARED) && input.just_pressed[KEY_R]) reset_game();
    if (state==PLAYING) {
        for (int i=0; i<3; ++i) if (input.just_pressed[KEY_1+i] && player.points>0 && player.rank[i]<rank_cap[i]) {
            player.points--; player.rank[i]++;
            if (i==2) player.pulse_wait=0;
        }
        if (input.just_pressed[KEY_SPACE] && player.dodge_wait<=0) {
            Vector2 move=v2((input.pressed[KEY_D]||input.pressed[KEY_RIGHT])-(input.pressed[KEY_A]||input.pressed[KEY_LEFT]),
                             (input.pressed[KEY_S]||input.pressed[KEY_DOWN])-(input.pressed[KEY_W]||input.pressed[KEY_UP]));
            if (len2(move)>0) player.move=unit(move);
            player.dodge=.20f; player.dodge_wait=2.4f; player.invulnerable=.27f;
            particles(player.p,.2f,8,PAPER,2);
        }
        accumulator+=dt;
        while (accumulator>=STEP && state==PLAYING) { simulate(STEP); accumulator-=STEP; }
        if (state!=PLAYING) accumulator=0;
    }
    if (state!=PAUSED) update_camera(dt);
    draw_world(); draw_hud();
}
static void cleanup(void) {
    for (int k=0; k<=KINDS; ++k) for (int p=0; p<POSES; ++p) model3d_free(actors[k][p]);
    for (int i=0; i<4; ++i) model3d_free(scenery[i]);
    model3d_free(ground); model3d_free(blade); model3d_free(gem);
    model3d_free(shadow); model3d_free(ring_mesh);
    free_bitmap(palette); free_bitmap(soil);
    ascii_font_free(font); ascii_font_free(small_font);
}
int main(void) {
    chao_run("Bonefield",VIEW_W,VIEW_H,&init,&update);
    cleanup();
    return 0;
}

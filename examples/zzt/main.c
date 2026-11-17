// ZZT 3D. Board data, object scripts and rendering are all in this file.
// WASD/arrows move; Space fires; Shift + direction fires without moving.
// Bump people to talk, push boulders onto plates, and match keys to doors.
// T lights a torch, U undoes the last push, R resets this room's boulders.
// H shows help, Esc pauses. Collect three fuses to open the south exit.
// ./build/debug/zzt --self-test       deterministic, windowless gameplay checks
// ./build/debug/zzt --snapshot out.ppm [0..4]   render a board without a window
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include "resources.h"
#include "../../chao/chao.h"

#define WIDTH 640
#define HEIGHT 400
#define SCENE_W 480
#define SCENE_H 376
#define COLS 21
#define ROWS 15
#define ROOMS 5
#define ACTORS 20
#define SHOTS 48
#define INK 0x000000FF
#define PANEL 0x0000AAFF
#define LINE 0x5555FFFF
#define PAPER 0xFFFFFFFF
#define MUTED 0xAAAAAAFF
#define GOLD 0xFFFF55FF
#define MINT 0x55FFFFFF
#define RED 0xFF5555FF

typedef enum { PLAY, TALK, PAUSE, HELP, DEAD, WON } State;
typedef struct { int x, y; float vx, vy; int kind, facing; bool alive; } Actor;
typedef struct {
    char tiles[ROWS][COLS];
    bool rocks[ROWS][COLS], initial_rocks[ROWS][COLS];
    Actor actors[ACTORS];
    int actor_count;
    bool visited, solved;
} Room;
typedef struct { int x, y, dx, dy; bool active, hostile; int steps; } Shot;
typedef struct { int x, y, facing, hp, ammo, gems, torches, score, fuses, keys; float vx, vy; } Player;
typedef struct { bool valid; int x, y, tx, ty, px, py; } Push;

static Room rooms[ROOMS];
static Player player;
static Push undo_push;
static Shot shots[SHOTS];
static State state;
static int room_id, distance_field[ROWS][COLS], last_direction = -1;
static float clock_time, run_time, move_wait, fire_wait, hurt_wait, enemy_wait, shot_wait, torch_time;
static float message_time;
static bool headless;
static char message[100], dialog_title[64], dialog_body[800];
static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
static const char *room_names[ROOMS] = {"TOWN", "LION PEN", "PUMP ROOM", "STORE ROOM", "EXIT"};
static const uint32_t key_colors[3] = {0x5555FFFF, RED, 0xFF55FFFF};
static uint32_t random_state = 0xA341316Cu;
static Camera3D camera;
static Bitmap *scene, *palette;
static AsciiFont *font;
static Model3D *board_mesh[ROOMS], *cube, *glyphs[256];

static Vector3 v3(float x, float y, float z) { return (Vector3){x,y,z}; }
static Vector3 world(float x, float y, float h) { return v3(x - COLS / 2, h, y - ROWS / 2); }
static bool inside(int x, int y) { return x >= 0 && y >= 0 && x < COLS && y < ROWS; }
static uint32_t next_random(void) {
    random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5;
    return random_state;
}
static char tile_at(int x, int y) { return inside(x,y) ? rooms[room_id].tiles[y][x] : '#'; }
static void notify(const char *fmt, ...) {
    va_list args; va_start(args,fmt); vsnprintf(message,sizeof(message),fmt,args); va_end(args);
    message_time = 5;
}
static void dialogue(const char *title, const char *body) {
    snprintf(dialog_title,sizeof(dialog_title),"%s",title);
    snprintf(dialog_body,sizeof(dialog_body),"%s",body);
    state = TALK;
}
static int actor_at(int x, int y) {
    Room *r = &rooms[room_id];
    for (int i = 0; i < r->actor_count; ++i)
        if (r->actors[i].alive && r->actors[i].x == x && r->actors[i].y == y) return i;
    return -1;
}
static bool terrain_blocks(char c) {
    return c == '#' || c == '~' || c == '?' || c == 'B' || c == 'R' || c == 'P' || c == '=' || c == 'G' || c == 'N' || c == '!' || c == '/' || c == '\\';
}
static bool solid(int x, int y) {
    return !inside(x,y) || terrain_blocks(tile_at(x,y)) || rooms[room_id].rocks[y][x];
}
static void rectangle(int b, int x, int y, int w, int h, char c) {
    for (int j = y; j < y+h; ++j) for (int i = x; i < x+w; ++i) {
        assert(inside(i,j)); rooms[b].tiles[j][i] = c;
    }
}
static void put(int b, int x, int y, char c) { assert(inside(x,y)); rooms[b].tiles[y][x] = c; }
static void add_actor(int b, int x, int y, int kind) {
    Room *r = &rooms[b]; assert(r->actor_count < ACTORS);
    r->actors[r->actor_count++] = (Actor){x,y,(float)x,(float)y,kind,2,true};
}
static void rock(int b, int x, int y) { rooms[b].rocks[y][x] = rooms[b].initial_rocks[y][x] = true; }

static void make_rooms(void) {
    memset(rooms,0,sizeof(rooms));
    for (int b = 0; b < ROOMS; ++b) {
        rectangle(b,0,0,COLS,ROWS,'#'); rectangle(b,1,1,COLS-2,ROWS-2,'.');
    }
    // The hub has four exits. The south wall only opens for all three fuses.
    rectangle(0,1,12,19,1,'#'); put(0,10,12,'G');
    put(0,10,0,'^'); put(0,20,7,'>'); put(0,0,7,'<'); put(0,10,14,'v');
    rectangle(0,4,3,3,2,'~'); rectangle(0,14,3,3,2,'~');
    for (int y = 3; y <= 9; y += 3) { put(0,3,y,'#'); put(0,17,y,'#'); }
    put(0,10,5,'N'); put(0,9,8,'!'); put(0,11,8,'a');
    put(0,6,7,'*'); put(0,14,7,'*'); put(0,5,10,'t'); put(0,15,10,'+');
    put(0,7,9,'*'); put(0,8,9,'*'); put(0,9,9,'*');
    // Lion pen: a blue key and a locked inner enclosure.
    rectangle(1,11,1,1,13,'#'); put(1,11,7,'B'); put(1,0,7,'<');
    rectangle(1,3,3,5,1,'#'); rectangle(1,3,10,5,1,'#');
    rectangle(1,14,4,4,1,'#'); rectangle(1,14,10,4,1,'#');
    put(1,5,2,'b'); put(1,4,6,'a'); put(1,3,8,'!'); put(1,8,12,'+');
    put(1,17,7,'1'); put(1,18,2,'t');
    for (int x = 3; x < 19; x += 3) { put(1,x,5,'*'); put(1,x,9,'*'); }
    add_actor(1,8,6,0); add_actor(1,8,9,0); add_actor(1,15,7,0); add_actor(1,17,12,1);
    // Waterworks: two independent push lanes. Both plates latch the sluice open.
    put(2,10,14,'v'); rectangle(2,17,1,1,13,'#'); put(2,17,7,'=');
    rectangle(2,3,2,4,3,'~'); rectangle(2,3,9,4,3,'~');
    rectangle(2,9,3,7,1,'#'); rectangle(2,9,5,7,1,'#');
    rectangle(2,9,9,7,1,'#'); rectangle(2,9,11,7,1,'#');
    put(2,14,4,'o'); put(2,14,10,'o'); rock(2,10,4); rock(2,10,10);
    put(2,19,7,'2'); put(2,8,7,'!'); put(2,4,7,'a'); put(2,15,7,'+');
    for (int y = 2; y < 13; y += 2) { put(2,2,y,'*'); put(2,16,y,'*'); }
    add_actor(2,8,2,0); add_actor(2,8,12,0);
    // Store room. The clerk sells the key; ricochets can reach behind cover.
    put(3,20,7,'>'); rectangle(3,8,1,1,13,'#'); put(3,8,7,'R');
    rectangle(3,2,3,5,1,'?'); rectangle(3,2,10,5,1,'?');
    rectangle(3,11,3,6,1,'#'); rectangle(3,11,10,6,1,'#');
    put(3,15,12,'*'); put(3,18,8,'t'); put(3,17,6,'a'); put(3,15,7,'N');
    put(3,12,5,'/'); put(3,12,9,'\\');
    put(3,4,7,'3'); put(3,10,7,'!'); put(3,6,12,'+'); put(3,3,2,'a');
    for (int x = 2; x < 19; x += 3) {
        if (rooms[3].tiles[5][x] == '.') put(3,x,5,'*');
        if (rooms[3].tiles[9][x] == '.') put(3,x,9,'*');
    }
    add_actor(3,10,5,1); add_actor(3,6,5,0); add_actor(3,5,11,1);
    // Exit. The sentry's shots bounce around the central enclosure.
    put(4,10,0,'^'); rectangle(4,5,4,1,7,'#'); rectangle(4,15,4,1,7,'#');
    rectangle(4,8,6,5,1,'?'); put(4,10,10,'C'); put(4,10,3,'!');
    put(4,3,3,'+'); put(4,17,3,'a'); put(4,3,11,'*'); put(4,17,11,'*');
    add_actor(4,7,8,1); add_actor(4,13,8,1); add_actor(4,10,12,2);
    put(4,7,11,'/'); put(4,13,11,'\\'); put(4,7,7,'\\'); put(4,13,7,'/');
}

static void enter_room(int b, int x, int y) {
    bool first = !rooms[b].visited;
    room_id = b; rooms[b].visited = true;
    player.x = x; player.y = y; player.vx = (float)x; player.vy = (float)y;
    memset(shots,0,sizeof(shots));
    undo_push.valid = false; enemy_wait = .7f; move_wait = .18f;
    if (first) player.score += 25;
    notify("%s",room_names[b]);
    if (b == 3 && first) notify("Dark board. T: torch.");
}
static void reset_game(void) {
    make_rooms(); memset(shots,0,sizeof(shots));
    player = (Player){.x=10,.y=10,.facing=0,.hp=100,.ammo=40,.torches=3};
    state = PLAY; torch_time = run_time = fire_wait = hurt_wait = shot_wait = 0;
    last_direction = -1; random_state = 0xA341316Cu;
    enter_room(0,10,10);
    notify("Find 3 fuses. The exit is south.");
}
static void hurt(int amount) {
    if (hurt_wait > 0 || state != PLAY) return;
    player.hp = max(0,player.hp-amount); hurt_wait = .9f;
    if (player.hp == 0) state = DEAD;
}
static void kill_actor(int n, bool reward) {
    Actor *a = &rooms[room_id].actors[n]; a->alive = false;
    if (reward) player.score += a->kind == 2 ? 100 : 30;
}
static void check_plates(void) {
    Room *r = &rooms[room_id];
    if (room_id != 2 || r->solved) return;
    int plates = 0, covered = 0;
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) if (r->tiles[y][x] == 'o') {
        ++plates; if (r->rocks[y][x]) ++covered;
    }
    if (plates && plates == covered) {
        r->solved = true;
        for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) if (r->tiles[y][x] == '=') r->tiles[y][x] = '.';
        player.score += 100; notify("Pump running. Gate open.");
    }
}
// BUMP shows a text window. A numbered link runs the object's action script.
typedef enum { SCRIPT_END, SCRIPT_TEXT, SCRIPT_COST, SCRIPT_AMMO, SCRIPT_KEY } Op;
typedef struct { Op op; int amount; const char *text; } Instruction;
static const Instruction ammo_buy[] = {
    {SCRIPT_COST,3,NULL}, {SCRIPT_AMMO,20,NULL},
    {SCRIPT_TEXT,0,"20 shots.\n\n1  Buy another box (3 gems)"}, {SCRIPT_END,0,NULL}
};
static const Instruction key_buy[] = {
    {SCRIPT_COST,3,NULL}, {SCRIPT_KEY,2,NULL},
    {SCRIPT_TEXT,0,"Red key issued."}, {SCRIPT_END,0,NULL}
};
static const Instruction *object_action;
static void run_script(const Instruction *code) {
    for (int pc = 0; pc < 16; ++pc) {
        Instruction i = code[pc];
        switch (i.op) {
            case SCRIPT_END: return;
            case SCRIPT_COST:
                if (player.gems < i.amount) { dialogue("SHOP","You need 3 gems."); return; }
                player.gems -= i.amount; break;
            case SCRIPT_AMMO: player.ammo += i.amount; break;
            case SCRIPT_KEY: player.keys |= i.amount; object_action = NULL; break;
            case SCRIPT_TEXT: dialogue("SHOP",i.text); return;
        }
    }
}
static void talk_to_object(char c) {
    object_action = NULL;
    if (c == 'N' && room_id == 0) {
        dialogue("AMMO","1  Buy 20 shots for 3 gems\n\nEnter  Leave"); object_action = ammo_buy;
    } else if (c == 'N') {
        if (rooms[3].tiles[7][8] == '.' || (player.keys&2)) dialogue("CLERK","You already have access.");
        else { dialogue("CLERK","1  Buy a red key for 3 gems\n\nEnter  Leave"); object_action = key_buy; }
    } else if (room_id == 2) dialogue("PUMP","Two stones. Two contacts.\nPush both stones east.\n\nU  Undo last push\nR  Reset stones");
    else if (room_id == 1) dialogue("LIONS","Lions chase. Tigers shoot.\n\nShift + direction: shoot\nSpace: fire ahead");
    else if (room_id == 4) dialogue("EXIT","Touch the white marker.\n\nThe sentry shoots clockwise.\nShots bounce off / and \\.");
    else dialogue("NOTICE","EXIT POWER: OFF\n\nThree fuses missing.\n\nNorth: pump room\nEast:  lion pen\nWest:  store room");
}
static void collect(char c) {
    Room *r = &rooms[room_id]; bool taken = true;
    switch (c) {
        case '*': player.gems++; player.score += 10; player.hp = min(100,player.hp+2); break;
        case 'a': player.ammo += 20; notify("+20 ammo"); break;
        case '+': player.hp = min(100,player.hp+35); notify("+35 health"); break;
        case 't': player.torches++; notify("+1 torch"); break;
        case 'b': case 'r': case 'p': {
            int k = c == 'b' ? 0 : c == 'r' ? 1 : 2;
            player.keys |= 1 << k; player.score += 25;
            notify("%s key",k == 0 ? "Blue" : k == 1 ? "Red" : "Purple"); break;
        }
        case '1': case '2': case '3': {
            int bit = 1 << (c-'1'); player.fuses |= bit; player.score += 200;
            player.hp = min(100,player.hp+20); player.ammo += 10;
            notify("Fuse found. +20 health, +10 ammo."); break;
        }
        case 'C': player.score += 500; state = WON; break;
        default: taken = false; break;
    }
    if (taken) { r->tiles[player.y][player.x] = '.'; }
}
static void try_move(int d) {
    if (state != PLAY) return;
    player.facing = d;
    int x = player.x+dx[d], y = player.y+dy[d];
    if (!inside(x,y)) return;
    Room *r = &rooms[room_id]; char c = tile_at(x,y);
    if (c == 'N' || c == '!') { talk_to_object(c); return; }
    if (c == 'B' || c == 'R' || c == 'P') {
        int k = c == 'B' ? 0 : c == 'R' ? 1 : 2;
        if (!(player.keys & (1 << k))) { notify("Locked. %s key required.",k == 0 ? "blue" : k == 1 ? "red" : "purple"); return; }
        player.keys &= ~(1 << k); r->tiles[y][x] = '.'; c = '.';
        notify("Door unlocked.");
    }
    if (c == 'G') {
        if (player.fuses != 7) { notify("Exit power: %d/3 fuses",!!(player.fuses&1)+!!(player.fuses&2)+!!(player.fuses&4)); return; }
        r->tiles[y][x] = '.'; c = '.'; notify("Exit power restored.");
    }
    if (terrain_blocks(c)) return;
    if (r->rocks[y][x]) {
        int tx = x+dx[d], ty = y+dy[d];
        if (solid(tx,ty) || actor_at(tx,ty) >= 0 || (tile_at(tx,ty) != '.' && tile_at(tx,ty) != 'o')) return;
        undo_push = (Push){true,x,y,tx,ty,player.x,player.y};
        r->rocks[y][x] = false; r->rocks[ty][tx] = true; check_plates();
    }
    int actor = actor_at(x,y);
    if (actor >= 0) { hurt(14); kill_actor(actor,false); if (state == DEAD) return; }
    player.x = x; player.y = y;
    collect(c);
    if (c == '^' || c == '>' || c == 'v' || c == '<') {
        if (room_id == 0) {
            if (c == '>') enter_room(1,1,7);
            else if (c == '^') enter_room(2,10,13);
            else if (c == '<') enter_room(3,19,7);
            else enter_room(4,10,1);
        } else {
            int b = room_id;
            enter_room(0,b == 1 ? 19 : b == 3 ? 1 : 10,b == 2 ? 1 : b == 4 ? 13 : 7);
        }
    }
}
static bool projectile_hit(Shot *s) {
    if (!inside(s->x,s->y) || --s->steps <= 0) return true;
    char c = tile_at(s->x,s->y);
    if (c == '/' || c == '\\') {
        int old_dx = s->dx;
        s->dx = c == '/' ? -s->dy : s->dy;
        s->dy = c == '/' ? -old_dx : old_dx;
        return false;
    }
    if (c == '?') {
        if (!s->hostile) { rooms[room_id].tiles[s->y][s->x] = '.'; }
        return true;
    }
    if (s->hostile && s->x == player.x && s->y == player.y) { hurt(12); return true; }
    int a = actor_at(s->x,s->y);
    if (!s->hostile && a >= 0) { kill_actor(a,true); return true; }
    // Water and pickups do not stop bullets. Doors, stone, people and boulders do.
    return (c != '~' && terrain_blocks(c)) || rooms[room_id].rocks[s->y][s->x] || c == '^' || c == '>' || c == '<' || c == 'v';
}
static void shoot(int x, int y, int d, bool hostile) {
    for (int i = 0; i < SHOTS; ++i) if (!shots[i].active) {
        shots[i] = (Shot){x+dx[d],y+dy[d],dx[d],dy[d],true,hostile,80};
        if (projectile_hit(&shots[i])) shots[i].active = false;
        break;
    }
}
static void player_fire(int d) {
    player.facing = d;
    if (fire_wait > 0) return;
    fire_wait = .24f;
    if (player.ammo <= 0) { notify("Out of ammo."); return; }
    --player.ammo; shoot(player.x,player.y,d,false);
}
static void rebuild_distances(void) {
    int qx[COLS*ROWS], qy[COLS*ROWS], head = 0, tail = 0;
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) distance_field[y][x] = 999;
    distance_field[player.y][player.x] = 0; qx[tail] = player.x; qy[tail++] = player.y;
    while (head < tail) {
        int x = qx[head], y = qy[head++];
        for (int d = 0; d < 4; ++d) {
            int nx = x+dx[d], ny = y+dy[d];
            if (solid(nx,ny) || tile_at(nx,ny) == '^' || tile_at(nx,ny) == 'v' || tile_at(nx,ny) == '<' || tile_at(nx,ny) == '>') continue;
            if (distance_field[ny][nx] <= distance_field[y][x]+1) continue;
            distance_field[ny][nx] = distance_field[y][x]+1;
            assert(tail < COLS*ROWS); qx[tail] = nx; qy[tail++] = ny;
        }
    }
}
static bool clear_line(int x, int y, int d) {
    for (int n = 0; n < COLS; ++n) {
        x += dx[d]; y += dy[d];
        if (x == player.x && y == player.y) return true;
        if (solid(x,y) && tile_at(x,y) != '~') return false;
    }
    return false;
}
static void enemy_tick(void) {
    rebuild_distances(); Room *r = &rooms[room_id];
    for (int i = 0; i < r->actor_count && state == PLAY; ++i) {
        Actor *a = &r->actors[i]; if (!a->alive) continue;
        if (a->kind == 2) { a->facing = (a->facing+1)%4; shoot(a->x,a->y,a->facing,true); continue; }
        if (a->kind == 1 && (a->x == player.x || a->y == player.y)) {
            int d = a->x == player.x ? (player.y < a->y ? 0 : 2) : (player.x < a->x ? 3 : 1);
            if (clear_line(a->x,a->y,d)) { a->facing = d; shoot(a->x,a->y,d,true); continue; }
        }
        if (distance_field[a->y][a->x] > 16) continue;
        int best = distance_field[a->y][a->x], direction = -1;
        int offset = (int)(next_random()%4);
        for (int j = 0; j < 4; ++j) {
            int d = (j+offset)%4, x = a->x+dx[d], y = a->y+dy[d];
            if (solid(x,y) || actor_at(x,y) >= 0) continue;
            if (distance_field[y][x] < best) { best = distance_field[y][x]; direction = d; }
        }
        if (direction < 0) continue;
        int x = a->x+dx[direction], y = a->y+dy[direction]; a->facing = direction;
        if (x == player.x && y == player.y) { hurt(14); kill_actor(i,false); }
        else {
            a->x = x; a->y = y;
            for (int j = 0; j < SHOTS; ++j) if (shots[j].active && !shots[j].hostile && shots[j].x == x && shots[j].y == y) {
                shots[j].active = false; kill_actor(i,true); break;
            }
        }
    }
}
static void undo_boulder(void) {
    if (!undo_push.valid) { notify("No push to undo."); return; }
    Room *r = &rooms[room_id];
    if (actor_at(undo_push.x,undo_push.y) >= 0 || actor_at(undo_push.px,undo_push.py) >= 0) {
        notify("Blocked."); return;
    }
    r->rocks[undo_push.ty][undo_push.tx] = false; r->rocks[undo_push.y][undo_push.x] = true;
    player.x = undo_push.px; player.y = undo_push.py; undo_push.valid = false;
    notify("Push undone.");
}
static void reset_boulders(void) {
    Room *r = &rooms[room_id];
    // Never materialize a boulder over the player or an enemy.
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) if (r->initial_rocks[y][x] &&
        ((player.x == x && player.y == y) || actor_at(x,y) >= 0)) { notify("Starting tile occupied."); return; }
    memcpy(r->rocks,r->initial_rocks,sizeof(r->rocks)); undo_push.valid = false;
    notify("Stones reset.");
}

// EGA colours. Each lit font pixel becomes an extruded piece of geometry.
static const uint32_t swatches[16] = {
    0x000000FF,0x0000AAFF,0x00AA00FF,0x00AAAAFF,
    0xAA0000FF,0xAA00AAFF,0xAA5500FF,0xAAAAAAFF,
    0x555555FF,0x5555FFFF,0x55FF55FF,0x55FFFFFF,
    0xFF5555FF,0xFF55FFFF,0xFFFF55FF,0xFFFFFFFF,
};
#define SWATCHES 16
#define MAX_TRIANGLES 12000
static struct { Vector3 p[MAX_TRIANGLES*3]; int color[MAX_TRIANGLES], count; } mesh_builder;
static void tri(Vector3 a, Vector3 b, Vector3 c, int color) {
    int n = mesh_builder.count++; assert(n < MAX_TRIANGLES);
    mesh_builder.p[n*3] = a; mesh_builder.p[n*3+1] = b; mesh_builder.p[n*3+2] = c;
    mesh_builder.color[n] = color;
}
static void box(float x, float y, float z, float w, float h, float d, int color) {
    Vector3 p[8];
    for (int i = 0; i < 8; ++i) p[i] = v3(x+(i&1 ? w : -w)*.5f,y+(i&2 ? h : -h)*.5f,z+(i&4 ? d : -d)*.5f);
    const int faces[6][4] = {{4,5,7,6},{1,0,2,3},{5,1,3,7},{0,4,6,2},{6,7,3,2},{0,1,5,4}};
    for (int i = 0; i < 6; ++i) {
        const int *f = faces[i]; tri(p[f[0]],p[f[1]],p[f[2]],color); tri(p[f[0]],p[f[2]],p[f[3]],color);
    }
}
static Model3D *finish_mesh(void) {
    Model3D *m = model3d_create_from_triangles(mesh_builder.p,mesh_builder.count);
    free(m->uvs); m->uvs = malloc(sizeof(Vector2)*SWATCHES); assert(m->uvs);
    for (int i = 0; i < SWATCHES; ++i) m->uvs[i] = (Vector2){(i+.5f)/SWATCHES,.5f};
    for (int i = 0; i < mesh_builder.count; ++i) {
        int c = mesh_builder.color[i]; m->tri_uvs[i] = (Vector3Int){c,c,c};
    }
    m->texture = palette; mesh_builder.count = 0; return m;
}
static void make_board_mesh(int b) {
    if (board_mesh[b]) model3d_free(board_mesh[b]);
    box(0,-.13f,0,COLS,.25f,ROWS,0);
    static const int wall_colors[ROOMS] = {9,2,3,5,4};
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) {
        float wx = (float)(x-COLS/2), wz = (float)(y-ROWS/2);
        if (rooms[b].tiles[y][x] == '#') {
            box(wx,.27f,wz,.98f,.54f,.98f,wall_colors[b]);
            box(wx,.29f,wz+.495f,.97f,.025f,.01f,0);
        }
    }
    board_mesh[b] = finish_mesh();
}
static bool font_pixel(int c, int x, int y) {
    return (get_pixel(font->bitmap,(c%16)*8+x,(c/16)*8+y)&255) > 128;
}
static void make_models(void) {
    palette = create_bitmap(SWATCHES,1); memcpy(palette->pixels,swatches,sizeof(swatches));
    cube = model3d_create_box(v3(1,1,1));
    for (int c = 1; c < 256; ++c) {
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8;) {
            if (!font_pixel(c,x,y)) { ++x; continue; }
            int start = x;
            while (x < 8 && font_pixel(c,x,y)) ++x;
            box(((start+x)*.5f-4)*.105f,.08f,(y-3.5f)*.105f,(x-start)*.105f,.16f,.105f,15);
        }
        if (mesh_builder.count) glyphs[c] = finish_mesh();
    }
    for (int b = 0; b < ROOMS; ++b) make_board_mesh(b);
}
static void draw_at(Model3D *m, Vector3 p, Vector3 size, uint32_t color) {
    Matrix4 matrix;
    MATRIX4_SET(matrix,size.x,0,0,p.x, 0,size.y,0,p.y, 0,0,size.z,p.z, 0,0,0,1);
    m->color = color; draw_model3d_m(scene,&camera,m,matrix);
}
static void block(float x, float y, float h, float w, float height, float depth, uint32_t color) {
    draw_at(cube,world(x,y,h),v3(w,height,depth),color);
}
static void glyph(int c, float x, float y, float h, uint32_t color) {
    if (glyphs[c]) draw_at(glyphs[c],world(x,y,h),VECTOR3_ONE,color);
}
static uint32_t lit_color(int x, int y, uint32_t color) {
    if (room_id != 3) return color;
    int distance = abs(x-player.x)+abs(y-player.y);
    if (distance <= (torch_time > 0 ? 8 : 2)) return color;
    return ((color>>1)&0x7F7F7F00)|255;
}
static void draw_world(void) {
    fill(scene,INK);
    camera.position = v3(0,40,28); camera.target = v3(0,0,0); camera.up = VECTOR3_UP;
    camera3d_update(&camera);
    camera3d_set_perspective(camera.projection,deg_to_rad(23.5f),(float)SCENE_W/SCENE_H,.1f,100);
    chao_3d_directional_light.enabled = true;
    chao_3d_directional_light.direction = v3(-.4f,.9f,.2f);
    chao_3d_directional_light.ambient = .65f; chao_3d_fog.enabled = false;
    chao_blend_mode = BLEND_NONE;
    draw_at(board_mesh[room_id],VECTOR3_ZERO,VECTOR3_ONE,room_id == 3 && torch_time <= 0 ? MUTED : PAPER);
    Room *r = &rooms[room_id];
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) {
        char c = r->tiles[y][x]; int g = 0; uint32_t color = PAPER;
        switch (c) {
            case '~':
                block(x,y,.025f,.98f,.05f,.98f,swatches[1]);
                glyph(247,x,y,.06f,swatches[(int)(clock_time*3+x+y)%3 ? 9 : 11]); break;
            case '?': block(x,y,.16f,.92f,.32f,.92f,swatches[6]); g = 177; color = GOLD; break;
            case 'B': case 'R': case 'P': case 'G': case '=': {
                color = c == 'B' ? key_colors[0] : c == 'R' ? key_colors[1] : c == 'P' ? key_colors[2] : GOLD;
                block(x,y,.27f,.94f,.54f,.94f,color);
                glyph(c == '=' ? 240 : 10,x,y,.55f,PAPER); break;
            }
            case '*': g = 4; color = swatches[11]; break;
            case 'b': case 'r': case 'p': g = 12; color = key_colors[c == 'b' ? 0 : c == 'r' ? 1 : 2]; break;
            case '1': case '2': case '3':
                block(x,y,.045f,.91f,.09f,.91f,key_colors[c-'1']); g = c; break;
            case '+': g = 3; color = RED; break;
            case 'a': g = 132; color = MINT; break;
            case 't': g = 157; color = GOLD; break;
            case 'o': g = 249; color = r->rocks[y][x] ? swatches[10] : GOLD; break;
            case 'N': g = 2; color = PAPER; block(x,y,.03f,.95f,.06f,.95f,swatches[room_id == 0 ? 2 : 5]); break;
            case '!': g = 232; color = PAPER; break;
            case 'C': g = 127; color = PAPER; block(x,y,.04f,.95f,.08f,.95f,swatches[2]); break;
            case '/': case '\\': g = c; color = MINT; break;
            case '^': g = 24; color = PAPER; break;
            case '>': g = 26; color = PAPER; break;
            case 'v': g = 25; color = PAPER; break;
            case '<': g = 27; color = PAPER; break;
        }
        if (g) glyph(g,x,y,c == '?' ? .33f : .10f,lit_color(x,y,color));
        if (r->rocks[y][x]) {
            block(x,y,.15f,.82f,.30f,.82f,swatches[8]); glyph(9,x,y,.31f,PAPER);
        }
    }
    for (int i = 0; i < r->actor_count; ++i) {
        Actor *a = &r->actors[i]; if (!a->alive) continue;
        const int arrows[4] = {24,26,25,27};
        int g = a->kind == 2 ? arrows[a->facing] : a->kind == 1 ? 227 : 234;
        uint32_t color = lit_color(a->x,a->y,a->kind == 1 ? RED : GOLD);
        glyph(g,a->vx,a->vy,.14f,color);
    }
    block(player.vx,player.vy,.05f,.91f,.10f,.91f,swatches[1]);
    glyph(2,player.vx,player.vy,.12f,hurt_wait > 0 && ((int)(clock_time*18)&1) ? RED : PAPER);
    int d = player.facing;
    block(player.vx+dx[d]*.43f,player.vy+dy[d]*.43f,.06f,dx[d] ? .08f : .28f,.04f,dy[d] ? .08f : .28f,GOLD);
    chao_3d_directional_light.enabled = false;
    for (int i = 0; i < SHOTS; ++i) if (shots[i].active)
        glyph(248,shots[i].x,shots[i].y,.2f,shots[i].hostile ? RED : PAPER);
}
static void text(int x, int y, uint32_t color, const char *fmt, ...) {
    char buffer[512]; va_list args; va_start(args,fmt); vsnprintf(buffer,sizeof(buffer),fmt,args); va_end(args);
    ascii_font_draw(chao_canvas,font,x,y,color,"%s",buffer);
}
static void ui_glyph(int c, int x, int y, uint32_t color) {
    blit_rect(chao_canvas,font->bitmap,(RectInt){(c%16)*8,(c/16)*8,8,8},(Vector2Int){x,y},color);
}
static int fuse_count(void) { return !!(player.fuses&1) + !!(player.fuses&2) + !!(player.fuses&4); }
static void draw_hud(void) {
    fill(chao_canvas,INK); draw_world(); blit(chao_canvas,scene,0,8,PAPER);
    fill_rect(chao_canvas,480,0,160,HEIGHT,PANEL);
    text(8,0,MUTED,"%-20s",room_names[room_id]);
    text(512,16,GOLD,"ZZT 3D");
    text(496,48,PAPER,"Health %5d",player.hp);
    text(496,64,PAPER,"Ammo   %5d",player.ammo);
    text(496,80,PAPER,"Gems   %5d",player.gems);
    text(496,96,PAPER,"Torches%5d",player.torches);
    text(496,112,PAPER,"Score  %5d",player.score);
    text(496,144,PAPER,"Keys");
    for (int i = 0; i < 3; ++i) ui_glyph(12,544+i*16,144,player.keys&(1<<i) ? key_colors[i] : LINE);
    text(496,168,PAPER,"Fuses");
    for (int i = 0; i < 3; ++i) ui_glyph('1'+i,544+i*16,168,player.fuses&(1<<i) ? key_colors[i] : LINE);
    if (torch_time > 0) text(496,192,GOLD,"Light: %2ds",(int)ceilf(torch_time));
    text(496,232,PAPER,"Arrows  Move\nShift   Shoot\nSpace   Shoot\nT       Torch\nU       Undo\nR       Reset\nH       Help\nEsc     Pause");
    fill_rect(chao_canvas,0,384,480,16,PANEL);
    if (message_time > 0) text(8,388,GOLD,"%.58s",message);
    if (state == PLAY) return;
    int x = 64, y = 64, w = 440, h = 256;
    fill_rect(chao_canvas,x+8,y+8,w,h,INK); fill_rect(chao_canvas,x,y,w,h,swatches[7]);
    draw_rect(chao_canvas,x+3,y+3,w-6,h-6,1,PAPER);
    draw_line(chao_canvas,x+8,y+32,x+w-9,y+32,PAPER);
    const char *heading = state == TALK ? dialog_title : state == PAUSE ? "PAUSED" : state == DEAD ? "GAME OVER" : state == WON ? "EXIT" : "HELP";
    text(x+16,y+16,PANEL,"%s",heading);
    if (state == TALK) text(x+16,y+48,INK,"%s",dialog_body);
    else if (state == HELP) text(x+16,y+48,INK,"Arrows / WASD    Move\nShift + arrow   Shoot without moving\nSpace           Shoot ahead\nT               Light a torch\nU               Undo last push\nR               Reset board's stones\n\nBump objects to read or use them.\nKeys match doors. / and \\ bounce shots.\nFind 3 fuses, then take the south exit.");
    else if (state == PAUSE) text(x+16,y+48,INK,"N  New game\nH  Help\n\nEnter / Esc  Resume");
    else if (state == DEAD) text(x+16,y+48,INK,"Score: %d\nFuses: %d/3\n\nR  Restart",player.score,fuse_count());
    else text(x+16,y+48,INK,"You got out.\n\nScore: %d\nTime: %d:%02d\n\nR  Restart",player.score,(int)run_time/60,(int)run_time%60);
    text(x+16,y+h-24,PANEL,"%s",state == DEAD || state == WON ? "R: Restart" : "Enter: Close");
}

static void simulate(float dt) {
    run_time += dt;
    move_wait = fmaxf(0,move_wait-dt); fire_wait = fmaxf(0,fire_wait-dt);
    hurt_wait = fmaxf(0,hurt_wait-dt); torch_time = fmaxf(0,torch_time-dt);
    int direction = -1;
    const int a[4] = {KEY_W,KEY_D,KEY_S,KEY_A}, b[4] = {KEY_UP,KEY_RIGHT,KEY_DOWN,KEY_LEFT};
    // A new direction gets priority over an older held key, for crisp grid turns.
    for (int d = 0; d < 4; ++d) if (input.just_pressed[a[d]] || input.just_pressed[b[d]]) { direction = d; break; }
    if (direction < 0) for (int d = 0; d < 4; ++d) if (input.pressed[a[d]] || input.pressed[b[d]]) { direction = d; break; }
    if (input.just_pressed[KEY_T]) {
        if (torch_time > 0) notify("Torch already lit.");
        else if (player.torches > 0) { --player.torches; torch_time = 25; notify("Torch lit: 25 seconds."); }
        else notify("Out of torches.");
    }
    if (input.just_pressed[KEY_U]) undo_boulder();
    if (input.just_pressed[KEY_R]) reset_boulders();
    bool aiming = input.pressed[KEY_LSHIFT];
    if (direction >= 0) {
        if (aiming) player_fire(direction);
        else if (move_wait <= 0 || direction != last_direction) {
            try_move(direction); move_wait = .14f;
        }
    }
    last_direction = direction;
    if (state != PLAY) return;
    if (input.pressed[KEY_SPACE]) player_fire(player.facing);
    shot_wait -= dt;
    while (shot_wait <= 0) {
        shot_wait += .065f;
        for (int i = 0; i < SHOTS; ++i) if (shots[i].active) {
            shots[i].x += shots[i].dx; shots[i].y += shots[i].dy;
            if (projectile_hit(&shots[i])) shots[i].active = false;
        }
    }
    enemy_wait -= dt;
    if (enemy_wait <= 0 && state == PLAY) { enemy_wait += .43f; enemy_tick(); }
    // Walking into an existing hostile bullet hurts as well as being shot at.
    for (int i = 0; i < SHOTS; ++i) if (shots[i].active && shots[i].hostile && shots[i].x == player.x && shots[i].y == player.y) {
        hurt(12); shots[i].active = false;
    }
}
static void update(float dt) {
    dt = clamp(dt,0,.05f);
    if (state == TALK || state == HELP) {
        if (state == TALK && input.just_pressed[KEY_1] && object_action) run_script(object_action);
        else if (input.just_pressed[KEY_ENTER] || input.just_pressed[KEY_ESC] || (state == HELP && input.just_pressed[KEY_H])) state = PLAY;
    } else if (state == PAUSE) {
        if (input.just_pressed[KEY_ESC] || input.just_pressed[KEY_ENTER]) state = PLAY;
        else if (input.just_pressed[KEY_N]) reset_game();
        else if (input.just_pressed[KEY_H]) state = HELP;
    } else if (state == DEAD || state == WON) {
        if (input.just_pressed[KEY_R]) reset_game();
    } else if (input.just_pressed[KEY_ESC]) state = PAUSE;
    else if (input.just_pressed[KEY_H]) state = HELP;
    else simulate(dt);
    if (state == PLAY || state == WON || state == DEAD) {
        clock_time += dt; message_time -= dt;
        float ease = 1-expf(-dt*24);
        player.vx += (player.x-player.vx)*ease; player.vy += (player.y-player.vy)*ease;
        Room *r = &rooms[room_id];
        for (int i = 0; i < r->actor_count; ++i) {
            Actor *a = &r->actors[i]; a->vx += (a->x-a->vx)*ease; a->vy += (a->y-a->vy)*ease;
        }
    }
    draw_hud();
}
static void init(void) {
    if (!headless) platform_set_window_size(WIDTH*2,HEIGHT*2);
    font = ascii_font_create(assets_load_bitmap("zzt_font","assets/fonts/cp437_8x8.png"),false);
    font->char_spacing = 0; font->space_size = 8; font->line_spacing = 8;
    scene = create_bitmap(SCENE_W,SCENE_H); create_bitmap_z_buffer(scene);
    reset_game(); make_models();
}
static void cleanup(void) {
    for (int b = 0; b < ROOMS; ++b) model3d_free(board_mesh[b]);
    model3d_free(cube);
    for (int c = 0; c < 256; ++c) model3d_free(glyphs[c]);
    free_bitmap(palette); free_bitmap(scene); ascii_font_free(font);
}

// These checks exercise the actual movement, interaction and combat code. The
// complete route runs without creatures so that puzzle reachability is deterministic.
static int test_failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition); ++test_failures; } } while (0)
static bool walk_to(int tx, int ty) {
    int previous[ROWS][COLS], queue[COLS*ROWS], head = 0, tail = 0;
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) previous[y][x] = -1;
    queue[tail++] = player.y*COLS+player.x; previous[player.y][player.x] = 4;
    while (head < tail && previous[ty][tx] < 0) {
        int cell = queue[head++], x = cell%COLS, y = cell/COLS;
        for (int d = 0; d < 4; ++d) {
            int nx = x+dx[d], ny = y+dy[d];
            if (!inside(nx,ny) || previous[ny][nx] >= 0 || rooms[room_id].rocks[ny][nx]) continue;
            char c = tile_at(nx,ny);
            bool can_open = (c == 'B' && (player.keys&1)) || (c == 'R' && (player.keys&2)) || (c == 'P' && (player.keys&4)) || (c == 'G' && player.fuses == 7);
            if (terrain_blocks(c) && !can_open) continue;
            if ((c == '^' || c == '>' || c == '<' || c == 'v') && (nx != tx || ny != ty)) continue;
            previous[ny][nx] = d; assert(tail < COLS*ROWS); queue[tail++] = ny*COLS+nx;
        }
    }
    if (previous[ty][tx] < 0) { fprintf(stderr,"No route: room %d (%d,%d) -> (%d,%d)\n",room_id,player.x,player.y,tx,ty); return false; }
    int path[COLS*ROWS], count = 0, x = tx, y = ty;
    while (x != player.x || y != player.y) {
        int d = previous[y][x]; path[count++] = d; x -= dx[d]; y -= dy[d];
    }
    for (int i = count-1; i >= 0; --i) try_move(path[i]);
    return true;
}
static int self_test(void) {
    reset_game(); talk_to_object('N');
    input.just_pressed[KEY_1] = true; update(0);
    CHECK(player.gems == 0); CHECK(player.ammo == 40); CHECK(state == TALK);
    player.gems = 3; update(0); chao_input_reset_keys(input.just_pressed);
    CHECK(player.gems == 0); CHECK(player.ammo == 60); CHECK(state == TALK);
    reset_game(); enter_room(4,10,1);
    Shot reflected = {.x=7,.y=11,.dx=1,.dy=0,.steps=80};
    CHECK(!projectile_hit(&reflected)); CHECK(reflected.dx == 0 && reflected.dy == -1);
    reflected = (Shot){.x=13,.y=11,.dx=1,.dy=0,.steps=80,.hostile=true};
    CHECK(!projectile_hit(&reflected)); CHECK(reflected.dx == 0 && reflected.dy == 1);
    reflected.steps = 1; CHECK(projectile_hit(&reflected));
    reset_game();
    player.x = 1; player.y = 1; try_move(3); CHECK(player.x == 1);
    player.x = 10; player.y = 6; try_move(0); CHECK(state == TALK); CHECK(player.y == 6); state = PLAY;
    player.x = 10; player.y = 11; try_move(2); CHECK(player.y == 11); CHECK(tile_at(10,12) == 'G');
    enter_room(1,10,7); try_move(1); CHECK(player.x == 10); CHECK(tile_at(11,7) == 'B');
    player.keys = 1; try_move(1); CHECK(player.x == 11); CHECK(player.keys == 0); CHECK(tile_at(11,7) == '.');
    enter_room(3,3,4); int ammo = player.ammo; player_fire(0); CHECK(tile_at(3,3) == '.'); CHECK(player.ammo == ammo-1);
    // Friendly shots, enemy attacks, invulnerability, projectiles and depleted ammo.
    enter_room(1,7,6); fire_wait = 0; player_fire(1); CHECK(!rooms[1].actors[0].alive);
    int hp = player.hp; hurt_wait = 0; hurt(12); hurt(12); CHECK(player.hp == hp-12);
    player.ammo = 0; fire_wait = 0; player_fire(1); CHECK(player.ammo == 0);
    reset_game(); enter_room(1,8,7); enemy_tick(); CHECK(player.hp == 86); CHECK(!rooms[1].actors[0].alive);
    reset_game(); enter_room(4,10,9); shoot(10,12,0,true);
    for (int n = 0; n < 3; ++n) for (int i = 0; i < SHOTS; ++i) if (shots[i].active) {
        shots[i].y += shots[i].dy; if (projectile_hit(&shots[i])) shots[i].active = false;
    }
    CHECK(player.hp == 88);
    reset_game();
    for (int b = 0; b < ROOMS; ++b) rooms[b].actor_count = 0;
    CHECK(walk_to(20,7)); CHECK(room_id == 1);
    CHECK(walk_to(5,2)); CHECK(player.keys&1);
    CHECK(walk_to(17,7)); CHECK(player.fuses == 1); CHECK(!(player.keys&1));
    CHECK(walk_to(0,7)); CHECK(room_id == 0);
    CHECK(walk_to(10,0)); CHECK(room_id == 2);
    CHECK(walk_to(9,4)); try_move(1); CHECK(rooms[2].rocks[4][11]);
    undo_boulder(); CHECK(rooms[2].rocks[4][10]); CHECK(player.x == 9);
    try_move(1); try_move(1); reset_boulders(); CHECK(rooms[2].rocks[4][10]);
    CHECK(walk_to(9,4)); for (int n = 0; n < 4; ++n) try_move(1);
    CHECK(rooms[2].rocks[4][14]); CHECK(!rooms[2].solved);
    CHECK(walk_to(9,10)); for (int n = 0; n < 4; ++n) try_move(1);
    CHECK(rooms[2].solved); CHECK(tile_at(17,7) == '.');
    CHECK(walk_to(19,7)); CHECK(player.fuses == 3);
    CHECK(walk_to(10,14)); CHECK(room_id == 0);
    CHECK(walk_to(0,7)); CHECK(room_id == 3);
    CHECK(walk_to(16,7)); try_move(3); CHECK(state == TALK);
    CHECK(object_action == key_buy); run_script(object_action); CHECK(player.keys&2); state = PLAY;
    CHECK(walk_to(4,7)); CHECK(player.fuses == 7); CHECK(!(player.keys&2));
    CHECK(walk_to(20,7)); CHECK(room_id == 0);
    CHECK(walk_to(10,14)); CHECK(room_id == 4); CHECK(rooms[0].tiles[12][10] == '.');
    CHECK(walk_to(10,10)); CHECK(state == WON); CHECK(player.hp > 0);
    // Pause/help freeze the simulation, and a full restart restores every board.
    reset_game(); state = PAUSE; float before = run_time;
    update(.05f); CHECK(run_time == before);
    state = HELP; update(.05f); CHECK(run_time == before);
    reset_game(); CHECK(player.hp == 100); CHECK(player.fuses == 0); CHECK(rooms[1].tiles[7][11] == 'B');
    CHECK(rooms[2].rocks[4][10]); CHECK(rooms[3].tiles[3][3] == '?');
    for (int b = 0; b < ROOMS; ++b) {
        enter_room(b,b == 1 ? 1 : b == 3 ? 19 : 10,b == 4 ? 1 : b == 2 ? 13 : 7);
        draw_hud();
        int painted = 0; float nearest = INFINITY, farthest = 0;
        for (int i = 0; i < SCENE_W*SCENE_H; ++i) if (isfinite(scene->z_buffer[i])) {
            ++painted; nearest = fminf(nearest,scene->z_buffer[i]); farthest = fmaxf(farthest,scene->z_buffer[i]);
        }
        CHECK(painted > 30000); CHECK(painted < SCENE_W*SCENE_H);
        CHECK(farthest-nearest > 5); // The renderer requires perspective depth for occlusion.
    }
    fprintf(stdout,"zzt: %s\n",test_failures ? "FAILED" : "all checks passed");
    return test_failures ? 1 : 0;
}
static bool snapshot(const char *path) {
    FILE *file = fopen(path,"wb"); if (!file) { perror(path); return false; }
    fprintf(file,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
    bool ok = true;
    for (int i = 0; i < WIDTH*HEIGHT; ++i) {
        uint32_t c = chao_canvas->pixels[i]; unsigned char rgb[3] = {(unsigned char)(c>>24),(unsigned char)(c>>16),(unsigned char)(c>>8)};
        if (fwrite(rgb,1,3,file) != 3) { ok = false; break; }
    }
    if (fclose(file) != 0) ok = false;
    return ok;
}
int main(int argc, char **argv) {
    if (argc > 1) {
        bool test = argc == 2 && strcmp(argv[1],"--self-test") == 0;
        bool shot = (argc == 3 || argc == 4) && strcmp(argv[1],"--snapshot") == 0;
        if (!test && !shot) { fprintf(stderr,"Usage: %s [--self-test | --snapshot out.ppm [0..4]]\n",argv[0]); return 1; }
        int b = 0;
        if (shot && argc == 4) {
            if (strlen(argv[3]) != 1 || argv[3][0] < '0' || argv[3][0] > '4') { fprintf(stderr,"Board must be 0..4\n"); return 1; }
            b = argv[3][0]-'0';
        }
        headless = true; chao_canvas = create_bitmap(WIDTH,HEIGHT); create_bitmap_z_buffer(chao_canvas); chao_input_init(); init();
        int result;
        if (test) result = self_test();
        else {
            enter_room(b,b == 1 ? 1 : b == 3 ? 19 : 10,b == 4 ? 1 : b == 2 ? 13 : 10);
            clock_time = 1.2f; draw_hud(); result = snapshot(argv[2]) ? 0 : 1;
        }
        cleanup(); assets_free_bitmaps(); free_bitmap(chao_canvas); chao_canvas = NULL; return result;
    }
    chao_run("ZZT 3D",WIDTH,HEIGHT,&init,&update);
    cleanup(); return 0;
}

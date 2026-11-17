# chao

A small header-only software renderer: 2D blitting, triangles, a basic 3D pipeline, fonts, tweens and more.
Everything is drawn on the CPU into `chao_canvas`, and a platform backend puts it on screen.

## Building

The build uses [nob.h](https://github.com/tsoding/nob.h). Compile the build script once; after that it rebuilds itself when `nob.c` changes.

```sh
cc -o nob nob.c
./nob                        # debug build of tools + examples -> build/debug/
./nob release                # optimized build                 -> build/release/
./nob run hello_triangle     # build and launch an example
```

## Examples

- `hello_triangle`: a spinning, vertex-colored triangle. The smallest possible chao program.
- `terrain`: a heightmap mesh and texture generated from `noise.h`, drawn with the 3D pipeline (directional light and fog), with a free-fly camera.
  Click to capture the mouse and Esc to release it. WASD moves, Space/E goes up, Q/Ctrl goes down, Shift moves faster, the arrow keys look around and R generates a new terrain.
- `beacon_run`: a small but complete game. Glide a paper plane over a generated island and light 5 beacons before the fog swallows it. Thermals, marked by clouds, let you climb.
  A/D turn, W/S dive and climb, Esc pauses. Everything in it (island, models, effects) is generated in code.
- `scrap_tug`: an orbital salvage game in one `main.c`. Tow three wrecks to the refinery before your fuel or hull runs out. Heavy cargo swings on the cable and can smash pirates; deliveries pay for engine, armor, and service upgrades.
  W/Up thrusts, A/D or Left/Right turns, S/Down reverses, Shift boosts, Space brakes, E attaches/releases the cable, and Q reels it in. Stop near the refinery to shop with 1/2/3. Esc pauses; R retries after a shift.
- `bonefield`: a 3D survivors game in one `main.c`, with procedural models, automatic weapons, XP pickups and a three-minute run. Kill the Keeper that arrives at 2:30 and survive to 3:00.
  WASD/arrows move, Space dodges, 1/2/3 spend upgrade points on the crossbow, orbiting blades and pulse. Esc pauses; R restarts after a run. Starts directly in play.
- `zzt`: a 3D ZZT-style game in one `main.c`, with extruded CP437 characters, EGA colors, five connected boards, object scripts, shops, ricochets, colored locks and pushable stones. Find three fuses to open the south exit.
  WASD/arrows move, Space shoots, Shift + direction shoots without moving, T lights a torch, U undoes a push, and R resets the room's boulders. Bump people and signs to read them. H shows help; Esc pauses. Starts directly in play.
  Run with `./nob run zzt`. `./build/debug/zzt --self-test` checks the complete adventure without a window; `--snapshot out.ppm [0..4]` renders a board to an image.

macOS and Linux use SDL2 (`/Library/Frameworks/SDL2.framework` or `sdl2-config`).
Windows uses the WinAPI backend and needs no extra dependencies.

## Using it

```c
#include "chao/chao.h"

void init(void) {}
void update(float dt) {
    fill(chao_canvas, COLOR_BLACK);
    fill_triangle(chao_canvas, 160, 40, 60, 200, 260, 200, COLOR_RED);
}

int main(void) {
    chao_run("My App", 320, 240, &init, &update);
}
```

Pick a backend with `-DPLATFORM_SDL`, `-DPLATFORM_WINAPI` or `-DPLATFORM_CACA`. With none of them you get a headless dummy backend.

## Assets

`load_bitmap()` / `load_model3d()` read files from disk by default. To embed assets in the binary,
generate a header and include it **before** `chao.h`:

```sh
./build/resource_packer assets resources.h
```

```c
#include "resources.h"
#include "chao/chao.h"
```

The nob build does this for the examples automatically: it packs `assets/` into `build/resources.h` and adds `build/` to the include path.

## Layout

```
chao/               the library (chao.h includes everything)
chao/mesh.h         building Model3D meshes in code: boxes, cylinders, cones, heightmaps, triangle lists
chao/platforms/     SDL2, WinAPI, libcaca and dummy backends
examples/           example programs, each built by nob.c
tools/              resource_packer: embeds assets/ into a C header
assets/fonts/       CP437 bitmap fonts for ascii_font.h
```

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
chao/platforms/     SDL2, WinAPI, libcaca and dummy backends
examples/           example programs, each built by nob.c
tools/              resource_packer: embeds assets/ into a C header
assets/fonts/       CP437 bitmap fonts for ascii_font.h
```

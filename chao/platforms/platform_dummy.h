#ifndef PLATFORM_DUMMY_H
#define PLATFORM_DUMMY_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

void(*platform_window_resized)(int width, int height) = NULL;
void platform_resize(int width, int height){}
void platform_init(const char* window_name, int width, int height) {}
void platform_set_window_size(int width, int height) {}
bool platform_update() { return true; }
void platform_set_mouse_locked(bool locked) {}
bool platform_is_mouse_locked() { return false; }
void platform_set_pixels(uint32_t* pixels, int width, int height) {}
uint32_t platform_get_time() { return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC); }
void platform_quit() {}

#endif //PLATFORM_DUMMY_H

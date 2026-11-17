// Build script for chao. Bootstrap once with:
//
//     cc -o nob nob.c        (or: cl nob.c  on Windows)
//
// then just run ./nob — it rebuilds itself whenever nob.c changes.
//
// Usage: ./nob [release] [run <example>]
//   ./nob                      build tools and all examples (debug)
//   ./nob release              optimized build
//   ./nob run terrain          build, then launch an example
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define BUILD_DIR    "build/"
#define EXAMPLES_DIR "examples/"
#define CHAO_DIR     "chao/"

static const char* examples[] = {
    "hello_triangle",
    "terrain",
    "beacon_run",
    "scrap_tug",
    "bonefield",
    "zzt",
};

#ifdef _WIN32
#define EXE ".exe"
#else
#define EXE ""
#endif

static bool release = false;

// Debug and release builds live side by side so switching modes always rebuilds.
static const char* out_dir(void) {
    return release ? BUILD_DIR "release/" : BUILD_DIR "debug/";
}

static void append_build_type_flags(Cmd* cmd) {
#ifdef _MSC_VER
    if (release) cmd_append(cmd, "/O2", "/DNDEBUG");
    else         cmd_append(cmd, "/Zi", "/DDEBUG");
#else
    if (release) cmd_append(cmd, "-O2", "-DNDEBUG");
    else         cmd_append(cmd, "-g", "-O0", "-DDEBUG");
#endif
}

// Platform backend + libraries. SDL2 on macOS/Linux, plain WinAPI on Windows (no dependencies).
static void append_platform(Cmd* cmd) {
#if defined(_WIN32)
#  ifdef _MSC_VER
    cmd_append(cmd, "/DPLATFORM_WINAPI", "/link", "user32.lib", "gdi32.lib", "shell32.lib", "winmm.lib");
#  else
    cmd_append(cmd, "-DPLATFORM_WINAPI", "-luser32", "-lgdi32", "-lshell32", "-lwinmm", "-lm");
#  endif
#else
    cmd_append(cmd, "-DPLATFORM_SDL");
#  if defined(__APPLE__)
    // Prefer the SDL2.framework install, fall back to Homebrew / pkg-config style layout.
    if (file_exists("/Library/Frameworks/SDL2.framework")) {
        cmd_append(cmd,
            "-F/Library/Frameworks", "-framework", "SDL2",
            "-Wl,-rpath,/Library/Frameworks");
        return;
    }
#  endif
    // sdl2-config output is split on spaces; fine for the usual install locations.
    Cmd cfg = {0};
    String_Builder out = {0};
    cmd_append(&cfg, "sdl2-config", "--cflags", "--libs");
    if (cmd_run(&cfg, .stdout_path = BUILD_DIR "sdl2_flags.txt") &&
        read_entire_file(BUILD_DIR "sdl2_flags.txt", &out)) {
        String_View sv = sb_to_sv(out);
        while (sv.count > 0) {
            String_View flag = sv_trim(sv_chop_by_delim(&sv, ' '));
            if (flag.count > 0) cmd_append(cmd, temp_sv_to_cstr(flag));
        }
    } else {
        nob_log(WARNING, "sdl2-config not found, assuming SDL2 is on the default paths");
        cmd_append(cmd, "-lSDL2");
    }
    cmd_append(cmd, "-lm");
    sb_free(out);
    cmd_free(cfg);
#endif
}

static bool build_resource_packer(void) {
    const char* output = BUILD_DIR "resource_packer" EXE;
    if (!needs_rebuild1(output, "tools/resource_packer.c")) return true;

    Cmd cmd = {0};
    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cc_output(&cmd, output);
    nob_cc_inputs(&cmd, "tools/resource_packer.c");
    return cmd_run(&cmd);
}

// Collects every regular, non-hidden file under assets/ so we know when to re-pack.
static bool collect_asset(Walk_Entry entry) {
    const char* name = path_name(entry.path);
    if (name[0] == '.') {
        if (entry.type == FILE_DIRECTORY && entry.level > 0) *entry.action = WALK_SKIP;
        return true;
    }
    if (entry.type == FILE_REGULAR) da_append((File_Paths*)entry.data, temp_strdup(entry.path));
    return true;
}

// Embeds assets/ into build/resources.h, which the examples include before chao.h.
static bool build_resources(void) {
    const char* output = BUILD_DIR "resources.h";
    File_Paths deps = {0};
    da_append(&deps, BUILD_DIR "resource_packer" EXE);
    if (!walk_dir("assets", collect_asset, .data = &deps)) return false;
    int rebuild = needs_rebuild(output, deps.items, deps.count);
    da_free(deps);
    if (rebuild < 0) return false;
    if (!rebuild) return true;

    Cmd cmd = {0};
    cmd_append(&cmd, BUILD_DIR "resource_packer" EXE, "assets", output);
    return cmd_run(&cmd);
}

static bool build_example(const char* name) {
    const char* source = temp_sprintf(EXAMPLES_DIR "%s/main.c", name);
    const char* output = temp_sprintf("%s%s" EXE, out_dir(), name);

    // Everything is header-only, so any header change means a rebuild.
    File_Paths deps = {0};
    da_append(&deps, source);
    File_Paths headers = {0};
    const char* header_dirs[] = { CHAO_DIR, CHAO_DIR "platforms/" };
    for (size_t i = 0; i < ARRAY_LEN(header_dirs); ++i) {
        headers.count = 0;
        if (!read_entire_dir(header_dirs[i], &headers)) return false;
        for (size_t j = 0; j < headers.count; ++j) {
            if (sv_end_with(sv_from_cstr(headers.items[j]), ".h")) {
                da_append(&deps, temp_sprintf("%s%s", header_dirs[i], headers.items[j]));
            }
        }
    }
    da_append(&deps, "nob.c");
    da_append(&deps, BUILD_DIR "resources.h");
    int rebuild = needs_rebuild(output, deps.items, deps.count);
    da_free(deps);
    da_free(headers);
    if (rebuild < 0) return false;
    if (!rebuild) {
        nob_log(INFO, "%s is up to date", output);
        return true;
    }

    Cmd cmd = {0};
    nob_cc(&cmd);
#ifndef _MSC_VER
    cmd_append(&cmd, "-std=gnu11", "-Wall", "-Wno-unused-function", "-Wno-unused-variable");
#endif
    append_build_type_flags(&cmd);
#ifdef _MSC_VER
    cmd_append(&cmd, "/I" BUILD_DIR);
#else
    cmd_append(&cmd, "-I" BUILD_DIR);
#endif
    nob_cc_output(&cmd, output);
    nob_cc_inputs(&cmd, source);
    append_platform(&cmd);
    return cmd_run(&cmd);
}

int main(int argc, char** argv) {
    GO_REBUILD_URSELF(argc, argv);

    const char* program = shift(argv, argc);
    const char* run_example = NULL;

    while (argc > 0) {
        const char* arg = shift(argv, argc);
        if (strcmp(arg, "release") == 0) {
            release = true;
        } else if (strcmp(arg, "debug") == 0) {
            release = false;
        } else if (strcmp(arg, "run") == 0) {
            if (argc == 0) {
                nob_log(ERROR, "run: expected an example name");
                return 1;
            }
            run_example = shift(argv, argc);
        } else {
            nob_log(ERROR, "unknown argument: %s", arg);
            nob_log(INFO, "usage: %s [release] [run <example>]", program);
            return 1;
        }
    }

    if (!mkdir_if_not_exists(BUILD_DIR)) return 1;
    if (!mkdir_if_not_exists(out_dir())) return 1;
    if (!build_resource_packer()) return 1;
    if (!build_resources()) return 1;
    for (size_t i = 0; i < ARRAY_LEN(examples); ++i) {
        if (!build_example(examples[i])) return 1;
    }

    if (run_example) {
        Cmd cmd = {0};
        cmd_append(&cmd, temp_sprintf("%s%s" EXE, out_dir(), run_example));
        if (!cmd_run(&cmd)) return 1;
    }
    return 0;
}

// web_main.cpp — Thin C API exposing SonyDemo to JavaScript via Emscripten.
// The core is portable C++ with no OS dependencies, so it compiles directly.

#include "sony_core.h"
#include <cstring>

static SonyDemo *g_demo = nullptr;

extern "C" {

void kaleidoscope_init() {
    if (!g_demo) {
        g_demo = new SonyDemo();
        // No arguments → play the intro, matching the DOS original.
        int argc = 0;
        char **argv = nullptr;
        g_demo->init(argc, argv);
    }
}

int kaleidoscope_step() {
    return g_demo ? g_demo->step() : 0;
}

void kaleidoscope_key(int ch) {
    if (g_demo) g_demo->key(ch);
}

const uint8_t *kaleidoscope_get_framebuffer() {
    return g_demo ? g_demo->framebuffer() : nullptr;
}

const uint8_t *kaleidoscope_get_dac() {
    return g_demo ? g_demo->dac_regs() : nullptr;
}

int kaleidoscope_phase() {
    return g_demo ? static_cast<int>(g_demo->phase()) : -1;
}

int kaleidoscope_quit() {
    return g_demo && g_demo->quit() ? 1 : 0;
}

double kaleidoscope_frame_dt() {
    return g_demo ? g_demo->frame_dt() : 1.0 / 70.0;
}

void kaleidoscope_intro_begin() {
    if (g_demo) g_demo->intro_begin();
}

void kaleidoscope_intro_frame() {
    if (g_demo) g_demo->intro_frame();
}

void kaleidoscope_intro_end() {
    if (g_demo) g_demo->intro_end();
}

} // extern "C"

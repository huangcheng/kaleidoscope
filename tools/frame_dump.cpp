// frame_dump.cpp — Headless harness that renders frames from the SonyDemo core
// and writes them as PPM images. Used to produce the README screenshots and to
// verify the emulation core outside of any GUI.
//
// Build:  c++ -std=c++17 -O2 frame_dump.cpp sony_core.cpp -o frame_dump
// Usage:  ./frame_dump <outdir>
#include "sony_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void writePPM(const char *path, const uint8_t *fb, const uint8_t *dac,
                     int scale) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", 320 * scale, 200 * scale);
    for (int y = 0; y < 200 * scale; y++) {
        for (int x = 0; x < 320 * scale; x++) {
            uint8_t idx = fb[(y / scale) * 320 + (x / scale)];
            uint8_t r = dac[idx * 3 + 0], g = dac[idx * 3 + 1], b = dac[idx * 3 + 2];
            uint8_t px[3] = { (uint8_t)((r << 2) | (r >> 4)),
                              (uint8_t)((g << 2) | (g >> 4)),
                              (uint8_t)((b << 2) | (b >> 4)) };
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
    printf("wrote %s\n", path);
}

// Advance the demo by n frames, mirroring ScreensaverWidget::tick().
static void pump(SonyDemo &d, int frames, int *pollPhase) {
    for (int i = 0; i < frames; i++) {
        switch (d.phase()) {
        case SonyDemo::Phase::Intro:
            d.intro_frame();
            if (pollPhase) *pollPhase = 0;
            break;
        case SonyDemo::Phase::Variant:
            if (!d.variant_step())
                d.switch_effect_continue();
            if (pollPhase) *pollPhase = 1;
            break;
        case SonyDemo::Phase::Main: {
            bool poll = d.step();
            if (poll) d.poll_no_key();
            if (pollPhase) *pollPhase = 2;
            break;
        }
        }
        if (d.quit()) return;
    }
}

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : "docs";
    std::string dir(outdir);

    // ---- Main kaleidoscope effect (default: no args -> intro) ----
    {
        SonyDemo d;
        // Force an effect immediately: pass /O1 so the core calls switch_effect()
        char arg0[] = "frame_dump", arg1[] = "/O1";
        char *av[] = { arg0, arg1 };
        d.init(2, av);
        if (d.phase() == SonyDemo::Phase::Intro)
            d.intro_end();
        pump(d, 400, nullptr);
        writePPM((dir + "/screenshot-main.ppm").c_str(), d.framebuffer(),
                 d.dac_regs(), 2);
    }

    // ---- Intro screen (as the original presented it) ----
    {
        SonyDemo d;
        char arg0[] = "frame_dump";
        char *av[] = { arg0 };
        d.init(1, av);          // no args -> intro_begin()
        pump(d, 200, nullptr);  // let the palette cascade in (~15s in the original)
        writePPM((dir + "/screenshot-intro.ppm").c_str(), d.framebuffer(),
                 d.dac_regs(), 2);
    }

    // ---- A second effect, to show variety ----
    {
        SonyDemo d;
        char arg0[] = "frame_dump", arg1[] = "/O3", arg2[] = "/W";
        char *av[] = { arg0, arg1, arg2 };
        d.init(3, av);
        if (d.phase() == SonyDemo::Phase::Intro)
            d.intro_end();
        pump(d, 900, nullptr);
        writePPM((dir + "/screenshot-effect3.ppm").c_str(), d.framebuffer(),
                 d.dac_regs(), 2);
    }

    return 0;
}
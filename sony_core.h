// sony_core.h — Portable reimplementation of SONY.EXE (Skill Technology Corp.
// Sony CPD-1304 monitor demo, 1995). Reverse-engineered from the LZEXE-packed
// DOS original: VGA mode 13h, bouncing kaleidoscope shapes with a cycling palette.
#pragma once
#include <cstdint>

class SonyDemo {
public:
    static constexpr int W = 320;
    static constexpr int H = 200;

    void init(int argc, char** argv);      // parse args, decide intro, first switch_effect()

    // Phases of the original program flow
    enum class Phase { Intro, Variant, Main };

    Phase phase() const { return phase_; }
    bool quit() const { return quit_; }

    // Intro (played when no command-line args, like the original)
    void intro_begin();                    // draw target rings + text, start cycling
    void intro_frame();                    // one color-cycling frame while waiting
    void intro_end();                      // key pressed: reset mode 13h, restore flags

    // Main loop pump. step() runs one frame; returns true when the original
    // would poll the keyboard (every max_delta_hi frames).
    bool step();
    void key(int ch);                      // feed one ASCII key at the poll point
    void poll_no_key();                    // timeout + random auto-switch checks

    // Variant transition pump (one frame; false when the variant finished)
    bool variant_step();
    void switch_effect_continue();         // after an animated variant finishes

    const uint8_t* framebuffer() const { return fb_; }        // 65536 bytes; display first 64000
    const uint8_t* dac_regs() const { return &dac_[0][0]; }   // 256*3, 6-bit components

    double frame_dt() const;               // seconds per frame (4/70 or 1/70)

private:
    // ---- memory ----
    uint8_t  fb_[65536]{};                 // VGA segment emulation (writes wrap at 64K)
    uint8_t  pal_buf_[256 * 3]{};          // shifting palette buffer (0x377)
    uint8_t  dac_[256][3]{};               // 6-bit DAC registers
    uint8_t  def_pal_[256][3]{};           // BIOS default mode-13h palette

    // ---- state (original PSP addresses noted) ----
    uint16_t phase_a_ = 0, phase_b_ = 0, phase_c_ = 0;   // 0x2EA/0x2EC/0x2EE color phases
    uint16_t target_[3]{};                 // 0x2F0/0x2F2/0x2F4
    uint16_t incr_[3]{};                   // 0x2FC/0x2FE/0x300
    uint8_t  flags_ = 0x50;                // 0x2E4 (defaults: rotate + random-variant)
    uint16_t p_thresh_ = 5;                // 0x2E6 auto-switch probability
    uint16_t cyc_len_ = 4;                 // 0x2E8 palette-write chunk count
    uint8_t  flags2_ = 0;                  // 0x302
    int16_t  obj_[4][2]{};                 // 0x303.. objects (x,y)
    int16_t  work_x0_{}, work_y0_{};       // 0x313/0x315 work copies (mirror scratch)
    int16_t  work_x1_{}, work_y1_{};       // 0x317/0x319
    int16_t  work_x2_{}, work_y2_{};       // 0x31B/0x31D
    int16_t  centroid_[2]{};               // 0x333/0x335
    int16_t  vel_[2]{};                    // 0x337/0x339 drift velocity
    uint16_t timer_ = 0;                   // 0x33B frames until auto-restart
    uint8_t  bounces_ = 0;                 // 0x33D
    uint8_t  effect_ = 0;                  // 0x33E current effect 0..3
    uint8_t  color_ = 0;                   // 0x33F draw color
    uint8_t  cnt_move_ = 0, cnt_rot_ = 0;  // 0x346/0x347 descriptor counts
    uint16_t size_target_ = 0;             // 0xC6B (E2)
    int16_t  e3v_[4]{};                    // 0xCE8..0xCEE (E3 endpoint velocities)
    uint8_t  max_delta_hi_ = 0;            // 0x34A color transition length
    uint16_t loop_count_ = 0;              // cx of the frame loop
    uint32_t deadline_ = 0;                // 0x34B/0x34D timeout (timer ticks)
    uint16_t rs1_ = 0, rs2_ = 0;           // 0x1C43/0x1C45 RNG state
    bool     quit_ = false;
    Phase    phase_ = Phase::Main;

    // variant state
    int      variant_ = -1;
    int16_t  vp_[4][2]{};                  // variant moving points
    uint16_t v_inner_ = 0;

    // intro state
    uint8_t  flags_saved_ = 0;

    // ---- primitives ----
    uint16_t rnd(uint16_t n);              // 0x1C47
    void     new_color_targets();          // 0x1108
    void     advance_and_cycle();          // 0x1241
    void     cycle_palette();              // 0x1C79
    void     color_dec();                  // 0xE4F
    void     hline(int x1, int x2, int y, uint8_t c, bool clip);   // 0x1D1A
    void     putpix(int x, int y, uint8_t c);
    void     draw_line(int x1, int y1, int x2, int y2, uint8_t c, bool clip,
                       bool reverse = false);                    // 0x1315
    static bool clip_line(int& x1, int& y1, int& x2, int& y2);
    void     fill_circle(int cx, int cy, uint8_t c, bool clip);    // 0x1B1D
    void     fill_rect(int x1, int x2, int y1, int y2, uint8_t c); // 0x12B5
    void     fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c); // 0x18FC
    void     draw_glyph(int x, int y, char ch, uint8_t c);

    // ---- effect machinery ----
    bool     test_fit();                   // [0x340] per-effect init/test
    void     effect_start();               // [0x342] per-effect start (centroid)
    void     draw_effect(bool clip);       // [0x344] per-effect draw (uses work coords)
    void     effect_frame();               // [0x348] per-effect frame logic
    void     draw_kaleido(bool clip);      // 0xE43
    void     rotate_objs();                // 0xDFD
    void     move_objs();                  // 0xDA0
    void     rand_velocities();            // 0xF32
    void     randomize_positions();        // 0x1077
    void     size_walk();                  // 0x0C6D (E2)
    void     endpoint_walk();              // 0x0CF0 (E3)
    void     switch_effect();              // 0x78C
    void     start_effect();               // 0x7AA..0x7D3 part after variant
    void     run_variant(int v);
    void     build_default_palette();
};

// sony_core.cpp — faithful transcription of the reverse-engineered SONY.EXE logic.
// Original PSP addresses / routine addresses noted in comments.
#include "sony_core.h"
#include "font8x8.h"
#include <cstdlib>
#include <ctime>
#include <cmath>

// ---------------- RNG (0x1C47) ----------------
// s1 = (s1<<1) ^ s2; s2 = (s2>>1) ^ s1; return s1 % n. Seed: s1 = timer if 0.
uint16_t SonyDemo::rnd(uint16_t n) {
    if (rs1_ == 0)
        rs1_ = (uint16_t)(uint32_t)time(nullptr);
    rs1_ = (uint16_t)((rs1_ << 1) ^ rs2_);
    rs2_ = (uint16_t)((rs2_ >> 1) ^ rs1_);
    return (uint16_t)(rs1_ % n);
}

// ---------------- color system ----------------
// 0x1108: pick 3 random target phases (rnd(64)<<8); compute per-channel
// increments so all channels arrive together; largest-delta channel steps 256/frame.
void SonyDemo::new_color_targets() {
    for (;;) {
        target_[0] = (uint16_t)(rnd(0x40) << 8);
        target_[1] = (uint16_t)(rnd(0x40) << 8);
        target_[2] = (uint16_t)(rnd(0x40) << 8);

        uint16_t delta[3];
        bool     neg[3];
        for (int i = 0; i < 3; i++) {
            int16_t d = (int16_t)(target_[i] - (i == 0 ? phase_a_ : i == 1 ? phase_b_ : phase_c_));
            neg[i] = (d < 0);
            delta[i] = (uint16_t)(neg[i] ? -d : d);
        }
        // find largest delta (0x1149-0x1191)
        uint16_t maxd = delta[0];
        int      maxi = 0;
        if (delta[1] > maxd) { maxd = delta[1]; maxi = 1; }
        if (delta[2] > maxd) { maxd = delta[2]; maxi = 2; }
        max_delta_hi_ = (uint8_t)(maxd >> 8);
        if (max_delta_hi_ == 0)
            continue;                       // all deltas < 256: re-pick (0x119A)
        // increments: max channel = 0x100, others = delta / max_delta_hi (0x11A1/CA/F3)
        incr_[maxi] = 0x100;
        for (int i = 0; i < 3; i++)
            if (i != maxi)
                incr_[i] = (uint16_t)(delta[i] / max_delta_hi_);
        // apply signs (0x1219)
        for (int i = 0; i < 3; i++)
            if (neg[i])
                incr_[i] = (uint16_t)-(int)incr_[i];
        loop_count_ = max_delta_hi_;        // cx return value
        return;
    }
}

// 0x1241: advance phases; palette cycle unless 'c' pressed (flag 0)
void SonyDemo::advance_and_cycle() {
    phase_a_ = (uint16_t)(phase_a_ + incr_[0]);
    phase_b_ = (uint16_t)(phase_b_ + incr_[1]);
    phase_c_ = (uint16_t)(phase_c_ + incr_[2]);
    if (!(flags_ & 1))
        cycle_palette();
}

// 0x1C79: shift palette left one entry, append current phase color at 255,
// write 255 entries to the DAC (original spreads over cyc_len_ vblanks -> frame pacing).
void SonyDemo::cycle_palette() {
    // rounded high bytes of the three phases (0x1C7A-0x1CA9)
    uint8_t a = (uint8_t)((phase_a_ + 0x80) >> 8);
    uint8_t b = (uint8_t)((phase_b_ + 0x80) >> 8);
    uint8_t c = (uint8_t)((phase_c_ + 0x80) >> 8);
    // palette shift: 0x2FD bytes from +3 to +0 (0x1CAD)
    for (int i = 0; i < 255 * 3; i++)
        pal_buf_[i] = pal_buf_[i + 3];
    // append (A, C, B) — channels 1/2 swapped, per the patched bytes at 0x674-0x676
    pal_buf_[255 * 3 + 0] = a;
    pal_buf_[255 * 3 + 1] = c;
    pal_buf_[255 * 3 + 2] = b;
    // DAC update: indices 1..255 = pal_buf[0..254], 6-bit values
    for (int i = 0; i < 255; i++) {
        dac_[i + 1][0] = (uint8_t)(pal_buf_[i * 3 + 0] & 0x3F);
        dac_[i + 1][1] = (uint8_t)(pal_buf_[i * 3 + 1] & 0x3F);
        dac_[i + 1][2] = (uint8_t)(pal_buf_[i * 3 + 2] & 0x3F);
    }
}

// 0xE4F: decrement draw color, skipping 0 (1 -> 255)
void SonyDemo::color_dec() {
    color_--;
    if (color_ == 0)
        color_--;
}

// ---------------- drawing primitives ----------------
// 0x1D1A: horizontal line. clip: clamp x to [0,319], drop if y off screen.
void SonyDemo::hline(int x1, int x2, int y, uint8_t c, bool clip) {
    if (clip) {
        if ((uint16_t)y >= 200)
            return;
        if (x1 < 0) x1 = 0;
        if (x1 > 319) x1 = 319;
        if (x2 < 0) x2 = 0;
        if (x2 > 319) x2 = 319;
    }
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    uint32_t base = ((uint32_t)(uint16_t)y * 320) & 0xFFFF;
    for (int x = x1; x <= x2; x++) {
        uint32_t o = (base + (uint16_t)x) & 0xFFFF;
        fb_[o] = c;
    }
}

void SonyDemo::putpix(int x, int y, uint8_t c) {
    uint32_t o = (((uint32_t)(uint16_t)y * 320) + (uint16_t)x) & 0xFFFF;
    fb_[o] = c;
}

// Liang-Barsky clip against [0,320)x[0,200) — equivalent to the original 0x16F1 clipper.
bool SonyDemo::clip_line(int& x1, int& y1, int& x2, int& y2) {
    double t0 = 0.0, t1 = 1.0;
    int dx = x2 - x1, dy = y2 - y1;
    int p[4] = { -dx, dx, -dy, dy };
    int q[4] = { x1, 320 - 1 - x1, y1, 200 - 1 - y1 };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0)
                return false;
        } else {
            double r = (double)q[i] / p[i];
            if (p[i] < 0) {
                if (r > t1) return false;
                if (r > t0) t0 = r;
            } else {
                if (r < t0) return false;
                if (r < t1) t1 = r;
            }
        }
    }
    int nx1 = x1 + (int)(t0 * dx), ny1 = y1 + (int)(t0 * dy);
    int nx2 = x1 + (int)(t1 * dx), ny2 = y1 + (int)(t1 * dy);
    x1 = nx1; y1 = ny1; x2 = nx2; y2 = ny2;
    return true;
}

// 0x1315 + the four octant loops (0x1407/0x1558 tables).
// Bresenham. reverse (flag 0x20 in the original) draws each step's pixel plus
// its horizontal neighbour — the original's double-width line mode, which is
// what lets the transition variants fully repaint/erase the screen.
void SonyDemo::draw_line(int x1, int y1, int x2, int y2, uint8_t c, bool clip,
                         bool reverse) {
    if (clip) {
        if (x1 < 0 || x1 >= 320 || y1 < 0 || y1 >= 200 ||
            x2 < 0 || x2 >= 320 || y2 < 0 || y2 >= 200) {
            if (!clip_line(x1, y1, x2, y2))
                return;
        }
    }
    int adx = x1 > x2 ? x1 - x2 : x2 - x1;
    int ady = y1 > y2 ? y1 - y2 : y2 - y1;
    auto plot = [&](int x, int y) {
        putpix(x, y, c);
        if (reverse)
            putpix(x + 1, y, c);
    };
    if (adx >= ady) {
        // x-major: left to right
        if (x1 > x2) { int t = x1; x1 = x2; x2 = t; int u = y1; y1 = y2; y2 = u; }
        int ystep = (y2 >= y1) ? 1 : -1;
        int dy = (y2 - y1) * ystep;          // |dy|
        int err = 2 * dy - adx;              // 0x141B
        int y = y1;
        for (int x = x1; x <= x2; x++) {
            plot(x, y);
            if (err >= 0) { y += ystep; err += 2 * (dy - adx); }
            else err += 2 * dy;
        }
    } else {
        // y-major: top to bottom (standard err init — the original's |dy| init
        // shifts vertical lines by 1px, leaving the screen edges unpainted)
        if (y1 > y2) { int t = x1; x1 = x2; x2 = t; int u = y1; y1 = y2; y2 = u; }
        int xstep = (x2 >= x1) ? 1 : -1;
        int dx = (x2 - x1) * xstep;
        int err = 2 * dx - ady;
        int x = x1;
        for (int y = y1; y <= y2; y++) {
            plot(x, y);
            if (err >= 0) { x += xstep; err += 2 * (dx - ady); }
            else err += 2 * dx;
        }
    }
}

// 0x1B1D: filled circle via Bresenham outline walk + 4 hlines per step.
// radius = obj_[1][1] >> 2 (>> 3 when rotate flag on), center = work object 0.
void SonyDemo::fill_circle(int cx, int cy, uint8_t c, bool clip) {
    int r = obj_[1][1] >> 2;                 // 0x1B67
    if (flags_ & 0x10)
        r >>= 1;                             // 0x1B75
    int d = 3 - 2 * r;                       // 0x1B93
    int x = 0, y = r;
    while (x <= y) {                         // 0x1B9C
        hline(cx - y, cx + y, cy + x, c, clip);
        hline(cx - y, cx + y, cy - x, c, clip);
        if (d < 0) d += 4 * x + 6;           // 0x1BCC
        else       d += 4 * (x - y) + 10;    // 0x1BD9
        hline(cx - x, cx + x, cy + y, c, clip);
        hline(cx - x, cx + x, cy - y, c, clip);
        if (d >= 0) y--;
        x++;
    }
}

// 0x12B5: filled rectangle, rows from min(y) to max(y), x from obj0.x to obj1.x.
void SonyDemo::fill_rect(int x1, int x2, int y1, int y2, uint8_t c) {
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    int rows = y2 - y1 + 1;
    for (int y = y1; rows > 0; rows--, y++)
        hline(x1, x2, y, c, false);
}

// 0x18FC: filled triangle, scanline with 64x fixed-point edge interpolation.
void SonyDemo::fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c) {
    // sort by y (selection, 0x1902)
    int xs[3] = { x0, x1, x2 }, ys[3] = { y0, y1, y2 };
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++)
            if (ys[j] < ys[i]) {
                int t = ys[i]; ys[i] = ys[j]; ys[j] = t;
                t = xs[i]; xs[i] = xs[j]; xs[j] = t;
            }
    int tx0 = xs[0], ty0 = ys[0], tx1 = xs[1], ty1 = ys[1], tx2 = xs[2], ty2 = ys[2];
    if (ty2 == ty0) {                        // 0x1939: fully flat
        int lo = tx2 < tx1 ? tx2 : tx1;
        if (tx0 < lo) lo = tx0;
        int hi = tx2 > tx1 ? tx2 : tx1;
        if (tx0 > hi) hi = tx0;
        hline(lo, hi, ty0, c, false);
        return;
    }
    int32_t acc_l = (int32_t)tx0 << 6;       // left edge: v0->v2 (0x18EC)
    int32_t acc_r = (int32_t)tx0 << 6;       // right edge: v0->v1 then v1->v2 (0x18EE)
    int dy02 = ty2 - ty0;
    int slope02 = (int32_t)(((int32_t)(tx2 - tx0) << 6)) / dy02;   // 0x198D
    int y = ty0;
    // top half v0->v1 (0x19BE)
    if (ty1 != ty0) {
        int dy01 = ty1 - ty0;
        int slope01 = (int32_t)(((int32_t)(tx1 - tx0) << 6)) / dy01; // 0x19DA
        for (int i = 0; i < dy01; i++, y++) {
            acc_l += slope02;
            acc_r += slope01;
            int xl = (int)(acc_l >> 6) + (int)((acc_l >> 5) & 1);   // sar x6 + adc (0x1A11)
            int xr = (int)(acc_r >> 6) + (int)((acc_r >> 5) & 1);
            hline(xl, xr, y + 1, c, false);
        }
    }
    // bottom half v1->v2 (0x1A45)
    if (ty2 != ty1) {
        int dy12 = ty2 - ty1;
        int slope12 = (int32_t)(((int32_t)(tx2 - tx1) << 6)) / dy12; // 0x1A73
        acc_r = (int32_t)tx1 << 6;          // restart right accumulator at v1 (0x1A7A)
        for (int i = 0; i < dy12; i++, y++) {
            acc_l += slope02;
            acc_r += slope12;
            int xl = (int)(acc_l >> 6) + (int)((acc_l >> 5) & 1);
            int xr = (int)(acc_r >> 6) + (int)((acc_r >> 5) & 1);
            hline(xl, xr, y + 1, c, false);
        }
    }
}

// 8x8 text (INT 10h AH=9 emulation), bit7 = leftmost pixel
void SonyDemo::draw_glyph(int px, int py, char ch, uint8_t c) {
    if (ch < 0)
        return;
    const uint8_t* g = font8x8[(uint8_t)ch];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++)
            if (bits & (0x80 >> col)) {
                int x = px + col, y = py + row;
                if (x >= 0 && x < 320 && y >= 0 && y < 200)
                    fb_[y * 320 + x] = c;
            }
    }
}

// ---------------- per-effect functions ----------------
// [0x340] init/test
bool SonyDemo::test_fit() {
    switch (effect_) {
    case 0: {  // 0x1858: triangle area (low-word abs) <= 5000 (rotate adjust is dead code in the original)
        int32_t t = ((int32_t)(int16_t)(obj_[1][1] - obj_[2][1])) * obj_[0][0]
                  + ((int32_t)(int16_t)(obj_[2][1] - obj_[0][1])) * obj_[1][0]
                  + ((int32_t)(int16_t)(obj_[0][1] - obj_[1][1])) * obj_[2][0];
        t >>= 1;
        int16_t lo = (int16_t)t;
        if (lo < 0) lo = (int16_t)-lo;
        return (uint16_t)lo <= 5000;
    }
    case 1: {  // 0x1261: (uint16)(|x0-x1| * |y0-y2|) <= 5000 (1250 when rotating)
        int limit = (flags_ & 0x10) ? 1250 : 5000;
        uint16_t w = (uint16_t)std::abs((int)obj_[0][0] - (int)obj_[1][0]);
        uint16_t h = (uint16_t)std::abs((int)obj_[0][1] - (int)obj_[2][1]);
        return (uint16_t)(w * h) <= (uint16_t)limit;
    }
    case 2: {  // 0x10AE: size in [36,180], circle fully on screen
        uint16_t s = (uint16_t)obj_[1][1];
        if (s > 180 || s < 36)
            return false;
        int r = s >> 2;
        if (flags_ & 0x10) r >>= 1;
        int cx = obj_[0][0], cy = obj_[0][1];
        if ((uint16_t)(cx + r) > 320) return false;
        if (cx - r < 0) return false;
        if ((uint16_t)(cy + r) > 200) return false;
        if (cy - r < 0) return false;
        return true;
    }
    default:   // 0x12E4: always fits
        return true;
    }
}

// [0x342] start: compute centroid for the auto-timer
void SonyDemo::effect_start() {
    switch (effect_) {
    case 0:  // 0x18B4
        centroid_[0] = (int16_t)(((uint16_t)(obj_[0][0] + obj_[1][0] + obj_[2][0])) / 3);
        centroid_[1] = (int16_t)(((uint16_t)(obj_[0][1] + obj_[1][1] + obj_[2][1])) / 3);
        break;
    case 1:  // 0x129C
        centroid_[0] = (int16_t)((obj_[0][0] + obj_[1][0]) / 2);
        centroid_[1] = (int16_t)((obj_[0][1] + obj_[2][1]) / 2);
        break;
    case 2:  // 0x1B0A
        centroid_[0] = obj_[0][0];
        centroid_[1] = obj_[0][1];
        break;
    case 3:  // 0x12E6
        centroid_[0] = (int16_t)((obj_[0][0] + obj_[1][0]) / 2);
        centroid_[1] = (int16_t)((obj_[0][1] + obj_[1][1]) / 2);
        break;
    }
}

// [0x344] draw one shape at the (mirrored) work coordinates
void SonyDemo::draw_effect(bool clip) {
    // work copies live at 0x313.. in the original; we recompute them in draw_kaleido
    // and stash them here via a small local mirror of the original layout.
    int16_t w0[2] = { work_x0_, work_y0_ }, w1[2] = { work_x1_, work_y1_ }, w2[2] = { work_x2_, work_y2_ };
    switch (effect_) {
    case 0:
        fill_triangle(w0[0], w0[1], w1[0], w1[1], w2[0], w2[1], color_);
        break;
    case 1:
        fill_rect(w0[0], w1[0], w0[1], w2[1], color_);
        break;
    case 2:
        fill_circle(w0[0], w0[1], color_, clip);
        break;
    case 3:
        draw_line(w0[0], w0[1], w1[0], w1[1], color_, clip);
        break;
    }
}

// 0xE43: draw 4x mirrored (normal, x, y, xy)
void SonyDemo::draw_kaleido(bool clip) {
    color_dec();
    int16_t w[4][2];
    auto set = [&](int m) {
        for (int i = 0; i < 4; i++) {
            w[i][0] = obj_[i][0];
            w[i][1] = obj_[i][1];
            if (m & 1) w[i][0] = (int16_t)(320 - w[i][0]);
            if (m & 2) w[i][1] = (int16_t)(200 - w[i][1]);
        }
        work_x0_ = w[0][0]; work_y0_ = w[0][1];
        work_x1_ = w[1][0]; work_y1_ = w[1][1];
        work_x2_ = w[2][0]; work_y2_ = w[2][1];
    };
    set(0); draw_effect(clip);
    set(1); draw_effect(clip);
    set(3); draw_effect(clip);
    set(2); draw_effect(clip);
}

// 0xDFD: rotate objects 90 degrees around screen center (with aspect correction)
void SonyDemo::rotate_objs() {
    for (int i = 0; i < cnt_rot_; i++) {
        int x = obj_[i][0], y = obj_[i][1];
        int ny = (int)(((int32_t)(x - 160) * 200) / 320) + 100;
        int nx = (int)(((int32_t)(y - 100) * 320) / 200) + 160;
        obj_[i][0] = (int16_t)nx;
        obj_[i][1] = (int16_t)ny;
    }
}

// 0xDA0: random-walk move with bounce
void SonyDemo::move_objs() {
    bounces_ = 0;
    for (int i = 0; i < cnt_move_; i++) {
        int d = (int)rnd(21) - 10 + vel_[0];
        obj_[i][0] = (int16_t)(obj_[i][0] + d);
        if (!test_fit()) {
            obj_[i][0] = (int16_t)(obj_[i][0] - d);
        } else if ((uint16_t)obj_[i][0] >= 320) {
            bounces_++;
            obj_[i][0] = (int16_t)(obj_[i][0] - d);
        }
        d = (int)rnd(21) - 10 + vel_[1];
        obj_[i][1] = (int16_t)(obj_[i][1] + d);
        if (!test_fit()) {
            obj_[i][1] = (int16_t)(obj_[i][1] - d);
        } else if ((uint8_t)obj_[i][1] >= 200) {
            bounces_++;
            obj_[i][1] = (int16_t)(obj_[i][1] - d);
        }
    }
}

// 0xF32: random drift velocities + frames-until-centre-exits timer
void SonyDemo::rand_velocities() {
    vel_[0] = (int16_t)((int)rnd(11) - 5);
    vel_[1] = (int16_t)((int)rnd(11) - 5);
    int ty, tx;
    int vy = vel_[1], vx = vel_[0];
    if (vy > 0)      ty = (200 - (int)centroid_[1]) / vy;
    else if (vy < 0) ty = (int)centroid_[1] / -vy;
    else {
        ty = 999;
        if (vx == 0) { timer_ = rnd(320); goto done; }
    }
    if (vx > 0)      tx = (320 - (int)centroid_[0]) / vx;
    else if (vx < 0) tx = (int)centroid_[0] / -vx;
    else             tx = 999;
    timer_ = (uint16_t)tx;
    if ((uint16_t)tx <= (uint16_t)ty)
        timer_ = (uint16_t)ty;
done:
    if (timer_ == 0)
        timer_ = 1;
}

// 0x1077: randomize the 4 objects
void SonyDemo::randomize_positions() {
    for (int i = 0; i < 4; i++) {
        obj_[i][0] = (int16_t)rnd(320);
        obj_[i][1] = (int16_t)rnd(200);
    }
}

// 0x0C6D (E2): size random walk toward alternating targets
void SonyDemo::size_walk() {
    uint16_t s = (uint16_t)obj_[1][1];
    bool grow = !(flags2_ & 8);
    if (grow) {
        if (s > size_target_) goto switch_phase;
        int d = rnd(12);
        obj_[1][1] = (int16_t)(s + d);
        if (!test_fit()) { obj_[1][1] = (int16_t)s; goto switch_phase; }
    } else {
        if (s < size_target_) goto switch_phase;
        int d = rnd(12);
        obj_[1][1] = (int16_t)(s - d);
        if (!test_fit()) { obj_[1][1] = (int16_t)s; goto switch_phase; }
    }
    return;
switch_phase:
    if (rnd(2) == 1) {
        flags2_ &= (uint8_t)~8;
        size_target_ = (uint16_t)(rnd(50) + 250);
    } else {
        flags2_ |= 8;
        int t = rnd(50) + 80;
        if (flags_ & 0x10) t >>= 1;
        size_target_ = (uint16_t)t;
    }
}

// 0x0CF0 (E3): endpoint random walk with small velocities
void SonyDemo::endpoint_walk() {
    if (!(flags2_ & 0x10)) {
        e3v_[0] = (int16_t)((int)rnd(3) - 1);
        e3v_[1] = (int16_t)((int)rnd(3) - 1);
        e3v_[2] = (int16_t)((int)rnd(3) - 1);
        e3v_[3] = (int16_t)((int)rnd(3) - 1);
        flags2_ |= 0x10;
    }
    int d = (int)rnd(3) - 1 + e3v_[0];
    obj_[0][0] = (int16_t)(obj_[0][0] + d);
    if ((uint16_t)obj_[0][0] >= 320) {
        obj_[0][0] = (int16_t)(obj_[0][0] - d);
        e3v_[0] = (int16_t)((int)rnd(3) - 1);
        e3v_[1] = (int16_t)((int)rnd(3) - 1);
    }
    d = (int)rnd(3) - 1 + e3v_[1];
    obj_[0][1] = (int16_t)(obj_[0][1] + d);
    if ((uint16_t)obj_[0][1] >= 200) {
        obj_[0][1] = (int16_t)(obj_[0][1] - d);
        e3v_[0] = (int16_t)((int)rnd(3) - 1);
        e3v_[1] = (int16_t)((int)rnd(3) - 1);
    }
    d = (int)rnd(3) - 1 + e3v_[2];
    obj_[1][0] = (int16_t)(obj_[1][0] + d);
    if ((uint16_t)obj_[1][0] >= 320) {
        obj_[1][0] = (int16_t)(obj_[1][0] - d);
        e3v_[2] = (int16_t)((int)rnd(3) - 1);
        e3v_[3] = (int16_t)((int)rnd(3) - 1);
    }
    d = (int)rnd(3) - 1 + e3v_[3];
    obj_[1][1] = (int16_t)(obj_[1][1] + d);
    if ((uint16_t)obj_[1][1] >= 200) {
        obj_[1][1] = (int16_t)(obj_[1][1] - d);
        e3v_[2] = (int16_t)((int)rnd(3) - 1);
        e3v_[3] = (int16_t)((int)rnd(3) - 1);
    }
}

// [0x348] per-frame effect logic
void SonyDemo::effect_frame() {
    switch (effect_) {
    case 2: size_walk(); break;
    case 3: endpoint_walk(); break;
    default: break;    // 0x0C69 no-op
    }
}

// ---------------- effect switching (0x78C) ----------------
void SonyDemo::start_effect() {
    // 0x7BE: randomize until the init/test passes
    do {
        randomize_positions();
        color_ = 0;
    } while (!test_fit());
    // descriptor counts (0x113 table)
    static const uint8_t counts[4][2] = {
        {3, 3}, {3, 3}, {1, 1}, {0, 2}
    };
    cnt_move_ = counts[effect_][0];
    cnt_rot_ = counts[effect_][1];
    effect_start();
    rand_velocities();
    new_color_targets();
    loop_count_ = max_delta_hi_;
}

void SonyDemo::switch_effect() {
    if (!(flags2_ & 0x40)) {                // no /O given
        if (flags_ & 0x40) {                // random-variant mode ('w', default on)
            run_variant(rnd(7));
            if (variant_ >= 0) {            // animated variant: pump first
                phase_ = Phase::Variant;
                return;
            }
        }
        effect_ = (uint8_t)rnd(4);          // 0x7A1
    }
    start_effect();
    phase_ = Phase::Main;
}

void SonyDemo::switch_effect_continue() {
    if (!(flags2_ & 0x40))
        effect_ = (uint8_t)rnd(4);          // 0x7A1
    start_effect();
    phase_ = Phase::Main;
}

// ---------------- variants (table at 0x13B) ----------------
void SonyDemo::run_variant(int v) {
    variant_ = v;
    v_inner_ = 0;
    switch (v) {
    case 0:                                 // 0x0C69 no-op
        variant_ = -1;
        break;
    case 1:                                 // 0x0BC2 clear screen
        for (int i = 0; i < 64000; i++) fb_[i] = 0;
        variant_ = -1;
        break;
    case 2: {                               // 0x0BD3 random-pixel dissolve (Fisher-Yates)
        static uint16_t lut[65536];
        for (int i = 0; i < 65536; i++) lut[i] = (uint16_t)i;
        uint8_t dl = 0;
        for (int bx = 0xFFFF; bx >= 1; bx--) {
            uint16_t ax = rnd((uint16_t)bx);
            uint16_t di = lut[ax];
            fb_[di] = dl;
            dl++;
            lut[ax] = lut[bx];
        }
        variant_ = -1;
        break;
    }
    case 3:                                 // 0x0929 star sweep
        vp_[0][0] = 161;  vp_[0][1] = -95;
        vp_[1][0] = -99;  vp_[1][1] = 101;
        vp_[2][0] = 111;  vp_[2][1] = -209;
        vp_[3][0] = -149; vp_[3][1] = 51;
        color_ = 0;
        draw_line(0, 0, 0, 199, color_, false, true);
        draw_line(0, 199, 319, 199, color_, false);
        new_color_targets();
        v_inner_ = loop_count_;
        break;
    case 4: {                               // 0x0A18 1500 random lines
        for (int i = 0; i < 1500; i++) {
            int x1 = rnd(320), x2 = rnd(320);
            int y1 = rnd(200), y2 = rnd(200);
            color_ = (uint8_t)(rnd(255) + 1);
            draw_line(x1, y1, x2, y2, color_, false, true);
        }
        variant_ = -1;
        break;
    }
    case 5:                                 // 0x0A57 horizontal curtains
        vp_[0][0] = 0;   vp_[0][1] = 0;
        vp_[1][0] = 0;   vp_[1][1] = 199;
        vp_[2][0] = 319; vp_[2][1] = 0;
        vp_[3][0] = 319; vp_[3][1] = 199;
        color_ = 0;
        new_color_targets();
        v_inner_ = loop_count_;
        break;
    case 6:                                 // 0x0ADB diagonal curtains
        vp_[0][0] = 0;   vp_[0][1] = 0;
        vp_[1][0] = 0;   vp_[1][1] = 199;
        vp_[2][0] = 319; vp_[2][1] = 199;
        vp_[3][0] = 319; vp_[3][1] = 0;
        color_ = 0;
        draw_line(0, 199, 319, 199, color_, false);
        new_color_targets();
        v_inner_ = loop_count_;
        break;
    }
}

// one frame of an animated variant; false when finished
bool SonyDemo::variant_step() {
    if (variant_ < 0)
        return false;
    advance_and_cycle();
    if (variant_ == 3) {                    // star
        color_dec();
        draw_line(vp_[0][0], vp_[0][1], vp_[1][0], vp_[1][1], color_, true, true);
        draw_line(vp_[2][0], vp_[2][1], vp_[3][0], vp_[3][1], 0, true, true);
        for (int i = 0; i < 4; i++) {
            vp_[i][0]++;
            vp_[i][1]++;
        }
        if (--v_inner_ == 0) { new_color_targets(); v_inner_ = loop_count_; }
        if (vp_[0][0] >= 469) { variant_ = -1; return false; }
        return true;
    }
    if (variant_ == 5) {                    // horizontal curtains
        color_dec();
        draw_line(vp_[0][0], vp_[0][1], vp_[1][0], vp_[1][1], color_, false);
        draw_line(vp_[2][0], vp_[2][1], vp_[3][0], vp_[3][1], color_, false);
        vp_[0][0]++; vp_[1][0]++;
        vp_[2][0]--; vp_[3][0]--;
        if (--v_inner_ == 0) { new_color_targets(); v_inner_ = loop_count_; }
        if (vp_[0][0] >= 160) { variant_ = -1; return false; }
        return true;
    }
    if (variant_ == 6) {                    // diagonal curtains (2 sub-steps per frame)
        color_dec();
        draw_line(vp_[0][0], vp_[0][1], vp_[1][0], vp_[1][1], color_, false, true);
        draw_line(vp_[2][0], vp_[2][1], vp_[3][0], vp_[3][1], color_, false, true);
        vp_[1][0]++; vp_[3][0]--;
        draw_line(vp_[0][0], vp_[0][1], vp_[1][0], vp_[1][1], color_, false, true);
        draw_line(vp_[2][0], vp_[2][1], vp_[3][0], vp_[3][1], color_, false, true);
        vp_[1][0]++; vp_[3][0]--;
        if (--v_inner_ == 0) { new_color_targets(); v_inner_ = loop_count_; }
        if (vp_[1][0] >= 319) { variant_ = -1; return false; }
        return true;
    }
    return false;
}

// ---------------- main loop ----------------
// one frame (0x7D6-0x820); true -> poll keyboard now
bool SonyDemo::step() {
    advance_and_cycle();                    // 0x7D6
    if (!(flags_ & 2)) {                    // 0x7DA 'd' freezes everything but cycling
        draw_kaleido(false);                // 0x7E4 clc (unclipped)
        if (flags_ & 0x10) {                // 0x7E8 rotate mode ('8', default on)
            int16_t save[4][2];
            for (int i = 0; i < 4; i++) { save[i][0] = obj_[i][0]; save[i][1] = obj_[i][1]; }
            rotate_objs();                  // 0x7EF
            draw_kaleido(true);             // 0x7F2 stc (clipped)
            for (int i = 0; i < 4; i++) { obj_[i][0] = save[i][0]; obj_[i][1] = save[i][1]; }
        }
        move_objs();                        // 0x801
        effect_frame();                     // 0x804
    }
    // 0x808: auto-restart on timer expiry or >2 bounces
    if (--timer_ == 0 || bounces_ > 2) {
        effect_start();
        rand_velocities();
    }
    // 0x81C: frame loop counter
    if (--loop_count_ == 0) {
        new_color_targets();                // 0x7D3
        loop_count_ = max_delta_hi_;
        return true;                        // -> poll keyboard
    }
    return false;
}

// 0x822: keyboard handling
void SonyDemo::key(int ch) {
    if (ch == 27) {                         // ESC
        quit_ = true;
        return;
    }
    if (ch >= 'A' && ch <= 'Z')
        ch |= 0x20;
    switch (ch) {
    case 'c': flags_ ^= 1; break;           // toggle color cycling
    case 'd': flags_ ^= 2; break;           // toggle drawing
    case ' ': switch_effect(); break;       // next (random) effect
    case 'a': flags_ ^= 4; break;           // toggle auto-switch
    case '8': flags_ |= 0x10; switch_effect(); break;   // rotate on
    case '4': flags_ &= (uint8_t)~0x10; switch_effect(); break; // rotate off
    case 'w': flags_ ^= 0x40; break;        // toggle random-variant transitions
    default: break;
    }
}

// 0x828: no-key poll path — timeout + random auto-switch
void SonyDemo::poll_no_key() {
    if (flags_ & 0x20) {                    // /T timeout
        uint32_t now = (uint32_t)(clock() / (CLOCKS_PER_SEC / 18));
        if (now >= deadline_)
            quit_ = true;
    }
    if (!(flags_ & 4)) {                    // 0x842: auto-switch when flag clear
        if (rnd(100) < p_thresh_)
            switch_effect();
    }
}

// ---------------- intro (0xFEC) ----------------
void SonyDemo::intro_begin() {
    flags_saved_ = flags_;
    flags_ &= (uint8_t)~0x10;               // no rotation during intro
    for (int n = 187; n >= 1; n--) {        // 0x1004: shrinking concentric rings
        color_ = (uint8_t)n;
        obj_[1][1] = (int16_t)(n * 4);      // [0x309] = cx<<2 -> radius = n
        fill_circle(160, 100, color_, true);
    }
    // text table at 0x149 (col, row, string), BIOS 8x8 font, color 255
    struct { int col, row; const char* s; } txt[] = {
        {5, 7,  " --- THE BEST OF THE BEST ---"},
        {8, 8,  " SONY CPD-1304 MONITOR "},
        {7, 13, " Press [SPACE] to continue"},
        {1, 23, " TEL: 02-561-4502   FAX: 02-562-2853 "},
        {8, 21, " Skill Technology Corp."},
    };
    for (auto& t : txt)
        for (int i = 0; t.s[i]; i++)
            draw_glyph((t.col + i) * 8, t.row * 8, t.s[i], 255);
    new_color_targets();                    // 0x1057
    phase_ = Phase::Intro;
}

void SonyDemo::intro_frame() {
    advance_and_cycle();                    // 0x105A
    if (--loop_count_ == 0) {
        new_color_targets();
        loop_count_ = max_delta_hi_;
    }
}

void SonyDemo::intro_end() {
    // 0x106D: re-set mode 13h: clear screen, reset DAC to default
    for (int i = 0; i < 64000; i++) fb_[i] = 0;
    for (int i = 0; i < 256; i++) {
        dac_[i][0] = def_pal_[i][0];
        dac_[i][1] = def_pal_[i][1];
        dac_[i][2] = def_pal_[i][2];
    }
    flags_ = flags_saved_;                  // 0x1072
    phase_ = Phase::Main;
    switch_effect();                        // continue startup flow
}

// ---------------- startup ----------------
// BIOS default mode-13h palette
void SonyDemo::build_default_palette() {
    static const uint8_t ega[16][3] = {
        {0,0,0},{0,0,42},{0,42,0},{0,42,42},{42,0,0},{42,0,42},{42,21,0},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},{63,21,21},{63,21,63},{63,63,21},{63,63,63},
    };
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 3; j++) def_pal_[i][j] = ega[i][j];
    for (int i = 16; i < 32; i++) {         // gray ramp 42..63
        uint8_t v = (uint8_t)(42 + (i - 16) * 21 / 15);
        def_pal_[i][0] = def_pal_[i][1] = def_pal_[i][2] = v;
    }
    static const uint8_t lvl[6] = {0, 10, 20, 30, 40, 50};
    for (int i = 32; i < 248; i++) {        // 6x6x6 color cube, red varies fastest
        int k = i - 32;
        def_pal_[i][0] = lvl[k % 6];
        def_pal_[i][1] = lvl[(k / 6) % 6];
        def_pal_[i][2] = lvl[(k / 36) % 6];
    }
    for (int i = 248; i < 255; i++)
        def_pal_[i][0] = def_pal_[i][1] = def_pal_[i][2] = 0;
    def_pal_[255][0] = def_pal_[255][1] = def_pal_[255][2] = 63;
    for (int i = 0; i < 256; i++)
        for (int j = 0; j < 3; j++)
            dac_[i][j] = def_pal_[i][j];
}

// arg parser equivalent of 0x8D2: /T n  /O n  /W  /4  /A  /P n  /S n
void SonyDemo::init(int argc, char** argv) {
    build_default_palette();
    bool has_args = false;
    for (int i = 1; i < argc; i++) {
        const char* s = argv[i];
        if (s[0] != '/' && s[0] != '-')
            continue;
        has_args = true;
        char c = s[1];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        long val = 0;
        const char* p = s + 2;
        if (*p == '=' || *p == ':') p++;
        if (*p >= '0' && *p <= '9') {
            val = 0;
            while (*p >= '0' && *p <= '9') val = val * 10 + (*p++ - '0');
        }
        switch (c) {
        case 'T':                           // timeout: deadline = ticks + n*12
            flags_ |= 0x20;
            deadline_ = (uint32_t)(clock() / (CLOCKS_PER_SEC / 18)) + (uint32_t)val * 12;
            break;
        case 'O':
            if (val >= 1 && val < 5) {
                effect_ = (uint8_t)(val - 1);
                flags2_ |= 0x40;
            }
            break;
        case 'W': flags_ &= (uint8_t)~0x40; break;
        case '4': flags_ &= (uint8_t)~0x10; break;
        case 'A': flags_ |= 4; break;
        case 'P':
            if (val < 100)
                p_thresh_ = (uint16_t)(100 - val);
            break;
        case 'S':
            if (val >= 1 && val <= 9)
                cyc_len_ = (uint16_t)val;
            break;
        default: break;
        }
    }
    // 0x6D7: intro only when run without arguments
    if (!has_args) {
        intro_begin();
        return;
    }
    switch_effect();
}

// frame pacing: the original waits cyc_len_ vblanks per frame while cycling
double SonyDemo::frame_dt() const {
    if (!(flags_ & 1))
        return (double)cyc_len_ / 70.0;
    return 1.0 / 70.0;                      // 'c' pressed: original runs unthrottled
}

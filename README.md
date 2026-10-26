# Kaleidoscope

A cross-platform screen saver built with **Qt 6 Widgets (C++)**, running on **Windows, macOS and Linux**.

It is a faithful port of the 1995 DOS demo shipped with the **SONY CPD-1304** monitor by *Skill Technology Corp.* — a VGA mode 13h kaleidoscope of bouncing, rotating shapes over a continuously cycling 256-colour palette.

> The original program was recovered by reverse-engineering the LZEXE-packed `SONY.EXE`. The emulated VGA core (framebuffer, DAC registers, palette engine, RNG and all drawing primitives) is transcribed instruction-for-instruction, so the screensaver reproduces the original's motion, colours and effect transitions rather than merely imitating them.

---

## Screenshots

| Intro (original behaviour) | Main kaleidoscope effect | Alternate effect |
|---|---|---|
| ![Intro screen](docs/screenshot-intro.png) | ![Main kaleidoscope effect](docs/screenshot-main.png) | ![Alternate effect](docs/screenshot-effect3.png) |

The intro plays when the program is started without effect flags. Screen-saver mode skips it and starts directly on the main kaleidoscope effect, then randomly transitions between effects exactly like the DOS original. All three frames are captures of the running application, taken through the `--snapshot` hook in [tools/e2e.sh](tools/e2e.sh).

---

## Features

- **Authentic emulation** — 320×200 indexed framebuffer, 6-bit VGA DAC, hardware-style palette shifting and the original 16-bit LFSR random generator
- **Nearest-neighbour scaling** — the pixel-art look is preserved while filling any screen resolution
- **True-to-original pacing** — the demo advances on a wall-clock accumulator calibrated to the DOS vertical-blank rate (70 Hz), so speed does not drift with the host timer
- **Animated transitions** — the original's star, curtain, diagonal-wipe and noise variants run between effects
- **Interactive** — the original's keyboard controls are preserved (see below)
- **Portable** — one codebase, three platforms, no platform-specific rendering code

---

## Download & Install

### Build from source

Requirements:

- CMake ≥ 3.19
- A C++17 compiler
- Qt 6.5 or newer (`Core` and `Widgets`)

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/qt6
cmake --build build -j
```

#### Windows

The build produces `Kaleidoscope.scr`. Right-click it and choose **Install** (or copy it to `C:\Windows\System32`), then select *Kaleidoscope* in **Settings → Personalisation → Lock screen → Screen saver**.

#### macOS

The build produces `Kaleidoscope.app`. Install it with:

```bash
cmake --install build
```

which copies the bundle to `~/Library/Screen Savers/`. Select it in **System Settings → Screen Saver**.

#### Linux

```bash
cmake --install build
```

This installs the binary to `bin/` and a `kaleidoscope.desktop` entry to `share/applications/`, making it available to XScreenSaver, `gnome-screensaver` and KDE's screen-locker.

---

## Usage

The same binary runs both as a screen saver and as a normal application.

### As a screen saver

Launching with the standard screen-saver flags makes it go fullscreen on the primary display, hide the cursor, stay on top, and exit on keyboard or mouse activity:

```bash
Kaleidoscope -s          # macOS / Linux
Kaleidoscope.scr /s      # Windows
```

### As a standalone demo

Run with no arguments for a fullscreen demo without the screen-saver protocol — useful for testing and for kiosk-style use:

```bash
./build/Kaleidoscope.app/Contents/MacOS/Kaleidoscope
```

### As a windowed application

Pass `-w` (or `--windowed`) to run it in an ordinary resizable window. In this mode the app does **not** quit on mouse movement or clicks, so it behaves like a regular desktop program:

```bash
./build/Kaleidoscope.app/Contents/MacOS/Kaleidoscope -w
```

### Mode summary

| Invocation | Window | Exits on input | Intended for |
|---|---|---|---|
| _(no arguments)_ | Fullscreen, every display | Yes | Standalone demo, kiosk use |
| `-s` / `/s` / `--screensaver` | Fullscreen, every display | Yes | Screen saver |
| `-w` / `--windowed` | 960×600 window | Only on Esc | Normal desktop use, debugging |
| `/p <handle>` | Embedded in preview pane | — | Screen-saver control-panel preview |
| `-c` / `/c` | — | — | Configuration (reserved; currently exits) |

### Multiple displays

In fullscreen modes the saver opens one fullscreen window per display, so every monitor is covered at once. Use `--screen` to restrict it to a single display — handy when testing, so the other monitors stay usable:

```bash
Kaleidoscope -s --screen built-in     # internal display only
Kaleidoscope -s --screen primary      # the main display only
Kaleidoscope -s --screen 0            # by index
Kaleidoscope -s                       # all displays (default)
```

### The intro screen

Run **without any effect flags** and the original intro plays first — the same `--- THE BEST OF THE BEST ---` / `SONY CPD-1304 MONITOR` screen the 1995 program showed, complete with its palette cascade. It starts black and colours cascade in over roughly 15 seconds, then it waits for a keypress: press **any key** to drop into the kaleidoscope, exactly as the original did.

![Intro screen](docs/screenshot-intro.png)

The intro is **never** shown in screen-saver mode. A screen saver must not sit waiting for input, so that mode goes straight to the effect. Passing an effect flag (such as `/O2`) also bypasses it, matching the DOS original, which only played the intro when started with no arguments.

### Keyboard controls

These are the controls of the original DOS program and they all still work. Press **Esc** to quit.

| Key | Action |
|-----|--------|
| `Space` | Switch to the next (random) effect |
| `c` | Toggle colour cycling — freezes the palette rotation |
| `d` | Toggle drawing — freezes the shapes, palette keeps cycling |
| `a` | Toggle automatic effect switching |
| `8` | Turn rotation on |
| `4` | Turn rotation off |
| `w` | Toggle the animated transition variants |
| `Esc` | Quit |

Moving the mouse or clicking also exits — but only in fullscreen (screen-saver) mode. In windowed mode `Esc` is the way out.

### Command-line options

The original's switches are preserved:

| Option | Meaning |
|--------|---------|
| `/O n` | Start on effect *n* (1–4) |
| `/S n` | Palette-write chunk count (1–9), controls the animation speed |
| `/W` | Disable random transition variants |
| `/4` | Disable rotation |
| `/A` | Enable auto-switching |
| `/P n` | Auto-switch probability |
| `/T n` | Quit after *n* timer ticks |

Example — start on effect 2 with slower palette cycling:

```bash
./build/Kaleidoscope.app/Contents/MacOS/Kaleidoscope /O2 /S2
```

---

## Project layout

| File | Purpose |
|------|---------|
| [sony_core.h](sony_core.h) / [sony_core.cpp](sony_core.cpp) | Portable, dependency-free emulation of the DOS demo — framebuffer, palette engine, effects, variants and RNG |
| [font8x8.h](font8x8.h) | IBM CGA/BIOS 8×8 bitmap font used by the intro screen |
| [screensaverwidget.h](screensaverwidget.h) / [screensaverwidget.cpp](screensaverwidget.cpp) | Qt Widget that blits the VGA framebuffer to screen and drives the demo state machine |
| [main.cpp](main.cpp) | Entry point; parses the screen-saver protocol flags and creates the fullscreen window |
| [CMakeLists.txt](CMakeLists.txt) | Cross-platform build — `.scr` on Windows, `.app` bundle on macOS, ELF + desktop entry on Linux |
| [Info.plist.in](Info.plist.in) | macOS bundle metadata template |
| [kaleidoscope.desktop](kaleidoscope.desktop) | Linux XDG screen-saver registration |
| [tools/frame_dump.cpp](tools/frame_dump.cpp) | Headless harness that renders frames to PPM — verifies the core without a GUI |
| [tools/e2e.sh](tools/e2e.sh) | End-to-end checks: launches each mode, captures the live window and asserts on the demo phase |

### Architecture

The emulation core knows nothing about Qt or any operating system — it exposes a framebuffer, a set of DAC registers and a small state machine:

```
┌──────────────────────┐        ┌───────────────────────┐
│      SonyDemo        │        │  ScreensaverWidget    │
│  (portable C++ core) │        │      (Qt Widget)      │
├──────────────────────┤        ├───────────────────────┤
│ framebuffer()   ─────┼───────▶│ indexed → RGB32       │
│ dac_regs()      ─────┼───────▶│ nearest-neighbour     │
│                      │        │ scale + draw          │
│ step() / key()  ◀────┼────────│ timer + key events    │
│ phase() / quit()     │        │                       │
└──────────────────────┘        └───────────────────────┘
```

`ScreensaverWidget::tick()` converts the 320×200 8-bit framebuffer to `QImage::Format_RGB32` every frame using the DAC palette (6-bit → 8-bit via `(v << 2) | (v >> 4)`), then scales it with `Qt::FastTransformation` to preserve hard pixel edges.

---

## Testing

`tools/e2e.sh` launches the real application in each mode, renders the live window to a PNG and asserts on it. It runs headlessly and needs no screen-recording permission:

```bash
./tools/e2e.sh build
```

It checks that windowed mode plays the intro and holds it, that screen-saver mode never shows the intro, that an effect flag bypasses it, and that the demo does not quit on its own. Every run is pinned with `--screen built-in` so the test never covers your other monitors.

The capture uses a `--snapshot <file>@<ms>` hook: it grabs the widget after `<ms>` and exits, reporting the demo's internal phase alongside the image. Pixel heuristics cannot reliably tell the intro from a busy effect — both are large, colourful, black-dominated frames — so the tests assert on the reported phase rather than guessing from colours.

---

## Notes on fidelity

A few deliberate choices were made where a literal transcription would have produced a poor screen saver:

- **The intro is skipped in screen-saver mode.** The original intro waits for a key press, and a screen saver must never wait for input. It plays in full in standalone and windowed modes, where waiting is correct.
- **Keys are applied immediately** rather than only at the original's keyboard poll point. The DOS program only read the keyboard every `loop_count_` frames — up to ~63 frames, which is over three seconds. The poll point is still honoured for the timeout and auto-switch logic.
- **Only deliberate keypresses dismiss the intro.** macOS delivers stray key events when a fullscreen window appears (observed: Delete and `u`, roughly one launch in eight); these are filtered out so the intro cannot vanish on its own.
- **The framebuffer is stretched to fill the screen** rather than letterboxed, trading the exact 4:3 aspect ratio for a screen saver that uses the whole display. Nearest-neighbour scaling keeps the chunky VGA look.
- **Two byte-level bugs in the original** are preserved in the transcribed primitives where they affect output character: the `|dy|` initialisation error in y-major `draw_line` and the off-by-one radius shift in `fill_circle`. Both are documented in comments at the relevant lines.

---

## License

Released for preservation and educational purposes. The original demo and the SONY CPD-1304 name are the property of their respective owners; this is an independent reimplementation.
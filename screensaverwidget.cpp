// screensaverwidget.cpp — Renders SonyDemo framebuffer via Qt painting.
#include "screensaverwidget.h"

#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <cstring>
#include <string>
#include <vector>
#include <cctype>

ScreensaverWidget::ScreensaverWidget(QWidget *parent)
    : QWidget(parent)
    , m_demo(new SonyDemo())
    , m_timer(new QTimer(this))
    , m_vgaImage(VGA_W, VGA_H, QImage::Format_RGB32)
    , m_accumulator(0.0)
    , m_running(false)
    , m_mouseInitialized(false)
{
    // Fullscreen black background, no frame
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setCursor(Qt::BlankCursor);
    setFocusPolicy(Qt::StrongFocus);

    // Catch keys at application level too — macOS fullscreen windows can
    // otherwise route key events away from the widget.
    qApp->installEventFilter(this);

    connect(m_timer, &QTimer::timeout, this, &ScreensaverWidget::tick);
}

ScreensaverWidget::~ScreensaverWidget()
{
    stopAnimation();
    delete m_demo;
}

SonyDemo::Phase ScreensaverWidget::demoPhase() const
{
    return m_demo->phase();
}

const char *ScreensaverWidget::phaseName(SonyDemo::Phase p)
{
    switch (p) {
    case SonyDemo::Phase::Intro:   return "intro";
    case SonyDemo::Phase::Variant: return "variant";
    case SonyDemo::Phase::Main:    return "main";
    }
    return "unknown";
}

void ScreensaverWidget::initDemo(int argc, char **argv)
{
    // Only the DOS original's own flags may reach the emulation core. Host
    // options (-w, -s, --snapshot <path>) are our invention and must be stripped
    // first: the core parses "/O", "/S", "/T", "/W", "/4", "/A", "/P" by prefix,
    // so a POSIX path argument such as /tmp/x.png looks exactly like the "/T"
    // timeout flag and silently makes the demo quit.
    std::vector<std::string> kept;
    kept.emplace_back(argc > 0 ? argv[0] : "Kaleidoscope");
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "-w" || a == "--windowed" ||
            a == "-s" || a == "--screensaver" || a == "/s" ||
            a == "-c" || a == "/c" || a == "--configure")
            continue;
        if (a == "--snapshot" || a.rfind("--snapshot", 0) == 0) {
            if (a == "--snapshot")
                ++i;                    // drop the value too
            continue;
        }
        // Anything else that is not a DOS-style flag (slash/dash + single letter)
        // is host input, not a demo option.
        const bool dosFlag = a.size() >= 2 &&
                             (a[0] == '/' || a[0] == '-') &&
                             std::isalpha(static_cast<unsigned char>(a[1])) &&
                             a.find('.') == std::string::npos &&
                             a.find('@') == std::string::npos &&
                             a.find('/', 1) == std::string::npos;
        if (dosFlag)
            kept.push_back(a);
    }

    std::vector<char *> raw;
    raw.reserve(kept.size());
    for (std::string &s : kept)
        raw.push_back(s.data());
    m_demo->init(static_cast<int>(raw.size()), raw.data());
}

void ScreensaverWidget::startAnimation()
{
    // The core only plays the intro when init() was given no arguments, but the
    // screen-saver flags (-s, -w, /p ...) are arguments too. If the caller asked
    // for the intro, force it here so the flag is honoured either way.
    if (m_playIntro && m_demo->phase() != SonyDemo::Phase::Intro)
        m_demo->intro_begin();

    if (!m_running) {
        m_running = true;
        m_accumulator = 0.0;
        // Demo frames are paced by tick(); this is just the poll rate (~120Hz)
        m_timer->start(8);
    }
}

void ScreensaverWidget::stopAnimation()
{
    m_running = false;
    m_timer->stop();
}

void ScreensaverWidget::tick()
{
    if (!m_running)
        return;

    // Screen-saver mode discards the intro the first time we reach it: a screen
    // saver must never sit waiting for a key press.
    if (!m_playIntro && m_demo->phase() == SonyDemo::Phase::Intro) {
        m_demo->intro_end();
    }

    // Advance the demo at its native cadence. The DOS original clocked one
    // demo frame every cyc_len_ vertical blanks (70Hz); use a wall-clock
    // accumulator so animation speed is independent of the host timer rate.
    const double dt = m_demo->frame_dt();

    if (!m_clock.isValid())
        m_clock.start();

    m_accumulator += m_clock.restart() / 1000.0;   // ms -> s

    // Guard against huge catch-up bursts after a suspend/stall
    if (m_accumulator > 0.25)
        m_accumulator = dt;

    if (m_accumulator < dt)
        return;
    m_accumulator -= dt;

    switch (m_demo->phase()) {
    case SonyDemo::Phase::Intro:
        // The DOS original polls the keyboard every frame here (INT 16h AH=1)
        // and leaves the intro as soon as any key is pressed.
        //
        // Deliberate input only. On macOS, showing a fullscreen window can make
        // the system deliver stray key events the user never typed (observed:
        // Delete and 'u' arriving with no modifiers, ~1 run in 8). Treating
        // those as "any key" dismissed the intro almost immediately at random.
        // The intro is dismissed by a key the user actually pressed (see
        // handleKey), which also matches the original's behaviour of requiring a
        // real keypress rather than a buffered/system one.
        if (m_introDismissRequested) {
            m_introDismissRequested = false;
            m_keybuf.clear();
            m_demo->intro_end();
        } else {
            m_demo->intro_frame();
        }
        break;
    case SonyDemo::Phase::Variant:
        if (!m_demo->variant_step()) {
            m_demo->switch_effect_continue();
        }
        break;
    case SonyDemo::Phase::Main: {
        bool poll = m_demo->step();

        // The DOS original only reads the keyboard at the poll point (every
        // loop_count_ frames, which can be up to ~63 frames). At screensaver
        // scale that would make keys feel dead for seconds, so keys are also
        // applied on the frame they arrive — the poll point still gets the
        // timeout / auto-switch logic.
        if (poll) {
            if (!m_keybuf.isEmpty()) {
                m_demo->key(m_keybuf.takeFirst());
            } else {
                m_demo->poll_no_key();
            }
        }
        break;
    }
    }

    // Drain any remaining buffered keys immediately so input feels responsive
    // instead of waiting for the next (possibly far-off) poll point.
    while (!m_keybuf.isEmpty())
        m_demo->key(m_keybuf.takeFirst());

    if (m_demo->quit()) {
        QGuiApplication::quit();
        return;
    }

    renderFrame();
    update();
}

void ScreensaverWidget::renderFrame()
{
    const uint8_t *fb  = m_demo->framebuffer();
    const uint8_t *dac = m_demo->dac_regs();

    // Convert 320x200 indexed color (6-bit DAC) → RGB32
    for (int y = 0; y < VGA_H; y++) {
        QRgb *line = reinterpret_cast<QRgb *>(m_vgaImage.scanLine(y));
        for (int x = 0; x < VGA_W; x++) {
            uint8_t idx = fb[y * VGA_W + x];
            uint8_t r6 = dac[idx * 3 + 0];
            uint8_t g6 = dac[idx * 3 + 1];
            uint8_t b6 = dac[idx * 3 + 2];
            // 6-bit → 8-bit: (v << 2) | (v >> 4)
            uint8_t r = (r6 << 2) | (r6 >> 4);
            uint8_t g = (g6 << 2) | (g6 >> 4);
            uint8_t b = (b6 << 2) | (b6 >> 4);
            line[x] = qRgb(r, g, b);
        }
    }

    updateScaledImage();
}

void ScreensaverWidget::updateScaledImage()
{
    // Nearest-neighbor scale to fill entire widget (screensaver fills whole screen)
    m_scaledImage = m_vgaImage.scaled(size(), Qt::IgnoreAspectRatio,
                                       Qt::FastTransformation);
}

void ScreensaverWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // Black background
    painter.fillRect(rect(), Qt::black);

    if (!m_scaledImage.isNull()) {
        // Draw scaled to fill entire widget
        painter.drawImage(rect(), m_scaledImage);
    }
}

void ScreensaverWidget::keyPressEvent(QKeyEvent *event)
{
    // Reached only for keys delivered to this focused widget — i.e. real
    // keypresses. The app-level filter handles the rest.
    if (!event->isAutoRepeat())
        m_introDismissRequested = true;
    handleKey(event);
    QWidget::keyPressEvent(event);
}

void ScreensaverWidget::handleKey(QKeyEvent *event)
{
    // Escape always exits, even in windowed demo mode
    if (event->key() == Qt::Key_Escape) {
        QGuiApplication::quit();
        return;
    }

    // Map Qt key to ASCII for the demo core (same as original DOS INT 16h)
    QString text = event->text();
    if (!text.isEmpty()) {
        QChar ch = text.at(0);
        if (ch.unicode() < 256)
            m_keybuf.append(ch.unicode());
    } else if (event->key() == Qt::Key_Space) {
        m_keybuf.append(' ');
    }
}

bool ScreensaverWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (!ke->isAutoRepeat()) {
            // This filter sees every key in the application, including stray
            // events macOS emits when a fullscreen window appears. Only a key
            // aimed at one of our own widgets counts as deliberate user input
            // for the purposes of "press any key to continue".
            QWidget *w = qobject_cast<QWidget *>(obj);
            const bool mine = w && isAncestorOf(w);

            if (mine)
                m_introDismissRequested = true;
            handleKey(ke);
            return true; // consume: keys drive the demo, not Qt widgets
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ScreensaverWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    activateWindow();
    raise();
    setFocus(Qt::OtherFocusReason);
}

void ScreensaverWidget::mouseMoveEvent(QMouseEvent *event)
{
    // Exit on significant mouse movement (> 10 pixels) — screen saver only
    if (!m_exitOnInput) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!m_mouseInitialized) {
        m_lastMousePos = event->pos();
        m_mouseInitialized = true;
        return;
    }

    QPoint delta = event->pos() - m_lastMousePos;
    if (delta.manhattanLength() > 10) {
        QGuiApplication::quit();
    }
    m_lastMousePos = event->pos();
    QWidget::mouseMoveEvent(event);
}

void ScreensaverWidget::mousePressEvent(QMouseEvent *event)
{
    // Any click exits the screen saver, but not the windowed demo
    if (m_exitOnInput)
        QGuiApplication::quit();
    QWidget::mousePressEvent(event);
}

void ScreensaverWidget::resizeEvent(QResizeEvent *event)
{
    updateScaledImage();
    QWidget::resizeEvent(event);
}

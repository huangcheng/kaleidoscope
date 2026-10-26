// screensaverwidget.h — Qt Widget that renders the SonyDemo core as a fullscreen
// screensaver with nearest-neighbor scaled VGA output. Cross-platform (Win/Mac/Linux).
#pragma once

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QElapsedTimer>
#include <QList>
#include "sony_core.h"

class ScreensaverWidget : public QWidget {
    Q_OBJECT

public:
    explicit ScreensaverWidget(QWidget *parent = nullptr);
    ~ScreensaverWidget() override;

    // Initialize demo with command-line args (for screensaver protocol)
    void initDemo(int argc, char **argv);

    // Start/stop the animation loop
    void startAnimation();
    void stopAnimation();

    // When true (the default), any keyboard or mouse activity quits the app —
    // correct for a screen saver. Windowed demo mode turns this off so the
    // window behaves like a normal application.
    void setExitOnInput(bool enable) { m_exitOnInput = enable; }

    // When true, the original's intro screen plays and waits for a key, exactly
    // as the DOS program did when started without arguments. Screen-saver mode
    // leaves this off: a screen saver must never wait for input.
    void setPlayIntro(bool enable) { m_playIntro = enable; }

    // Current demo phase, for tests and diagnostics.
    SonyDemo::Phase demoPhase() const;
    static const char *phaseName(SonyDemo::Phase p);

    // Install as app-level event filter for reliable key capture on macOS fullscreen
    bool eventFilter(QObject *obj, QEvent *event) override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void tick();

private:
    void renderFrame();
    void updateScaledImage();
    void handleKey(QKeyEvent *event);

    SonyDemo       *m_demo;
    QTimer         *m_timer;
    QImage          m_vgaImage;      // 320x200 paletted or RGB32
    QImage          m_scaledImage;   // Nearest-neighbor scaled to widget size
    double          m_accumulator;
    QElapsedTimer   m_clock;
    bool            m_running;
    QPoint          m_lastMousePos;
    bool            m_mouseInitialized;
    bool            m_exitOnInput = true;
    bool            m_playIntro = false;
    bool            m_introDismissRequested = false;
    QList<int>      m_keybuf;        // buffered keypresses for demo core

    static constexpr int VGA_W = 320;
    static constexpr int VGA_H = 200;
};

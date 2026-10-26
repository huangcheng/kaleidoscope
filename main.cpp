// main.cpp — Cross-platform screensaver entry point.
// Windows: /s (show), /p <hwnd> (preview), /c (configure)
// macOS:   -s (show), -p <windowid> (preview in System Preferences)
// Linux:   --screensaver (show), standard XDG screensaver protocol
#include "screensaverwidget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QScreen>
#include <QDebug>
#include <QPixmap>
#include <QTimer>
#include <QGuiApplication>
#include <QPixmap>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static bool parseScreensaverArgs(int argc, char *argv[],
                                  QString &mode, quintptr &previewHandle)
{
    mode = "show"; // default
    previewHandle = 0;

    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromLocal8Bit(argv[i]).toLower().trimmed();

        // Windows screensaver protocol
        if (arg == "/s" || arg == "-s" || arg == "--screensaver") {
            mode = "show";
            return true;
        }
        if (arg.startsWith("/p") || arg.startsWith("-p")) {
            mode = "preview";
            // Handle may be attached or next arg
            QString handleStr;
            if (arg.length() > 2) {
                handleStr = arg.mid(2).trimmed();
            } else if (i + 1 < argc) {
                handleStr = QString::fromLocal8Bit(argv[++i]);
            }
            if (!handleStr.isEmpty())
                previewHandle = handleStr.toULongLong();
            return true;
        }
        if (arg == "/c" || arg == "-c" || arg == "--configure") {
            mode = "configure";
            return true;
        }
        if (arg == "-w" || arg == "--windowed") {
            mode = "windowed";
            return true;
        }
    }

    // No screensaver args → run as a standalone fullscreen demo
    mode = "standalone";
    return false;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Kaleidoscope");
    app.setOrganizationName("Kaleidoscope");
    app.setApplicationVersion("1.0.0");

    QString mode;
    quintptr previewHandle = 0;
    parseScreensaverArgs(argc, argv, mode, previewHandle);

    if (mode == "configure") {
        // TODO: Configuration dialog (palette speed, effect preferences, etc.)
        // For now, just exit — no config UI yet
        return 0;
    }

    // Show the original's intro screen when the demo was started without any
    // effect flags — matching the DOS program, which only played the intro when
    // run with no arguments. A screen saver never plays it: it must not wait
    // for input.
    bool hasEffectArgs = false;
    for (int i = 1; i < argc; i++) {
        const QString a = QString::fromLocal8Bit(argv[i]).toLower();
        if (a == "-w" || a == "--windowed" ||
            a == "-s" || a == "--screensaver" || a == "/s" ||
            a == "-c" || a == "/c" || a == "--configure")
            continue;
        if (a == "--screen" || a.startsWith("--snapshot")) {
            ++i;                        // consume the value that follows
            continue;
        }
        // The core's own flags are slash/dash + a single letter, optionally
        // followed by digits (/O2, /S4, /T30, -W, -4). Require that shape, and
        // reject anything path-like: a POSIX path such as /tmp/x.png otherwise
        // looks exactly like the /T (timeout) flag.
        const bool shape = a.size() >= 2 &&
                           (a.at(0) == QLatin1Char('/') || a.at(0) == QLatin1Char('-')) &&
                           a.at(1).isLetter() &&
                           !a.contains(QLatin1Char('.')) &&
                           !a.contains(QLatin1Char('@')) &&
                           a.indexOf(QLatin1Char('/'), 1) < 0;
        if (shape)
            hasEffectArgs = true;
    }
    const bool playIntro = (mode != "show" && !hasEffectArgs);

    ScreensaverWidget widget;

    // Pass original args to demo core (for effect selection flags like /O, /S, etc.)
    widget.initDemo(argc, argv);

    if (mode == "preview" && previewHandle != 0) {
        // Preview mode: embed inside the provided window handle
        widget.setPlayIntro(playIntro);
#ifdef Q_OS_WIN
        HWND parentHwnd = reinterpret_cast<HWND>(previewHandle);
        if (parentHwnd && IsWindow(parentHwnd)) {
            RECT rc;
            GetClientRect(parentHwnd, &rc);
            widget.setParent(Q_NULLPTR);
            // Create native window embedded in the preview pane
            widget.winId(); // force native window creation
            SetParent(reinterpret_cast<HWND>(widget.winId()), parentHwnd);
            widget.setGeometry(0, 0, rc.right - rc.left, rc.bottom - rc.top);
            widget.show();
            widget.startAnimation();
            return app.exec();
        }
#endif
#ifdef Q_OS_MACOS
        // macOS preview: handle passed as window ID — use Qt's native embedding
        if (previewHandle != 0) {
            widget.winId(); // force native window creation
            widget.resize(320, 200);
            widget.show();
            widget.startAnimation();
            return app.exec();
        }
#endif
        // Fallback: just show normally
        widget.resize(320, 200);
        widget.show();
        widget.startAnimation();
        return app.exec();
    }

    // Fullscreen screensaver, standalone fullscreen demo, or windowed demo
    //
    // In fullscreen modes one widget is created per screen so every display is
    // covered at once. `--screen <n>|built-in|primary` restricts this to a single
    // display, which is what the E2E tests use so they do not disturb the user's
    // other monitors.
    QString screenSelector;
    for (int i = 1; i < argc; i++) {
        if (QString::fromLocal8Bit(argv[i]) == "--screen" && i + 1 < argc)
            screenSelector = QString::fromLocal8Bit(argv[i + 1]).toLower();
    }

    QList<QScreen *> targets;
    if (!screenSelector.isEmpty() && screenSelector != "all") {
        QScreen *chosen = nullptr;
        if (screenSelector == "built-in" || screenSelector == "internal") {
            for (QScreen *s : QGuiApplication::screens()) {
                if (s->name().startsWith("Built-in") ||
                    s->name().startsWith("Color LCD") ||
                    s->name().startsWith("eDP")) {
                    chosen = s;
                    break;
                }
            }
            // Single-display machines have no internal screen name to match.
            if (!chosen && QGuiApplication::screens().size() == 1)
                chosen = QGuiApplication::primaryScreen();
        } else if (screenSelector == "primary") {
            chosen = QGuiApplication::primaryScreen();
        } else {
            const int idx = screenSelector.toInt();
            if (idx >= 0 && idx < QGuiApplication::screens().size())
                chosen = QGuiApplication::screens().at(idx);
        }
        if (chosen)
            targets << chosen;
    } else {
        targets = QGuiApplication::screens();
    }
    if (targets.isEmpty() && QGuiApplication::primaryScreen())
        targets << QGuiApplication::primaryScreen();

    QList<ScreensaverWidget *> widgets;
    for (QScreen *screen : targets) {
        auto *w = new ScreensaverWidget();
        w->initDemo(argc, argv);
        w->setExitOnInput(mode != "windowed");
        w->setPlayIntro(playIntro);
        if (mode == "windowed") {
            w->resize(960, 600);
        } else {
            w->setGeometry(screen->geometry());
            w->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
            w->setWindowState(Qt::WindowFullScreen);
        }
        w->setWindowTitle("Kaleidoscope — SONY CPD-1304 Screensaver");
        widgets << w;
    }

    // E2E self-test hook: `--snapshot <file>@<ms>` renders the live widget to a PNG
    // after <ms> and exits. Lets a test assert on what is actually on screen
    // without needing screen-recording permission, and works headlessly in CI.
    QString snapshotSpec;
    for (int i = 1; i < argc; i++) {
        if (QString::fromLocal8Bit(argv[i]).startsWith("--snapshot") && i + 1 < argc)
            snapshotSpec = QString::fromLocal8Bit(argv[i + 1]);
    }
    if (!snapshotSpec.isEmpty() && !widgets.isEmpty()) {
        const QStringList parts = snapshotSpec.split('@');
        const QString path = parts.value(0);
        const int ms = parts.size() > 1 ? parts.at(1).toInt() : 1500;
        ScreensaverWidget *first = widgets.first();
        QTimer::singleShot(ms, first, [first, path]() {
            const QPixmap pm = first->grab();
            // Report the demo phase alongside the image. Pixel heuristics cannot
            // reliably tell the intro from a busy effect, so tests assert on this
            // instead of guessing from colours.
            qInfo().noquote() << "snapshot" << path
                              << (pm.save(path) ? "ok" : "FAILED")
                              << pm.size()
                              << "phase=" << ScreensaverWidget::phaseName(first->demoPhase());
            QGuiApplication::quit();
        });
    }

    // Show only the first window when snapshotting, so the test captures the
    // intended display instead of a random one.
    const bool snapshotting = !snapshotSpec.isEmpty();
    for (int i = 0; i < widgets.size(); i++) {
        ScreensaverWidget *w = widgets.at(i);
        if (snapshotting && i > 0)
            continue;
        w->show();
        w->activateWindow();
        w->raise();
        w->setFocus();
        w->startAnimation();
    }

    return app.exec();
}

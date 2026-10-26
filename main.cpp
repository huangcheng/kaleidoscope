// main.cpp — Cross-platform screensaver entry point.
// Windows: /s (show), /p <hwnd> (preview), /c (configure)
// macOS:   -s (show), -p <windowid> (preview in System Preferences)
// Linux:   --screensaver (show), standard XDG screensaver protocol
#include "screensaverwidget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QScreen>
#include <QDebug>

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
    }

    // No screensaver args → run as standalone demo (fullscreen)
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
    bool isScreensaver = parseScreensaverArgs(argc, argv, mode, previewHandle);

    if (mode == "configure") {
        // TODO: Configuration dialog (palette speed, effect preferences, etc.)
        // For now, just exit — no config UI yet
        return 0;
    }

    ScreensaverWidget widget;

    // Pass original args to demo core (for effect selection flags like /O, /S, etc.)
    widget.initDemo(argc, argv);

    if (mode == "preview" && previewHandle != 0) {
        // Preview mode: embed inside the provided window handle
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

    // Fullscreen screensaver or standalone mode
    widget.setWindowTitle("Kaleidoscope — SONY CPD-1304 Screensaver");

    if (isScreensaver || mode == "standalone") {
        // Fullscreen on primary screen
        QScreen *screen = app.primaryScreen();
        if (screen) {
            widget.setGeometry(screen->geometry());
        }
        widget.setWindowState(Qt::WindowFullScreen);
        widget.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    } else {
        widget.resize(640, 480);
    }

    widget.show();
    widget.activateWindow();
    widget.raise();
    widget.setFocus();
    widget.startAnimation();

    return app.exec();
}

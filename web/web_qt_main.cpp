// web_qt_main.cpp — Qt for WebAssembly entry point.
// Renders SonyDemo to a QImage, displays it fullscreen in a QWidget.

#include "sony_core.h"
#include <QApplication>
#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QKeyEvent>
#include <QScreen>

static const int W = 320;
static const int H = 200;

class KaleidoscopeWidget : public QWidget {
    Q_OBJECT
public:
    explicit KaleidoscopeWidget(QWidget *parent = nullptr)
        : QWidget(parent), m_image(W, H, QImage::Format_RGB32)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setFocusPolicy(Qt::StrongFocus);
        m_image.fill(Qt::black);

        // Initialize demo with no arguments → play intro
        m_demo.init(0, nullptr);

        // Start render timer at ~70fps (matching VGA refresh)
        m_timer.setInterval(14); // ~71fps
        connect(&m_timer, &QTimer::timeout, this, &KaleidoscopeWidget::tick);
        m_timer.start();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        // Scale to fill widget with nearest-neighbour
        painter.drawImage(rect(), m_image);
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) return;

        int ch = 0;
        QString text = e->text();
        if (text.length() == 1) {
            ch = text.at(0).toLatin1();
        } else if (e->key() == Qt::Key_Space) {
            ch = 32;
        }

        if (ch > 0 && ch < 256) {
            m_demo.key(ch);
        }
    }

    void resizeEvent(QResizeEvent *) override {
        update();
    }

private slots:
    void tick() {
        // Step the simulation
        SonyDemo::Phase p = m_demo.phase();
        if (p == SonyDemo::Phase::Intro) {
            m_demo.intro_frame();
        } else {
            m_demo.step();
        }

        // Read framebuffer and DAC
        const uint8_t *fb = m_demo.framebuffer();
        const uint8_t *dac = m_demo.dac_regs();

        // Convert indexed framebuffer to RGB32
        for (int y = 0; y < H; y++) {
            QRgb *scanline = reinterpret_cast<QRgb *>(m_image.scanLine(y));
            for (int x = 0; x < W; x++) {
                uint8_t idx = fb[y * W + x];
                uint8_t r = dac[idx * 3];
                uint8_t g = dac[idx * 3 + 1];
                uint8_t b = dac[idx * 3 + 2];
                // 6-bit DAC → 8-bit
                scanline[x] = qRgb(
                    (r << 2) | (r >> 4),
                    (g << 2) | (g >> 4),
                    (b << 2) | (b >> 4)
                );
            }
        }

        update();
    }

private:
    SonyDemo m_demo;
    QImage m_image;
    QTimer m_timer;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    KaleidoscopeWidget widget;
    widget.setWindowTitle("Kaleidoscope — SONY CPD-1304");
    widget.showFullScreen();

    return app.exec();
}

#include "web_qt_main.moc"

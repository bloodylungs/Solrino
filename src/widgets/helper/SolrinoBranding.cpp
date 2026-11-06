#include "widgets/helper/SolrinoBranding.hpp"

#include "singletons/Settings.hpp"

#include <pajlada/signals/signalholder.hpp>

#include <QCursor>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>

namespace chatterino {

namespace {

const auto LOGO_PATH = QStringLiteral(":/icon.png");

// ---------------------------------------------------------------- Title logo

constexpr int SWEEP_FRAME_MS = 40;
constexpr qint64 SWEEP_PERIOD_MS = 2400;
const QColor SWEEP_COLOR(58, 0, 6, 235);

class TitleLogo : public QWidget
{
public:
    TitleLogo()
        : logo_(LOGO_PATH)
    {
        QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        policy.setHorizontalStretch(1);
        this->setSizePolicy(policy);
        this->setAccessibleName("Solrino");
        // The title bar still has to drag the window from here
        this->setAttribute(Qt::WA_TransparentForMouseEvents);

        this->timer_.setInterval(SWEEP_FRAME_MS);
        QObject::connect(&this->timer_, &QTimer::timeout, this, [this] {
            if (this->shouldAnimate())
            {
                this->update(this->logoRect());
            }
        });
        this->clock_.start();

        getSettings()->solrinoLayoutLogo.connect(
            [this](const QString &, auto) {
                this->update();
            },
            this->connections_, false);
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        this->timer_.start();
    }

    void hideEvent(QHideEvent *event) override
    {
        QWidget::hideEvent(event);
        this->timer_.stop();
    }

    void paintEvent(QPaintEvent * /*event*/) override
    {
        if (getSettings()->solrinoLayoutLogo.getValue() == "hidden")
        {
            return;
        }
        const auto rect = this->logoRect();
        const auto ratio = this->devicePixelRatioF();
        const QSize size(int(std::ceil(rect.width() * ratio)),
                         int(std::ceil(rect.height() * ratio)));
        if (size.isEmpty())
        {
            return;
        }

        QImage frame(size, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);
        {
            QPainter layer(&frame);
            layer.setRenderHint(QPainter::SmoothPixmapTransform);
            layer.drawPixmap(frame.rect(), this->logo_);

            // SourceAtop only tints where the logo is, not the title bar
            const double width = frame.width();
            const double progress =
                double(this->clock_.elapsed() % SWEEP_PERIOD_MS) /
                SWEEP_PERIOD_MS;
            const double center = -0.5 * width + progress * 2.0 * width;
            const double band = 0.45 * width;
            QLinearGradient sweep(center - band, 0, center + band, width * 0.4);
            auto clear = SWEEP_COLOR;
            clear.setAlpha(0);
            sweep.setColorAt(0.0, clear);
            sweep.setColorAt(0.5, SWEEP_COLOR);
            sweep.setColorAt(1.0, clear);
            layer.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            layer.fillRect(frame.rect(), sweep);
        }
        frame.setDevicePixelRatio(ratio);

        QPainter painter(this);
        painter.drawImage(rect.topLeft(), frame);
    }

private:
    QRect logoRect() const
    {
        const int side = std::max(10, this->height() - 8);
        const auto where = getSettings()->solrinoLayoutLogo.getValue();
        int x = 6;
        if (where == "center")
        {
            x = (this->width() - side) / 2;
        }
        else if (where == "right")
        {
            x = this->width() - side - 6;
        }
        return {x, (this->height() - side) / 2, side, side};
    }

    bool shouldAnimate() const
    {
        const auto *window = this->window();
        if (window == nullptr || window->isMinimized() || !this->isVisible())
        {
            return false;
        }
        // Follows the app's "animate only when focused" setting
        return !getSettings()->animationsWhenFocused || window->isActiveWindow();
    }

    QPixmap logo_;
    QTimer timer_;
    QElapsedTimer clock_;
    pajlada::Signals::SignalHolder connections_;
};

// ---------------------------------------------------------------- Intro

constexpr int INTRO_FRAME_MS = 16;
constexpr qint64 SLIDE_MS = 1100;
constexpr qint64 HOLD_MS = 150;
constexpr qint64 BLINK_MS = 320;
constexpr int BLINK_COUNT = 3;
constexpr qint64 FADE_MS = 280;
constexpr int INTRO_LOGO_SIDE = 260;

class IntroWindow : public QWidget
{
public:
    explicit IntroWindow(std::function<void()> onFinished)
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                               Qt::Tool | Qt::WindowTransparentForInput |
                               Qt::WindowDoesNotAcceptFocus)
        , logo_(LOGO_PATH)
        , onFinished_(std::move(onFinished))
    {
        this->setAttribute(Qt::WA_TranslucentBackground);
        this->setAttribute(Qt::WA_ShowWithoutActivating);
        this->setAttribute(Qt::WA_DeleteOnClose);

        auto *screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen == nullptr)
        {
            screen = QGuiApplication::primaryScreen();
        }
        this->area_ = screen != nullptr ? screen->geometry()
                                        : QRect(0, 0, 1280, 720);
        const int side = std::min(
            {INTRO_LOGO_SIDE, this->area_.width() / 2, this->area_.height() / 2});
        this->setFixedSize(side, side);

        this->timer_.setInterval(INTRO_FRAME_MS);
        QObject::connect(&this->timer_, &QTimer::timeout, this, [this] {
            this->step();
        });
    }

    void start()
    {
        this->place(0.0);
        this->show();
        this->clock_.start();
        this->timer_.start();
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(this->rect(), this->logo_);
    }

private:
    void place(double slide)
    {
        const int x = this->area_.center().x() - this->width() / 2;
        const int startY = this->area_.bottom() + 1;
        const int endY = this->area_.center().y() - this->height() / 2;
        this->move(x, startY + int((endY - startY) * slide));
    }

    void step()
    {
        const auto elapsed = this->clock_.elapsed();
        const auto blinkStart = SLIDE_MS + HOLD_MS;
        const auto fadeStart = blinkStart + BLINK_MS * BLINK_COUNT;

        if (elapsed < SLIDE_MS)
        {
            // Eases out, so it glides to a stop at the center
            const double t = double(elapsed) / SLIDE_MS;
            this->place(1.0 - std::pow(1.0 - t, 3.0));
        }
        else if (elapsed < blinkStart)
        {
            this->place(1.0);
            this->setWindowOpacity(1.0);
        }
        else if (elapsed < fadeStart)
        {
            this->place(1.0);
            const double phase =
                double((elapsed - blinkStart) % BLINK_MS) / BLINK_MS;
            const double wave = std::abs(2.0 * phase - 1.0);
            this->setWindowOpacity(0.12 + 0.88 * wave);
        }
        else if (elapsed < fadeStart + FADE_MS)
        {
            this->setWindowOpacity(
                1.0 - double(elapsed - fadeStart) / FADE_MS);
        }
        else
        {
            this->timer_.stop();
            auto finished = std::move(this->onFinished_);
            this->close();
            if (finished)
            {
                finished();
            }
        }
    }

    QPixmap logo_;
    QRect area_;
    QTimer timer_;
    QElapsedTimer clock_;
    std::function<void()> onFinished_;
};

}  // namespace

QWidget *makeSolrinoTitleLogo()
{
    return new TitleLogo();
}

void playSolrinoIntro(std::function<void()> onFinished)
{
    (new IntroWindow(std::move(onFinished)))->start();
}

}  // namespace chatterino

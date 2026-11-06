#include "widgets/helper/SolrinoTransitions.hpp"
#include "widgets/helper/SolrinoPing.hpp"

#include "Application.hpp"
#include "singletons/Settings.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "singletons/StreamerMode.hpp"
#include "widgets/dialogs/SolrinoTools.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/splits/Split.hpp"

#include <QBuffer>
#include <QElapsedTimer>
#include <QEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace chatterino {

namespace {

constexpr int CARD_WIDTH = 368;
constexpr int MIN_CARD_WIDTH = 240;
constexpr int CARD_HEIGHT = 84;
constexpr int SHADOW = 16;
constexpr int GAP = 8;
constexpr int EDGE = 12;
constexpr int MAX_VISIBLE = 4;
constexpr double RADIUS = 13;

constexpr qint64 SLIDE_IN_MS = 340;
constexpr qint64 DRAIN_MS = 6000;
constexpr qint64 FADE_OUT_MS = 420;
constexpr qint64 FLASH_MS = 900;
constexpr int SLIDE_DISTANCE = 46;
constexpr qint64 PICTURE_FADE_MS = 240;
constexpr int PICTURE_SIZE = 42;
constexpr double CLOSE_SIZE = 20;
constexpr double CLOSE_MARGIN = 8;
constexpr qint64 MAX_PICTURE_BYTES = 5 * 1024 * 1024;

const QColor WHITE(248, 241, 239);
const QColor MUTED(186, 164, 162);
const QColor ACCENT(255, 92, 92);
const QColor RED(227, 16, 28);

double easeOut(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return 1.0 - std::pow(1.0 - t, 3.0);
}

/// The card's size from the "Ping popup size" setting.
double chosenScale()
{
    const auto chosen = getSettings()->solrinoPingSize.getValue();
    if (chosen == QStringLiteral("small"))
    {
        return 0.8;
    }
    if (chosen == QStringLiteral("large"))
    {
        return 1.25;
    }
    if (chosen == QStringLiteral("xlarge"))
    {
        return 1.5;
    }
    return 1.0;
}

double smooth(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

QFont uiFont(double pixels, bool strong)
{
    QFont font(strong ? QStringLiteral("Segoe UI Semibold")
                      : QStringLiteral("Segoe UI"));
    font.setPixelSize(int(pixels));
    return font;
}

/// Square picture cut to a circle at the exact size it's drawn, so it stays sharp.
QPixmap toCircle(const QImage &source, int size, double ratio)
{
    const int pixels = int(std::lround(size * ratio));
    const int side = std::min(source.width(), source.height());
    const auto square =
        source
            .copy((source.width() - side) / 2, (source.height() - side) / 2,
                  side, side)
            .scaled(pixels, pixels, Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);

    QPixmap result(pixels, pixels);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, pixels, pixels));
    painter.fillPath(circle, QColor(24, 10, 12));
    painter.setClipPath(circle);
    painter.drawImage(0, 0, square);
    painter.end();
    result.setDevicePixelRatio(ratio);
    return result;
}

/// Loads each profile picture once per run, however many pings mention it.
class ProfilePictures
{
public:
    using Callback = std::function<void(const QImage &)>;

    static ProfilePictures &instance()
    {
        static auto *pictures = new ProfilePictures();
        return *pictures;
    }

    void get(const QString &login, const Callback &callback)
    {
        const auto cached = this->pictures_.constFind(login);
        if (cached != this->pictures_.constEnd())
        {
            callback(*cached);
            return;
        }

        auto &waiting = this->waiting_[login];
        waiting.push_back(callback);
        if (waiting.size() > 1)
        {
            return;
        }

        getHelix()->getUserByName(
            login,
            [this, login](const HelixUser &user) {
                this->download(login, user.profileImageUrl);
            },
            [this, login] {
                this->finish(login, {});
            });
    }

private:
    void download(const QString &login, const QString &address)
    {
        const QUrl url(address);
        if (!url.isValid() || url.scheme() != QStringLiteral("https") ||
            url.host().isEmpty())
        {
            this->finish(login, {});
            return;
        }

        NetworkRequest(url)
            .timeout(10000)
            .maximumResponseSize(MAX_PICTURE_BYTES)
            .onSuccess([this, login](const NetworkResult &result) {
                auto data = result.getData();
                QBuffer buffer(&data);
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                const auto size = reader.size();
                if (!size.isValid() || size.width() > 2048 ||
                    size.height() > 2048)
                {
                    this->finish(login, {});
                    return;
                }
                this->finish(login, reader.read());
            })
            .onError([this, login](const NetworkResult &) {
                this->finish(login, {});
            })
            .execute();
    }

    void finish(const QString &login, const QImage &image)
    {
        if (!image.isNull())
        {
            this->pictures_.insert(login, image);
        }
        const auto callbacks = this->waiting_.take(login);
        for (const auto &callback : callbacks)
        {
            callback(image);
        }
    }

    QHash<QString, QImage> pictures_;
    QHash<QString, std::vector<Callback>> waiting_;
};

/// The X on a card. It's its own widget because the card lets every click
/// through, and only this small spot should catch one.
class PingCloseButton : public QWidget
{
public:
    PingCloseButton(QWidget *parent, std::function<void()> onClick)
        : QWidget(parent, parent != nullptr
                              ? Qt::Widget
                              : Qt::FramelessWindowHint |
                                    Qt::WindowStaysOnTopHint | Qt::Tool |
                                    Qt::WindowDoesNotAcceptFocus |
                                    Qt::NoDropShadowWindowHint)
        , onClick_(std::move(onClick))
    {
        this->setAttribute(Qt::WA_TranslucentBackground);
        this->setAttribute(Qt::WA_ShowWithoutActivating);
        this->setCursor(Qt::PointingHandCursor);
    }

    void setFade(double fade)
    {
        if (fade != this->fade_)
        {
            this->fade_ = fade;
            this->update();
        }
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(this->fade_);

        const QRectF area(this->rect());
        painter.setPen(Qt::NoPen);
        painter.setBrush(this->hovered_ ? QColor(227, 16, 28, 235)
                                        : QColor(255, 255, 255, 26));
        painter.drawEllipse(area.adjusted(0.5, 0.5, -0.5, -0.5));

        const double arm = area.width() * 0.17;
        const auto center = area.center();
        painter.setPen(QPen(this->hovered_ ? WHITE : QColor(226, 208, 206),
                            std::max(1.4, area.width() * 0.08), Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawLine(center + QPointF(-arm, -arm),
                         center + QPointF(arm, arm));
        painter.drawLine(center + QPointF(arm, -arm),
                         center + QPointF(-arm, arm));
    }

    void enterEvent(QEnterEvent * /*event*/) override
    {
        this->hovered_ = true;
        this->update();
    }

    void leaveEvent(QEvent * /*event*/) override
    {
        this->hovered_ = false;
        this->update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton &&
            this->rect().contains(event->position().toPoint()))
        {
            this->onClick_();
        }
    }

private:
    std::function<void()> onClick_;
    double fade_ = 0;
    bool hovered_ = false;
};

class PingCard : public QWidget
{
public:
    /// With a parent it sits in that chat. Without one it's a screen overlay.
    PingCard(QWidget *parent, QString sender, QString channel, QString message,
             bool isWhisper)
        : QWidget(parent,
                  parent != nullptr
                      ? Qt::Widget
                      : Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                            Qt::Tool | Qt::WindowTransparentForInput |
                            Qt::WindowDoesNotAcceptFocus |
                            Qt::NoDropShadowWindowHint)
        , sender_(std::move(sender))
        , channel_(std::move(channel))
        , message_(std::move(message).simplified())
        , isWhisper_(isWhisper)
        , logo_(QStringLiteral(":/icon.png"))
        , smooth_(solrino::smoothAnimationsEnabled())
        , scale_(chosenScale())
    {
        // A big card still has to fit in a narrow chat
        if (parent != nullptr)
        {
            this->scale_ = std::min(
                this->scale_,
                std::max(1.0, (parent->width() - 24.0) / MIN_CARD_WIDTH));
        }

        this->setFixedSize(this->scaled(CARD_WIDTH + SHADOW * 2),
                           this->scaled(CARD_HEIGHT + SHADOW * 2));
        this->setAttribute(Qt::WA_TranslucentBackground);
        this->setAttribute(Qt::WA_TransparentForMouseEvents);
        this->setAttribute(Qt::WA_ShowWithoutActivating);
        this->setAttribute(Qt::WA_DeleteOnClose);

        this->timer_.setInterval(16);
        QObject::connect(&this->timer_, &QTimer::timeout, this, [this] {
            this->tick();
        });

        this->close_ = new PingCloseButton(parent, [this] {
            this->dismiss();
        });
        const int closeSize = this->scaled(CLOSE_SIZE);
        this->close_->setFixedSize(closeSize, closeSize);
    }

    ~PingCard() override
    {
        // On screen the X is a separate window, so it won't go by itself
        delete this->close_.data();
    }

    /// Fades the logo into this person's profile picture once it's loaded.
    void loadPicture(const QString &login)
    {
        static const QRegularExpression validLogin(
            QStringLiteral("^[a-z0-9_]{1,25}$"));
        const auto wanted = login.toLower();
        if (!validLogin.match(wanted).hasMatch())
        {
            return;
        }

        QPointer<PingCard> self(this);
        ProfilePictures::instance().get(wanted, [self](const QImage &image) {
            if (self && !image.isNull())
            {
                self->picture_ = image;
                self->circle_ = {};
                self->pictureStart_ = -1;
            }
        });
    }

    void start()
    {
        this->clock_.start();
        this->timer_.start();
        this->position_ = QPointF(this->target_);
        this->applyPosition();
        this->show();
        this->raise();
        this->close_->show();
        this->close_->raise();
    }

    /// Size in pixels of something that's `value` big on a normal size card.
    int scaled(double value) const
    {
        return int(std::lround(value * this->scale_));
    }

    double scale() const
    {
        return this->scale_;
    }

    /// Where this card should settle. Cards glide there when others come or go.
    void setTarget(QPoint target)
    {
        this->target_ = target;
    }

    void setCardWidth(int width)
    {
        const int wanted = this->scaled(width + SHADOW * 2);
        if (wanted != this->width())
        {
            this->setFixedWidth(wanted);
            this->cache_ = {};
        }
    }

    bool matches(const QString &sender, const QString &channel) const
    {
        return this->sender_ == sender && this->channel_ == channel &&
               !this->isLeaving();
    }

    /// The same person pinged again: update this card instead of adding another.
    void repeat(const QString &message)
    {
        this->message_ = message.simplified();
        this->count_ += 1;
        this->cache_ = {};
        this->drainStart_ = this->clock_.elapsed();
        this->flashStart_ = this->clock_.elapsed();
        this->update();
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        const auto elapsed = this->clock_.elapsed();
        const double ratio = this->devicePixelRatioF();
        if (this->cache_.isNull() || this->cacheRatio_ != ratio)
        {
            this->renderCard(ratio);
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(this->opacityAt(elapsed));
        painter.drawPixmap(0, 0, this->cache_);

        // Everything below is drawn at normal size and scaled to fit
        painter.scale(this->scale_, this->scale_);
        const auto card = this->cardRect();

        this->drawPicture(painter, card, elapsed, ratio);

        // A red flash around the edge when it arrives
        const double flash =
            1.0 - std::clamp(double(elapsed - this->flashStart_) / FLASH_MS, 0.0,
                             1.0);
        if (flash > 0 && this->smooth_)
        {
            QPainterPath edge;
            edge.addRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), RADIUS,
                                RADIUS);
            auto color = QColor(255, 60, 64);
            color.setAlphaF(0.85 * smooth(flash));
            painter.setPen(QPen(color, 1.6));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(edge);
        }

        this->drawLine(painter, card, elapsed);
    }

private:
    double opacityAt(qint64 elapsed) const
    {
        if (!this->smooth_)
        {
            return 1.0;
        }
        return smooth(double(elapsed) / (SLIDE_IN_MS * 0.7)) *
               (1.0 - smooth(this->leavingProgress(elapsed)));
    }

    /// The card without its shadow, in normal size units.
    QRectF cardRect() const
    {
        return {double(SHADOW), double(SHADOW),
                this->width() / this->scale_ - SHADOW * 2.0,
                double(CARD_HEIGHT)};
    }

    /// Ends the wait, so the card fades out the same way it does on its own.
    void dismiss()
    {
        if (this->clock_.isValid() && !this->isLeaving())
        {
            this->drainStart_ = this->clock_.elapsed() - DRAIN_MS;
        }
    }

    bool isLeaving() const
    {
        return this->clock_.isValid() &&
               this->clock_.elapsed() - this->drainStart_ >= DRAIN_MS;
    }

    double drainProgress(qint64 elapsed) const
    {
        return std::clamp(double(elapsed - this->drainStart_) / DRAIN_MS, 0.0,
                          1.0);
    }

    double leavingProgress(qint64 elapsed) const
    {
        return std::clamp(
            double(elapsed - this->drainStart_ - DRAIN_MS) / FADE_OUT_MS, 0.0,
            1.0);
    }

    void tick()
    {
        const auto elapsed = this->clock_.elapsed();
        if (this->close_ != nullptr)
        {
            this->close_->setFade(this->opacityAt(elapsed));
        }
        if (this->leavingProgress(elapsed) >= 1.0 ||
            (!this->smooth_ && this->isLeaving()))
        {
            this->timer_.stop();
            this->close();
            return;
        }

        // Eased so stacked cards glide rather than jump
        this->position_ +=
            (QPointF(this->target_) - this->position_) *
            (this->smooth_ ? 0.22 : 1.0);
        this->applyPosition();
        this->update();
    }

    void applyPosition()
    {
        const auto elapsed = this->clock_.elapsed();
        const double in = easeOut(double(elapsed) / SLIDE_IN_MS);
        const double out = smooth(this->leavingProgress(elapsed));
        const int offset =
            this->smooth_ ? int((1.0 - in) * SLIDE_DISTANCE +
                                out * (SLIDE_DISTANCE * 0.5))
                          : 0;
        const QPoint next(int(std::lround(this->position_.x())) + offset,
                          int(std::lround(this->position_.y())));
        if (next != this->pos())
        {
            this->move(next);
        }

        if (this->close_ != nullptr)
        {
            const auto card = this->cardRect();
            const QPoint corner(
                this->scaled(card.right() - CLOSE_MARGIN - CLOSE_SIZE),
                this->scaled(card.top() + 12.5));
            this->close_->move(next + corner);
        }
    }

    void drawPicture(QPainter &painter, const QRectF &card, qint64 elapsed,
                     double ratio)
    {
        const QRectF area(card.left() + 14, card.top() + 13, PICTURE_SIZE,
                          PICTURE_SIZE);
        const double cardOpacity = painter.opacity();

        double shown = 0;
        if (!this->picture_.isNull())
        {
            if (this->pictureStart_ < 0)
            {
                this->pictureStart_ = elapsed;
            }
            // Cut at the size it ends up on screen, so it stays sharp
            const double pictureRatio = ratio * this->scale_;
            if (this->circle_.isNull() ||
                this->circle_.devicePixelRatio() != pictureRatio)
            {
                this->circle_ =
                    toCircle(this->picture_, PICTURE_SIZE, pictureRatio);
            }
            shown = this->smooth_
                        ? smooth(double(elapsed - this->pictureStart_) /
                                 PICTURE_FADE_MS)
                        : 1.0;
        }

        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (shown < 1.0)
        {
            painter.setOpacity(cardOpacity * (1.0 - shown));
            painter.drawPixmap(area, this->logo_, QRectF(this->logo_.rect()));
        }
        if (shown > 0.0)
        {
            painter.setOpacity(cardOpacity * shown);
            painter.drawPixmap(area.topLeft(), this->circle_);

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(255, 70, 74, 190), 1.6));
            painter.drawEllipse(area.adjusted(-0.8, -0.8, 0.8, 0.8));
        }
        painter.setOpacity(cardOpacity);
    }

    void drawLine(QPainter &painter, const QRectF &card, qint64 elapsed)
    {
        const double remaining = 1.0 - this->drainProgress(elapsed);
        const double height = 3.5;
        const QRectF track(card.left() + 16, card.bottom() - 11,
                           card.width() - 32, height);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawRoundedRect(track, height / 2, height / 2);
        if (remaining <= 0)
        {
            return;
        }

        QRectF line = track;
        line.setWidth(std::max(height, track.width() * remaining));

        // Soft glow behind the line
        for (int i = 3; i > 0; --i)
        {
            auto glow = QColor(255, 40, 48);
            glow.setAlpha(16);
            painter.setBrush(glow);
            const auto r = line.adjusted(-i * 1.4, -i * 1.4, i * 1.4, i * 1.4);
            painter.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        }

        QLinearGradient color(track.topLeft(), track.topRight());
        color.setColorAt(0, QColor(142, 8, 18));
        color.setColorAt(0.6, RED);
        color.setColorAt(1, QColor(255, 84, 84));
        painter.setBrush(color);
        painter.drawRoundedRect(line, height / 2, height / 2);

        // Bright tip that the line drains toward
        const QPointF tip(line.right() - height / 2, line.center().y());
        painter.setBrush(QColor(255, 220, 215));
        painter.drawEllipse(tip, height * 0.62, height * 0.62);
    }

    /// Everything that doesn't move is drawn once, so the animation stays light.
    void renderCard(double ratio)
    {
        this->cacheRatio_ = ratio;
        this->cache_ = QPixmap(this->size() * ratio);
        this->cache_.setDevicePixelRatio(ratio);
        this->cache_.fill(Qt::transparent);

        QPainter p(&this->cache_);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.scale(this->scale_, this->scale_);

        const auto card = this->cardRect();

        // Soft drop shadow
        p.setPen(Qt::NoPen);
        for (int i = SHADOW; i > 0; i -= 2)
        {
            p.setBrush(QColor(0, 0, 0, 13));
            const auto r = card.adjusted(-i, -i + 3, i, i + 3);
            p.drawRoundedRect(r, RADIUS + i, RADIUS + i);
        }

        QPainterPath path;
        path.addRoundedRect(card, RADIUS, RADIUS);

        QLinearGradient background(card.topLeft(), card.bottomLeft());
        background.setColorAt(0, QColor(33, 15, 17, 250));
        background.setColorAt(1, QColor(15, 9, 10, 250));
        p.fillPath(path, background);

        // Ember glow from behind the logo
        p.save();
        p.setClipPath(path);
        QRadialGradient glow(card.left() + 36, card.top() + 34, 130);
        glow.setColorAt(0, QColor(214, 20, 32, 110));
        glow.setColorAt(1, QColor(214, 20, 32, 0));
        p.fillRect(card, glow);
        p.restore();

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 18), 1));
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), RADIUS, RADIUS);

        // The picture itself is drawn live, so it can fade in once it's loaded
        const double textLeft = card.left() + 14 + PICTURE_SIZE + 13;
        const double textRight = card.right() - 15;

        // Channel tag on the right of the first line
        const auto tagFont = uiFont(11, true);
        const QFontMetricsF tagMetrics(tagFont);
        const QString tagText =
            this->isWhisper_ ? QStringLiteral("whisper") : '#' + this->channel_;
        const double tagRight = card.right() - CLOSE_MARGIN - CLOSE_SIZE - 6;
        const double maxTag = (tagRight - textLeft) * 0.42;
        const auto tag = tagMetrics.elidedText(tagText, Qt::ElideRight, maxTag);
        const double tagWidth = tagMetrics.horizontalAdvance(tag) + 15;
        const QRectF tagRect(tagRight - tagWidth, card.top() + 13, tagWidth, 19);
        p.setPen(QPen(QColor(255, 80, 84, 90), 1));
        p.setBrush(QColor(150, 12, 22, 110));
        p.drawRoundedRect(tagRect, 9.5, 9.5);
        p.setFont(tagFont);
        p.setPen(QColor(255, 196, 194));
        p.drawText(tagRect, Qt::AlignCenter, tag);

        // @sender pinged you
        const auto nameFont = uiFont(15, true);
        const auto actionFont = uiFont(13, false);
        const QFontMetricsF nameMetrics(nameFont);
        const QFontMetricsF actionMetrics(actionFont);
        QString action = this->isWhisper_ ? QStringLiteral(" whispered you")
                                          : QStringLiteral(" pinged you");
        if (this->count_ > 1)
        {
            action += QStringLiteral(" ×%1").arg(this->count_);
        }
        const double room = tagRect.left() - 9 - textLeft;
        const auto name = nameMetrics.elidedText(
            '@' + this->sender_, Qt::ElideRight,
            std::max(40.0, room - actionMetrics.horizontalAdvance(action)));
        const double base = card.top() + 28;
        p.setFont(nameFont);
        p.setPen(ACCENT);
        p.drawText(QPointF(textLeft, base), name);
        p.setFont(actionFont);
        p.setPen(WHITE);
        p.drawText(QPointF(textLeft + nameMetrics.horizontalAdvance(name), base),
                   actionMetrics.elidedText(
                       action, Qt::ElideRight,
                       std::max(0.0, tagRect.left() - 6 - textLeft -
                                         nameMetrics.horizontalAdvance(name))));

        // What they said
        const auto bodyFont = uiFont(12.5, false);
        p.setFont(bodyFont);
        p.setPen(MUTED);
        p.drawText(QPointF(textLeft, card.top() + 50),
                   QFontMetricsF(bodyFont).elidedText(
                       this->message_, Qt::ElideRight, textRight - textLeft));
    }

    QString sender_;
    QString channel_;
    QString message_;
    bool isWhisper_;
    int count_ = 1;
    QPixmap logo_;
    QImage picture_;
    QPixmap circle_;
    qint64 pictureStart_ = -1;
    bool smooth_;
    double scale_;
    QPointer<PingCloseButton> close_;
    QPixmap cache_;
    double cacheRatio_ = 0;
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 drainStart_ = SLIDE_IN_MS;
    qint64 flashStart_ = 0;
    QPoint target_;
    QPointF position_;
};

/// Where the on-screen cards go. Without a chosen spot it's the corner.
struct PingPlace {
    QScreen *screen = nullptr;
    double x = 1.0;
    double y = 1.0;
    bool chosen = false;
};

PingPlace savedPingPlace()
{
    PingPlace place;
    place.screen = QGuiApplication::primaryScreen();
    const auto parts = getSettings()->solrinoPingPlace.getValue().split(';');
    if (parts.size() != 3)
    {
        return place;
    }
    bool okX = false;
    bool okY = false;
    const double x = parts.at(1).toDouble(&okX);
    const double y = parts.at(2).toDouble(&okY);
    if (!okX || !okY)
    {
        return place;
    }
    place.x = std::clamp(x, 0.0, 1.0);
    place.y = std::clamp(y, 0.0, 1.0);
    place.chosen = true;
    // A monitor that's been unplugged gives way to the main one
    for (auto *screen : QGuiApplication::screens())
    {
        if (screen->name() == parts.at(0))
        {
            place.screen = screen;
        }
    }
    return place;
}

/// Keeps cards stacked, and moves chat cards when their chat is resized.
class PingStack : public QObject
{
public:
    static PingStack &instance()
    {
        static auto *stack = new PingStack();
        return *stack;
    }

    /// Returns true if the same person's existing card was updated instead.
    bool repeatInChat(QWidget *chat, const QString &sender,
                      const QString &channel, const QString &message)
    {
        return repeat(this->chatCards_, chat, sender, channel, message);
    }

    bool repeatOnScreen(const QString &sender, const QString &channel,
                        const QString &message)
    {
        return repeat(this->screenCards_, nullptr, sender, channel, message);
    }

    void addToChat(QWidget *chat, PingCard *card)
    {
        chat->removeEventFilter(this);
        chat->installEventFilter(this);
        this->track(this->chatCards_, card);
        this->layoutChat();
    }

    void addToScreen(PingCard *card)
    {
        this->track(this->screenCards_, card);
        this->layoutScreen();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize)
        {
            this->layoutChat();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    using Cards = std::vector<QPointer<PingCard>>;

    static bool repeat(Cards &cards, QWidget *parent, const QString &sender,
                       const QString &channel, const QString &message)
    {
        for (const auto &card : cards)
        {
            if (card && card->parentWidget() == parent &&
                card->matches(sender, channel))
            {
                card->repeat(message);
                return true;
            }
        }
        return false;
    }

    void track(Cards &cards, PingCard *card)
    {
        // A burst of pings shouldn't fill the screen: the oldest makes way
        std::erase_if(cards, [](const auto &c) {
            return c.isNull();
        });
        while (cards.size() >= MAX_VISIBLE)
        {
            if (cards.front())
            {
                cards.front()->close();
            }
            cards.erase(cards.begin());
        }
        cards.emplace_back(card);
        QObject::connect(card, &QObject::destroyed, this, [this] {
            QTimer::singleShot(0, this, [this] {
                this->layoutChat();
                this->layoutScreen();
            });
        });
    }

    void layoutChat()
    {
        std::erase_if(this->chatCards_, [](const auto &c) {
            return c.isNull();
        });
        int y = EDGE;
        // Newest at the top, closest to where you're looking
        for (auto it = this->chatCards_.rbegin(); it != this->chatCards_.rend();
             ++it)
        {
            auto *card = it->data();
            auto *parent = card->parentWidget();
            if (parent == nullptr)
            {
                continue;
            }
            // Fits narrow chats, and leaves room for the scrollbar
            const int width = std::clamp(
                int((parent->width() - 24) / card->scale()), MIN_CARD_WIDTH,
                CARD_WIDTH);
            card->setCardWidth(width);
            card->setTarget({parent->width() - 16 - card->width() +
                                 card->scaled(SHADOW),
                             y - card->scaled(SHADOW)});
            y += card->scaled(CARD_HEIGHT) + GAP;
        }
    }

    void layoutScreen()
    {
        std::erase_if(this->screenCards_, [](const auto &c) {
            return c.isNull();
        });
        const auto place = savedPingPlace();
        if (place.screen == nullptr)
        {
            return;
        }
        const auto area = place.screen->availableGeometry();
        if (place.chosen)
        {
            // The newest goes where it was put. Older ones move away from the
            // nearest edge, so the stack stays on screen.
            const bool upward = place.y >= 0.5;
            bool first = true;
            int y = 0;
            for (auto it = this->screenCards_.rbegin();
                 it != this->screenCards_.rend(); ++it)
            {
                auto *card = it->data();
                const int shadow = card->scaled(SHADOW);
                const int width = card->width() - shadow * 2;
                const int height = card->scaled(CARD_HEIGHT);
                const int x = area.left() +
                              int(std::lround(place.x * (area.width() - width)));
                if (first)
                {
                    y = area.top() +
                        int(std::lround(place.y * (area.height() - height)));
                    first = false;
                }
                card->setTarget({x - shadow, y - shadow});
                y += upward ? -(height + GAP) : height + GAP;
            }
            return;
        }
        int bottom = area.bottom() - EDGE;
        // Newest at the bottom, in the corner
        for (auto it = this->screenCards_.rbegin();
             it != this->screenCards_.rend(); ++it)
        {
            auto *card = it->data();
            const int shadow = card->scaled(SHADOW);
            card->setTarget({area.right() - EDGE + shadow - card->width(),
                             bottom + shadow - card->height()});
            bottom -= card->scaled(CARD_HEIGHT) + GAP;
        }
    }

    Cards chatCards_;
    Cards screenCards_;
};

/// A sample card that can be dragged around, to choose where pings go.
class PingPlacement : public QWidget
{
public:
    PingPlacement()
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                               Qt::Tool | Qt::NoDropShadowWindowHint)
        , logo_(QStringLiteral(":/icon.png"))
        , scale_(chosenScale())
    {
        this->setFixedSize(this->scaled(CARD_WIDTH + SHADOW * 2),
                           this->scaled(CARD_HEIGHT + SHADOW * 2));
        this->setAttribute(Qt::WA_TranslucentBackground);
        this->setAttribute(Qt::WA_DeleteOnClose);
        this->setMouseTracking(true);
        this->setWindowTitle(QStringLiteral("Move ping popup"));
        this->setCursor(Qt::SizeAllCursor);
    }

    void start()
    {
        const auto place = savedPingPlace();
        const auto area = place.screen != nullptr
                              ? place.screen->availableGeometry()
                              : QRect(0, 0, 1280, 720);
        const int shadow = this->scaled(SHADOW);
        const int width = this->width() - shadow * 2;
        const int height = this->scaled(CARD_HEIGHT);
        QPoint spot;
        if (place.chosen)
        {
            spot = {area.left() + int(std::lround(place.x * (area.width() - width))),
                    area.top() +
                        int(std::lround(place.y * (area.height() - height)))};
        }
        else
        {
            spot = {area.right() - EDGE - width, area.bottom() - EDGE - height};
        }
        this->move(spot - QPoint(shadow, shadow));
        this->show();
        this->raise();
        this->activateWindow();
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.scale(this->scale_, this->scale_);

        const QRectF card(SHADOW, SHADOW, this->width() / this->scale_ - SHADOW * 2,
                          CARD_HEIGHT);
        for (int i = SHADOW; i > 0; i -= 2)
        {
            QPainterPath shadow;
            shadow.addRoundedRect(card.adjusted(-i, -i + 3, i, i + 3),
                                  RADIUS + i, RADIUS + i);
            painter.fillPath(shadow, QColor(0, 0, 0, 8));
        }
        QPainterPath shape;
        shape.addRoundedRect(card, RADIUS, RADIUS);
        QLinearGradient background(card.topLeft(), card.bottomLeft());
        background.setColorAt(0, QColor(46, 16, 19, 248));
        background.setColorAt(1, QColor(20, 8, 10, 248));
        painter.fillPath(shape, background);
        // Dashed edge: this is a stand-in, not a real ping
        QPen dashed(ACCENT, 1.4, Qt::DashLine);
        painter.setPen(dashed);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape);

        const QRectF logo(card.left() + 16, card.center().y() - PICTURE_SIZE / 2.0,
                          PICTURE_SIZE, PICTURE_SIZE);
        painter.drawPixmap(logo, this->logo_, QRectF(this->logo_.rect()));

        const double textLeft = logo.right() + 12;
        const double textRight = this->buttonRect(Done).left() - 10;
        painter.setFont(uiFont(14, true));
        painter.setPen(WHITE);
        painter.drawText(QRectF(textLeft, card.top() + 16, textRight - textLeft, 22),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Pings show up here"));
        painter.setFont(uiFont(12, false));
        painter.setPen(MUTED);
        painter.drawText(QRectF(textLeft, card.top() + 40, textRight - textLeft, 34),
                         Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                         QStringLiteral("Drag this anywhere, on any screen."));

        for (const auto which : {Done, Reset})
        {
            const auto box = this->buttonRect(which);
            const bool primary = which == Done;
            const bool hover = this->hovered_ == which;
            QPainterPath button;
            button.addRoundedRect(box, 8, 8);
            painter.fillPath(button,
                             primary ? (hover ? ACCENT : RED)
                                     : QColor(255, 255, 255, hover ? 40 : 18));
            if (!primary)
            {
                painter.setPen(QPen(QColor(255, 255, 255, 60), 1));
                painter.drawPath(button);
            }
            painter.setFont(uiFont(12, true));
            painter.setPen(Qt::white);
            painter.drawText(box, Qt::AlignCenter,
                             primary ? QStringLiteral("Done")
                                     : QStringLiteral("Reset"));
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            return;
        }
        this->pressed_ = this->buttonAt(event->position());
        if (this->pressed_ == None)
        {
            this->dragging_ = true;
            this->dragOffset_ = event->globalPosition().toPoint() - this->pos();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (this->dragging_)
        {
            this->move(event->globalPosition().toPoint() - this->dragOffset_);
            return;
        }
        const auto over = this->buttonAt(event->position());
        if (over != this->hovered_)
        {
            this->hovered_ = over;
            this->setCursor(over == None ? Qt::SizeAllCursor
                                         : Qt::PointingHandCursor);
            this->update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (this->dragging_)
        {
            this->dragging_ = false;
            return;
        }
        const auto released = this->buttonAt(event->position());
        if (released != None && released == this->pressed_)
        {
            if (released == Done)
            {
                this->save();
            }
            else
            {
                getSettings()->solrinoPingPlace = QString();
                getSettings()->requestSave();
            }
            this->close();
        }
        this->pressed_ = None;
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape)
        {
            this->close();
        }
        else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            this->save();
            this->close();
        }
    }

private:
    enum Button { None, Done, Reset };

    int scaled(double value) const
    {
        return int(std::lround(value * this->scale_));
    }

    QRectF buttonRect(Button which) const
    {
        const double right = this->width() / this->scale_ - SHADOW - 12;
        const double top = SHADOW + (which == Done ? 11 : 47);
        return {right - 70, top, 70, 27};
    }

    Button buttonAt(const QPointF &position) const
    {
        const QPointF point = position / this->scale_;
        for (const auto which : {Done, Reset})
        {
            if (this->buttonRect(which).contains(point))
            {
                return which;
            }
        }
        return None;
    }

    /// Kept as a share of the free space, so it's the same spot after the
    /// resolution or taskbar changes.
    void save()
    {
        const int shadow = this->scaled(SHADOW);
        const QRect card(this->pos() + QPoint(shadow, shadow),
                         QSize(this->width() - shadow * 2,
                               this->scaled(CARD_HEIGHT)));
        auto *screen = QGuiApplication::screenAt(card.center());
        if (screen == nullptr)
        {
            screen = this->screen();
        }
        if (screen == nullptr)
        {
            return;
        }
        const auto area = screen->availableGeometry();
        const auto share = [](int offset, int space) {
            return space > 0 ? std::clamp(double(offset) / space, 0.0, 1.0)
                             : 0.0;
        };
        const double x = share(card.left() - area.left(),
                               area.width() - card.width());
        const double y = share(card.top() - area.top(),
                               area.height() - card.height());
        getSettings()->solrinoPingPlace =
            QStringLiteral("%1;%2;%3")
                .arg(screen->name(), QString::number(x, 'f', 4),
                     QString::number(y, 'f', 4));
        getSettings()->requestSave();
    }

    QPixmap logo_;
    double scale_ = 1.0;
    Button hovered_ = None;
    Button pressed_ = None;
    bool dragging_ = false;
    QPoint dragOffset_;
};

}  // namespace

void moveSolrinoPingPopup()
{
    static QPointer<PingPlacement> open;
    if (open != nullptr)
    {
        open->raise();
        open->activateWindow();
        return;
    }
    open = new PingPlacement();
    open->start();
}

void showSolrinoPing(const QString &sender, const QString &channel,
                     const QString &message, bool isWhisper,
                     const QString &pictureLogin)
{
    auto *settings = getSettings();

    if (settings->solrinoPingHideWhileStreaming &&
        getApp()->getStreamerMode()->isEnabled())
    {
        return;
    }

    auto &stack = PingStack::instance();

    if (settings->solrinoPingInChat)
    {
        auto *split = solrino::currentSplit();
        if (split != nullptr && split->isVisible() &&
            !split->window()->isMinimized())
        {
            auto &view = split->getChannelView();
            if (!stack.repeatInChat(&view, sender, channel, message))
            {
                auto *card =
                    new PingCard(&view, sender, channel, message, isWhisper);
                stack.addToChat(&view, card);
                card->loadPicture(pictureLogin);
                card->start();
            }
        }
    }

    if (settings->solrinoPingOnScreen)
    {
        if (!stack.repeatOnScreen(sender, channel, message))
        {
            auto *card =
                new PingCard(nullptr, sender, channel, message, isWhisper);
            stack.addToScreen(card);
            card->loadPicture(pictureLogin);
            card->start();
        }
    }
}

}  // namespace chatterino

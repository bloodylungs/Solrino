#include "widgets/dialogs/SolrinoBadgeGifts.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/moltorino/MoltorinoSupporterBadges.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/SolrinoUpdater.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/dialogs/SolrinoTools.hpp"
#include "widgets/dialogs/VanityDialog.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/Window.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFont>
#include <QGuiApplication>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSet>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace chatterino::solrino {

namespace {

constexpr int FIRST_CHECK_MS = 4000;
constexpr int CHECK_MS = 5000;
constexpr int NEXT_GIFT_MS = 1200;
constexpr int FRAME_MS = 16;
constexpr int FADE_IN_MS = 240;
constexpr int FADE_OUT_MS = 800;
constexpr int STAY_MS = 5000;
constexpr int SHADOW = 14;
constexpr int CARD_WIDTH = 330;
constexpr int CARD_HEIGHT = 84;
constexpr int BADGE_SIZE = 60;
constexpr int TEXT_LEFT = 84;
constexpr int SLIDE = 12;
constexpr int BELOW_WINDOW_TOP = 48;

/// Only Solrino's owner can give badges, so the giver is always them
const QString GIVER = QStringLiteral("scary");

double easeOutBack(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    constexpr double overshoot = 1.9;
    const double p = t - 1;
    return 1 + (overshoot + 1) * p * p * p + overshoot * p * p;
}

double easeOutCubic(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return 1 - std::pow(1 - t, 3.0);
}

/// A small card: the badge, then three lines about it. Clicking it opens the
/// badge in vanity, and it leaves by itself if it's ignored.
class BadgeGiftWindow : public QWidget
{
public:
    BadgeGiftWindow(const MoltorinoSupporterBadge &badge, QWidget *parent,
                    bool givenByOwner = false)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
        , animate_(getSettings()->solrinoSmoothAnimations)
    {
        this->name_ = badge.displayName;
        static const QString prefix = QStringLiteral("Solrino ");
        if (this->name_.startsWith(prefix))
        {
            this->name_.remove(0, prefix.size());
        }
        if (givenByOwner)
        {
            this->heading_ = QStringLiteral("%1 has given you a badge").arg(GIVER);
            this->note_ = QStringLiteral("Click to wear it.");
        }
        else
        {
            this->heading_ = QStringLiteral("You got a new badge");
            this->note_ = QStringLiteral("It's free for everyone on Solrino.");
        }

        this->setAttribute(Qt::WA_TranslucentBackground);
        this->setAttribute(Qt::WA_DeleteOnClose);
        // Never takes the keyboard from whatever someone is typing
        this->setAttribute(Qt::WA_ShowWithoutActivating);
        this->setMouseTracking(true);
        this->setCursor(Qt::PointingHandCursor);
        this->setWindowTitle(this->heading_);
        this->setAccessibleName(
            QStringLiteral("%1: %2").arg(this->heading_, this->name_));
        this->setToolTip(QStringLiteral("Click to wear it"));
        this->setFixedSize(CARD_WIDTH + SHADOW * 2, CARD_HEIGHT + SHADOW * 2);

        this->movie_ = new QMovie(QStringLiteral(":/solrino-badges/%1/3x.webp")
                                      .arg(badge.categoryId),
                                  {}, this);
        this->movie_->start();

        this->timer_.setInterval(FRAME_MS);
        QObject::connect(&this->timer_, &QTimer::timeout, this, [this] {
            this->step();
        });
    }

    void start()
    {
        auto *parent = this->parentWidget();
        const auto area = parent->geometry();
        auto spot = QPoint(area.center().x() - this->width() / 2,
                           area.top() + BELOW_WINDOW_TOP);
        if (auto *screen = parent->screen())
        {
            const auto limits = screen->availableGeometry();
            spot.setX(std::clamp(spot.x(), limits.left(),
                                 std::max(limits.left(),
                                          limits.right() - this->width())));
            spot.setY(std::clamp(spot.y(), limits.top(),
                                 std::max(limits.top(),
                                          limits.bottom() - this->height())));
        }
        this->home_ = spot;
        this->move(this->animate_ ? spot - QPoint(0, SLIDE) : spot);
        this->setWindowOpacity(this->animate_ ? 0.0 : 1.0);
        this->show();
        this->clock_.start();
        this->stayed_.start();
        this->timer_.start();
    }

protected:
    void enterEvent(QEnterEvent * /*event*/) override
    {
        this->hovered_ = true;
        this->update();
    }

    void leaveEvent(QEvent * /*event*/) override
    {
        this->hovered_ = false;
        this->overClose_ = false;
        this->update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const bool over = this->closeRect().contains(event->position());
        if (over != this->overClose_)
        {
            this->overClose_ = over;
            this->update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            return;
        }
        if (this->closeRect().contains(event->position()))
        {
            this->fadeOut();
            return;
        }
        this->openBadges();
    }

    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const QRectF card(SHADOW, SHADOW, CARD_WIDTH, CARD_HEIGHT);
        constexpr double radius = 14;

        for (int i = SHADOW; i > 0; i -= 2)
        {
            QPainterPath shadow;
            shadow.addRoundedRect(card.adjusted(-i, -i + 3, i, i + 3),
                                  radius + i, radius + i);
            painter.fillPath(shadow, QColor(0, 0, 0, 7));
        }

        QPainterPath shape;
        shape.addRoundedRect(card, radius, radius);
        // A plain see-through grey, so the only colour is the badge itself
        painter.fillPath(shape, this->hovered_ ? QColor(58, 58, 62, 232)
                                               : QColor(44, 44, 48, 222));
        painter.setPen(QPen(QColor(255, 255, 255, this->hovered_ ? 70 : 38), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape);

        const double t = this->animate_ ? this->clock_.elapsed() / 1000.0 : 10.0;
        const double pop = this->animate_ ? easeOutBack((t - 0.05) / 0.45) : 1.0;
        const double size = BADGE_SIZE * std::max(0.0, pop);
        const auto pixmap = this->movie_->currentPixmap();
        if (size > 1 && !pixmap.isNull())
        {
            const QPointF center(card.left() + 12 + BADGE_SIZE / 2.0,
                                 card.center().y());
            painter.drawPixmap(QRectF(center.x() - size / 2,
                                      center.y() - size / 2, size, size),
                               pixmap, QRectF(pixmap.rect()));
        }

        const double left = card.left() + TEXT_LEFT;
        const double width = card.width() - TEXT_LEFT - 14;

        auto heading = this->font();
        heading.setPixelSize(12);
        heading.setWeight(QFont::DemiBold);
        painter.setFont(heading);
        painter.setPen(QColor(255, 255, 255, 175));
        painter.drawText(QRectF(left, card.top() + 13, width - 18, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(
                             this->heading_, Qt::ElideRight, int(width - 18)));

        auto title = this->font();
        title.setPixelSize(19);
        title.setWeight(QFont::Bold);
        painter.setFont(title);
        painter.setPen(Qt::white);
        painter.drawText(QRectF(left, card.top() + 30, width, 25),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(
                             this->name_, Qt::ElideRight, int(width)));

        auto body = this->font();
        body.setPixelSize(12);
        painter.setFont(body);
        painter.setPen(QColor(255, 255, 255, 185));
        painter.drawText(QRectF(left, card.top() + 54, width, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(
                             this->note_, Qt::ElideRight, int(width)));

        // The X is only shown when it can be used, to keep the card clean
        if (this->hovered_)
        {
            const auto box = this->closeRect();
            if (this->overClose_)
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 255, 255, 40));
                painter.drawRoundedRect(box, 6, 6);
            }
            QPen cross(QColor(255, 255, 255, this->overClose_ ? 255 : 170), 1.5);
            cross.setCapStyle(Qt::RoundCap);
            painter.setPen(cross);
            const auto inner = box.adjusted(6.5, 6.5, -6.5, -6.5);
            painter.drawLine(inner.topLeft(), inner.bottomRight());
            painter.drawLine(inner.topRight(), inner.bottomLeft());
        }
    }

private:
    QRectF closeRect() const
    {
        return {double(SHADOW + CARD_WIDTH - 28), double(SHADOW + 6), 22.0, 22.0};
    }

    void step()
    {
        const auto elapsed = this->clock_.elapsed();
        if (this->fadeStart_ >= 0)
        {
            const double left =
                1.0 - double(elapsed - this->fadeStart_) / FADE_OUT_MS;
            if (left <= 0 || !this->animate_)
            {
                this->timer_.stop();
                this->close();
                return;
            }
            // Slows into the fade and out of it
            const double eased = std::max(left, 0.0);
            this->setWindowOpacity(eased * eased * (3 - 2 * eased));
        }
        else
        {
            if (this->animate_ && elapsed <= FADE_IN_MS + FRAME_MS)
            {
                const double in = double(elapsed) / FADE_IN_MS;
                this->setWindowOpacity(std::min(1.0, in));
                this->move(this->home_ -
                           QPoint(0, int(SLIDE * (1 - easeOutCubic(in)))));
            }
            // Stays while the mouse is on it
            if (!this->hovered_ && this->stayed_.elapsed() >= STAY_MS)
            {
                this->fadeOut();
            }
        }
        this->update();
    }

    void fadeOut()
    {
        if (this->fadeStart_ < 0)
        {
            this->fadeStart_ = this->clock_.elapsed();
        }
    }

    void openBadges()
    {
        auto *split = currentSplit();
        auto channel = split != nullptr
                           ? std::dynamic_pointer_cast<TwitchChannel>(
                                 split->getSelectedChannel())
                           : nullptr;
        if (channel == nullptr)
        {
            // Vanity needs a channel to open in
            this->note_ = QStringLiteral("Open a chat, then click here.");
            this->update();
            return;
        }
        VanityDialog::showOtherBadges(
            channel, &getApp()->getWindows()->getMainWindow());
        this->fadeOut();
    }

    QString name_;
    QString heading_;
    QString note_;
    bool animate_ = true;
    bool hovered_ = false;
    bool overClose_ = false;
    QPoint home_;
    QMovie *movie_{};
    QTimer timer_;
    QElapsedTimer clock_;
    QElapsedTimer stayed_;
    qint64 fadeStart_ = -1;
};

QPointer<BadgeGiftWindow> openGift;

void checkForGift();

void openGiftWindow(BadgeGiftWindow *window)
{
    openGift = window;
    QObject::connect(openGift, &QObject::destroyed, qApp, [] {
        QTimer::singleShot(NEXT_GIFT_MS, qApp, [] {
            checkForGift();
        });
    });
    openGift->start();
}

/// A badge given by the owner that this account hasn't been told about yet.
/// The first check for an account only remembers what it already has, so old
/// badges don't all pop up at once.
bool showGivenBadge(const QString &userId, MoltorinoSupporterBadges &provider,
                    QWidget *parent)
{
    // Until the list has loaded, an account would look like it has no badges
    if (provider.getBadgeCatalog().empty())
    {
        return false;
    }

    const auto now = QDateTime::currentDateTimeUtc();
    std::vector<MoltorinoSupporterBadge> given;
    QSet<QString> current;
    for (const auto &badge : provider.getAssignedBadges(userId))
    {
        // Badges for everyone have their own popup
        if (badge.everyoneFrom.isValid() && badge.everyoneFrom <= now)
        {
            continue;
        }
        if (!current.contains(badge.categoryId))
        {
            current.insert(badge.categoryId);
            given.push_back(badge);
        }
    }

    const auto prefix = userId + u':';
    const auto checked = prefix + u'*';
    QStringList others;
    QSet<QString> known;
    bool checkedBefore = false;
    for (const auto &entry : getSettings()->solrinoGivenBadgesSeen.getValue())
    {
        if (!entry.startsWith(prefix))
        {
            others.append(entry);
        }
        else if (entry == checked)
        {
            checkedBefore = true;
        }
        else
        {
            known.insert(entry.mid(prefix.size()));
        }
    }

    const MoltorinoSupporterBadge *fresh = nullptr;
    if (checkedBefore)
    {
        for (const auto &badge : given)
        {
            if (!known.contains(badge.categoryId))
            {
                fresh = &badge;
                break;
            }
        }
    }

    // Taken-away badges are forgotten, so giving one back pops up again
    QStringList saved = others;
    saved.append(checked);
    for (const auto &id : current)
    {
        if (known.contains(id) || !checkedBefore ||
            (fresh != nullptr && id == fresh->categoryId))
        {
            saved.append(prefix + id);
        }
    }
    if (saved != getSettings()->solrinoGivenBadgesSeen.getValue())
    {
        getSettings()->solrinoGivenBadgesSeen = saved;
        getSettings()->requestSave();
    }

    if (fresh == nullptr)
    {
        return false;
    }
    openGiftWindow(new BadgeGiftWindow(*fresh, parent, true));
    return true;
}

void showGift(const MoltorinoSupporterBadge &badge, QWidget *parent)
{
    // Counted as soon as it shows, so it can't appear twice even after a crash
    auto seen = getSettings()->solrinoBadgeGiftsSeen.getValue();
    seen.append(badge.categoryId);
    getSettings()->solrinoBadgeGiftsSeen = seen;
    getSettings()->requestSave();

    openGiftWindow(new BadgeGiftWindow(badge, parent));
}

void checkForGift()
{
    if (isAppAboutToQuit() || openGift != nullptr)
    {
        return;
    }
    // An update always comes first
    if (SolrinoUpdater::instance().holdingOtherPopups())
    {
        return;
    }
    const auto account = getApp()->getAccounts()->twitch.getCurrent();
    if (account->isAnon())
    {
        return;
    }
    // Only while someone is really using the app, never over a game
    auto &window = getApp()->getWindows()->getMainWindow();
    if (!window.isVisible() || window.isMinimized() || !window.isActiveWindow())
    {
        return;
    }
    auto *provider = getApp()->getMoltorinoSupporterBadges();
    if (provider == nullptr)
    {
        return;
    }

    // Badges given personally come first
    if (showGivenBadge(account->getUserId(), *provider, &window))
    {
        return;
    }

    const auto seen = getSettings()->solrinoBadgeGiftsSeen.getValue();
    const auto now = QDateTime::currentDateTimeUtc();
    for (const auto &badge : provider->getBadgeCatalog())
    {
        if (badge.everyoneFrom.isValid() && badge.everyoneFrom <= now &&
            !seen.contains(badge.categoryId))
        {
            showGift(badge, &window);
            return;
        }
    }
}

}  // namespace

void startBadgeGifts()
{
    QTimer::singleShot(FIRST_CHECK_MS, qApp, [] {
        checkForGift();
    });
    auto *timer = new QTimer(qApp);
    timer->setInterval(CHECK_MS);
    QObject::connect(timer, &QTimer::timeout, qApp, [] {
        checkForGift();
    });
    timer->start();
}

}  // namespace chatterino::solrino

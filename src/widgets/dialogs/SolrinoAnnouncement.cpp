#include "widgets/dialogs/SolrinoAnnouncement.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/SolrinoVersion.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"
#include "util/PostToThread.hpp"
#include "widgets/helper/SolrinoTransitions.hpp"
#include "widgets/Window.hpp"

#include <QBoxLayout>
#include <QDateTime>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace chatterino::solrino {

namespace {

/// Someone who opens Solrino after this long has missed it
constexpr qint64 SHOW_FOR_SECONDS = 3 * 24 * 60 * 60;
constexpr int SIDE = 14;
constexpr int CLOSE_SIZE = 26;
constexpr int MAX_LINES = 4;

QPointer<AnnouncementBanner> shownBanner;

QFont bannerFont(double pixels, bool strong)
{
    QFont font(strong ? QStringLiteral("Segoe UI Semibold")
                      : QStringLiteral("Segoe UI"));
    font.setPixelSize(int(pixels));
    return font;
}

void showBanner(const QString &id, const QString &message, int attempt)
{
    auto *app = tryGetApp();
    if (app == nullptr || isAppAboutToQuit())
    {
        return;
    }
    auto *container = app->getWindows() != nullptr
                          ? app->getWindows()->getMainWindow().getLayoutContainer()
                          : nullptr;
    auto *layout =
        container != nullptr ? qobject_cast<QBoxLayout *>(container->layout())
                             : nullptr;
    if (layout == nullptr)
    {
        // Solrino is still starting up; try again in a moment
        if (attempt < 20)
        {
            QTimer::singleShot(3000, qApp, [id, message, attempt] {
                showBanner(id, message, attempt + 1);
            });
        }
        return;
    }

    // Counted as soon as it shows, so it never appears twice
    getSettings()->solrinoAnnouncementSeen = id;
    getSettings()->requestSave();

    if (shownBanner)
    {
        shownBanner->deleteLater();
    }
    shownBanner = new AnnouncementBanner(message, container);
    layout->setSpacing(0);
    layout->insertWidget(0, shownBanner);
    shownBanner->show();
    fadeChange(container);
}

}  // namespace

void receiveAnnouncement(const QJsonObject &announcement)
{
    const auto id = announcement.value("id").toString().trimmed();
    const auto message = announcement.value("message").toString().trimmed();
    const auto at = QDateTime::fromString(announcement.value("at").toString(),
                                          Qt::ISODate);
    if (id.isEmpty() || message.isEmpty() || !at.isValid())
    {
        return;
    }
    postToThread([id, message, at] {
        if (getSettings()->solrinoAnnouncementSeen.getValue() == id)
        {
            return;
        }
        if (at.secsTo(QDateTime::currentDateTimeUtc()) > SHOW_FOR_SECONDS)
        {
            return;
        }
        showBanner(id, message.left(300), 0);
    });
}

void sendAnnouncement(const QString &message,
                      std::function<void(bool, const QString &)> done)
{
    const auto account = getApp()->getAccounts()->twitch.getCurrent();
    if (account->isAnon())
    {
        done(false, QStringLiteral("Log in to Twitch first."));
        return;
    }
    const auto text = message.trimmed();
    auto shared = std::make_shared<std::function<void(bool, const QString &)>>(
        std::move(done));
    NetworkRequest(QString::fromLatin1(SOLRINO_BADGE_SERVER) +
                       QStringLiteral("/v2/announcements"),
                   NetworkRequestType::Post)
        .header("Authorization",
                ("OAuth " + account->getOAuthToken()).toUtf8())
        .json(QJsonObject{{"message", text}})
        .timeout(10000)
        .onSuccess([shared, cleared = text.isEmpty()](const NetworkResult &) {
            (*shared)(true, cleared
                                ? QStringLiteral("Announcement taken down.")
                                : QStringLiteral("Sent to everyone on Solrino."));
        })
        .onError([shared](const NetworkResult &result) {
            (*shared)(false, QStringLiteral("That didn't work: ") +
                                 result.parseJson()
                                     .value("error")
                                     .toObject()
                                     .value("message")
                                     .toString(result.formatError()));
        })
        .execute();
}

// ------------------------------------------------------------------ Banner

AnnouncementBanner::AnnouncementBanner(const QString &message, QWidget *parent)
    : QWidget(parent)
    , message_(message)
{
    this->setMouseTracking(true);
    this->setAttribute(Qt::WA_OpaquePaintEvent);
    this->setToolTip(message);
    this->relayout();
}

QRectF AnnouncementBanner::messageRect() const
{
    const double left = 40;
    const double right = this->width() - SIDE - CLOSE_SIZE - 10;
    return {left, 24, std::max(40.0, right - left), this->height() - 32.0};
}

void AnnouncementBanner::relayout()
{
    // As tall as the message needs, up to a few lines
    const QFontMetricsF metrics(bannerFont(13.5, false));
    const double width =
        std::max(120.0, double(this->width()) - 40 - SIDE - CLOSE_SIZE - 10);
    const auto bounds = metrics.boundingRect(QRectF(0, 0, width, 10000),
                                             Qt::TextWordWrap, this->message_);
    const double lines =
        std::clamp(std::ceil(bounds.height() / metrics.lineSpacing()), 1.0,
                   double(MAX_LINES));
    const int height = int(24 + (lines * metrics.lineSpacing()) + 10);
    if (this->height() != height)
    {
        this->setFixedHeight(height);
    }
    this->close_ = QRectF(this->width() - SIDE - CLOSE_SIZE,
                          (height - CLOSE_SIZE) / 2.0, CLOSE_SIZE, CLOSE_SIZE);
}

void AnnouncementBanner::resizeEvent(QResizeEvent * /*event*/)
{
    this->relayout();
}

void AnnouncementBanner::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF area(this->rect());

    QLinearGradient fill(area.topLeft(), area.topRight());
    fill.setColorAt(0, QColor(150, 14, 26));
    fill.setColorAt(0.6, QColor(110, 12, 30));
    fill.setColorAt(1, QColor(70, 14, 40));
    p.fillRect(area, fill);
    p.fillRect(QRectF(0, area.bottom() - 1, area.width(), 1),
               QColor(255, 90, 96, 140));

    // A little megaphone
    const QPointF at(20, 19);
    QPainterPath horn;
    horn.moveTo(at + QPointF(-6, -3));
    horn.lineTo(at + QPointF(4, -8));
    horn.lineTo(at + QPointF(4, 8));
    horn.lineTo(at + QPointF(-6, 3));
    horn.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 222, 140));
    p.drawPath(horn);
    p.drawRoundedRect(QRectF(at.x() - 9, at.y() - 3, 4, 6), 1, 1);
    p.setPen(QPen(QColor(255, 222, 140), 1.4, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(at.x() + 3, at.y() - 6, 8, 12), -50 * 16, 100 * 16);

    p.setFont(bannerFont(11.5, true));
    p.setPen(QColor(255, 200, 196));
    p.drawText(QRectF(40, 6, area.width() - 90, 16),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("ANNOUNCEMENT FROM SCARY"));

    p.setFont(bannerFont(13.5, false));
    p.setPen(QColor(255, 246, 244));
    const auto box = this->messageRect();
    const QFontMetricsF metrics(p.font());
    // Anything past the last line is cut off with "...", and the tooltip has it all
    QString text = this->message_;
    const auto full = metrics.boundingRect(QRectF(0, 0, box.width(), 10000),
                                           Qt::TextWordWrap, text);
    if (full.height() > box.height() + 1)
    {
        while (text.size() > 1 &&
               metrics
                       .boundingRect(QRectF(0, 0, box.width(), 10000),
                                     Qt::TextWordWrap, text + QStringLiteral("..."))
                       .height() > box.height() + 1)
        {
            text.chop(1);
        }
        text = text.trimmed() + QStringLiteral("...");
    }
    p.drawText(box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);

    if (this->overClose_)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawRoundedRect(this->close_, 6, 6);
    }
    p.setPen(QPen(QColor(255, 220, 218), 1.6, Qt::SolidLine, Qt::RoundCap));
    const auto c = this->close_.center();
    p.drawLine(c + QPointF(-4.5, -4.5), c + QPointF(4.5, 4.5));
    p.drawLine(c + QPointF(4.5, -4.5), c + QPointF(-4.5, 4.5));
}

void AnnouncementBanner::mouseMoveEvent(QMouseEvent *event)
{
    const bool over = this->close_.contains(event->position());
    if (over != this->overClose_)
    {
        this->overClose_ = over;
        this->setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
        this->update();
    }
}

void AnnouncementBanner::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton ||
        !this->close_.contains(event->position()))
    {
        return;
    }
    if (auto *parent = this->parentWidget())
    {
        fadeChange(parent);
    }
    this->hide();
    this->deleteLater();
}

void AnnouncementBanner::leaveEvent(QEvent * /*event*/)
{
    this->overClose_ = false;
    this->setCursor(Qt::ArrowCursor);
    this->update();
}

}  // namespace chatterino::solrino

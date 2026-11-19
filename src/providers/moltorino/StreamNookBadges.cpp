#include "providers/moltorino/StreamNookBadges.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSize>
#include <QTimer>
#include <QUrl>

#include <mutex>

namespace chatterino {

namespace {

constexpr auto LIST_URL = "https://streamnook.app/api/v1/cosmetics/badges/users";
constexpr auto CACHE_FILE = "streamnook-badges.json";
constexpr qsizetype MAX_BYTES = 8 * 1024 * 1024;
// StreamNook asks for no more than every 5 minutes; this is gentler
constexpr int REFRESH_MS = 10 * 60 * 1000;
constexpr int FIRST_REQUEST_MS = 5000;
const QSize BASE_SIZE(18, 18);

QString cachePath()
{
    return getApp()->getPaths().cacheFilePath(QString::fromLatin1(CACHE_FILE));
}

/// Pictures only from StreamNook's own sites
bool isTheirImage(const QString &address)
{
    const QUrl url(address);
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           (url.host() == QStringLiteral("streamnook.app") ||
            url.host() == QStringLiteral("cdn.streamnook.app"));
}

ImagePtr imageOf(const QJsonObject &badge, const char *key, qreal scale,
                 int times)
{
    const auto address = badge.value(QLatin1String(key)).toString().trimmed();
    if (!isTheirImage(address))
    {
        return Image::getEmpty();
    }
    // Their pictures can be huge (the same big file for every size), so
    // shrink each to the size it's meant to be
    // Always shown at badge size, whichever copy is used. The picture
    // itself stays full size, so it's sharp when zoomed in.
    (void)scale;
    (void)times;
    return Image::fromAutoscaledUrl(Url{address},
                                    uint16_t(BASE_SIZE.width()));
}

}  // namespace

StreamNookBadges &StreamNookBadges::instance()
{
    static auto *badges = new StreamNookBadges();
    return *badges;
}

void StreamNookBadges::start()
{
    QFile cache(cachePath());
    if (cache.open(QIODevice::ReadOnly))
    {
        this->apply(cache.read(MAX_BYTES + 1));
    }

    QTimer::singleShot(FIRST_REQUEST_MS, this, [this] {
        this->request();
    });
    auto *timer = new QTimer(this);
    timer->setTimerType(Qt::VeryCoarseTimer);
    QObject::connect(timer, &QTimer::timeout, this, [this] {
        this->request();
    });
    timer->start(REFRESH_MS);
    // Turned on later: fetch now rather than at the next refresh
    static pajlada::Signals::SignalHolder holder;
    getSettings()->showBadgesStreamNook.connect(
        [this](bool on, auto) {
            if (on)
            {
                this->request();
            }
        },
        holder, false);
}

std::shared_ptr<const Emote> StreamNookBadges::getBadge(
    const QString &userId) const
{
    std::shared_lock lock(this->mutex_);
    return this->users_.value(userId);
}

void StreamNookBadges::request()
{
    // Nothing to fetch while StreamNook badges are turned off
    if (!getSettings()->showBadgesStreamNook)
    {
        return;
    }
    NetworkRequest(QString::fromLatin1(LIST_URL))
        .header("Accept", "application/json")
        .timeout(20000)
        .maximumResponseSize(MAX_BYTES)
        .caller(this)
        .onSuccess([this](const NetworkResult &result) {
            const auto &data = result.getData();
            if (this->apply(data))
            {
                QSaveFile file(cachePath());
                if (file.open(QIODevice::WriteOnly))
                {
                    file.write(data);
                    file.commit();
                }
            }
        })
        .execute();
}

bool StreamNookBadges::apply(const QByteArray &payload)
{
    if (payload.isEmpty() || payload.size() > MAX_BYTES)
    {
        return false;
    }
    const auto root = QJsonDocument::fromJson(payload).object();
    if (!root.value("badges").isArray() || !root.value("users").isObject())
    {
        return false;
    }

    QHash<QString, std::shared_ptr<const Emote>> catalog;
    for (const auto &value : root.value("badges").toArray())
    {
        const auto badge = value.toObject();
        const auto slug = badge.value("slug").toString().trimmed().toLower();
        auto small = imageOf(badge, "image_url", 1.0, 1);
        if (slug.isEmpty() || slug.size() > 128 || small->isEmpty())
        {
            continue;
        }
        auto name = badge.value("name").toString().trimmed().left(80);
        if (name.isEmpty())
        {
            name = QStringLiteral("StreamNook");
        }
        catalog.insert(
            slug, std::make_shared<const Emote>(Emote{
                      .name = EmoteName{QStringLiteral("streamnook:") + slug},
                      .images = ImageSet{small, imageOf(badge, "image_url_2", 0.5, 2),
                                         imageOf(badge, "image_url_4", 0.25, 4)},
                      .tooltip = Tooltip{name.toHtmlEscaped()},
                      .homePage = Url{QStringLiteral("https://streamnook.app")},
                      .id = EmoteId{QStringLiteral("streamnook:") + slug},
                  }));
    }

    QHash<QString, std::shared_ptr<const Emote>> users;
    const auto people = root.value("users").toObject();
    for (auto it = people.constBegin(); it != people.constEnd(); ++it)
    {
        // The badge they chose to show on StreamNook
        const auto applied = it.value()
                                 .toObject()
                                 .value("applied")
                                 .toString()
                                 .trimmed()
                                 .toLower();
        if (catalog.contains(applied) && it.key().size() <= 20)
        {
            users.insert(it.key(), catalog.value(applied));
        }
    }

    std::unique_lock lock(this->mutex_);
    this->users_ = std::move(users);
    return true;
}

}  // namespace chatterino

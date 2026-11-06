#include "providers/moltorino/MoltorinoOriginalBadges.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "singletons/Paths.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSize>
#include <QTimer>
#include <QUrl>

#include <limits>
#include <mutex>

namespace chatterino {

namespace {

constexpr auto LIST_URL = "https://api.moltorino.com/v2/badges";
constexpr auto CACHE_FILE = "moltorino-original-badges.json";
constexpr qsizetype MAX_BYTES = 8 * 1024 * 1024;
constexpr int REFRESH_MS = 30 * 60 * 1000;
constexpr int FIRST_REQUEST_MS = 4000;
const QSize BASE_SIZE(18, 18);

QString cachePath()
{
    return getApp()->getPaths().cacheFilePath(QString::fromLatin1(CACHE_FILE));
}

bool isTheirImage(const QString &address)
{
    const QUrl url(address);
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           url.host() == QStringLiteral("api.moltorino.com");
}

ImagePtr imageOf(const QJsonObject &images, const char *key, qreal scale,
                 int times)
{
    const auto address = images.value(QLatin1String(key)).toString().trimmed();
    if (!isTheirImage(address))
    {
        return Image::getEmpty();
    }
    return Image::fromUrl(Url{address}, scale, BASE_SIZE * times);
}

struct Catalogued {
    std::shared_ptr<const Emote> emote;
    int priority = std::numeric_limits<int>::max();
};

}  // namespace

MoltorinoOriginalBadges &MoltorinoOriginalBadges::instance()
{
    static auto *badges = new MoltorinoOriginalBadges();
    return *badges;
}

void MoltorinoOriginalBadges::start()
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
}

std::shared_ptr<const Emote> MoltorinoOriginalBadges::getBadge(
    const QString &userId) const
{
    std::shared_lock lock(this->mutex_);
    return this->users_.value(userId);
}

void MoltorinoOriginalBadges::request()
{
    NetworkRequest(QString::fromLatin1(LIST_URL))
        .header("Accept", "application/json")
        .timeout(15000)
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

bool MoltorinoOriginalBadges::apply(const QByteArray &payload)
{
    if (payload.isEmpty() || payload.size() > MAX_BYTES)
    {
        return false;
    }
    const auto root = QJsonDocument::fromJson(payload).object();
    if (root.value("schemaVersion").toInt() != 2 ||
        !root.value("badges").isArray() || !root.value("users").isObject())
    {
        return false;
    }

    QHash<QString, Catalogued> catalog;
    const auto badges = root.value("badges").toArray();
    for (const auto &value : badges)
    {
        const auto badge = value.toObject();
        const auto id = badge.value("id").toString().trimmed().toLower();
        const auto images = badge.value("images").toObject();
        auto small = imageOf(images, "1x", 1.0, 1);
        if (id.isEmpty() || id.size() > 128 || small->isEmpty())
        {
            continue;
        }
        catalog.insert(
            id,
            Catalogued{
                std::make_shared<const Emote>(Emote{
                    // Not "moltorino:", which Solrino's own badges use
                    .name = EmoteName{QStringLiteral("moltorino-original:") + id},
                    .images = ImageSet{small, imageOf(images, "2x", 0.5, 2),
                                       imageOf(images, "3x", 0.25, 4),
                                       imageOf(images, "4x", 0.125, 8)},
                    .tooltip = Tooltip{
                        badge.value("tooltip").toString().trimmed().toHtmlEscaped()},
                    .homePage = Url{},
                    .id = EmoteId{QStringLiteral("moltorino-original:") + id},
                }),
                badge.value("priority").toInt(std::numeric_limits<int>::max()),
            });
    }

    QHash<QString, std::shared_ptr<const Emote>> users;
    const auto people = root.value("users").toObject();
    for (auto it = people.constBegin(); it != people.constEnd(); ++it)
    {
        const auto person = it.value().toObject();
        // Someone who chose "No badge" on Moltorino doesn't show one here either
        if (person.contains("activeBadge"))
        {
            const auto active =
                person.value("activeBadge").toString().trimmed().toLower();
            if (catalog.contains(active))
            {
                users.insert(it.key(), catalog.value(active).emote);
            }
            continue;
        }

        Catalogued best;
        const auto owned = person.value("badges").toArray();
        for (const auto &id : owned)
        {
            const auto found = catalog.constFind(id.toString().trimmed().toLower());
            if (found != catalog.constEnd() &&
                (best.emote == nullptr || found->priority < best.priority))
            {
                best = *found;
            }
        }
        if (best.emote != nullptr)
        {
            users.insert(it.key(), best.emote);
        }
    }

    std::unique_lock lock(this->mutex_);
    this->users_ = std::move(users);
    return true;
}

}  // namespace chatterino

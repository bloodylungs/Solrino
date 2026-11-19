#include "providers/moltorino/SolrinoReactions.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/SolrinoVersion.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"

#include <QJsonArray>
#include <QUrl>

namespace chatterino {

namespace {

constexpr int MAX_MESSAGES = 2000;
constexpr qsizetype MAX_BYTES = 2 * 1024 * 1024;

QString server()
{
    return QString::fromLatin1(SOLRINO_BADGE_SERVER);
}

bool isEmoteSite(const QString &address)
{
    const QUrl url(address);
    static const QStringList hosts{
        QStringLiteral("cdn.7tv.app"),
        QStringLiteral("cdn.betterttv.net"),
        QStringLiteral("cdn.frankerfacez.com"),
        QStringLiteral("static-cdn.jtvnw.net"),
    };
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           hosts.contains(url.host());
}

}  // namespace

bool solrinoReactionsEnabled()
{
    return getSettings()->solrinoReactions;
}

SolrinoReactions &SolrinoReactions::instance()
{
    static auto *reactions = new SolrinoReactions();
    return *reactions;
}

void SolrinoReactions::refresh()
{
    NetworkRequest(server() + QStringLiteral("/v2/reactions"))
        .timeout(15000)
        .maximumResponseSize(MAX_BYTES)
        .onSuccess([](const NetworkResult &result) {
            auto &self = SolrinoReactions::instance();
            const auto messages =
                result.parseJson().value(QStringLiteral("messages")).toArray();
            for (const auto &value : messages)
            {
                const auto message = value.toObject();
                self.store(
                    message.value(QStringLiteral("messageId")).toString(),
                    message.value(QStringLiteral("reactions")).toArray());
            }
            self.changed.invoke();
            getApp()->getWindows()->layoutChannelViews();
        })
        .execute();
}

void SolrinoReactions::apply(const QJsonObject &notice)
{
    this->store(notice.value(QStringLiteral("messageId")).toString(),
                notice.value(QStringLiteral("reactions")).toArray());
    this->changed.invoke();
    getApp()->getWindows()->layoutChannelViews();
}

void SolrinoReactions::store(const QString &messageId,
                             const QJsonArray &reactions)
{
    if (messageId.isEmpty() || messageId.size() > 64)
    {
        return;
    }
    const auto me = getApp()->getAccounts()->twitch.getCurrent()->getUserId();

    std::vector<Reaction> list;
    for (const auto &value : reactions)
    {
        const auto object = value.toObject();
        Reaction reaction;
        reaction.key = object.value(QStringLiteral("key")).toString().left(64);
        reaction.count = object.value(QStringLiteral("count")).toInt();
        if (reaction.key.isEmpty() || reaction.count <= 0)
        {
            continue;
        }
        const auto image = object.value(QStringLiteral("image")).toString();
        if (isEmoteSite(image))
        {
            reaction.image = image;
            reaction.picture = Image::fromUrl(Url{image}, 1, QSize(28, 28));
        }
        for (const auto &user : object.value(QStringLiteral("users")).toArray())
        {
            if (!me.isEmpty() && user.toString() == me)
            {
                reaction.mine = true;
            }
        }
        list.push_back(std::move(reaction));
        if (list.size() >= 20)
        {
            break;
        }
    }

    if (list.empty())
    {
        this->messages_.remove(messageId);
    }
    else
    {
        // Only so many are kept: the oldest messages are long scrolled away
        if (this->messages_.size() >= MAX_MESSAGES &&
            !this->messages_.contains(messageId))
        {
            this->messages_.erase(this->messages_.begin());
        }
        this->messages_.insert(messageId, std::move(list));
    }
    // Kept from growing forever. Numbers only go up, so a message forgotten
    // here just lays itself out once more.
    if (this->versions_.size() >= MAX_MESSAGES * 2)
    {
        this->versions_.clear();
    }
    this->versions_.insert(messageId, this->nextVersion_++);
}

const std::vector<SolrinoReactions::Reaction> *SolrinoReactions::get(
    const QString &messageId) const
{
    const auto found = this->messages_.constFind(messageId);
    return found == this->messages_.constEnd() ? nullptr : &found.value();
}

int SolrinoReactions::version(const QString &messageId) const
{
    return this->versions_.value(messageId, 0);
}

void SolrinoReactions::toggle(const QString &channelId,
                              const QString &messageId, const QString &key,
                              const QString &image)
{
    const auto account = getApp()->getAccounts()->twitch.getCurrent();
    if (account->isAnon() || channelId.isEmpty() || messageId.isEmpty() ||
        key.isEmpty())
    {
        return;
    }
    QJsonObject body{
        {QStringLiteral("channelId"), channelId},
        {QStringLiteral("messageId"), messageId},
        {QStringLiteral("reaction"), key},
    };
    if (isEmoteSite(image))
    {
        body.insert(QStringLiteral("image"), image);
    }
    NetworkRequest(server() + QStringLiteral("/v2/reactions"),
                   NetworkRequestType::Post)
        .header("Authorization", ("OAuth " + account->getOAuthToken()).toUtf8())
        .json(body)
        .timeout(10000)
        .onSuccess([](const NetworkResult &result) {
            // The server also tells everyone, this included, but this shows
            // it straight away
            SolrinoReactions::instance().apply(result.parseJson());
        })
        .execute();
}

}  // namespace chatterino

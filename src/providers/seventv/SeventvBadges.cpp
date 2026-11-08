#include "providers/seventv/SeventvBadges.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/Message.hpp"
#include "messages/MessageElement.hpp"
#include "providers/moltorino/MoltorinoSupporterBadges.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvPaints.hpp"
#include "singletons/Settings.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "util/PostToThread.hpp"
#include "util/Variant.hpp"

#include <QJsonArray>

namespace chatterino {

SeventvBadges::SeventvBadges()
{
    this->fetchTimer_.setSingleShot(true);
    QObject::connect(&this->fetchTimer_, &QTimer::timeout, &this->fetchTimer_,
                     [this] {
                         this->loadPendingBadges();
                     });

    // Several badges often arrive together, so their messages are updated in
    // one go
    this->backfillTimer_.setSingleShot(true);
    this->backfillTimer_.setInterval(250);
    QObject::connect(&this->backfillTimer_, &QTimer::timeout,
                     &this->backfillTimer_, [this] {
                         this->runBackfill();
                     });

    QTimer::singleShot(1500, &this->fetchTimer_, [this] {
        this->prefetchCatalog();
    });
}

void SeventvBadges::prefetchCatalog()
{
    if (this->catalogRequested_)
    {
        return;
    }
    this->catalogRequested_ = true;

    // 7TV has a couple hundred badges, so they all come in one small request
    NetworkRequest("https://7tv.io/v3/gql", NetworkRequestType::Post)
        .caller(&this->fetchTimer_)
        .timeout(20000)
        .maximumResponseSize(4 * 1024 * 1024)
        .json(QJsonObject{{"query", QStringLiteral(R"(
query SolrinoAllBadges {
  cosmetics(list: []) {
    badges {
      id name tooltip
      host { url files { name static_name format width height } }
    }
  }
}
)")}})
        .onSuccess([this](const NetworkResult &result) {
            const auto root = result.parseJson();
            const auto badges = root.value("data")
                                    .toObject()
                                    .value("cosmetics")
                                    .toObject()
                                    .value("badges")
                                    .toArray();
            for (const auto &badge : badges)
            {
                this->registerBadge(badge.toObject(), false);
            }
            this->scheduleBackfill();
        })
        .onError([this](const auto &) {
            // Badges still load one by one, so this only tries again once
            QTimer::singleShot(std::chrono::minutes(2), &this->fetchTimer_,
                               [this] {
                                   this->catalogRequested_ = false;
                                   this->prefetchCatalog();
                               });
        })
        .execute();
}

void SeventvBadges::backfillUsers(
    std::span<const seventv::eventapi::User> users)
{
    QStringList ids;
    for (const auto &user : users)
    {
        if (const auto *twitch = std::get_if<seventv::eventapi::TwitchUser>(&user))
        {
            ids.push_back(twitch->id);
        }
    }
    if (ids.isEmpty())
    {
        return;
    }
    postToThread(
        [this, ids] {
            // A busy chat has a few thousand chatters at most
            if (this->backfillUsers_.size() > 5000)
            {
                this->backfillUsers_.clear();
            }
            for (const auto &id : ids)
            {
                this->backfillUsers_.insert(id);
            }
            this->scheduleBackfill();
        },
        &this->backfillTimer_);
}

void SeventvBadges::scheduleBackfill()
{
    if (!this->backfillUsers_.isEmpty() && !this->backfillTimer_.isActive())
    {
        this->backfillTimer_.start();
    }
}

void SeventvBadges::runBackfill()
{
    auto *app = tryGetApp();
    if (app == nullptr || app->getTwitch() == nullptr)
    {
        return;
    }

    // Users whose badge picture hasn't loaded yet wait for the next run
    QSet<QString> ready;
    for (const auto &userID : std::as_const(this->backfillUsers_))
    {
        if (this->getBadge({userID}))
        {
            ready.insert(userID);
        }
    }
    if (ready.isEmpty())
    {
        return;
    }
    this->backfillUsers_.subtract(ready);

    auto *layouts = app->getMoltorinoSupporterBadges();
    const bool customLayouts = getSettings()->useCustomBadgeLayouts;

    app->getTwitch()->forEachChannel([&](const ChannelPtr &channel) {
        // Only what's on screen or close to it needs updating
        for (const auto &message : channel->getMessageSnapshot(500))
        {
            if (message == nullptr || !ready.contains(message->userID))
            {
                continue;
            }

            const auto badge = this->getBadge({message->userID});
            if (!badge)
            {
                continue;
            }
            if (customLayouts && layouts != nullptr)
            {
                const auto layout = layouts->getVanityLayout(message->userID);
                if (layout && !vanity::detail::isBadgeVisible(*layout, QStringLiteral("7")))
                {
                    continue;
                }
            }

            // Goes after the other badges, just before the name. Homies
            // badges come after 7TV ones, like in a fresh message.
            const auto &elements = message->elements;
            std::optional<size_t> usernameAt;
            std::optional<size_t> homiesAt;
            bool alreadyHasOne = false;
            for (size_t i = 0; i < elements.size(); ++i)
            {
                const auto flags = elements[i]->getFlags();
                if (flags.has(MessageElementFlag::BadgeSevenTV))
                {
                    alreadyHasOne = true;
                    break;
                }
                if (!homiesAt &&
                    flags.hasAny(MessageElementFlag::BadgeHomiesSupporter,
                                 MessageElementFlag::BadgeHomiesCustom))
                {
                    homiesAt = i;
                }
                if (flags.has(MessageElementFlag::Username))
                {
                    usernameAt = i;
                    break;
                }
            }
            if (alreadyHasOne || !usernameAt)
            {
                continue;
            }
            size_t insertAt = homiesAt.value_or(*usernameAt);
            if (!homiesAt)
            {
                // After the last badge, so a pronoun stays beside the name
                for (size_t i = *usernameAt; i > 0; --i)
                {
                    if (elements[i - 1]->getFlags().hasAny(
                            MessageElementFlag::Badges))
                    {
                        insertAt = i;
                        break;
                    }
                }
            }

            auto replacement = message->clone();
            replacement->elements.insert(
                replacement->elements.begin() + static_cast<ptrdiff_t>(insertAt),
                std::make_unique<BadgeElement>(
                    *badge, MessageElementFlag::BadgeSevenTV));
            replacement->externalBadges.push_back((*badge)->name.string);
            channel->replaceMessage(message, replacement);
        }
    });
}

void SeventvBadges::loadBadgeByID(const QString &id)
{
    if (!SeventvPaints::isValidPaintID(id) || this->getBadgeByID(id))
    {
        return;
    }
    postToThread(
        [this, id] {
            if (this->deferredBadges_.contains(id))
            {
                return;
            }
            this->pendingBadges_.insert(id);
            if (!this->loading_ && !this->fetchTimer_.isActive())
            {
                this->fetchTimer_.start(0);
            }
        },
        &this->fetchTimer_);
}

void SeventvBadges::loadPendingBadges()
{
    this->pendingBadges_.removeIf([this](const auto &id) {
        return this->getBadgeByID(id).has_value() || !this->isBadgeAssigned(id);
    });
    if (this->loading_ || this->pendingBadges_.isEmpty())
    {
        return;
    }
    QJsonArray ids;
    for (const auto &id : this->pendingBadges_)
    {
        ids.append(id);
        if (ids.size() == 50)
        {
            break;
        }
    }
    this->loading_ = true;
    NetworkRequest("https://7tv.io/v3/gql", NetworkRequestType::Post)
        .caller(&this->fetchTimer_)
        .timeout(15000)
        .maximumResponseSize(2 * 1024 * 1024)
        .json(QJsonObject{{"query", QStringLiteral(R"(
query MoltorinoBadgesByID($list: [Id!]!) {
  cosmetics(list: $list) {
    badges {
      id name tooltip
      host { url files { name static_name format width height } }
    }
  }
}
)")},
                          {"variables", QJsonObject{{"list", ids}}}})
        .onSuccess([this, ids](const NetworkResult &result) {
            const auto root = result.parseJson();
            const auto cosmetics = root["data"]["cosmetics"];
            if (!root["errors"].toArray().isEmpty() || !cosmetics.isObject())
            {
                this->fetchTimer_.start(std::chrono::seconds(30));
                return;
            }
            for (const auto &badge : cosmetics["badges"].toArray())
            {
                this->registerBadge(badge.toObject(), false);
            }
            this->scheduleBackfill();
            QStringList missing;
            for (const auto &id : ids)
            {
                this->pendingBadges_.remove(id.toString());
                if (!this->getBadgeByID(id.toString()))
                {
                    missing.push_back(id.toString());
                    this->deferredBadges_.insert(id.toString());
                }
            }
            if (!missing.isEmpty())
            {
                QTimer::singleShot(std::chrono::seconds(30), &this->fetchTimer_,
                                   [this, missing] {
                                       for (const auto &id : missing)
                                       {
                                           this->deferredBadges_.remove(id);
                                           if (this->isBadgeAssigned(id))
                                           {
                                               this->loadBadgeByID(id);
                                           }
                                       }
                                   });
            }
        })
        .onError([this](const auto &) {
            this->fetchTimer_.start(std::chrono::seconds(30));
        })
        .finally([this] {
            this->loading_ = false;
            if (!this->pendingBadges_.isEmpty() &&
                !this->fetchTimer_.isActive())
            {
                this->fetchTimer_.start(0);
            }
        })
        .execute();
}

QString SeventvBadges::idForBadge(const QJsonObject &badgeJson) const
{
    return badgeJson["id"].toString();
}

EmotePtr SeventvBadges::createBadge(const QString &id,
                                    const QJsonObject &badgeJson) const
{
    auto emote = Emote{

        .name = EmoteName{u"7tv:" % badgeJson["name"].toString()},
        .images = SeventvEmotes::createImageSet(
            badgeJson, !getSettings()->animateSevenTVBadges),
        .tooltip = Tooltip{badgeJson["tooltip"].toString()},
        .homePage = Url{},
        .id = EmoteId{id},
    };

    if (emote.images.getImage1()->isEmpty())
    {
        return nullptr;
    }

    return std::make_shared<const Emote>(std::move(emote));
}

}

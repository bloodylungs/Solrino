#pragma once

#include "util/BadgeRegistry.hpp"

#include <QJsonObject>
#include <QSet>
#include <QTimer>

#include <memory>
#include <span>

namespace chatterino {

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;

class SeventvBadges : public BadgeRegistry
{
public:
    SeventvBadges();
    void loadBadgeByID(const QString &id);

    /// Loads every 7TV badge once, so a badge can be shown the moment 7TV
    /// says someone has it, without waiting for another download
    void prefetchCatalog();

    /// Adds these users' 7TV badges to messages they already sent, instead
    /// of only showing them from their next message on
    void backfillUsers(std::span<const seventv::eventapi::User> users);

protected:
    QString idForBadge(const QJsonObject &badgeJson) const override;
    EmotePtr createBadge(const QString &id,
                         const QJsonObject &badgeJson) const override;

private:
    void loadPendingBadges();
    void scheduleBackfill();
    void runBackfill();

    QTimer fetchTimer_;
    QSet<QString> pendingBadges_;
    QSet<QString> deferredBadges_;
    bool loading_ = false;
    bool catalogRequested_ = false;

    /// Users whose earlier messages still need their badge. GUI thread only.
    QSet<QString> backfillUsers_;
    QTimer backfillTimer_;
};

}

#pragma once

#include <functional>

class QString;
class QWidget;

namespace chatterino::solrino {

/// When a badge was added compared to the others: higher is newer. A badge this
/// version of the app doesn't know yet counts as the newest.
int badgeAddedRank(const QString &badge);

/// A badge's place on the Solrino Badges tab, most important first. -1 means it
/// belongs on the Other Badges tab instead, as anything added later does.
int mainBadgeRank(const QString &badge);

/// A badge's place at the top of the Other Badges tab, in the order Solrino
/// shows them. -1 for the other badges there, which follow A to Z.
int otherBadgeRank(const QString &badge);

/// Whether the logged in account is Solrino's owner.
bool canGiveBadges();

/// Gives a badge to a Twitch user or takes it away. The callback gets whether it
/// worked and a message saying what happened. The server checks it's the owner.
void changeBadge(const QString &user, const QString &badge, bool giving,
                 std::function<void(bool, const QString &)> done);

/// Pick a badge, type a name, press Enter. Opens on Ctrl+3 for the owner.
void showBadgePanel(QWidget *parent);

}  // namespace chatterino::solrino

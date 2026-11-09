#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

#include <memory>

class QMenu;
class QWidget;

namespace chatterino {

class Channel;
struct Message;
class Split;

namespace solrino {

using ChannelPtr = std::shared_ptr<Channel>;
using MessagePtr = std::shared_ptr<const Message>;

/// The split selected in the window you last used, or nullptr.
Split *currentSplit();

/// Saved messages: kept in saved-messages.json in the settings folder.
void saveMessage(const MessagePtr &message);
void showSavedMessages(QWidget *parent);

/// Quick phrases: saved text sent to the channel you are using.
void showQuickPhrases(Split *split);

/// Shares your Twitch watch streak in the channel, with an optional message.
/// With askForMessage, a small box asks for the message first.
void shareWatchStreak(Split *split, const QString &message = {},
                      bool askForMessage = true);
void shareWatchStreakIn(const ChannelPtr &channel, const QString &message);

/// Messages per minute, unique chatters and top emotes for a channel.
void showChatStats(Split *split);

/// Moderator commands sent during this session, with undo for bans/timeouts.
void recordModCommand(const QStringList &words, const ChannelPtr &channel);
void showModHistory(Split *split);

/// One-click moderator actions. Each preset is "Name = /command {user} ...".
void showModPresets(QWidget *parent);
void addModPresetsToMenu(QMenu *menu, const ChannelPtr &channel,
                         const QString &userName);

/// Hides everything except highlights, staff, first-time chatters and alerts.
bool isFocusViewOn(const Split *split);
void toggleFocusView(Split *split);

/// A custom highlight sound for one channel.
void chooseChannelSound(Split *split);
void clearChannelSound(Split *split);
bool hasChannelSound(const QString &channelName);
QUrl channelSoundFor(const QString &channelName);

/// Search-and-run list of app actions.
void showCommandPalette(Split *split);

/// Adds the "Solrino" submenu to a split's menu.
void addToolsMenu(QMenu *menu, Split *split);

}  // namespace solrino
}  // namespace chatterino

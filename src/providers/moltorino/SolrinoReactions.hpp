#pragma once

#include "messages/Image.hpp"

#include <pajlada/signals/signal.hpp>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace chatterino {

/// Reactions that Solrino users put on chat messages. Only other Solrino users
/// see them: they travel through Solrino's own server, never through Twitch.
class SolrinoReactions
{
public:
    struct Reaction {
        /// The emoji itself, or an emote's name
        QString key;
        /// An emote's picture. Empty for an emoji, which is drawn as text.
        QString image;
        int count = 0;
        /// Whether you're one of the people who reacted this way
        bool mine = false;
        ImagePtr picture;
    };

    static SolrinoReactions &instance();

    /// Loads the reactions of the last day, so a newly opened chat has them
    void refresh();
    /// A change sent by Solrino's server
    void apply(const QJsonObject &notice);

    /// The reactions on one message, or nullptr when it has none
    const std::vector<Reaction> *get(const QString &messageId) const;
    /// Changes whenever that message's reactions do
    int version(const QString &messageId) const;

    /// Adds your reaction, or takes it back if you'd already reacted that way
    void toggle(const QString &channelId, const QString &messageId,
                const QString &key, const QString &image);

    /// Something changed, so chats should draw again
    pajlada::Signals::NoArgSignal changed;

private:
    SolrinoReactions() = default;
    void store(const QString &messageId, const QJsonArray &reactions);

    QHash<QString, std::vector<Reaction>> messages_;
    QHash<QString, int> versions_;
    int nextVersion_ = 1;
};

/// Whether Solrino reactions are shown and can be added
bool solrinoReactionsEnabled();

}  // namespace chatterino

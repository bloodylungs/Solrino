#pragma once

#include "widgets/BaseWidget.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QString>
#include <QStringList>

#include <vector>

class QHBoxLayout;
class QPushButton;

namespace chatterino {

class Split;

namespace solrino {

/// Saved games, most used first. Games used equally keep their saved order.
QStringList mostUsedGames();

/// Counts a game change, so the games used most move to the front.
void noteGameUsed(const QString &name);

/// The command a channel uses to change game. Empty until one has been set for
/// that channel, which means !game.
QString gameCommand(const QString &channel);

/// Saves a channel's command. Other channels keep their own.
void setGameCommand(const QString &channel, const QString &command);

}  // namespace solrino

/// A row of small boxes by the chat box. Clicking one sends the channel's game
/// command, normally "!game <name>", to the split's channel straight away. Only moderators and the broadcaster see
/// it, like the game button, and it's only a chat message: a bot in the channel
/// has to act on it.
class SolrinoQuickGames : public BaseWidget
{
public:
    SolrinoQuickGames(Split *split, QWidget *parent);

    /// Whether this user may use it in the current channel.
    void setAllowed(bool allowed);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void scaleChangedEvent(float newScale) override;
    void themeChangedEvent() override;

private:
    void rebuild();
    void updateTooltips();
    QString messageFor(const QString &name) const;
    void send(QPushButton *box, const QString &name);
    void updateVisibility();
    void hideWhatDoesntFit();
    QString boxStyle(bool sent) const;

    Split *split_;
    bool allowed_ = false;
    QHBoxLayout *layout_{};
    std::vector<QPushButton *> boxes_;
    pajlada::Signals::SignalHolder connections_;
};

}  // namespace chatterino

#pragma once

#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <functional>

class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;

namespace chatterino {

class SolrinoEmoteTile;

/// The "7TV Emotes" tab in Vanity: trending, top and newly uploaded emotes
/// from 7TV. Click one, then Add, and it goes into your channel's 7TV emote
/// set.
class SolrinoEmoteBrowser final : public QWidget
{
public:
    explicit SolrinoEmoteBrowser(QWidget *parent = nullptr);

    /// Gives the 7TV token Vanity is connected with, or empty when it isn't
    std::function<QString()> token;
    /// Sends a message in the chat Vanity was opened from. Returns false when
    /// there's no chat to send to.
    std::function<bool(const QString &)> sendToChat;

    void setUiScale(float scale);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class List { Trending, Top, New };

    struct Emote {
        QString id;
        QString name;
        QString owner;
        bool animated = false;
    };

    void setList(List list);
    void load(bool more);
    void showEmotes(const QVector<Emote> &emotes, bool append);
    void relayout();
    void select(SolrinoEmoteTile *tile);
    void addSelected();
    void setStatus(const QString &text, bool error = false);

    QVector<QPushButton *> listButtons_;
    QLineEdit *search_{};
    QLabel *status_{};
    QScrollArea *scroll_{};
    QWidget *grid_{};
    QGridLayout *gridLayout_{};
    QPushButton *moreButton_{};
    QLabel *pickedName_{};
    QLabel *pickedOwner_{};
    QPushButton *addButton_{};
    /// Asks a chat bot to add it instead, with "#add <emote link>"
    QPushButton *botButton_{};
    QPushButton *replaceButton_{};
    QVector<SolrinoEmoteTile *> tiles_;
    SolrinoEmoteTile *picked_{};
    QTimer animationTimer_;
    QTimer searchDelay_;
    List list_ = List::Trending;
    int page_ = 0;
    int generation_ = 0;
    bool loading_ = false;
    bool loadedOnce_ = false;
    int columns_ = 0;
    float uiScale_ = 1.F;
};

}  // namespace chatterino

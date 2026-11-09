#include "widgets/helper/SolrinoPing.hpp"
#include "widgets/dialogs/SolrinoTools.hpp"
#include "widgets/dialogs/SolrinoWrappedDialog.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/filters/FilterRecord.hpp"
#include "messages/Emote.hpp"
#include "messages/Message.hpp"
#include "messages/MessageElement.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "util/Clipboard.hpp"
#include "widgets/buttons/SvgButton.hpp"
#include "widgets/dialogs/GameSelectorDialog.hpp"
#include "widgets/dialogs/PopupControlMetrics.hpp"
#include "widgets/dialogs/SettingsDialog.hpp"
#include "widgets/DraggablePopup.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitContainer.hpp"
#include "widgets/Window.hpp"

#include <pajlada/signals/signal.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <map>
#include <unordered_set>
#include <vector>

namespace chatterino::solrino {

namespace {

constexpr QSize DEFAULT_POPUP_SIZE(340, 400);
constexpr qint64 RESEND_GUARD_MS = 1000;
constexpr int STATS_WINDOW_MINUTES = 5;
constexpr int MAX_MOD_HISTORY = 500;
const QString FOCUS_FILTER_NAME = QStringLiteral("Solrino focus view");
const QString FOCUS_FILTER_TEXT = QStringLiteral(
    "flags.highlighted || flags.first_message || flags.announcement || "
    "flags.system_message || flags.moderation_action || "
    "author.badges contains \"moderator\" || "
    "author.badges contains \"lead_moderator\" || "
    "author.badges contains \"broadcaster\"");

/// Sends text through the same path as the message box, so commands work.
void sendText(const ChannelPtr &channel, const QString &text)
{
    channel->sendMessage(
        getApp()->getCommands()->execCommand(text, channel, false));
}

/// A resizable list popup with a title and a search box, styled like the rest
/// of the app. Each tool fills it in through populate().
class ToolPopup : public DraggablePopup
{
public:
    ToolPopup(bool closeAutomatically, QWidget *parent, const QString &title,
              const QString &placeholder)
        : DraggablePopup(closeAutomatically, parent)
    {
        this->setAttribute(Qt::WA_DeleteOnClose);
        this->setWindowTitle(title);
        this->enableResize(getSettings()->solrinoToolPopupSize,
                           DEFAULT_POPUP_SIZE);

        auto *container = this->getLayoutContainer();
        container->setObjectName("SolrinoToolRoot");
        auto *layout = new QVBoxLayout(container);

        auto *header = new QHBoxLayout();
        auto *headerText = new QVBoxLayout();
        headerText->setSpacing(0);
        this->title_ = new QLabel(title, container);
        this->title_->setObjectName("SolrinoToolTitle");
        this->subtitle_ = new QLabel(container);
        this->subtitle_->setObjectName("SolrinoToolMuted");
        this->subtitle_->setTextFormat(Qt::PlainText);
        this->subtitle_->hide();
        headerText->addWidget(this->title_);
        headerText->addWidget(this->subtitle_);
        header->addLayout(headerText, 1);

        this->closeButton_ = new SvgButton(
            {
                .dark = ":/buttons/cancel.svg",
                .light = ":/buttons/cancelDark.svg",
            },
            this, QSize{3, 3});
        this->closeButton_->setScaleIndependentSize(18, 18);
        this->closeButton_->setToolTip("Close");
        QObject::connect(this->closeButton_, &Button::leftClicked, this,
                         &QWidget::close);
        header->addWidget(this->closeButton_, 0, Qt::AlignTop);
        layout->addLayout(header);

        this->search_ = new QLineEdit(container);
        this->search_->setObjectName("SolrinoToolSearch");
        this->search_->setPlaceholderText(placeholder);
        this->search_->setClearButtonEnabled(true);
        this->search_->installEventFilter(this);
        layout->addWidget(this->search_);

        this->list_ = new QListWidget(container);
        this->list_->setObjectName("SolrinoToolList");
        this->list_->setMouseTracking(true);
        this->list_->setWordWrap(true);
        this->list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        this->list_->installEventFilter(this);
        layout->addWidget(this->list_, 1);

        this->status_ = new QLabel(container);
        this->status_->setObjectName("SolrinoToolMuted");
        this->status_->setWordWrap(true);
        this->status_->setTextFormat(Qt::PlainText);
        this->status_->hide();
        layout->addWidget(this->status_);

        QObject::connect(this->search_, &QLineEdit::textChanged, this,
                         [this] {
                             this->rebuild();
                         });
        QObject::connect(this->list_, &QListWidget::itemClicked, this,
                         [this](QListWidgetItem *item) {
                             this->activate(item);
                         });
        this->refreshStyle();
    }

    void present(QWidget *anchor)
    {
        this->rebuild();
        QPoint center = QCursor::pos();
        if (anchor != nullptr && anchor->window() != nullptr)
        {
            center = anchor->window()->geometry().center();
        }
        this->show();
        const auto size = this->size();
        this->showAndMoveTo(center - QPoint(size.width() / 2, size.height() / 2),
                            widgets::BoundsChecking::DesiredPosition);
        this->raise();
        this->activateWindow();
        this->search_->setFocus();
    }

protected:
    struct Row {
        std::function<void()> onActivate;
        std::function<void()> onRemove;
    };

    virtual void populate(const QString &query) = 0;

    void addRow(const QString &text, const QString &toolTip,
                std::function<void()> onActivate,
                std::function<void()> onRemove = {})
    {
        auto *item = new QListWidgetItem(text, this->list_);
        item->setToolTip(toolTip);
        item->setData(Qt::UserRole, int(this->rows_.size()));
        if (!onActivate)
        {
            item->setFlags(Qt::ItemIsEnabled);
        }
        this->rows_.push_back({std::move(onActivate), onRemove});

        if (!onRemove)
        {
            return;
        }

        // Only the X takes clicks. The rest of the row passes them to the list.
        auto *row = new QWidget();
        row->setMouseTracking(true);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 2, 0);
        rowLayout->addStretch(1);
        auto *remove = new SvgButton(
            {
                .dark = ":/buttons/cancel.svg",
                .light = ":/buttons/cancelDark.svg",
            },
            nullptr, QSize{4, 4});
        const int size = std::max(14, int(16 * this->scale()));
        remove->setFixedSize(size, size);
        remove->setToolTip("Remove");
        remove->setColor(this->theme->window.text);
        QObject::connect(remove, &Button::leftClicked, this,
                         [this, onRemove = std::move(onRemove)] {
                             // The button is destroyed by the rebuild
                             QTimer::singleShot(0, this, [this, onRemove] {
                                 onRemove();
                                 this->rebuild();
                             });
                         });
        rowLayout->addWidget(remove, 0, Qt::AlignVCenter);
        this->list_->setItemWidget(item, row);
    }

    void rebuild()
    {
        const auto scroll = this->list_->currentRow();
        this->list_->clear();
        this->rows_.clear();
        this->populate(this->search_->text().simplified());
        if (scroll >= 0 && scroll < this->list_->count() &&
            this->list_->hasFocus())
        {
            this->list_->setCurrentRow(scroll);
        }
    }

    void setStatus(const QString &text)
    {
        this->status_->setText(text);
        this->status_->setVisible(!text.isEmpty());
    }

    void setSubtitle(const QString &text)
    {
        this->subtitle_->setText(text);
        this->subtitle_->setVisible(!text.isEmpty());
    }

    QString query() const
    {
        return this->search_->text().simplified();
    }

    void clearQuery()
    {
        this->search_->clear();
    }

    void hideSearch()
    {
        this->search_->hide();
    }

    void themeChangedEvent() override
    {
        if (this->list_ == nullptr)
        {
            return;
        }
        this->refreshStyle();
        this->rebuild();
    }

    void scaleChangedEvent(float scale) override
    {
        DraggablePopup::scaleChangedEvent(scale);
        if (this->list_ == nullptr)
        {
            return;
        }
        this->refreshStyle();
        this->rebuild();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape)
        {
            this->close();
            return;
        }
        DraggablePopup::keyPressEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress)
        {
            const auto key = static_cast<QKeyEvent *>(event)->key();
            const bool isEnter = key == Qt::Key_Return || key == Qt::Key_Enter;
            if (watched == this->search_ && (key == Qt::Key_Down || isEnter))
            {
                if (this->list_->count() > 0)
                {
                    this->list_->setFocus();
                    this->list_->setCurrentRow(0);
                }
                return true;
            }
            if (watched == this->list_)
            {
                if (key == Qt::Key_Up && this->list_->currentRow() <= 0)
                {
                    this->list_->setCurrentRow(-1);
                    this->search_->setFocus();
                    return true;
                }
                if (isEnter)
                {
                    this->activate(this->list_->currentItem());
                    return true;
                }
            }
        }
        return DraggablePopup::eventFilter(watched, event);
    }

private:
    void activate(QListWidgetItem *item)
    {
        if (item == nullptr)
        {
            return;
        }
        const auto index = size_t(item->data(Qt::UserRole).toInt());
        if (index < this->rows_.size() && this->rows_[index].onActivate)
        {
            // Copied, because the action may rebuild the list
            auto action = this->rows_[index].onActivate;
            action();
        }
    }

    void refreshStyle()
    {
        const float scale = this->scale();
        auto *app = getApp();
        const auto uiFont = app->getFonts()->getFont(FontStyle::UiMedium, scale);
        this->title_->setFont(
            app->getFonts()->getFont(FontStyle::UiMediumBold, scale));
        this->subtitle_->setFont(uiFont);
        this->search_->setFont(uiFont);
        this->search_->setFixedHeight(popupControlHeight(uiFont, scale));
        this->list_->setFont(uiFont);
        this->status_->setFont(uiFont);

        const int margin = std::max(3, int(6 * scale));
        auto *layout = this->getLayoutContainer()->layout();
        layout->setContentsMargins(margin, margin, margin, margin);
        layout->setSpacing(std::max(2, int(4 * scale)));

        const auto text = this->theme->window.text;
        auto muted = text;
        muted.setAlpha(160);
        const auto card = this->theme->splits.header.background;
        const auto hover = this->theme->isLightTheme() ? card.darker(104)
                                                       : card.lighter(108);
        this->closeButton_->setColor(text);
        this->setStyleSheet(
            QString(R"(
                QWidget#SolrinoToolRoot { background: %1; }
                QLabel#SolrinoToolTitle { color: %2; }
                QLabel#SolrinoToolMuted { color: %3; }
                QLineEdit#SolrinoToolSearch {
                    background: %4; color: %2; border: 1px solid %5;
                    border-radius: %6px; padding: 0 %7px;
                }
                QListWidget#SolrinoToolList {
                    background: %8; color: %2; border: 1px solid %5;
                    border-radius: %6px; outline: none;
                }
                QListWidget#SolrinoToolList::item { padding: %9px %7px; }
                QListWidget#SolrinoToolList::item:hover,
                QListWidget#SolrinoToolList::item:selected {
                    background: %10; color: %2;
                }
            )")
                .arg(this->theme->window.background.name(), text.name(),
                     muted.name(QColor::HexArgb),
                     this->theme->splits.input.background.name(),
                     this->theme->splits.header.border.name())
                .arg(std::max(1, int(2 * scale)))
                .arg(std::max(4, int(5 * scale)))
                .arg(card.name())
                .arg(std::max(2, int(3 * scale)))
                .arg(hover.name()));
    }

    QLabel *title_{};
    QLabel *subtitle_{};
    SvgButton *closeButton_{};
    QLineEdit *search_{};
    QListWidget *list_{};
    QLabel *status_{};
    std::vector<Row> rows_;
};

// ---------------------------------------------------------------- Saved

QString savedMessagesPath()
{
    return getApp()->getPaths().settingsDirectory + "/saved-messages.json";
}

QJsonArray loadSavedMessages()
{
    QFile file(savedMessagesPath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).array();
}

bool writeSavedMessages(const QJsonArray &messages)
{
    QSaveFile file(savedMessagesPath());
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }
    file.write(QJsonDocument(messages).toJson());
    return file.commit();
}

class SavedMessagesPopup : public ToolPopup
{
public:
    explicit SavedMessagesPopup(QWidget *parent)
        : ToolPopup(false, parent, "Saved messages", "Search saved messages")
    {
    }

protected:
    void populate(const QString &query) override
    {
        const auto messages = loadSavedMessages();
        // Newest first
        for (int i = int(messages.size()) - 1; i >= 0; --i)
        {
            const auto object = messages.at(i).toObject();
            const auto id = object.value("id").toString();
            const auto body = object.value("text").toString();
            const auto line =
                QString("#%1  %2: %3")
                    .arg(object.value("channel").toString(),
                         object.value("user").toString(), body);
            if (!line.contains(query, Qt::CaseInsensitive))
            {
                continue;
            }
            const auto when =
                QDateTime::fromString(object.value("time").toString(),
                                      Qt::ISODate)
                    .toLocalTime()
                    .toString("MMM d, h:mm ap");
            this->addRow(
                line, when + "\nClick to copy this message.",
                [this, body] {
                    crossPlatformCopy(body);
                    this->setStatus("Copied.");
                },
                [this, id] {
                    auto current = loadSavedMessages();
                    for (int j = 0; j < current.size(); ++j)
                    {
                        if (current.at(j).toObject().value("id") == id)
                        {
                            current.removeAt(j);
                            break;
                        }
                    }
                    this->setStatus(writeSavedMessages(current)
                                        ? QString("Removed.")
                                        : QString("Couldn't update the file."));
                });
        }
        if (messages.isEmpty())
        {
            this->setStatus(
                "Nothing saved yet. Right-click a message and choose Save "
                "message.");
        }
    }
};

// ---------------------------------------------------------------- Phrases

/// A popup that sends to whichever channel you last clicked in.
class ChannelFollowingPopup : public ToolPopup
{
public:
    ChannelFollowingPopup(Split *split, const QString &title,
                          const QString &placeholder)
        : ToolPopup(false, split != nullptr ? split->window() : nullptr, title,
                    placeholder)
        , split_(split)
    {
        auto *follow = new QTimer(this);
        follow->setInterval(400);
        QObject::connect(follow, &QTimer::timeout, this, [this] {
            auto *current = currentSplit();
            if (current != nullptr && current != this->split_)
            {
                this->split_ = current;
                this->channelChanged();
                this->rebuild();
            }
        });
        follow->start();
    }

protected:
    ChannelPtr channel() const
    {
        return this->split_ != nullptr ? this->split_->getSelectedChannel()
                                       : nullptr;
    }

    virtual void channelChanged()
    {
        const auto current = this->channel();
        this->setSubtitle(current != nullptr ? "Sends to #" + current->getName()
                                             : QString());
    }

private:
    QPointer<Split> split_;
};

class QuickPhrasesPopup : public ChannelFollowingPopup
{
public:
    explicit QuickPhrasesPopup(Split *split)
        : ChannelFollowingPopup(split, "Quick phrases",
                                "Type a phrase or /command to add it")
    {
        this->channelChanged();
    }

protected:
    void populate(const QString &query) override
    {
        const auto phrases = getSettings()->solrinoQuickPhrases.getValue();
        for (const auto &phrase : phrases)
        {
            if (!phrase.contains(query, Qt::CaseInsensitive))
            {
                continue;
            }
            this->addRow(
                phrase, "Click to send this.",
                [this, phrase] {
                    this->send(phrase);
                },
                [phrase] {
                    auto current = getSettings()->solrinoQuickPhrases.getValue();
                    current.removeAll(phrase);
                    getSettings()->solrinoQuickPhrases = current;
                    getSettings()->requestSave();
                });
        }
        if (!query.isEmpty() && !phrases.contains(query, Qt::CaseInsensitive))
        {
            this->addRow("+  Add \"" + query + "\"",
                         "Save this. It isn't sent until you click it.",
                         [this, query] {
                             auto current =
                                 getSettings()->solrinoQuickPhrases.getValue();
                             current.append(query);
                             getSettings()->solrinoQuickPhrases = current;
                             getSettings()->requestSave();
                             this->clearQuery();
                             this->setStatus("Saved. Click it to send it.");
                         });
        }
        if (phrases.isEmpty() && query.isEmpty())
        {
            this->setStatus("No phrases yet. Type one above to add it.");
        }
    }

private:
    void send(const QString &phrase)
    {
        if (this->sinceLastSend_.isValid() &&
            this->sinceLastSend_.elapsed() < RESEND_GUARD_MS)
        {
            return;
        }
        const auto target = this->channel();
        if (target == nullptr)
        {
            this->setStatus("Click in a channel first.");
            return;
        }
        this->sinceLastSend_.start();
        sendText(target, phrase);
        this->setStatus("Sending to #" + target->getName() + ": " + phrase);
    }

    QElapsedTimer sinceLastSend_;
};

// ---------------------------------------------------------------- Stats

class ChatStatsPopup : public ChannelFollowingPopup
{
public:
    explicit ChatStatsPopup(Split *split)
        : ChannelFollowingPopup(split, "Chat stats", "")
    {
        this->hideSearch();
        this->channelChanged();
        auto *timer = new QTimer(this);
        timer->setInterval(2000);
        QObject::connect(timer, &QTimer::timeout, this, [this] {
            this->rebuild();
        });
        timer->start();
    }

protected:
    void channelChanged() override
    {
        const auto current = this->channel();
        this->setSubtitle(current != nullptr ? "#" + current->getName()
                                             : QString());
    }

    void populate(const QString & /*query*/) override
    {
        const auto current = this->channel();
        if (current == nullptr)
        {
            this->addRow("Click in a channel to see its stats.", {}, {});
            return;
        }

        const auto cutoff = QDateTime::currentDateTime().addSecs(
            -60 * STATS_WINDOW_MINUTES);
        int count = 0;
        std::unordered_set<QString> chatters;
        std::map<QString, int> emotes;
        for (const auto &message : current->getMessageSnapshot())
        {
            if (message->flags.has(MessageFlag::System) ||
                message->loginName.isEmpty() ||
                !message->serverReceivedTime.isValid() ||
                message->serverReceivedTime < cutoff)
            {
                continue;
            }
            ++count;
            chatters.insert(message->loginName);
            for (const auto &element : message->elements)
            {
                if (const auto *emote =
                        dynamic_cast<const EmoteElement *>(element.get());
                    emote != nullptr && emote->getEmote() != nullptr)
                {
                    emotes[emote->getEmote()->name.string] += 1;
                }
            }
        }

        const auto rate = double(count) / STATS_WINDOW_MINUTES;
        this->addRow(QString("Messages per minute: %1").arg(rate, 0, 'f', 1),
                     QString("Average over the last %1 minutes of chat you "
                             "have loaded.")
                         .arg(STATS_WINDOW_MINUTES),
                     {});
        this->addRow(QString("Unique chatters: %1").arg(chatters.size()),
                     QString("In the last %1 minutes.")
                         .arg(STATS_WINDOW_MINUTES),
                     {});
        this->addRow(QString("Messages: %1").arg(count), {}, {});

        std::vector<std::pair<QString, int>> top(emotes.begin(), emotes.end());
        std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) {
            return a.second > b.second;
        });
        if (top.empty())
        {
            this->addRow("Top emotes: none yet", {}, {});
        }
        for (size_t i = 0; i < std::min<size_t>(5, top.size()); ++i)
        {
            this->addRow(QString("Top emote %1: %2 (%3)")
                             .arg(i + 1)
                             .arg(top[i].first)
                             .arg(top[i].second),
                         {}, {});
        }
    }
};

// ---------------------------------------------------------------- Mod history

struct ModAction {
    QDateTime time;
    std::weak_ptr<Channel> channel;
    QString channelName;
    QString command;
    QString undoCommand;
    bool undone = false;
};

std::vector<ModAction> &modHistory()
{
    static std::vector<ModAction> history;
    return history;
}

pajlada::Signals::NoArgSignal &modHistoryChanged()
{
    static pajlada::Signals::NoArgSignal signal;
    return signal;
}

class ModHistoryPopup : public ChannelFollowingPopup
{
public:
    explicit ModHistoryPopup(Split *split)
        : ChannelFollowingPopup(split, "Mod history", "Search your actions")
    {
        this->connections_.managedConnect(modHistoryChanged(), [this] {
            this->rebuild();
        });
    }

protected:
    void channelChanged() override
    {
    }

    void populate(const QString &query) override
    {
        auto &history = modHistory();
        for (int i = int(history.size()) - 1; i >= 0; --i)
        {
            const auto &action = history[size_t(i)];
            const auto line = QString("%1  #%2  %3%4")
                                  .arg(action.time.toString("h:mm ap"),
                                       action.channelName, action.command,
                                       action.undone ? "  (undone)" : "");
            if (!line.contains(query, Qt::CaseInsensitive))
            {
                continue;
            }
            const bool canUndo =
                !action.undoCommand.isEmpty() && !action.undone;
            std::function<void()> onActivate;
            if (canUndo)
            {
                onActivate = [this, i] {
                    this->undo(size_t(i));
                };
            }
            this->addRow(line, canUndo ? "Click to undo." : "", onActivate);
        }
        if (history.empty())
        {
            this->setStatus("No moderator commands sent yet this session.");
        }
    }

private:
    void undo(size_t index)
    {
        auto &history = modHistory();
        if (index >= history.size())
        {
            return;
        }
        auto &action = history[index];
        const auto target = action.channel.lock();
        if (target == nullptr)
        {
            this->setStatus("That channel is no longer open.");
            return;
        }
        const auto undoCommand = action.undoCommand;
        const auto answer = QMessageBox::question(
            this, "Undo",
            QString("Send %1 in #%2?").arg(undoCommand, action.channelName));
        if (answer != QMessageBox::Yes)
        {
            return;
        }
        action.undone = true;
        sendText(target, undoCommand);
    }

    pajlada::Signals::SignalHolder connections_;
};

// ---------------------------------------------------------------- Presets

struct ModPreset {
    QString name;
    QString command;
};

std::optional<ModPreset> parsePreset(const QString &text)
{
    const auto split = text.indexOf(" = ");
    if (split <= 0)
    {
        return std::nullopt;
    }
    ModPreset preset{text.left(split).trimmed(), text.mid(split + 3).trimmed()};
    if (preset.name.isEmpty() || !preset.command.startsWith('/') ||
        !preset.command.contains("{user}"))
    {
        return std::nullopt;
    }
    return preset;
}

class ModPresetsPopup : public ToolPopup
{
public:
    explicit ModPresetsPopup(QWidget *parent)
        : ToolPopup(false, parent, "Mod presets",
                    "Name = /command {user} ...")
    {
        this->setSubtitle("Right-click a chatter's message to use these.");
    }

protected:
    void populate(const QString &query) override
    {
        const auto presets = getSettings()->solrinoModPresets.getValue();
        for (const auto &preset : presets)
        {
            this->addRow(preset, "", {}, [preset] {
                auto current = getSettings()->solrinoModPresets.getValue();
                current.removeAll(preset);
                getSettings()->solrinoModPresets = current;
                getSettings()->requestSave();
            });
        }
        if (query.isEmpty())
        {
            return;
        }
        if (!parsePreset(query).has_value())
        {
            this->setStatus(
                "Use this format: Timeout 5 min = /timeout {user} 300 spam");
            return;
        }
        this->setStatus({});
        if (!presets.contains(query))
        {
            this->addRow("+  Add \"" + query + "\"", "", [this, query] {
                auto current = getSettings()->solrinoModPresets.getValue();
                current.append(query);
                getSettings()->solrinoModPresets = current;
                getSettings()->requestSave();
                this->clearQuery();
            });
        }
    }
};

// ---------------------------------------------------------------- Sounds

QString channelSoundKey(const QString &channelName)
{
    return channelName.toLower() + '\t';
}

Split *focusedOrGiven(Split *split)
{
    return split != nullptr ? split : currentSplit();
}

// ---------------------------------------------------------------- Palette

class CommandPalettePopup : public ToolPopup
{
public:
    explicit CommandPalettePopup(Split *split)
        : ToolPopup(true, split != nullptr ? split->window() : nullptr,
                    "Command palette", "Type what you want to do")
        , split_(split)
    {
    }

protected:
    void populate(const QString &query) override
    {
        const auto add = [&](const QString &name, std::function<void(Split *)> run) {
            if (name.contains(query, Qt::CaseInsensitive))
            {
                this->addRow(name, "", [this, run = std::move(run)] {
                    // Runs after the palette has closed. Actions can open their
                    // own dialogs, and those must not outlive this click.
                    QPointer<Split> target = this->split_;
                    this->close();
                    QTimer::singleShot(0, qApp, [run, target] {
                        run(target.data());
                    });
                });
            }
        };

        add("Open settings", [](Split *) {
            SettingsDialog::showDialog();
        });
        add("Open emote picker", [](Split *s) {
            if (s != nullptr)
            {
                s->openEmotes();
            }
        });
        add("Search all open tabs", [](Split *s) {
            if (s != nullptr)
            {
                s->openSearchAllTabs();
            }
        });
        add("Change game", [](Split *s) {
            if (s == nullptr)
            {
                return;
            }
            const auto channel = std::dynamic_pointer_cast<TwitchChannel>(
                s->getSelectedChannel());
            if (channel != nullptr)
            {
                GameSelectorDialog::showDialog(channel, s);
            }
        });
        add("Quick phrases", &showQuickPhrases);
        add("Saved messages", [](Split *s) {
            showSavedMessages(s);
        });
        add("Chat stats", &showChatStats);
        add("Solrino Wrapped", [](Split *) {
            SolrinoWrappedDialog::showDialog();
        });
        add("Mod history", &showModHistory);
        add("Mod presets", [](Split *s) {
            showModPresets(s);
        });
        add("Toggle focus view", &toggleFocusView);
        add("Test ping popup", [](Split *s) {
            const auto channel = s != nullptr ? s->getSelectedChannel() : nullptr;
            showSolrinoPing("Solrino",
                            channel != nullptr ? channel->getName() : "chat",
                            "This is what a ping looks like.", false,
                            channel != nullptr ? channel->getName() : "");
        });
        add("Move ping popup", [](Split *) {
            moveSolrinoPingPopup();
        });
        add("Set alert sound for this channel", &chooseChannelSound);
    }

private:
    QPointer<Split> split_;
};

}  // namespace

// -------------------------------------------------------------------- Public

Split *currentSplit()
{
    auto *window = getApp()->getWindows()->getLastSelectedWindow();
    if (window == nullptr)
    {
        return nullptr;
    }
    auto *page = window->getNotebook().getSelectedPage();
    return page != nullptr ? page->getSelectedSplit() : nullptr;
}

void saveMessage(const MessagePtr &message)
{
    if (message == nullptr)
    {
        return;
    }
    auto messages = loadSavedMessages();
    messages.append(QJsonObject{
        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"channel", message->channelName},
        {"user", message->displayName.isEmpty() ? message->loginName
                                                : message->displayName},
        {"text", message->messageText.isEmpty() ? message->searchText
                                                : message->messageText},
    });
    if (!writeSavedMessages(messages))
    {
        QMessageBox::warning(nullptr, "Saved messages",
                             "Couldn't save that message.");
    }
}

void showSavedMessages(QWidget *parent)
{
    (new SavedMessagesPopup(parent != nullptr ? parent->window() : nullptr))
        ->present(parent);
}

void showQuickPhrases(Split *split)
{
    (new QuickPhrasesPopup(focusedOrGiven(split)))->present(split);
}

void showChatStats(Split *split)
{
    (new ChatStatsPopup(focusedOrGiven(split)))->present(split);
}

void recordModCommand(const QStringList &words, const ChannelPtr &channel)
{
    if (words.isEmpty() || channel == nullptr)
    {
        return;
    }
    const auto name = words.front().toLower();
    static const QStringList commands{
        "/ban",     "/timeout", "/unban",  "/untimeout", "/delete",
        "/warn",    "/clear",   "/mod",    "/unmod",     "/vip",
        "/unvip",   "/slow",    "/slowoff", "/followers", "/followersoff",
        "/subscribers", "/subscribersoff", "/emoteonly", "/emoteonlyoff",
        "/shieldmode", "/shieldmodeoff",
    };
    if (!commands.contains(name))
    {
        return;
    }

    QString undo;
    if (words.size() >= 2 && (name == "/ban" || name == "/timeout"))
    {
        auto user = words.at(1);
        if (user.startsWith('@'))
        {
            user.remove(0, 1);
        }
        undo = (name == "/ban" ? "/unban " : "/untimeout ") + user;
    }

    auto &history = modHistory();
    history.push_back({QDateTime::currentDateTime(), channel,
                       channel->getName(), words.join(' '), undo});
    if (history.size() > MAX_MOD_HISTORY)
    {
        history.erase(history.begin());
    }
    modHistoryChanged().invoke();
}

void showModHistory(Split *split)
{
    (new ModHistoryPopup(focusedOrGiven(split)))->present(split);
}

void showModPresets(QWidget *parent)
{
    (new ModPresetsPopup(parent != nullptr ? parent->window() : nullptr))
        ->present(parent);
}

void addModPresetsToMenu(QMenu *menu, const ChannelPtr &channel,
                         const QString &userName)
{
    if (channel == nullptr || userName.isEmpty() ||
        !channel->isTwitchChannel() ||
        !(channel->isMod() || channel->isBroadcaster()))
    {
        return;
    }
    const auto presets = getSettings()->solrinoModPresets.getValue();
    if (presets.isEmpty())
    {
        return;
    }
    auto *submenu = menu->addMenu("Mod &presets");
    for (const auto &text : presets)
    {
        const auto preset = parsePreset(text);
        if (!preset.has_value())
        {
            continue;
        }
        auto command = preset->command;
        command.replace("{user}", userName);
        submenu->addAction(preset->name, [channel, command] {
            sendText(channel, command);
        });
    }
}

bool isFocusViewOn(const Split *split)
{
    for (const auto &record : getSettings()->filterRecords)
    {
        if (record->getName() == FOCUS_FILTER_NAME)
        {
            return split->getFilters().contains(record->getId());
        }
    }
    return false;
}

void toggleFocusView(Split *split)
{
    if (split == nullptr)
    {
        return;
    }
    QUuid id;
    for (const auto &record : getSettings()->filterRecords)
    {
        if (record->getName() == FOCUS_FILTER_NAME)
        {
            id = record->getId();
            break;
        }
    }
    if (id.isNull())
    {
        auto record =
            std::make_shared<FilterRecord>(FOCUS_FILTER_NAME, FOCUS_FILTER_TEXT);
        id = record->getId();
        getSettings()->filterRecords.append(record);
    }

    auto filters = split->getFilters();
    if (filters.contains(id))
    {
        filters.removeAll(id);
    }
    else
    {
        filters.append(id);
    }
    split->setFilters(filters);

    // Filters only apply to new messages, so reload what's already on screen
    auto &view = split->getChannelView();
    view.setChannel(view.underlyingChannel());
}

void chooseChannelSound(Split *split)
{
    if (split == nullptr || split->getSelectedChannel() == nullptr)
    {
        return;
    }
    const auto name = split->getSelectedChannel()->getName();
    const auto path = QFileDialog::getOpenFileName(
        split, "Alert sound for #" + name, {},
        "Audio files (*.wav *.mp3 *.flac *.ogg)");
    if (path.isEmpty())
    {
        return;
    }
    clearChannelSound(split);
    auto sounds = getSettings()->solrinoChannelSounds.getValue();
    sounds.append(channelSoundKey(name) + path);
    getSettings()->solrinoChannelSounds = sounds;
    getSettings()->requestSave();
}

void clearChannelSound(Split *split)
{
    if (split == nullptr || split->getSelectedChannel() == nullptr)
    {
        return;
    }
    const auto key = channelSoundKey(split->getSelectedChannel()->getName());
    auto sounds = getSettings()->solrinoChannelSounds.getValue();
    sounds.removeIf([&key](const QString &entry) {
        return entry.startsWith(key);
    });
    getSettings()->solrinoChannelSounds = sounds;
    getSettings()->requestSave();
}

QUrl channelSoundFor(const QString &channelName)
{
    const auto key = channelSoundKey(channelName);
    const auto sounds = getSettings()->solrinoChannelSounds.getValue();
    for (const auto &entry : sounds)
    {
        if (entry.startsWith(key))
        {
            return QUrl::fromLocalFile(entry.mid(key.size()));
        }
    }
    return {};
}

bool hasChannelSound(const QString &channelName)
{
    return !channelSoundFor(channelName).isEmpty();
}

void showCommandPalette(Split *split)
{
    (new CommandPalettePopup(focusedOrGiven(split)))->present(split);
}

void addToolsMenu(QMenu *menu, Split *split)
{
    auto *tools = menu->addMenu("Solrino");
    tools->addAction("Command palette", split, [split] {
        showCommandPalette(split);
    });
    tools->addAction("Quick phrases", split, [split] {
        showQuickPhrases(split);
    });
    tools->addAction("Saved messages", split, [split] {
        showSavedMessages(split);
    });
    tools->addAction("Chat stats", split, [split] {
        showChatStats(split);
    });
    tools->addAction("Solrino Wrapped", split, [] {
        SolrinoWrappedDialog::showDialog();
    });
    tools->addAction("Mod history", split, [split] {
        showModHistory(split);
    });
    tools->addAction("Mod presets", split, [split] {
        showModPresets(split);
    });
    tools->addSeparator();
    auto *focus = tools->addAction("Focus view", split, [split] {
        toggleFocusView(split);
    });
    focus->setCheckable(true);
    focus->setChecked(isFocusViewOn(split));
    focus->setToolTip(
        "Show only highlights, moderators, first-time chatters and alerts.");
    tools->addAction("Set alert sound for this channel...", split, [split] {
        chooseChannelSound(split);
    });
    const auto selected = split->getSelectedChannel();
    if (selected != nullptr && hasChannelSound(selected->getName()))
    {
        tools->addAction("Clear alert sound for this channel", split, [split] {
            clearChannelSound(split);
        });
    }
}

}  // namespace chatterino::solrino

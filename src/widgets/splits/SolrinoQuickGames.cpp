#include "widgets/splits/SolrinoQuickGames.hpp"

#include "providers/twitch/ChannelManagement.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "widgets/splits/Split.hpp"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHash>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace chatterino {

namespace {

constexpr int SENT_MS = 900;
constexpr int MAX_BOX_WIDTH = 150;
const QChar USAGE_SEPARATOR('\t');
const char *GAME_NAME_PROPERTY = "solrinoGameName";

QHash<QString, int> readUsage()
{
    QHash<QString, int> usage;
    const QStringList entries = getSettings()->solrinoGameUsage.getValue();
    for (const auto &entry : entries)
    {
        const auto at = entry.lastIndexOf(USAGE_SEPARATOR);
        if (at > 0)
        {
            usage.insert(entry.left(at), entry.mid(at + 1).toInt());
        }
    }
    return usage;
}

}  // namespace

namespace solrino {

QStringList mostUsedGames()
{
    QStringList games = getSettings()->gameSelectorGames.getValue();
    const auto usage = readUsage();
    std::ranges::stable_sort(games, [&usage](const auto &a, const auto &b) {
        return usage.value(a.toLower()) > usage.value(b.toLower());
    });
    return games;
}

void noteGameUsed(const QString &name)
{
    auto usage = readUsage();
    usage[name.simplified().toLower()] += 1;

    QStringList entries;
    for (auto it = usage.cbegin(); it != usage.cend(); ++it)
    {
        entries.append(it.key() + USAGE_SEPARATOR + QString::number(it.value()));
    }
    getSettings()->solrinoGameUsage = entries;
}

QString gameCommand(const QString &channel)
{
    const auto start = channel.toLower() + USAGE_SEPARATOR;
    const QStringList entries = getSettings()->solrinoGameCommands.getValue();
    for (const auto &entry : entries)
    {
        if (entry.startsWith(start))
        {
            return entry.mid(start.size());
        }
    }
    return {};
}

void setGameCommand(const QString &channel, const QString &command)
{
    const auto start = channel.toLower() + USAGE_SEPARATOR;
    QStringList entries = getSettings()->solrinoGameCommands.getValue();
    entries.removeIf([&start](const auto &entry) {
        return entry.startsWith(start);
    });

    const auto cleaned = command.simplified();
    if (!cleaned.isEmpty())
    {
        entries.append(start + cleaned);
    }
    getSettings()->solrinoGameCommands = entries;
    getSettings()->requestSave();
}

}  // namespace solrino

SolrinoQuickGames::SolrinoQuickGames(Split *split, QWidget *parent)
    : BaseWidget(parent)
    , split_(split)
{
    this->layout_ = new QHBoxLayout(this);
    this->layout_->setContentsMargins(0, 0, 0, 0);

    auto *settings = getSettings();
    const auto rebuild = [this](const auto &, auto) {
        this->rebuild();
    };
    settings->gameSelectorGames.connect(rebuild, this->connections_, false);
    settings->solrinoGameUsage.connect(rebuild, this->connections_, false);
    settings->solrinoGameBar.connect(rebuild, this->connections_, false);
    settings->solrinoGameBarCount.connect(rebuild, this->connections_, false);
    settings->solrinoGameBarAlign.connect(rebuild, this->connections_, false);
    settings->solrinoGameBarSize.connect(rebuild, this->connections_, false);
    settings->solrinoGameCommands.connect(
        [this](const auto &, auto) {
            this->updateTooltips();
        },
        this->connections_, false);
    this->connections_.managedConnect(this->split_->channelChanged, [this] {
        this->updateTooltips();
    });

    this->rebuild();
}

void SolrinoQuickGames::setAllowed(bool allowed)
{
    if (this->allowed_ != allowed)
    {
        this->allowed_ = allowed;
        this->updateVisibility();
    }
}

void SolrinoQuickGames::resizeEvent(QResizeEvent *event)
{
    BaseWidget::resizeEvent(event);
    this->hideWhatDoesntFit();
}

void SolrinoQuickGames::scaleChangedEvent(float /*newScale*/)
{
    this->rebuild();
}

void SolrinoQuickGames::themeChangedEvent()
{
    this->rebuild();
}

QString SolrinoQuickGames::boxStyle(bool sent) const
{
    const auto text = this->theme != nullptr ? this->theme->window.text.name()
                                             : QStringLiteral("#f8f1ef");
    const bool small = getSettings()->solrinoGameBarSize.getValue() != "normal";
    const int fontSize = int((small ? 11 : 13) * this->scale());
    const int padding = int((small ? 7 : 10) * this->scale());
    const int radius = int(5 * this->scale());
    return QStringLiteral(
               "QPushButton { background: %1; color: %2; border: 1px solid %3;"
               " border-radius: %4px; padding: 0 %5px; font-size: %6px; }"
               "QPushButton:hover { background: rgba(214, 28, 40, 0.42);"
               " border-color: rgba(255, 92, 92, 0.85); }"
               "QPushButton:pressed { background: rgba(214, 28, 40, 0.65); }")
        .arg(sent ? QStringLiteral("rgba(214, 28, 40, 0.7)")
                  : QStringLiteral("rgba(160, 20, 30, 0.22)"),
             text,
             sent ? QStringLiteral("rgba(255, 120, 120, 1)")
                  : QStringLiteral("rgba(255, 80, 84, 0.42)"))
        .arg(radius)
        .arg(padding)
        .arg(fontSize);
}

void SolrinoQuickGames::rebuild()
{
    for (auto *box : this->boxes_)
    {
        box->hide();
        box->deleteLater();
    }
    this->boxes_.clear();
    while (this->layout_->count() > 0)
    {
        delete this->layout_->takeAt(0);
    }

    auto *settings = getSettings();
    const float scale = this->scale();
    const bool small = settings->solrinoGameBarSize.getValue() != "normal";
    const int height = int((small ? 20 : 26) * scale);
    const int limit =
        std::clamp(settings->solrinoGameBarCount.getValue().toInt(), 1, 12);
    const auto align = settings->solrinoGameBarAlign.getValue();

    this->layout_->setSpacing(int(4 * scale));
    this->setFixedHeight(height);

    if (align != "left")
    {
        this->layout_->addStretch(1);
    }

    const auto style = this->boxStyle(false);
    const auto games = solrino::mostUsedGames();
    for (const auto &name : games)
    {
        if (int(this->boxes_.size()) >= limit)
        {
            break;
        }
        auto *box = new QPushButton(this);
        const QFontMetrics metrics(box->font());
        box->setText(metrics.elidedText(name, Qt::ElideRight,
                                        int(MAX_BOX_WIDTH * scale)));
        box->setFixedHeight(height);
        box->setFocusPolicy(Qt::NoFocus);
        box->setCursor(Qt::PointingHandCursor);
        box->setStyleSheet(style);
        box->setProperty(GAME_NAME_PROPERTY, name);
        QObject::connect(box, &QPushButton::clicked, this, [this, box, name] {
            this->send(box, name);
        });
        this->layout_->addWidget(box);
        this->boxes_.push_back(box);
    }

    if (align != "right")
    {
        this->layout_->addStretch(1);
    }

    this->updateTooltips();
    this->updateVisibility();
    this->hideWhatDoesntFit();
}

QString SolrinoQuickGames::messageFor(const QString &name) const
{
    const auto channel = this->split_->getSelectedChannel();
    return ChannelManagement::gameCommandMessage(
        name, channel != nullptr ? solrino::gameCommand(channel->getName())
                                 : QString{});
}

void SolrinoQuickGames::updateTooltips()
{
    for (auto *box : this->boxes_)
    {
        box->setToolTip("Sends: " +
                        this->messageFor(
                            box->property(GAME_NAME_PROPERTY).toString()));
    }
}

void SolrinoQuickGames::send(QPushButton *box, const QString &name)
{
    const auto channel = std::dynamic_pointer_cast<TwitchChannel>(
        this->split_->getSelectedChannel());
    const auto message = this->messageFor(name);
    if (channel == nullptr || message.isEmpty() || !box->isEnabled())
    {
        return;
    }

    channel->sendMessage(message);

    // Lights up to show it was sent, and can't be double-clicked meanwhile.
    // Counted afterwards: the rebuild that follows would replace this box.
    box->setEnabled(false);
    box->setStyleSheet(this->boxStyle(true));
    QPointer<QPushButton> sent(box);
    QTimer::singleShot(SENT_MS, this, [this, sent, name] {
        solrino::noteGameUsed(name);
        if (sent)
        {
            sent->setEnabled(true);
            sent->setStyleSheet(this->boxStyle(false));
        }
    });
}

void SolrinoQuickGames::updateVisibility()
{
    this->setVisible(this->allowed_ && getSettings()->solrinoGameBar &&
                     !this->boxes_.empty());
}

void SolrinoQuickGames::hideWhatDoesntFit()
{
    // In a narrow chat, the least used games make way
    const int spacing = this->layout_->spacing();
    int used = 0;
    bool full = false;
    for (auto *box : this->boxes_)
    {
        used += box->sizeHint().width() + spacing;
        full = full || used - spacing > this->width();
        box->setVisible(!full);
    }
}

}  // namespace chatterino

#include "widgets/dialogs/SolrinoEmoteBrowser.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "messages/Image.hpp"

#include <QAbstractButton>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QVBoxLayout>

namespace chatterino {

namespace {

constexpr auto GQL = "https://api.7tv.app/v4/gql";
constexpr int PER_PAGE = 48;

QString gqlError(const QJsonObject &root)
{
    const auto errors = root.value("errors").toArray();
    if (errors.isEmpty())
    {
        return {};
    }
    return errors.first().toObject().value("message").toString(
        QStringLiteral("7TV said no"));
}

QString cleanToken(QString token)
{
    token = token.trimmed();
    while (token.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive))
    {
        token = token.mid(7).trimmed();
    }
    return token;
}

}  // namespace

/// One emote in the grid: its picture and name
class SolrinoEmoteTile final : public QAbstractButton
{
public:
    SolrinoEmoteTile(QString id, QString name, QString owner, bool animated,
                     QWidget *parent)
        : QAbstractButton(parent)
        , id_(std::move(id))
        , name_(std::move(name))
        , owner_(std::move(owner))
        , animated_(animated)
    {
        this->setCheckable(true);
        this->setCursor(Qt::PointingHandCursor);
        this->setFocusPolicy(Qt::StrongFocus);
        this->setAccessibleName(this->name_);
        this->setToolTip(this->owner_.isEmpty()
                             ? this->name_
                             : this->name_ + QStringLiteral("\nby ") +
                                   this->owner_);
        this->image_ = Image::fromUrl(
            Url{QStringLiteral("https://cdn.7tv.app/emote/%1/2x.webp")
                    .arg(this->id_)},
            1, QSize{56, 56});
        this->setFixedSize(84, 88);
    }

    const QString &id() const
    {
        return this->id_;
    }
    const QString &name() const
    {
        return this->name_;
    }
    const QString &owner() const
    {
        return this->owner_;
    }
    bool animated() const
    {
        return this->animated_;
    }

protected:
    void enterEvent(QEnterEvent * /*event*/) override
    {
        this->hovered_ = true;
        this->update();
    }
    void leaveEvent(QEvent * /*event*/) override
    {
        this->hovered_ = false;
        this->update();
    }

    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF box = QRectF(this->rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        QPainterPath shape;
        shape.addRoundedRect(box, 9, 9);
        const QColor fill = this->isChecked()  ? QColor(227, 16, 28, 60)
                            : this->hovered_  ? QColor(255, 255, 255, 22)
                                              : QColor(255, 255, 255, 10);
        painter.fillPath(shape, fill);
        painter.setPen(QPen(this->isChecked() ? QColor(255, 92, 77)
                                              : QColor(255, 255, 255, 30),
                            this->isChecked() ? 1.6 : 1));
        painter.drawPath(shape);

        const QRect picture((this->width() - 48) / 2, 8, 48, 48);
        if (const auto pixmap = this->image_->pixmapOrLoad();
            pixmap && !pixmap->isNull())
        {
            // Keeps wide emotes inside the box, the right way round
            const auto size =
                pixmap->size().scaled(picture.size(), Qt::KeepAspectRatio);
            QRect target(QPoint(0, 0), size);
            target.moveCenter(picture.center());
            painter.drawPixmap(target, *pixmap);
        }

        QFont font = painter.font();
        font.setPixelSize(11);
        painter.setFont(font);
        painter.setPen(QColor(235, 228, 226));
        const QRect text(4, 60, this->width() - 8, 22);
        painter.drawText(text, Qt::AlignCenter,
                         QFontMetrics(font).elidedText(
                             this->name_, Qt::ElideRight, text.width()));
    }

private:
    QString id_;
    QString name_;
    QString owner_;
    bool animated_ = false;
    bool hovered_ = false;
    ImagePtr image_;
};

SolrinoEmoteBrowser::SolrinoEmoteBrowser(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *intro = new QLabel(QStringLiteral(
        "Find 7TV emotes and add them to your channel. Pick one, then Add."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Trending / Top / New, plus a search box
    auto *bar = new QHBoxLayout();
    bar->setSpacing(6);
    const std::pair<List, QString> lists[] = {
        {List::Trending, QStringLiteral("Trending")},
        {List::Top, QStringLiteral("Most popular")},
        {List::New, QStringLiteral("New")},
    };
    for (const auto &[list, label] : lists)
    {
        auto *button = new QPushButton(label);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setChecked(list == this->list_);
        QObject::connect(button, &QPushButton::clicked, this,
                         [this, list = list] {
                             this->setList(list);
                         });
        this->listButtons_.push_back(button);
        bar->addWidget(button);
    }
    this->search_ = new QLineEdit();
    this->search_->setPlaceholderText(QStringLiteral("Search 7TV emotes"));
    this->search_->setClearButtonEnabled(true);
    bar->addWidget(this->search_, 1);
    layout->addLayout(bar);

    this->searchDelay_.setSingleShot(true);
    this->searchDelay_.setInterval(400);
    QObject::connect(this->search_, &QLineEdit::textChanged, this, [this] {
        this->searchDelay_.start();
    });
    QObject::connect(&this->searchDelay_, &QTimer::timeout, this, [this] {
        this->load(false);
    });

    this->status_ = new QLabel();
    this->status_->setWordWrap(true);
    this->status_->hide();
    layout->addWidget(this->status_);

    this->scroll_ = new QScrollArea();
    this->scroll_->setWidgetResizable(true);
    this->scroll_->setFrameShape(QFrame::NoFrame);
    this->grid_ = new QWidget();
    auto *gridOuter = new QVBoxLayout(this->grid_);
    gridOuter->setContentsMargins(0, 0, 0, 0);
    this->gridLayout_ = new QGridLayout();
    this->gridLayout_->setSpacing(6);
    this->gridLayout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    gridOuter->addLayout(this->gridLayout_);
    this->moreButton_ = new QPushButton(QStringLiteral("Load more"));
    this->moreButton_->setCursor(Qt::PointingHandCursor);
    this->moreButton_->hide();
    QObject::connect(this->moreButton_, &QPushButton::clicked, this, [this] {
        this->load(true);
    });
    gridOuter->addWidget(this->moreButton_, 0, Qt::AlignHCenter);
    gridOuter->addStretch(1);
    this->scroll_->setWidget(this->grid_);
    layout->addWidget(this->scroll_, 1);

    // The chosen emote and the Add button
    auto *picked = new QHBoxLayout();
    auto *names = new QVBoxLayout();
    names->setSpacing(0);
    this->pickedName_ = new QLabel(QStringLiteral("No emote picked"));
    QFont bold = this->pickedName_->font();
    bold.setBold(true);
    this->pickedName_->setFont(bold);
    this->pickedOwner_ = new QLabel();
    names->addWidget(this->pickedName_);
    names->addWidget(this->pickedOwner_);
    picked->addLayout(names, 1);
    this->addButton_ = new QPushButton(QStringLiteral("Add to my channel"));
    this->addButton_->setCursor(Qt::PointingHandCursor);
    this->addButton_->setEnabled(false);
    QObject::connect(this->addButton_, &QPushButton::clicked, this, [this] {
        this->addSelected();
    });
    this->botButton_ = new QPushButton(QStringLiteral("Add with bot"));
    this->botButton_->setCursor(Qt::PointingHandCursor);
    this->botButton_->setEnabled(false);
    this->botButton_->setToolTip(QStringLiteral(
        "Sends #add with the emote's 7TV link in this chat, for a bot like "
        "PotatBot to add it"));
    // Both bot buttons send the emote's link, not its name: new emotes aren't
    // searchable by name for a while, and many emotes share a name
    auto sendBot = [this](bool force) {
        if (this->picked_ == nullptr)
        {
            return;
        }
        auto message = QStringLiteral("#add https://7tv.app/emotes/") +
                       this->picked_->id();
        if (force)
        {
            message += QStringLiteral(" -f");
        }
        if (this->sendToChat && this->sendToChat(message))
        {
            this->setStatus(QStringLiteral("Sent \"%1\" in chat.").arg(message));
        }
        else
        {
            this->setStatus(QStringLiteral("Open Vanity from a Twitch chat to "
                                           "send the bot command."),
                            true);
        }
    };
    QObject::connect(this->botButton_, &QPushButton::clicked, this, [sendBot] {
        sendBot(false);
    });
    this->replaceButton_ = new QPushButton(QStringLiteral("Replace with bot"));
    this->replaceButton_->setCursor(Qt::PointingHandCursor);
    this->replaceButton_->setEnabled(false);
    this->replaceButton_->setToolTip(QStringLiteral(
        "Sends #add with the emote's 7TV link and -f, so the bot swaps out an "
        "emote that already has the same name for this one"));
    QObject::connect(this->replaceButton_, &QPushButton::clicked, this,
                     [sendBot] {
                         sendBot(true);
                     });
    auto *buttons = new QVBoxLayout();
    buttons->setSpacing(4);
    buttons->addWidget(this->addButton_);
    buttons->addWidget(this->botButton_);
    buttons->addWidget(this->replaceButton_);
    picked->addLayout(buttons);
    layout->addLayout(picked);

    // Animated emotes move while the tab is open
    this->animationTimer_.setInterval(50);
    QObject::connect(&this->animationTimer_, &QTimer::timeout, this, [this] {
        const auto view = this->scroll_->viewport()->rect();
        for (auto *tile : this->tiles_)
        {
            if (tile->animated() &&
                view.intersects(QRect(
                    tile->mapTo(this->scroll_->viewport(), QPoint(0, 0)),
                    tile->size())))
            {
                tile->update();
            }
        }
    });
    // Pictures arriving after the first paint
    auto *refresh = new QTimer(this);
    refresh->setInterval(600);
    QObject::connect(refresh, &QTimer::timeout, this, [this] {
        if (this->isVisible())
        {
            this->grid_->update();
            for (auto *tile : this->tiles_)
            {
                if (!tile->animated())
                {
                    tile->update();
                }
            }
        }
    });
    refresh->start();
}

void SolrinoEmoteBrowser::setUiScale(float scale)
{
    this->uiScale_ = scale;
}

void SolrinoEmoteBrowser::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    this->animationTimer_.start();
    if (!this->loadedOnce_)
    {
        this->loadedOnce_ = true;
        this->load(false);
    }
}

void SolrinoEmoteBrowser::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    this->animationTimer_.stop();
}

void SolrinoEmoteBrowser::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    this->relayout();
}

void SolrinoEmoteBrowser::setList(List list)
{
    for (int i = 0; i < this->listButtons_.size(); ++i)
    {
        this->listButtons_[i]->setChecked(int(list) == i);
    }
    if (list == this->list_ && this->search_->text().trimmed().isEmpty())
    {
        return;
    }
    this->list_ = list;
    this->load(false);
}

void SolrinoEmoteBrowser::setStatus(const QString &text, bool error)
{
    this->status_->setText(text);
    this->status_->setStyleSheet(error ? QStringLiteral("color: #ff8a80;")
                                       : QString());
    this->status_->setVisible(!text.isEmpty());
}

void SolrinoEmoteBrowser::load(bool more)
{
    if (more && this->loading_)
    {
        return;
    }
    const auto generation = ++this->generation_;
    this->loading_ = true;
    this->page_ = more ? this->page_ + 1 : 1;
    this->moreButton_->setEnabled(false);
    if (!more)
    {
        this->setStatus(QStringLiteral("Loading..."));
    }

    const auto words = this->search_->text().trimmed();
    QString sortBy = QStringLiteral("TRENDING_WEEKLY");
    if (this->list_ == List::Top)
    {
        sortBy = QStringLiteral("TOP_ALL_TIME");
    }
    else if (this->list_ == List::New)
    {
        sortBy = QStringLiteral("UPLOAD_DATE");
    }

    static constexpr auto QUERY = R"(
query SolrinoEmoteBrowser($query: String, $sort: Sort!, $page: Int, $perPage: Int) {
  emotes {
    search(query: $query, sort: $sort, page: $page, perPage: $perPage,
           filters: { nsfw: false }) {
      items { id defaultName flags { animated } owner { mainConnection { platformDisplayName } } }
    }
  }
}
)";
    QJsonObject variables{
        {"sort", QJsonObject{{"sortBy", sortBy}, {"order", "DESCENDING"}}},
        {"page", this->page_},
        {"perPage", PER_PAGE},
    };
    if (!words.isEmpty())
    {
        variables.insert("query", words);
    }

    QPointer<SolrinoEmoteBrowser> self(this);
    NetworkRequest(GQL, NetworkRequestType::Post)
        .header("Accept", "application/json")
        .json(QJsonObject{{"query", QString::fromUtf8(QUERY)},
                          {"variables", variables}})
        .timeout(15000)
        .maximumResponseSize(4 * 1024 * 1024)
        .caller(this)
        .onSuccess([self, generation, more](const NetworkResult &result) {
            if (!self || generation != self->generation_)
            {
                return;
            }
            self->loading_ = false;
            self->moreButton_->setEnabled(true);
            const auto root = result.parseJson();
            if (const auto error = gqlError(root); !error.isEmpty())
            {
                self->setStatus(QStringLiteral("Couldn't load emotes: ") + error,
                                true);
                return;
            }
            const auto items = root.value("data")
                                   .toObject()
                                   .value("emotes")
                                   .toObject()
                                   .value("search")
                                   .toObject()
                                   .value("items")
                                   .toArray();
            QVector<Emote> emotes;
            for (const auto &value : items)
            {
                const auto item = value.toObject();
                Emote emote;
                emote.id = item.value("id").toString();
                emote.name = item.value("defaultName").toString();
                emote.animated = item.value("flags")
                                     .toObject()
                                     .value("animated")
                                     .toBool();
                emote.owner = item.value("owner")
                                  .toObject()
                                  .value("mainConnection")
                                  .toObject()
                                  .value("platformDisplayName")
                                  .toString();
                if (!emote.id.isEmpty() && !emote.name.isEmpty())
                {
                    emotes.push_back(emote);
                }
            }
            self->setStatus(emotes.isEmpty() && !more
                                ? QStringLiteral("No emotes found.")
                                : QString());
            self->showEmotes(emotes, more);
            self->moreButton_->setVisible(emotes.size() >= PER_PAGE);
        })
        .onError([self, generation](const NetworkResult &result) {
            if (!self || generation != self->generation_)
            {
                return;
            }
            self->loading_ = false;
            self->moreButton_->setEnabled(true);
            self->setStatus(QStringLiteral("Couldn't reach 7TV: ") +
                                result.formatError(),
                            true);
        })
        .execute();
}

void SolrinoEmoteBrowser::showEmotes(const QVector<Emote> &emotes, bool append)
{
    if (!append)
    {
        for (auto *tile : this->tiles_)
        {
            tile->deleteLater();
        }
        this->tiles_.clear();
        this->picked_ = nullptr;
        this->pickedName_->setText(QStringLiteral("No emote picked"));
        this->pickedOwner_->clear();
        this->addButton_->setEnabled(false);
        this->botButton_->setEnabled(false);
        this->replaceButton_->setEnabled(false);
        this->scroll_->verticalScrollBar()->setValue(0);
    }
    for (const auto &emote : emotes)
    {
        auto *tile = new SolrinoEmoteTile(emote.id, emote.name, emote.owner,
                                          emote.animated, this->grid_);
        QObject::connect(tile, &QAbstractButton::clicked, this, [this, tile] {
            this->select(tile);
        });
        this->tiles_.push_back(tile);
    }
    this->columns_ = 0;
    this->relayout();
}

void SolrinoEmoteBrowser::relayout()
{
    const int available = this->scroll_->viewport()->width();
    const int columns = std::max(1, (available + 6) / (84 + 6));
    if (columns == this->columns_ &&
        this->gridLayout_->count() == this->tiles_.size())
    {
        return;
    }
    this->columns_ = columns;
    while (this->gridLayout_->count() > 0)
    {
        delete this->gridLayout_->takeAt(0);
    }
    for (int i = 0; i < this->tiles_.size(); ++i)
    {
        this->gridLayout_->addWidget(this->tiles_[i], i / columns, i % columns);
    }
}

void SolrinoEmoteBrowser::select(SolrinoEmoteTile *tile)
{
    for (auto *other : this->tiles_)
    {
        other->setChecked(other == tile);
    }
    this->picked_ = tile;
    this->pickedName_->setText(tile->name());
    this->pickedOwner_->setText(tile->owner().isEmpty()
                                    ? QString()
                                    : QStringLiteral("by ") + tile->owner());
    this->addButton_->setEnabled(true);
    this->addButton_->setText(QStringLiteral("Add to my channel"));
    this->botButton_->setEnabled(true);
    this->replaceButton_->setEnabled(true);
}

void SolrinoEmoteBrowser::addSelected()
{
    if (this->picked_ == nullptr)
    {
        return;
    }
    const auto token = cleanToken(this->token ? this->token() : QString());
    if (token.isEmpty())
    {
        this->setStatus(
            QStringLiteral("Connect your 7TV account first, in the 7TV tab, "
                           "to add emotes to your channel."),
            true);
        return;
    }

    const auto emoteId = this->picked_->id();
    const auto emoteName = this->picked_->name();
    this->addButton_->setEnabled(false);
    this->addButton_->setText(QStringLiteral("Adding..."));

    // First find your channel's emote set, then add the emote to it
    static constexpr auto ME = R"(
query SolrinoMyEmoteSet {
  users { me { style { activeEmoteSetId activeEmoteSet { name } } } }
}
)";
    QPointer<SolrinoEmoteBrowser> self(this);
    const auto failed = [self](const QString &why) {
        if (!self)
        {
            return;
        }
        self->addButton_->setEnabled(self->picked_ != nullptr);
        self->addButton_->setText(QStringLiteral("Add to my channel"));
        self->setStatus(why, true);
    };
    NetworkRequest(GQL, NetworkRequestType::Post)
        .header("Authorization", QStringLiteral("Bearer ") + token)
        .header("Accept", "application/json")
        .json(QJsonObject{{"query", QString::fromUtf8(ME)},
                          {"variables", QJsonObject{}}})
        .timeout(10000)
        .maximumResponseSize(1024 * 1024)
        .caller(this)
        .onSuccess([self, token, emoteId, emoteName,
                    failed](const NetworkResult &result) {
            if (!self)
            {
                return;
            }
            const auto root = result.parseJson();
            if (const auto error = gqlError(root); !error.isEmpty())
            {
                return failed(QStringLiteral("7TV: ") + error);
            }
            const auto style = root.value("data")
                                   .toObject()
                                   .value("users")
                                   .toObject()
                                   .value("me")
                                   .toObject()
                                   .value("style")
                                   .toObject();
            const auto setId = style.value("activeEmoteSetId").toString();
            const auto setName =
                style.value("activeEmoteSet").toObject().value("name").toString();
            if (setId.isEmpty())
            {
                return failed(QStringLiteral(
                    "Your 7TV account has no channel emote set yet. Make one "
                    "on 7tv.app first."));
            }

            static constexpr auto ADD = R"(
mutation SolrinoAddEmote($set: Id!, $emote: Id!) {
  emoteSets { emoteSet(id: $set) { addEmote(id: { emoteId: $emote }) { id } } }
}
)";
            NetworkRequest(GQL, NetworkRequestType::Post)
                .header("Authorization", QStringLiteral("Bearer ") + token)
                .header("Accept", "application/json")
                .json(QJsonObject{
                    {"query", QString::fromUtf8(ADD)},
                    {"variables",
                     QJsonObject{{"set", setId}, {"emote", emoteId}}}})
                .timeout(10000)
                .maximumResponseSize(1024 * 1024)
                .caller(self.data())
                .onSuccess([self, emoteName, setName,
                            failed](const NetworkResult &added) {
                    if (!self)
                    {
                        return;
                    }
                    const auto addRoot = added.parseJson();
                    if (const auto error = gqlError(addRoot); !error.isEmpty())
                    {
                        return failed(QStringLiteral("7TV: ") + error);
                    }
                    self->addButton_->setText(QStringLiteral("Added"));
                    self->setStatus(
                        QStringLiteral("Added %1 to %2.")
                            .arg(emoteName,
                                 setName.isEmpty()
                                     ? QStringLiteral("your channel")
                                     : setName));
                })
                .onError([failed](const NetworkResult &added) {
                    failed(QStringLiteral("Couldn't add it: ") +
                           added.formatError());
                })
                .execute();
        })
        .onError([failed](const NetworkResult &result) {
            failed(QStringLiteral("Couldn't reach 7TV: ") + result.formatError());
        })
        .execute();
}

}  // namespace chatterino

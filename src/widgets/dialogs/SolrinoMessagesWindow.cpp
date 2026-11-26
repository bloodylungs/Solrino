#include "widgets/dialogs/SolrinoMessagesWindow.hpp"

#include "providers/moltorino/SolrinoMessages.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "singletons/WindowManager.hpp"

#include <pajlada/signals/scoped-connection.hpp>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QJsonArray>
#include <QMenu>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#    include <Windows.h>
#    include <dwmapi.h>
#    pragma comment(lib, "Dwmapi.lib")
#endif

namespace chatterino {

namespace {

using Messages = SolrinoMessages;

const QString STYLE = QStringLiteral(R"(
QWidget#solrinoMessages { background: #0f0a0c; color: #f2e7e3; font-size: 13px; }
QWidget#side { background: #150e11; border-right: 1px solid #241a1d; }
QWidget#head { background: #130c0f; border-bottom: 1px solid #241a1d; }
QWidget#composerBar { background: #130c0f; border-top: 1px solid #241a1d; }
QLabel#title { font-size: 19px; font-weight: 800; }
QLabel#peer { font-size: 15px; font-weight: 700; }
QLabel#lock { color: #8fd19e; font-size: 11.5px; }
QLabel#muted { color: #9d8b8f; }
QLabel#emptyTitle { font-size: 15px; font-weight: 700; color: #d9cbc7; }
QLabel#promise { color: #d9cbc7; font-size: 13px; }
QLabel#hello { color: #9d8b8f; font-size: 12px; }
QLabel#day { color: #9d8b8f; font-size: 11px; background: #1d1417; border-radius: 9px; padding: 3px 10px; }
QLabel#time { color: #8a787c; font-size: 10.5px; }
QLabel#bubbleMine { background: #c8101a; color: #fff; border-radius: 16px; padding: 9px 13px; }
QLabel#bubbleTheirs { background: #261b1e; color: #f2e7e3; border-radius: 16px; padding: 9px 13px; }
QWidget#banner { background: #24131a; border: 1px solid #4d1d27; border-radius: 12px; }
QWidget#warn { background: #2b200d; border: 1px solid #6b4e12; border-radius: 12px; }
QLineEdit#search { background: #1c1316; border: 1px solid #2c2024; border-radius: 10px; padding: 7px 10px; color: #f2e7e3; }
QLineEdit#search:focus { border-color: #e3101c; }
QWidget#segment { background: #1c1316; border-radius: 10px; }
QPushButton#seg { background: transparent; border: none; border-radius: 8px; padding: 6px 10px; color: #b3a1a5; font-weight: 600; }
QPushButton#seg:checked { background: #e3101c; color: #fff; }
QPushButton#seg:hover:!checked { color: #f2e7e3; }
QListWidget { background: transparent; border: none; outline: none; }
QListWidget::item { border-radius: 12px; margin: 1px 0; }
QListWidget::item:selected { background: #2b1418; }
QListWidget::item:hover:!selected { background: #1d1417; }
QPushButton { background: #1f1619; border: 1px solid #33262a; border-radius: 10px; padding: 7px 12px; color: #f2e7e3; font-weight: 600; }
QPushButton:hover { background: #291d21; border-color: #4a383c; }
QPushButton#primary { background: #e3101c; border-color: #ff4d55; color: #fff; }
QPushButton#primary:hover { background: #f02a33; }
QPushButton#primary:disabled { background: #3a2a2c; border-color: #3a2a2c; color: #8a787c; }
QPushButton#ghost { background: transparent; border-color: transparent; color: #b3a1a5; }
QPushButton#ghost:hover { background: #1f1619; color: #f2e7e3; }
QPushButton#danger { background: transparent; border-color: transparent; color: #ff7b72; }
QPushButton#danger:hover { background: #2a1416; }
QPushButton#round { background: #e3101c; border: none; border-radius: 19px; padding: 0; }
QPushButton#round:hover { background: #f5303a; }
QPushButton#round:pressed { background: #b80c16; }
QPushButton#round:disabled { background: #2a1f22; }
QPushButton#replyButton { background: #1f1619; border: 1px solid #33262a; border-radius: 12px; padding: 0; color: #b3a1a5; font-size: 13px; }
QPushButton#replyButton:hover { background: #2d2125; color: #f2e7e3; }
QLabel#replyQuote { color: #b3a1a5; font-size: 11px; border-left: 2px solid #5a3a40; padding: 1px 0 1px 7px; }
QWidget#replyBar { background: #1a1114; border-top: 1px solid #241a1d; }
QLabel#replyBarText { color: #c9b8bb; font-size: 12px; }
QPushButton#replyClose { background: transparent; border: none; color: #9d8b8f; font-size: 15px; padding: 0 4px; }
QPushButton#replyClose:hover { color: #f2e7e3; }
QPlainTextEdit { background: #1c1316; border: 1px solid #2c2024; border-radius: 19px; padding: 8px 12px; color: #f2e7e3; }
QPlainTextEdit:focus { border-color: #e3101c; }
QScrollArea { background: #0f0a0c; border: none; }
QWidget#page { background: #0f0a0c; }
QWidget#fileMine { background: #c8101a; border-radius: 14px; }
QWidget#fileTheirs { background: #261b1e; border-radius: 14px; }
QWidget#fileMine QPushButton, QWidget#fileTheirs QPushButton { padding: 4px 10px; }
QWidget#picker { background: #170f12; border: 1px solid #33262a; border-radius: 12px; }
QLabel#receipt { color: #9d8b8f; font-size: 11px; }
QPushButton#tool { background: transparent; border: none; border-radius: 17px; color: #b3a1a5; font-size: 17px; padding: 0; }
QPushButton#tool:hover { background: #241a1d; color: #f2e7e3; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: #3a2c30; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)");

/// A round picture with their first letter, in a colour that's always the
/// same for the same person
QPixmap avatar(const QString &name, int size)
{
    static const QList<QColor> colours{
        QColor("#e3101c"), QColor("#ff7a45"), QColor("#d6336c"),
        QColor("#7048e8"), QColor("#1c7ed6"), QColor("#0ca678"),
        QColor("#f59f00"), QColor("#ae3ec9"),
    };
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const auto colour = colours[qHash(name.toLower()) % colours.size()];
    QLinearGradient fill(0, 0, size, size);
    fill.setColorAt(0, colour.lighter(125));
    fill.setColorAt(1, colour.darker(135));
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawEllipse(QRectF(0, 0, size, size));
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(int(size * 0.44));
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter,
               name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper());
    return pixmap;
}

/// A padlock, drawn so it doesn't need an image file
void drawPadlock(QPainter &p, const QRectF &box, const QColor &colour,
                 bool keyhole)
{
    const qreal s = box.width();
    const qreal x = box.x();
    const qreal y = box.y();
    p.setPen(QPen(colour, s * 0.1, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    QPainterPath shackle;
    shackle.moveTo(x + s * 0.3, y + s * 0.46);
    shackle.lineTo(x + s * 0.3, y + s * 0.32);
    shackle.arcTo(QRectF(x + s * 0.3, y + s * 0.1, s * 0.4, s * 0.44), 180, -180);
    shackle.lineTo(x + s * 0.7, y + s * 0.46);
    p.drawPath(shackle);
    QLinearGradient body(x, y + s * 0.42, x, y + s * 0.92);
    body.setColorAt(0, colour.lighter(118));
    body.setColorAt(1, colour.darker(118));
    p.setPen(Qt::NoPen);
    p.setBrush(body);
    p.drawRoundedRect(QRectF(x + s * 0.18, y + s * 0.42, s * 0.64, s * 0.5),
                      s * 0.12, s * 0.12);
    if (keyhole)
    {
        p.setBrush(QColor(15, 10, 12, 210));
        p.drawEllipse(QPointF(x + s * 0.5, y + s * 0.62), s * 0.07, s * 0.07);
        p.drawRoundedRect(QRectF(x + s * 0.47, y + s * 0.63, s * 0.06, s * 0.15),
                          s * 0.02, s * 0.02);
    }
}

QPixmap padlock(int size, const QColor &colour)
{
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    drawPadlock(p, QRectF(0, 0, size, size), colour, size >= 24);
    return pixmap;
}

/// The big emblem on the empty screen: a padlock in a glowing ring

/// A small green tick for the list of promises

/// A pencil for "new chat"
QPixmap pencil(int size, const QColor &colour)
{
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size;
    p.setPen(QPen(colour, s * 0.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    path.moveTo(s * 0.2, s * 0.8);
    path.lineTo(s * 0.24, s * 0.62);
    path.lineTo(s * 0.66, s * 0.2);
    path.lineTo(s * 0.8, s * 0.34);
    path.lineTo(s * 0.38, s * 0.76);
    path.closeSubpath();
    p.drawPath(path);
    return pixmap;
}


QString shortTime(const QDateTime &at)
{
    const auto local = at.toLocalTime();
    const auto today = QDate::currentDate();
    if (local.date() == today)
    {
        return QLocale().toString(local.time(), QLocale::ShortFormat);
    }
    if (local.date().daysTo(today) < 7)
    {
        return QLocale().dayName(local.date().dayOfWeek(), QLocale::ShortFormat);
    }
    return QLocale().toString(local.date(), QLocale::ShortFormat);
}

/// Enter sends, Shift+Enter makes a new line
class Composer : public QPlainTextEdit
{
public:
    std::function<void()> onSend;
    std::function<void()> onEscape;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape && this->onEscape)
        {
            this->onEscape();
            return;
        }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            !(event->modifiers() & Qt::ShiftModifier))
        {
            if (this->onSend)
            {
                this->onSend();
            }
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
};

/// The send button's paper plane, white when it can send, grey when not
QIcon sendIcon()
{
    auto plane = [](const QColor &color) {
        const qreal ratio = qApp->devicePixelRatio();
        QPixmap pix(int(20 * ratio), int(20 * ratio));
        pix.setDevicePixelRatio(ratio);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.moveTo(2.5, 9.0);
        path.lineTo(17.5, 2.5);
        path.lineTo(11.0, 17.5);
        path.lineTo(9.0, 11.0);
        path.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(path);
        // The fold down the middle
        p.setPen(QPen(QColor(0, 0, 0, 60), 1.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(9.0, 11.0), QPointF(17.5, 2.5));
        return pix;
    };
    QIcon icon;
    icon.addPixmap(plane(Qt::white), QIcon::Normal);
    icon.addPixmap(plane(QColor("#6e5c60")), QIcon::Disabled);
    return icon;
}

/// A message row that shows its Reply button while the mouse is over it
class HoverRow : public QWidget
{
public:
    QWidget *reveal = nullptr;

protected:
    void enterEvent(QEnterEvent *event) override
    {
        QWidget::enterEvent(event);
        if (this->reveal != nullptr)
        {
            this->reveal->setVisible(true);
        }
    }
    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        if (this->reveal != nullptr)
        {
            this->reveal->setVisible(false);
        }
    }
};

/// Runs something when a widget is clicked
class ClickFilter : public QObject
{
public:
    ClickFilter(QObject *parent, std::function<void()> action)
        : QObject(parent)
        , action_(std::move(action))
    {
    }

protected:
    bool eventFilter(QObject * /*watched*/, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease)
        {
            this->action_();
            return true;
        }
        return false;
    }

private:
    std::function<void()> action_;
};

// ---------------------------------------------------------------- Pictures

/// Twitch profile pictures, fetched once per person and kept round
class Avatars
{
public:
    static Avatars &instance()
    {
        static Avatars avatars;
        return avatars;
    }

    std::function<void()> onLoaded;

    /// Their picture if it's here, otherwise their letter while it loads
    QPixmap get(const QString &userId, const QString &name, int size)
    {
        const auto found = this->pictures_.constFind(userId);
        if (found != this->pictures_.constEnd() && !found->isNull())
        {
            return round(*found, size);
        }
        this->fetch(userId);
        return avatar(name, size);
    }

private:
    void fetch(const QString &userId)
    {
        if (userId.isEmpty() || this->asked_.contains(userId))
        {
            return;
        }
        this->asked_.insert(userId);
        getHelix()->getUserById(
            userId,
            [this, userId](const HelixUser &user) {
                if (user.profileImageUrl.isEmpty())
                {
                    return;
                }
                NetworkRequest(user.profileImageUrl)
                    .timeout(15000)
                    .maximumResponseSize(2 * 1024 * 1024)
                    .onSuccess([this, userId](const NetworkResult &result) {
                        QImage image;
                        if (!image.loadFromData(result.getData()))
                        {
                            return;
                        }
                        this->pictures_.insert(userId,
                                               QPixmap::fromImage(image));
                        if (this->onLoaded)
                        {
                            this->onLoaded();
                        }
                    })
                    .execute();
            },
            [] {});
    }

    static QPixmap round(const QPixmap &source, int size)
    {
        const qreal ratio = qApp->devicePixelRatio();
        QPixmap out(QSize(size, size) * ratio);
        out.setDevicePixelRatio(ratio);
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addEllipse(QRectF(0, 0, size, size));
        p.setClipPath(clip);
        p.drawPixmap(QRectF(0, 0, size, size), source, source.rect());
        return out;
    }

    QHash<QString, QPixmap> pictures_;
    QSet<QString> asked_;
};

// ---------------------------------------------------------------- 7TV emotes

/// 7TV's global emotes plus each person's channel emotes, so both people's
/// emotes work in their chat
class Emotes
{
public:
    static Emotes &instance()
    {
        static Emotes emotes;
        return emotes;
    }

    std::function<void()> onLoaded;

    void want(const QString &userId)
    {
        if (userId.isEmpty() || this->asked_.contains(userId))
        {
            return;
        }
        this->asked_.insert(userId);
        NetworkRequest(QStringLiteral("https://7tv.io/v3/users/twitch/") + userId)
            .timeout(15000)
            .maximumResponseSize(8 * 1024 * 1024)
            .onSuccess([this, userId](const NetworkResult &result) {
                const auto emotes = result.parseJson()
                                        .value("emote_set")
                                        .toObject()
                                        .value("emotes")
                                        .toArray();
                this->sets_.insert(
                    userId, std::make_shared<const EmoteMap>(
                                seventv::detail::parseEmotes(
                                    emotes, SeventvEmoteSetKind::Channel)));
                if (this->onLoaded)
                {
                    this->onLoaded();
                }
            })
            .execute();
    }

    /// The emote with this name for a chat between these people
    EmotePtr find(const QString &word, const QStringList &people) const
    {
        const EmoteName name{word};
        for (const auto &id : people)
        {
            const auto set = this->sets_.value(id);
            if (set != nullptr)
            {
                const auto found = set->find(name);
                if (found != set->end())
                {
                    return found->second;
                }
            }
        }
        if (auto global = getApp()->getSeventvEmotes()->globalEmote(name))
        {
            return *global;
        }
        return nullptr;
    }

    /// Everything that can be used in this chat, people's own first
    std::vector<std::pair<QString, EmotePtr>> all(const QStringList &people) const
    {
        std::vector<std::pair<QString, EmotePtr>> out;
        QSet<QString> seen;
        auto add = [&](const std::shared_ptr<const EmoteMap> &set,
                       const QString &group) {
            if (set == nullptr)
            {
                return;
            }
            std::vector<EmotePtr> sorted;
            for (const auto &[name, emote] : *set)
            {
                sorted.push_back(emote);
            }
            std::sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) {
                return a->name.string.compare(b->name.string,
                                              Qt::CaseInsensitive) < 0;
            });
            for (const auto &emote : sorted)
            {
                if (!seen.contains(emote->name.string))
                {
                    seen.insert(emote->name.string);
                    out.emplace_back(group, emote);
                }
            }
        };
        for (const auto &id : people)
        {
            add(this->sets_.value(id), id);
        }
        add(getApp()->getSeventvEmotes()->globalEmotes(), QStringLiteral("global"));
        return out;
    }

private:
    QHash<QString, std::shared_ptr<const EmoteMap>> sets_;
    QSet<QString> asked_;
};

// ---------------------------------------------------------------- Bubbles

constexpr int EMOTE_HEIGHT = 28;

/// A message bubble that draws 7TV emotes in the text, animated
class TextBubble : public QWidget
{
public:
    std::function<void()> onReply;

    TextBubble(const QString &text, bool mine, const QStringList &people,
               int maxWidth)
        : mine_(mine)
        , text_(text)
        , maxWidth_(maxWidth)
    {
        this->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(this, &QWidget::customContextMenuRequested, this,
                         [this](const QPoint &at) {
                             QMenu menu;
                             if (this->onReply)
                             {
                                 menu.addAction(QStringLiteral("Reply"),
                                                [this] {
                                                    this->onReply();
                                                });
                             }
                             menu.addAction(QStringLiteral("Copy text"), [this] {
                                 QApplication::clipboard()->setText(this->text_);
                             });
                             menu.exec(this->mapToGlobal(at));
                         });
        for (const auto &line : text.split(u'\n'))
        {
            const auto words = line.split(u' ');
            for (int i = 0; i < words.size(); i++)
            {
                Piece piece;
                piece.text = words[i];
                piece.emote = Emotes::instance().find(words[i], people);
                if (piece.emote)
                {
                    this->animated_ = true;
                }
                this->pieces_.push_back(piece);
            }
            Piece breakLine;
            breakLine.lineBreak = true;
            this->pieces_.push_back(breakLine);
        }
        if (!this->pieces_.empty())
        {
            this->pieces_.pop_back();
        }
        this->relayout();
        this->setMouseTracking(true);
        if (this->animated_)
        {
            this->repaint_ = std::make_unique<pajlada::Signals::ScopedConnection>(
                getApp()->getWindows()->gifRepaintRequested.connect([this] {
                    if (this->isVisible())
                    {
                        this->checkSizes();
                        this->update();
                    }
                }));
        }
    }

protected:
    // Like chat: hovering an emote shows its name and where it's from
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::ToolTip)
        {
            const auto *help = static_cast<QHelpEvent *>(event);
            for (const auto &placed : this->placed_)
            {
                const auto &piece = this->pieces_[placed.index];
                if (piece.emote && placed.rect.contains(help->pos()))
                {
                    auto tip = piece.emote->tooltip.string;
                    if (tip.isEmpty())
                    {
                        tip = piece.emote->name.string;
                    }
                    QToolTip::showText(help->globalPos(), tip, this,
                                       placed.rect.toRect());
                    return true;
                }
            }
            QToolTip::hideText();
            event->ignore();
            return true;
        }
        return QWidget::event(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        bool overEmote = false;
        for (const auto &placed : this->placed_)
        {
            if (this->pieces_[placed.index].emote &&
                placed.rect.contains(event->position()))
            {
                overEmote = true;
            }
        }
        this->setCursor(overEmote ? Qt::PointingHandCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }

    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setPen(Qt::NoPen);
        p.setBrush(this->mine_ ? QColor("#c8101a") : QColor("#261b1e"));
        p.drawRoundedRect(QRectF(this->rect()), 16, 16);
        p.setPen(this->mine_ ? QColor(Qt::white) : QColor("#f2e7e3"));
        p.setFont(this->font());
        const QFontMetrics fm(this->font());
        for (const auto &placed : this->placed_)
        {
            const auto &piece = this->pieces_[placed.index];
            if (piece.emote)
            {
                if (auto pix = piece.emote->images.getImage2()->pixmapOrLoad())
                {
                    p.drawPixmap(placed.rect, *pix, pix->rect());
                }
            }
            else
            {
                p.drawText(placed.rect, Qt::AlignLeft | Qt::AlignVCenter,
                           piece.text);
            }
        }
    }

private:
    struct Piece {
        QString text;
        EmotePtr emote;
        bool lineBreak = false;
    };
    struct Placed {
        int index;
        QRectF rect;
    };

    QSizeF emoteSize(const Piece &piece) const
    {
        const auto &image = piece.emote->images.getImage2();
        const qreal w = image->width();
        const qreal h = image->height();
        const qreal width = (w > 0 && h > 0) ? EMOTE_HEIGHT * w / h : EMOTE_HEIGHT;
        return {width, qreal(EMOTE_HEIGHT)};
    }

    // Emotes that just loaded may be wider than guessed
    void checkSizes()
    {
        for (size_t i = 0; i < this->pieces_.size(); i++)
        {
            const auto &piece = this->pieces_[i];
            if (piece.emote && this->guessed_.value(int(i)) !=
                                   this->emoteSize(piece).width())
            {
                this->relayout();
                return;
            }
        }
    }

    void relayout()
    {
        const QFontMetrics fm(this->font());
        const int padX = 13;
        const int padY = 9;
        const int inner = this->maxWidth_ - padX * 2;
        const qreal space = fm.horizontalAdvance(u' ');
        const qreal textHeight = fm.height();
        const bool hasEmote = this->animated_;
        const qreal lineHeight = hasEmote ? std::max<qreal>(textHeight, EMOTE_HEIGHT)
                                          : textHeight;
        this->placed_.clear();
        this->guessed_.clear();
        qreal x = 0;
        qreal y = 0;
        qreal widest = 0;
        for (size_t i = 0; i < this->pieces_.size(); i++)
        {
            const auto &piece = this->pieces_[i];
            if (piece.lineBreak)
            {
                widest = std::max(widest, x);
                x = 0;
                y += lineHeight + 2;
                continue;
            }
            QSizeF size;
            if (piece.emote)
            {
                size = this->emoteSize(piece);
                this->guessed_.insert(int(i), size.width());
            }
            else
            {
                size = QSizeF(std::min<qreal>(fm.horizontalAdvance(piece.text),
                                              inner),
                              textHeight);
            }
            if (x > 0 && x + size.width() > inner)
            {
                widest = std::max(widest, x - space);
                x = 0;
                y += lineHeight + 2;
            }
            const qreal top = y + (lineHeight - size.height()) / 2;
            this->placed_.push_back(
                {int(i), QRectF(padX + x, padY + top, size.width(), size.height())});
            x += size.width() + space;
        }
        widest = std::max(widest, x > 0 ? x - space : 0);
        const int width = int(std::ceil(widest)) + padX * 2;
        const int height = int(std::ceil(y + lineHeight)) + padY * 2;
        this->setFixedSize(std::max(width, 36), height);
        this->update();
    }

    bool mine_;
    QString text_;
    int maxWidth_;
    bool animated_ = false;
    std::vector<Piece> pieces_;
    std::vector<Placed> placed_;
    QHash<int, qreal> guessed_;
    std::unique_ptr<pajlada::Signals::ScopedConnection> repaint_;
};

QString readableSize(qint64 bytes)
{
    if (bytes < 1024)
    {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024)
    {
        return QStringLiteral("%1 KB").arg(bytes / 1024);
    }
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024 * 1024), 0, 'f', 1);
}

/// Writes an opened file somewhere temporary and opens it with the PC's app
void openWithSystem(const Messages::File &file, const QByteArray &data)
{
    const auto folder =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
        QStringLiteral("/Solrino");
    QDir().mkpath(folder);
    auto name = file.name;
    name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")),
                 QStringLiteral("_"));
    const auto path = folder + u'/' + file.id.left(8) + u'-' + name;
    QFile out(path);
    if (out.open(QIODevice::WriteOnly))
    {
        out.write(data);
        out.close();
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

/// A photo shows as a picture; anything else as a card with Open and Save
QWidget *fileBubble(const Messages::Message &m, int maxWidth, QWidget *owner)
{
    const auto file = *m.file;
    auto *box = new QWidget;
    box->setObjectName(m.mine ? QStringLiteral("fileMine")
                              : QStringLiteral("fileTheirs"));
    box->setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    QPointer<QWidget> guard(box);

    if (file.mime.startsWith(QStringLiteral("image/")))
    {
        auto *picture = new QLabel(QStringLiteral("Loading photo..."));
        picture->setObjectName(QStringLiteral("muted"));
        picture->setMinimumSize(160, 100);
        picture->setAlignment(Qt::AlignCenter);
        picture->setCursor(Qt::PointingHandCursor);
        picture->setToolTip(QStringLiteral("Click to open"));
        layout->addWidget(picture);
        auto data = std::make_shared<QByteArray>();
        Messages::instance().loadFile(
            file, [guard, picture, data, maxWidth](QByteArray bytes,
                                                   QString error) {
                if (guard == nullptr)
                {
                    return;
                }
                QImage image;
                if (!error.isEmpty() || !image.loadFromData(bytes))
                {
                    picture->setText(error.isEmpty()
                                         ? QStringLiteral("Couldn't show it")
                                         : error);
                    return;
                }
                *data = bytes;
                const int side = std::min(maxWidth - 16, 320);
                picture->setPixmap(QPixmap::fromImage(image.scaled(
                    side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                picture->setMinimumSize(0, 0);
            });
        picture->installEventFilter(new ClickFilter(picture, [file, data] {
            if (!data->isEmpty())
            {
                openWithSystem(file, *data);
            }
        }));
    }
    else
    {
        const bool video = file.mime.startsWith(QStringLiteral("video/"));
        auto *name = new QLabel(QStringLiteral("<b>%1</b><br><span style='color:#b3a1a5'>%2 · %3</span>")
                                    .arg(file.name.toHtmlEscaped(),
                                         video ? QStringLiteral("Video")
                                               : QStringLiteral("File"),
                                         readableSize(file.size)));
        name->setTextFormat(Qt::RichText);
        name->setWordWrap(true);
        name->setMaximumWidth(maxWidth - 16);
        layout->addWidget(name);
        auto *buttons = new QHBoxLayout;
        auto *openButton = new QPushButton(video ? QStringLiteral("Play")
                                                 : QStringLiteral("Open"));
        auto *saveButton = new QPushButton(QStringLiteral("Save"));
        buttons->addWidget(openButton);
        buttons->addWidget(saveButton);
        buttons->addStretch();
        layout->addLayout(buttons);
        auto status = [guard, name, file](const QString &why) {
            if (guard != nullptr)
            {
                name->setText(QStringLiteral("<b>%1</b><br><span style='color:#ff7b72'>%2</span>")
                                  .arg(file.name.toHtmlEscaped(), why.toHtmlEscaped()));
            }
        };
        QObject::connect(openButton, &QPushButton::clicked, box, [file, status] {
            Messages::instance().loadFile(file, [file, status](QByteArray bytes,
                                                              QString error) {
                if (!error.isEmpty())
                {
                    status(error);
                    return;
                }
                openWithSystem(file, bytes);
            });
        });
        QObject::connect(saveButton, &QPushButton::clicked, box, [file, status,
                                                                owner] {
            const auto path = QFileDialog::getSaveFileName(
                owner, QStringLiteral("Save file"),
                QStandardPaths::writableLocation(
                    QStandardPaths::DownloadLocation) +
                    u'/' + file.name);
            if (path.isEmpty())
            {
                return;
            }
            Messages::instance().loadFile(file, [path, status](QByteArray bytes,
                                                              QString error) {
                if (!error.isEmpty())
                {
                    status(error);
                    return;
                }
                QFile out(path);
                if (out.open(QIODevice::WriteOnly))
                {
                    out.write(bytes);
                }
            });
        });
    }
    if (!m.text.isEmpty())
    {
        auto *caption = new QLabel(m.text);
        caption->setWordWrap(true);
        caption->setMaximumWidth(maxWidth - 16);
        layout->addWidget(caption);
    }
    return box;
}

// ---------------------------------------------------------------- Emote picker

/// A small searchable grid of the 7TV emotes this chat can use
class EmotePicker : public QWidget
{
public:
    std::function<void(QString)> picked;

    EmotePicker(QWidget *parent, const QStringList &people)
        : QWidget(parent, Qt::Popup)
    {
        this->setObjectName(QStringLiteral("picker"));
        this->setAttribute(Qt::WA_StyledBackground);
        this->setAttribute(Qt::WA_DeleteOnClose);
        this->resize(360, 380);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        auto *search = new QLineEdit;
        search->setObjectName(QStringLiteral("search"));
        search->setPlaceholderText(QStringLiteral("Search 7TV emotes"));
        layout->addWidget(search);
        this->grid_ = new QListWidget;
        this->grid_->setViewMode(QListView::IconMode);
        this->grid_->setIconSize(QSize(32, 32));
        this->grid_->setGridSize(QSize(48, 48));
        this->grid_->setResizeMode(QListView::Adjust);
        this->grid_->setMovement(QListView::Static);
        this->grid_->setUniformItemSizes(true);
        layout->addWidget(this->grid_, 1);
        auto *hint = new QLabel(QStringLiteral(
            "Your emotes, their emotes, and 7TV's global emotes"));
        hint->setObjectName(QStringLiteral("muted"));
        layout->addWidget(hint);

        this->emotes_ = Emotes::instance().all(people);
        QObject::connect(search, &QLineEdit::textChanged, this,
                         [this](const QString &text) {
                             this->fill(text);
                         });
        QObject::connect(this->grid_, &QListWidget::itemClicked, this,
                         [this](QListWidgetItem *item) {
                             if (this->picked)
                             {
                                 this->picked(item->toolTip());
                             }
                             this->close();
                         });
        // Pictures arrive as they load
        this->timer_.setInterval(300);
        QObject::connect(&this->timer_, &QTimer::timeout, this, [this] {
            this->loadIcons();
        });
        this->timer_.start();
        this->fill({});
        search->setFocus();
    }

private:
    void fill(const QString &filter)
    {
        this->grid_->clear();
        this->shown_.clear();
        int count = 0;
        for (const auto &[group, emote] : this->emotes_)
        {
            if (!filter.isEmpty() &&
                !emote->name.string.contains(filter, Qt::CaseInsensitive))
            {
                continue;
            }
            auto *item = new QListWidgetItem;
            item->setToolTip(emote->name.string);
            this->grid_->addItem(item);
            this->shown_.push_back({item, emote});
            if (++count >= 400)
            {
                break;
            }
        }
        this->loadIcons();
    }

    void loadIcons()
    {
        for (auto &[item, emote] : this->shown_)
        {
            if (item->icon().isNull())
            {
                if (auto pix = emote->images.getImage1()->pixmapOrLoad())
                {
                    item->setIcon(QIcon(*pix));
                }
            }
        }
    }

    QListWidget *grid_;
    QTimer timer_;
    std::vector<std::pair<QString, EmotePtr>> emotes_;
    std::vector<std::pair<QListWidgetItem *, EmotePtr>> shown_;
};

/// One row in the chat list
QWidget *chatRow(const Messages::Conversation &c)
{
    auto *row = new QWidget;
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(10);
    const auto name = c.login.isEmpty() ? c.userId : c.login;
    auto *pic = new QLabel;
    pic->setPixmap(Avatars::instance().get(c.userId, name, 38));
    pic->setFixedSize(38, 38);
    layout->addWidget(pic);

    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    auto *top = new QHBoxLayout;
    auto *who = new QLabel(name);
    QFont bold = who->font();
    bold.setBold(true);
    who->setFont(bold);
    top->addWidget(who, 1);
    if (!c.messages.empty())
    {
        auto *when = new QLabel(shortTime(c.messages.back().at));
        when->setObjectName(QStringLiteral("time"));
        top->addWidget(when);
    }
    text->addLayout(top);

    auto *bottom = new QHBoxLayout;
    QString preview;
    if (c.blocked)
    {
        preview = QStringLiteral("Blocked");
    }
    else if (c.messages.empty())
    {
        preview = QStringLiteral("Say hi");
    }
    else
    {
        preview = (c.messages.back().mine ? QStringLiteral("You: ") : QString()) +
                  c.messages.back().text.simplified();
    }
    auto *line = new QLabel;
    line->setObjectName(QStringLiteral("muted"));
    line->setText(line->fontMetrics().elidedText(preview, Qt::ElideRight, 160));
    bottom->addWidget(line, 1);
    if (c.unread > 0)
    {
        auto *pill = new QLabel(QString::number(c.unread));
        pill->setAlignment(Qt::AlignCenter);
        pill->setMinimumWidth(20);
        pill->setStyleSheet(QStringLiteral(
            "background:#e3101c; color:#fff; border-radius:9px; "
            "padding:1px 6px; font-weight:700; font-size:11px;"));
        bottom->addWidget(pill);
    }
    text->addLayout(bottom);
    layout->addLayout(text, 1);
    return row;
}

class MessagesWindow : public QWidget
{
public:
    MessagesWindow()
        : QWidget(nullptr, Qt::Window)
    {
        this->setObjectName(QStringLiteral("solrinoMessages"));
        this->setAttribute(Qt::WA_StyledBackground);
        this->setWindowTitle(QStringLiteral("Solrino messages"));
        this->setStyleSheet(STYLE);
        this->resize(940, 660);
        this->setMinimumSize(700, 480);
#ifdef Q_OS_WIN
        // A dark title bar instead of Windows' white one
        const auto hwnd = reinterpret_cast<HWND>(this->winId());
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                              &dark, sizeof(dark));
        COLORREF caption = RGB(0x15, 0x0e, 0x11);
        DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption,
                              sizeof(caption));
#endif

        auto *root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(this->buildSide());
        root->addWidget(this->buildMain(), 1);

        QObject::connect(&Messages::instance(), &Messages::changed, this,
                         [this] {
                             this->refresh();
                         });
        this->later_.setSingleShot(true);
        this->later_.setInterval(150);
        QObject::connect(&this->later_, &QTimer::timeout, this, [this] {
            this->refresh();
        });
        Avatars::instance().onLoaded = [self = QPointer<MessagesWindow>(this)] {
            if (self != nullptr)
            {
                self->later_.start();
            }
        };
        Emotes::instance().onLoaded = Avatars::instance().onLoaded;
        this->refresh();
    }

    void startWith(const QString &name)
    {
        this->status_->setText(QStringLiteral("Looking them up..."));
        QPointer<MessagesWindow> self(this);
        Messages::instance().findUser(
            name, [self](QString userId, QString login, QString error) {
                if (self == nullptr)
                {
                    return;
                }
                if (!error.isEmpty())
                {
                    self->status_->setText(error);
                    return;
                }
                self->status_->clear();
                self->pendingLogin_ = login;
                self->showRequests_ = false;
                self->current_ = userId;
                self->refresh();
                self->composer_->setFocus();
            });
    }

private:
    QWidget *buildSide()
    {
        auto *side = new QWidget;
        side->setObjectName(QStringLiteral("side"));
        side->setAttribute(Qt::WA_StyledBackground);
        side->setFixedWidth(290);
        auto *layout = new QVBoxLayout(side);
        layout->setContentsMargins(14, 16, 14, 14);
        layout->setSpacing(10);

        auto *top = new QHBoxLayout;
        auto *titles = new QVBoxLayout;
        titles->setSpacing(0);
        auto *title = new QLabel(QStringLiteral("Messages"));
        title->setObjectName(QStringLiteral("title"));
        this->hello_ = new QLabel;
        this->hello_->setObjectName(QStringLiteral("hello"));
        titles->addWidget(title);
        titles->addWidget(this->hello_);
        auto *newChat = new QPushButton(QStringLiteral(" New"));
        newChat->setObjectName(QStringLiteral("primary"));
        newChat->setIcon(QIcon(pencil(16, Qt::white)));
        newChat->setIconSize(QSize(14, 14));
        newChat->setToolTip(QStringLiteral("Message someone by their Twitch name"));
        newChat->setCursor(Qt::PointingHandCursor);
        top->addLayout(titles);
        top->addStretch();
        top->addWidget(newChat, 0, Qt::AlignTop);
        layout->addLayout(top);
        layout->addSpacing(2);

        this->search_ = new QLineEdit;
        this->search_->setObjectName(QStringLiteral("search"));
        this->search_->setPlaceholderText(QStringLiteral("Search chats"));
        this->search_->setClearButtonEnabled(true);
        layout->addWidget(this->search_);

        auto *segment = new QWidget;
        segment->setObjectName(QStringLiteral("segment"));
        segment->setAttribute(Qt::WA_StyledBackground);
        auto *segLayout = new QHBoxLayout(segment);
        segLayout->setContentsMargins(3, 3, 3, 3);
        segLayout->setSpacing(3);
        this->chatsTab_ = new QPushButton(QStringLiteral("Chats"));
        this->requestsTab_ = new QPushButton(QStringLiteral("Requests"));
        auto *group = new QButtonGroup(segment);
        for (auto *tab : {this->chatsTab_, this->requestsTab_})
        {
            tab->setObjectName(QStringLiteral("seg"));
            tab->setCheckable(true);
            group->addButton(tab);
            segLayout->addWidget(tab);
        }
        this->chatsTab_->setChecked(true);
        layout->addWidget(segment);

        this->list_ = new QListWidget;
        this->list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        layout->addWidget(this->list_, 1);

        this->listEmpty_ = new QLabel;
        this->listEmpty_->setObjectName(QStringLiteral("muted"));
        this->listEmpty_->setAlignment(Qt::AlignCenter);
        this->listEmpty_->setWordWrap(true);
        layout->addWidget(this->listEmpty_, 1);

        this->status_ = new QLabel;
        this->status_->setObjectName(QStringLiteral("muted"));
        this->status_->setWordWrap(true);
        layout->addWidget(this->status_);

        QObject::connect(newChat, &QPushButton::clicked, this, [this] {
            bool ok = false;
            const auto name = QInputDialog::getText(
                this, QStringLiteral("New message"),
                QStringLiteral("Their Twitch name:"), QLineEdit::Normal, {},
                &ok);
            if (ok && !name.trimmed().isEmpty())
            {
                this->startWith(name);
            }
        });
        QObject::connect(this->search_, &QLineEdit::textChanged, this, [this] {
            this->refresh();
        });
        QObject::connect(this->chatsTab_, &QPushButton::clicked, this, [this] {
            this->showRequests_ = false;
            this->refresh();
        });
        QObject::connect(this->requestsTab_, &QPushButton::clicked, this,
                         [this] {
                             this->showRequests_ = true;
                             this->refresh();
                         });
        QObject::connect(this->list_, &QListWidget::currentRowChanged, this,
                         [this](int row) {
                             if (row >= 0 &&
                                 row < static_cast<int>(this->rows_.size()) &&
                                 this->rows_[row] != this->current_)
                             {
                                 this->current_ = this->rows_[row];
                                 this->pendingLogin_.clear();
                                 this->refresh();
                             }
                         });
        return side;
    }

    QWidget *buildMain()
    {
        auto *main = new QWidget;
        auto *layout = new QVBoxLayout(main);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // ---- Header
        this->head_ = new QWidget;
        this->head_->setObjectName(QStringLiteral("head"));
        this->head_->setAttribute(Qt::WA_StyledBackground);
        auto *head = new QHBoxLayout(this->head_);
        head->setContentsMargins(18, 12, 14, 12);
        head->setSpacing(12);
        this->peerPic_ = new QLabel;
        this->peerPic_->setFixedSize(40, 40);
        head->addWidget(this->peerPic_);
        auto *names = new QVBoxLayout;
        names->setSpacing(1);
        this->peer_ = new QLabel;
        this->peer_->setObjectName(QStringLiteral("peer"));
        auto *lockRow = new QHBoxLayout;
        lockRow->setSpacing(5);
        auto *lockIcon = new QLabel;
        lockIcon->setPixmap(padlock(12, QColor("#8fd19e")));
        auto *lock = new QLabel(QStringLiteral("End-to-end encrypted"));
        lock->setObjectName(QStringLiteral("lock"));
        lock->setToolTip(QStringLiteral(
            "Only you and them can read these messages. Solrino's server only "
            "passes on sealed envelopes and can't open them."));
        lockRow->addWidget(lockIcon);
        lockRow->addWidget(lock);
        lockRow->addStretch();
        names->addWidget(this->peer_);
        names->addLayout(lockRow);
        head->addLayout(names, 1);
        this->codeButton_ = new QPushButton(QStringLiteral("Security code"));
        this->codeButton_->setObjectName(QStringLiteral("ghost"));
        this->blockButton_ = new QPushButton(QStringLiteral("Block"));
        this->blockButton_->setObjectName(QStringLiteral("danger"));
        this->deleteButton_ = new QPushButton(QStringLiteral("Delete"));
        this->deleteButton_->setObjectName(QStringLiteral("ghost"));
        head->addWidget(this->codeButton_);
        head->addWidget(this->blockButton_);
        head->addWidget(this->deleteButton_);
        layout->addWidget(this->head_);

        // ---- Banners
        auto banner = [](const QString &object, const QString &text,
                         QPushButton *&button, const QString &label,
                         const QString &buttonObject) {
            auto *box = new QWidget;
            box->setObjectName(object);
            box->setAttribute(Qt::WA_StyledBackground);
            auto *row = new QHBoxLayout(box);
            row->setContentsMargins(14, 10, 10, 10);
            auto *words = new QLabel(text);
            words->setWordWrap(true);
            button = new QPushButton(label);
            button->setObjectName(buttonObject);
            row->addWidget(words, 1);
            row->addWidget(button);
            return box;
        };
        auto *banners = new QVBoxLayout;
        banners->setContentsMargins(18, 10, 18, 0);
        this->requestBanner_ = banner(
            QStringLiteral("banner"),
            QStringLiteral("<b>Message request.</b> Accept to reply and get "
                           "notified when they message you."),
            this->acceptButton_, QStringLiteral("Accept"),
            QStringLiteral("primary"));
        this->keysBanner_ = banner(
            QStringLiteral("warn"),
            QStringLiteral("<b>Their security code changed.</b> That happens "
                           "on a new PC, but could mean someone is in the "
                           "middle. Check it with them before sending."),
            this->trustButton_, QStringLiteral("I checked it"),
            QStringLiteral(""));
        banners->addWidget(this->requestBanner_);
        banners->addWidget(this->keysBanner_);
        layout->addLayout(banners);

        // ---- Messages
        this->scroll_ = new QScrollArea;
        this->scroll_->setWidgetResizable(true);
        this->scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        this->scroll_->viewport()->setAutoFillBackground(true);
        {
            auto palette = this->scroll_->viewport()->palette();
            palette.setColor(QPalette::Window, QColor("#0f0a0c"));
            this->scroll_->viewport()->setPalette(palette);
        }
        layout->addWidget(this->scroll_, 1);

        // ---- Empty state
        this->empty_ = new QWidget;
        auto *emptyLayout = new QVBoxLayout(this->empty_);
        emptyLayout->setAlignment(Qt::AlignCenter);
        emptyLayout->setSpacing(10);
        auto *emptyIcon = new QLabel;
        emptyIcon->setPixmap(padlock(28, QColor("#8a787c")));
        emptyIcon->setAlignment(Qt::AlignCenter);
        auto *emptyTitle = new QLabel(QStringLiteral("No chat open"));
        emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
        emptyTitle->setAlignment(Qt::AlignCenter);
        auto *emptyText = new QLabel(QStringLiteral(
            "Pick a chat on the left, or start a new one. Messages are "
            "end-to-end encrypted, so Solrino can't read them."));
        emptyText->setObjectName(QStringLiteral("muted"));
        emptyText->setAlignment(Qt::AlignCenter);
        emptyText->setWordWrap(true);
        emptyText->setFixedWidth(340);
        auto *start = new QPushButton(QStringLiteral("New message"));
        start->setCursor(Qt::PointingHandCursor);
        start->setFixedHeight(34);
        emptyLayout->addWidget(emptyIcon, 0, Qt::AlignHCenter);
        emptyLayout->addSpacing(4);
        emptyLayout->addWidget(emptyTitle);
        emptyLayout->addWidget(emptyText, 0, Qt::AlignHCenter);
        emptyLayout->addSpacing(8);
        emptyLayout->addWidget(start, 0, Qt::AlignHCenter);
        layout->addWidget(this->empty_, 1);

        // ---- Replying to...
        this->replyBar_ = new QWidget;
        this->replyBar_->setObjectName(QStringLiteral("replyBar"));
        this->replyBar_->setAttribute(Qt::WA_StyledBackground);
        {
            auto *row = new QHBoxLayout(this->replyBar_);
            row->setContentsMargins(18, 6, 12, 6);
            this->replyText_ = new QLabel;
            this->replyText_->setObjectName(QStringLiteral("replyBarText"));
            this->replyText_->setTextFormat(Qt::PlainText);
            auto *close = new QPushButton(QStringLiteral("\u2715"));
            close->setObjectName(QStringLiteral("replyClose"));
            close->setToolTip(QStringLiteral("Cancel reply (Esc)"));
            close->setCursor(Qt::PointingHandCursor);
            row->addWidget(this->replyText_, 1);
            row->addWidget(close);
            QObject::connect(close, &QPushButton::clicked, this, [this] {
                this->cancelReply();
            });
        }
        this->replyBar_->hide();
        layout->addWidget(this->replyBar_);

        // ---- Composer
        this->composerBar_ = new QWidget;
        this->composerBar_->setObjectName(QStringLiteral("composerBar"));
        this->composerBar_->setAttribute(Qt::WA_StyledBackground);
        auto *bottom = new QHBoxLayout(this->composerBar_);
        bottom->setContentsMargins(16, 12, 16, 12);
        bottom->setSpacing(10);
        this->composer_ = new Composer;
        this->composer_->setFixedHeight(44);
        this->composer_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        this->sendButton_ = new QPushButton;
        this->sendButton_->setObjectName(QStringLiteral("round"));
        this->sendButton_->setFixedSize(38, 38);
        this->sendButton_->setIcon(sendIcon());
        this->sendButton_->setIconSize(QSize(18, 18));
        this->sendButton_->setCursor(Qt::PointingHandCursor);
        this->sendButton_->setToolTip(QStringLiteral("Send (Enter)"));
        this->attachButton_ = new QPushButton(QStringLiteral("+"));
        this->attachButton_->setObjectName(QStringLiteral("tool"));
        this->attachButton_->setFixedSize(34, 34);
        this->attachButton_->setToolTip(
            QStringLiteral("Send a photo, video or file (up to 25 MB)"));
        this->emoteButton_ = new QPushButton(QStringLiteral("\u263A"));
        this->emoteButton_->setObjectName(QStringLiteral("tool"));
        this->emoteButton_->setFixedSize(34, 34);
        this->emoteButton_->setToolTip(QStringLiteral("7TV emotes"));
        bottom->addWidget(this->attachButton_);
        bottom->addWidget(this->composer_, 1);
        bottom->addWidget(this->emoteButton_);
        bottom->addWidget(this->sendButton_);
        QObject::connect(this->attachButton_, &QPushButton::clicked, this,
                         [this] {
                             this->attachFile();
                         });
        QObject::connect(this->emoteButton_, &QPushButton::clicked, this,
                         [this] {
                             this->openPicker();
                         });
        layout->addWidget(this->composerBar_);

        // ---- Wiring
        QObject::connect(start, &QPushButton::clicked, this, [this] {
            bool ok = false;
            const auto name = QInputDialog::getText(
                this, QStringLiteral("New message"),
                QStringLiteral("Their Twitch name:"), QLineEdit::Normal, {},
                &ok);
            if (ok && !name.trimmed().isEmpty())
            {
                this->startWith(name);
            }
        });
        this->composer_->onSend = [this] {
            this->sendNow();
        };
        this->composer_->onEscape = [this] {
            this->cancelReply();
        };
        QObject::connect(this->sendButton_, &QPushButton::clicked, this,
                         [this] {
                             this->sendNow();
                         });
        QObject::connect(this->acceptButton_, &QPushButton::clicked, this,
                         [this] {
                             Messages::instance().accept(this->current_);
                             this->showRequests_ = false;
                             this->refresh();
                         });
        QObject::connect(this->trustButton_, &QPushButton::clicked, this,
                         [this] {
                             Messages::instance().trustNewKeys(this->current_);
                         });
        QObject::connect(this->codeButton_, &QPushButton::clicked, this,
                         [this] {
                             this->showCode();
                         });
        QObject::connect(this->blockButton_, &QPushButton::clicked, this,
                         [this] {
                             this->toggleBlock();
                         });
        QObject::connect(this->deleteButton_, &QPushButton::clicked, this,
                         [this] {
                             if (QMessageBox::question(
                                     this, QStringLiteral("Delete chat"),
                                     QStringLiteral(
                                         "Delete this chat from this PC? It "
                                         "can't be brought back.")) !=
                                 QMessageBox::Yes)
                             {
                                 return;
                             }
                             Messages::instance().remove(this->current_);
                             this->current_.clear();
                             this->refresh();
                         });
        return main;
    }

    void toggleBlock()
    {
        const auto *c = Messages::instance().conversation(this->current_);
        const bool block = c == nullptr || !c->blocked;
        if (block && QMessageBox::question(
                         this, QStringLiteral("Block"),
                         QStringLiteral("Block them? Their messages won't "
                                        "reach you, and they won't know.")) !=
                         QMessageBox::Yes)
        {
            return;
        }
        Messages::instance().setBlocked(this->current_, block);
    }

    void sendNow()
    {
        const auto text = this->composer_->toPlainText().trimmed();
        if (text.isEmpty() || this->current_.isEmpty() ||
            !this->sendButton_->isEnabled())
        {
            return;
        }
        const auto *c = Messages::instance().conversation(this->current_);
        const auto login =
            c != nullptr && !c->login.isEmpty() ? c->login : this->pendingLogin_;
        Messages::instance().send(this->current_, login, text, this->replyTo_);
        this->composer_->clear();
        this->cancelReply();
    }

    /// A short line about a message, for reply quotes
    QString describe(const Messages::Message &m) const
    {
        const auto *c = Messages::instance().conversation(this->current_);
        const auto who = m.mine ? QStringLiteral("you")
                                : (c != nullptr && !c->login.isEmpty()
                                       ? c->login
                                       : QStringLiteral("them"));
        auto what = m.file ? m.file->name : m.text;
        what = what.simplified();
        if (what.size() > 70)
        {
            what = what.left(70) + QStringLiteral("...");
        }
        return who + QStringLiteral(": ") + what;
    }

    const Messages::Message *findMessage(const QString &id) const
    {
        const auto *c = Messages::instance().conversation(this->current_);
        if (c == nullptr || id.isEmpty())
        {
            return nullptr;
        }
        for (const auto &m : c->messages)
        {
            if (m.id == id)
            {
                return &m;
            }
        }
        return nullptr;
    }

    void startReply(const Messages::Message &m)
    {
        if (!this->sendButton_->isEnabled())
        {
            return;
        }
        this->replyTo_ = m.id;
        this->replyFor_ = this->current_;
        this->replyText_->setText(QStringLiteral("Replying to ") +
                                  this->describe(m));
        this->replyBar_->show();
        this->composer_->setFocus();
    }

    void cancelReply()
    {
        this->replyTo_.clear();
        this->replyFor_.clear();
        this->replyBar_->hide();
    }

    void showCode()
    {
        QPointer<MessagesWindow> self(this);
        const auto userId = this->current_;
        Messages::instance().securityCode(userId, [self, userId](QString code) {
            if (self == nullptr)
            {
                return;
            }
            if (code.isEmpty())
            {
                QMessageBox::warning(self, QStringLiteral("Security code"),
                                     QStringLiteral("Couldn't get the code. "
                                                    "Try again in a moment."));
                return;
            }
            const auto *c = Messages::instance().conversation(userId);
            QMessageBox box(self);
            box.setWindowTitle(QStringLiteral("Security code"));
            box.setIconPixmap(padlock(48, QColor("#8fd19e")));
            box.setTextFormat(Qt::RichText);
            box.setText(
                QStringLiteral("<p>Your security code with <b>%1</b>:</p>"
                               "<p style='font-size:20px; font-family:Consolas,"
                               "monospace; letter-spacing:2px'>%2</p>"
                               "<p>Compare it with them somewhere else, like "
                               "on stream or in a call. If you both see the "
                               "same code, your messages are only between you "
                               "two.</p>")
                    .arg((c != nullptr ? c->login : userId).toHtmlEscaped(),
                         code.toHtmlEscaped()));
            box.exec();
        });
    }

    void refresh()
    {
        if (!this->replyFor_.isEmpty() && this->replyFor_ != this->current_)
        {
            this->cancelReply();
        }
        auto &messages = Messages::instance();
        const int requests = messages.requestCount();
        this->requestsTab_->setText(
            requests > 0 ? QStringLiteral("Requests  %1").arg(requests)
                         : QStringLiteral("Requests"));
        this->chatsTab_->setChecked(!this->showRequests_);
        this->requestsTab_->setChecked(this->showRequests_);
        this->status_->setText(messages.problem());
        this->status_->setVisible(!messages.problem().isEmpty());
        this->hello_->setText(messages.myLogin().isEmpty()
                                  ? QStringLiteral("Not signed in")
                                  : QStringLiteral("Signed in as %1")
                                        .arg(messages.myLogin()));

        // The list, newest chat first
        const auto filter = this->search_->text().trimmed().toLower();
        std::vector<const Messages::Conversation *> shown;
        for (const auto &c : messages.conversations())
        {
            if (c.messages.empty() && c.userId != this->current_)
            {
                continue;
            }
            const bool request =
                !c.accepted && !c.blocked && !c.messages.empty();
            if (this->showRequests_ != request)
            {
                continue;
            }
            if (!filter.isEmpty() && !c.login.toLower().contains(filter))
            {
                continue;
            }
            shown.push_back(&c);
        }
        std::sort(shown.begin(), shown.end(), [](auto *a, auto *b) {
            const auto at =
                a->messages.empty() ? QDateTime() : a->messages.back().at;
            const auto bt =
                b->messages.empty() ? QDateTime() : b->messages.back().at;
            return at > bt;
        });

        const QSignalBlocker blocker(this->list_);
        this->list_->clear();
        this->rows_.clear();
        int selected = -1;
        for (const auto *c : shown)
        {
            auto *item = new QListWidgetItem;
            item->setSizeHint(QSize(0, 58));
            this->list_->addItem(item);
            this->list_->setItemWidget(item, chatRow(*c));
            if (c->userId == this->current_)
            {
                selected = static_cast<int>(this->rows_.size());
            }
            this->rows_.push_back(c->userId);
        }
        if (selected >= 0)
        {
            this->list_->setCurrentRow(selected);
        }
        this->list_->setVisible(!shown.empty());
        this->listEmpty_->setVisible(shown.empty());
        this->listEmpty_->setText(
            !filter.isEmpty()   ? QStringLiteral("No chats match that.")
            : this->showRequests_ ? QStringLiteral("No message requests.")
                                  : QStringLiteral("No chats yet.\nPress New to "
                                                   "message someone."));

        this->drawConversation();
    }

    QStringList people() const
    {
        return {Messages::instance().myUserId(), this->current_};
    }

    int bubbleWidth() const
    {
        return std::max(260, int(this->scroll_->viewport()->width() * 0.68));
    }

    QWidget *bubble(const Messages::Message &m, const QString &receipt)
    {
        auto *row = new HoverRow;
        auto *layout = new QVBoxLayout(row);
        layout->setContentsMargins(0, 2, 0, 2);
        layout->setSpacing(2);
        const auto align = m.mine ? Qt::AlignRight : Qt::AlignLeft;
        if (!m.replyTo.isEmpty())
        {
            const auto *original = this->findMessage(m.replyTo);
            auto *quote = new QLabel(
                QStringLiteral("\u21AA ") +
                (original != nullptr ? this->describe(*original)
                                     : QStringLiteral("an older message")));
            quote->setObjectName(QStringLiteral("replyQuote"));
            quote->setTextFormat(Qt::PlainText);
            quote->setMaximumWidth(this->bubbleWidth());
            layout->addWidget(quote, 0, align);
        }
        const auto message = m;
        QPointer<MessagesWindow> self(this);
        auto reply = [self, message] {
            if (self != nullptr)
            {
                self->startReply(message);
            }
        };
        QWidget *body = nullptr;
        if (m.file)
        {
            body = fileBubble(m, this->bubbleWidth(), this);
        }
        else
        {
            auto *text = new TextBubble(m.text, m.mine, this->people(),
                                        this->bubbleWidth());
            text->onReply = reply;
            body = text;
        }
        if (m.status == Messages::Status::Sent)
        {
            // The bubble with a small Reply button beside it, shown on hover
            auto *line = new QWidget;
            auto *lineLayout = new QHBoxLayout(line);
            lineLayout->setContentsMargins(0, 0, 0, 0);
            lineLayout->setSpacing(6);
            auto *replyButton = new QPushButton(QStringLiteral("\u21A9"));
            replyButton->setObjectName(QStringLiteral("replyButton"));
            replyButton->setFixedSize(24, 24);
            replyButton->setToolTip(QStringLiteral("Reply"));
            replyButton->setCursor(Qt::PointingHandCursor);
            // Keeps its space while hidden, so the bubble doesn't jump
            auto policy = replyButton->sizePolicy();
            policy.setRetainSizeWhenHidden(true);
            replyButton->setSizePolicy(policy);
            replyButton->hide();
            QObject::connect(replyButton, &QPushButton::clicked, replyButton,
                             reply);
            if (m.mine)
            {
                lineLayout->addWidget(replyButton, 0, Qt::AlignVCenter);
                lineLayout->addWidget(body);
            }
            else
            {
                lineLayout->addWidget(body);
                lineLayout->addWidget(replyButton, 0, Qt::AlignVCenter);
            }
            row->reveal = replyButton;
            body = line;
        }
        QString note =
            QLocale().toString(m.at.toLocalTime().time(), QLocale::ShortFormat);
        if (m.status == Messages::Status::Sending)
        {
            note += m.file ? QStringLiteral(" \u00B7 Uploading...")
                           : QStringLiteral(" \u00B7 Sending...");
        }
        else if (m.status == Messages::Status::Failed)
        {
            note += QStringLiteral(" \u00B7 Didn't send");
        }
        else if (!receipt.isEmpty())
        {
            note += QStringLiteral(" \u00B7 ") + receipt;
        }
        auto *time = new QLabel(note);
        time->setObjectName(QStringLiteral("time"));
        if (m.status == Messages::Status::Failed)
        {
            time->setStyleSheet(QStringLiteral("color:#ff7b72;"));
        }
        layout->addWidget(body, 0, align);
        layout->addWidget(time, 0, align);
        return row;
    }

    void attachFile()
    {
        if (this->current_.isEmpty() || !this->sendButton_->isEnabled())
        {
            return;
        }
        const auto path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Send a photo, video or file"), {},
            QStringLiteral("Photos and videos (*.png *.jpg *.jpeg *.gif *.webp "
                           "*.bmp *.mp4 *.webm *.mov *.mkv);;All files (*)"));
        if (path.isEmpty())
        {
            return;
        }
        const auto *c = Messages::instance().conversation(this->current_);
        const auto login =
            c != nullptr && !c->login.isEmpty() ? c->login : this->pendingLogin_;
        QPointer<MessagesWindow> self(this);
        Messages::instance().sendFile(this->current_, login, path,
                                      [self](QString error) {
                                          if (self != nullptr &&
                                              !error.isEmpty())
                                          {
                                              self->status_->setText(error);
                                              self->status_->show();
                                          }
                                      });
    }

    void openPicker()
    {
        auto *picker = new EmotePicker(this, this->people());
        picker->picked = [this](const QString &name) {
            auto cursor = this->composer_->textCursor();
            const auto before = this->composer_->toPlainText();
            const bool needsSpace = cursor.position() > 0 &&
                                    !before.at(cursor.position() - 1).isSpace();
            cursor.insertText((needsSpace ? QStringLiteral(" ") : QString()) +
                              name + u' ');
            this->composer_->setFocus();
        };
        const auto at = this->emoteButton_->mapToGlobal(QPoint(0, 0));
        picker->move(at.x() - picker->width() + this->emoteButton_->width(),
                     at.y() - picker->height() - 6);
        picker->show();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        this->later_.start();
    }

    void drawConversation()
    {
        auto &messages = Messages::instance();
        const auto *c = messages.conversation(this->current_);
        const bool open = !this->current_.isEmpty();
        this->head_->setVisible(open);
        this->scroll_->setVisible(open);
        this->composerBar_->setVisible(open);
        this->empty_->setVisible(!open);
        this->requestBanner_->setVisible(open && c != nullptr && !c->accepted &&
                                         !c->blocked && !c->messages.empty());
        this->keysBanner_->setVisible(open && c != nullptr && c->keysChanged);
        if (!open)
        {
            return;
        }
        const auto login = c != nullptr && !c->login.isEmpty()
                               ? c->login
                               : this->pendingLogin_;
        this->peer_->setText(login);
        this->peerPic_->setPixmap(
            Avatars::instance().get(this->current_, login, 40));
        this->blockButton_->setText(c != nullptr && c->blocked
                                        ? QStringLiteral("Unblock")
                                        : QStringLiteral("Block"));
        const bool canSend = messages.ready() &&
                             (c == nullptr || (!c->blocked && !c->keysChanged));
        this->sendButton_->setEnabled(canSend);
        this->composer_->setEnabled(canSend);
        this->attachButton_->setEnabled(canSend);
        this->emoteButton_->setEnabled(canSend);
        Emotes::instance().want(Messages::instance().myUserId());
        Emotes::instance().want(this->current_);
        this->composer_->setPlaceholderText(
            c != nullptr && c->blocked
                ? QStringLiteral("Unblock them to send a message")
                : QStringLiteral("Message %1").arg(login));

        // The messages, with a date between days
        auto *page = new QWidget;
        page->setObjectName(QStringLiteral("page"));
        page->setAttribute(Qt::WA_StyledBackground);
        auto *list = new QVBoxLayout(page);
        list->setContentsMargins(18, 14, 18, 14);
        list->setSpacing(4);
        list->addStretch();
        if (c == nullptr || c->messages.empty())
        {
            auto *hint = new QLabel(
                QStringLiteral("This is the start of your chat with %1. Say "
                               "hi! Only the two of you can read it.")
                    .arg(login));
            hint->setObjectName(QStringLiteral("muted"));
            hint->setAlignment(Qt::AlignCenter);
            hint->setWordWrap(true);
            list->addWidget(hint);
        }
        else
        {
            // "Sent" or "Seen" goes under my newest message only
            const Messages::Message *lastMine = nullptr;
            for (const auto &m : c->messages)
            {
                if (m.mine)
                {
                    lastMine = &m;
                }
            }
            QDate lastDay;
            for (const auto &m : c->messages)
            {
                const auto day = m.at.toLocalTime().date();
                if (day != lastDay)
                {
                    lastDay = day;
                    auto *label = new QLabel(
                        day == QDate::currentDate()
                            ? QStringLiteral("Today")
                            : QLocale().toString(day, QLocale::LongFormat));
                    label->setObjectName(QStringLiteral("day"));
                    list->addSpacing(6);
                    list->addWidget(label, 0, Qt::AlignHCenter);
                    list->addSpacing(4);
                }
                QString receipt;
                if (&m == lastMine && m.status == Messages::Status::Sent)
                {
                    receipt = c->theirSeenAt.isValid() && c->theirSeenAt >= m.at
                                  ? QStringLiteral("Seen")
                                  : QStringLiteral("Sent");
                }
                list->addWidget(this->bubble(m, receipt));
            }
        }
        this->scroll_->setWidget(page);
        QTimer::singleShot(0, this, [this] {
            this->scroll_->verticalScrollBar()->setValue(
                this->scroll_->verticalScrollBar()->maximum());
        });

        if (c != nullptr && c->unread > 0 && c->accepted &&
            this->isActiveWindow())
        {
            QTimer::singleShot(0, this, [id = this->current_] {
                Messages::instance().markRead(id);
            });
        }
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::WindowActivate &&
            !this->current_.isEmpty())
        {
            const auto *c = Messages::instance().conversation(this->current_);
            if (c != nullptr && c->unread > 0 && c->accepted)
            {
                Messages::instance().markRead(this->current_);
            }
        }
        return QWidget::event(event);
    }

    QLabel *hello_{};
    QLineEdit *search_{};
    QPushButton *chatsTab_{};
    QPushButton *requestsTab_{};
    QListWidget *list_{};
    QLabel *listEmpty_{};
    QLabel *status_{};
    QWidget *head_{};
    QLabel *peerPic_{};
    QLabel *peer_{};
    QPushButton *codeButton_{};
    QPushButton *blockButton_{};
    QPushButton *deleteButton_{};
    QWidget *requestBanner_{};
    QPushButton *acceptButton_{};
    QWidget *keysBanner_{};
    QPushButton *trustButton_{};
    QScrollArea *scroll_{};
    QWidget *empty_{};
    QWidget *composerBar_{};
    Composer *composer_{};
    QPushButton *sendButton_{};
    QWidget *replyBar_{};
    QLabel *replyText_{};
    QString replyTo_;
    QString replyFor_;
    QPushButton *attachButton_{};
    QPushButton *emoteButton_{};
    QTimer later_;

    bool showRequests_ = false;
    QString current_;
    QString pendingLogin_;
    std::vector<QString> rows_;
};

QPointer<MessagesWindow> window;

}  // namespace

void openSolrinoMessages(const QString &login)
{
    if (window == nullptr)
    {
        window = new MessagesWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
    }
    window->show();
    window->raise();
    window->activateWindow();
    if (!login.trimmed().isEmpty())
    {
        window->startWith(login);
    }
}

QPixmap solrinoMessagesIcon(int size, bool unread)
{
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal s = size;
    QPainterPath bubble;
    bubble.addRoundedRect(QRectF(s * 0.12, s * 0.18, s * 0.72, s * 0.52),
                          s * 0.16, s * 0.16);
    QPainterPath tail;
    tail.moveTo(s * 0.3, s * 0.66);
    tail.lineTo(s * 0.24, s * 0.86);
    tail.lineTo(s * 0.48, s * 0.68);
    bubble = bubble.united(tail);
    painter.setPen(QPen(QColor(230, 220, 216), s * 0.08));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(bubble);
    if (unread)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(227, 16, 28));
        painter.drawEllipse(QPointF(s * 0.8, s * 0.22), s * 0.16, s * 0.16);
    }
    return pixmap;
}

}  // namespace chatterino

#include "widgets/dialogs/SolrinoUpdatePrompt.hpp"

#include "widgets/helper/SolrinoTransitions.hpp"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QScreen>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace chatterino {

namespace {

constexpr int CARD_WIDTH = 460;
constexpr int SHADOW = 24;
constexpr double RADIUS = 16;
constexpr int PAD = 24;
constexpr int LOGO = 58;
constexpr int BUTTON_HEIGHT = 36;
constexpr int NOTE_MAX_HEIGHT = 96;
/// The sectioned list can grow taller than a paragraph, but not past this
constexpr int CHANGES_MAX_HEIGHT = 380;
constexpr int BULLET_INDENT = 16;

const QColor WHITE(248, 241, 239);
const QColor MUTED(186, 164, 162);
const QColor RED(227, 16, 28);

QFont uiFont(int pixels, bool strong)
{
    QFont font(strong ? QStringLiteral("Segoe UI Semibold")
                      : QStringLiteral("Segoe UI"));
    font.setPixelSize(pixels);
    return font;
}

QString defaultNotes()
{
    return QStringLiteral(
        "It only takes a few seconds, and your chats and settings stay exactly "
        "as they are.");
}

/// The sections, in the order they're shown
struct Section {
    QString kind;
    QString label;
    QColor text;
    QColor fill;
    QColor border;
};

const std::array<Section, 4> &sections()
{
    static const std::array<Section, 4> list{{
        {"new", "NEW", QColor(139, 227, 168), QColor(31, 143, 74, 60),
         QColor(63, 191, 109, 100)},
        {"fixed", "FIXED", QColor(156, 194, 255), QColor(47, 111, 214, 60),
         QColor(91, 143, 230, 100)},
        {"changed", "CHANGED", QColor(255, 196, 120), QColor(200, 120, 20, 50),
         QColor(255, 180, 84, 90)},
        {"removed", "REMOVED", MUTED, QColor(255, 255, 255, 16),
         QColor(255, 255, 255, 34)},
    }};
    return list;
}

const Section &sectionFor(const QString &kind)
{
    for (const auto &section : sections())
    {
        if (section.kind == kind)
        {
            return section;
        }
    }
    return sections()[2];
}

double wrappedHeight(const QFont &font, int width, const QString &text)
{
    return std::ceil(QFontMetricsF(font)
                         .boundingRect(QRectF(0, 0, width, 1000),
                                       Qt::TextWordWrap, text)
                         .height());
}

}  // namespace

SolrinoUpdatePrompt::SolrinoUpdatePrompt(const QString &version,
                                         const QString &notes,
                                         std::function<void(Choice)> onChoice,
                                         QVector<Change> changes)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint |
                           Qt::NoDropShadowWindowHint)
    , version_(version)
    , notes_(notes.isEmpty() ? defaultNotes() : notes)
    , changes_(std::move(changes))
    , onChoice_(std::move(onChoice))
    , logo_(QStringLiteral(":/icon.png"))
{
    this->setAttribute(Qt::WA_TranslucentBackground);
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setAttribute(Qt::WA_ShowWithoutActivating);
    this->setAttribute(Qt::WA_QuitOnClose, false);
    this->setMouseTracking(true);
    this->setWindowTitle(QStringLiteral("Solrino update"));

    const int textWidth = CARD_WIDTH - PAD * 2;
    // Anything taller than the card's limit scrolls
    this->contentHeight_ =
        this->changes_.isEmpty()
            ? int(wrappedHeight(uiFont(13, false), textWidth, this->notes_))
            : this->layoutChanges(textWidth);
    const int noteHeight = std::min(
        this->changes_.isEmpty() ? NOTE_MAX_HEIGHT : CHANGES_MAX_HEIGHT,
        this->contentHeight_);
    const int header = PAD + LOGO;
    const int cardHeight =
        header + 16 + noteHeight + 8 + 18 + 18 + BUTTON_HEIGHT + PAD;

    this->setFixedSize(CARD_WIDTH + SHADOW * 2, cardHeight + SHADOW * 2);
    this->card_ = QRectF(SHADOW, SHADOW, CARD_WIDTH, cardHeight);
    this->notesArea_ = QRectF(this->card_.left() + PAD,
                              this->card_.top() + header + 16, textWidth,
                              noteHeight);

    const double buttonsTop = this->card_.bottom() - PAD - BUTTON_HEIGHT;
    const double right = this->card_.right() - PAD;
    this->controls_[UpdateNow] =
        QRectF(right - 120, buttonsTop, 120, BUTTON_HEIGHT);
    this->controls_[UpdateLater] =
        QRectF(right - 120 - 8 - 110, buttonsTop, 110, BUTTON_HEIGHT);
    this->controls_[RemindLater] =
        QRectF(right - 120 - 8 - 110 - 8 - 130, buttonsTop, 130, BUTTON_HEIGHT);
    this->controls_[Close] =
        QRectF(this->card_.right() - 38, this->card_.top() + 10, 28, 28);

    this->timer_.setTimerType(Qt::PreciseTimer);
    this->timer_.setInterval(16);
    QObject::connect(&this->timer_, &QTimer::timeout, this, [this] {
        this->tick();
    });
    this->timer_.start();
    this->clock_.start();

    if (auto *screen = QGuiApplication::primaryScreen())
    {
        const auto area = screen->availableGeometry();
        this->move(area.center() - this->rect().center());
    }

    solrino::fadeInWindow(this, 18);
}

void SolrinoUpdatePrompt::setReady()
{
    this->ready_ = true;
    this->waiting_ = false;
    this->failed_ = false;
    this->update();
}

void SolrinoUpdatePrompt::showProblem(const QString &problem)
{
    this->ready_ = false;
    this->waiting_ = false;
    this->failed_ = true;
    this->notes_ = problem;
    this->contentHeight_ = int(wrappedHeight(
        uiFont(13, false), int(this->notesArea_.width()), this->notes_));
    this->scrollTo(0, true);
    this->update();
}

int SolrinoUpdatePrompt::layoutChanges(int width)
{
    const auto titleFont = uiFont(13, true);
    const auto detailFont = uiFont(12, false);
    const auto tagFont = uiFont(10, true);
    const int textWidth = width - BULLET_INDENT;

    double y = 0;
    bool first = true;
    for (const auto &section : sections())
    {
        QVector<const Change *> inSection;
        for (const auto &change : this->changes_)
        {
            if (&sectionFor(change.kind) == &section)
            {
                inSection.push_back(&change);
            }
        }
        if (inSection.isEmpty())
        {
            continue;
        }

        if (!first)
        {
            y += 12;
        }
        first = false;

        const double tagWidth =
            QFontMetricsF(tagFont).horizontalAdvance(section.label) + 18;
        this->pieces_.push_back(
            {Piece::Kind::Tag, QRectF(0, y, tagWidth, 19), section.label,
             section.kind});
        y += 19 + 8;

        for (const auto *change : inSection)
        {
            const double titleHeight =
                wrappedHeight(titleFont, textWidth, change->title);
            this->pieces_.push_back({Piece::Kind::Bullet,
                                     QRectF(3, y + 6, 6, 6), {}, section.kind});
            this->pieces_.push_back(
                {Piece::Kind::Title,
                 QRectF(BULLET_INDENT, y, textWidth, titleHeight),
                 change->title, section.kind});
            y += titleHeight;

            if (!change->detail.isEmpty())
            {
                const double detailHeight =
                    wrappedHeight(detailFont, textWidth, change->detail);
                y += 1;
                this->pieces_.push_back(
                    {Piece::Kind::Detail,
                     QRectF(BULLET_INDENT, y, textWidth, detailHeight),
                     change->detail, section.kind});
                y += detailHeight;
            }
            y += 9;
        }
        y -= 9;
    }
    return int(std::ceil(y));
}

void SolrinoUpdatePrompt::drawChanges(QPainter &p)
{
    const auto origin = this->notesArea_.topLeft();
    for (const auto &piece : this->pieces_)
    {
        const auto &section = sectionFor(piece.changeKind);
        const auto rect = piece.rect.translated(origin);
        switch (piece.kind)
        {
            case Piece::Kind::Tag:
                p.setPen(QPen(section.border, 1));
                p.setBrush(section.fill);
                p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
                p.setFont(uiFont(10, true));
                p.setPen(section.text);
                p.drawText(rect, Qt::AlignCenter, piece.text);
                break;

            case Piece::Kind::Bullet:
                p.setPen(Qt::NoPen);
                p.setBrush(section.text);
                p.drawEllipse(rect);
                break;

            case Piece::Kind::Title:
                p.setFont(uiFont(13, true));
                p.setPen(WHITE);
                p.drawText(rect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                           piece.text);
                break;

            case Piece::Kind::Detail:
                p.setFont(uiFont(12, false));
                p.setPen(MUTED);
                p.drawText(rect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                           piece.text);
                break;
        }
    }
}

double SolrinoUpdatePrompt::maxScroll() const
{
    return std::max(0.0, this->contentHeight_ - this->notesArea_.height());
}

void SolrinoUpdatePrompt::scrollTo(double offset, bool instant)
{
    this->scrollTarget_ = std::clamp(offset, 0.0, this->maxScroll());
    if (instant || !solrino::smoothAnimationsEnabled())
    {
        this->scroll_ = this->scrollTarget_;
    }
    this->update();
}

QRectF SolrinoUpdatePrompt::scrollTrack() const
{
    // In the card's right padding, beside the list
    return {this->notesArea_.right() + 9, this->notesArea_.top(), 4,
            this->notesArea_.height()};
}

QRectF SolrinoUpdatePrompt::scrollThumb() const
{
    const auto track = this->scrollTrack();
    const double height = std::max(
        28.0, track.height() * track.height() / this->contentHeight_);
    const double travel = track.height() - height;
    const double max = this->maxScroll();
    return {track.left(),
            track.top() + (max > 0 ? travel * this->scroll_ / max : 0.0),
            track.width(), height};
}

void SolrinoUpdatePrompt::drawNotes(QPainter &p)
{
    // Drawn on its own layer, so the edges can fade out where it scrolls
    const double dpr = this->devicePixelRatioF();
    QPixmap layer((this->notesArea_.size() * dpr).toSize());
    layer.setDevicePixelRatio(dpr);
    layer.fill(Qt::transparent);
    {
        QPainter lp(&layer);
        lp.setRenderHints(p.renderHints());
        lp.translate(-this->notesArea_.topLeft());
        lp.translate(0, -this->scroll_);
        if (!this->changes_.isEmpty() && !this->failed_)
        {
            this->drawChanges(lp);
        }
        else
        {
            lp.setFont(uiFont(13, false));
            lp.setPen(MUTED);
            lp.drawText(
                QRectF(this->notesArea_.topLeft(),
                       QSizeF(this->notesArea_.width(),
                              std::max<double>(this->contentHeight_,
                                               this->notesArea_.height()))),
                Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, this->notes_);
        }

        const double height = this->notesArea_.height();
        const double fade = std::min(28.0, height / 4);
        const bool moreAbove = this->scroll_ > 0.5;
        const bool moreBelow = this->scroll_ < this->maxScroll() - 0.5;
        if (moreAbove || moreBelow)
        {
            lp.resetTransform();
            lp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            QLinearGradient mask(0, 0, 0, height);
            mask.setColorAt(0, QColor(0, 0, 0, moreAbove ? 0 : 255));
            mask.setColorAt(fade / height, Qt::black);
            mask.setColorAt(1 - (fade / height), Qt::black);
            mask.setColorAt(1, QColor(0, 0, 0, moreBelow ? 0 : 255));
            lp.fillRect(QRectF(0, 0, this->notesArea_.width(), height), mask);
        }
    }
    p.drawPixmap(this->notesArea_.topLeft(), layer);
}

void SolrinoUpdatePrompt::drawScrollBar(QPainter &p)
{
    if (this->maxScroll() <= 0)
    {
        return;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 18));
    p.drawRoundedRect(this->scrollTrack(), 2, 2);
    const bool active = this->thumbHovered_ || this->draggingThumb_;
    p.setBrush(active ? QColor(255, 110, 114, 220)
                      : QColor(255, 255, 255, 90));
    p.drawRoundedRect(this->scrollThumb(), 2, 2);
}

void SolrinoUpdatePrompt::wheelEvent(QWheelEvent *event)
{
    if (this->maxScroll() <= 0)
    {
        event->ignore();
        return;
    }
    const auto pixels = event->pixelDelta();
    const double step = !pixels.isNull()
                            ? double(pixels.y())
                            : double(event->angleDelta().y()) / 120.0 * 52.0;
    this->scrollTo(this->scrollTarget_ - step, !pixels.isNull());
    event->accept();
}

void SolrinoUpdatePrompt::tick()
{
    // Hover eases in and out; the glow behind the logo breathes slowly
    for (int i = 0; i < ControlCount; ++i)
    {
        const double target = this->hovered_ == i ? 1.0 : 0.0;
        this->hover_[i] += (target - this->hover_[i]) *
                           (solrino::smoothAnimationsEnabled() ? 0.22 : 1.0);
    }
    this->scroll_ += (this->scrollTarget_ - this->scroll_) *
                     (solrino::smoothAnimationsEnabled() ? 0.3 : 1.0);
    if (std::abs(this->scrollTarget_ - this->scroll_) < 0.25)
    {
        this->scroll_ = this->scrollTarget_;
    }
    this->update();
}

int SolrinoUpdatePrompt::controlAt(const QPointF &position) const
{
    for (int i = 0; i < ControlCount; ++i)
    {
        if (this->controls_[i].contains(position))
        {
            return i;
        }
    }
    return -1;
}

void SolrinoUpdatePrompt::drawButton(QPainter &painter, const QRectF &area,
                                     const QString &text, bool primary,
                                     double hover)
{
    QPainterPath path;
    path.addRoundedRect(area, 9, 9);
    painter.setPen(Qt::NoPen);

    if (primary)
    {
        // A soft glow that grows under the mouse
        for (int i = 4; i > 0; --i)
        {
            auto glow = QColor(255, 40, 48);
            glow.setAlphaF((0.05 + 0.07 * hover));
            painter.setBrush(glow);
            const auto outer = area.adjusted(-i * 1.5, -i * 1.5 + 1, i * 1.5,
                                             i * 1.5 + 1);
            painter.drawRoundedRect(outer, 9 + i * 1.5, 9 + i * 1.5);
        }
        QLinearGradient fill(area.topLeft(), area.bottomLeft());
        fill.setColorAt(0, QColor(236 + int(19 * hover), 30 + int(26 * hover),
                                  42 + int(26 * hover)));
        fill.setColorAt(1, QColor(178 + int(30 * hover), 12 + int(10 * hover),
                                  24 + int(10 * hover)));
        painter.fillPath(path, fill);
    }
    else
    {
        painter.fillPath(path, QColor(255, 255, 255, 12 + int(20 * hover)));
        painter.setPen(QPen(QColor(255, 255, 255, 30 + int(30 * hover)), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(area.adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);
    }

    painter.setPen(primary ? QColor(255, 255, 255) : WHITE);
    painter.setFont(uiFont(13, true));
    painter.drawText(area, Qt::AlignCenter, text);
}

void SolrinoUpdatePrompt::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::TextAntialiasing);

    const auto &card = this->card_;

    // Soft drop shadow
    p.setPen(Qt::NoPen);
    for (int i = SHADOW; i > 0; i -= 2)
    {
        p.setBrush(QColor(0, 0, 0, 9));
        const auto r = card.adjusted(-i, -i + 5, i, i + 5);
        p.drawRoundedRect(r, RADIUS + i, RADIUS + i);
    }

    QPainterPath path;
    path.addRoundedRect(card, RADIUS, RADIUS);

    QLinearGradient background(card.topLeft(), card.bottomLeft());
    background.setColorAt(0, QColor(36, 15, 17));
    background.setColorAt(1, QColor(14, 8, 9));
    p.fillPath(path, background);

    // Ember glow behind the logo, breathing slowly
    const double breath =
        0.5 + 0.5 * std::sin(double(this->clock_.elapsed()) / 900.0);
    const QPointF logoCenter(card.left() + PAD + LOGO / 2.0,
                             card.top() + PAD + LOGO / 2.0);
    p.save();
    p.setClipPath(path);
    QRadialGradient glow(logoCenter, 190);
    glow.setColorAt(0, QColor(220, 24, 36, 96 + int(44 * breath)));
    glow.setColorAt(1, QColor(214, 20, 32, 0));
    p.fillRect(card, glow);

    // Thin bright line along the top edge
    QLinearGradient edge(card.topLeft(), card.topRight());
    edge.setColorAt(0, QColor(255, 80, 84, 0));
    edge.setColorAt(0.3, QColor(255, 80, 84, 210));
    edge.setColorAt(0.7, QColor(255, 80, 84, 60));
    edge.setColorAt(1, QColor(255, 80, 84, 0));
    p.fillRect(QRectF(card.left(), card.top(), card.width(), 1.5), edge);
    p.restore();

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 20), 1));
    p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), RADIUS, RADIUS);

    // Logo
    p.drawPixmap(QRectF(card.left() + PAD, card.top() + PAD, LOGO, LOGO),
                 this->logo_, QRectF(this->logo_.rect()));

    // "UPDATE READY" pill
    const double textLeft = card.left() + PAD + LOGO + 16;
    const auto pillFont = uiFont(10, true);
    const QString pillText = this->ready_ ? QStringLiteral("UPDATE READY")
                                          : QStringLiteral("NEW UPDATE");
    const QRectF pill(textLeft, card.top() + PAD + 2,
                      QFontMetricsF(pillFont).horizontalAdvance(pillText) + 18,
                      19);
    p.setPen(QPen(QColor(255, 80, 84, 100), 1));
    p.setBrush(QColor(150, 12, 22, 120));
    p.drawRoundedRect(pill, 9.5, 9.5);
    p.setFont(pillFont);
    p.setPen(QColor(255, 196, 194));
    p.drawText(pill, Qt::AlignCenter, pillText);

    // Version
    p.setFont(uiFont(23, true));
    p.setPen(WHITE);
    p.drawText(QPointF(textLeft, card.top() + PAD + 50),
               QStringLiteral("Solrino %1").arg(this->version_));

    // What's new: in sections when the update lists its changes, otherwise
    // one paragraph. A failed download shows why instead.
    this->drawNotes(p);
    this->drawScrollBar(p);

    // What "Update later" means
    p.setFont(uiFont(11, false));
    p.setPen(QColor(MUTED.red(), MUTED.green(), MUTED.blue(), 170));
    p.drawText(QRectF(card.left() + PAD, this->notesArea_.bottom() + 8,
                      card.width() - PAD * 2, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               this->waiting_
                   ? QStringLiteral("Downloading now. Solrino restarts by itself "
                                    "when it's done.")
                   : QStringLiteral(
                         "Update later installs it when you fully quit Solrino."));

    this->drawButton(p, this->controls_[RemindLater],
                     QStringLiteral("Remind me later"), false,
                     this->hover_[RemindLater]);
    this->drawButton(p, this->controls_[UpdateLater],
                     QStringLiteral("Update later"), false,
                     this->hover_[UpdateLater]);
    this->drawButton(p, this->controls_[UpdateNow],
                     this->waiting_  ? QStringLiteral("Downloading...")
                     : this->failed_ ? QStringLiteral("Try again")
                                     : QStringLiteral("Update now"),
                     true, this->waiting_ ? 0.0 : this->hover_[UpdateNow]);
    if (this->waiting_)
    {
        // A light passing along the button, since it can take a little while
        const auto &button = this->controls_[UpdateNow];
        QPainterPath shape;
        shape.addRoundedRect(button, 9, 9);
        const double phase =
            std::fmod(double(this->clock_.elapsed()) / 1100.0, 1.0);
        const double x = button.left() - 50 + phase * (button.width() + 100);
        QLinearGradient light(x, 0, x + 50, 0);
        light.setColorAt(0, QColor(255, 255, 255, 0));
        light.setColorAt(0.5, QColor(255, 255, 255, 80));
        light.setColorAt(1, QColor(255, 255, 255, 0));
        p.fillPath(shape, light);
    }

    // Close X
    const auto &close = this->controls_[Close];
    if (this->hover_[Close] > 0.01)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, int(26 * this->hover_[Close])));
        p.drawRoundedRect(close, 7, 7);
    }
    p.setPen(QPen(QColor(MUTED.red() + int(60 * this->hover_[Close]),
                         MUTED.green() + int(70 * this->hover_[Close]),
                         MUTED.blue() + int(70 * this->hover_[Close])),
                  1.5, Qt::SolidLine, Qt::RoundCap));
    const auto c = close.center();
    p.drawLine(c + QPointF(-4.5, -4.5), c + QPointF(4.5, 4.5));
    p.drawLine(c + QPointF(4.5, -4.5), c + QPointF(-4.5, 4.5));
}

void SolrinoUpdatePrompt::mouseMoveEvent(QMouseEvent *event)
{
    if (this->draggingThumb_)
    {
        const auto track = this->scrollTrack();
        const double travel = track.height() - this->scrollThumb().height();
        if (travel > 0)
        {
            const double top =
                event->position().y() - this->thumbGrab_ - track.top();
            this->scrollTo(top / travel * this->maxScroll(), true);
        }
        return;
    }
    if (this->dragging_)
    {
        this->move(event->globalPosition().toPoint() - this->dragOffset_);
        return;
    }
    const bool overThumb =
        this->maxScroll() > 0 &&
        this->scrollTrack().adjusted(-6, 0, 6, 0).contains(event->position());
    if (overThumb != this->thumbHovered_)
    {
        this->thumbHovered_ = overThumb;
        this->update();
    }
    this->hovered_ = this->controlAt(event->position());
    this->setCursor(this->hovered_ >= 0 ? Qt::PointingHandCursor
                                        : Qt::ArrowCursor);
}

void SolrinoUpdatePrompt::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
    {
        return;
    }
    // The scrollbar: grab the thumb, or click the track to jump a page
    if (this->maxScroll() > 0 &&
        this->scrollTrack().adjusted(-6, 0, 6, 0).contains(event->position()))
    {
        const auto thumb = this->scrollThumb();
        if (thumb.adjusted(-6, 0, 6, 0).contains(event->position()))
        {
            this->draggingThumb_ = true;
            this->thumbGrab_ = event->position().y() - thumb.top();
        }
        else
        {
            const double page = this->notesArea_.height() * 0.85;
            this->scrollTo(this->scrollTarget_ +
                           (event->position().y() < thumb.top() ? -page : page));
        }
        return;
    }
    this->pressed_ = this->controlAt(event->position());
    if (this->pressed_ < 0 && this->card_.contains(event->position()))
    {
        this->dragging_ = true;
        this->dragOffset_ = event->globalPosition().toPoint() - this->pos();
    }
}

void SolrinoUpdatePrompt::mouseReleaseEvent(QMouseEvent *event)
{
    this->dragging_ = false;
    if (this->draggingThumb_)
    {
        this->draggingThumb_ = false;
        this->update();
        return;
    }
    const int released = this->controlAt(event->position());
    const int pressed = this->pressed_;
    this->pressed_ = -1;
    if (released < 0 || released != pressed)
    {
        return;
    }

    const auto onChoice = this->onChoice_;
    if (released == UpdateNow)
    {
        if (this->ready_)
        {
            this->close();
            onChoice(Choice::UpdateNow);
        }
        else if (!this->waiting_)
        {
            // Stays up until the download is done, and then installs
            this->waiting_ = true;
            this->failed_ = false;
            onChoice(Choice::UpdateNow);
        }
        return;
    }
    solrino::fadeOutWindow(this);
    this->close();
    onChoice(released == UpdateLater ? Choice::UpdateLater
                                     : Choice::RemindLater);
}

void SolrinoUpdatePrompt::leaveEvent(QEvent * /*event*/)
{
    this->hovered_ = -1;
    this->thumbHovered_ = false;
    this->setCursor(Qt::ArrowCursor);
}

}  // namespace chatterino

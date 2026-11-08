#include "widgets/dialogs/SolrinoPaintGallery.hpp"

#include "providers/moltorino/SolrinoPaints.hpp"
#include "singletons/Theme.hpp"

#include <QButtonGroup>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>

namespace chatterino {

using namespace Qt::Literals::StringLiterals;

class SolrinoPaintTile final : public QWidget
{
public:
    SolrinoPaintTile(std::shared_ptr<SolrinoPaint> paint, QWidget *parent)
        : QWidget(parent)
        , paint_(std::move(paint))
    {
        this->setCursor(Qt::PointingHandCursor);
        this->setAttribute(Qt::WA_Hover, true);
        this->setToolTip(this->paint_ ? this->paint_->style().description
                                      : u"Wear no Solrino paint."_s);
    }

    std::function<void()> clicked;

    QString paintId() const
    {
        return this->paint_ ? this->paint_->style().id : QString();
    }

    bool animated() const
    {
        return this->paint_ && this->paint_->animated();
    }

    void setIdentity(const QString &name, const QColor &color)
    {
        this->name_ = name;
        this->color_ = color;
        this->update();
    }

    void setSelected(bool selected)
    {
        if (this->selected_ != selected)
        {
            this->selected_ = selected;
            this->update();
        }
    }

    void setUiScale(float scale)
    {
        this->scale_ = scale;
        this->setMinimumHeight(qRound(96 * scale));
        this->update();
    }

protected:
    void enterEvent(QEnterEvent *) override
    {
        this->hovered_ = true;
        this->update();
    }

    void leaveEvent(QEvent *) override
    {
        this->hovered_ = false;
        this->update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton &&
            this->rect().contains(event->position().toPoint()) &&
            this->clicked)
        {
            this->clicked();
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const auto *theme = getTheme();
        const auto s = this->scale_;
        const auto accent = theme->tabs.selected.backgrounds.regular;
        auto text = theme->window.text;
        auto muted = text;
        muted.setAlpha(150);

        // Card
        const QRectF card = QRectF(this->rect()).adjusted(1, 1, -1, -1);
        auto cardFill = theme->splits.input.background;
        if (this->hovered_)
        {
            cardFill = cardFill.lighter(118);
        }
        painter.setPen(this->selected_ ? QPen(accent, 2 * s)
                                       : QPen(theme->splits.header.border, 1));
        painter.setBrush(cardFill);
        painter.drawRoundedRect(card, 8 * s, 8 * s);

        // A strip of chat so the paint is seen how it looks in chat
        const qreal footer = 24 * s;
        const QRectF stage = card.adjusted(6 * s, 6 * s, -6 * s, -footer);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme->splits.background);
        painter.drawRoundedRect(stage, 6 * s, 6 * s);

        QFont font = this->font();
        font.setBold(true);
        font.setPixelSize(std::max(8, qRound(19 * s)));
        QFontMetricsF metrics(font);
        const auto name = this->name_.isEmpty() ? u"YourName"_s : this->name_;
        while (metrics.horizontalAdvance(name) > stage.width() - (12 * s) &&
               font.pixelSize() > 8)
        {
            font.setPixelSize(font.pixelSize() - 1);
            metrics = QFontMetricsF(font);
        }
        const QSizeF textSize(metrics.horizontalAdvance(name), metrics.height());
        const QPointF textAt(stage.center().x() - (textSize.width() / 2),
                             stage.center().y() - (textSize.height() / 2));
        const auto color =
            this->color_.isValid() ? this->color_ : QColor(0xb0, 0xb0, 0xb0);

        if (this->paint_)
        {
            const auto margins = this->paint_->getShadowMargins(s);
            const QSizeF canvas(
                textSize.width() + margins.left() + margins.right(),
                textSize.height() + margins.top() + margins.bottom());
            const auto pixmap = this->paint_->getPixmap(
                name, font, color, canvas, s, float(this->devicePixelRatioF()),
                margins, false);
            painter.drawPixmap(textAt - QPointF(margins.left(), margins.top()),
                               pixmap);
        }
        else
        {
            painter.setFont(font);
            painter.setPen(color);
            painter.drawText(QPointF(textAt.x(), textAt.y() + metrics.ascent()),
                             name);
        }

        // Footer: name of the paint, and whether it moves
        QFont label = this->font();
        label.setPixelSize(std::max(7, qRound(12 * s)));
        label.setWeight(QFont::DemiBold);
        painter.setFont(label);
        const QFontMetricsF labelMetrics(label);
        const qreal footerY = card.bottom() - (footer / 2);

        QString tag;
        if (this->selected_)
        {
            tag = u"WEARING"_s;
        }
        else if (this->paint_)
        {
            tag = this->paint_->animated() ? u"ANIMATED"_s : u"STILL"_s;
        }

        QFont tagFont = label;
        tagFont.setPixelSize(std::max(6, qRound(9 * s)));
        tagFont.setWeight(QFont::Bold);
        tagFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.6 * s);
        const QFontMetricsF tagMetrics(tagFont);
        qreal tagWidth = 0;
        if (!tag.isEmpty())
        {
            tagWidth = tagMetrics.horizontalAdvance(tag) + (12 * s);
            const QRectF pill(card.right() - (8 * s) - tagWidth,
                              footerY - (8 * s), tagWidth, 16 * s);
            const bool loud = this->selected_ || this->animated();
            painter.setPen(loud ? Qt::NoPen : QPen(muted, 1));
            painter.setBrush(loud ? accent : Qt::transparent);
            painter.drawRoundedRect(pill, 8 * s, 8 * s);
            painter.setFont(tagFont);
            painter.setPen(loud ? theme->tabs.selected.text : muted);
            painter.drawText(pill, Qt::AlignCenter, tag);
        }

        painter.setFont(label);
        painter.setPen(text);
        const QString title =
            this->paint_ ? this->paint_->style().name : u"No paint"_s;
        const QRectF titleRect(card.left() + (10 * s), footerY - (9 * s),
                               card.width() - (24 * s) - tagWidth, 18 * s);
        painter.drawText(
            titleRect, Qt::AlignLeft | Qt::AlignVCenter,
            labelMetrics.elidedText(title, Qt::ElideRight, titleRect.width()));
    }

private:
    std::shared_ptr<SolrinoPaint> paint_;
    QString name_;
    QColor color_;
    bool selected_ = false;
    bool hovered_ = false;
    float scale_ = 1.F;
};

SolrinoPaintGallery::SolrinoPaintGallery(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(7, 7, 7, 7);
    layout->setSpacing(6);

    auto *title = new QLabel(u"Solrino Paints"_s, this);
    title->setObjectName(u"VanitySectionTitle"_s);
    layout->addWidget(title);

    this->status_ = new QLabel(
        u"Free for everyone. Click a paint to wear it on your name."_s, this);
    this->status_->setObjectName(u"VanityHint"_s);
    this->status_->setWordWrap(true);
    layout->addWidget(this->status_);

    auto *filters = new QHBoxLayout;
    filters->setContentsMargins(0, 0, 0, 0);
    filters->setSpacing(4);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    const std::pair<QString, Filter> filterChoices[] = {
        {u"All"_s, Filter::All},
        {u"Animated"_s, Filter::Animated},
        {u"Still"_s, Filter::Still},
    };
    for (const auto &[label, filter] : filterChoices)
    {
        auto *button = new QPushButton(label, this);
        button->setCheckable(true);
        button->setChecked(filter == Filter::All);
        button->setCursor(Qt::PointingHandCursor);
        group->addButton(button);
        QObject::connect(button, &QPushButton::clicked, this,
                         [this, filter = filter] {
                             this->setFilter(filter);
                         });
        filters->addWidget(button);
        this->filterButtons_.push_back(button);
    }
    filters->addStretch(1);
    layout->addLayout(filters);

    this->scroll_ = new QScrollArea(this);
    this->scroll_->setWidgetResizable(true);
    this->scroll_->setFrameShape(QFrame::NoFrame);
    this->scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->grid_ = new QWidget(this->scroll_);
    this->grid_->setObjectName(u"VanityTabPage"_s);
    this->gridLayout_ = new QGridLayout(this->grid_);
    this->gridLayout_->setContentsMargins(0, 0, 0, 0);
    this->gridLayout_->setSpacing(6);
    this->scroll_->setWidget(this->grid_);
    layout->addWidget(this->scroll_, 1);

    // "No paint" first, then every paint
    auto *none = new SolrinoPaintTile(nullptr, this->grid_);
    this->tiles_.push_back(none);
    for (const auto &paint : SolrinoPaints::instance().catalog())
    {
        this->tiles_.push_back(new SolrinoPaintTile(paint, this->grid_));
    }
    for (auto *tile : this->tiles_)
    {
        tile->clicked = [this, tile] {
            this->setSelected(tile->paintId());
            if (this->paintChosen)
            {
                this->paintChosen(tile->paintId());
            }
        };
    }

    this->animationTimer_.setInterval(40);
    QObject::connect(&this->animationTimer_, &QTimer::timeout, this,
                     [this] {
                         this->animateTiles();
                     });
    this->setUiScale(1.F);
}

void SolrinoPaintGallery::setIdentity(const QString &displayName,
                                      const QColor &color)
{
    for (auto *tile : this->tiles_)
    {
        tile->setIdentity(displayName, color);
    }
}

void SolrinoPaintGallery::setSelected(const QString &paintId)
{
    for (auto *tile : this->tiles_)
    {
        tile->setSelected(tile->paintId() == paintId);
    }
}

void SolrinoPaintGallery::setStatus(const QString &text, bool error)
{
    this->status_->setText(text);
    this->status_->setStyleSheet(error ? u"color: #ff6b6b;"_s : QString());
}

void SolrinoPaintGallery::setUiScale(float scale)
{
    this->uiScale_ = scale;
    for (auto *tile : this->tiles_)
    {
        tile->setUiScale(scale);
    }
    this->columns_ = 0;
    this->relayout();
}

void SolrinoPaintGallery::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    this->relayout();
    this->animationTimer_.start();
}

void SolrinoPaintGallery::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    this->animationTimer_.stop();
}

void SolrinoPaintGallery::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    this->relayout();
}

void SolrinoPaintGallery::setFilter(Filter filter)
{
    this->filter_ = filter;
    this->columns_ = 0;
    this->relayout();
}

void SolrinoPaintGallery::relayout()
{
    const int available = std::max(
        1, this->scroll_->viewport()->width() -
               this->scroll_->verticalScrollBar()->sizeHint().width());
    const int columns = std::clamp(
        available / std::max(1, qRound(170 * this->uiScale_)), 1, 4);
    if (columns == this->columns_)
    {
        return;
    }
    this->columns_ = columns;

    while (this->gridLayout_->count() > 0)
    {
        delete this->gridLayout_->takeAt(0);
    }
    int index = 0;
    for (auto *tile : this->tiles_)
    {
        const bool shown =
            tile->paintId().isEmpty() || this->filter_ == Filter::All ||
            (this->filter_ == Filter::Animated) == tile->animated();
        tile->setVisible(shown);
        if (!shown)
        {
            continue;
        }
        this->gridLayout_->addWidget(tile, index / columns, index % columns);
        ++index;
    }
    for (int column = 0; column < 4; ++column)
    {
        this->gridLayout_->setColumnStretch(column, column < columns ? 1 : 0);
    }
    this->gridLayout_->setRowStretch((index / columns) + 1, 1);
}

void SolrinoPaintGallery::animateTiles()
{
    const QRect visible(
        this->grid_->mapFrom(this->scroll_->viewport(), QPoint()),
        this->scroll_->viewport()->size());
    for (auto *tile : this->tiles_)
    {
        if (tile->animated() && tile->isVisible() &&
            visible.intersects(tile->geometry()))
        {
            tile->update();
        }
    }
}

}  // namespace chatterino

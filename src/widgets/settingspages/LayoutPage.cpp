// SPDX-FileCopyrightText: 2026 Contributors to Solrino
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/LayoutPage.hpp"

#include "widgets/NotebookEnums.hpp"
#include "Application.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

#include <array>

namespace chatterino {

namespace {

SolrinoLayoutChoice savedChoice()
{
    auto *s = getSettings();
    return {
        .tabs = s->tabDirection.getValue(),
        .chatBox = s->solrinoLayoutChatBox.getValue(),
        .logo = s->solrinoLayoutLogo.getValue(),
        .gear = s->solrinoLayoutGear.getValue(),
        .account = s->solrinoLayoutAccount.getValue(),
    };
}

void applyChoice(const SolrinoLayoutChoice &choice)
{
    auto *s = getSettings();
    s->tabDirection = NotebookTabLocation(choice.tabs);
    s->solrinoLayoutChatBox = choice.chatBox;
    s->solrinoLayoutLogo = choice.logo;
    s->solrinoLayoutGear = choice.gear;
    s->solrinoLayoutAccount = choice.account;
}

QComboBox *addChoice(QFormLayout *form, const QString &label,
                     const std::vector<std::pair<QString, QVariant>> &options)
{
    auto *box = new ComboBox;
    for (const auto &[text, value] : options)
    {
        box->addItem(text, value);
    }
    form->addRow(label, box);
    return box;
}

void select(QComboBox *box, const QVariant &value)
{
    const auto at = box->findData(value);
    box->setCurrentIndex(at >= 0 ? at : 0);
}

}  // namespace

/// A small drawing of the window, so a choice can be judged straight away.
class SolrinoLayoutMock : public QWidget
{
public:
    SolrinoLayoutMock()
        : logo_(QStringLiteral(":/icon.png"))
    {
        this->setMinimumHeight(250);
        this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        this->setAccessibleName("Picture of the layout");
    }

    void setChoice(const SolrinoLayoutChoice &choice)
    {
        this->choice_ = choice;
        this->update();
    }

    QSize sizeHint() const override
    {
        return {440, 270};
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        const auto *theme = getTheme();
        const auto text = theme->window.text;
        auto muted = text;
        muted.setAlpha(110);
        auto faint = text;
        faint.setAlpha(34);
        const auto accent = theme->tabs.selected.line.regular;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF frame = QRectF(this->rect()).adjusted(1, 1, -1, -1);
        QPainterPath shape;
        shape.addRoundedRect(frame, 8, 8);
        p.fillPath(shape, theme->window.background);
        p.setClipPath(shape);

        // Title bar
        const qreal barHeight = 26;
        QRectF bar(frame.left(), frame.top(), frame.width(), barHeight);
        p.fillRect(bar, theme->tabs.selected.backgrounds.regular.darker(130));

        qreal left = bar.left() + 8;
        qreal right = bar.right() - 8;
        const qreal middle = bar.center().y();

        // Minimize, maximize, close
        p.setPen(QPen(muted, 1.3));
        for (int i = 0; i < 3; ++i)
        {
            const QPointF c(right - 5, middle);
            if (i == 0)
            {
                p.drawLine(c + QPointF(-4, -4), c + QPointF(4, 4));
                p.drawLine(c + QPointF(-4, 4), c + QPointF(4, -4));
            }
            else if (i == 1)
            {
                p.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
            }
            else
            {
                p.drawLine(c + QPointF(-4, 0), c + QPointF(4, 0));
            }
            right -= 20;
        }
        right -= 4;

        const auto drawGear = [&](qreal &x, bool fromLeft) {
            const qreal size = 14;
            const QRectF r(fromLeft ? x : x - size, middle - size / 2, size,
                           size);
            p.setPen(QPen(text, 2.2));
            p.drawEllipse(r.adjusted(3, 3, -3, -3));
            p.setPen(QPen(text, 2));
            for (int a = 0; a < 8; ++a)
            {
                p.save();
                p.translate(r.center());
                p.rotate(a * 45.0);
                p.drawLine(QPointF(0, -4.6), QPointF(0, -6.6));
                p.restore();
            }
            x += fromLeft ? size + 8 : -(size + 8);
        };
        const auto drawAccount = [&](qreal &x, bool fromLeft) {
            const qreal width = 58;
            const QRectF r(fromLeft ? x : x - width, middle - 7, width, 14);
            p.setPen(text);
            auto font = p.font();
            font.setPixelSize(11);
            p.setFont(font);
            p.drawText(r, Qt::AlignCenter, "username");
            x += fromLeft ? width + 8 : -(width + 8);
        };

        if (this->choice_.gear == "left")
        {
            drawGear(left, true);
        }
        if (this->choice_.account == "left")
        {
            drawAccount(left, true);
        }
        if (this->choice_.gear != "left")
        {
            drawGear(right, false);
        }
        if (this->choice_.account != "left")
        {
            drawAccount(right, false);
        }

        // The logo sits in the free space between the two sides
        if (this->choice_.logo != "hidden")
        {
            const qreal size = 20;
            qreal x = left;
            if (this->choice_.logo == "center")
            {
                x = (left + right - size) / 2;
            }
            else if (this->choice_.logo == "right")
            {
                x = right - size;
            }
            p.drawPixmap(QRectF(x, middle - size / 2, size, size), this->logo_,
                         this->logo_.rect());
        }

        // Tabs and chat
        QRectF body(frame.left(), bar.bottom(), frame.width(),
                    frame.height() - barHeight);
        const auto location = NotebookTabLocation(this->choice_.tabs);
        const bool sideways = location == NotebookTabLocation::Left ||
                              location == NotebookTabLocation::Right;
        const qreal stripSize = sideways ? 74 : 26;

        QRectF strip = body;
        QRectF chat = body;
        switch (location)
        {
            case NotebookTabLocation::Left:
                strip.setWidth(stripSize);
                chat.setLeft(strip.right());
                break;
            case NotebookTabLocation::Right:
                strip.setLeft(body.right() - stripSize);
                chat.setRight(strip.left());
                break;
            case NotebookTabLocation::Bottom:
                strip.setTop(body.bottom() - stripSize);
                chat.setBottom(strip.top());
                break;
            case NotebookTabLocation::Top:
            default:
                strip.setHeight(stripSize);
                chat.setTop(strip.bottom());
                break;
        }

        auto font = p.font();
        font.setPixelSize(11);
        p.setFont(font);
        const std::array<QString, 3> names{"channel", "friend", "mods"};
        qreal along = sideways ? strip.top() + 4 : strip.left() + 4;
        for (size_t i = 0; i < names.size(); ++i)
        {
            const QRectF tab = sideways
                                   ? QRectF(strip.left(), along,
                                            strip.width(), 24)
                                   : QRectF(along, strip.top(), 64,
                                            strip.height());
            if (i == 0)
            {
                p.fillRect(tab, theme->tabs.selected.backgrounds.regular);
                p.setPen(Qt::NoPen);
                QRectF line = tab;
                switch (location)
                {
                    case NotebookTabLocation::Left:
                        line.setWidth(2);
                        break;
                    case NotebookTabLocation::Right:
                        line.setLeft(tab.right() - 2);
                        break;
                    case NotebookTabLocation::Bottom:
                        line.setTop(tab.bottom() - 2);
                        break;
                    default:
                        line.setHeight(2);
                        break;
                }
                p.fillRect(line, accent);
            }
            p.setPen(i == 0 ? text : muted);
            p.drawText(tab, Qt::AlignCenter, names.at(i));
            along += sideways ? 26 : 66;
        }

        // Chat box
        const qreal boxHeight = 24;
        const bool boxOnTop = this->choice_.chatBox == "above";
        const QRectF box(chat.left() + 6,
                         boxOnTop ? chat.top() + 5
                                  : chat.bottom() - boxHeight - 5,
                         chat.width() - 12, boxHeight);
        QPainterPath boxShape;
        boxShape.addRoundedRect(box, 5, 5);
        p.fillPath(boxShape, faint);
        p.setPen(muted);
        p.drawText(box.adjusted(8, 0, -8, 0),
                   Qt::AlignVCenter | Qt::AlignLeft, "Send a message...");

        // Messages
        const QRectF messages = chat.adjusted(
            10, boxOnTop ? boxHeight + 12 : 8, -10,
            boxOnTop ? -6 : -(boxHeight + 12));
        const std::array<QColor, 4> colors{
            QColor(255, 92, 92), QColor(90, 170, 255), QColor(120, 220, 130),
            QColor(240, 180, 80)};
        const std::array<qreal, 4> widths{0.72, 0.48, 0.86, 0.6};
        qreal y = messages.top() + 2;
        int row = 0;
        p.setPen(Qt::NoPen);
        while (y + 8 <= messages.bottom())
        {
            const auto index = size_t(row) % colors.size();
            p.setBrush(colors.at(index));
            p.drawRoundedRect(QRectF(messages.left(), y, 34, 7), 3, 3);
            p.setBrush(faint);
            p.drawRoundedRect(
                QRectF(messages.left() + 40, y,
                       (messages.width() - 40) * widths.at(index), 7),
                3, 3);
            y += 17;
            ++row;
        }

        p.setClipping(false);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(faint, 1));
        p.drawPath(shape);
    }

private:
    SolrinoLayoutChoice choice_;
    QPixmap logo_;
};

LayoutPage::LayoutPage()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *content = new QWidget;
    scroll->setWidget(content);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto *intro = new QLabel(
        "Pick how Solrino is arranged. Nothing changes until you press Save.");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Ready-made arrangements
    const auto addPreset = [this](QHBoxLayout *row, const QString &name,
                                  const QString &tip,
                                  const SolrinoLayoutChoice &choice) {
        auto *button = new QPushButton(name);
        button->setToolTip(tip);
        QObject::connect(button, &QPushButton::clicked, this, [this, choice] {
            this->pick(choice);
        });
        row->addWidget(button);
    };
    auto *presets = new QHBoxLayout();
    addPreset(presets, "Basic", "The layout Solrino starts with", {});
    addPreset(presets, "Side tabs", "Tabs down the left", {.tabs = 1});
    addPreset(presets, "Bottom tabs", "Tabs under the chat, logo in the middle",
              {.tabs = 3, .logo = QStringLiteral("center")});
    addPreset(presets, "Mirrored", "Tabs on the right, controls on the left",
              {.tabs = 2,
               .logo = QStringLiteral("right"),
               .gear = QStringLiteral("left"),
               .account = QStringLiteral("left")});
    presets->addStretch(1);
    layout->addLayout(presets);

    this->mock_ = new SolrinoLayoutMock;
    layout->addWidget(this->mock_);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    this->tabs_ = addChoice(form, "Chat tabs",
                            {{"Top", 0}, {"Left", 1}, {"Right", 2}, {"Bottom", 3}});
    this->chatBox_ = addChoice(form, "Chat box",
                               {{"Below the chat", "below"},
                                {"Above the chat", "above"}});
    this->logo_ = addChoice(form, "Logo",
                            {{"Left", "left"},
                             {"Middle", "center"},
                             {"Right", "right"},
                             {"Hidden", "hidden"}});
    this->gear_ = addChoice(form, "Settings gear",
                            {{"Right", "right"}, {"Left", "left"}});
    this->account_ = addChoice(form, "Account name",
                               {{"Right", "right"}, {"Left", "left"}});
    layout->addLayout(form);

    for (auto *box :
         {this->tabs_, this->chatBox_, this->logo_, this->gear_, this->account_})
    {
        QObject::connect(box, &QComboBox::activated, this, [this] {
            this->readControls();
        });
    }

    this->status_ = new QLabel;
    this->status_->setWordWrap(true);
    layout->addWidget(this->status_);

    auto *buttons = new QHBoxLayout();
    this->previewButton_ = new QPushButton;
    this->previewButton_->setToolTip(
        "Shows this layout on the real window. It goes back if you close "
        "settings without saving.");
    QObject::connect(this->previewButton_, &QPushButton::clicked, this, [this] {
        if (this->previewing_)
        {
            this->stopPreview();
        }
        else
        {
            this->previewing_ = true;
            applyChoice(this->pending_);
        }
        this->refresh();
    });
    buttons->addWidget(this->previewButton_);

    this->saveButton_ = new QPushButton("Save");
    QObject::connect(this->saveButton_, &QPushButton::clicked, this, [this] {
        this->previewing_ = false;
        applyChoice(this->pending_);
        this->saved_ = this->pending_;
        getSettings()->requestSave();
        // So that the dialog's Cancel button can't undo a layout that was saved
        getSettings()->saveSnapshot();
        this->refresh();
    });
    buttons->addWidget(this->saveButton_);

    this->undoButton_ = new QPushButton("Undo");
    this->undoButton_->setToolTip("Go back to your saved layout");
    QObject::connect(this->undoButton_, &QPushButton::clicked, this, [this] {
        this->pick(this->saved_);
    });
    buttons->addWidget(this->undoButton_);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    layout->addStretch(1);

    this->saved_ = savedChoice();
    this->pick(this->saved_);
}

LayoutPage::~LayoutPage()
{
    if (!isAppAboutToQuit())
    {
        this->stopPreview();
    }
}

void LayoutPage::showEvent(QShowEvent *event)
{
    SettingsPage::showEvent(event);
    // "Tab layout" on the General page can change it too
    const auto current = savedChoice();
    if (!this->previewing_ && current != this->saved_)
    {
        this->saved_ = current;
        this->pick(current);
    }
}

void LayoutPage::hideEvent(QHideEvent *event)
{
    SettingsPage::hideEvent(event);
    // Minimizing settings isn't leaving, so a preview carries on
    if (!event->spontaneous())
    {
        this->stopPreview();
        this->refresh();
    }
}

void LayoutPage::pick(const SolrinoLayoutChoice &choice)
{
    this->pending_ = choice;
    select(this->tabs_, choice.tabs);
    select(this->chatBox_, choice.chatBox);
    select(this->logo_, choice.logo);
    select(this->gear_, choice.gear);
    select(this->account_, choice.account);
    if (this->previewing_)
    {
        applyChoice(this->pending_);
    }
    this->refresh();
}

void LayoutPage::readControls()
{
    this->pick({
        .tabs = this->tabs_->currentData().toInt(),
        .chatBox = this->chatBox_->currentData().toString(),
        .logo = this->logo_->currentData().toString(),
        .gear = this->gear_->currentData().toString(),
        .account = this->account_->currentData().toString(),
    });
}

void LayoutPage::stopPreview()
{
    if (this->previewing_)
    {
        this->previewing_ = false;
        applyChoice(this->saved_);
    }
}

void LayoutPage::refresh()
{
    this->mock_->setChoice(this->pending_);

    const bool changed = this->pending_ != this->saved_;
    this->saveButton_->setEnabled(changed);
    this->undoButton_->setEnabled(changed);
    this->previewButton_->setEnabled(changed || this->previewing_);
    this->previewButton_->setText(this->previewing_ ? "Stop previewing"
                                                    : "Preview on my window");

    if (this->previewing_)
    {
        this->status_->setText(
            changed ? "Previewing on your window. Press Save to keep it."
                    : "Previewing your saved layout.");
    }
    else if (changed)
    {
        this->status_->setText(
            "Not saved yet. This is only the picture so far.");
    }
    else
    {
        this->status_->setText(this->saved_ == SolrinoLayoutChoice{}
                                   ? "You're using the basic layout."
                                   : "This is your saved layout.");
    }
}

}  // namespace chatterino

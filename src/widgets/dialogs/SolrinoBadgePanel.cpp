#include "widgets/dialogs/SolrinoBadgePanel.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/SolrinoVersion.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/moltorino/MoltorinoSupporterBadges.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Settings.hpp"
#include "widgets/BaseWindow.hpp"
#include "widgets/dialogs/SolrinoAnnouncement.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMovie>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace chatterino::solrino {

namespace {

struct BadgeChoice {
    const char *id;
    const char *name;
};

/// Oldest first: new badges go on the end, which is what "Newest added" reads.
constexpr std::array<BadgeChoice, 40> BADGES{{
    {"supporter", "Supporter"},
    {"supporter_plus", "Supporter+"},
    {"supporter_plus_alt", "Supporter+ Alt"},
    {"supporter_plus_plus", "Supporter++"},
    {"supporter_plus_plus_alt", "Supporter++ Alt"},
    {"founder", "Founder"},
    {"top_donor", "Top Donor"},
    {"top_donor_alt", "Top Donor Alt"},
    {"developer", "Developer"},
    {"contributor", "Contributor"},
    {"obsidian", "Early Supporter"},
    {"special", "Special"},
    {"moderator", "Moderator"},
    {"bug_hunter", "Bug Hunter"},
    {"spooky", "Spooky 2026"},
    {"winter_2026", "Winter 2026"},
    {"valentines_2027", "Valentine's 2027"},
    {"friend_of_solrino", "Friend of Solrino"},
    {"black_hole", "Black Hole"},
    {"beta_tester", "Beta Tester"},
    {"number_one_donor", "#1 Donor"},
    {"supporter_plus_plus_plus", "Supporter+++"},
    {"staff", "Staff"},
    {"supporter_plus_plus_plus_alt", "Supporter+++ Alt"},
    {"donor_of_the_month", "Donor of the Month"},
    {"donor_of_the_year", "Donor of the Year"},
    {"first_donor", "First Donor"},
    {"one_year_on_solrino", "One Year on Solrino"},
    {"veteran", "Veteran"},
    {"alpha_tester", "Alpha Tester"},
    {"top_bug_hunter", "Top Bug Hunter"},
    {"katana", "Katana"},
    {"halo", "Halo"},
    {"matrix_rain", "Matrix Rain"},
    {"plasma_ball", "Plasma Ball"},
    {"clockwork", "Clockwork"},
    {"kaleidoscope", "Kaleidoscope"},
    {"lava_lamp", "Lava Lamp"},
    {"oil_slick", "Oil Slick"},
    {"nebula", "Nebula"},
}};

QString nameOf(const QString &badge)
{
    for (const auto &choice : BADGES)
    {
        if (badge == QString::fromLatin1(choice.id))
        {
            return QString::fromLatin1(choice.name);
        }
    }
    return badge;
}

constexpr int COLUMNS = 8;

class BadgePanel : public BaseWindow
{
public:
    explicit BadgePanel(QWidget *parent)
        : BaseWindow({BaseWindow::EnableCustomFrame, BaseWindow::Dialog,
                      BaseWindow::DisableLayoutSave},
                     parent)
    {
        this->setAttribute(Qt::WA_DeleteOnClose);
        this->setWindowTitle(QStringLiteral("Give a badge"));
        this->setStyleSheet(QStringLiteral(
            "QWidget { color: #f8f1ef; }"
            "QToolButton { background: rgba(255, 255, 255, 10); border: 1px solid rgba(255, 255, 255, 22);"
            " border-radius: 9px; padding: 5px 2px 4px; font-size: 11px; }"
            "QToolButton:hover { background: rgba(255, 255, 255, 24); }"
            "QToolButton:checked { background: rgba(227, 16, 28, 72);"
            " border: 2px solid #ff5c4d; }"
            "QLineEdit { background: #0e0809; border: 1px solid #5a2228;"
            " border-radius: 8px; padding: 9px 11px; font-size: 15px; }"
            "QLineEdit:focus { border-color: #ff5c4d; }"
            "QPushButton { background: rgba(255, 255, 255, 18); border: 1px solid rgba(255, 255, 255, 46);"
            " border-radius: 8px; padding: 9px 18px; font-weight: 600; }"
            "QPushButton:hover { background: rgba(255, 255, 255, 34); }"
            "QPushButton:disabled { color: rgba(255, 255, 255, 110); }"
            "QPushButton#give { background: #d3121f; border-color: #ff5c4d; }"
            "QPushButton#give:hover { background: #ee2230; }"
            "QPushButton#give:disabled { background: #6e1a20; }"
            "QLabel#section { font-size: 15px; font-weight: 700; }"
            "QFrame#rule { background: rgba(255, 255, 255, 30); }"));

        auto *layout = new QVBoxLayout();
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(11);
        this->getLayoutContainer()->setLayout(layout);

        auto *grid = new QGridLayout();
        grid->setSpacing(6);
        this->group_ = new QButtonGroup(this);
        // The same order as vanity: the main badges, then the ones that
        // lead Other Badges, then everything else A to Z
        std::vector<BadgeChoice> ordered(BADGES.begin(), BADGES.end());
        const auto placeOf = [](const BadgeChoice &choice) {
            const auto id = QString::fromLatin1(choice.id);
            const auto rank = mainBadgeRank(id);
            if (rank >= 0)
            {
                return rank;
            }
            const auto other = otherBadgeRank(id);
            return int(BADGES.size()) + (other >= 0 ? other : int(BADGES.size()));
        };
        std::ranges::stable_sort(
            ordered, [&placeOf](const auto &a, const auto &b) {
                const auto placeA = placeOf(a);
                const auto placeB = placeOf(b);
                if (placeA != placeB)
                {
                    return placeA < placeB;
                }
                return QString::fromLatin1(a.name).compare(
                           QString::fromLatin1(b.name), Qt::CaseInsensitive) < 0;
            });
        int index = 0;
        for (const auto &choice : ordered)
        {
            grid->addWidget(this->makeBadge(choice), index / COLUMNS,
                            index % COLUMNS);
            ++index;
        }
        layout->addLayout(grid);

        this->nameInput_ = new QLineEdit();
        this->nameInput_->setPlaceholderText(QStringLiteral("Twitch username"));
        this->nameInput_->setMaxLength(26);
        layout->addWidget(this->nameInput_);

        auto *buttons = new QHBoxLayout();
        this->status_ = new QLabel();
        this->status_->setWordWrap(true);
        buttons->addWidget(this->status_, 1);
        this->takeButton_ = new QPushButton(QStringLiteral("Take badge"));
        this->giveButton_ = new QPushButton(QStringLiteral("Give badge"));
        this->giveButton_->setObjectName(QStringLiteral("give"));
        buttons->addWidget(this->takeButton_);
        buttons->addWidget(this->giveButton_);
        layout->addLayout(buttons);

        // Announcement: a message for everyone using Solrino
        auto *rule = new QFrame();
        rule->setObjectName(QStringLiteral("rule"));
        rule->setFixedHeight(1);
        layout->addSpacing(4);
        layout->addWidget(rule);
        auto *title = new QLabel(QStringLiteral("Announcement"));
        title->setObjectName(QStringLiteral("section"));
        layout->addWidget(title);
        this->announcementInput_ = new QLineEdit();
        this->announcementInput_->setPlaceholderText(
            QStringLiteral("Say something to everyone on Solrino"));
        this->announcementInput_->setMaxLength(300);
        layout->addWidget(this->announcementInput_);
        auto *announceRow = new QHBoxLayout();
        this->announceStatus_ = new QLabel(QStringLiteral(
            "Shows as a banner at the top of everyone's Solrino."));
        this->announceStatus_->setWordWrap(true);
        this->announceStatus_->setStyleSheet(
            QStringLiteral("color: rgba(248, 241, 239, 170);"));
        announceRow->addWidget(this->announceStatus_, 1);
        this->clearAnnouncementButton_ = new QPushButton(QStringLiteral("Clear"));
        this->clearAnnouncementButton_->setToolTip(
            QStringLiteral("Take the current announcement down"));
        this->announceButton_ = new QPushButton(QStringLiteral("Announce"));
        this->announceButton_->setObjectName(QStringLiteral("give"));
        this->announceButton_->setEnabled(false);
        announceRow->addWidget(this->clearAnnouncementButton_);
        announceRow->addWidget(this->announceButton_);
        layout->addLayout(announceRow);

        // Just for you: redeem First the moment an open chat goes live
        auto *firstRule = new QFrame();
        firstRule->setObjectName(QStringLiteral("rule"));
        firstRule->setFixedHeight(1);
        layout->addSpacing(4);
        layout->addWidget(firstRule);
        auto *firstBox = new QCheckBox(
            QStringLiteral("Auto-redeem First when a chat goes live"));
        firstBox->setChecked(getSettings()->solrinoAutoRedeemFirst);
        layout->addWidget(firstBox);
        auto *firstNote = new QLabel(QStringLiteral(
            "Works in every chat you have open, even in tabs you aren't on."));
        firstNote->setWordWrap(true);
        firstNote->setStyleSheet(
            QStringLiteral("color: rgba(248, 241, 239, 170);"));
        layout->addWidget(firstNote);
        QObject::connect(firstBox, &QCheckBox::toggled, this, [](bool on) {
            getSettings()->solrinoAutoRedeemFirst = on;
        });

        QObject::connect(this->announcementInput_, &QLineEdit::textChanged, this,
                         [this] {
                             this->announceButton_->setEnabled(
                                 !this->announcing_ &&
                                 !this->announcementInput_->text()
                                      .trimmed()
                                      .isEmpty());
                         });
        QObject::connect(this->announceButton_, &QPushButton::clicked, this,
                         [this] {
                             this->announce(false);
                         });
        QObject::connect(this->announcementInput_, &QLineEdit::returnPressed,
                         this, [this] {
                             this->announce(false);
                         });
        QObject::connect(this->clearAnnouncementButton_, &QPushButton::clicked,
                         this, [this] {
                             this->announce(true);
                         });

        QObject::connect(this->giveButton_, &QPushButton::clicked, this, [this] {
            this->submit(true);
        });
        QObject::connect(this->takeButton_, &QPushButton::clicked, this, [this] {
            this->submit(false);
        });
        QObject::connect(this->nameInput_, &QLineEdit::returnPressed, this,
                         [this] {
                             this->submit(true);
                         });
        QObject::connect(this->nameInput_, &QLineEdit::textChanged, this,
                         [this] {
                             this->updateButtons();
                         });
        QObject::connect(this->group_, &QButtonGroup::buttonClicked, this, [this] {
            this->updateButtons();
            this->nameInput_->setFocus();
        });

        this->updateButtons();
        this->nameInput_->setFocus();
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape)
        {
            this->close();
            return;
        }
        BaseWindow::keyPressEvent(event);
    }

private:
    QToolButton *makeBadge(const BadgeChoice &choice)
    {
        auto *button = new QToolButton();
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(40, 40));
        button->setFixedWidth(116);
        button->setText(QString::fromLatin1(choice.name));
        button->setProperty("badge", QString::fromLatin1(choice.id));
        this->group_->addButton(button);

        // The stars keep moving in here, just like in chat
        auto *movie = new QMovie(QStringLiteral(":/solrino-badges/") +
                                     QString::fromLatin1(choice.id) +
                                     QStringLiteral("/3x.webp"),
                                 {}, button);
        movie->setScaledSize(QSize(40, 40));
        QObject::connect(movie, &QMovie::frameChanged, button,
                         [button, movie] {
                             button->setIcon(QIcon(movie->currentPixmap()));
                         });
        movie->start();
        return button;
    }

    QString selectedBadge() const
    {
        auto *button = this->group_->checkedButton();
        return button != nullptr ? button->property("badge").toString()
                                 : QString{};
    }

    void updateButtons()
    {
        const bool ready = !this->busy_ && !this->selectedBadge().isEmpty() &&
                           !this->nameInput_->text().trimmed().isEmpty();
        this->giveButton_->setEnabled(ready);
        this->takeButton_->setEnabled(ready);
    }

    void submit(bool giving)
    {
        const auto badge = this->selectedBadge();
        const auto name = this->nameInput_->text().trimmed();
        if (this->busy_ || name.isEmpty())
        {
            return;
        }
        if (badge.isEmpty())
        {
            this->status_->setText(QStringLiteral("Pick a badge first."));
            return;
        }

        this->busy_ = true;
        this->updateButtons();
        this->status_->setText(giving ? QStringLiteral("Giving...")
                                      : QStringLiteral("Taking..."));
        QPointer<BadgePanel> self(this);
        changeBadge(name, badge, giving,
                    [self](bool ok, const QString &message) {
                        if (self == nullptr)
                        {
                            return;
                        }
                        self->busy_ = false;
                        self->status_->setText(message);
                        if (ok)
                        {
                            self->nameInput_->clear();
                        }
                        self->updateButtons();
                        self->nameInput_->setFocus();
                    });
    }

    void announce(bool clear)
    {
        const auto text = this->announcementInput_->text().trimmed();
        if (this->announcing_ || (!clear && text.isEmpty()))
        {
            return;
        }
        // It reaches everyone at once, so it's checked first
        const auto question =
            clear ? QStringLiteral("Take the current announcement down for "
                                   "everyone?")
                  : QStringLiteral("Send this to everyone on Solrino?\n\n\"%1\"")
                        .arg(text);
        if (QMessageBox::question(this, QStringLiteral("Announcement"),
                                  question) != QMessageBox::Yes)
        {
            return;
        }

        this->announcing_ = true;
        this->announceButton_->setEnabled(false);
        this->clearAnnouncementButton_->setEnabled(false);
        this->announceStatus_->setText(clear ? QStringLiteral("Taking it down...")
                                             : QStringLiteral("Sending..."));
        QPointer<BadgePanel> self(this);
        sendAnnouncement(clear ? QString() : text,
                         [self, clear](bool ok, const QString &message) {
                             if (self == nullptr)
                             {
                                 return;
                             }
                             self->announcing_ = false;
                             self->announceStatus_->setText(message);
                             if (ok && !clear)
                             {
                                 self->announcementInput_->clear();
                             }
                             self->clearAnnouncementButton_->setEnabled(true);
                             self->announceButton_->setEnabled(
                                 !self->announcementInput_->text()
                                      .trimmed()
                                      .isEmpty());
                         });
    }

    QLineEdit *announcementInput_{};
    QLabel *announceStatus_{};
    QPushButton *announceButton_{};
    QPushButton *clearAnnouncementButton_{};
    bool announcing_ = false;

    QButtonGroup *group_{};
    QLineEdit *nameInput_{};
    QLabel *status_{};
    QPushButton *giveButton_{};
    QPushButton *takeButton_{};
    bool busy_ = false;
};

}  // namespace

int mainBadgeRank(const QString &badge)
{
    static const QStringList order{
        QStringLiteral("developer"),
        QStringLiteral("founder"),
        QStringLiteral("contributor"),
        QStringLiteral("number_one_donor"),
        QStringLiteral("donor_of_the_year"),
        QStringLiteral("donor_of_the_month"),
        QStringLiteral("first_donor"),
        QStringLiteral("top_donor"),
        QStringLiteral("top_donor_alt"),
        QStringLiteral("supporter_plus_plus_plus_alt"),
        QStringLiteral("supporter_plus_plus_plus"),
        QStringLiteral("obsidian"),
        QStringLiteral("supporter_plus_plus_alt"),
        QStringLiteral("supporter_plus_plus"),
        QStringLiteral("supporter_plus_alt"),
        QStringLiteral("supporter_plus"),
        QStringLiteral("supporter"),
    };
    return order.indexOf(badge);
}

int otherBadgeRank(const QString &badge)
{
    // These lead the Other Badges tab, in this order
    static const QStringList order{
        QStringLiteral("staff"),
        QStringLiteral("moderator"),
        QStringLiteral("top_bug_hunter"),
        QStringLiteral("alpha_tester"),
        QStringLiteral("beta_tester"),
        QStringLiteral("veteran"),
        QStringLiteral("one_year_on_solrino"),
    };
    return order.indexOf(badge);
}

int badgeAddedRank(const QString &badge)
{
    int rank = 0;
    for (const auto &choice : BADGES)
    {
        if (badge == QString::fromLatin1(choice.id))
        {
            return rank;
        }
        ++rank;
    }
    return rank;
}

bool canGiveBadges()
{
    return getApp()->getAccounts()->twitch.getCurrent()->getUserId() ==
           QStringLiteral(SOLRINO_OWNER_USER_ID);
}

void changeBadge(const QString &user, const QString &badge, bool giving,
                 std::function<void(bool, const QString &)> done)
{
    const auto account = getApp()->getAccounts()->twitch.getCurrent();
    if (account->isAnon())
    {
        done(false, QStringLiteral("Log in to Twitch first."));
        return;
    }

    auto login = user.trimmed().toLower();
    if (login.startsWith('@'))
    {
        login.remove(0, 1);
    }
    static const QRegularExpression validLogin(
        QStringLiteral("^[a-z0-9_]{1,25}$"));
    if (!validLogin.match(login).hasMatch())
    {
        done(false, QStringLiteral("That isn't a Twitch username."));
        return;
    }

    // This badge's stored name stays "obsidian", so people who have it keep it
    const auto stored = badge == QStringLiteral("early_supporter")
                            ? QStringLiteral("obsidian")
                            : badge;
    const auto token = account->getOAuthToken();
    auto shared = std::make_shared<std::function<void(bool, const QString &)>>(
        std::move(done));

    getHelix()->getUserByName(
        login,
        [shared, badge = stored, token, giving](const HelixUser &person) {
            const auto path = giving ? QStringLiteral("/v2/badges/give")
                                     : QStringLiteral("/v2/badges/take");
            NetworkRequest(QString::fromLatin1(SOLRINO_BADGE_SERVER) + path,
                           NetworkRequestType::Post)
                .header("Authorization", ("OAuth " + token).toUtf8())
                .json(QJsonObject{{"userId", person.id}, {"badge", badge}})
                .timeout(10000)
                .onSuccess([shared, person, badge,
                            giving](const NetworkResult &) {
                    const auto name = nameOf(badge);
                    (*shared)(
                        true,
                        giving
                            ? QStringLiteral("Gave %1 the %2 badge.")
                                  .arg(person.displayName, name)
                            : QStringLiteral("Took the %1 badge from %2.")
                                  .arg(name, person.displayName));
                    if (auto *badges = getApp()->getMoltorinoSupporterBadges())
                    {
                        badges->refreshNow();
                    }
                })
                .onError([shared](const NetworkResult &result) {
                    (*shared)(false, QStringLiteral("That didn't work: ") +
                                         result.parseJson()
                                             .value("error")
                                             .toObject()
                                             .value("message")
                                             .toString(result.formatError()));
                })
                .execute();
        },
        [shared, login] {
            (*shared)(false,
                      QStringLiteral("Couldn't find the user %1.").arg(login));
        });
}

void showBadgePanel(QWidget *parent)
{
    static QPointer<BadgePanel> panel;
    if (panel == nullptr)
    {
        panel = new BadgePanel(parent);
    }
    panel->show();
    // With this many badges it's tall, so it mustn't run off the screen
    const auto *screen = parent != nullptr && parent->screen() != nullptr
                             ? parent->screen()
                             : QGuiApplication::primaryScreen();
    if (screen != nullptr)
    {
        const auto area = screen->availableGeometry();
        panel->move(area.center() - panel->rect().center());
    }
    panel->raise();
    panel->activateWindow();
}

}  // namespace chatterino::solrino

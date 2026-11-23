#include "widgets/dialogs/SolrinoGiftDialog.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/SolrinoVersion.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/TwitchAccount.hpp"

#include <QButtonGroup>
#include <QGridLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace chatterino {

namespace {

const QString STYLE = QStringLiteral(R"(
QWidget#solrinoGift { background: #120c0e; color: #f2e7e3; }
QLabel#title { font-size: 18px; font-weight: 700; }
QLabel#muted { color: #a8979a; }
QLabel#note { background: #221a1c; border: 1px solid #3a2c30; border-radius: 10px; padding: 9px; color: #e6d7d2; }
QToolButton { background: #1b1416; border: 1px solid #3a2c30; border-radius: 12px; padding: 8px; color: #e6d7d2; }
QToolButton:hover { border-color: #ff5c4d88; }
QToolButton:checked { background: #3a1418; border-color: #e3101c; color: #fff; }
QLineEdit { background: #1b1416; border: 1px solid #3a2c30; border-radius: 9px; padding: 8px; color: #f2e7e3; }
QLineEdit:focus { border-color: #e3101c; }
QPushButton { background: #e3101c; border: 1px solid #ff5c4d; border-radius: 10px; padding: 10px 14px; color: #fff; font-weight: 700; }
QPushButton:disabled { background: #3a2a2c; border-color: #3a2a2c; color: #a8979a; }
)");

QString nameOf(const QString &id)
{
    // supporter_plus_plus_alt -> Supporter++ Alt
    static const QMap<QString, QString> names{
        {"supporter", "Supporter"},
        {"supporter_plus", "Supporter+"},
        {"supporter_plus_alt", "Supporter+ Alt"},
        {"supporter_plus_plus", "Supporter++"},
        {"supporter_plus_plus_alt", "Supporter++ Alt"},
        {"supporter_plus_plus_plus", "Supporter+++"},
        {"supporter_plus_plus_plus_alt", "Supporter+++ Alt"},
        {"top_donor", "Top Donor"},
        {"top_donor_alt", "Top Donor Alt"},
        {"number_one_donor", "#1 Donor"},
        {"donor_of_the_month", "Donor of the Month"},
        {"donor_of_the_year", "Donor of the Year"},
    };
    return names.value(id, id);
}

class GiftWindow : public QWidget
{
public:
    explicit GiftWindow(const QString &forLogin)
        : QWidget(nullptr, Qt::Window)
    {
        this->setObjectName(QStringLiteral("solrinoGift"));
        this->setAttribute(Qt::WA_StyledBackground);
        this->setAttribute(Qt::WA_DeleteOnClose);
        this->setWindowTitle(QStringLiteral("Gift a badge"));
        this->setStyleSheet(STYLE);
        this->resize(560, 600);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 18, 20, 18);
        layout->setSpacing(12);
        auto *title = new QLabel(QStringLiteral("Gift a badge"));
        title->setObjectName(QStringLiteral("title"));
        auto *intro = new QLabel(QStringLiteral(
            "Buy a Solrino badge for someone else, like a gifted sub. They get "
            "a popup saying it's from you, and it shows in their vanity."));
        intro->setObjectName(QStringLiteral("muted"));
        intro->setWordWrap(true);
        layout->addWidget(title);
        layout->addWidget(intro);

        this->grid_ = new QGridLayout;
        this->grid_->setSpacing(8);
        layout->addLayout(this->grid_);
        this->group_ = new QButtonGroup(this);
        this->group_->setExclusive(true);

        layout->addWidget(new QLabel(QStringLiteral("Who it's for")));
        this->to_ = new QLineEdit(forLogin);
        this->to_->setPlaceholderText(QStringLiteral("Their Twitch name"));
        this->to_->setMaxLength(26);
        layout->addWidget(this->to_);
        layout->addWidget(new QLabel(QStringLiteral("A note (optional)")));
        this->note_ = new QLineEdit;
        this->note_->setPlaceholderText(QStringLiteral("Shown on their popup"));
        this->note_->setMaxLength(120);
        layout->addWidget(this->note_);

        this->status_ = new QLabel;
        this->status_->setObjectName(QStringLiteral("note"));
        this->status_->setWordWrap(true);
        this->status_->hide();
        layout->addWidget(this->status_);
        layout->addStretch();
        this->go_ = new QPushButton(QStringLiteral("Loading..."));
        this->go_->setEnabled(false);
        layout->addWidget(this->go_);

        QObject::connect(this->go_, &QPushButton::clicked, this, [this] {
            this->checkout();
        });
        this->load();
    }

private:
    void say(const QString &text)
    {
        this->status_->setText(text);
        this->status_->setVisible(!text.isEmpty());
    }

    void load()
    {
        QPointer<GiftWindow> self(this);
        NetworkRequest(QString::fromLatin1(SOLRINO_BADGE_SERVER) +
                       QStringLiteral("/v2/gifts"))
            .timeout(10000)
            .onSuccess([self](const NetworkResult &result) {
                if (self == nullptr)
                {
                    return;
                }
                const auto body = result.parseJson();
                self->open_ = body.value("open").toBool();
                int i = 0;
                for (const auto &value : body.value("badges").toArray())
                {
                    const auto badge = value.toObject();
                    const auto id = badge.value("id").toString();
                    const auto price =
                        badge.contains("price")
                            ? QStringLiteral("$%1").arg(
                                  badge.value("price").toInt())
                            : QStringLiteral("from $%1").arg(
                                  badge.value("startsAt").toInt());
                    auto *button = new QToolButton;
                    button->setCheckable(true);
                    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                    button->setIcon(QIcon(
                        QStringLiteral(":/solrino-badges/%1/3x.webp").arg(id)));
                    button->setIconSize(QSize(44, 44));
                    button->setText(nameOf(id) + u'\n' + price);
                    button->setMinimumWidth(120);
                    button->setProperty("badge", id);
                    self->group_->addButton(button);
                    self->grid_->addWidget(button, i / 4, i % 4);
                    i++;
                }
                if (auto *first = self->group_->buttons().value(0))
                {
                    first->setChecked(true);
                }
                if (self->open_)
                {
                    self->go_->setText(QStringLiteral("Continue to checkout"));
                    self->go_->setEnabled(true);
                }
                else
                {
                    self->go_->setText(QStringLiteral("Checkout coming soon"));
                    self->say(QStringLiteral(
                        "Gifting opens at the same time as buying badges. You "
                        "can look around and pick one now."));
                }
            })
            .onError([self](const NetworkResult &) {
                if (self != nullptr)
                {
                    self->go_->setText(QStringLiteral("Couldn't load badges"));
                    self->say(QStringLiteral(
                        "Couldn't reach Solrino. Close this and try again."));
                }
            })
            .execute();
    }

    void checkout()
    {
        const auto account = getApp()->getAccounts()->twitch.getCurrent();
        auto *picked = this->group_->checkedButton();
        const auto to = this->to_->text().trimmed().remove(u'@').toLower();
        if (account->isAnon())
        {
            this->say(QStringLiteral("Log in with Twitch to gift a badge."));
            return;
        }
        if (picked == nullptr || to.isEmpty())
        {
            this->say(QStringLiteral("Pick a badge and type who it's for."));
            return;
        }
        if (to == account->getUserName().toLower())
        {
            this->say(QStringLiteral(
                "That's you. To get one for yourself, buy it on the Purchase "
                "page."));
            return;
        }
        this->go_->setEnabled(false);
        QPointer<GiftWindow> self(this);
        NetworkRequest(QString::fromLatin1(SOLRINO_BADGE_SERVER) +
                           QStringLiteral("/v2/gifts/checkout"),
                       NetworkRequestType::Post)
            .header("Authorization",
                    ("OAuth " + account->getOAuthToken()).toUtf8())
            .json(QJsonObject{
                {"badge", picked->property("badge").toString()},
                {"to", to},
                {"note", this->note_->text().trimmed()},
            })
            .timeout(15000)
            .onSuccess([self](const NetworkResult &) {
                if (self != nullptr)
                {
                    self->go_->setEnabled(true);
                }
            })
            .onError([self](const NetworkResult &result) {
                if (self == nullptr)
                {
                    return;
                }
                self->go_->setEnabled(self->open_);
                self->say(result.parseJson()
                              .value("error")
                              .toObject()
                              .value("message")
                              .toString(QStringLiteral("Couldn't start that.")));
            })
            .execute();
    }

    QGridLayout *grid_;
    QButtonGroup *group_;
    QLineEdit *to_;
    QLineEdit *note_;
    QLabel *status_;
    QPushButton *go_;
    bool open_ = false;
};

}  // namespace

void openSolrinoGiftDialog(const QString &forLogin)
{
    auto *window = new GiftWindow(forLogin.trimmed().remove(u'@'));
    window->show();
    window->raise();
    window->activateWindow();
}

}  // namespace chatterino

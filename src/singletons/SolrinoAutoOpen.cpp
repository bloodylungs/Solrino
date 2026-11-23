#include "singletons/SolrinoAutoOpen.hpp"
#include "singletons/SolrinoInstance.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitContainer.hpp"
#include "widgets/Window.hpp"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace chatterino {

namespace {

constexpr int CHECK_EVERY_MS = 60 * 1000;
constexpr int FIRST_CHECK_MS = 15 * 1000;
// Twitch looks up at most 100 channels at once
constexpr int MAX_CHANNELS = 100;

/// Channels that were live at the last check, so a tab only opens once per
/// stream, and not again if it's closed
QSet<QString> &wasLive()
{
    static QSet<QString> live;
    return live;
}

/// False until the first check after Solrino starts has come back. Channels
/// already live then get a tab, but not a browser tab: otherwise every start
/// of Solrino would open the browser for every live channel.
bool &checkedOnce()
{
    static bool checked = false;
    return checked;
}

QStringList chosenChannels()
{
    QStringList channels;
    for (const auto &name : getSettings()->solrinoAutoOpenChannels.getValue())
    {
        const auto login = name.trimmed().toLower();
        if (!login.isEmpty() && !channels.contains(login))
        {
            channels.push_back(login);
        }
    }
    return channels.mid(0, MAX_CHANNELS);
}

bool isOpen(const QString &login)
{
    // In the main window or any popped-out one
    return getApp()->getWindows()->isTwitchChannelOpen(login);
}

void openTab(const QString &login)
{
    auto &notebook = getApp()->getWindows()->getMainWindow().getNotebook();
    const bool jump = getSettings()->solrinoAutoOpenSwitch;
    auto *page = notebook.addPage(jump);
    if (page == nullptr)
    {
        return;
    }
    auto *split = page->appendNewSplit(false);
    if (split == nullptr)
    {
        return;
    }
    split->setChannel(getApp()->getTwitch()->getOrAddChannel(login));
    if (jump)
    {
        notebook.select(page);
    }
}

void check()
{
    const auto channels = chosenChannels();
    if (channels.isEmpty())
    {
        wasLive().clear();
        return;
    }
    getHelix()->fetchStreams(
        {}, channels,
        [](const std::vector<HelixStream> &streams) {
            QSet<QString> live;
            for (const auto &stream : streams)
            {
                live.insert(stream.userLogin.toLower());
            }
            for (const auto &login : live)
            {
                // Just went live (or was live when Solrino opened)
                if (wasLive().contains(login))
                {
                    continue;
                }
                if (!isOpen(login))
                {
                    openTab(login);
                }
                if (getSettings()->solrinoAutoOpenBrowser && checkedOnce() &&
                    !solrino::isSecondInstance())
                {
                    // The system's default browser, whatever it is
                    QDesktopServices::openUrl(
                        QUrl(QStringLiteral("https://www.twitch.tv/") + login));
                }
            }
            wasLive() = live;
            checkedOnce() = true;
        },
        [] {
            // Try again next time
        },
        [] {});
}

}  // namespace

void startSolrinoAutoOpen()
{
    auto *timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, qApp, [] {
        check();
    });
    timer->start(CHECK_EVERY_MS);
    QTimer::singleShot(FIRST_CHECK_MS, qApp, [] {
        check();
    });
}

void showSolrinoAutoOpenDialog(QWidget *parent)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Open tabs when they go live"));
    dialog->resize(380, 460);
    auto *layout = new QVBoxLayout(dialog);

    auto *intro = new QLabel(QStringLiteral(
        "When one of these Twitch channels goes live, Solrino opens a tab for "
        "it, so you never miss a stream. If the tab is already open, nothing "
        "changes. Checked every minute while Solrino is open."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *addRow = new QHBoxLayout();
    auto *input = new QLineEdit();
    input->setPlaceholderText(QStringLiteral("Twitch name, like xqc"));
    auto *add = new QPushButton(QStringLiteral("Add"));
    addRow->addWidget(input, 1);
    addRow->addWidget(add);
    layout->addLayout(addRow);

    auto *list = new QListWidget();
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const auto &login : chosenChannels())
    {
        list->addItem(login);
    }
    layout->addWidget(list, 1);

    auto *remove = new QPushButton(QStringLiteral("Remove selected"));
    layout->addWidget(remove);

    auto *jump = new QCheckBox(QStringLiteral("Switch to the tab when it opens"));
    jump->setChecked(getSettings()->solrinoAutoOpenSwitch);
    layout->addWidget(jump);

    auto *browser = new QCheckBox(
        QStringLiteral("Also open the stream in my browser"));
    browser->setChecked(getSettings()->solrinoAutoOpenBrowser);
    layout->addWidget(browser);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    const auto addName = [input, list] {
        // Takes "name", "@name" or a twitch.tv link
        auto name = input->text().trimmed().toLower();
        name.remove(QRegularExpression(
            QStringLiteral("^(https?://)?(www\\.)?twitch\\.tv/")));
        name.remove(QLatin1Char('@'));
        name = name.section(QLatin1Char('/'), 0, 0);
        static const QRegularExpression valid(
            QStringLiteral("^[a-z0-9_]{1,25}$"));
        if (!valid.match(name).hasMatch() ||
            !list->findItems(name, Qt::MatchFixedString).isEmpty() ||
            list->count() >= MAX_CHANNELS)
        {
            return;
        }
        list->addItem(name);
        input->clear();
    };
    QObject::connect(add, &QPushButton::clicked, dialog, addName);
    QObject::connect(input, &QLineEdit::returnPressed, dialog, addName);
    QObject::connect(remove, &QPushButton::clicked, dialog, [list] {
        qDeleteAll(list->selectedItems());
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog,
                     &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog,
                     [dialog, list, jump, browser] {
                         QStringList channels;
                         for (int i = 0; i < list->count(); ++i)
                         {
                             channels.push_back(list->item(i)->text());
                         }
                         getSettings()->solrinoAutoOpenChannels = channels;
                         getSettings()->solrinoAutoOpenSwitch =
                             jump->isChecked();
                         getSettings()->solrinoAutoOpenBrowser =
                             browser->isChecked();
                         getSettings()->requestSave();
                         dialog->accept();
                         // Check the new list soon, not in a minute
                         QTimer::singleShot(1000, qApp, [] {
                             check();
                         });
                     });
    dialog->show();
}

}  // namespace chatterino

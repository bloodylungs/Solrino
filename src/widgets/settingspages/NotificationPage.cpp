// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/NotificationPage.hpp"

#include "Application.hpp"
#include "controllers/notifications/NotificationController.hpp"
#include "controllers/notifications/NotificationModel.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Toasts.hpp"
#include "singletons/SolrinoAutoOpen.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/Window.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/helper/NotebookTab.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitContainer.hpp"
#include "common/Channel.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QTreeWidget>
#include <QSet>
#include "util/LayoutCreator.hpp"
#include "util/Twitch.hpp"
#include "widgets/helper/EditableModelView.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QTableView>
#include <QTimer>

namespace chatterino {

namespace {

/// A checklist of the Twitch channels in your open tabs, grouped by folder
void pickNotificationsFromOpenTabs(QWidget *parent)
{
    // Folder name -> channels in it, in tab order, each once
    std::vector<std::pair<QString, QStringList>> folders;
    QSet<QString> seen;
    const auto addTo = [&](const QString &folder, const QString &channel) {
        if (seen.contains(channel))
        {
            return;
        }
        seen.insert(channel);
        for (auto &entry : folders)
        {
            if (entry.first == folder)
            {
                entry.second.append(channel);
                return;
            }
        }
        folders.push_back({folder, {channel}});
    };
    for (auto *window : getApp()->getWindows()->windows())
    {
        auto &notebook = window->getNotebook();
        QHash<QString, QString> groupNames;
        for (const auto &group : notebook.tabGroups())
        {
            groupNames.insert(group.id, group.name);
        }
        for (int i = 0; i < notebook.getPageCount(); ++i)
        {
            auto *page = dynamic_cast<SplitContainer *>(notebook.getPageAt(i));
            if (page == nullptr)
            {
                continue;
            }
            const auto groupId =
                page->getTab() != nullptr ? page->getTab()->groupId() : QString();
            const auto folder = groupNames.value(groupId);
            for (auto *split : page->getSplits())
            {
                const auto channel = split->getChannel();
                if (channel != nullptr && channel->isTwitchChannel() &&
                    !channel->getName().isEmpty())
                {
                    addTo(folder, channel->getName().toLower());
                }
            }
        }
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Live notifications from open tabs"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(QStringLiteral(
        "Tick the channels you want to hear about when they go live."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *tree = new QTreeWidget(&dialog);
    tree->setHeaderHidden(true);
    layout->addWidget(tree, 1);

    auto *notifications = getApp()->getNotifications();
    std::vector<QTreeWidgetItem *> channelItems;
    for (const auto &[folder, channels] : folders)
    {
        QTreeWidgetItem *parentItem = nullptr;
        if (!folder.isEmpty())
        {
            parentItem = new QTreeWidgetItem(tree, {folder});
            parentItem->setFlags(parentItem->flags() | Qt::ItemIsAutoTristate |
                                 Qt::ItemIsUserCheckable);
            parentItem->setExpanded(true);
        }
        for (const auto &channel : channels)
        {
            auto *item = parentItem != nullptr
                             ? new QTreeWidgetItem(parentItem, {channel})
                             : new QTreeWidgetItem(tree, {channel});
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(
                0, notifications->isChannelNotified(channel, Platform::Twitch)
                       ? Qt::Checked
                       : Qt::Unchecked);
            channelItems.push_back(item);
        }
    }
    if (channelItems.empty())
    {
        new QTreeWidgetItem(tree, {QStringLiteral("No Twitch tabs are open")});
    }

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(360, 460);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    for (auto *item : channelItems)
    {
        const auto channel = item->text(0);
        const bool wanted = item->checkState(0) == Qt::Checked;
        const bool has = notifications->isChannelNotified(channel, Platform::Twitch);
        if (wanted && !has)
        {
            notifications->addChannelNotification(channel, Platform::Twitch);
        }
        else if (!wanted && has)
        {
            notifications->removeChannelNotification(channel, Platform::Twitch);
        }
    }
}

}  // namespace

NotificationPage::NotificationPage()
{
    LayoutCreator<NotificationPage> layoutCreator(this);
    auto layout = layoutCreator.emplace<QVBoxLayout>().withoutMargin();
    {
        auto tabs = layout.emplace<QTabWidget>();
        {
            auto settings = tabs.appendTab(new QVBoxLayout, "Options");
            {
                settings.emplace<QLabel>(
                    "You can be informed when certain channels go live. You "
                    "can be informed in multiple ways:");

                settings.append(this->createCheckBox(
                    "Flash taskbar", getSettings()->notificationFlashTaskbar));
                settings.append(
                    this->createCheckBox("Play sound for selected channels",
                                         getSettings()->notificationPlaySound));
                settings.append(this->createCheckBox(
                    "Play sound for any channel going live",
                    getSettings()->notificationOnAnyChannel));

                auto autoOpen = settings.emplace<QPushButton>(
                    "Open tabs when channels go live...");
                autoOpen->setToolTip(
                    "Pick Twitch channels to open a tab for as soon as they "
                    "go live");
                QObject::connect(autoOpen.getElement(), &QPushButton::clicked,
                                 this, [this] {
                                     showSolrinoAutoOpenDialog(this);
                                 });
                autoOpen->setSizePolicy(QSizePolicy::Maximum,
                                        QSizePolicy::Preferred);

                settings.append(this->createCheckBox(
                    "Suppress live notifications on startup",
                    getSettings()->suppressInitialLiveNotification));
#if defined(Q_OS_WIN) || defined(CHATTERINO_WITH_LIBNOTIFY)
                settings.append(this->createCheckBox(
                    "Show notification", getSettings()->notificationToast));
#endif
#ifdef Q_OS_WIN
                settings.append(this->createCheckBox(
                    "Create start menu shortcut (requires "
                    "restart)",
                    getSettings()->createShortcutForToasts,
                    "When enabled, a shortcut will be created inside your "
                    "start menu folder if needed by live notifications."
                    "\n(On portable mode, this is disabled by "
                    "default)"));
#endif
#if defined(Q_OS_WIN) || defined(CHATTERINO_WITH_LIBNOTIFY)
                auto openIn = settings.emplace<QHBoxLayout>().withoutMargin();
                {
                    openIn
                        .emplace<QLabel>(
                            "Action when clicking on a notification:  ")
                        ->setSizePolicy(QSizePolicy::Maximum,
                                        QSizePolicy::Preferred);

                    openIn.append(this->createToastReactionComboBox())
                        ->setSizePolicy(QSizePolicy::Maximum,
                                        QSizePolicy::Preferred);
                }
                openIn->setContentsMargins(40, 0, 0, 0);
                openIn->setSizeConstraint(QLayout::SetMaximumSize);
#endif
                auto customSound =
                    layout.emplace<QHBoxLayout>().withoutMargin();
                {
                    customSound.append(this->createCheckBox(
                        "Custom sound",
                        getSettings()->notificationCustomSound));
                    auto selectFile = customSound.emplace<QPushButton>(
                        "Select custom sound file");
                    QObject::connect(
                        selectFile.getElement(), &QPushButton::clicked, this,
                        [this] {
                            auto fileName = QFileDialog::getOpenFileName(
                                this, tr("Open Sound"), "",
                                tr("Audio Files (*.mp3 *.wav)"));
                            if (!fileName.isEmpty())
                            {
                                getSettings()->notificationPathSound = fileName;
                            }
                        });
                }

                settings->addStretch(1);
            }
            for (const auto platform : {Platform::Twitch, Platform::YouTube})
            {
                const bool youtube = platform == Platform::YouTube;
                const QString title =
                    youtube ? "YouTube channels" : "Twitch channels";
                auto channels = tabs.appendTab(new QVBoxLayout, title);
                channels
                    .emplace<QLabel>(
                        youtube
                            ? "Enter a YouTube handle or channel URL. Keep its "
                              "channel tab open to receive live notifications."
                            : "These are the channels for which you will be "
                              "informed when they go live:")
                    ->setWordWrap(true);

                EditableModelView *view =
                    channels
                        .emplace<EditableModelView>(
                            getApp()->getNotifications()->createModel(nullptr,
                                                                      platform))
                        .getElement();
                view->setTitles({title});
                if (!youtube)
                {
                    view->setValidationRegexp(twitchUserNameRegexp());
                }

                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    QHeaderView::Fixed);
                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::Stretch);

                QTimer::singleShot(1, view, [view] {
                    view->getTableView()->resizeColumnsToContents();
                    view->getTableView()->setColumnWidth(0, 200);
                });

                std::ignore = view->addButtonPressed.connect([platform] {
                    getApp()->getNotifications()->addChannelNotification(
                        platform == Platform::YouTube ? "@channel" : "channel",
                        platform);
                });
                if (!youtube)
                {
                    auto pick = channels.emplace<QPushButton>(
                        QStringLiteral("Pick from open tabs..."));
                    pick->setToolTip(QStringLiteral(
                        "Choose channels from your open tabs and folders"));
                    QObject::connect(pick.getElement(), &QPushButton::clicked,
                                     this, [this] {
                                         pickNotificationsFromOpenTabs(this);
                                     });
                }
            }
        }
    }
}
QComboBox *NotificationPage::createToastReactionComboBox()
{
    QComboBox *toastReactionOptions = new QComboBox();

    for (int i = 0; i <= static_cast<int>(ToastReaction::OpenInCustomPlayer);
         i++)
    {
        toastReactionOptions->insertItem(
            i, Toasts::findStringFromReaction(static_cast<ToastReaction>(i)));
    }

    pajlada::Settings::Setting<int> setting = getSettings()->openFromToast;
    setting.connect(
        [toastReactionOptions](const int &index, auto) {
            toastReactionOptions->setCurrentIndex(index);
        },
        this->managedConnections_);

    QObject::connect(toastReactionOptions,
                     QOverload<int>::of(&QComboBox::currentIndexChanged),
                     [](const int &newValue) {
                         getSettings()->openFromToast.setValue(newValue);
                     });

    return toastReactionOptions;
}
}

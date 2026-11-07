#pragma once

#include "widgets/dialogs/SolrinoUpdatePrompt.hpp"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

namespace chatterino {

/// Keeps Solrino up to date from the project's GitHub releases.
///
/// Checks as soon as the app opens. If there's a newer version, the popup shows
/// straight away while the setup downloads behind it. The file's SHA-256 is
/// checked against the release's solrino-update.json before it's used.
/// People choose to update now, when they quit, or to be reminded: the next time
/// they open Solrino, or after some hours if it stays open.
///
/// Moltorino's own updater stays off: its updates would replace Solrino.
class SolrinoUpdater : public QObject
{
    Q_OBJECT

public:
    enum class State {
        /// This build has no release page to check, or is a portable copy
        Unavailable,
        Idle,
        Checking,
        Downloading,
        UpToDate,
        Ready,
        Failed,
    };

    static SolrinoUpdater &instance();

    void start();
    void check(bool byUser);

    /// Call while the app is closing: installs a downloaded update if any.
    void installOnQuit();

    /// Other popups wait while this is checking or showing its own, so that
    /// the update is the first thing people see.
    bool holdingOtherPopups() const;

    State state() const;
    QString statusText() const;

Q_SIGNALS:
    void changed();

private:
    SolrinoUpdater();

    void setState(State state, const QString &error = {});
    void readManifest(const QByteArray &data, bool byUser);
    void download();
    void finishDownload(bool ok, const QString &error);
    void offerRestart();
    void showPromptOnceWindowIsUp(int triesLeft);
    void installNow();
    void runSetup(bool launchAfter);
    QString setupPath() const;
    QString baseUrl() const;
    void removeOldSetups() const;
    void downloadFromWeb();

    State state_ = State::Unavailable;
    QString error_;
    QString onlineVersion_;
    QString onlineHash_;
    QString notes_;
    /// The update log in sections, from newer update files
    QVector<SolrinoUpdatePrompt::Change> changes_;
    bool installWhenQuitting_ = false;
    /// Pressed "Update now" before the download had finished
    bool installWhenReady_ = false;
    /// Chose something on the popup, so it won't come back this session unasked
    bool answered_ = false;
    bool waitingForWindow_ = false;
    bool setupStarted_ = false;
    QPointer<SolrinoUpdatePrompt> prompt_;
    QTimer pollTimer_;
};

}  // namespace chatterino

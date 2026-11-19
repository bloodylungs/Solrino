#include "singletons/SolrinoUpdater.hpp"

#include "Application.hpp"
#include "common/Modes.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "common/SolrinoVersion.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"
#include "util/PostToThread.hpp"
#include "widgets/dialogs/SolrinoUpdatePrompt.hpp"
#include "widgets/Window.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QVersionNumber>
#include <QWindow>
#include <QtConcurrent>

namespace chatterino {

namespace {

constexpr int PROMPT_RETRY_MS = 150;
constexpr int PROMPT_TRIES = 200;
constexpr int CHECK_EVERY_MS = 6 * 60 * 60 * 1000;
constexpr qsizetype MAX_MANIFEST_BYTES = 64 * 1024;
constexpr qsizetype MAX_SETUP_BYTES = 400 * 1024 * 1024;
constexpr int DOWNLOAD_TIMEOUT_MS = 15 * 60 * 1000;
const auto SETUP_PREFIX = QStringLiteral("Solrino-Update-");

QString hashOf(const QByteArray &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

}  // namespace

SolrinoUpdater &SolrinoUpdater::instance()
{
    static auto *updater = new SolrinoUpdater();
    return *updater;
}

SolrinoUpdater::SolrinoUpdater()
{
    this->pollTimer_.setInterval(CHECK_EVERY_MS);
    QObject::connect(&this->pollTimer_, &QTimer::timeout, this, [this] {
        this->check(false);
    });
}

QString SolrinoUpdater::baseUrl() const
{
    return QStringLiteral("https://github.com/%1/releases")
        .arg(QString::fromLatin1(SOLRINO_UPDATE_REPOSITORY));
}

void SolrinoUpdater::start()
{
    // A portable copy has no install to update, and a build with no release
    // page has nowhere to look
    if (Modes::instance().isPortable ||
        QString::fromLatin1(SOLRINO_UPDATE_REPOSITORY).isEmpty())
    {
        return;
    }

    this->setState(State::Idle);
    this->removeOldSetups();
    // Straight away, as soon as the app is running
    QTimer::singleShot(0, this, [this] {
        this->check(false);
    });
    this->pollTimer_.start();
}

void SolrinoUpdater::check(bool byUser)
{
    if (this->state_ == State::Unavailable ||
        this->state_ == State::Checking ||
        this->state_ == State::Downloading)
    {
        return;
    }
    if (this->state_ == State::Ready)
    {
        // Someone who chose "Update later" has answered, unless they ask again
        if (byUser || !this->installWhenQuitting_)
        {
            this->answered_ = false;
            this->offerRestart();
        }
        return;
    }
    if (!byUser && !getSettings()->solrinoAutoUpdate)
    {
        return;
    }

    this->setState(State::Checking);
    NetworkRequest(this->baseUrl() +
                   QStringLiteral("/latest/download/solrino-update.json"))
        .timeout(20000)
        .followRedirects(true)
        .maximumResponseSize(MAX_MANIFEST_BYTES)
        .onSuccess([this, byUser](const NetworkResult &result) {
            this->readManifest(result.getData(), byUser);
        })
        .onError([this](const NetworkResult &) {
            this->setState(State::Failed,
                           QStringLiteral("Couldn't reach the update page."));
        })
        .execute();
}

void SolrinoUpdater::readManifest(const QByteArray &data, bool /*byUser*/)
{
    const auto manifest = QJsonDocument::fromJson(data).object();
    const auto version = manifest["version"].toString();
    const auto hash = manifest["sha256"].toString().toLower();

    static const QRegularExpression validHash(
        QStringLiteral("^[0-9a-f]{64}$"));
    qsizetype parsedLength = 0;
    const auto online = QVersionNumber::fromString(version, &parsedLength);
    if (online.isNull() || parsedLength != version.size() ||
        !validHash.match(hash).hasMatch())
    {
        this->setState(State::Failed,
                       QStringLiteral("The update information was invalid."));
        return;
    }

    // Only ever forward, so an old release can't be pushed back onto anyone
    if (online <=
        QVersionNumber::fromString(QString::fromLatin1(SOLRINO_VERSION)))
    {
        this->setState(State::UpToDate);
        return;
    }

    this->onlineVersion_ = version;
    this->onlineHash_ = hash;
    this->notes_ = manifest["notes"].toString().simplified().left(300);
    this->changes_.clear();
    // Newer update files also list each change, so the card can show sections
    for (const auto &value : manifest["changes"].toArray())
    {
        const auto item = value.toObject();
        SolrinoUpdatePrompt::Change change{
            .kind = item["kind"].toString().trimmed().toLower(),
            .title = item["title"].toString().simplified().left(140),
            .detail = item["detail"].toString().simplified().left(240),
        };
        if (!change.title.isEmpty() && this->changes_.size() < 8)
        {
            this->changes_.push_back(change);
        }
    }
    this->answered_ = false;
    this->download();
    // Doesn't wait for the download, which can take a while
    this->offerRestart();
}

void SolrinoUpdater::download()
{
    this->setState(State::Downloading);

    // Already fetched on an earlier run: the popup can appear right away
    (void)QtConcurrent::run([this, hash = this->onlineHash_,
                             path = this->setupPath()] {
        QFile file(path);
        const bool alreadyHere = file.open(QIODevice::ReadOnly) &&
                                 hashOf(file.readAll()) == hash;
        postToThread([this, alreadyHere] {
            if (alreadyHere)
            {
                this->finishDownload(true, {});
            }
            else
            {
                this->downloadFromWeb();
            }
        });
    });
}

void SolrinoUpdater::downloadFromWeb()
{
    // The address comes from the version, not from anything in the manifest,
    // so the manifest can't point the download somewhere else.
    const auto url = this->baseUrl() +
                     QStringLiteral("/download/v%1/Solrino-Setup.exe")
                         .arg(this->onlineVersion_);
    NetworkRequest(url)
        .timeout(DOWNLOAD_TIMEOUT_MS)
        .followRedirects(true)
        .maximumResponseSize(MAX_SETUP_BYTES)
        .onSuccess([this](const NetworkResult &result) {
            // Hashing and saving a big file would freeze the chat for a moment
            const auto data = result.getData();
            const auto hash = this->onlineHash_;
            const auto path = this->setupPath();
            (void)QtConcurrent::run([this, data, hash, path] {
                QString error;
                if (hashOf(data) != hash)
                {
                    error = QStringLiteral(
                        "The download didn't match, so it was thrown away.");
                }
                else
                {
                    QSaveFile file(path);
                    if (!file.open(QIODevice::WriteOnly) ||
                        file.write(data) != data.size() || !file.commit())
                    {
                        error =
                            QStringLiteral("The update couldn't be saved.");
                    }
                }
                postToThread([this, error] {
                    this->finishDownload(error.isEmpty(), error);
                });
            });
        })
        .onError([this](const NetworkResult &) {
            this->finishDownload(false, QStringLiteral("The download failed."));
        })
        .execute();
}

void SolrinoUpdater::finishDownload(bool ok, const QString &error)
{
    if (!ok)
    {
        this->installWhenReady_ = false;
        this->setState(State::Failed, error);
        if (this->prompt_ != nullptr)
        {
            this->prompt_->showProblem(error);
        }
        return;
    }
    this->setState(State::Ready);
    if (this->installWhenReady_)
    {
        this->installNow();
        return;
    }
    if (this->prompt_ != nullptr)
    {
        this->prompt_->setReady();
    }
    else if (!this->answered_)
    {
        this->offerRestart();
    }
}

void SolrinoUpdater::installNow()
{
    this->installWhenReady_ = false;
    this->runSetup(true);
    if (this->setupStarted_)
    {
        if (this->prompt_ != nullptr)
        {
            this->prompt_->close();
        }
        requestApplicationQuit();
    }
    else if (this->prompt_ != nullptr)
    {
        this->prompt_->showProblem(this->error_);
    }
}

bool SolrinoUpdater::holdingOtherPopups() const
{
    return this->state_ == State::Checking || this->prompt_ != nullptr ||
           this->waitingForWindow_;
}

void SolrinoUpdater::offerRestart()
{
    if (isAppAboutToQuit())
    {
        return;
    }
    if (this->prompt_ != nullptr)
    {
        this->prompt_->raise();
        return;
    }
    this->waitingForWindow_ = true;
    this->showPromptOnceWindowIsUp(PROMPT_TRIES);
}

void SolrinoUpdater::showPromptOnceWindowIsUp(int triesLeft)
{
    if (isAppAboutToQuit())
    {
        return;
    }
    // The first launch plays an intro before the window opens. The popup comes
    // after that, so it isn't lost behind the window. Someone who starts Solrino
    // hidden in the tray still gets it after a while.
    if (triesLeft > 0 && !getApp()->getWindows()->getMainWindow().isVisible())
    {
        QTimer::singleShot(PROMPT_RETRY_MS, this, [this, triesLeft] {
            this->showPromptOnceWindowIsUp(triesLeft - 1);
        });
        return;
    }
    this->waitingForWindow_ = false;
    if (this->prompt_ != nullptr)
    {
        return;
    }

    auto *prompt = new SolrinoUpdatePrompt(
        this->onlineVersion_, this->notes_,
        [this](SolrinoUpdatePrompt::Choice choice) {
            this->answered_ = true;
            switch (choice)
            {
                case SolrinoUpdatePrompt::Choice::UpdateNow:
                    if (this->state_ == State::Ready)
                    {
                        this->installNow();
                    }
                    else
                    {
                        this->installWhenReady_ = true;
                        if (this->state_ == State::Failed)
                        {
                            this->download();
                        }
                    }
                    break;
                case SolrinoUpdatePrompt::Choice::UpdateLater:
                    this->installWhenQuitting_ = true;
                    break;
                case SolrinoUpdatePrompt::Choice::RemindLater:
                    this->installWhenQuitting_ = false;
                    break;
            }
            Q_EMIT this->changed();
        },
        this->changes_);
    this->prompt_ = prompt;
    if (this->state_ == State::Ready)
    {
        prompt->setReady();
    }
    else if (this->state_ == State::Failed)
    {
        prompt->showProblem(this->error_);
    }
    // Belongs to the main window, so that window can never come up over it. It's
    // still not on top of other apps.
    prompt->winId();
    auto &window = getApp()->getWindows()->getMainWindow();
    window.winId();
    if (prompt->windowHandle() != nullptr)
    {
        prompt->windowHandle()->setTransientParent(window.windowHandle());
    }
    prompt->show();
    prompt->raise();
}

void SolrinoUpdater::runSetup(bool launchAfter)
{
    if (this->setupStarted_ || this->state_ != State::Ready)
    {
        return;
    }

    QStringList arguments{
        QStringLiteral("--update"),
        QStringLiteral("--dest"),
        QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
    };
    if (!launchAfter)
    {
        arguments << QStringLiteral("--no-launch");
    }

    this->setupStarted_ = QProcess::startDetached(this->setupPath(), arguments);
    if (!this->setupStarted_)
    {
        this->setState(State::Failed,
                       QStringLiteral("The update couldn't be started."));
    }
}

void SolrinoUpdater::installOnQuit()
{
    if (this->installWhenQuitting_)
    {
        this->runSetup(false);
    }
}

QString SolrinoUpdater::setupPath() const
{
    return QDir(getApp()->getPaths().miscDirectory)
        .filePath(SETUP_PREFIX + this->onlineVersion_ + ".exe");
}

void SolrinoUpdater::removeOldSetups() const
{
    QDir misc(getApp()->getPaths().miscDirectory);
    const auto current =
        QVersionNumber::fromString(QString::fromLatin1(SOLRINO_VERSION));
    const auto setups = misc.entryList({SETUP_PREFIX + "*.exe"}, QDir::Files);
    for (const auto &name : setups)
    {
        // A newer one is kept for the popup. Already installed ones are not.
        const auto version = QVersionNumber::fromString(
            name.mid(SETUP_PREFIX.size()).chopped(4));
        if (version <= current)
        {
            misc.remove(name);
        }
    }
}

void SolrinoUpdater::setState(State state, const QString &error)
{
    this->state_ = state;
    this->error_ = error;
    Q_EMIT this->changed();
}

SolrinoUpdater::State SolrinoUpdater::state() const
{
    return this->state_;
}

QString SolrinoUpdater::statusText() const
{
    const auto current = QString::fromLatin1(SOLRINO_VERSION);
    switch (this->state_)
    {
        case State::Unavailable:
            if (Modes::instance().isPortable)
            {
                return QStringLiteral(
                           "Version %1. Portable copies don't update.")
                    .arg(current);
            }
            return QStringLiteral("Version %1. This build isn't connected to "
                                  "a release page yet.")
                .arg(current);
        case State::Idle:
            return QStringLiteral("Version %1.").arg(current);
        case State::Checking:
            return QStringLiteral("Version %1. Checking for updates...")
                .arg(current);
        case State::Downloading:
            return QStringLiteral("Version %1. Downloading %2...")
                .arg(current, this->onlineVersion_);
        case State::UpToDate:
            return QStringLiteral("Version %1. You're up to date.")
                .arg(current);
        case State::Ready:
            return (this->installWhenQuitting_
                        ? QStringLiteral("Version %1. Version %2 will install "
                                         "when you fully quit Solrino.")
                        : QStringLiteral("Version %1. Version %2 is ready. "
                                         "Click below to install it."))
                .arg(current, this->onlineVersion_);
        case State::Failed:
            return QStringLiteral("Version %1. %2").arg(current, this->error_);
    }
    return current;
}

}  // namespace chatterino

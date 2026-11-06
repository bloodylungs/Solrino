#include "singletons/SolrinoDiscordPresence.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/QLogging.hpp"
#include "common/SolrinoVersion.hpp"
#include "singletons/Settings.hpp"
#include "widgets/dialogs/SolrinoTools.hpp"
#include "widgets/splits/Split.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QtEndian>
#include <QUuid>

namespace chatterino {

namespace {

constexpr int OP_HANDSHAKE = 0;
constexpr int OP_FRAME = 1;
constexpr int OP_CLOSE = 2;
constexpr int OP_PING = 3;
constexpr int OP_PONG = 4;
constexpr int HEADER_SIZE = 8;
constexpr int MAX_FRAME = 64 * 1024;
constexpr int PIPE_COUNT = 10;
/// Shown as "Watching Solrino" rather than "Playing Solrino", so that it isn't
/// competing with the games someone is actually playing.
constexpr int ACTIVITY_TYPE_WATCHING = 3;

constexpr int TICK_MS = 2000;
constexpr qint64 RETRY_MS = 20000;
/// Discord only shows about one activity change per 15 seconds.
constexpr qint64 MIN_SEND_GAP_MS = 15000;

QString applicationId()
{
    return QString::fromLatin1(SOLRINO_DISCORD_APP_ID);
}

QString pipeName(int number)
{
    return QStringLiteral("discord-ipc-%1").arg(number);
}

}  // namespace

SolrinoDiscordPresence &SolrinoDiscordPresence::instance()
{
    static auto *presence = new SolrinoDiscordPresence();
    return *presence;
}

SolrinoDiscordPresence::SolrinoDiscordPresence()
{
    this->timer_.setInterval(TICK_MS);
    QObject::connect(&this->timer_, &QTimer::timeout, this, [this] {
        this->tick();
    });
}

void SolrinoDiscordPresence::start()
{
    if (applicationId().isEmpty())
    {
        return;
    }
    this->startedAt_ = QDateTime::currentSecsSinceEpoch();
    this->timer_.start();
    this->tick();
}

void SolrinoDiscordPresence::readCurrentActivity(QString &details,
                                                 QString &state) const
{
    auto *split = solrino::currentSplit();
    const auto channel =
        split != nullptr ? split->getSelectedChannel() : nullptr;
    const auto name = channel != nullptr ? channel->getName() : QString{};
    const bool inChat = !name.isEmpty() && !name.startsWith('/');
    const auto doing = inChat && split->getModerationMode()
                           ? QStringLiteral("Modding")
                           : QStringLiteral("Chatting");

    if (!getSettings()->solrinoDiscordShowChat)
    {
        details = doing;
        state.clear();
    }
    else if (inChat)
    {
        details = QStringLiteral("In #%1's chat").arg(name);
        state = doing;
    }
    else
    {
        details = QStringLiteral("Browsing chats");
        state.clear();
    }
}

void SolrinoDiscordPresence::tick()
{
    if (isAppAboutToQuit())
    {
        return;
    }

    if (!getSettings()->solrinoDiscordPresence)
    {
        this->disconnectFromDiscord();
        return;
    }

    if (this->socket_ == nullptr)
    {
        if (!this->sinceLastAttempt_.isValid() ||
            this->sinceLastAttempt_.elapsed() >= RETRY_MS)
        {
            this->sinceLastAttempt_.start();
            this->pipeNumber_ = 0;
            this->connectToDiscord();
        }
        return;
    }

    if (!this->ready_)
    {
        return;
    }

    QString details;
    QString state;
    this->readCurrentActivity(details, state);
    if ((details != this->lastDetails_ || state != this->lastState_) &&
        (!this->sentOnce_ || this->sinceLastSend_.elapsed() >= MIN_SEND_GAP_MS))
    {
        this->lastDetails_ = details;
        this->lastState_ = state;
        this->sendActivity();
    }
}

void SolrinoDiscordPresence::connectToDiscord()
{
    auto *socket = new QLocalSocket(this);
    this->socket_ = socket;
    this->received_.clear();
    this->ready_ = false;
    this->sentOnce_ = false;
    this->lastDetails_.clear();
    this->lastState_.clear();

    QObject::connect(socket, &QLocalSocket::connected, this, [this] {
        QJsonObject hello;
        hello["v"] = 1;
        hello["client_id"] = applicationId();
        this->sendFrame(OP_HANDSHAKE, hello);
    });
    QObject::connect(socket, &QLocalSocket::readyRead, this, [this] {
        this->onReadable();
    });
    QObject::connect(socket, &QLocalSocket::errorOccurred, this,
                     [this, socket](QLocalSocket::LocalSocketError) {
                         if (this->socket_ != socket)
                         {
                             return;
                         }
                         const bool neverConnected = !this->ready_;
                         this->disconnectFromDiscord();
                         // Discord uses the first free of ten pipes
                         if (neverConnected &&
                             ++this->pipeNumber_ < PIPE_COUNT)
                         {
                             this->connectToDiscord();
                         }
                     });
    QObject::connect(socket, &QLocalSocket::disconnected, this,
                     [this, socket] {
                         if (this->socket_ == socket)
                         {
                             this->disconnectFromDiscord();
                         }
                     });

    socket->connectToServer(pipeName(this->pipeNumber_));
}

void SolrinoDiscordPresence::disconnectFromDiscord()
{
    if (this->socket_ == nullptr)
    {
        return;
    }
    auto *socket = this->socket_;
    this->socket_ = nullptr;
    this->ready_ = false;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}

void SolrinoDiscordPresence::onReadable()
{
    if (this->socket_ == nullptr)
    {
        return;
    }
    this->received_.append(this->socket_->readAll());

    while (this->received_.size() >= HEADER_SIZE)
    {
        const auto opcode =
            qFromLittleEndian<qint32>(this->received_.constData());
        const auto length =
            qFromLittleEndian<qint32>(this->received_.constData() + 4);
        if (length < 0 || length > MAX_FRAME)
        {
            this->disconnectFromDiscord();
            return;
        }
        if (this->received_.size() < HEADER_SIZE + length)
        {
            return;
        }

        const auto payload = this->received_.mid(HEADER_SIZE, length);
        this->received_.remove(0, HEADER_SIZE + length);
        const auto body = QJsonDocument::fromJson(payload).object();

        if (opcode == OP_PING)
        {
            this->sendFrame(OP_PONG, body);
        }
        else if (opcode == OP_CLOSE)
        {
            qCWarning(chatterinoApp)
                << "Discord closed the connection:" << body["message"].toString();
            this->disconnectFromDiscord();
            return;
        }
        else if (opcode == OP_FRAME && body["evt"].toString() == "READY")
        {
            this->ready_ = true;
            this->tick();
        }
        else if (opcode == OP_FRAME && body["evt"].toString() == "ERROR")
        {
            qCWarning(chatterinoApp)
                << "Discord rejected the activity:"
                << body["data"].toObject()["message"].toString();
        }

        if (this->socket_ == nullptr)
        {
            return;
        }
    }
}

void SolrinoDiscordPresence::sendFrame(int opcode, const QJsonObject &body)
{
    if (this->socket_ == nullptr)
    {
        return;
    }
    const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QByteArray frame(HEADER_SIZE, Qt::Uninitialized);
    qToLittleEndian<qint32>(opcode, frame.data());
    qToLittleEndian<qint32>(qint32(payload.size()), frame.data() + 4);
    frame.append(payload);
    this->socket_->write(frame);
    this->socket_->flush();
}

void SolrinoDiscordPresence::sendActivity()
{
    QJsonObject assets;
    assets["large_image"] = QStringLiteral(SOLRINO_DISCORD_LOGO_URL);
    assets["large_text"] = QStringLiteral("Solrino");

    QJsonObject timestamps;
    timestamps["start"] = this->startedAt_;

    QJsonObject activity;
    activity["type"] = ACTIVITY_TYPE_WATCHING;
    activity["details"] = this->lastDetails_;
    if (!this->lastState_.isEmpty())
    {
        activity["state"] = this->lastState_;
    }
    activity["assets"] = assets;
    activity["timestamps"] = timestamps;

    QJsonObject args;
    args["pid"] = qint64(QCoreApplication::applicationPid());
    args["activity"] = activity;

    QJsonObject command;
    command["cmd"] = QStringLiteral("SET_ACTIVITY");
    command["args"] = args;
    command["nonce"] = QUuid::createUuid().toString(QUuid::WithoutBraces);

    this->sendFrame(OP_FRAME, command);
    this->sentOnce_ = true;
    this->sinceLastSend_.start();
}

}  // namespace chatterino

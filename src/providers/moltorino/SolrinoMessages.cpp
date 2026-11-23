#include "providers/moltorino/SolrinoMessages.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "common/SolrinoVersion.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/moltorino/SolrinoCrypto.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Paths.hpp"
#include "util/PostToThread.hpp"
#include "widgets/helper/SolrinoPing.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace chatterino {

namespace crypto = solrino::crypto;

namespace {

constexpr int MAX_TEXT = 2000;
constexpr int MAX_KEPT = 1000;
constexpr int POLL_MS = 60 * 1000;
constexpr int RECONNECT_MS = 15 * 1000;
constexpr int DIRECTORY_MINUTES = 10;
const QByteArray INFO = "solrino-dm-v1|";

QString server()
{
    return QString::fromLatin1(SOLRINO_BADGE_SERVER);
}

QString b64(const QByteArray &data)
{
    return QString::fromLatin1(data.toBase64());
}

QByteArray unb64(const QJsonValue &value)
{
    return QByteArray::fromBase64(value.toString().toLatin1(),
                                  QByteArray::AbortOnBase64DecodingErrors);
}

pajlada::Signals::SignalHolder &holder()
{
    static pajlada::Signals::SignalHolder signals;
    return signals;
}

bool writeProtected(const QString &path, const QJsonObject &object)
{
    auto plain = QJsonDocument(object).toJson(QJsonDocument::Compact);
    auto locked = crypto::protectForThisUser(plain);
    crypto::wipe(plain);
    if (locked.isEmpty())
    {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }
    file.write(locked);
    if (!file.commit())
    {
        return false;
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

std::optional<QJsonObject> readProtected(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }
    auto plain = crypto::unprotectForThisUser(file.readAll());
    if (!plain)
    {
        return std::nullopt;
    }
    auto object = QJsonDocument::fromJson(*plain).object();
    crypto::wipe(*plain);
    return object;
}

class MessagesSocketListener : public WebSocketListener
{
public:
    explicit MessagesSocketListener(int generation)
        : generation_(generation)
    {
    }

    void onOpen() override
    {
        runInGuiThread([generation = this->generation_] {
            SolrinoMessages::instance().socketOpened(generation);
        });
    }

    void onTextMessage(QByteArray data) override
    {
        if (data.size() > 4096)
        {
            return;
        }
        runInGuiThread([generation = this->generation_,
                        data = std::move(data)] {
            SolrinoMessages::instance().socketMessage(generation, data);
        });
    }

    void onBinaryMessage(QByteArray data) override
    {
        this->onTextMessage(std::move(data));
    }

    void onClose(std::unique_ptr<WebSocketListener> /*self*/) override
    {
        runInGuiThread([generation = this->generation_] {
            SolrinoMessages::instance().socketClosed(generation);
        });
    }

private:
    int generation_;
};

}  // namespace

SolrinoMessages &SolrinoMessages::instance()
{
    static auto *messages = new SolrinoMessages();
    return *messages;
}

SolrinoMessages::SolrinoMessages()
{
    this->reconnect_.setSingleShot(true);
    QObject::connect(&this->reconnect_, &QTimer::timeout, this, [this] {
        this->connectSocket();
    });
    // In case the socket misses something
    this->poll_.setInterval(POLL_MS);
    QObject::connect(&this->poll_, &QTimer::timeout, this, [this] {
        this->fetchInbox();
    });
}

void SolrinoMessages::start()
{
    holder().managedConnect(getApp()->getAccounts()->twitch.currentUserChanged,
                            [this] {
                                this->switchAccount();
                            });
    this->switchAccount();
}

bool SolrinoMessages::ready() const
{
    return this->keys_ != nullptr && this->published_;
}

QString SolrinoMessages::problem() const
{
    return this->problem_;
}

QString SolrinoMessages::myUserId() const
{
    return this->userId_;
}

QString SolrinoMessages::myLogin() const
{
    return this->login_;
}

const QMap<QString, SolrinoMessages::Conversation> &
    SolrinoMessages::conversations() const
{
    return this->conversations_;
}

const SolrinoMessages::Conversation *SolrinoMessages::conversation(
    const QString &userId) const
{
    const auto found = this->conversations_.constFind(userId);
    return found == this->conversations_.constEnd() ? nullptr : &found.value();
}

int SolrinoMessages::unreadCount() const
{
    int count = 0;
    for (const auto &c : this->conversations_)
    {
        if (c.accepted && !c.blocked)
        {
            count += c.unread;
        }
    }
    return count;
}

int SolrinoMessages::requestCount() const
{
    int count = 0;
    for (const auto &c : this->conversations_)
    {
        if (!c.accepted && !c.blocked && !c.messages.empty())
        {
            count++;
        }
    }
    return count;
}

QByteArray SolrinoMessages::authHeader() const
{
    return ("OAuth " + this->token_).toUtf8();
}

QString SolrinoMessages::folder() const
{
    return getApp()->getPaths().rootAppDataDirectory +
           QStringLiteral("/Messages/") + this->userId_;
}

void SolrinoMessages::stopAll()
{
    ++this->socketGeneration_;
    this->socket_.close();
    this->socket_ = WebSocketHandle();
    this->socketOpen_ = false;
    this->reconnect_.stop();
    this->poll_.stop();
    this->queue_.clear();
    this->fetching_ = false;
    if (this->keys_)
    {
        crypto::wipe(this->keys_->boxPrivate);
        crypto::wipe(this->keys_->signPrivate);
    }
    this->keys_.reset();
    this->published_ = false;
    this->conversations_.clear();
    this->directory_.clear();
}

void SolrinoMessages::switchAccount()
{
    this->stopAll();
    const auto account = getApp()->getAccounts()->twitch.getCurrent();
    if (account->isAnon())
    {
        this->userId_.clear();
        this->problem_ = QStringLiteral("Log in with Twitch to use messages.");
        Q_EMIT this->changed();
        return;
    }
    this->userId_ = account->getUserId();
    this->login_ = account->getUserName().toLower();
    this->token_ = account->getOAuthToken();
    this->problem_.clear();

    QDir().mkpath(this->folder());
    if (!this->loadKeys())
    {
        this->problem_ =
            QStringLiteral("Couldn't make the keys for messages on this PC.");
        Q_EMIT this->changed();
        return;
    }
    this->loadHistory();
    Q_EMIT this->changed();
    this->publishKeys();
}

bool SolrinoMessages::loadKeys()
{
    const auto path = this->folder() + QStringLiteral("/keys.dat");
    if (auto saved = readProtected(path))
    {
        auto keys = std::make_unique<Keys>();
        keys->device = saved->value("device").toString();
        keys->boxPrivate = unb64(saved->value("boxPrivate"));
        keys->boxPublic = unb64(saved->value("boxPublic"));
        keys->signPrivate = unb64(saved->value("signPrivate"));
        keys->signPublic = unb64(saved->value("signPublic"));
        if (QRegularExpression(QStringLiteral("^[0-9a-f]{32}$"))
                .match(keys->device)
                .hasMatch() &&
            keys->boxPrivate.size() == 32 && keys->boxPublic.size() == 32 &&
            keys->signPrivate.size() == 32 && keys->signPublic.size() == 32)
        {
            this->keys_ = std::move(keys);
            return true;
        }
    }

    // First time on this PC: new keys that never leave it
    auto box = crypto::newBoxKeys();
    auto sign = crypto::newSignKeys();
    if (!box || !sign)
    {
        return false;
    }
    auto keys = std::make_unique<Keys>();
    keys->device = QString::fromLatin1(crypto::randomBytes(16).toHex());
    keys->boxPrivate = box->privateKey;
    keys->boxPublic = box->publicKey;
    keys->signPrivate = sign->privateKey;
    keys->signPublic = sign->publicKey;
    crypto::wipe(box->privateKey);
    crypto::wipe(sign->privateKey);

    QJsonObject saved{
        {"device", keys->device},
        {"boxPrivate", b64(keys->boxPrivate)},
        {"boxPublic", b64(keys->boxPublic)},
        {"signPrivate", b64(keys->signPrivate)},
        {"signPublic", b64(keys->signPublic)},
    };
    if (!writeProtected(path, saved))
    {
        return false;
    }
    this->keys_ = std::move(keys);
    return true;
}

bool SolrinoMessages::loadHistory()
{
    this->conversations_.clear();
    const auto saved =
        readProtected(this->folder() + QStringLiteral("/history.dat"));
    if (!saved)
    {
        return false;
    }
    for (const auto &value : saved->value("conversations").toArray())
    {
        const auto object = value.toObject();
        Conversation c;
        c.userId = object.value("userId").toString();
        if (c.userId.isEmpty())
        {
            continue;
        }
        c.login = object.value("login").toString();
        c.accepted = object.value("accepted").toBool();
        c.blocked = object.value("blocked").toBool();
        c.keysChanged = object.value("keysChanged").toBool();
        c.unread = object.value("unread").toInt();
        for (const auto &k : object.value("pinned").toArray())
        {
            c.pinned.append(k.toString());
        }
        for (const auto &m : object.value("messages").toArray())
        {
            const auto mo = m.toObject();
            Message message;
            message.id = mo.value("id").toString();
            message.mine = mo.value("mine").toBool();
            message.text = mo.value("text").toString();
            message.at = QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(mo.value("at").toDouble()), QTimeZone::UTC);
            // Anything still sending when Solrino closed didn't make it
            message.status = mo.value("status").toString() == "sent"
                                 ? Status::Sent
                                 : Status::Failed;
            c.messages.push_back(message);
        }
        this->conversations_.insert(c.userId, c);
    }
    return true;
}

void SolrinoMessages::saveHistory()
{
    if (this->userId_.isEmpty() || !this->keys_)
    {
        return;
    }
    QJsonArray list;
    for (const auto &c : this->conversations_)
    {
        QJsonArray messages;
        for (const auto &m : c.messages)
        {
            messages.append(QJsonObject{
                {"id", m.id},
                {"mine", m.mine},
                {"text", m.text},
                {"at", static_cast<double>(m.at.toMSecsSinceEpoch())},
                {"status", m.status == Status::Sent ? "sent" : "failed"},
            });
        }
        list.append(QJsonObject{
            {"userId", c.userId},
            {"login", c.login},
            {"accepted", c.accepted},
            {"blocked", c.blocked},
            {"keysChanged", c.keysChanged},
            {"unread", c.unread},
            {"pinned", QJsonArray::fromStringList(c.pinned)},
            {"messages", messages},
        });
    }
    writeProtected(this->folder() + QStringLiteral("/history.dat"),
                   QJsonObject{{"conversations", list}});
}

void SolrinoMessages::publishKeys()
{
    if (!this->keys_)
    {
        return;
    }
    const auto userId = this->userId_;
    NetworkRequest(server() + QStringLiteral("/v2/dm/keys"),
                   NetworkRequestType::Put)
        .header("Authorization", this->authHeader())
        .json(QJsonObject{
            {"device", this->keys_->device},
            {"box", b64(this->keys_->boxPublic)},
            {"sign", b64(this->keys_->signPublic)},
        })
        .timeout(15000)
        .onSuccess([this, userId](const NetworkResult &) {
            if (userId != this->userId_)
            {
                return;
            }
            this->published_ = true;
            this->problem_.clear();
            this->directory_.remove(userId);
            Q_EMIT this->changed();
            this->connectSocket();
            this->poll_.start();
            this->fetchInbox();
        })
        .onError([this, userId](const NetworkResult &) {
            if (userId != this->userId_)
            {
                return;
            }
            this->problem_ = QStringLiteral(
                "Couldn't reach Solrino's server. Trying again soon.");
            Q_EMIT this->changed();
            QTimer::singleShot(RECONNECT_MS, this, [this, userId] {
                if (userId == this->userId_ && !this->published_)
                {
                    this->publishKeys();
                }
            });
        })
        .execute();
}

void SolrinoMessages::connectSocket()
{
    if (!this->ready() || this->socketOpen_)
    {
        return;
    }
    if (!this->pool_)
    {
        this->pool_ =
            std::make_unique<WebSocketPool>(QStringLiteral("Solrino messages"));
    }
    const auto generation = ++this->socketGeneration_;
    QUrl url(server().replace(QStringLiteral("https://"),
                              QStringLiteral("wss://")) +
             QStringLiteral("/v2/dm/socket"));
    this->socket_ = this->pool_->createSocket(
        WebSocketOptions{
            .url = url,
            .headers = {{"Authorization", this->authHeader().toStdString()}},
            .maxMessageBytes = 4096,
        },
        std::make_unique<MessagesSocketListener>(generation));
}

void SolrinoMessages::socketOpened(int generation)
{
    if (generation != this->socketGeneration_)
    {
        return;
    }
    this->socketOpen_ = true;
    this->fetchInbox();
}

void SolrinoMessages::socketMessage(int generation, const QByteArray &data)
{
    if (generation != this->socketGeneration_)
    {
        return;
    }
    if (QJsonDocument::fromJson(data).object().value("type").toString() ==
        QStringLiteral("dm"))
    {
        this->fetchInbox();
    }
}

void SolrinoMessages::socketClosed(int generation)
{
    if (generation != this->socketGeneration_)
    {
        return;
    }
    this->socketOpen_ = false;
    this->reconnect_.start(RECONNECT_MS);
}

void SolrinoMessages::fetchInbox()
{
    if (!this->ready())
    {
        return;
    }
    if (this->fetching_)
    {
        this->fetchAgain_ = true;
        return;
    }
    this->fetching_ = true;
    this->fetchAgain_ = false;
    const auto userId = this->userId_;
    NetworkRequest(server() + QStringLiteral("/v2/dm/inbox?device=") +
                   this->keys_->device)
        .header("Authorization", this->authHeader())
        .timeout(15000)
        .maximumResponseSize(8 * 1024 * 1024)
        .onSuccess([this, userId](const NetworkResult &result) {
            if (userId != this->userId_)
            {
                return;
            }
            for (const auto &value :
                 result.parseJson().value("messages").toArray())
            {
                this->queue_.push_back(value.toObject());
            }
            this->processNext();
        })
        .onError([this, userId](const NetworkResult &) {
            if (userId == this->userId_)
            {
                this->fetching_ = false;
            }
        })
        .execute();
}

void SolrinoMessages::processNext()
{
    if (this->queue_.empty())
    {
        this->acknowledge(this->done_);
        this->done_.clear();
        this->fetching_ = false;
        this->saveHistory();
        Q_EMIT this->changed();
        if (this->fetchAgain_)
        {
            this->fetchInbox();
        }
        return;
    }
    const auto envelope = this->queue_.front();
    this->queue_.pop_front();
    const auto id = envelope.value("id").toString();
    // Every envelope is deleted from the server, even ones that can't be
    // opened, so nothing piles up
    this->done_.append(id);

    auto inner = this->openEnvelope(envelope);
    if (!inner)
    {
        qCDebug(chatterinoApp) << "A Solrino message couldn't be opened";
        QTimer::singleShot(0, this, [this] {
            this->processNext();
        });
        return;
    }
    this->handleOpened(id, *inner, [this] {
        this->processNext();
    });
}

std::optional<QByteArray> SolrinoMessages::openEnvelope(
    const QJsonObject &envelope) const
{
    const auto eph = unb64(envelope.value("eph"));
    const auto nonce = unb64(envelope.value("nonce"));
    const auto sealed = unb64(envelope.value("ct"));
    if (eph.size() != 32 || nonce.size() != 12 || sealed.isEmpty())
    {
        return std::nullopt;
    }
    auto secret = crypto::agree(this->keys_->boxPrivate, eph);
    if (!secret)
    {
        return std::nullopt;
    }
    const auto salt = eph + this->keys_->boxPublic;
    auto key =
        crypto::deriveKey(*secret, salt, INFO + this->keys_->device.toLatin1());
    crypto::wipe(*secret);
    if (!key)
    {
        return std::nullopt;
    }
    auto inner = crypto::open(*key, nonce, salt, sealed);
    crypto::wipe(*key);
    return inner;
}

void SolrinoMessages::handleOpened(const QString & /*envelopeId*/,
                                   const QByteArray &inner,
                                   std::function<void()> next)
{
    const auto wrapper = QJsonDocument::fromJson(inner).object();
    const auto payloadBytes = unb64(wrapper.value("p"));
    const auto signature = unb64(wrapper.value("s"));
    const auto payload = QJsonDocument::fromJson(payloadBytes).object();
    const auto from = payload.value("from").toObject();
    const auto fromUser = from.value("u").toString();
    const auto fromDevice = from.value("d").toString();
    const auto to = payload.value("to").toString();
    const auto text = payload.value("text").toString().left(MAX_TEXT);
    const auto id = payload.value("id").toString().left(64);
    const auto at = QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>(payload.value("at").toDouble()), QTimeZone::UTC);
    if (payload.value("v").toInt() != 1 || fromUser.isEmpty() ||
        to.isEmpty() || id.isEmpty() || text.isEmpty())
    {
        QTimer::singleShot(0, this, next);
        return;
    }
    // A copy of something sent from our other device, or a message for us.
    // Anything else was meant for someone else and is ignored.
    const bool mine = fromUser == this->userId_;
    if (!mine && to != this->userId_)
    {
        QTimer::singleShot(0, this, next);
        return;
    }
    const auto peer = mine ? to : fromUser;

    auto check = [this, payloadBytes, signature, fromUser, fromDevice, peer,
                  mine, text, id, at, next](const Directory *directory) {
        bool trusted = false;
        QString login;
        if (directory != nullptr)
        {
            for (const auto &device : directory->devices)
            {
                if (device.id == fromDevice &&
                    crypto::verify(device.sign, payloadBytes, signature))
                {
                    trusted = true;
                }
            }
            login = directory->login;
        }
        if (!trusted)
        {
            // Not signed by one of their devices: dropped
            qCDebug(chatterinoApp)
                << "Dropped a Solrino message with a bad signature";
            next();
            return;
        }

        auto &c = this->conversations_[peer];
        c.userId = peer;
        if (!mine && !login.isEmpty())
        {
            c.login = login;
        }
        if (c.blocked)
        {
            next();
            return;
        }
        for (const auto &m : c.messages)
        {
            if (m.id == id)
            {
                next();
                return;
            }
        }
        Message message;
        message.id = id;
        message.mine = mine;
        message.text = text;
        message.at = at.isValid() ? at : QDateTime::currentDateTimeUtc();
        message.status = Status::Sent;
        c.messages.push_back(message);
        std::stable_sort(c.messages.begin(), c.messages.end(),
                         [](const Message &a, const Message &b) {
                             return a.at < b.at;
                         });
        if (c.messages.size() > MAX_KEPT)
        {
            c.messages.erase(c.messages.begin(),
                             c.messages.begin() +
                                 static_cast<long long>(c.messages.size() -
                                                        MAX_KEPT));
        }
        if (mine)
        {
            c.accepted = true;
        }
        else
        {
            c.unread++;
            Q_EMIT this->arrived(peer, text);
            // Only people you've accepted get a ping card
            if (c.accepted)
            {
                showSolrinoPing(c.login.isEmpty() ? peer : c.login,
                                QStringLiteral("Solrino messages"), text, true,
                                c.login);
            }
        }
        next();
    };

    // Our own other devices sign with keys under our own account
    const bool known = this->directory_.contains(fromUser) &&
                       std::any_of(this->directory_[fromUser].devices.begin(),
                                   this->directory_[fromUser].devices.end(),
                                   [&](const Device &d) {
                                       return d.id == fromDevice;
                                   });
    this->lookUpKeys(fromUser, !known, check);
}

void SolrinoMessages::acknowledge(const QStringList &ids)
{
    if (ids.isEmpty() || !this->ready())
    {
        return;
    }
    NetworkRequest(server() + QStringLiteral("/v2/dm/ack"),
                   NetworkRequestType::Post)
        .header("Authorization", this->authHeader())
        .json(QJsonObject{
            {"device", this->keys_->device},
            {"ids", QJsonArray::fromStringList(ids)},
        })
        .timeout(15000)
        .execute();
}

void SolrinoMessages::lookUpKeys(const QString &userId, bool fresh,
                                 std::function<void(const Directory *)> done)
{
    const auto found = this->directory_.constFind(userId);
    if (!fresh && found != this->directory_.constEnd() &&
        found->fetched.secsTo(QDateTime::currentDateTimeUtc()) <
            DIRECTORY_MINUTES * 60)
    {
        done(&found.value());
        return;
    }
    const auto me = this->userId_;
    NetworkRequest(server() + QStringLiteral("/v2/dm/keys?userId=") + userId)
        .timeout(15000)
        .onSuccess([this, me, userId, done](const NetworkResult &result) {
            if (me != this->userId_)
            {
                return;
            }
            const auto body = result.parseJson();
            Directory directory;
            directory.login = body.value("login").toString();
            directory.fetched = QDateTime::currentDateTimeUtc();
            for (const auto &value : body.value("devices").toArray())
            {
                const auto object = value.toObject();
                Device device{object.value("device").toString(),
                              unb64(object.value("box")),
                              unb64(object.value("sign"))};
                if (device.box.size() == 32 && device.sign.size() == 32 &&
                    !device.id.isEmpty())
                {
                    directory.devices.push_back(device);
                }
            }
            this->directory_.insert(userId, directory);
            if (userId != this->userId_)
            {
                this->checkPinned(userId, directory);
            }
            done(&this->directory_[userId]);
        })
        .onError([this, me, done](const NetworkResult &) {
            if (me == this->userId_)
            {
                done(nullptr);
            }
        })
        .execute();
}

void SolrinoMessages::checkPinned(const QString &userId,
                                  const Directory &directory)
{
    QStringList keys;
    for (const auto &d : directory.devices)
    {
        keys.append(b64(d.sign));
    }
    keys.sort();
    auto &c = this->conversations_[userId];
    c.userId = userId;
    if (c.login.isEmpty())
    {
        c.login = directory.login;
    }
    if (c.pinned.isEmpty())
    {
        // First time: remember them
        c.pinned = keys;
        return;
    }
    // A device they removed is fine. A key we've never seen means a new
    // device, or someone in the middle, so it's worth checking.
    for (const auto &k : keys)
    {
        if (!c.pinned.contains(k))
        {
            c.keysChanged = true;
            return;
        }
    }
    c.pinned = keys;
}

void SolrinoMessages::trustNewKeys(const QString &userId)
{
    auto &c = this->conversations_[userId];
    QStringList keys;
    if (this->directory_.contains(userId))
    {
        for (const auto &d : this->directory_[userId].devices)
        {
            keys.append(b64(d.sign));
        }
    }
    keys.sort();
    c.pinned = keys;
    c.keysChanged = false;
    this->saveHistory();
    Q_EMIT this->changed();
}

QByteArray SolrinoMessages::sealFor(const Device &device,
                                    const QByteArray &inner,
                                    QJsonObject &envelope) const
{
    auto eph = crypto::newBoxKeys();
    if (!eph)
    {
        return {};
    }
    auto secret = crypto::agree(eph->privateKey, device.box);
    crypto::wipe(eph->privateKey);
    if (!secret)
    {
        return {};
    }
    const auto salt = eph->publicKey + device.box;
    auto key = crypto::deriveKey(*secret, salt, INFO + device.id.toLatin1());
    crypto::wipe(*secret);
    if (!key)
    {
        return {};
    }
    const auto nonce = crypto::randomBytes(12);
    auto sealed = crypto::seal(*key, nonce, salt, inner);
    crypto::wipe(*key);
    if (!sealed)
    {
        return {};
    }
    envelope = QJsonObject{
        {"device", device.id},
        {"eph", b64(eph->publicKey)},
        {"nonce", b64(nonce)},
        {"ct", b64(*sealed)},
    };
    return *sealed;
}

void SolrinoMessages::sendSealed(const QString &to,
                                 const std::vector<Device> &devices,
                                 const QByteArray &inner,
                                 std::function<void(bool, QString)> done)
{
    QJsonArray envelopes;
    for (const auto &device : devices)
    {
        QJsonObject envelope;
        if (this->sealFor(device, inner, envelope).isEmpty())
        {
            done(false, QStringLiteral("Couldn't encrypt the message."));
            return;
        }
        envelopes.append(envelope);
    }
    if (envelopes.isEmpty())
    {
        done(true, {});
        return;
    }
    NetworkRequest(server() + QStringLiteral("/v2/dm/send"),
                   NetworkRequestType::Post)
        .header("Authorization", this->authHeader())
        .json(QJsonObject{{"to", to}, {"envelopes", envelopes}})
        .timeout(15000)
        .onSuccess([done](const NetworkResult &) {
            done(true, {});
        })
        .onError([done](const NetworkResult &result) {
            done(false, result.parseJson()
                            .value("error")
                            .toObject()
                            .value("message")
                            .toString(QStringLiteral("Couldn't send it.")));
        })
        .execute();
}

void SolrinoMessages::send(const QString &userId, const QString &login,
                           const QString &rawText)
{
    const auto text = rawText.trimmed().left(MAX_TEXT);
    if (!this->ready() || text.isEmpty() || userId == this->userId_)
    {
        return;
    }
    auto &c = this->conversations_[userId];
    c.userId = userId;
    if (!login.isEmpty())
    {
        c.login = login;
    }
    if (c.keysChanged || c.blocked)
    {
        return;
    }
    // Sending to someone accepts them
    c.accepted = true;

    Message message;
    message.id = QString::fromLatin1(crypto::randomBytes(12).toHex());
    message.mine = true;
    message.text = text;
    message.at = QDateTime::currentDateTimeUtc();
    message.status = Status::Sending;
    c.messages.push_back(message);
    Q_EMIT this->changed();

    // Signed by this device, so they know it's really from us
    const auto payload =
        QJsonDocument(
            QJsonObject{
                {"v", 1},
                {"id", message.id},
                {"from", QJsonObject{{"u", this->userId_},
                                     {"l", this->login_},
                                     {"d", this->keys_->device}}},
                {"to", userId},
                {"at", static_cast<double>(message.at.toMSecsSinceEpoch())},
                {"text", text},
            })
            .toJson(QJsonDocument::Compact);
    auto signature = crypto::sign(this->keys_->signPrivate, payload);
    if (!signature)
    {
        this->setStatus(userId, message.id, Status::Failed);
        return;
    }
    const auto inner = QJsonDocument(QJsonObject{{"p", b64(payload)},
                                                 {"s", b64(*signature)}})
                           .toJson(QJsonDocument::Compact);

    const auto me = this->userId_;
    const auto messageId = message.id;
    this->lookUpKeys(userId, true, [this, me, userId, messageId,
                                    inner](const Directory *directory) {
        if (me != this->userId_)
        {
            return;
        }
        auto *c = &this->conversations_[userId];
        if (directory == nullptr || directory->devices.empty())
        {
            this->setStatus(userId, messageId, Status::Failed);
            return;
        }
        if (c->keysChanged)
        {
            // Their keys changed while sending: checked before it goes out
            this->setStatus(userId, messageId, Status::Failed);
            return;
        }
        this->sendSealed(
            userId, directory->devices, inner,
            [this, me, userId, messageId, inner](bool ok, const QString &) {
                if (me != this->userId_)
                {
                    return;
                }
                this->setStatus(userId, messageId,
                                ok ? Status::Sent : Status::Failed);
                if (!ok)
                {
                    return;
                }
                // A copy for our own other devices, so they see it too
                this->lookUpKeys(
                    me, false, [this, me, inner](const Directory *mine) {
                        if (mine == nullptr || me != this->userId_)
                        {
                            return;
                        }
                        std::vector<Device> others;
                        for (const auto &d : mine->devices)
                        {
                            if (d.id != this->keys_->device)
                            {
                                others.push_back(d);
                            }
                        }
                        this->sendSealed(me, others, inner,
                                         [](bool, const QString &) {});
                    });
            });
    });
}

void SolrinoMessages::setStatus(const QString &userId,
                                const QString &messageId, Status status)
{
    auto found = this->conversations_.find(userId);
    if (found == this->conversations_.end())
    {
        return;
    }
    for (auto &m : found->messages)
    {
        if (m.id == messageId)
        {
            m.status = status;
        }
    }
    this->saveHistory();
    Q_EMIT this->changed();
}

void SolrinoMessages::findUser(
    const QString &rawLogin,
    std::function<void(QString, QString, QString)> done)
{
    const auto login = rawLogin.trimmed().remove(u'@').toLower();
    if (!QRegularExpression(QStringLiteral("^[a-z0-9_]{1,25}$"))
             .match(login)
             .hasMatch())
    {
        done({}, {}, QStringLiteral("That isn't a Twitch name."));
        return;
    }
    QPointer<SolrinoMessages> self(this);
    getHelix()->getUserByName(
        login,
        [self, done](const HelixUser &user) {
            if (self == nullptr)
            {
                return;
            }
            if (user.id == self->userId_)
            {
                done({}, {}, QStringLiteral("That's you."));
                return;
            }
            // Only people who use Solrino can get messages
            self->lookUpKeys(user.id, true,
                             [done, user](const Directory *directory) {
                                 if (directory == nullptr ||
                                     directory->devices.empty())
                                 {
                                     done({}, {},
                                          QStringLiteral(
                                              "%1 isn't on Solrino messages "
                                              "yet.")
                                              .arg(user.displayName));
                                     return;
                                 }
                                 done(user.id, user.login, {});
                             });
        },
        [done, login] {
            done({}, {},
                 QStringLiteral("Couldn't find %1 on Twitch.").arg(login));
        });
}

void SolrinoMessages::accept(const QString &userId)
{
    auto &c = this->conversations_[userId];
    c.accepted = true;
    this->saveHistory();
    Q_EMIT this->changed();
}

void SolrinoMessages::setBlocked(const QString &userId, bool blocked)
{
    auto &c = this->conversations_[userId];
    c.userId = userId;
    c.blocked = blocked;
    if (blocked)
    {
        c.accepted = false;
        c.unread = 0;
    }
    this->saveHistory();
    Q_EMIT this->changed();
}

void SolrinoMessages::remove(const QString &userId)
{
    auto found = this->conversations_.find(userId);
    if (found == this->conversations_.end())
    {
        return;
    }
    // Blocked people stay blocked, without the messages
    if (found->blocked)
    {
        found->messages.clear();
    }
    else
    {
        this->conversations_.erase(found);
    }
    this->saveHistory();
    Q_EMIT this->changed();
}

void SolrinoMessages::markRead(const QString &userId)
{
    auto found = this->conversations_.find(userId);
    if (found == this->conversations_.end() || found->unread == 0)
    {
        return;
    }
    found->unread = 0;
    this->saveHistory();
    Q_EMIT this->changed();
}

void SolrinoMessages::securityCode(const QString &userId,
                                   std::function<void(QString)> done)
{
    const auto me = this->userId_;
    this->lookUpKeys(me, true, [this, me, userId,
                                done](const Directory *mine) {
        if (mine == nullptr || me != this->userId_)
        {
            done({});
            return;
        }
        QStringList ours;
        for (const auto &d : mine->devices)
        {
            ours.append(b64(d.sign));
        }
        ours.sort();
        this->lookUpKeys(userId, true, [me, userId, ours,
                                        done](const Directory *theirs) {
            if (theirs == nullptr)
            {
                done({});
                return;
            }
            QStringList keys;
            for (const auto &d : theirs->devices)
            {
                keys.append(b64(d.sign));
            }
            keys.sort();
            // The same for both people: the lower user ID's keys go first
            const auto a = me.toLongLong() < userId.toLongLong() ? ours : keys;
            const auto b = me.toLongLong() < userId.toLongLong() ? keys : ours;
            const auto hash = crypto::sha256(
                (QStringLiteral("solrino-code-v1|") + a.join(',') + u'|' +
                 b.join(','))
                    .toUtf8());
            // 30 digits in six groups, easy to read out loud
            QString code;
            for (int i = 0; i < 6; i++)
            {
                quint32 chunk = (static_cast<quint8>(hash[i * 4]) << 24) |
                                (static_cast<quint8>(hash[i * 4 + 1]) << 16) |
                                (static_cast<quint8>(hash[i * 4 + 2]) << 8) |
                                static_cast<quint8>(hash[i * 4 + 3]);
                if (!code.isEmpty())
                {
                    code += u' ';
                }
                code += QStringLiteral("%1").arg(chunk % 100000, 5, 10,
                                                 QLatin1Char('0'));
            }
            done(code);
        });
    });
}

}  // namespace chatterino

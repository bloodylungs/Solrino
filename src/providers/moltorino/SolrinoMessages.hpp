#pragma once

#include "common/websockets/WebSocketPool.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace chatterino {

/// Solrino messages: private messages between Solrino users, encrypted end to
/// end. Only the devices of the two people talking can read them.
///
/// How it works:
///  - Each Solrino install makes its own keys the first time: one for opening
///    messages (X25519) and one for signing them (Ed25519). The private halves
///    never leave this computer, and are saved locked to this Windows user.
///  - The public halves go to Solrino's server, under your Twitch account.
///  - Sending: the message, who wrote it and when are signed, then sealed
///    separately for each of the other person's devices with a key nobody
///    else can work out. The server only gets sealed envelopes and who they're
///    for. It never learns who sent them or what's inside, and deletes them
///    once they're received (or after 14 days).
///  - Receiving: each envelope is opened, and the signature is checked
///    against the sender's published key, so nobody can pretend to be them.
///  - Security codes: both people can compare a code made from their keys. If
///    it matches, nobody (not even Solrino's server) swapped keys in between.
///    If someone's keys change later, Solrino warns before sending again.
///  - First messages from someone new go to Requests until you accept.
///  - History is kept on this computer only, locked to this Windows user.
class SolrinoMessages : public QObject
{
    Q_OBJECT

public:
    enum class Status { Sending, Sent, Failed };

    /// A photo, video or any file in a message. The file itself is sealed
    /// with its own key, which only travels inside the encrypted message.
    struct File {
        QString id;
        QByteArray key;
        QByteArray nonce;
        QString name;
        qint64 size = 0;
        QString mime;
        int pieces = 0;
    };

    struct Message {
        QString id;
        bool mine = false;
        QString text;
        QDateTime at;
        Status status = Status::Sent;
        std::optional<File> file;
    };

    struct Conversation {
        QString userId;
        QString login;
        bool accepted = false;
        bool blocked = false;
        // Their keys changed since they were last checked
        bool keysChanged = false;
        // The signing keys we last saw for them
        QStringList pinned;
        int unread = 0;
        // They've seen everything I sent up to this time
        QDateTime theirSeenAt;
        std::vector<Message> messages;
    };

    static SolrinoMessages &instance();

    /// Starts after the app has loaded, and follows account switches
    void start();

    /// Whether messages work right now (signed in, keys made and published)
    bool ready() const;
    QString problem() const;
    QString myUserId() const;
    QString myLogin() const;

    const QMap<QString, Conversation> &conversations() const;
    const Conversation *conversation(const QString &userId) const;
    int unreadCount() const;
    int requestCount() const;

    /// Looks up a Twitch name, then calls back with their user ID, or an error
    void findUser(const QString &login,
                  std::function<void(QString userId, QString login,
                                     QString error)>
                      done);

    void send(const QString &userId, const QString &login,
              const QString &text);
    /// Seals a file from this PC and sends it. Up to 25 MB.
    void sendFile(const QString &userId, const QString &login,
                  const QString &path, std::function<void(QString error)> done);
    /// Gets a file from a message, opened and ready to show or save
    void loadFile(const File &file,
                  std::function<void(QByteArray data, QString error)> done);
    void accept(const QString &userId);
    void setBlocked(const QString &userId, bool blocked);
    void remove(const QString &userId);
    void markRead(const QString &userId);
    /// The person checked the new security code, so sending is allowed again
    void trustNewKeys(const QString &userId);

    /// The code both people compare. Calls back with an empty string if their
    /// keys can't be fetched.
    void securityCode(const QString &userId,
                      std::function<void(QString code)> done);

    /// Called by the socket listener
    void socketOpened(int generation);
    void socketMessage(int generation, const QByteArray &data);
    void socketClosed(int generation);

Q_SIGNALS:
    void changed();
    void arrived(const QString &userId, const QString &text);

private:
    struct Device {
        QString id;
        QByteArray box;
        QByteArray sign;
    };
    struct Directory {
        QString login;
        std::vector<Device> devices;
        QDateTime fetched;
    };
    struct Keys {
        QString device;
        QByteArray boxPrivate;
        QByteArray boxPublic;
        QByteArray signPrivate;
        QByteArray signPublic;
    };

    SolrinoMessages();

    void switchAccount();
    void stopAll();
    QString folder() const;
    bool loadKeys();
    bool loadHistory();
    void saveHistory();
    void publishKeys();
    void connectSocket();
    void fetchInbox();
    void processNext();
    void acknowledge(const QStringList &ids);
    void lookUpKeys(const QString &userId, bool fresh,
                    std::function<void(const Directory *)> done);
    void checkPinned(const QString &userId, const Directory &directory);
    void sendSealed(const QString &to, const std::vector<Device> &devices,
                    const QByteArray &inner,
                    std::function<void(bool ok, QString error)> done);
    QByteArray sealFor(const Device &device, const QByteArray &inner,
                       QJsonObject &envelope) const;
    std::optional<QByteArray> openEnvelope(const QJsonObject &envelope) const;
    void handleOpened(const QString &envelopeId, const QByteArray &inner,
                      std::function<void()> next);
    void setStatus(const QString &userId, const QString &messageId,
                   Status status);
    /// Signs a payload and sends it sealed to their devices and our others
    void sendPayload(const QString &userId, const QJsonObject &fields,
                     std::function<void(bool ok)> done);
    void sendSeen(const QString &userId);
    QString filePath(const QString &id) const;
    void addMine(const QString &userId, const QString &login, Message message);
    QByteArray authHeader() const;

    QString userId_;
    QString login_;
    QString token_;
    QString problem_;
    std::unique_ptr<Keys> keys_;
    bool published_ = false;
    QMap<QString, Conversation> conversations_;
    QMap<QString, Directory> directory_;

    std::unique_ptr<WebSocketPool> pool_;
    WebSocketHandle socket_;
    int socketGeneration_ = 0;
    bool socketOpen_ = false;
    QTimer reconnect_;
    QTimer poll_;

    bool fetching_ = false;
    bool fetchAgain_ = false;
    std::deque<QJsonObject> queue_;
    QStringList done_;
};

}  // namespace chatterino

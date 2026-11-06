#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

class QLocalSocket;
class QJsonObject;

namespace chatterino {

/// Shows Solrino as your activity on Discord, by talking to the Discord app
/// running on this PC. Each person can turn it off, or hide which chat they're
/// in, in Settings. It retries quietly while Discord is closed.
class SolrinoDiscordPresence : public QObject
{
public:
    static SolrinoDiscordPresence &instance();

    void start();

private:
    SolrinoDiscordPresence();

    void tick();
    void connectToDiscord();
    void disconnectFromDiscord();
    void onReadable();
    void sendFrame(int opcode, const QJsonObject &body);
    void sendActivity();
    void readCurrentActivity(QString &details, QString &state) const;

    QLocalSocket *socket_{};
    QByteArray received_;
    QTimer timer_;
    QElapsedTimer sinceLastSend_;
    QElapsedTimer sinceLastAttempt_;
    qint64 startedAt_ = 0;
    int pipeNumber_ = 0;
    bool ready_ = false;
    bool sentOnce_ = false;
    QString lastDetails_;
    QString lastState_;
};

}  // namespace chatterino

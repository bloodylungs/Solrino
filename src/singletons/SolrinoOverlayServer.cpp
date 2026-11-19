#include "singletons/SolrinoOverlayServer.hpp"

#include "singletons/Settings.hpp"
#include "singletons/SolrinoNowPlaying.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace chatterino {

namespace {

/// Only Solrino's website may read the song. Anything else gets no CORS
/// headers, so a browser won't let other pages see the answer.
const QByteArray ALLOWED_ORIGIN =
    "https://solrino.solrino-badge-server.workers.dev";

/// How long after the last request the browser source still counts as
/// showing. The overlay page asks every second.
constexpr qint64 SHOWING_FOR_MS = 4000;

QByteArray statusText(int code)
{
    switch (code)
    {
        case 200:
            return "OK";
        case 204:
            return "No Content";
        case 404:
            return "Not Found";
        default:
            return "Bad Request";
    }
}

}  // namespace

struct SolrinoOverlayServer::Impl {
    QTcpServer server;
    QTimer watch;
    qint64 lastSongRequest = 0;
    bool showing = false;
    bool turnedOnReader = false;
};

SolrinoOverlayServer &SolrinoOverlayServer::instance()
{
    static auto *server = new SolrinoOverlayServer();
    return *server;
}

bool SolrinoOverlayServer::browserShowing() const
{
    return this->impl_ != nullptr && this->impl_->showing;
}

void SolrinoOverlayServer::start()
{
    if (this->impl_ != nullptr)
    {
        return;
    }
    this->impl_ = new Impl();
    auto *impl = this->impl_;

    // Only this PC can connect
    if (!impl->server.listen(QHostAddress::LocalHost, PORT))
    {
        return;
    }

    const auto updateShowing = [this, impl] {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        const bool recent = now - impl->lastSongRequest < SHOWING_FOR_MS;
        const bool showing =
            recent && SolrinoNowPlaying::instance().track().present;
        if (showing != impl->showing)
        {
            impl->showing = showing;
            this->browserChanged.invoke();
        }
        // The song reader runs for the browser source even with Solrino's
        // own overlay off, and stops again once the browser source is gone
        if (!recent && impl->turnedOnReader &&
            !getSettings()->solrinoNowPlaying.getValue())
        {
            impl->turnedOnReader = false;
            SolrinoNowPlaying::instance().setActive(false);
        }
    };
    impl->watch.setInterval(1000);
    QObject::connect(&impl->watch, &QTimer::timeout, &impl->server,
                     updateShowing);
    impl->watch.start();

    QObject::connect(
        &impl->server, &QTcpServer::newConnection, &impl->server,
        [this, impl, updateShowing] {
            while (auto *socket = impl->server.nextPendingConnection())
            {
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QObject::deleteLater);
                QObject::connect(
                    socket, &QTcpSocket::readyRead, socket,
                    [this, impl, socket, updateShowing] {
                        const auto request = socket->readAll();
                        if (request.size() > 16 * 1024)
                        {
                            socket->abort();
                            return;
                        }
                        const auto lines = request.split('\n');
                        const auto first = lines.value(0).trimmed().split(' ');
                        const auto method = first.value(0);
                        auto path = first.value(1);
                        if (const auto q = path.indexOf('?'); q >= 0)
                        {
                            path = path.left(q);
                        }
                        QByteArray origin;
                        for (const auto &line : lines)
                        {
                            if (line.toLower().startsWith("origin:"))
                            {
                                origin = line.mid(7).trimmed();
                            }
                        }
                        const bool allowed = origin == ALLOWED_ORIGIN;

                        const auto reply = [socket, allowed](
                                               int code,
                                               const QByteArray &type,
                                               const QByteArray &body) {
                            QByteArray head = "HTTP/1.1 " +
                                              QByteArray::number(code) + ' ' +
                                              statusText(code) + "\r\n";
                            head += "Content-Type: " + type + "\r\n";
                            head += "Content-Length: " +
                                    QByteArray::number(body.size()) + "\r\n";
                            head += "Cache-Control: no-store\r\n";
                            if (allowed)
                            {
                                head += "Access-Control-Allow-Origin: " +
                                        ALLOWED_ORIGIN + "\r\n";
                                head += "Access-Control-Allow-Private-Network: "
                                        "true\r\n";
                                head += "Access-Control-Allow-Methods: GET\r\n";
                                head += "Vary: Origin\r\n";
                            }
                            head += "Connection: close\r\n\r\n";
                            socket->write(head + body);
                            socket->disconnectFromHost();
                        };

                        if (method == "OPTIONS")
                        {
                            reply(204, "text/plain", {});
                            return;
                        }
                        if (method != "GET" || !allowed)
                        {
                            reply(404, "text/plain", "Not found");
                            return;
                        }

                        auto &nowPlaying = SolrinoNowPlaying::instance();
                        if (path == "/now-playing")
                        {
                            impl->lastSongRequest =
                                QDateTime::currentMSecsSinceEpoch();
                            if (!getSettings()->solrinoNowPlaying.getValue() &&
                                !impl->turnedOnReader)
                            {
                                impl->turnedOnReader = true;
                                nowPlaying.setActive(true);
                            }
                            const auto &track = nowPlaying.track();
                            qint64 position = track.position;
                            if (track.playing && track.readAt > 0)
                            {
                                position += QDateTime::currentMSecsSinceEpoch() -
                                            track.readAt;
                            }
                            if (track.length > 0)
                            {
                                position = std::clamp<qint64>(position, 0,
                                                              track.length);
                            }
                            const QJsonObject song{
                                {"present", track.present},
                                {"playing", track.playing},
                                {"title", track.title},
                                {"artist", track.artist},
                                {"album", track.album},
                                {"position", double(position)},
                                {"length", double(track.length)},
                                {"cover", track.cover.isNull()
                                              ? QString()
                                              : QString::number(
                                                    track.cover.cacheKey())},
                            };
                            reply(200, "application/json",
                                  QJsonDocument(song).toJson(
                                      QJsonDocument::Compact));
                            updateShowing();
                            return;
                        }
                        if (path == "/cover.png")
                        {
                            const auto &cover = nowPlaying.track().cover;
                            if (cover.isNull())
                            {
                                reply(404, "text/plain", "No cover");
                                return;
                            }
                            QByteArray png;
                            QBuffer buffer(&png);
                            buffer.open(QIODevice::WriteOnly);
                            cover
                                .scaled(256, 256, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation)
                                .save(&buffer, "PNG");
                            reply(200, "image/png", png);
                            return;
                        }
                        reply(404, "text/plain", "Not found");
                    });
            }
        });
}

}  // namespace chatterino

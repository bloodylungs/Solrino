#pragma once

#include "providers/seventv/paints/Paint.hpp"

#include <pajlada/signals/signal.hpp>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <deque>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

namespace chatterino {

namespace seventv::eventapi {
struct TwitchUser;
struct KickUser;
using User = std::variant<TwitchUser, KickUser>;
}

enum class SeventvPaintLoadStatus : std::uint8_t {
    Unknown,
    Loading,
    Ready,
    NotFound,
    Failed,
};

namespace seventv::detail {

bool shouldStartPaintRequest(SeventvPaintLoadStatus status, bool retry,
                             bool queued, bool stalled);

}

class SeventvPaints
{
public:
    SeventvPaints();

    pajlada::Signals::Signal<const QString &, bool> paintChanged;

    pajlada::Signals::Signal<const QString &, SeventvPaintLoadStatus>
        paintLoadStatusChanged;

    void addPaint(const QJsonObject &paintJson, bool updateExisting = true);
    void assignPaintToUsers(const QString &paintID,
                            std::span<const seventv::eventapi::User> users);
    void clearPaintFromUsers(const QString &paintID,
                             std::span<const seventv::eventapi::User> users);

    std::shared_ptr<Paint> getPaint(const QString &userName, bool kick) const;
    /// Their 7TV paint only, even when they also wear a Solrino paint
    std::shared_ptr<Paint> getSeventvOnlyPaint(const QString &userName,
                                               bool kick) const;
    std::shared_ptr<Paint> getPaintByID(const QString &paintID) const;
    SeventvPaintLoadStatus getPaintLoadStatus(const QString &paintID) const;

    void loadPaintByID(const QString &paintID, bool retry = false);

    void loadPaintsByID(const QStringList &paintIDs, bool retry = false);

    /// "Reload 7TV paints": tries every paint that didn't load again, asks
    /// 7TV which paint each of these Twitch users wears now, then redraws.
    /// `users` are (Twitch user ID, login) pairs.
    void reload(const std::vector<std::pair<QString, QString>> &users);

    static QString normalizePaintID(QString paintID);
    static bool isValidPaintID(const QString &paintID);

private:
    struct PaintBatchItem {
        QString paintID;
        std::uint64_t generation = 0;
    };

    struct PaintBatchRequest {
        std::vector<PaintBatchItem> paints;
        bool retry = false;
        std::uint64_t requestID = 0;
    };

    struct PaintLoadState {
        SeventvPaintLoadStatus status = SeventvPaintLoadStatus::Unknown;
        std::uint64_t generation = 0;
        std::uint64_t requestID = 0;
        std::chrono::steady_clock::time_point startedAt{};
        bool retry = false;
        QJsonObject definition;
    };

    void loadPaintsByIDInternal(const QStringList &paintIDs, bool retry,
                                bool prioritize);
    void enqueuePaintBatch(std::vector<PaintBatchItem> paints, bool retry,
                           bool prioritize = false);
    void pumpPaintRequests();
    void requestPaintBatch(PaintBatchRequest request);
    void finishPaintBatchRequest(std::uint64_t requestID,
                                 bool paintsChanged = false);
    void handlePaintRequestWatchdog(std::uint64_t requestID);
    void schedulePaintRetry(const QStringList &paintIDs,
                            bool assignedOnly = false);
    bool finishPaintLoad(const PaintBatchItem &paint,
                         std::uint64_t requestID,
                         SeventvPaintLoadStatus status);
    void notifyPaintChanged(std::vector<std::pair<QString, bool>> users,
                            bool repaint = false);
    /// Sets (or with an empty ID, clears) one Twitch user's 7TV paint
    void setTwitchUserPaint(const QString &login, const QString &paintID);

    mutable std::shared_mutex mutex_;

    std::unordered_map<QString, QString> kickPaintMap_;

    std::unordered_map<QString, QString> twitchPaintMap_;

    std::unordered_map<QString, std::shared_ptr<Paint>> knownPaints_;

    std::unordered_map<QString, PaintLoadState> paintLoadStates_;

    std::deque<PaintBatchRequest> pendingPaintBatches_;
    std::unordered_map<std::uint64_t, PaintBatchRequest>
        activePaintRequests_;
    std::uint64_t nextPaintGeneration_ = 0;
    std::uint64_t nextPaintRequestID_ = 0;

    QObject lifetimeGuard_;
};

}

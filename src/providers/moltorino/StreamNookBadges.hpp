#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <memory>
#include <shared_mutex>

namespace chatterino {

struct Emote;

/// StreamNook's badges, from its public list. Only read: nothing is sent to
/// StreamNook but the request itself.
class StreamNookBadges final : public QObject
{
public:
    static StreamNookBadges &instance();

    void start();

    /// The badge this person shows on StreamNook, or nullptr.
    std::shared_ptr<const Emote> getBadge(const QString &userId) const;

private:
    void request();
    bool apply(const QByteArray &payload);

    mutable std::shared_mutex mutex_;
    QHash<QString, std::shared_ptr<const Emote>> users_;
};

}  // namespace chatterino

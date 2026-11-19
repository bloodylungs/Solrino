#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <memory>
#include <shared_mutex>

namespace chatterino {

struct Emote;

/// Reads Moltorino's own badge list, only to show people's Moltorino badges with
/// Moltorino's artwork next to any Solrino badge they have. Nothing is ever sent
/// to Moltorino, and these badges can't be given or changed from Solrino.
class MoltorinoOriginalBadges final : public QObject
{
public:
    static MoltorinoOriginalBadges &instance();

    void start();

    /// The badge this person shows on Moltorino, or nullptr.
    std::shared_ptr<const Emote> getBadge(const QString &userId) const;

private:
    void request();
    bool apply(const QByteArray &payload);

    mutable std::shared_mutex mutex_;
    QHash<QString, std::shared_ptr<const Emote>> users_;
};

}  // namespace chatterino

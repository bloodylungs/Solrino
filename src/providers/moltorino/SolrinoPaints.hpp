#pragma once

#include "providers/seventv/paints/Paint.hpp"

#include <QJsonObject>
#include <QString>

#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace chatterino {

/// A username paint made by Solrino. Colors come from a function of where
/// along the name (0 to 1) and, for animated paints, how many seconds have
/// passed, so animated paints move on their own as chats repaint.
class SolrinoPaint final : public Paint
{
public:
    using ColorAt = std::function<QColor(qreal position, qreal seconds)>;

    struct Style {
        QString id;
        QString name;
        QString description;
        bool animated = false;
        /// Degrees, 0 runs left to right, 90 runs top to bottom
        qreal angle = 0;
        /// For animated paints, how many names the pattern spans before it
        /// repeats. 1 fits the pattern to the name.
        qreal repeatLength = 1;
        ColorAt color;
        /// Sharp-edged stops for striped paints; used instead of `color`
        QGradientStops stripes;
        /// Stripe width relative to the text height
        qreal stripeWidth = 0;
        std::vector<PaintDropShadow> shadows;
        /// For animated striped paints, how many stripes slide past a second
        qreal stripeSpeed = 0;
    };

    explicit SolrinoPaint(Style style);

    QBrush asBrush(QColor userColor, QRectF drawingRect) const override;
    const std::vector<PaintDropShadow> &getDropShadows() const override;
    bool animated() const override;

    const Style &style() const;

protected:
    qint64 sourcePixmapCacheKey() const override;

private:
    Style style_;
};

class SolrinoPaints
{
public:
    static constexpr auto ID_PREFIX = "solrino:";

    static SolrinoPaints &instance();

    /// Every paint Solrino offers, in the order shown in Vanity
    const std::vector<std::shared_ptr<SolrinoPaint>> &catalog() const;

    /// Accepts a bare paint id or one starting with "solrino:"
    std::shared_ptr<SolrinoPaint> byId(QStringView id) const;

    /// The paint someone is wearing, by Twitch login
    std::shared_ptr<SolrinoPaint> forLogin(const QString &login) const;

    /// The paint this person chose on this device, or empty for none
    QString ownPaint(const QString &login) const;

    /// Wears a paint (or none, when `paintId` is empty) on this device right
    /// away, and saves it to the Solrino server so other people see it.
    /// `done` gets whether the server confirmed it.
    void setOwnPaint(const QString &login, const QString &userId,
                     const QString &oauthToken, const QString &paintId,
                     QObject *caller, std::function<void(bool)> done);

    /// Reads everyone's paints from the Solrino badge list
    void applyServerUsers(const QJsonObject &users);

private:
    SolrinoPaints();
    void loadOwnPaints();
    void saveOwnPaints() const;

    std::vector<std::shared_ptr<SolrinoPaint>> catalog_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<QString, QString> serverPaints_;
    std::unordered_map<QString, QString> ownPaints_;
};

}  // namespace chatterino

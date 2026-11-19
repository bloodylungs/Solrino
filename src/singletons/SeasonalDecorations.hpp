#pragma once

#include <QRect>
#include <QString>

class QPainter;

namespace chatterino {

/// Paints the Seasonal theme's decorations (Halloween bats, pumpkins and
/// cobwebs, or Winter icicles, snow and pine trees) over a chat's background.
/// The art hugs the edges of `rect` so the middle stays clear for messages.
/// `season` is a value from currentSeasonThemeId(); anything else paints
/// nothing.
void paintSeasonalDecorations(QPainter &painter, const QRect &rect,
                              const QString &season, qreal scale);

}  // namespace chatterino

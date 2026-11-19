#pragma once

#include "singletons/SolrinoNowPlaying.hpp"

#include <QRectF>
#include <QSizeF>

#include <array>

class QPainter;

namespace chatterino {

/// Shows or hides the now playing overlay, following its settings. Call once at
/// startup; it keeps itself up to date after that.
void startSolrinoNowPlayingOverlay();

/// Lets the overlay be dragged until the mouse is let go. It can't be dragged
/// otherwise, so it never gets in the way of clicks.
void moveSolrinoNowPlayingOverlay();
/// Puts the overlay back in its first spot, the top left of the main screen
void resetSolrinoNowPlayingOverlayPlace();

/// Whether the card last drawn is moving (a sliding name, a spinning cover,
/// bouncing bars), so it needs redrawing often
bool solrinoNowPlayingCardAnimating();

/// The card's size with the current settings
QSizeF solrinoNowPlayingCardSize();

/// Draws the card into `card`. `level` is how loud the music is, 0 to 1, or
/// below 0 if unknown. Where its buttons are goes into `buttons` (previous,
/// play/pause, next, shuffle, repeat), empty for any that aren't shown.
void paintSolrinoNowPlayingCard(QPainter &painter, const QRectF &card,
                                const SolrinoNowPlaying::Track &track,
                                qreal devicePixelRatio, float level,
                                std::array<QRectF, 5> *buttons = nullptr);

}  // namespace chatterino

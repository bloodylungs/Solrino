#pragma once

class QString;

namespace chatterino {

/// Shows "@sender pinged you in #channel" at the top right of the chat you're
/// on and at the bottom right of your screen. A red line drains under it, and
/// it fades away once the line runs out. Each spot can be turned off in settings.
///
/// Clicks pass through both, so they can't interrupt a game or typing. Only the
/// small X in the corner takes clicks, and it dismisses the card.
///
/// How big they are is set by the "Ping popup size" setting.
///
/// The card shows pictureLogin's Twitch profile picture, or the Solrino logo
/// until it has loaded and if it can't be loaded.
void showSolrinoPing(const QString &sender, const QString &channel,
                     const QString &message, bool isWhisper,
                     const QString &pictureLogin);

}  // namespace chatterino

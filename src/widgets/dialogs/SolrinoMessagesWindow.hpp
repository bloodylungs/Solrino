#pragma once

#include <QPixmap>
#include <QString>

namespace chatterino {

/// Opens the Solrino messages window. With a Twitch name, it opens (or
/// starts) the chat with that person.
void openSolrinoMessages(const QString &login = {});

/// The speech-bubble icon for the title bar, with a red dot when there's
/// something unread
QPixmap solrinoMessagesIcon(int size, bool unread);

}  // namespace chatterino

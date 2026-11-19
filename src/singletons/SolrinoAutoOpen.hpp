#pragma once

class QWidget;

namespace chatterino {

/// Opens a tab for each chosen Twitch channel as it goes live, so a stream is
/// never missed. Call once at startup.
void startSolrinoAutoOpen();

/// The list of channels to open, to add to and remove from
void showSolrinoAutoOpenDialog(QWidget *parent);

}  // namespace chatterino

#pragma once

#include <functional>

class QWidget;

namespace chatterino {

/// The Solrino logo for the title bar, with a darker red shine that keeps
/// sweeping across it. It fills the space the window title used to use.
QWidget *makeSolrinoTitleLogo();

/// Plays the first-launch intro: the logo slides up from the bottom of the
/// screen to the center, blinks a few times and fades, then onFinished runs.
void playSolrinoIntro(std::function<void()> onFinished);

}  // namespace chatterino

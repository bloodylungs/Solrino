#pragma once

#include "widgets/settingspages/SettingsPage.hpp"

#include <pajlada/signals/signalholder.hpp>

namespace chatterino {

/// Everything about the now playing overlay, with a live preview of it.
class NowPlayingPage : public SettingsPage
{
public:
    NowPlayingPage();

private:
    pajlada::Signals::SignalHolder connections_;
};

}  // namespace chatterino

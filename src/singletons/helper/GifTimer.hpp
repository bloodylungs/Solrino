// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QTimer>

namespace chatterino {

// 30 frames a second. Solrino's badges and paints are made at 30, and most
// emotes look the same, for about 40% less redrawing than 50.
constexpr long unsigned GIF_FRAME_LENGTH = 33;

class GIFTimer
{
public:
    void initialize();

    bool shouldAnimate() const;
    long unsigned position()
    {
        return this->position_;
    }

    void registerOpenOverlayWindow()
    {
        this->openOverlayWindows_++;
    }

    void unregisterOpenOverlayWindow()
    {
        assert(this->openOverlayWindows_ >= 1);
        this->openOverlayWindows_--;
    }

private:
    QTimer timer;
    long unsigned position_{};
    size_t openOverlayWindows_ = 0;
};

}

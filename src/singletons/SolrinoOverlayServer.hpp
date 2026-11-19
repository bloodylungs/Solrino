#pragma once

#include <pajlada/signals/signal.hpp>

namespace chatterino {

/// A tiny web server on this PC only (127.0.0.1), for the OBS overlay on
/// Solrino's website. It hands that page the song that's playing, so a
/// streamer's browser source can show it. Only Solrino's own website may read
/// it, so other sites can't see what you listen to.
///
///   GET /now-playing   The song, as JSON
///   GET /cover.png     The song's album art
class SolrinoOverlayServer
{
public:
    static constexpr int PORT = 47391;

    static SolrinoOverlayServer &instance();

    void start();

    /// True while an OBS browser source is showing the song, so Solrino's own
    /// on-screen card can step aside instead of showing twice
    bool browserShowing() const;

    /// Fires when browserShowing() changes
    pajlada::Signals::NoArgSignal browserChanged;

private:
    SolrinoOverlayServer() = default;
    struct Impl;
    Impl *impl_{};
};

}  // namespace chatterino

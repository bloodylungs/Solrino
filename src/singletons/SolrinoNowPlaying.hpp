#pragma once

#include <pajlada/signals/signal.hpp>
#include <QImage>
#include <QString>

#include <memory>
#include <optional>

namespace chatterino {

/// What's playing in Spotify (or, if chosen, any player), read from Windows'
/// own media controls. No Spotify login is needed. Windows only.
class SolrinoNowPlaying
{
public:
    struct Track {
        bool present = false;
        bool playing = false;
        QString title;
        QString artist;
        QString album;
        QImage cover;
        /// Milliseconds into the song, and the song's length (0 if unknown)
        qint64 position = 0;
        qint64 length = 0;
        /// When position was read, so it can keep counting while playing
        qint64 readAt = 0;
        bool shuffle = false;
        /// 0 off, 1 the whole list, 2 this song
        int repeat = 0;
        /// Which app it's from, as Windows names it
        QString app;
    };

    enum class Command { Previous, PlayPause, Next, Shuffle, Repeat };

    static SolrinoNowPlaying &instance();

    /// Starts or stops watching, following the overlay setting
    void setActive(bool active);
    const Track &track() const;
    void send(Command command);
    /// A reading from the background reader
    void receive(Track track);
    /// The background reader's shared state
    struct Worker;

    /// How loud the player is right now, 0 to 1, or nothing if it can't be
    /// read
    std::optional<float> level();
    /// Turns the player's own volume (not the whole PC's) up or down by
    /// `steps` of 5%, and gives the new volume, 0 to 100
    std::optional<int> nudgeVolume(int steps);

    /// The song, its cover or whether it's playing changed
    pajlada::Signals::NoArgSignal changed;

private:
    SolrinoNowPlaying();
    std::shared_ptr<Worker> worker_;
    Track track_;
};

}  // namespace chatterino

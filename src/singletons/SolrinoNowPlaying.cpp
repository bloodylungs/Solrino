#include "singletons/SolrinoNowPlaying.hpp"

#include "singletons/Settings.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>

#ifdef Q_OS_WIN
// Windows' media controls, the same ones the volume flyout uses
#    include <winrt/base.h>
#    include <winrt/Windows.Foundation.h>
#    include <winrt/Windows.Foundation.Collections.h>
#    include <winrt/Windows.Media.Control.h>
#    include <winrt/Windows.Media.h>
#    include <winrt/Windows.Storage.Streams.h>
#endif

#ifdef Q_OS_WIN
// Each app's own volume and loudness, like the volume mixer
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <Windows.h>
#    include <audiopolicy.h>
#    include <endpointvolume.h>
#    include <mmdeviceapi.h>
#    include <wrl/client.h>
#endif

#include <QFileInfo>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace chatterino {

struct SolrinoNowPlaying::Worker {
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Command> command;
    std::atomic<bool> running{true};
    std::atomic<bool> anyPlayer{false};
};

namespace {

#ifdef Q_OS_WIN
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;

QString text(const winrt::hstring &value)
{
    return QString::fromWCharArray(value.c_str(), int(value.size()));
}

qint64 toMs(winrt::Windows::Foundation::TimeSpan span)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(span).count();
}

QImage readCover(const IRandomAccessStreamReference &reference)
{
    if (!reference)
    {
        return {};
    }
    const auto stream = reference.OpenReadAsync().get();
    const auto size = uint32_t(std::min<uint64_t>(stream.Size(), 8 * 1024 * 1024));
    if (size == 0)
    {
        return {};
    }
    Buffer buffer(size);
    const auto read =
        stream.ReadAsync(buffer, size, InputStreamOptions::None).get();
    return QImage::fromData(read.data(), int(read.Length()));
}

void run(std::shared_ptr<SolrinoNowPlaying::Worker> worker)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    QString lastKey;
    QImage lastCover;
    int coverRefreshes = 0;
    int coverRetries = 0;

    while (worker->running)
    {
        SolrinoNowPlaying::Track track;
        try
        {
            if (!manager)
            {
                manager = GlobalSystemMediaTransportControlsSessionManager::
                              RequestAsync()
                                  .get();
            }
            GlobalSystemMediaTransportControlsSession chosen{nullptr};
            for (const auto &session : manager.GetSessions())
            {
                if (text(session.SourceAppUserModelId())
                        .contains(QStringLiteral("spotify"), Qt::CaseInsensitive))
                {
                    chosen = session;
                    break;
                }
            }
            if (!chosen && worker->anyPlayer)
            {
                chosen = manager.GetCurrentSession();
            }

            std::optional<SolrinoNowPlaying::Command> command;
            {
                std::lock_guard lock(worker->mutex);
                command.swap(worker->command);
            }
            if (chosen && command)
            {
                switch (*command)
                {
                    case SolrinoNowPlaying::Command::Previous:
                        chosen.TrySkipPreviousAsync().get();
                        break;
                    case SolrinoNowPlaying::Command::PlayPause:
                        chosen.TryTogglePlayPauseAsync().get();
                        break;
                    case SolrinoNowPlaying::Command::Next:
                        chosen.TrySkipNextAsync().get();
                        break;
                    case SolrinoNowPlaying::Command::Shuffle: {
                        const auto now = chosen.GetPlaybackInfo().IsShuffleActive();
                        chosen.TryChangeShuffleActiveAsync(!(now && now.Value()))
                            .get();
                        break;
                    }
                    case SolrinoNowPlaying::Command::Repeat: {
                        // Off, then the whole list, then this song, like Spotify
                        using winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
                        const auto now = chosen.GetPlaybackInfo().AutoRepeatMode();
                        const auto mode =
                            now ? now.Value() : MediaPlaybackAutoRepeatMode::None;
                        chosen
                            .TryChangeAutoRepeatModeAsync(
                                mode == MediaPlaybackAutoRepeatMode::None
                                    ? MediaPlaybackAutoRepeatMode::List
                                : mode == MediaPlaybackAutoRepeatMode::List
                                    ? MediaPlaybackAutoRepeatMode::Track
                                    : MediaPlaybackAutoRepeatMode::None)
                            .get();
                        break;
                    }
                }
                // Give the player a moment to change song before reading it
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }

            if (chosen)
            {
                const auto properties = chosen.TryGetMediaPropertiesAsync().get();
                track.title = text(properties.Title());
                track.artist = text(properties.Artist());
                track.album = text(properties.AlbumTitle());
                track.present = !track.title.isEmpty();
                const auto info = chosen.GetPlaybackInfo();
                track.playing =
                    info.PlaybackStatus() ==
                    GlobalSystemMediaTransportControlsSessionPlaybackStatus::
                        Playing;
                if (const auto shuffle = info.IsShuffleActive())
                {
                    track.shuffle = shuffle.Value();
                }
                if (const auto repeat = info.AutoRepeatMode())
                {
                    using winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
                    track.repeat =
                        repeat.Value() == MediaPlaybackAutoRepeatMode::List  ? 1
                        : repeat.Value() == MediaPlaybackAutoRepeatMode::Track ? 2
                                                                              : 0;
                }
                track.app = text(chosen.SourceAppUserModelId());

                const auto timeline = chosen.GetTimelineProperties();
                track.length = toMs(timeline.EndTime() - timeline.StartTime());
                track.position = toMs(timeline.Position());
                if (track.playing)
                {
                    // The position is from when the player last reported it
                    track.position += std::max<qint64>(
                        0, toMs(winrt::clock::now() - timeline.LastUpdatedTime()));
                }
                if (track.length > 0)
                {
                    track.position = std::clamp<qint64>(track.position, 0,
                                                        track.length);
                }

                // The cover only changes with the song
                const auto key = track.title + u'\n' + track.artist + u'\n' +
                                 track.album;
                if (key != lastKey)
                {
                    lastKey = key;
                    lastCover = {};
                    // Spotify often names the new song before its cover is
                    // ready, or still hands out the old cover for a moment,
                    // so keep reading it for a few seconds after a change
                    coverRefreshes = 8;
                    coverRetries = 40;
                }
                if (coverRefreshes > 0 || (lastCover.isNull() && coverRetries > 0))
                {
                    if (coverRefreshes > 0)
                    {
                        --coverRefreshes;
                    }
                    else
                    {
                        --coverRetries;
                    }
                    try
                    {
                        auto cover = readCover(properties.Thumbnail());
                        // Only a different picture counts, so re-reading
                        // the same cover doesn't look like a song change
                        if (!cover.isNull() && cover != lastCover)
                        {
                            lastCover = std::move(cover);
                        }
                    }
                    catch (...)
                    {
                    }
                }
                track.cover = lastCover;
            }
        }
        catch (...)
        {
            // The player went away mid-read; try again next time
            manager = nullptr;
        }

        track.readAt = QDateTime::currentMSecsSinceEpoch();
        QMetaObject::invokeMethod(
            qApp, [track, weak = std::weak_ptr(worker)] {
                // Only from the reader that's still wanted
                if (auto alive = weak.lock(); alive && alive->running)
                {
                    SolrinoNowPlaying::instance().receive(track);
                }
            });

        std::unique_lock lock(worker->mutex);
        worker->wake.wait_for(lock, std::chrono::milliseconds(700), [&] {
            return !worker->running || worker->command.has_value();
        });
    }
}
#endif

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;

/// The audio of the app that's playing, found in Windows' volume mixer
struct AppAudio {
    QString app;
    qint64 foundAt = 0;
    std::vector<ComPtr<ISimpleAudioVolume>> volumes;
    std::vector<ComPtr<IAudioMeterInformation>> meters;
};

AppAudio &appAudio()
{
    static AppAudio audio;
    return audio;
}

QString processName(DWORD pid)
{
    auto *process =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr)
    {
        return {};
    }
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(process, 0, path, &size) != 0)
    {
        name = QFileInfo(QString::fromWCharArray(path, int(size)))
                   .completeBaseName()
                   .toLower();
    }
    CloseHandle(process);
    return name;
}

/// Finds the player's audio again every few seconds, as apps come and go
AppAudio &findAppAudio(const QString &app)
{
    auto &audio = appAudio();
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (audio.app == app && now - audio.foundAt < 3000)
    {
        return audio;
    }
    audio = {};
    audio.app = app;
    audio.foundAt = now;
    const auto wanted = app.toLower();
    if (wanted.isEmpty())
    {
        return audio;
    }

    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IMMDevice> speakers;
    ComPtr<IAudioSessionManager2> manager;
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&devices))) ||
        FAILED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia,
                                                &speakers)) ||
        FAILED(speakers->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
                                  nullptr, &manager)) ||
        FAILED(manager->GetSessionEnumerator(&sessions)))
    {
        return audio;
    }
    int count = 0;
    sessions->GetCount(&count);
    for (int i = 0; i < count; ++i)
    {
        ComPtr<IAudioSessionControl> control;
        ComPtr<IAudioSessionControl2> control2;
        DWORD pid = 0;
        if (FAILED(sessions->GetSession(i, &control)) ||
            FAILED(control.As(&control2)) ||
            FAILED(control2->GetProcessId(&pid)) || pid == 0)
        {
            continue;
        }
        // "Spotify.exe" is in Spotify's name, "chrome.exe" in Chrome's
        const auto name = processName(pid);
        if (name.size() < 3 || !wanted.contains(name))
        {
            continue;
        }
        ComPtr<ISimpleAudioVolume> volume;
        ComPtr<IAudioMeterInformation> meter;
        if (SUCCEEDED(control.As(&volume)))
        {
            audio.volumes.push_back(volume);
        }
        if (SUCCEEDED(control.As(&meter)))
        {
            audio.meters.push_back(meter);
        }
    }
    return audio;
}
#endif

}  // namespace

std::optional<float> SolrinoNowPlaying::level()
{
#ifdef Q_OS_WIN
    if (!this->track_.present)
    {
        return std::nullopt;
    }
    auto &audio = findAppAudio(this->track_.app);
    if (audio.meters.empty())
    {
        return std::nullopt;
    }
    float loudest = 0;
    for (const auto &meter : audio.meters)
    {
        float peak = 0;
        if (SUCCEEDED(meter->GetPeakValue(&peak)))
        {
            loudest = std::max(loudest, peak);
        }
    }
    return std::clamp(loudest, 0.0F, 1.0F);
#else
    return std::nullopt;
#endif
}

std::optional<int> SolrinoNowPlaying::nudgeVolume(int steps)
{
#ifdef Q_OS_WIN
    if (!this->track_.present)
    {
        return std::nullopt;
    }
    auto &audio = findAppAudio(this->track_.app);
    std::optional<int> result;
    for (const auto &volume : audio.volumes)
    {
        float now = 0;
        if (FAILED(volume->GetMasterVolume(&now)))
        {
            continue;
        }
        // To the nearest 5%, so it always lands on round numbers
        const float next = std::clamp(
            std::round(now * 20.0F + float(steps)) / 20.0F, 0.0F, 1.0F);
        if (SUCCEEDED(volume->SetMasterVolume(next, nullptr)))
        {
            result = int(std::lround(next * 100));
        }
    }
    return result;
#else
    (void)steps;
    return std::nullopt;
#endif
}

SolrinoNowPlaying &SolrinoNowPlaying::instance()
{
    static auto *nowPlaying = new SolrinoNowPlaying();
    return *nowPlaying;
}

SolrinoNowPlaying::SolrinoNowPlaying() = default;

const SolrinoNowPlaying::Track &SolrinoNowPlaying::track() const
{
    return this->track_;
}

void SolrinoNowPlaying::setActive(bool active)
{
#ifdef Q_OS_WIN
    if (active == (this->worker_ != nullptr))
    {
        if (this->worker_)
        {
            this->worker_->anyPlayer =
                getSettings()->solrinoNowPlayingAnyPlayer.getValue();
        }
        return;
    }
    if (!active)
    {
        this->worker_->running = false;
        this->worker_->wake.notify_all();
        this->worker_.reset();
        this->track_ = {};
        this->changed.invoke();
        return;
    }
    this->worker_ = std::make_shared<Worker>();
    this->worker_->anyPlayer =
        getSettings()->solrinoNowPlayingAnyPlayer.getValue();
    std::thread(run, this->worker_).detach();
#else
    (void)active;
#endif
}

void SolrinoNowPlaying::receive(Track track)
{
    const bool same = track.present == this->track_.present &&
                      track.playing == this->track_.playing &&
                      track.title == this->track_.title &&
                      track.shuffle == this->track_.shuffle &&
                      track.repeat == this->track_.repeat &&
                      track.artist == this->track_.artist &&
                      track.cover.cacheKey() == this->track_.cover.cacheKey() &&
                      std::abs((track.position - this->track_.position) -
                               (track.readAt - this->track_.readAt)) < 1500;
    this->track_ = std::move(track);
    if (!same)
    {
        this->changed.invoke();
    }
}

void SolrinoNowPlaying::send(Command command)
{
    if (!this->worker_)
    {
        return;
    }
    {
        std::lock_guard lock(this->worker_->mutex);
        this->worker_->command = command;
    }
    this->worker_->wake.notify_all();
}

}  // namespace chatterino

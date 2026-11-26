#include "widgets/settingspages/NowPlayingPage.hpp"

#include "singletons/Settings.hpp"
#include "singletons/SolrinoNowPlaying.hpp"
#include "widgets/helper/SolrinoNowPlayingOverlay.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"

#include <QCheckBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace chatterino {

namespace {

/// The overlay as it will look, on a stand-in for a game
class NowPlayingPreview : public QWidget
{
public:
    NowPlayingPreview()
    {
        this->setMinimumHeight(170);
        this->timer_.setInterval(33);
        QObject::connect(&this->timer_, &QTimer::timeout, this, [this] {
            if (solrinoNowPlayingCardAnimating() || ++this->ticks_ % 15 == 0)
            {
                this->update();
            }
        });
        this->timer_.start();
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // A busy, colourful backdrop, so the see-through and glass looks show
        const QRectF stage = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient sky(stage.topLeft(), stage.bottomRight());
        sky.setColorAt(0, QColor(38, 70, 120));
        sky.setColorAt(0.5, QColor(120, 60, 110));
        sky.setColorAt(1, QColor(200, 110, 60));
        p.setPen(Qt::NoPen);
        p.setBrush(sky);
        p.drawRoundedRect(stage, 10, 10);
        p.setBrush(QColor(255, 255, 255, 30));
        for (int i = 0; i < 6; ++i)
        {
            p.drawEllipse(QPointF(stage.width() * (0.1 + i * 0.17),
                                  stage.height() * (i % 2 ? 0.3 : 0.75)),
                          28 + i * 4, 28 + i * 4);
        }

        auto &nowPlaying = SolrinoNowPlaying::instance();
        auto track = nowPlaying.track();
        float level = -1;
        if (track.present)
        {
            level = nowPlaying.level().value_or(-1.0F);
        }
        else
        {
            // Nothing playing: a stand-in song
            track.title =
                QStringLiteral("A song with a long name, to show it sliding");
            track.artist = QStringLiteral("Artist");
            track.playing = true;
            track.length = 200000;
            track.position = 80000;
            track.readAt = 0;
            track.shuffle = true;
            track.repeat = 1;
        }

        const auto size = solrinoNowPlayingCardSize();
        const QRectF card(QPointF(stage.center().x() - size.width() / 2,
                                  stage.center().y() - size.height() / 2),
                          size);
        p.setOpacity(
            std::clamp(getSettings()->solrinoNowPlayingOpacity.getValue().toInt(),
                       20, 100) /
            100.0);
        paintSolrinoNowPlayingCard(p, card, track, this->devicePixelRatioF(),
                                   level);
    }

private:
    QTimer timer_;
    int ticks_ = 0;
};

QComboBox *addChoice(QFormLayout *form, const QString &label,
                     QStringSetting &setting,
                     const std::vector<std::pair<QString, QString>> &options,
                     QWidget *preview,
                     pajlada::Signals::SignalHolder &connections)
{
    auto *box = new ComboBox;
    for (const auto &[text, value] : options)
    {
        box->addItem(text, value);
    }
    // Follows the setting, so the theme buttons show up here too
    setting.connect(
        [box](const QString &value, auto) {
            const auto at = box->findData(value);
            box->setCurrentIndex(at >= 0 ? at : 0);
        },
        connections, true);
    QObject::connect(box, &QComboBox::activated, box, [box, &setting, preview] {
        setting = box->currentData().toString();
        preview->update();
    });
    form->addRow(label, box);
    return box;
}

QCheckBox *addCheck(QVBoxLayout *layout, const QString &label,
                    BoolSetting &setting, QWidget *preview,
                    pajlada::Signals::SignalHolder &connections,
                    const QString &tooltip = {})
{
    auto *box = new QCheckBox(label);
    box->setToolTip(tooltip);
    setting.connect(
        [box](bool on, auto) {
            box->setChecked(on);
        },
        connections, true);
    QObject::connect(box, &QCheckBox::toggled, box, [&setting, preview](bool on) {
        if (setting.getValue() != on)
        {
            setting = on;
        }
        preview->update();
    });
    layout->addWidget(box);
    return box;
}

QLabel *addTitle(QVBoxLayout *layout, const QString &text)
{
    auto *title = new QLabel(text);
    auto font = title->font();
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);
    return title;
}

/// A ready-made look: it sets the looks settings in one go
struct Theme {
    const char *name;
    const char *style;
    const char *accent;
    const char *border;
    const char *corners;
    const char *coverAnim;
    bool visualiser;
};

constexpr Theme THEMES[] = {
    {"Spotify", "glass", "spotify", "default", "round", "none", false},
    {"Apple Music", "solid", "pink", "default", "round", "none", false},
    {"Minimal", "transparent", "white", "none", "square", "none", false},
    {"Neon", "solid", "cyan", "accent", "round", "glow", true},
};

}  // namespace

NowPlayingPage::NowPlayingPage()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *content = new QWidget;
    scroll->setWidget(content);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto *s = getSettings();
    auto &holder = this->connections_;
    auto *intro = new QLabel(
        "Shows the song playing in Spotify (the app or open.spotify.com in "
        "your browser) on top of your screen, even over games. It reads it "
        "from Windows, so no Spotify login is needed.");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *preview = new NowPlayingPreview;
    layout->addWidget(preview);

    addCheck(layout, "Show the now playing overlay", s->solrinoNowPlaying,
             preview, holder);

    // Themes
    addTitle(layout, "Themes");
    auto *themeRow = new QHBoxLayout();
    for (const auto &theme : THEMES)
    {
        auto *button = new QPushButton(QString::fromUtf8(theme.name));
        QObject::connect(button, &QPushButton::clicked, this,
                         [&theme, preview] {
                             auto *settings = getSettings();
                             settings->solrinoNowPlayingStyle =
                                 QString::fromLatin1(theme.style);
                             settings->solrinoNowPlayingAccent =
                                 QString::fromLatin1(theme.accent);
                             settings->solrinoNowPlayingBorder =
                                 QString::fromLatin1(theme.border);
                             settings->solrinoNowPlayingCorners =
                                 QString::fromLatin1(theme.corners);
                             settings->solrinoNowPlayingCoverAnim =
                                 QString::fromLatin1(theme.coverAnim);
                             settings->solrinoNowPlayingVisualiser =
                                 theme.visualiser;
                             preview->update();
                         });
        themeRow->addWidget(button);
    }
    themeRow->addStretch(1);
    layout->addLayout(themeRow);

    // Looks
    addTitle(layout, "Look");
    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    addChoice(form, "Layout", s->solrinoNowPlayingLayout,
              {{"Album art + song", "song"},
               {"One line, no art", "compact"},
               {"Album art only", "art"}},
              preview, holder);
    addChoice(form, "Card", s->solrinoNowPlayingStyle,
              {{"Glass", "glass"},
               {"Solid", "solid"},
               {"Transparent", "transparent"}},
              preview, holder);
    addChoice(form, "Size", s->solrinoNowPlayingSize,
              {{"Small", "small"},
               {"Normal", "normal"},
               {"Large", "large"},
               {"Extra large", "xlarge"}},
              preview, holder);
    addChoice(form, "See-through", s->solrinoNowPlayingOpacity,
              {{"Not at all", "100"},
               {"A little", "85"},
               {"Half", "60"},
               {"Mostly", "35"}},
              preview, holder);
    addChoice(form, "Corners", s->solrinoNowPlayingCorners,
              {{"Rounded", "round"}, {"Square", "square"}}, preview, holder);
    addChoice(form, "Colour", s->solrinoNowPlayingAccent,
              {{"Spotify green", "spotify"},
               {"From the album art", "cover"},
               {"White", "white"},
               {"Pink", "pink"},
               {"Cyan", "cyan"},
               {"Purple", "purple"}},
              preview, holder);
    addChoice(form, "Border", s->solrinoNowPlayingBorder,
              {{"Normal", "default"},
               {"None", "none"},
               {"Glowing, in the colour", "accent"},
               {"White", "white"}},
              preview, holder);
    addChoice(form, "Album art", s->solrinoNowPlayingCoverAnim,
              {{"Still", "none"},
               {"Spin like a record", "spin"},
               {"Glow with the music", "glow"}},
              preview, holder);
    addChoice(form, "Show it", s->solrinoNowPlayingShow,
              {{"All the time", "always"},
               {"For 8 seconds when the song changes", "change"}},
              preview, holder);
    layout->addLayout(form);
    addCheck(layout, "Bars that bounce with the music",
             s->solrinoNowPlayingVisualiser, preview, holder,
             "Little bars on the album art that move with how loud the "
             "song is.");
    addCheck(layout, "Slide in when the song changes",
             s->solrinoNowPlayingSlideIn, preview, holder);
    addCheck(layout, "Slide long song names along", s->solrinoNowPlayingScroll,
             preview, holder, "Off cuts long names short with ...");

    // Buttons
    addTitle(layout, "Buttons and extras");
    addCheck(layout, "Shuffle button", s->solrinoNowPlayingShuffle, preview,
             holder);
    addCheck(layout, "Previous button", s->solrinoNowPlayingPrevious, preview,
             holder);
    addCheck(layout, "Play/pause button", s->solrinoNowPlayingPlayPause,
             preview, holder);
    addCheck(layout, "Next button", s->solrinoNowPlayingNext, preview, holder);
    addCheck(layout, "Repeat button", s->solrinoNowPlayingRepeat, preview,
             holder, "Click to go from off, to the whole list, to this song.");
    addCheck(layout, "Progress bar", s->solrinoNowPlayingProgress, preview,
             holder);
    addCheck(layout, "Show times by the progress bar", s->solrinoNowPlayingTimes,
             preview, holder);
    addCheck(layout, "Scroll over it to change the volume",
             s->solrinoNowPlayingVolumeScroll, preview, holder,
             "Changes only the music app's volume, not your whole PC's.");
    addCheck(layout, "Hide when paused", s->solrinoNowPlayingHideWhenPaused,
             preview, holder);
    addCheck(layout, "Spotify in a browser", s->solrinoNowPlayingBrowser,
             preview, holder,
             "Works with Spotify's web player (open.spotify.com) in Chrome, "
             "Edge, Firefox, Brave, Opera or Vivaldi, so you don't need the "
             "Spotify app open. The app is used first when both are playing.");
    addCheck(layout, "Any music player, not just Spotify",
             s->solrinoNowPlayingAnyPlayer, preview, holder,
             "Also show songs from other apps, like YouTube in your browser.");

    auto *clicksNote = new QLabel(
        "With no buttons and volume scrolling off, the overlay lets every "
        "click through to whatever is underneath it.");
    clicksNote->setWordWrap(true);
    clicksNote->setStyleSheet("color: gray;");
    layout->addWidget(clicksNote);

    // It can only be moved from here, so it never gets dragged by accident
    auto *placeRow = new QHBoxLayout();
    auto *move = new QPushButton("Move overlay");
    move->setToolTip("Then drag the overlay where you want it, on any screen, "
                     "and let go.");
    QObject::connect(move, &QPushButton::clicked, this, [] {
        moveSolrinoNowPlayingOverlay();
    });
    auto *reset = new QPushButton("Reset position");
    QObject::connect(reset, &QPushButton::clicked, this, [] {
        resetSolrinoNowPlayingOverlayPlace();
    });
    placeRow->addWidget(move);
    placeRow->addWidget(reset);
    placeRow->addStretch(1);
    layout->addLayout(placeRow);

    auto *songNote = new QLabel(
        "Type /song in any chat to send the song you're listening to.");
    songNote->setWordWrap(true);
    songNote->setStyleSheet("color: gray;");
    layout->addWidget(songNote);
    layout->addStretch(1);

    // The preview follows the song too
    holder.managedConnect(SolrinoNowPlaying::instance().changed, [preview] {
        preview->update();
    });
}

}  // namespace chatterino

#include "widgets/helper/SolrinoNowPlayingOverlay.hpp"

#include "singletons/Settings.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QDateTime>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QRadialGradient>
#include <QScreen>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace chatterino {

namespace {

const QColor SPOTIFY_GREEN(30, 215, 96);
constexpr int MARGIN = 3;
// Long song names slide this fast, and wait this long at the start
constexpr double SCROLL_PX_PER_SECOND = 30.0;
constexpr qint64 SCROLL_PAUSE_MS = 2000;
// One turn of the spinning cover
constexpr qint64 SPIN_MS = 6000;
constexpr qint64 SLIDE_IN_MS = 380;
constexpr qint64 VOLUME_SHOWN_MS = 1200;

// Whether the last card drawn is moving, so it needs redrawing often
bool lastCardAnimating = false;

double sizeFactor()
{
    const auto size = getSettings()->solrinoNowPlayingSize.getValue();
    if (size == QStringLiteral("small"))
    {
        return 0.8;
    }
    if (size == QStringLiteral("large"))
    {
        return 1.3;
    }
    if (size == QStringLiteral("xlarge"))
    {
        return 1.7;
    }
    return 1.0;
}

bool artOnly()
{
    return getSettings()->solrinoNowPlayingLayout.getValue() ==
           QStringLiteral("art");
}

bool compact()
{
    return getSettings()->solrinoNowPlayingLayout.getValue() ==
           QStringLiteral("compact");
}

bool showProgress()
{
    return !artOnly() && getSettings()->solrinoNowPlayingProgress;
}

bool anyButtons()
{
    auto *s = getSettings();
    return !artOnly() &&
           (s->solrinoNowPlayingPrevious || s->solrinoNowPlayingPlayPause ||
            s->solrinoNowPlayingNext || s->solrinoNowPlayingShuffle ||
            s->solrinoNowPlayingRepeat);
}

bool squareCorners()
{
    return getSettings()->solrinoNowPlayingCorners.getValue() ==
           QStringLiteral("square");
}

QString clock(qint64 ms)
{
    const auto seconds = std::max<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(
        seconds % 60, 2, 10, QLatin1Char('0'));
}

/// A bright colour from the album art
QColor coverColour(const QImage &cover)
{
    static qint64 lastKey = 0;
    static QColor lastColour;
    if (cover.isNull())
    {
        return SPOTIFY_GREEN;
    }
    if (cover.cacheKey() == lastKey)
    {
        return lastColour;
    }
    // The most colourful of a few averaged patches
    const auto small = cover.scaled(4, 4, Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_RGB32);
    QColor best = small.pixelColor(0, 0);
    for (int y = 0; y < small.height(); ++y)
    {
        for (int x = 0; x < small.width(); ++x)
        {
            const auto c = small.pixelColor(x, y);
            if (c.hsvSaturationF() * c.valueF() >
                best.hsvSaturationF() * best.valueF())
            {
                best = c;
            }
        }
    }
    float h = 0;
    float sat = 0;
    float v = 0;
    best.getHsvF(&h, &sat, &v);
    // Bright enough to see on a dark card
    lastColour = sat < 0.15F ? QColor(235, 235, 240)
                             : QColor::fromHsvF(h < 0 ? 0 : h,
                                                std::max(sat, 0.55F),
                                                std::max(v, 0.85F));
    lastKey = cover.cacheKey();
    return lastColour;
}

QColor accentColour(const QImage &cover)
{
    const auto accent = getSettings()->solrinoNowPlayingAccent.getValue();
    if (accent == QStringLiteral("cover"))
    {
        return coverColour(cover);
    }
    if (accent == QStringLiteral("white"))
    {
        return QColor(240, 240, 245);
    }
    if (accent == QStringLiteral("pink"))
    {
        return QColor(250, 45, 85);
    }
    if (accent == QStringLiteral("cyan"))
    {
        return QColor(0, 229, 255);
    }
    if (accent == QStringLiteral("purple"))
    {
        return QColor(170, 110, 255);
    }
    return SPOTIFY_GREEN;
}

/// Where the cover has turned to. It only turns while the song plays.
double spinAngle(bool playing)
{
    static double angle = 0;
    static qint64 lastAt = 0;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (playing && lastAt > 0)
    {
        // Small steps only, so a long gap between draws doesn't jump
        const auto step = std::clamp<qint64>(now - lastAt, 0, 100);
        angle = std::fmod(angle + 360.0 * double(step) / SPIN_MS, 360.0);
    }
    lastAt = now;
    return angle;
}

/// 0 to 1 for each bouncing bar. With no reading of the music's loudness,
/// they bounce on their own while it plays.
std::array<double, 5> barHeights(float level, bool playing)
{
    static std::array<double, 5> shown{};
    const double t = double(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
    const double loud =
        !playing ? 0.0 : level >= 0 ? std::sqrt(double(level)) : 0.7;
    for (size_t i = 0; i < shown.size(); ++i)
    {
        const double wobble =
            0.5 + 0.5 * std::sin(t * (6.0 + double(i) * 1.7) + double(i) * 1.3);
        const double target = loud * (0.35 + 0.65 * wobble);
        // Up fast, down slower, like a real meter
        shown[i] += (target - shown[i]) * (target > shown[i] ? 0.6 : 0.25);
    }
    return shown;
}

void drawBars(QPainter &p, const QRectF &area, const std::array<double, 5> &bars,
              const QColor &colour)
{
    const double gap = area.width() * 0.08;
    const double width = (area.width() - gap * 4) / 5;
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    for (int i = 0; i < 5; ++i)
    {
        const double h = std::max(width, area.height() * bars[size_t(i)]);
        p.drawRoundedRect(QRectF(area.left() + i * (width + gap),
                                 area.bottom() - h, width, h),
                          width / 2, width / 2);
    }
}

QPolygonF arrowHead(QPointF tip, double dx, double size)
{
    return QPolygonF({tip, QPointF(tip.x() - dx * size, tip.y() - size),
                      QPointF(tip.x() - dx * size, tip.y() + size)});
}

/// Previous, play/pause, next, shuffle or repeat
void drawButton(QPainter &p, const QRectF &area, int which,
                const SolrinoNowPlaying::Track &track, const QColor &accent,
                double k)
{
    p.setPen(Qt::NoPen);
    const auto c = area.center();
    const double s = 5.5 * k;
    if (which == 1)
    {
        p.setBrush(QColor(255, 255, 255, 235));
        p.drawEllipse(area.adjusted(2 * k, 2 * k, -2 * k, -2 * k));
        p.setBrush(QColor(18, 18, 20));
        if (track.playing)
        {
            p.drawRect(QRectF(c.x() - s * 0.75, c.y() - s, s * 0.5, s * 2));
            p.drawRect(QRectF(c.x() + s * 0.25, c.y() - s, s * 0.5, s * 2));
        }
        else
        {
            p.drawPolygon(QPolygonF({QPointF(c.x() - s * 0.6, c.y() - s),
                                     QPointF(c.x() + s, c.y()),
                                     QPointF(c.x() - s * 0.6, c.y() + s)}));
        }
        return;
    }
    if (which == 0 || which == 2)
    {
        // Previous points left, next points right, each with a bar
        p.setBrush(QColor(255, 255, 255, 230));
        const double dir = which == 0 ? -1 : 1;
        p.drawPolygon(QPolygonF({QPointF(c.x() - dir * s * 0.7, c.y() - s),
                                 QPointF(c.x() + dir * s * 0.6, c.y()),
                                 QPointF(c.x() - dir * s * 0.7, c.y() + s)}));
        p.drawRect(QRectF(c.x() + dir * s * 0.6 - (dir > 0 ? 0 : 2 * k),
                          c.y() - s, 2 * k, s * 2));
        return;
    }

    // Shuffle and repeat light up in the bar colour when on
    const bool on = which == 3 ? track.shuffle : track.repeat != 0;
    const QColor colour = on ? accent : QColor(255, 255, 255, 150);
    QPen pen(colour, 1.6 * k);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    const double w = s * 1.1;
    if (which == 3)
    {
        // Two crossing paths, each ending in an arrow
        QPainterPath one;
        one.moveTo(c.x() - w, c.y() - s * 0.6);
        one.cubicTo(c.x() - w * 0.1, c.y() - s * 0.6, c.x() + w * 0.1,
                    c.y() + s * 0.6, c.x() + w * 0.75, c.y() + s * 0.6);
        QPainterPath two;
        two.moveTo(c.x() - w, c.y() + s * 0.6);
        two.cubicTo(c.x() - w * 0.1, c.y() + s * 0.6, c.x() + w * 0.1,
                    c.y() - s * 0.6, c.x() + w * 0.75, c.y() - s * 0.6);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(one);
        p.drawPath(two);
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawPolygon(arrowHead(QPointF(c.x() + w * 1.1, c.y() - s * 0.6), 1,
                                s * 0.38));
        p.drawPolygon(arrowHead(QPointF(c.x() + w * 1.1, c.y() + s * 0.6), 1,
                                s * 0.38));
    }
    else
    {
        // A loop with an arrow, and a 1 when it repeats this song
        const QRectF loop(c.x() - w, c.y() - s * 0.65, w * 2, s * 1.3);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(loop, s * 0.45, s * 0.45);
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawPolygon(
            arrowHead(QPointF(c.x() + w * 0.35, loop.top()), 1, s * 0.38));
        if (track.repeat == 2)
        {
            auto font = p.font();
            font.setPixelSize(std::max(6, int(7 * k)));
            font.setBold(true);
            p.setFont(font);
            p.setPen(colour);
            p.drawText(loop, Qt::AlignCenter, QStringLiteral("1"));
        }
    }
    if (on)
    {
        // A dot under it, like Spotify
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawEllipse(QPointF(c.x(), c.y() + s * 1.45), 1.2 * k, 1.2 * k);
    }
}

}  // namespace

bool solrinoNowPlayingCardAnimating()
{
    return lastCardAnimating;
}

QSizeF solrinoNowPlayingCardSize()
{
    const double k = sizeFactor();
    const double progress = showProgress() ? 6 : 0;
    if (compact())
    {
        return {300 * k, (40 + progress) * k};
    }
    const double height = (68 + progress) * k;
    if (artOnly())
    {
        return {height, height};
    }
    return {330 * k, height};
}

void paintSolrinoNowPlayingCard(QPainter &p, const QRectF &card,
                                const SolrinoNowPlaying::Track &track,
                                qreal devicePixelRatio, float level,
                                std::array<QRectF, 5> *buttons)
{
    auto *s = getSettings();
    const double k = sizeFactor();
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto accent = accentColour(track.cover);
    bool animating = false;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const double radius = (squareCorners() ? 3 : 14) * k;
    QPainterPath shape;
    shape.addRoundedRect(card, radius, radius);
    const auto style = s->solrinoNowPlayingStyle.getValue();

    if (style == QStringLiteral("glass"))
    {
        // Frosted: the cover, blurred, behind a dark tint
        p.save();
        p.setClipPath(shape);
        if (!track.cover.isNull())
        {
            const auto tiny = track.cover.scaled(6, 6, Qt::IgnoreAspectRatio,
                                                 Qt::SmoothTransformation);
            p.drawImage(card, tiny.scaled(card.size().toSize(),
                                          Qt::IgnoreAspectRatio,
                                          Qt::SmoothTransformation));
        }
        else
        {
            p.fillRect(card, QColor(60, 60, 70));
        }
        p.fillRect(card, QColor(14, 14, 18, 150));
        QLinearGradient shine(card.topLeft(), card.bottomLeft());
        shine.setColorAt(0, QColor(255, 255, 255, 34));
        shine.setColorAt(0.5, QColor(255, 255, 255, 6));
        shine.setColorAt(1, QColor(255, 255, 255, 0));
        p.fillRect(card, shine);
        p.restore();
    }
    else if (style == QStringLiteral("solid"))
    {
        p.fillPath(shape, QColor(20, 20, 23, 245));
    }
    // "transparent" draws no card at all

    // The edge
    const auto border = s->solrinoNowPlayingBorder.getValue();
    if (border == QStringLiteral("accent"))
    {
        // A neon edge, with a soft glow inside it
        p.setBrush(Qt::NoBrush);
        for (int i = 3; i >= 1; --i)
        {
            auto glow = accent;
            glow.setAlpha(28);
            p.setPen(QPen(glow, i * 2.2 * k));
            p.drawPath(shape);
        }
        p.setPen(QPen(accent, 1.4 * k));
        p.drawPath(shape);
    }
    else if (border == QStringLiteral("white"))
    {
        p.setPen(QPen(QColor(255, 255, 255, 200), 1.2 * k));
        p.setBrush(Qt::NoBrush);
        p.drawPath(shape);
    }
    else if (border != QStringLiteral("none") &&
             style != QStringLiteral("transparent"))
    {
        p.setPen(QPen(QColor(255, 255, 255,
                             style == QStringLiteral("glass") ? 46 : 22),
                      1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(shape);
    }

    const double pad = (compact() ? 8 : 10) * k;
    const double barSpace = showProgress() ? 6 * k : 0;
    const double inner = card.height() - pad * 2 - barSpace;
    const auto coverAnim = s->solrinoNowPlayingCoverAnim.getValue();
    const bool visualiser = s->solrinoNowPlayingVisualiser;

    // Album art, except in the one-line look
    QRectF artRect(card.left() + pad, card.top() + pad, 0, inner);
    if (!compact())
    {
        artRect.setWidth(inner);
        const bool spin = coverAnim == QStringLiteral("spin");
        QPainterPath artShape;
        if (spin)
        {
            artShape.addEllipse(artRect);
        }
        else
        {
            const double artRadius = (squareCorners() ? 2 : 7) * k;
            artShape.addRoundedRect(artRect, artRadius, artRadius);
        }

        if (coverAnim == QStringLiteral("glow"))
        {
            // A soft light behind the cover that breathes with the music
            const double beat =
                level >= 0 && track.playing
                    ? double(level)
                    : 0.5 + 0.5 * std::sin(double(now % 4000) / 4000.0 * 6.2832);
            QRadialGradient light(artRect.center(), artRect.width() * 0.85);
            auto inside = accent;
            inside.setAlpha(int(70 + 110 * beat));
            auto outside = accent;
            outside.setAlpha(0);
            light.setColorAt(0.55, inside);
            light.setColorAt(1, outside);
            p.save();
            p.setClipPath(shape);
            p.setPen(Qt::NoPen);
            p.setBrush(light);
            p.drawEllipse(artRect.center(), artRect.width() * 0.85,
                          artRect.width() * 0.85);
            p.restore();
            animating = true;
        }

        if (!track.cover.isNull())
        {
            p.save();
            p.setClipPath(artShape);
            const auto cover = track.cover.scaled(
                (artRect.size() * devicePixelRatio).toSize(),
                Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            if (spin)
            {
                p.translate(artRect.center());
                p.rotate(spinAngle(track.playing));
                p.translate(-artRect.center());
                animating = animating || track.playing;
            }
            p.drawImage(artRect, cover);
            p.restore();
        }
        else
        {
            p.fillPath(artShape, QColor(40, 40, 46));
            auto noteFont = p.font();
            noteFont.setPixelSize(int(inner * 0.5));
            p.setFont(noteFont);
            p.setPen(accent);
            p.drawText(artRect, Qt::AlignCenter, QStringLiteral("♫"));
        }
        if (spin)
        {
            // The middle of a record
            p.setPen(QPen(QColor(0, 0, 0, 120), 1));
            p.setBrush(QColor(18, 18, 20));
            p.drawEllipse(artRect.center(), inner * 0.09, inner * 0.09);
        }

        if (visualiser)
        {
            // Bars bouncing along the bottom of the cover
            const QRectF barsArea(artRect.left() + inner * 0.18,
                                  artRect.bottom() - inner * 0.42,
                                  inner * 0.64, inner * 0.34);
            p.save();
            p.setClipPath(artShape);
            QLinearGradient shade(artRect.bottomLeft(),
                                  QPointF(artRect.left(),
                                          artRect.bottom() - inner * 0.55));
            shade.setColorAt(0, QColor(0, 0, 0, 150));
            shade.setColorAt(1, QColor(0, 0, 0, 0));
            p.fillRect(artRect, shade);
            drawBars(p, barsArea, barHeights(level, track.playing),
                     QColor(255, 255, 255, 225));
            p.restore();
            animating = animating || track.playing;
        }
    }

    if (buttons != nullptr)
    {
        *buttons = {};
    }
    if (artOnly())
    {
        lastCardAnimating = animating;
        p.restore();
        return;
    }

    // Buttons on the right: shuffle, previous, play/pause, next, repeat
    double right = card.right() - pad;
    const double button = (compact() ? 24 : 30) * k;
    const double middle = card.top() + pad + inner / 2;
    const std::array<bool, 5> shown{
        s->solrinoNowPlayingPrevious.getValue(),
        s->solrinoNowPlayingPlayPause.getValue(),
        s->solrinoNowPlayingNext.getValue(),
        s->solrinoNowPlayingShuffle.getValue(),
        s->solrinoNowPlayingRepeat.getValue(),
    };
    for (int i : {4, 2, 1, 0, 3})
    {
        if (!shown[size_t(i)])
        {
            continue;
        }
        const double width = i >= 3 ? button * 0.8 : button;
        const QRectF area(right - width, middle - button / 2, width, button);
        if (buttons != nullptr)
        {
            (*buttons)[size_t(i)] = area;
        }
        drawButton(p, area, i, track, accent, k);
        right -= width + 2 * k;
    }

    // In the one-line look the bars sit in front of the song name
    double textLeft = compact() ? card.left() + pad + 2 * k
                                : artRect.right() + 11 * k;
    if (compact() && visualiser)
    {
        const double h = inner * 0.5;
        drawBars(p, QRectF(textLeft, middle - h / 2, h * 1.1, h),
                 barHeights(level, track.playing), accent);
        textLeft += h * 1.1 + 8 * k;
        animating = animating || track.playing;
    }

    // Song and artist
    const QRectF textArea(textLeft, card.top() + pad,
                          right - 6 * k - textLeft, inner);
    QFont titleFont = p.font();
    titleFont.setPixelSize(int((compact() ? 13 : 14) * k));
    titleFont.setBold(true);
    QFont artistFont = p.font();
    artistFont.setPixelSize(int(12 * k));
    artistFont.setBold(false);
    const QFontMetricsF titleMetrics(titleFont);
    const QFontMetricsF artistMetrics(artistFont);
    const bool shadow = style == QStringLiteral("transparent");
    const bool scroll = s->solrinoNowPlayingScroll;

    const auto drawLine = [&](const QFont &font, const QFontMetricsF &metrics,
                              const QString &line, double y,
                              const QColor &color) {
        const double room = std::max(0.0, textArea.width());
        const double width = metrics.horizontalAdvance(line);
        p.setFont(font);
        const auto put = [&](const QString &text, double x) {
            if (shadow)
            {
                p.setPen(QColor(0, 0, 0, 200));
                p.drawText(QPointF(x + 1, y + metrics.ascent() + 1), text);
            }
            p.setPen(color);
            p.drawText(QPointF(x, y + metrics.ascent()), text);
        };
        if (!scroll || width <= room)
        {
            put(metrics.elidedText(line, Qt::ElideRight, room), textArea.left());
            return;
        }
        // Too long: it waits, slides along, and comes round again
        animating = true;
        const double gap = 30 * k;
        const double travel = width + gap;
        const auto slideMs = qint64(travel / SCROLL_PX_PER_SECOND * 1000);
        const auto at = now % (slideMs + SCROLL_PAUSE_MS);
        const double offset =
            at < SCROLL_PAUSE_MS
                ? 0
                : travel * double(at - SCROLL_PAUSE_MS) / double(slideMs);
        p.save();
        p.setClipRect(QRectF(textArea.left(), y - 2 * k, room,
                             metrics.height() + 4 * k));
        put(line, textArea.left() - offset);
        put(line, textArea.left() - offset + travel);
        p.restore();
    };

    if (compact())
    {
        // One line: "Song · Artist"
        const auto line = track.artist.isEmpty()
                              ? track.title
                              : track.title + QStringLiteral("  ·  ") +
                                    track.artist;
        drawLine(titleFont, titleMetrics, line,
                 textArea.center().y() - titleMetrics.height() / 2, Qt::white);
    }
    else
    {
        const double block =
            titleMetrics.height() + 2 * k + artistMetrics.height();
        const double top = textArea.center().y() - block / 2;
        drawLine(titleFont, titleMetrics, track.title, top, Qt::white);
        drawLine(artistFont, artistMetrics, track.artist,
                 top + titleMetrics.height() + 2 * k, QColor(200, 200, 206));
    }

    // Progress along the bottom
    if (showProgress() && track.length > 0)
    {
        auto position = track.position;
        if (track.playing && track.readAt > 0)
        {
            position += now - track.readAt;
        }
        position = std::clamp<qint64>(position, 0, track.length);
        const double done = double(position) / double(track.length);
        QRectF bar(card.left() + pad, card.bottom() - pad - 3 * k,
                   card.width() - pad * 2, 3 * k);
        if (s->solrinoNowPlayingTimes)
        {
            // Time played on the left, song length on the right
            QFont timeFont = p.font();
            timeFont.setPixelSize(std::max(8, int(10 * k)));
            timeFont.setBold(false);
            const QFontMetricsF timeMetrics(timeFont);
            const auto played = clock(position);
            const auto total = clock(track.length);
            const double room =
                timeMetrics.horizontalAdvance(QStringLiteral("00:00")) + 6 * k;
            p.setFont(timeFont);
            p.setPen(QColor(220, 220, 226));
            const double baseline = bar.center().y() + timeMetrics.ascent() / 2 -
                                    timeMetrics.descent() / 2;
            p.drawText(QPointF(bar.left(), baseline), played);
            p.drawText(QPointF(bar.right() -
                                   timeMetrics.horizontalAdvance(total),
                               baseline),
                       total);
            bar.adjust(room, 0, -room, 0);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawRoundedRect(bar, bar.height() / 2, bar.height() / 2);
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(bar.left(), bar.top(),
                                 std::max(bar.height(), bar.width() * done),
                                 bar.height()),
                          bar.height() / 2, bar.height() / 2);
    }
    lastCardAnimating = animating;
    p.restore();
}

namespace {

class NowPlayingOverlay : public QWidget
{
public:
    NowPlayingOverlay()
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                               Qt::WindowStaysOnTopHint |
                               Qt::NoDropShadowWindowHint |
                               Qt::WindowDoesNotAcceptFocus)
    {
        this->setAttribute(Qt::WA_TranslucentBackground);
        this->setAttribute(Qt::WA_ShowWithoutActivating);
        this->setMouseTracking(true);
        this->setWindowTitle(QStringLiteral("Solrino now playing"));

        // The progress bar moves while a song plays; names slide, covers
        // spin and bars bounce
        this->tick_.setInterval(33);
        QObject::connect(&this->tick_, &QTimer::timeout, this, [this] {
            if (!this->isVisible())
            {
                return;
            }
            const auto now = QDateTime::currentMSecsSinceEpoch();
            const bool slow = ++this->ticks_ % 15 == 0;
            if (lastCardAnimating || now < this->slideInUntil() ||
                now < this->volumeUntil_ ||
                (slow && SolrinoNowPlaying::instance().track().playing))
            {
                this->update();
            }
        });
        this->tick_.start();

        this->popupEnd_.setSingleShot(true);
        QObject::connect(&this->popupEnd_, &QTimer::timeout, this, [this] {
            this->refresh();
        });
    }

    /// Slides in, or pops up, when the song changes
    void songMaybeChanged()
    {
        const auto &track = SolrinoNowPlaying::instance().track();
        const auto key = track.present ? track.title + u'\n' + track.artist
                                       : QString();
        if (key != this->lastSong_)
        {
            this->lastSong_ = key;
            if (!key.isEmpty())
            {
                this->popupEnd_.start(POPUP_MS);
                this->changedAt_ = QDateTime::currentMSecsSinceEpoch();
            }
        }
        this->refresh();
    }

    /// Size, place, clicks and visibility from the settings and the song
    void refresh()
    {
        const auto &track = SolrinoNowPlaying::instance().track();
        const bool popupOnly = getSettings()->solrinoNowPlayingShow.getValue() ==
                               QStringLiteral("change");
        const bool wanted =
            this->moving_ ||
            (getSettings()->solrinoNowPlaying && track.present &&
             !(getSettings()->solrinoNowPlayingHideWhenPaused && !track.playing) &&
             (!popupOnly || this->popupEnd_.isActive()));
        if (!wanted)
        {
            this->hide();
            return;
        }

        // With no buttons and no volume scrolling it takes no clicks at all:
        // they go to whatever is underneath. While moving it, it has to.
        const bool clickThrough =
            !this->moving_ && !anyButtons() &&
            !getSettings()->solrinoNowPlayingVolumeScroll;
        if (clickThrough != this->clickThrough_ || !this->isVisible())
        {
            const bool wasVisible = this->isVisible();
            this->clickThrough_ = clickThrough;
            this->setWindowFlag(Qt::WindowTransparentForInput, clickThrough);
            if (wasVisible)
            {
                this->show();
            }
        }

        const auto oldSize = this->size();
        this->setFixedSize(solrinoNowPlayingCardSize().toSize() +
                           QSize(MARGIN * 2, MARGIN * 2));
        this->setWindowOpacity(
            this->moving_
                ? 1.0
                : std::clamp(
                      getSettings()->solrinoNowPlayingOpacity.getValue().toInt(),
                      20, 100) /
                      100.0);
        if (!this->isVisible() || oldSize != this->size())
        {
            this->placeFromSettings();
        }
        if (!this->isVisible())
        {
            this->show();
        }
        this->update();
    }

    void startMoving()
    {
        this->moving_ = true;
        this->refresh();
        this->raise();
    }

    void placeFromSettings()
    {
        // "screen;x;y" with x and y from 0 to 1 across that screen's free space
        const auto parts =
            getSettings()->solrinoNowPlayingPlace.getValue().split(';');
        QScreen *screen = QGuiApplication::primaryScreen();
        double x = 0.02;
        double y = 0.04;
        if (parts.size() == 3)
        {
            for (auto *candidate : QGuiApplication::screens())
            {
                if (candidate->name() == parts[0])
                {
                    screen = candidate;
                    x = std::clamp(parts[1].toDouble(), 0.0, 1.0);
                    y = std::clamp(parts[2].toDouble(), 0.0, 1.0);
                }
            }
        }
        if (screen == nullptr)
        {
            return;
        }
        const auto area = screen->availableGeometry();
        this->move(area.left() + int((area.width() - this->width()) * x),
                   area.top() + int((area.height() - this->height()) * y));
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        auto track = SolrinoNowPlaying::instance().track();
        if (!track.present)
        {
            // Moving it with nothing playing: show what it will look like
            track.title = QStringLiteral("Drag me where you want me");
            track.artist = QStringLiteral("Let go to place it");
        }
        const QRectF card(MARGIN, MARGIN, this->width() - MARGIN * 2,
                          this->height() - MARGIN * 2);

        // Sliding in from the side as a new song starts
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (now < this->slideInUntil() && !this->moving_)
        {
            const double t =
                double(now - this->changedAt_) / double(SLIDE_IN_MS);
            const double ease = 1 - std::pow(1 - std::clamp(t, 0.0, 1.0), 3);
            p.setOpacity(ease);
            p.translate(-(1 - ease) * 28, 0);
        }

        // Loudness is only needed for the bars and the glow
        const bool needsLevel =
            getSettings()->solrinoNowPlayingVisualiser ||
            getSettings()->solrinoNowPlayingCoverAnim.getValue() ==
                QStringLiteral("glow");
        const auto level = needsLevel ? SolrinoNowPlaying::instance().level()
                                      : std::nullopt;
        paintSolrinoNowPlayingCard(p, card, track, this->devicePixelRatioF(),
                                   level ? *level : -1.0F, &this->buttons_);
        p.resetTransform();
        p.setOpacity(1);

        if (now < this->volumeUntil_)
        {
            this->paintVolume(p, card);
        }
        if (this->moving_)
        {
            p.setPen(QPen(SPOTIFY_GREEN, 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(card.adjusted(1, 1, -1, -1), 14, 14);
        }
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (this->moving_ || !getSettings()->solrinoNowPlayingVolumeScroll)
        {
            return;
        }
        // One notch of the wheel is 5%
        this->wheelSum_ += event->angleDelta().y();
        const int steps = this->wheelSum_ / 120;
        if (steps == 0)
        {
            return;
        }
        this->wheelSum_ -= steps * 120;
        if (auto volume = SolrinoNowPlaying::instance().nudgeVolume(steps))
        {
            this->volume_ = *volume;
            this->volumeUntil_ =
                QDateTime::currentMSecsSinceEpoch() + VOLUME_SHOWN_MS;
            this->update();
        }
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (this->moving_)
        {
            if (event->button() == Qt::LeftButton)
            {
                this->dragFrom_ = event->globalPosition().toPoint() - this->pos();
                this->dragging_ = true;
            }
            return;
        }
        if (event->button() == Qt::RightButton)
        {
            this->showMenu(event->globalPosition().toPoint());
            return;
        }
        if (event->button() != Qt::LeftButton)
        {
            return;
        }
        static constexpr std::array<SolrinoNowPlaying::Command, 5> commands{
            SolrinoNowPlaying::Command::Previous,
            SolrinoNowPlaying::Command::PlayPause,
            SolrinoNowPlaying::Command::Next,
            SolrinoNowPlaying::Command::Shuffle,
            SolrinoNowPlaying::Command::Repeat,
        };
        for (size_t i = 0; i < commands.size(); ++i)
        {
            if (this->buttons_[i].contains(event->position()))
            {
                SolrinoNowPlaying::instance().send(commands[i]);
                return;
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (this->dragging_)
        {
            // Anywhere on any screen
            this->move(event->globalPosition().toPoint() - this->dragFrom_);
            return;
        }
        bool overButton = false;
        for (const auto &area : this->buttons_)
        {
            overButton = overButton || area.contains(event->position());
        }
        this->setCursor(this->moving_  ? Qt::SizeAllCursor
                        : overButton ? Qt::PointingHandCursor
                                     : Qt::ArrowCursor);
    }

    void mouseReleaseEvent(QMouseEvent * /*event*/) override
    {
        if (this->dragging_)
        {
            // Placed: it stays put until Move overlay is pressed again
            this->dragging_ = false;
            this->moving_ = false;
            this->savePlace();
            this->refresh();
        }
    }

private:
    qint64 slideInUntil() const
    {
        return getSettings()->solrinoNowPlayingSlideIn
                   ? this->changedAt_ + SLIDE_IN_MS
                   : 0;
    }

    /// "Volume 60%" with a bar, over the middle of the card
    void paintVolume(QPainter &p, const QRectF &card)
    {
        const double k = sizeFactor();
        const QRectF pill(card.center().x() - 60 * k, card.center().y() - 14 * k,
                          120 * k, 28 * k);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(10, 10, 12, 225));
        p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        auto font = p.font();
        font.setPixelSize(int(11 * k));
        font.setBold(true);
        p.setFont(font);
        p.setPen(Qt::white);
        const QRectF label(pill.left() + 12 * k, pill.top(), 40 * k,
                           pill.height());
        p.drawText(label, Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1%").arg(this->volume_));
        const QRectF bar(label.right() + 4 * k, pill.center().y() - 2 * k,
                         pill.right() - 12 * k - label.right() - 4 * k, 4 * k);
        p.setBrush(QColor(255, 255, 255, 50));
        p.drawRoundedRect(bar, 2 * k, 2 * k);
        p.setBrush(accentColour(SolrinoNowPlaying::instance().track().cover));
        p.drawRoundedRect(QRectF(bar.left(), bar.top(),
                                 bar.width() * this->volume_ / 100.0,
                                 bar.height()),
                          2 * k, 2 * k);
    }

    void savePlace()
    {
        auto *screen = QGuiApplication::screenAt(this->geometry().center());
        if (screen == nullptr)
        {
            return;
        }
        const auto area = screen->availableGeometry();
        const auto room = [](int space) {
            return std::max(1, space);
        };
        const double x = std::clamp(
            double(this->x() - area.left()) / room(area.width() - this->width()),
            0.0, 1.0);
        const double y = std::clamp(
            double(this->y() - area.top()) / room(area.height() - this->height()),
            0.0, 1.0);
        getSettings()->solrinoNowPlayingPlace =
            QStringLiteral("%1;%2;%3")
                .arg(screen->name(), QString::number(x, 'f', 4),
                     QString::number(y, 'f', 4));
        getSettings()->requestSave();
        // Snaps back inside the screen if it was dragged half off
        this->placeFromSettings();
    }

    void showMenu(QPoint at)
    {
        QMenu menu;
        menu.addAction(QStringLiteral("Move overlay"), [this] {
            this->startMoving();
        });
        menu.addAction(QStringLiteral("Hide overlay"), [] {
            getSettings()->solrinoNowPlaying = false;
        });
        menu.exec(at);
    }

    static constexpr int POPUP_MS = 8000;
    QTimer tick_;
    QTimer popupEnd_;
    int ticks_ = 0;
    QString lastSong_;
    qint64 changedAt_ = 0;
    int wheelSum_ = 0;
    int volume_ = 0;
    qint64 volumeUntil_ = 0;
    std::array<QRectF, 5> buttons_{};
    bool moving_ = false;
    bool dragging_ = false;
    bool clickThrough_ = false;
    QPoint dragFrom_;
};

QPointer<NowPlayingOverlay> &overlayInstance()
{
    static QPointer<NowPlayingOverlay> overlay;
    return overlay;
}

}  // namespace

void moveSolrinoNowPlayingOverlay()
{
    if (auto overlay = overlayInstance())
    {
        overlay->startMoving();
    }
}

void resetSolrinoNowPlayingOverlayPlace()
{
    getSettings()->solrinoNowPlayingPlace = QString();
    getSettings()->requestSave();
    if (auto overlay = overlayInstance())
    {
        overlay->placeFromSettings();
    }
}

void startSolrinoNowPlayingOverlay()
{
    static pajlada::Signals::SignalHolder holder;
    auto &overlay = overlayInstance();
    if (overlay != nullptr)
    {
        return;
    }
    overlay = new NowPlayingOverlay();

    const auto refresh = [] {
        SolrinoNowPlaying::instance().setActive(
            getSettings()->solrinoNowPlaying.getValue());
        if (auto current = overlayInstance())
        {
            current->refresh();
        }
    };
    holder.managedConnect(SolrinoNowPlaying::instance().changed, [] {
        if (auto current = overlayInstance())
        {
            current->songMaybeChanged();
        }
    });
    auto *s = getSettings();
    for (auto *setting :
         {&s->solrinoNowPlaying, &s->solrinoNowPlayingProgress,
          &s->solrinoNowPlayingPrevious, &s->solrinoNowPlayingPlayPause,
          &s->solrinoNowPlayingNext, &s->solrinoNowPlayingShuffle,
          &s->solrinoNowPlayingRepeat, &s->solrinoNowPlayingHideWhenPaused,
          &s->solrinoNowPlayingAnyPlayer, &s->solrinoNowPlayingTimes,
          &s->solrinoNowPlayingScroll, &s->solrinoNowPlayingVisualiser,
          &s->solrinoNowPlayingSlideIn, &s->solrinoNowPlayingVolumeScroll})
    {
        setting->connect(
            [refresh](const auto &, auto) {
                refresh();
            },
            holder, false);
    }
    for (auto *setting :
         {&s->solrinoNowPlayingLayout, &s->solrinoNowPlayingStyle,
          &s->solrinoNowPlayingSize, &s->solrinoNowPlayingOpacity,
          &s->solrinoNowPlayingShow, &s->solrinoNowPlayingAccent,
          &s->solrinoNowPlayingCoverAnim, &s->solrinoNowPlayingCorners,
          &s->solrinoNowPlayingBorder})
    {
        setting->connect(
            [refresh](const auto &, auto) {
                refresh();
            },
            holder, false);
    }
    refresh();
}

}  // namespace chatterino

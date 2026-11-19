#include "singletons/SeasonalDecorations.hpp"

#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintDevice>
#include <QPixmap>
#include <QPolygonF>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace chatterino {

namespace {

using namespace Qt::Literals::StringLiterals;

constexpr qreal PI = std::numbers::pi;

/// Same seed every paint, so the snow and embers do not jump around when a
/// chat repaints at the same size
class Dice
{
public:
    explicit Dice(quint32 seed)
        : gen_(seed)
    {
    }

    qreal operator()(qreal low, qreal high)
    {
        return low + (this->gen_.generateDouble() * (high - low));
    }

private:
    QRandomGenerator gen_;
};

QColor rgba(int r, int g, int b, int a)
{
    return {r, g, b, std::clamp(a, 0, 255)};
}

void fillDot(QPainter &p, QPointF center, qreal radius, const QColor &color)
{
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(center, radius, radius);
}

void fillGlow(QPainter &p, QPointF center, qreal radius, const QColor &color)
{
    QRadialGradient glow(center, radius);
    glow.setColorAt(0, color);
    auto fade = color;
    fade.setAlpha(0);
    glow.setColorAt(1, fade);
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(center, radius, radius);
}

// ---------------------------------------------------------------- Halloween

void drawBat(QPainter &p, QPointF center, qreal width, qreal tilt,
             const QColor &color)
{
    // One wing in a 2-unit-wide box, mirrored for the other side
    QPainterPath wing;
    wing.moveTo(0, -0.16);
    wing.cubicTo(0.18, -0.30, 0.55, -0.44, 1.0, -0.24);
    wing.quadTo(0.86, -0.10, 0.80, 0.07);
    wing.quadTo(0.67, -0.05, 0.55, 0.03);
    wing.quadTo(0.42, -0.04, 0.31, 0.15);
    wing.quadTo(0.17, 0.03, 0.0, 0.12);
    wing.closeSubpath();

    QPainterPath ear;
    ear.moveTo(0.01, -0.22);
    ear.lineTo(0.06, -0.36);
    ear.lineTo(0.09, -0.20);
    ear.closeSubpath();

    QPainterPath body;
    body.addEllipse(QPointF(0, 0.02), 0.10, 0.17);
    body.addEllipse(QPointF(0, -0.17), 0.08, 0.08);

    const QTransform mirror = QTransform::fromScale(-1, 1);
    auto bat = wing.united(mirror.map(wing))
                   .united(body)
                   .united(ear)
                   .united(mirror.map(ear));

    p.save();
    p.translate(center);
    p.rotate(tilt);
    p.scale(width / 2, width / 2);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(bat);
    p.restore();
}

void drawPumpkin(QPainter &p, QPointF bottom, qreal r)
{
    const QPointF c(bottom.x(), bottom.y() - (r * 0.85));

    fillGlow(p, c, r * 2.7, rgba(255, 128, 28, 58));

    QRadialGradient skin(c + QPointF(-r * 0.2, -r * 0.35), r * 1.5);
    skin.setColorAt(0, rgba(255, 170, 64, 215));
    skin.setColorAt(1, rgba(196, 78, 12, 215));

    p.setPen(QPen(rgba(120, 44, 8, 150), r * 0.06));
    p.setBrush(skin);
    p.drawEllipse(c + QPointF(-r * 0.52, 0), r * 0.58, r * 0.84);
    p.drawEllipse(c + QPointF(r * 0.52, 0), r * 0.58, r * 0.84);
    p.drawEllipse(c, r * 0.6, r * 0.9);

    // Stem
    QPainterPath stem;
    stem.moveTo(c.x() - (r * 0.11), c.y() - (r * 0.82));
    stem.quadTo(c.x() - (r * 0.08), c.y() - (r * 1.12), c.x() + (r * 0.12),
                c.y() - (r * 1.22));
    stem.lineTo(c.x() + (r * 0.16), c.y() - (r * 1.08));
    stem.quadTo(c.x() + (r * 0.06), c.y() - (r * 1.0), c.x() + (r * 0.1),
                c.y() - (r * 0.82));
    stem.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(rgba(92, 110, 42, 230));
    p.drawPath(stem);

    // Carved face, lit from inside
    const auto at = [&](qreal x, qreal y) {
        return c + QPointF(x * r, y * r);
    };
    QPainterPath face;
    face.addPolygon(QPolygonF{at(-0.46, -0.04), at(-0.14, -0.04),
                              at(-0.30, -0.38)});
    face.closeSubpath();
    face.addPolygon(QPolygonF{at(0.14, -0.04), at(0.46, -0.04),
                              at(0.30, -0.38)});
    face.closeSubpath();
    face.addPolygon(QPolygonF{at(-0.07, 0.12), at(0.07, 0.12), at(0, 0.0)});
    face.closeSubpath();
    face.addPolygon(QPolygonF{
        at(-0.52, 0.22), at(-0.36, 0.30), at(-0.26, 0.22), at(-0.14, 0.32),
        at(0.0, 0.24), at(0.14, 0.32), at(0.26, 0.22), at(0.36, 0.30),
        at(0.52, 0.22), at(0.34, 0.52), at(0.18, 0.44), at(0.0, 0.56),
        at(-0.18, 0.44), at(-0.34, 0.52)});
    face.closeSubpath();
    p.setBrush(rgba(255, 222, 96, 235));
    p.drawPath(face);
}

void drawCobweb(QPainter &p, qreal length, qreal s, Dice &dice)
{
    // Spokes fan out from the top-left corner
    std::vector<qreal> angles;
    for (qreal a : {0.0, 14.0, 31.0, 49.0, 67.0, 90.0})
    {
        angles.push_back((a + dice(-3, 3)) * PI / 180);
    }
    angles.front() = 0;
    angles.back() = PI / 2;

    const auto point = [](qreal angle, qreal radius) {
        return QPointF(std::cos(angle) * radius, std::sin(angle) * radius);
    };

    QPainterPath spokes;
    for (auto angle : angles)
    {
        spokes.moveTo(0, 0);
        spokes.lineTo(point(angle, length));
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(rgba(220, 208, 236, 78), 0.9 * s));
    p.drawPath(spokes);

    // Sagging threads between the spokes
    QPainterPath threads;
    for (int ring = 1; ring <= 5; ++ring)
    {
        const qreal radius = length * ring / 5.6;
        threads.moveTo(point(angles[0], radius));
        for (size_t i = 1; i < angles.size(); ++i)
        {
            const auto from = point(angles[i - 1], radius);
            const auto to = point(angles[i], radius * dice(0.96, 1.04));
            threads.quadTo((from + to) / 2 * 0.80, to);
        }
    }
    p.setPen(QPen(rgba(220, 208, 236, 62), 0.8 * s));
    p.drawPath(threads);
}

void drawSpider(QPainter &p, qreal x, qreal hangTo, qreal s)
{
    p.setPen(QPen(rgba(220, 208, 236, 70), 0.8 * s));
    p.drawLine(QPointF(x, 0), QPointF(x, hangTo));

    const QPointF c(x, hangTo + (6 * s));
    p.setPen(QPen(rgba(82, 58, 98, 230), 1.1 * s, Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin));
    for (int side : {-1, 1})
    {
        for (int i = 0; i < 4; ++i)
        {
            const qreal dy = (i - 1.5) * 3 * s;
            const QPointF knee = c + QPointF(side * 7 * s, dy - (3.5 * s));
            const QPointF foot =
                c + QPointF(side * 11 * s, (dy * 1.7) + (3.5 * s));
            p.drawPolyline(QPolygonF{c, knee, foot});
        }
    }
    p.setPen(Qt::NoPen);
    p.setBrush(rgba(62, 42, 78, 240));
    p.drawEllipse(c, 4.5 * s, 5.5 * s);
    p.drawEllipse(c - QPointF(0, 6 * s), 3 * s, 3 * s);
    fillDot(p, c - QPointF(1.2 * s, 6.4 * s), 0.7 * s, rgba(255, 70, 60, 220));
    fillDot(p, c - QPointF(-1.2 * s, 6.4 * s), 0.7 * s, rgba(255, 70, 60, 220));
}

void drawGhost(QPainter &p, QPointF c, qreal height, int alpha)
{
    const qreal w = height * 0.72;
    const qreal top = -height / 2;
    const qreal bottom = height / 2;

    QPainterPath ghost;
    ghost.moveTo(-w / 2, top + (w / 2));
    ghost.arcTo(QRectF(-w / 2, top, w, w), 180, -180);
    ghost.lineTo(w / 2, bottom);
    const int waves = 3;
    const qreal step = w / waves;
    for (int i = 0; i < waves; ++i)
    {
        const qreal x = (w / 2) - (step * i);
        ghost.quadTo(x - (step / 2), bottom - (height * 0.16), x - step,
                     bottom);
    }
    ghost.closeSubpath();

    p.save();
    p.translate(c);
    p.rotate(-8);
    p.setPen(Qt::NoPen);
    p.setBrush(rgba(240, 232, 255, alpha));
    p.drawPath(ghost);
    p.setBrush(rgba(18, 8, 26, alpha * 3));
    p.drawEllipse(QPointF(-w * 0.17, top + (w * 0.45)), w * 0.08, w * 0.12);
    p.drawEllipse(QPointF(w * 0.17, top + (w * 0.45)), w * 0.08, w * 0.12);
    p.drawEllipse(QPointF(0, top + (w * 0.75)), w * 0.07, w * 0.1);
    p.restore();
}

void drawTombstone(QPainter &p, qreal x, qreal base, qreal s)
{
    const qreal w = 17 * s;
    const qreal h = 25 * s;
    QPainterPath stone;
    stone.moveTo(x - (w / 2), base);
    stone.lineTo(x - (w / 2), base - h + (w / 2));
    stone.arcTo(QRectF(x - (w / 2), base - h, w, w), 180, -180);
    stone.lineTo(x + (w / 2), base);
    stone.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(rgba(50, 40, 64, 235));
    p.drawPath(stone);

    p.setPen(QPen(rgba(24, 16, 32, 230), 1.4 * s));
    p.drawLine(QPointF(x, base - (h * 0.78)), QPointF(x, base - (h * 0.32)));
    p.drawLine(QPointF(x - (w * 0.22), base - (h * 0.64)),
               QPointF(x + (w * 0.22), base - (h * 0.64)));
}

void paintHalloween(QPainter &p, qreal W, qreal H, qreal s)
{
    Dice dice(1031);

    // Purple night above, a pumpkin glow rising from the ground
    QLinearGradient haze(0, 0, 0, H);
    haze.setColorAt(0, rgba(96, 32, 140, 38));
    haze.setColorAt(0.42, rgba(96, 32, 140, 0));
    haze.setColorAt(0.72, rgba(255, 100, 20, 0));
    haze.setColorAt(1, rgba(255, 100, 20, 30));
    p.fillRect(QRectF(0, 0, W, H), haze);

    // Embers drifting up from the bottom
    for (int i = 0; i < 26; ++i)
    {
        fillDot(p, {dice(0, W), H - dice(0, H * 0.5)}, dice(0.7, 1.8) * s,
                rgba(255, int(dice(110, 170)), 40, int(dice(22, 75))));
    }

    // Full moon in the top right, with a cloud across it
    const qreal moonR = std::min(26 * s, W * 0.12);
    const QPointF moon(W - (moonR + (22 * s)), moonR + (20 * s));
    fillGlow(p, moon, moonR * 3.4, rgba(255, 214, 140, 46));
    QRadialGradient moonFace(moon - QPointF(moonR * 0.3, moonR * 0.3),
                             moonR * 1.4);
    moonFace.setColorAt(0, rgba(255, 244, 206, 185));
    moonFace.setColorAt(1, rgba(232, 200, 132, 160));
    p.setPen(Qt::NoPen);
    p.setBrush(moonFace);
    p.drawEllipse(moon, moonR, moonR);
    p.setBrush(rgba(196, 162, 104, 62));
    p.drawEllipse(moon + QPointF(-moonR * 0.35, -moonR * 0.2), moonR * 0.22,
                  moonR * 0.2);
    p.drawEllipse(moon + QPointF(moonR * 0.3, moonR * 0.32), moonR * 0.15,
                  moonR * 0.14);
    p.drawEllipse(moon + QPointF(moonR * 0.1, -moonR * 0.45), moonR * 0.1,
                  moonR * 0.1);

    QPainterPath cloud;
    const QPointF cloudAt = moon + QPointF(-moonR * 0.2, moonR * 0.55);
    cloud.addEllipse(cloudAt, moonR * 1.5, moonR * 0.16);
    cloud.addEllipse(cloudAt + QPointF(moonR * 0.5, -moonR * 0.12),
                     moonR * 0.8, moonR * 0.18);
    cloud.setFillRule(Qt::WindingFill);
    p.setBrush(rgba(30, 16, 42, 150));
    p.drawPath(cloud.simplified());

    // Bats flying out past the moon
    drawBat(p, moon + QPointF(-moonR * 0.2, -moonR * 0.05), moonR * 1.25, -8,
            rgba(12, 5, 16, 240));
    drawBat(p, moon + QPointF(-moonR * 2.6, moonR * 1.1), moonR * 0.9, 14,
            rgba(18, 8, 24, 225));
    drawBat(p, moon + QPointF(-moonR * 1.6, -moonR * 0.55), moonR * 0.7, -16,
            rgba(18, 8, 24, 225));
    if (W > 300 * s)
    {
        drawBat(p, moon + QPointF(-moonR * 4.4, moonR * 0.4), moonR * 0.6, 6,
                rgba(24, 12, 32, 210));
    }

    // Cobweb and a dangling spider in the top left
    const qreal webLength = std::min({110 * s, W * 0.38, H * 0.38});
    drawCobweb(p, webLength, s, dice);
    if (H > 180 * s && W > 160 * s)
    {
        drawSpider(p, webLength * 0.62, webLength * 1.05, s);
    }

    // A shy ghost floating along the left edge
    if (H > 260 * s)
    {
        drawGhost(p, {std::max(22 * s, W * 0.08), H * 0.52}, 34 * s, 20);
    }
    if (H > 260 * s && W > 380 * s)
    {
        drawGhost(p, {W - (30 * s), H * 0.4}, 26 * s, 14);
    }

    // Graveyard hill along the bottom
    const qreal ground = H - (18 * s);
    if (W > 260 * s)
    {
        drawTombstone(p, W * 0.34, ground - (3 * s), s);
        drawTombstone(p, W * 0.66, ground - (2 * s), s * 0.85);
    }
    QPainterPath hill;
    hill.moveTo(0, H);
    hill.lineTo(0, ground - (8 * s));
    hill.cubicTo(W * 0.15, ground - (18 * s), W * 0.30, ground + (2 * s),
                 W * 0.50, ground - (6 * s));
    hill.cubicTo(W * 0.68, ground - (14 * s), W * 0.82, ground + (4 * s), W,
                 ground - (12 * s));
    hill.lineTo(W, H);
    hill.closeSubpath();
    QLinearGradient hillFill(0, ground - (18 * s), 0, H);
    hillFill.setColorAt(0, rgba(20, 10, 28, 238));
    hillFill.setColorAt(1, rgba(7, 3, 10, 248));
    p.setPen(Qt::NoPen);
    p.setBrush(hillFill);
    p.drawPath(hill);

    // Jack-o'-lanterns on the hill
    drawPumpkin(p, {26 * s, H - (6 * s)}, 13 * s);
    if (W > 200 * s)
    {
        drawPumpkin(p, {54 * s, H - (4 * s)}, 8.5 * s);
        drawPumpkin(p, {W - (32 * s), H - (5 * s)}, 11 * s);
    }
}

// ------------------------------------------------------------------- Winter

void drawPine(QPainter &p, QPointF base, qreal h)
{
    p.setPen(Qt::NoPen);
    p.setBrush(rgba(34, 24, 20, 235));
    p.drawRect(QRectF(base.x() - (h * 0.05), base.y() - (h * 0.14), h * 0.1,
                      h * 0.14));

    for (int tier = 0; tier < 3; ++tier)
    {
        const qreal top = base.y() - h + (tier * h * 0.24);
        const qreal bottom = top + (h * 0.42);
        const qreal half = h * (0.17 + (tier * 0.08));

        QPainterPath branches;
        branches.moveTo(base.x(), top);
        branches.lineTo(base.x() + half, bottom);
        branches.lineTo(base.x() - half, bottom);
        branches.closeSubpath();
        p.setBrush(rgba(22, 50, 58, 245));
        p.drawPath(branches);

        // Snow resting on each layer
        QPainterPath snow;
        snow.moveTo(base.x() - half, bottom);
        snow.quadTo(base.x(), bottom - (h * 0.11), base.x() + half, bottom);
        snow.quadTo(base.x(), bottom - (h * 0.035), base.x() - half, bottom);
        snow.closeSubpath();
        p.setBrush(rgba(214, 232, 250, 175));
        p.drawPath(snow);
    }

    QPainterPath cap;
    const qreal top = base.y() - h;
    cap.moveTo(base.x(), top);
    cap.lineTo(base.x() + (h * 0.06), top + (h * 0.11));
    cap.quadTo(base.x(), top + (h * 0.08), base.x() - (h * 0.06),
               top + (h * 0.11));
    cap.closeSubpath();
    p.setBrush(rgba(230, 242, 255, 200));
    p.drawPath(cap);
}

void drawSnowflake(QPainter &p, QPointF c, qreal size, qreal turn, int alpha)
{
    p.save();
    p.translate(c);
    p.rotate(turn);
    p.setPen(QPen(rgba(225, 240, 255, alpha), size * 0.11, Qt::SolidLine,
                  Qt::RoundCap));
    for (int arm = 0; arm < 6; ++arm)
    {
        p.drawLine(QPointF(0, 0), QPointF(0, -size));
        for (qreal along : {0.45, 0.72})
        {
            const qreal branch = size * (along < 0.5 ? 0.34 : 0.22);
            p.drawLine(QPointF(0, -size * along),
                       QPointF(branch * 0.7, (-size * along) - branch * 0.7));
            p.drawLine(QPointF(0, -size * along),
                       QPointF(-branch * 0.7, (-size * along) - branch * 0.7));
        }
        p.rotate(60);
    }
    p.restore();
}

void drawAurora(QPainter &p, qreal W, qreal centerY, qreal amplitude,
                qreal thickness, qreal phase, const QColor &color)
{
    QPolygonF ribbon;
    const int steps = 48;
    for (int i = 0; i <= steps; ++i)
    {
        const qreal x = W * i / steps;
        ribbon << QPointF(
            x, centerY + (amplitude * std::sin((i * 2.2 * PI / steps) + phase)));
    }
    for (int i = steps; i >= 0; --i)
    {
        const qreal x = W * i / steps;
        ribbon << QPointF(x, centerY + thickness +
                                 (amplitude * 0.6 *
                                  std::sin((i * 2.2 * PI / steps) + phase + 0.8)));
    }

    QLinearGradient fill(0, centerY - amplitude, 0,
                         centerY + thickness + amplitude);
    auto clear = color;
    clear.setAlpha(0);
    fill.setColorAt(0, clear);
    fill.setColorAt(0.35, color);
    fill.setColorAt(1, clear);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPolygon(ribbon);
}

void paintWinter(QPainter &p, qreal W, qreal H, qreal s)
{
    Dice dice(1225);

    QLinearGradient haze(0, 0, 0, H);
    haze.setColorAt(0, rgba(40, 90, 160, 34));
    haze.setColorAt(0.4, rgba(40, 90, 160, 0));
    haze.setColorAt(1, rgba(150, 200, 255, 22));
    p.fillRect(QRectF(0, 0, W, H), haze);

    // Northern lights and stars
    drawAurora(p, W, 30 * s, 9 * s, 34 * s, 0.4, rgba(90, 255, 190, 40));
    drawAurora(p, W, 46 * s, 12 * s, 30 * s, 2.1, rgba(150, 120, 255, 26));
    for (int i = 0; i < 34; ++i)
    {
        fillDot(p, {dice(0, W), dice(10 * s, H * 0.38)}, dice(0.5, 1.15) * s,
                rgba(232, 242, 255, int(dice(40, 130))));
    }

    // Falling snow over the whole chat
    const int flakes =
        int(std::clamp(W * H / (2300 * s * s), qreal(30), qreal(260)));
    for (int i = 0; i < flakes; ++i)
    {
        fillDot(p, {dice(0, W), dice(0, H)}, dice(0.7, 2.0) * s,
                rgba(236, 246, 255, int(dice(24, 95))));
    }

    // Big crystal flakes drifting down the sides
    const int crystals = H > 300 * s ? 6 : 3;
    for (int i = 0; i < crystals; ++i)
    {
        const bool left = i % 2 == 0;
        const qreal x = left ? dice(10 * s, 38 * s) : W - dice(10 * s, 38 * s);
        drawSnowflake(p, {x, dice(H * 0.16, H * 0.78)}, dice(6, 11) * s,
                      dice(0, 60), int(dice(55, 95)));
    }

    // Snow piled on the top edge, with icicles hanging from it
    QPainterPath snowCap;
    snowCap.moveTo(0, 0);
    snowCap.lineTo(W, 0);
    snowCap.lineTo(W, 4 * s);
    for (qreal x = W; x > 0; x -= 18 * s)
    {
        snowCap.quadTo(x - (9 * s), dice(6.5, 8.5) * s, x - (18 * s),
                       dice(3.5, 5) * s);
    }
    snowCap.lineTo(0, 0);
    snowCap.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(rgba(218, 234, 250, 150));
    p.drawPath(snowCap);

    for (qreal x = dice(2, 8) * s; x < W - (4 * s); x += dice(7, 16) * s)
    {
        const qreal width = dice(3, 6) * s;
        qreal length = dice(5, 20) * s;
        if (dice(0, 1) > 0.85)
        {
            length = dice(24, 34) * s;
        }
        const qreal top = 3.5 * s;
        QPainterPath icicle;
        icicle.moveTo(x, top);
        icicle.lineTo(x + width, top);
        icicle.lineTo(x + (width * 0.5) + (dice(-0.6, 0.6) * s), top + length);
        icicle.closeSubpath();
        QLinearGradient ice(0, top, 0, top + length);
        ice.setColorAt(0, rgba(228, 242, 255, 175));
        ice.setColorAt(1, rgba(160, 206, 242, 40));
        p.setBrush(ice);
        p.drawPath(icicle);
    }

    // Snow drifts and pine trees along the bottom
    QPainterPath backDrift;
    backDrift.moveTo(0, H);
    backDrift.lineTo(0, H - (26 * s));
    backDrift.cubicTo(W * 0.25, H - (40 * s), W * 0.45, H - (14 * s),
                      W * 0.62, H - (24 * s));
    backDrift.cubicTo(W * 0.80, H - (34 * s), W * 0.92, H - (20 * s), W,
                      H - (30 * s));
    backDrift.lineTo(W, H);
    backDrift.closeSubpath();
    p.setBrush(rgba(30, 46, 66, 215));
    p.drawPath(backDrift);

    drawPine(p, {24 * s, H - (6 * s)}, 78 * s);
    if (W > 220 * s)
    {
        drawPine(p, {54 * s, H - (4 * s)}, 50 * s);
    }
    if (W > 150 * s)
    {
        drawPine(p, {W - (28 * s), H - (6 * s)}, 68 * s);
    }

    QPainterPath frontEdge;
    frontEdge.moveTo(0, H - (14 * s));
    frontEdge.cubicTo(W * 0.20, H - (6 * s), W * 0.38, H - (22 * s), W * 0.55,
                      H - (12 * s));
    frontEdge.cubicTo(W * 0.72, H - (4 * s), W * 0.86, H - (20 * s), W,
                      H - (10 * s));
    QPainterPath frontDrift = frontEdge;
    frontDrift.lineTo(W, H);
    frontDrift.lineTo(0, H);
    frontDrift.closeSubpath();
    QLinearGradient driftFill(0, H - (22 * s), 0, H);
    driftFill.setColorAt(0, rgba(54, 80, 108, 238));
    driftFill.setColorAt(1, rgba(36, 54, 76, 242));
    p.setBrush(driftFill);
    p.drawPath(frontDrift);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(rgba(204, 230, 252, 100), 1.3 * s));
    p.drawPath(frontEdge);
}

}  // namespace

void paintSeasonalDecorations(QPainter &painter, const QRect &rect,
                              const QString &season, qreal scale)
{
    const bool halloween = season == u"season-halloween"_s;
    const bool winter = season == u"season-winter"_s;
    if ((!halloween && !winter) || rect.width() < 40 || rect.height() < 40)
    {
        return;
    }

    const qreal s = std::clamp(scale, 0.5, 3.0);
    const qreal dpr =
        painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1;

    // Chats repaint often, so draw each size once and reuse it
    static QHash<QString, QPixmap> cache;
    const auto key = u"%1:%2x%3:%4:%5"_s.arg(season)
                         .arg(rect.width())
                         .arg(rect.height())
                         .arg(dpr)
                         .arg(s);
    auto it = cache.find(key);
    if (it == cache.end())
    {
        if (cache.size() > 16)
        {
            cache.clear();
        }
        QPixmap art(rect.size() * dpr);
        art.setDevicePixelRatio(dpr);
        art.fill(Qt::transparent);
        {
            QPainter p(&art);
            p.setRenderHint(QPainter::Antialiasing, true);
            if (halloween)
            {
                paintHalloween(p, rect.width(), rect.height(), s);
            }
            else
            {
                paintWinter(p, rect.width(), rect.height(), s);
            }
        }
        it = cache.insert(key, art);
    }

    painter.drawPixmap(rect.topLeft(), *it);
}

}  // namespace chatterino

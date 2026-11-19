#include "providers/moltorino/SolrinoPaints.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/SolrinoVersion.hpp"
#include "singletons/Settings.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QLinearGradient>
#include <QLineF>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numbers>

namespace chatterino {

namespace {

using namespace Qt::Literals::StringLiterals;

constexpr auto PAINT_ENDPOINT = SOLRINO_BADGE_SERVER "/v2/paints/me";
constexpr int ANIMATED_SAMPLES = 40;
/// Animated paints draw a new frame this often. Chats redraw every 20ms, so
/// every other redraw reuses the same picture instead of making a new one.
constexpr qint64 FRAME_MS = 33;

/// Seconds into the current hour, rounded to the start of the current frame
qreal animationSeconds()
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    return qreal((now - (now % FRAME_MS)) % 3600000) / 1000;
}

qreal fract(qreal value)
{
    return value - std::floor(value);
}

QColor mix(const QColor &a, const QColor &b, qreal amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    return QColor::fromRgbF(
        float(a.redF() + ((b.redF() - a.redF()) * amount)),
        float(a.greenF() + ((b.greenF() - a.greenF()) * amount)),
        float(a.blueF() + ((b.blueF() - a.blueF()) * amount)),
        float(a.alphaF() + ((b.alphaF() - a.alphaF()) * amount)));
}

std::vector<QColor> colors(std::initializer_list<const char *> names)
{
    std::vector<QColor> out;
    for (const auto *name : names)
    {
        out.emplace_back(QLatin1String(name));
    }
    return out;
}

/// Even blend across a list of colors, from 0 to 1
QColor blend(const std::vector<QColor> &palette, qreal position)
{
    if (palette.size() == 1)
    {
        return palette.front();
    }
    position = std::clamp(position, 0.0, 1.0) * qreal(palette.size() - 1);
    const auto index = std::min<size_t>(size_t(position), palette.size() - 2);
    return mix(palette[index], palette[index + 1], position - qreal(index));
}

/// Wraps around, so the last color flows back into the first
QColor blendLoop(const std::vector<QColor> &palette, qreal position)
{
    position = fract(position) * qreal(palette.size());
    const auto index = size_t(position) % palette.size();
    return mix(palette[index], palette[(index + 1) % palette.size()],
               position - std::floor(position));
}

/// A bright band sweeping across the name every `period` seconds
qreal sheen(qreal position, qreal seconds, qreal period)
{
    const qreal center = (fract(seconds / period) * 1.8) - 0.4;
    const qreal distance = std::abs(position - center) / 0.14;
    return distance >= 1 ? 0 : std::pow(1 - distance, 1.5) * 0.85;
}

/// Same number for the same input, spread evenly between 0 and 1
qreal hash01(qreal seed)
{
    return fract(std::sin(seed * 12.9898) * 43758.5453);
}

/// Little twinkles that pop up at random spots along the name
qreal sparkle(qreal position, qreal seconds, qreal density = 14)
{
    const qreal cell = std::floor(position * density);
    const qreal phase = hash01(cell + 7.0) * 6.2832;
    const qreal speed = 1.6 + (hash01(cell + 31.0) * 2.4);
    const qreal twinkle = std::max(0.0, std::sin((seconds * speed) + phase));
    const qreal center = std::abs(fract(position * density) - 0.5) * 2;
    return std::pow(twinkle, 10) * (1 - center);
}

/// Brightness rolling along the name like a wave
qreal wave(qreal position, qreal seconds, qreal waves = 2, qreal speed = 3)
{
    return 0.5 + (0.5 * std::sin((position * waves * 6.2832) - (seconds * speed)));
}

/// Random bright flashes, like lightning
qreal flash(qreal seconds, qreal seed = 0)
{
    const qreal slot = std::floor(seconds * 6);
    if (hash01(slot + seed) < 0.9)
    {
        return 0;
    }
    return 1 - fract(seconds * 6);
}

/// Goes 0 to 1 and back again every `period` seconds, easing at each end
qreal pingpong(qreal seconds, qreal period)
{
    return 0.5 - (0.5 * std::cos(seconds / period * 6.2832));
}

/// Smooth, wandering 0 to 1 pattern for organic looks like oil and smoke
qreal swirl(qreal position, qreal seconds)
{
    const qreal value = std::sin((position * 7.3) + (seconds * 1.1)) +
                        std::sin((position * 3.1) - (seconds * 0.7) + 1.7) +
                        std::sin((position * 11.7) + (seconds * 0.45) + 4.2);
    return (value + 3) / 6;
}

/// A soft hump of light centered on `center`, `width` wide
qreal bump(qreal position, qreal center, qreal width)
{
    const qreal distance = std::abs(position - center) / width;
    return distance >= 1 ? 0 : 0.5 + (0.5 * std::cos(distance * 3.14159));
}

PaintDropShadow glow(const char *color, float radius = 4)
{
    return {0, 0, radius, QColor(QLatin1String(color))};
}

std::vector<std::shared_ptr<SolrinoPaint>> buildCatalog()
{
    using Style = SolrinoPaint::Style;
    std::vector<Style> styles;

    const auto gradient = [](std::vector<QColor> palette) {
        return [palette](qreal x, qreal) {
            return blend(palette, x);
        };
    };

    // ---- Still paints

    styles.push_back({
        .id = u"sunset"_s,
        .name = u"Sunset"_s,
        .description = u"Golden hour fading into a violet sky."_s,
        .angle = 0,
        .color = gradient(colors({"#ffb347", "#ff6a5c", "#ff4f9a", "#b44cff"})),
        .shadows = {glow("#80ff5e7e", 3)},
    });
    styles.push_back({
        .id = u"deep_ocean"_s,
        .name = u"Deep Ocean"_s,
        .description = u"Shallow turquoise sinking into deep blue."_s,
        .angle = 20,
        .color = gradient(colors({"#5ef7e0", "#2fb4ff", "#3d6bff", "#6a4bff"})),
    });
    styles.push_back({
        .id = u"rose_gold"_s,
        .name = u"Rose Gold"_s,
        .description = u"Polished metal with a soft pink shine."_s,
        .angle = 90,
        .color = gradient(colors(
            {"#f9dcd4", "#d99a8f", "#b76e79", "#f6c9bb", "#a35d68"})),
        .shadows = {PaintDropShadow(0, 1, 1, QColor(40, 10, 15, 150))},
    });
    styles.push_back({
        .id = u"chrome"_s,
        .name = u"Chrome"_s,
        .description = u"Mirror-bright silver, like polished steel."_s,
        .angle = 90,
        .color = gradient(colors(
            {"#ffffff", "#c9d3da", "#7d8b96", "#eef3f6", "#9aa7b2"})),
        .shadows = {PaintDropShadow(0, 1, 1, QColor(0, 0, 0, 170))},
    });
    styles.push_back({
        .id = u"toxic"_s,
        .name = u"Toxic"_s,
        .description = u"Radioactive green that glows in the dark."_s,
        .angle = 0,
        .color = gradient(colors({"#e4ff3a", "#5dff6a", "#18e8a0"})),
        .shadows = {glow("#b03dff7a", 5)},
    });
    styles.push_back({
        .id = u"candy_cane"_s,
        .name = u"Candy Cane"_s,
        .description = u"Red and white peppermint stripes."_s,
        .angle = 45,
        .stripes = {{0.0, QColor("#ff3048")},
                    {0.5, QColor("#ff3048")},
                    {0.5, QColor("#fff4f4")},
                    {1.0, QColor("#fff4f4")}},
        .stripeWidth = 0.3,
        .shadows = {PaintDropShadow(0, 1, 1, QColor(60, 0, 10, 170))},
    });
    styles.push_back({
        .id = u"galaxy"_s,
        .name = u"Galaxy"_s,
        .description = u"Pink and violet nebula with a starry glow."_s,
        .angle = 30,
        .color = gradient(
            colors({"#ff9ce0", "#b06bff", "#5a5cff", "#47b8ff", "#e0b3ff"})),
        .shadows = {glow("#a08a5cff", 4)},
    });
    styles.push_back({
        .id = u"cotton_candy"_s,
        .name = u"Cotton Candy"_s,
        .description = u"Soft pink and baby blue."_s,
        .angle = 0,
        .color = gradient(colors({"#ff9ad5", "#ffc8ea", "#b9dcff", "#8fc8ff"})),
    });
    styles.push_back({
        .id = u"blood_moon"_s,
        .name = u"Blood Moon"_s,
        .description = u"Deep crimson with an eerie red glow."_s,
        .angle = 90,
        .color = gradient(colors({"#ff6b4a", "#e01e37", "#9e0d2a"})),
        .shadows = {glow("#c0ff1e37", 4)},
    });

    // ---- Animated paints

    styles.push_back({
        .id = u"rainbow_flow"_s,
        .name = u"Rainbow Flow"_s,
        .description = u"Every color of the rainbow, always moving."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                return QColor::fromHsvF(float(fract((x * 0.8) - (t * 0.35))),
                                        0.72F, 1.0F);
            },
    });
    styles.push_back({
        .id = u"aurora"_s,
        .name = u"Aurora"_s,
        .description = u"Northern lights drifting across your name."_s,
        .animated = true,
        .color =
            [palette = colors({"#4dffb5", "#3ac8ff", "#a77bff", "#3ac8ff"})](
                qreal x, qreal t) {
                return blendLoop(palette, (x * 0.7) - (t * 0.12));
            },
        .shadows = {glow("#703ac8ff", 4)},
    });
    styles.push_back({
        .id = u"ember"_s,
        .name = u"Ember"_s,
        .description = u"Flickering fire with a hot orange glow."_s,
        .animated = true,
        .color =
            [palette = colors({"#ffe066", "#ff8a1f", "#ff3d1f", "#ff8a1f"})](
                qreal x, qreal t) {
                const auto base = blendLoop(palette, (x * 0.9) + (t * 0.45));
                const qreal flicker = 0.5 + (0.5 * std::sin((t * 9) + (x * 7)));
                return mix(base, QColor("#fff3b0"), flicker * 0.22);
            },
        .shadows = {glow("#c0ff6a00", 5)},
    });
    styles.push_back({
        .id = u"gold_shimmer"_s,
        .name = u"Gold Shimmer"_s,
        .description = u"Solid gold with a shine that sweeps across."_s,
        .animated = true,
        .color =
            [palette = colors({"#ffe9a8", "#e8b73e", "#b8862a", "#f7d774"})](
                qreal x, qreal t) {
                return mix(blend(palette, x), QColor(Qt::white),
                           sheen(x, t, 2.6));
            },
        .shadows = {PaintDropShadow(0, 1, 1, QColor(60, 35, 0, 170))},
    });
    styles.push_back({
        .id = u"neon_pulse"_s,
        .name = u"Neon Pulse"_s,
        .description = u"Pink and cyan neon that breathes in and out."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal swap = 0.5 + (0.5 * std::sin(t * 2.2));
                const auto base = mix(QColor("#ff3df2"), QColor("#2ef2ff"),
                                      std::clamp(x + (swap - 0.5), 0.0, 1.0));
                const qreal pulse = 0.5 + (0.5 * std::sin(t * 4.4));
                return mix(base, QColor(Qt::white), pulse * 0.28);
            },
        .shadows = {glow("#b0ff3df2", 5)},
    });
    styles.push_back({
        .id = u"frostbite"_s,
        .name = u"Frostbite"_s,
        .description = u"Icy blue with frost glinting across it."_s,
        .animated = true,
        .color =
            [palette = colors({"#e9f8ff", "#9fdcff", "#5fb2ff", "#c8ecff"})](
                qreal x, qreal t) {
                return mix(blend(palette, x), QColor(Qt::white),
                           sheen(x, t + 1.1, 3.2));
            },
        .shadows = {glow("#905fb2ff", 3)},
    });
    styles.push_back({
        .id = u"witching_hour"_s,
        .name = u"Witching Hour"_s,
        .description = u"Potion purple, slime green and pumpkin orange."_s,
        .animated = true,
        .color =
            [palette = colors({"#b05cff", "#6dff5c", "#ff8a1f", "#6dff5c"})](
                qreal x, qreal t) {
                return blendLoop(palette, (x * 0.6) - (t * 0.18));
            },
        .shadows = {glow("#90b05cff", 4)},
    });

    // ---- More paints, built from a few shared looks

    using ColorAt = SolrinoPaint::ColorAt;
    const auto flow = [](std::vector<QColor> palette, qreal speed,
                         qreal span = 0.7) -> ColorAt {
        return [palette, speed, span](qreal x, qreal t) {
            return blendLoop(palette, (x * span) - (t * speed));
        };
    };
    const auto glint = [](std::vector<QColor> palette, QColor shine,
                          qreal period, qreal offset = 0) -> ColorAt {
        return [palette, shine, period, offset](qreal x, qreal t) {
            return mix(blend(palette, x), shine, sheen(x, t + offset, period));
        };
    };
    const auto twinkle = [](ColorAt base, QColor star,
                            qreal density = 14) -> ColorAt {
        return [base, star, density](qreal x, qreal t) {
            return mix(base(x, t), star, sparkle(x, t, density));
        };
    };
    const auto split = [](const char *top, const char *bottom) -> ColorAt {
        return [top = QColor(QLatin1String(top)),
                bottom = QColor(QLatin1String(bottom))](qreal x, qreal) {
            return x < 0.5 ? top : bottom;
        };
    };
    const auto bands = [](std::initializer_list<const char *> names) {
        QGradientStops stops;
        const auto count = qreal(names.size());
        qreal i = 0;
        for (const auto *name : names)
        {
            const QColor color{QLatin1String(name)};
            stops.append({i / count, color});
            stops.append({(i + 1) / count, color});
            ++i;
        }
        return stops;
    };
    const auto shade = [](int r, int g, int b, int a = 170) {
        return PaintDropShadow(0, 1, 1, QColor(r, g, b, a));
    };

    // Still

    styles.push_back({
        .id = u"peach_fuzz"_s,
        .name = u"Peach Fuzz"_s,
        .description = u"Soft peach melting into coral."_s,
        .angle = 15,
        .color = gradient(colors({"#ffe0b8", "#ffb08a", "#ff7a8a"})),
    });
    styles.push_back({
        .id = u"lavender_haze"_s,
        .name = u"Lavender Haze"_s,
        .description = u"Dreamy lilac drifting into sky blue."_s,
        .angle = 0,
        .color = gradient(colors({"#f0d4ff", "#c3a6ff", "#94c8ff"})),
    });
    styles.push_back({
        .id = u"matcha_latte"_s,
        .name = u"Matcha Latte"_s,
        .description = u"Creamy green tea, frothed on top."_s,
        .angle = 90,
        .color = gradient(colors({"#f4ffd6", "#bfe88a", "#7fc96b"})),
    });
    styles.push_back({
        .id = u"pink_lemonade"_s,
        .name = u"Pink Lemonade"_s,
        .description = u"Sour lemon swirled with sweet pink."_s,
        .angle = 0,
        .color = gradient(colors({"#fff275", "#ffd166", "#ff8fab", "#ff5c9a"})),
    });
    styles.push_back({
        .id = u"tropical"_s,
        .name = u"Tropical"_s,
        .description = u"Mango, watermelon and lagoon blue."_s,
        .angle = 0,
        .color = gradient(colors({"#ffe66d", "#ff6b6b", "#4ecdc4"})),
    });
    styles.push_back({
        .id = u"mermaid"_s,
        .name = u"Mermaid"_s,
        .description = u"Scales of aqua, mint and violet."_s,
        .angle = 35,
        .color = gradient(colors({"#7afcff", "#5ee7b7", "#b388ff", "#ff9ff3"})),
        .shadows = {glow("#605ee7b7", 3)},
    });
    styles.push_back({
        .id = u"dragonfruit"_s,
        .name = u"Dragonfruit"_s,
        .description = u"Hot magenta skin with a lime-green tip."_s,
        .angle = 0,
        .color = gradient(colors({"#ff2fb4", "#ff7ad9", "#c6ff6e"})),
    });
    styles.push_back({
        .id = u"sakura_petal"_s,
        .name = u"Sakura Petal"_s,
        .description = u"Cherry blossom pink fading to white."_s,
        .angle = 90,
        .color = gradient(colors({"#ffffff", "#ffd6e8", "#ff9ec7"})),
        .shadows = {shade(90, 20, 50)},
    });
    styles.push_back({
        .id = u"midnight_city"_s,
        .name = u"Midnight City"_s,
        .description = u"Streetlight blue and neon-sign pink."_s,
        .angle = 0,
        .color = gradient(colors({"#8ea7ff", "#6c63ff", "#ff6ec7"})),
        .shadows = {glow("#806c63ff", 3)},
    });
    styles.push_back({
        .id = u"vaporwave"_s,
        .name = u"Vaporwave"_s,
        .description = u"Retro pastel neon from a 90s mall."_s,
        .angle = 0,
        .color = gradient(
            colors({"#ff71ce", "#01cdfe", "#05ffa1", "#b967ff", "#fffb96"})),
    });
    styles.push_back({
        .id = u"bubblegum_split"_s,
        .name = u"Bubblegum Split"_s,
        .description = u"Pink on top, baby blue on the bottom."_s,
        .angle = 90,
        .color = split("#ff8fd0", "#8fd3ff"),
        .shadows = {shade(30, 10, 40)},
    });
    styles.push_back({
        .id = u"sunrise_split"_s,
        .name = u"Sunrise Split"_s,
        .description = u"Sun over the horizon: gold above, violet below."_s,
        .angle = 90,
        .color = split("#ffc04a", "#b07bff"),
        .shadows = {shade(40, 10, 30)},
    });
    styles.push_back({
        .id = u"holographic"_s,
        .name = u"Holographic"_s,
        .description = u"Pastel rainbow, like a holo trading card."_s,
        .angle = 45,
        .color = gradient(colors(
            {"#ffd1f7", "#c9b8ff", "#a8e8ff", "#b8ffd9", "#fff5b8", "#ffd1f7"})),
    });
    styles.push_back({
        .id = u"copper"_s,
        .name = u"Copper"_s,
        .description = u"Warm, polished copper."_s,
        .angle = 90,
        .color = gradient(colors({"#ffd8b0", "#e0955a", "#b87333", "#f3b989"})),
        .shadows = {shade(50, 20, 0)},
    });
    styles.push_back({
        .id = u"emerald"_s,
        .name = u"Emerald"_s,
        .description = u"A cut green gem catching the light."_s,
        .angle = 90,
        .color = gradient(colors({"#d4ffe8", "#3ee08f", "#1fae6b", "#a6f7cf"})),
        .shadows = {shade(0, 40, 20)},
    });
    styles.push_back({
        .id = u"sapphire"_s,
        .name = u"Sapphire"_s,
        .description = u"Deep, faceted royal blue."_s,
        .angle = 90,
        .color = gradient(colors({"#d6e6ff", "#6f95ff", "#3b5bdb", "#b3c9ff"})),
        .shadows = {shade(0, 10, 50)},
    });
    styles.push_back({
        .id = u"amethyst"_s,
        .name = u"Amethyst"_s,
        .description = u"Violet crystal with pale highlights."_s,
        .angle = 90,
        .color = gradient(colors({"#f1d9ff", "#c27bff", "#8f3ff0", "#e0b3ff"})),
        .shadows = {shade(30, 0, 50)},
    });
    styles.push_back({
        .id = u"ruby"_s,
        .name = u"Ruby"_s,
        .description = u"Blood-red gem with a pink shine."_s,
        .angle = 90,
        .color = gradient(colors({"#ffd6dc", "#ff4a68", "#c0143a", "#ff9aa9"})),
        .shadows = {shade(50, 0, 10)},
    });
    styles.push_back({
        .id = u"pearl"_s,
        .name = u"Pearl"_s,
        .description = u"Creamy white with a hint of every color."_s,
        .angle = 30,
        .color = gradient(
            colors({"#fff8fd", "#ffd3f1", "#cfe8ff", "#ffe2c4", "#e2d6ff"})),
        .shadows = {shade(40, 30, 50, 190)},
    });
    styles.push_back({
        .id = u"barbershop"_s,
        .name = u"Barbershop"_s,
        .description = u"Red, white and blue stripes."_s,
        .angle = 50,
        .stripes = bands({"#ff4057", "#ffffff", "#5b8cff"}),
        .stripeWidth = 0.4,
        .shadows = {shade(0, 0, 0)},
    });
    styles.push_back({
        .id = u"bumblebee"_s,
        .name = u"Bumblebee"_s,
        .description = u"Yellow and orange buzzing stripes."_s,
        .angle = 0,
        .stripes = bands({"#ffe14d", "#ff9500"}),
        .stripeWidth = 0.32,
        .shadows = {shade(40, 20, 0)},
    });
    styles.push_back({
        .id = u"watermelon"_s,
        .name = u"Watermelon"_s,
        .description = u"Juicy pink with a green rind."_s,
        .angle = 90,
        .color = gradient(colors({"#ff5c7a", "#ff7a93", "#ff9db0", "#7dff8a"})),
    });
    styles.push_back({
        .id = u"arctic"_s,
        .name = u"Arctic"_s,
        .description = u"Fresh snow fading into glacier blue."_s,
        .angle = 0,
        .color = gradient(colors({"#ffffff", "#c9f0ff", "#5ad1ff"})),
    });
    styles.push_back({
        .id = u"lava"_s,
        .name = u"Lava"_s,
        .description = u"White-hot on top, cooling to red below."_s,
        .angle = 90,
        .color = gradient(colors({"#fff36b", "#ff9a1f", "#ff3d00", "#c4002b"})),
        .shadows = {glow("#90ff3d00", 3)},
    });
    styles.push_back({
        .id = u"forest_spirit"_s,
        .name = u"Forest Spirit"_s,
        .description = u"Glowing leaves deep in an old forest."_s,
        .angle = 20,
        .color = gradient(colors({"#e0ffb8", "#7dff8a", "#3dd6a0"})),
        .shadows = {glow("#703dd6a0", 4)},
    });
    styles.push_back({
        .id = u"cherry_cola"_s,
        .name = u"Cherry Cola"_s,
        .description = u"Fizzy cherry red."_s,
        .angle = 0,
        .color = gradient(colors({"#ff9fb4", "#ff4d6d", "#d7263d"})),
    });

    // Animated

    styles.push_back({
        .id = u"glitter"_s,
        .name = u"Glitter"_s,
        .description = u"Gold covered in twinkling sparkles."_s,
        .animated = true,
        .color = twinkle(gradient(colors({"#ffe9a8", "#f2c14e", "#e09f2d"})),
                         QColor(Qt::white), 16),
        .shadows = {shade(60, 35, 0)},
    });
    styles.push_back({
        .id = u"starlight"_s,
        .name = u"Starlight"_s,
        .description = u"A night sky with stars blinking on and off."_s,
        .animated = true,
        .color = twinkle(gradient(colors({"#8fa8ff", "#a78bff", "#6fc3ff"})),
                         QColor("#fffbe0"), 18),
        .shadows = {glow("#806f8bff", 4)},
    });
    styles.push_back({
        .id = u"disco_ball"_s,
        .name = u"Disco Ball"_s,
        .description = u"Fast rainbow with flashing mirror glints."_s,
        .animated = true,
        .color = twinkle(
            [](qreal x, qreal t) {
                return QColor::fromHsvF(float(fract((x * 1.4) + (t * 0.9))),
                                        0.55F, 1.0F);
            },
            QColor(Qt::white), 12),
    });
    styles.push_back({
        .id = u"ocean_wave"_s,
        .name = u"Ocean Wave"_s,
        .description = u"Waves of light rolling through the water."_s,
        .animated = true,
        .color =
            [palette = colors({"#2fb4ff", "#3d7bff", "#22e0d0"})](qreal x,
                                                                  qreal t) {
                return mix(blend(palette, x), QColor("#e6fbff"),
                           std::pow(wave(x, t, 1.5, 3.2), 3) * 0.7);
            },
    });
    styles.push_back({
        .id = u"lava_lamp"_s,
        .name = u"Lava Lamp"_s,
        .description = u"Slow, gooey blobs of color."_s,
        .animated = true,
        .color = flow(colors({"#ff4f7b", "#ff9a3c", "#ff4fd8", "#9b5cff"}),
                      0.08, 0.5),
        .shadows = {glow("#70ff4f7b", 4)},
    });
    styles.push_back({
        .id = u"toxic_waste"_s,
        .name = u"Toxic Waste"_s,
        .description = u"Bubbling radioactive green."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const auto base = mix(QColor("#5dff3a"), QColor("#d4ff3a"),
                                      wave(x, t, 3, 5));
                return mix(base, QColor("#f4ffe0"), sparkle(x, t, 10) * 0.8);
            },
        .shadows = {glow("#c03dff5a", 5)},
    });
    styles.push_back({
        .id = u"glitch"_s,
        .name = u"Glitch"_s,
        .description = u"Corrupted cyan and magenta that jumps around."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal frame = std::floor(t * 10);
                const qreal shift = hash01(frame) > 0.7
                                        ? (hash01(frame + 3) - 0.5) * 0.6
                                        : 0.0;
                const qreal pos = fract(x + shift);
                if (hash01(std::floor(pos * 9) + frame) > 0.85)
                {
                    return QColor(Qt::white);
                }
                return pos < 0.5 ? QColor("#00f0ff") : QColor("#ff2bd6");
            },
        .shadows = {PaintDropShadow(1.5, 0, 0.5, QColor(255, 0, 80, 200))},
    });
    styles.push_back({
        .id = u"siren"_s,
        .name = u"Siren"_s,
        .description = u"Red and blue lights taking turns."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const bool swap = fract(t * 1.6) < 0.5;
                const bool left = x < 0.5;
                const auto lit =
                    left != swap ? QColor("#ff2e4d") : QColor("#2e7bff");
                const qreal strobe = fract(t * 6.4) < 0.5 ? 0.35 : 0;
                return mix(lit, QColor(Qt::white), strobe);
            },
    });
    styles.push_back({
        .id = u"heartbeat"_s,
        .name = u"Heartbeat"_s,
        .description = u"Red that beats like a heart: lub-dub."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal beat = fract(t * 1.1);
                const qreal pulse =
                    std::max(std::exp(-beat * 22),
                             std::exp(-std::abs(beat - 0.22) * 22));
                const auto base =
                    mix(QColor("#d6123c"), QColor("#ff5c86"), x * 0.6);
                return mix(base, QColor("#ffd0dc"), pulse * 0.8);
            },
        .shadows = {glow("#a0ff1e4d", 4)},
    });
    styles.push_back({
        .id = u"mainframe"_s,
        .name = u"Mainframe"_s,
        .description = u"Hacker green code flickering through."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal cell = std::floor(x * 10);
                const qreal on =
                    hash01(cell + std::floor((t * 12) + (hash01(cell) * 5)));
                return mix(QColor("#00b85c"), QColor("#b4ffc8"),
                           on * on * 0.9);
            },
        .shadows = {glow("#a000ff7a", 4)},
    });
    styles.push_back({
        .id = u"prism"_s,
        .name = u"Prism"_s,
        .description = u"White light splitting into a sweeping rainbow."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal center = (fract(t / 2.4) * 1.8) - 0.4;
                const qreal offset = (x - center) / 0.3;
                if (std::abs(offset) >= 1)
                {
                    return QColor("#f4f4ff");
                }
                return QColor::fromHsvF(float((offset + 1) / 2 * 0.85), 0.75F,
                                        1.0F);
            },
        .shadows = {shade(40, 40, 70)},
    });
    styles.push_back({
        .id = u"liquid_chrome"_s,
        .name = u"Liquid Chrome"_s,
        .description = u"Molten silver with a rippling reflection."_s,
        .animated = true,
        .angle = 90,
        .color =
            [palette = colors({"#ffffff", "#b9c4cc", "#6e7c88", "#e5ecf1"})](
                qreal x, qreal t) {
                const qreal ripple =
                    0.5 + (0.5 * std::sin((x * 9) + (t * 2.5)));
                return mix(blend(palette, fract(x + (ripple * 0.15))),
                           QColor(Qt::white), ripple * 0.25);
            },
        .shadows = {shade(0, 0, 0, 190)},
    });
    styles.push_back({
        .id = u"holo_flow"_s,
        .name = u"Holo"_s,
        .description = u"A shifting holographic foil."_s,
        .animated = true,
        .angle = 30,
        .color = flow(colors({"#ffd1f7", "#c9b8ff", "#a8e8ff", "#b8ffd9",
                              "#fff5b8"}),
                      0.3, 1.0),
    });
    styles.push_back({
        .id = u"candy_swirl"_s,
        .name = u"Candy Swirl"_s,
        .description = u"Pink peppermint stripes that keep spinning."_s,
        .animated = true,
        .angle = 45,
        .stripes = bands({"#ff4fa3", "#fff0f8"}),
        .stripeWidth = 0.3,
        .shadows = {shade(70, 0, 30)},
        .stripeSpeed = 1.2,
    });
    styles.push_back({
        .id = u"barber_pole"_s,
        .name = u"Barber Pole"_s,
        .description = u"Red, white and blue stripes always climbing."_s,
        .animated = true,
        .angle = 60,
        .stripes = bands({"#ff4057", "#ffffff", "#5b8cff", "#ffffff"}),
        .stripeWidth = 0.45,
        .shadows = {shade(0, 0, 0)},
        .stripeSpeed = 0.6,
    });
    styles.push_back({
        .id = u"caution_tape"_s,
        .name = u"Caution Tape"_s,
        .description = u"Moving yellow and orange hazard stripes."_s,
        .animated = true,
        .angle = 135,
        .stripes = bands({"#ffe600", "#ff8a00"}),
        .stripeWidth = 0.28,
        .shadows = {shade(40, 25, 0)},
        .stripeSpeed = 1.5,
    });
    styles.push_back({
        .id = u"sunset_drift"_s,
        .name = u"Sunset Drift"_s,
        .description = u"Sunset colors slowly drifting by."_s,
        .animated = true,
        .color = flow(colors({"#ffb347", "#ff6a5c", "#ff4f9a", "#b44cff"}),
                      0.14, 0.8),
        .shadows = {glow("#60ff5e7e", 3)},
    });
    styles.push_back({
        .id = u"thunderstorm"_s,
        .name = u"Thunderstorm"_s,
        .description = u"Storm clouds lit up by sudden lightning."_s,
        .animated = true,
        .color =
            [palette = colors({"#7b8cff", "#9f7bff", "#5c7aff", "#9f7bff"})](
                qreal x, qreal t) {
                return mix(blendLoop(palette, (x * 0.5) - (t * 0.1)),
                           QColor("#f4f0ff"), flash(t, 2) * 0.95);
            },
        .shadows = {glow("#909f7bff", 4)},
    });
    styles.push_back({
        .id = u"plasma"_s,
        .name = u"Plasma"_s,
        .description = u"Electric magenta and cyan crackling."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal a = std::sin((x * 11) + (t * 3.1));
                const qreal b = std::sin((x * 5) - (t * 1.7));
                return mix(QColor("#ff38e0"), QColor("#38e8ff"),
                           0.5 + (0.25 * (a + b)));
            },
        .shadows = {glow("#a0b84cff", 5)},
    });
    styles.push_back({
        .id = u"phantom"_s,
        .name = u"Phantom"_s,
        .description = u"A ghostly name that fades in and out."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                auto color = mix(QColor("#ffffff"), QColor("#c9b8ff"), x);
                color.setAlphaF(
                    float(0.45 + (0.55 * (0.5 + (0.5 * std::sin(t * 1.8))))));
                return color;
            },
        .shadows = {glow("#80c9b8ff", 5)},
    });
    styles.push_back({
        .id = u"bubblegum_pop"_s,
        .name = u"Bubblegum Pop"_s,
        .description = u"Bouncy pink that pops with sparkles."_s,
        .animated = true,
        .color = twinkle(
            [](qreal x, qreal t) {
                return mix(QColor("#ff6fc8"), QColor("#ffb3e6"),
                           wave(x, t, 1, 4));
            },
            QColor(Qt::white), 10),
    });
    styles.push_back({
        .id = u"sea_glass"_s,
        .name = u"Sea Glass"_s,
        .description = u"Frosted teal and mint washing back and forth."_s,
        .animated = true,
        .color = flow(colors({"#9ff5e1", "#5fd4c4", "#b8f2ff", "#7de8c8"}),
                      0.1, 0.6),
    });
    styles.push_back({
        .id = u"fireflies"_s,
        .name = u"Fireflies"_s,
        .description = u"Warm little lights blinking in the dark."_s,
        .animated = true,
        .color = twinkle(gradient(colors({"#9fd66a", "#6fbf73", "#a8d672"})),
                         QColor("#fff56b"), 9),
        .shadows = {glow("#80fff56b", 4)},
    });
    styles.push_back({
        .id = u"snowfall"_s,
        .name = u"Snowfall"_s,
        .description = u"Icy blue with snowflakes glittering."_s,
        .animated = true,
        .color = twinkle(gradient(colors({"#cfeeff", "#8fd3ff", "#b8e4ff"})),
                         QColor(Qt::white), 20),
        .shadows = {glow("#708fd3ff", 3)},
    });
    styles.push_back({
        .id = u"rgb_gamer"_s,
        .name = u"RGB Gamer"_s,
        .description = u"Blocky RGB that cycles like a gaming keyboard."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal hue = fract((std::floor(x * 6) / 6) + (t * 0.5));
                return QColor::fromHsvF(float(hue), 0.85F, 1.0F);
            },
        .shadows = {glow("#80ffffff", 2)},
    });
    styles.push_back({
        .id = u"rose_shimmer"_s,
        .name = u"Rose Shimmer"_s,
        .description = u"Rose gold with a shine gliding across."_s,
        .animated = true,
        .angle = 90,
        .color = glint(colors({"#f9dcd4", "#d99a8f", "#b76e79", "#f6c9bb"}),
                       QColor(Qt::white), 2.8),
        .shadows = {shade(40, 10, 15)},
    });
    styles.push_back({
        .id = u"diamond"_s,
        .name = u"Diamond"_s,
        .description = u"Brilliant white with flashes of every color."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const auto fire = QColor::fromHsvF(
                    float(hash01(std::floor(x * 16) + std::floor(t))), 0.6F,
                    1.0F);
                return mix(QColor("#eef6ff"), fire, sparkle(x, t, 16));
            },
        .shadows = {shade(30, 40, 70, 200)},
    });
    styles.push_back({
        .id = u"inferno"_s,
        .name = u"Inferno"_s,
        .description = u"Roaring flames racing across your name."_s,
        .animated = true,
        .angle = 90,
        .color =
            [palette = colors({"#fff7a1", "#ffb21f", "#ff4a1f", "#c4002b"})](
                qreal x, qreal t) {
                return blend(palette,
                             x + (0.25 * std::sin((t * 7) + (x * 13))));
            },
        .shadows = {glow("#c0ff4a1f", 5)},
    });
    styles.push_back({
        .id = u"galaxy_swirl"_s,
        .name = u"Galaxy Swirl"_s,
        .description = u"A spinning galaxy dusted with stars."_s,
        .animated = true,
        .color = twinkle(
            flow(colors({"#ff9ce0", "#b06bff", "#5a5cff", "#47b8ff"}), 0.2,
                 0.9),
            QColor(Qt::white), 15),
        .shadows = {glow("#a08a5cff", 4)},
    });

    // ---- Even more animated paints

    styles.push_back({
        .id = u"string_lights"_s,
        .name = u"String Lights"_s,
        .description = u"Holiday bulbs blinking on one after another."_s,
        .animated = true,
        .color =
            [palette = colors({"#ff4d5e", "#4dff88", "#ffd24d", "#4db8ff"})](
                qreal x, qreal t) {
                const int bulb = int(std::floor(x * 8));
                const auto color = palette[size_t(bulb) % palette.size()];
                const bool lit = (bulb + int(std::floor(t * 4))) % 3 != 0;
                return lit ? mix(color, QColor(Qt::white), 0.15)
                           : mix(color, QColor("#2a2a3a"), 0.55);
            },
        .shadows = {glow("#80ffd24d", 3)},
    });
    styles.push_back({
        .id = u"fireworks"_s,
        .name = u"Fireworks"_s,
        .description = u"Colorful bursts popping across a night sky."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal cell = std::floor(x * 7);
                const qreal round = std::floor((t * 0.9) + hash01(cell));
                const qreal age = fract((t * 0.9) + hash01(cell));
                const auto burst = QColor::fromHsvF(
                    float(hash01(cell + (round * 3.1))), 0.75F, 1.0F);
                const qreal strength = std::exp(-age * 4);
                return mix(QColor("#8f9bd9"), burst, strength);
            },
        .shadows = {glow("#80ffffff", 3)},
    });
    styles.push_back({
        .id = u"confetti"_s,
        .name = u"Confetti"_s,
        .description = u"Party colors flipping all over your name."_s,
        .animated = true,
        .color =
            [palette = colors({"#ff5c8a", "#ffd23f", "#3fe0d0", "#9b6bff",
                               "#7dff6b"})](qreal x, qreal t) {
                const qreal cell = std::floor(x * 12);
                const qreal step = std::floor(t * 5);
                return palette[size_t(hash01(cell + (step * 13.7)) *
                                      qreal(palette.size())) %
                               palette.size()];
            },
    });
    styles.push_back({
        .id = u"cyber_scan"_s,
        .name = u"Cyber Scan"_s,
        .description = u"A laser beam scanning back and forth."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal beam = bump(x, (pingpong(t, 2.2) * 1.2) - 0.1, 0.12);
                return mix(QColor("#2aa9c9"), QColor("#eaffff"), beam);
            },
        .shadows = {glow("#9000e5ff", 4)},
    });
    styles.push_back({
        .id = u"breathing"_s,
        .name = u"Breathing"_s,
        .description = u"Calm violet that slowly breathes in and out."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const auto base =
                    mix(QColor("#8f6bff"), QColor("#d47bff"), x);
                return mix(base, QColor("#f6eaff"), pingpong(t, 4) * 0.55);
            },
        .shadows = {glow("#808f6bff", 4)},
    });
    styles.push_back({
        .id = u"heat_wave"_s,
        .name = u"Heat Wave"_s,
        .description = u"Shimmering like air above hot asphalt."_s,
        .animated = true,
        .color =
            [palette = colors({"#ffe066", "#ffa94d", "#ff6b3d"})](qreal x,
                                                                  qreal t) {
                const qreal wobble = 0.08 * std::sin((x * 30) + (t * 9));
                return blend(palette, x + wobble);
            },
    });
    styles.push_back({
        .id = u"magma_rise"_s,
        .name = u"Magma Rise"_s,
        .description = u"Molten rock welling up from below."_s,
        .animated = true,
        .angle = 270,
        .color = flow(colors({"#ffef7a", "#ff8a1f", "#e8341c", "#ff8a1f"}),
                      0.5, 1.2),
        .shadows = {glow("#b0ff5a1f", 4)},
    });
    styles.push_back({
        .id = u"rainfall"_s,
        .name = u"Rainfall"_s,
        .description = u"Cool blue with raindrops streaking down."_s,
        .animated = true,
        .angle = 90,
        .color =
            [](qreal x, qreal t) {
                const qreal streak = std::pow(fract((x * 2) - (t * 1.8)), 6);
                return mix(QColor("#6f9cff"), QColor("#e6f3ff"), streak * 0.85);
            },
    });
    styles.push_back({
        .id = u"tide_pool"_s,
        .name = u"Tide Pool"_s,
        .description = u"Turquoise water sloshing back and forth."_s,
        .animated = true,
        .color =
            [palette = colors({"#3ff2d7", "#2ab8e0", "#6be8ff", "#a8fff0"})](
                qreal x, qreal t) {
                return blend(palette,
                             std::clamp(x + (pingpong(t, 3) - 0.5) * 0.8, 0.0,
                                        1.0));
            },
    });
    styles.push_back({
        .id = u"oil_slick"_s,
        .name = u"Oil Slick"_s,
        .description = u"Rainbow sheen floating on a puddle."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal hue = fract(0.55 + (swirl(x, t * 0.6) * 0.7));
                return QColor::fromHsvF(float(hue), 0.55F, 0.95F);
            },
        .shadows = {shade(10, 0, 30, 200)},
    });
    styles.push_back({
        .id = u"tie_dye"_s,
        .name = u"Tie-Dye"_s,
        .description = u"Groovy swirls of every color."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal hue = fract((swirl(x * 1.6, t) * 1.5) + (t * 0.05));
                return QColor::fromHsvF(float(hue), 0.8F, 1.0F);
            },
    });
    styles.push_back({
        .id = u"neon_sign"_s,
        .name = u"Neon Sign"_s,
        .description = u"Hot pink neon with a loose wire that flickers."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal slot = std::floor(t * 8);
                const bool flicker =
                    hash01(slot) > 0.93 && x > 0.35 && x < 0.75;
                return flicker ? QColor("#5a2348")
                               : mix(QColor("#ff4fd8"), QColor("#ffd1f4"),
                                     0.15);
            },
        .shadows = {glow("#d0ff2bd0", 6)},
    });
    styles.push_back({
        .id = u"traffic_light"_s,
        .name = u"Traffic Light"_s,
        .description = u"Red, yellow, green. Go!"_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const int lit = int(std::floor(t * 0.8)) % 3;
                const int part = std::min(2, int(x * 3));
                static const QColor on[3] = {QColor("#ff3b3b"),
                                             QColor("#ffd23b"),
                                             QColor("#3bff6b")};
                return part == lit ? on[part] : mix(on[part], QColor("#30303a"),
                                                    0.65);
            },
    });
    styles.push_back({
        .id = u"campfire"_s,
        .name = u"Campfire"_s,
        .description = u"Cozy orange flames with sparks drifting up."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal flame = swirl(x * 2, t * 3);
                const auto base =
                    mix(QColor("#ff5a1f"), QColor("#ffb23f"), flame);
                return mix(base, QColor("#fff3a0"), sparkle(x, t * 1.5, 11));
            },
        .shadows = {glow("#a0ff7a1f", 5)},
    });
    styles.push_back({
        .id = u"will_o_wisp"_s,
        .name = u"Will-o'-Wisp"_s,
        .description = u"A ghostly light wandering through the dark."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal center = 0.5 + (0.45 * std::sin(t * 0.9)) +
                                     (0.1 * std::sin(t * 2.3));
                return mix(QColor("#3d8f86"), QColor("#c8fff4"),
                           bump(x, center, 0.3));
            },
        .shadows = {glow("#a03dffd8", 5)},
    });
    styles.push_back({
        .id = u"spotlight"_s,
        .name = u"Spotlight"_s,
        .description = u"A stage light sliding back and forth across you."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                return mix(QColor("#8a93a8"), QColor("#fffbe8"),
                           bump(x, pingpong(t, 3.5), 0.35));
            },
        .shadows = {shade(0, 0, 0, 200)},
    });
    styles.push_back({
        .id = u"equalizer"_s,
        .name = u"Equalizer"_s,
        .description = u"Bouncing like a music visualizer."_s,
        .animated = true,
        .angle = 270,
        .color =
            [palette = colors({"#3dff7a", "#d4ff3d", "#ff5a3d"})](qreal y,
                                                                  qreal t) {
                const qreal level = 0.3 + (0.7 * hash01(std::floor(t * 7)));
                return y > level ? QColor("#3a5a48") : blend(palette, y);
            },
        .shadows = {glow("#703dff7a", 3)},
    });
    styles.push_back({
        .id = u"hypnotize"_s,
        .name = u"Hypnotize"_s,
        .description = u"Purple and pink stripes rushing past."_s,
        .animated = true,
        .angle = 0,
        .stripes = bands({"#b44cff", "#ff8fe6"}),
        .stripeWidth = 0.25,
        .shadows = {shade(30, 0, 40)},
        .stripeSpeed = 2.6,
    });
    styles.push_back({
        .id = u"zebra_run"_s,
        .name = u"Zebra Run"_s,
        .description = u"White and lilac stripes on the move."_s,
        .animated = true,
        .angle = 100,
        .stripes = bands({"#ffffff", "#b9a6ff"}),
        .stripeWidth = 0.22,
        .shadows = {shade(20, 0, 40)},
        .stripeSpeed = 0.9,
    });
    styles.push_back({
        .id = u"peppermint_twist"_s,
        .name = u"Peppermint Twist"_s,
        .description = u"Red, white and green candy stripes turning."_s,
        .animated = true,
        .angle = 35,
        .stripes = bands({"#ff3048", "#fff4f4", "#3fcf6b", "#fff4f4"}),
        .stripeWidth = 0.35,
        .shadows = {shade(50, 0, 10)},
        .stripeSpeed = 0.8,
    });
    styles.push_back({
        .id = u"spectrum_stripes"_s,
        .name = u"Spectrum Stripes"_s,
        .description = u"Diagonal rainbow bands marching along."_s,
        .animated = true,
        .angle = 60,
        .stripes = bands({"#ff5c5c", "#ffb35c", "#fff05c", "#5cff8a",
                          "#5cc8ff", "#a87bff"}),
        .stripeWidth = 0.6,
        .shadows = {shade(0, 0, 0)},
        .stripeSpeed = 0.5,
    });
    styles.push_back({
        .id = u"day_and_night"_s,
        .name = u"Day & Night"_s,
        .description = u"Sunrise, midday, sunset and night, over and over."_s,
        .animated = true,
        .color =
            [sky = colors({"#ffb37a", "#ffe98a", "#8fd8ff", "#ff7a6b",
                           "#8a7bff", "#6f8cff"})](qreal x, qreal t) {
                return blendLoop(sky, (t / 14) + (x * 0.12));
            },
    });
    styles.push_back({
        .id = u"mood_ring"_s,
        .name = u"Mood Ring"_s,
        .description = u"Your whole name slowly changes color with your mood."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                return QColor::fromHsvF(float(fract((t * 0.06) + (x * 0.08))),
                                        0.6F, 1.0F);
            },
    });
    styles.push_back({
        .id = u"chameleon"_s,
        .name = u"Chameleon"_s,
        .description = u"Blending between leaf green, teal and gold."_s,
        .animated = true,
        .color =
            [palette = colors({"#7dff6b", "#3fe0c8", "#e8ff5c", "#3fe0c8"})](
                qreal x, qreal t) {
                return blendLoop(palette, (t * 0.12) + (swirl(x, t) * 0.25));
            },
    });
    styles.push_back({
        .id = u"tv_static"_s,
        .name = u"TV Static"_s,
        .description = u"Fuzzy black-and-white snow from an old TV."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal grain =
                    hash01(std::floor(x * 40) + (std::floor(t * 20) * 17.3));
                return mix(QColor("#7a7a86"), QColor("#ffffff"), grain);
            },
        .shadows = {shade(0, 0, 0, 200)},
    });
    styles.push_back({
        .id = u"arcade"_s,
        .name = u"Arcade"_s,
        .description = u"Chunky neon pixels flashing like a game cabinet."_s,
        .animated = true,
        .color =
            [palette = colors({"#ff3df2", "#3dfff2", "#fff23d", "#3d8bff"})](
                qreal x, qreal t) {
                const qreal block = std::floor(x * 5);
                const int index = int(block + std::floor(t * 3)) %
                                  int(palette.size());
                return palette[size_t(index)];
            },
        .shadows = {glow("#90ff3df2", 3)},
    });
    styles.push_back({
        .id = u"soda_fizz"_s,
        .name = u"Soda Fizz"_s,
        .description = u"Lemon-lime with bubbles popping everywhere."_s,
        .animated = true,
        .color = twinkle(
            [](qreal x, qreal t) {
                return mix(QColor("#c6ff5c"), QColor("#5cffb0"),
                           wave(x, t, 1, 1.5));
            },
            QColor(Qt::white), 22),
    });
    styles.push_back({
        .id = u"static_shock"_s,
        .name = u"Static Shock"_s,
        .description = u"Crackling electric blue jolts."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal jolt = hash01(std::floor(x * 14) +
                                          (std::floor(t * 15) * 5.1));
                return jolt > 0.8 ? QColor("#ffffff")
                                  : mix(QColor("#3d7bff"), QColor("#7de8ff"),
                                        jolt);
            },
        .shadows = {glow("#c03d9bff", 5)},
    });
    styles.push_back({
        .id = u"comet"_s,
        .name = u"Comet"_s,
        .description = u"A blazing comet with a long tail flying by."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal head = (fract(t / 2.2) * 1.6) - 0.2;
                const qreal behind = head - x;
                qreal light = 0;
                if (behind >= 0 && behind < 0.6)
                {
                    light = std::pow(1 - (behind / 0.6), 2);
                }
                else if (behind < 0 && behind > -0.05)
                {
                    light = 1 + (behind / 0.05);
                }
                return mix(QColor("#5a6bb8"),
                           mix(QColor("#7dd8ff"), QColor(Qt::white), light),
                           light);
            },
        .shadows = {glow("#807dd8ff", 4)},
    });
    styles.push_back({
        .id = u"bioluminescence"_s,
        .name = u"Bioluminescence"_s,
        .description = u"Deep sea blue with creatures glowing cyan."_s,
        .animated = true,
        .color = twinkle(gradient(colors({"#3d6bff", "#2a8fd9", "#3d6bff"})),
                         QColor("#5cfff2"), 8),
        .shadows = {glow("#905cfff2", 5)},
    });
    styles.push_back({
        .id = u"sakura_breeze"_s,
        .name = u"Sakura Breeze"_s,
        .description = u"Pink blossoms with petals drifting past."_s,
        .animated = true,
        .color = twinkle(flow(colors({"#ffb3d9", "#ff8fc4", "#ffd6ea"}), 0.15,
                              0.8),
                         QColor(Qt::white), 13),
    });
    styles.push_back({
        .id = u"supernova"_s,
        .name = u"Supernova"_s,
        .description = u"A star exploding outward from the middle."_s,
        .animated = true,
        .color =
            [palette = colors({"#ffffff", "#ffe066", "#ff7a3d", "#c23dff",
                               "#5a3dff"})](qreal x, qreal t) {
                const qreal blast = fract(t / 2.6);
                const qreal reach = std::abs(x - 0.5) * 2;
                return blend(palette,
                             std::clamp(reach - blast + 0.5, 0.0, 1.0));
            },
        .shadows = {glow("#a0ff7a3d", 5)},
    });
    styles.push_back({
        .id = u"ripple"_s,
        .name = u"Ripple"_s,
        .description = u"Rings of water spreading out from the center."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal ring =
                    0.5 + (0.5 * std::sin((std::abs(x - 0.5) * 22) - (t * 6)));
                return mix(QColor("#2f8fff"), QColor("#c8f0ff"), ring * ring);
            },
    });
    styles.push_back({
        .id = u"acid_trip"_s,
        .name = u"Acid Trip"_s,
        .description = u"Wildly warping, eye-melting colors."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal warp = std::sin((x * 9) + (t * 4)) * 0.35;
                return QColor::fromHsvF(float(fract((x * 1.3) + warp + t * 0.6)),
                                        1.0F, 1.0F);
            },
    });
    styles.push_back({
        .id = u"champagne"_s,
        .name = u"Champagne"_s,
        .description = u"Pale gold full of tiny rising bubbles."_s,
        .animated = true,
        .color = twinkle(gradient(colors({"#fff4c8", "#f2dc8f", "#fff0b8"})),
                         QColor(Qt::white), 28),
        .shadows = {shade(70, 50, 0)},
    });
    styles.push_back({
        .id = u"nebula_drift"_s,
        .name = u"Nebula Drift"_s,
        .description = u"Clouds of magenta and teal space dust."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                return mix(QColor("#ff5ce0"), QColor("#3fe0e0"),
                           swirl(x, t * 0.5));
            },
        .shadows = {glow("#90c25cff", 5)},
    });
    styles.push_back({
        .id = u"candlelight"_s,
        .name = u"Candlelight"_s,
        .description = u"A warm, gently flickering glow."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal flicker = 0.5 + (0.3 * std::sin(t * 13)) +
                                      (0.2 * std::sin(t * 7.7 + 1));
                const auto base =
                    mix(QColor("#ffb84d"), QColor("#ffdf8a"), x * 0.5);
                return mix(base, QColor("#fff6d6"), flicker * 0.4);
            },
        .shadows = {glow("#a0ffb84d", 5)},
    });
    styles.push_back({
        .id = u"bleeding"_s,
        .name = u"Bleeding"_s,
        .description = u"Crimson dripping down your name."_s,
        .animated = true,
        .angle = 90,
        .color = flow(colors({"#ff4a5c", "#b3122c", "#ff2a40", "#8a0a1e"}),
                      -0.35, 1.0),
        .shadows = {glow("#a0ff1e37", 3)},
    });
    styles.push_back({
        .id = u"freeze"_s,
        .name = u"Freeze"_s,
        .description = u"Ice creeping over warm colors, then thawing."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal front = (fract(t / 4) * 1.4) - 0.2;
                const qreal frozen = std::clamp((front - x) / 0.15, 0.0, 1.0);
                const auto warm = mix(QColor("#ff9a5c"), QColor("#ff5c8a"), x);
                return mix(warm, QColor("#d6f4ff"), frozen);
            },
        .shadows = {glow("#708fd3ff", 3)},
    });
    styles.push_back({
        .id = u"charging"_s,
        .name = u"Charging"_s,
        .description = u"Filling up with energy like a battery."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal level = fract(t / 3) * 1.15;
                if (x > level)
                {
                    return QColor("#4a5a50");
                }
                const auto fill = level > 1 ? QColor("#d4ffd9")
                                            : mix(QColor("#3dff7a"),
                                                  QColor("#b4ff3d"), x);
                return fill;
            },
        .shadows = {glow("#803dff7a", 3)},
    });
    styles.push_back({
        .id = u"kaleidoscope"_s,
        .name = u"Kaleidoscope"_s,
        .description = u"Mirrored colors turning from the center out."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal mirrored = std::abs(x - 0.5) * 2;
                return QColor::fromHsvF(
                    float(fract((mirrored * 0.9) - (t * 0.3))), 0.7F, 1.0F);
            },
    });
    styles.push_back({
        .id = u"fire_and_ice"_s,
        .name = u"Fire & Ice"_s,
        .description = u"Flames on one side, frost on the other, fighting."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal border = 0.5 + (0.15 * std::sin(t * 1.3));
                const qreal side =
                    std::clamp((x - border) / 0.12 + 0.5, 0.0, 1.0);
                const auto fire = mix(QColor("#ff5a1f"), QColor("#ffd23f"),
                                      swirl(x, t * 2));
                const auto ice = mix(QColor("#5cc8ff"), QColor("#e6f8ff"),
                                     swirl(x + 3, t));
                return mix(fire, ice, side);
            },
        .shadows = {shade(0, 0, 0, 180)},
    });
    styles.push_back({
        .id = u"candy_floss_spin"_s,
        .name = u"Candy Floss Spin"_s,
        .description = u"Soft pink and blue fluff spinning around."_s,
        .animated = true,
        .angle = 45,
        .stripes = bands({"#ffa8dc", "#a8d8ff"}),
        .stripeWidth = 0.7,
        .shadows = {shade(40, 10, 50)},
        .stripeSpeed = 0.7,
    });
    styles.push_back({
        .id = u"pulsar"_s,
        .name = u"Pulsar"_s,
        .description = u"Sharp beams of blue light firing across."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal beam = std::pow(fract((x * 1.5) - (t * 1.2)), 12);
                return mix(QColor("#4a5cff"), QColor("#eaf0ff"), beam);
            },
        .shadows = {glow("#904a5cff", 4)},
    });
    styles.push_back({
        .id = u"witch_cauldron"_s,
        .name = u"Witch's Cauldron"_s,
        .description = u"Bubbling green potion with purple smoke."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const auto brew = mix(QColor("#6bff3d"), QColor("#b05cff"),
                                      swirl(x, t * 1.4));
                return mix(brew, QColor("#eaffd6"), sparkle(x, t * 2, 9));
            },
        .shadows = {glow("#906bff3d", 4)},
    });
    styles.push_back({
        .id = u"vinyl"_s,
        .name = u"Vinyl"_s,
        .description = u"A shiny record spinning on the turntable."_s,
        .animated = true,
        .color =
            [](qreal x, qreal t) {
                const qreal grooves =
                    0.5 + (0.5 * std::sin((std::abs(x - 0.5) * 60)));
                const qreal shine = bump(fract(x + (t * 0.5)), 0.5, 0.18);
                const auto base =
                    mix(QColor("#8a8aa0"), QColor("#b8b8cc"), grooves);
                return mix(base, QColor("#ffffff"), shine * 0.8);
            },
        .shadows = {shade(0, 0, 0, 200)},
    });

    std::vector<std::shared_ptr<SolrinoPaint>> catalog;
    for (auto &style : styles)
    {
        catalog.push_back(std::make_shared<SolrinoPaint>(std::move(style)));
    }
    return catalog;
}

QString bareId(QStringView id)
{
    if (id.startsWith(QLatin1String(SolrinoPaints::ID_PREFIX)))
    {
        id = id.sliced(qsizetype(std::strlen(SolrinoPaints::ID_PREFIX)));
    }
    return id.trimmed().toString().toLower();
}

}  // namespace

// ------------------------------------------------------------- SolrinoPaint

SolrinoPaint::SolrinoPaint(Style style)
    : Paint(style.name, QString(SolrinoPaints::ID_PREFIX) + style.id)
    , style_(std::move(style))
{
}

bool SolrinoPaint::animated() const
{
    return this->style_.animated;
}

const SolrinoPaint::Style &SolrinoPaint::style() const
{
    return this->style_;
}

const std::vector<PaintDropShadow> &SolrinoPaint::getDropShadows() const
{
    return this->style_.shadows;
}

qint64 SolrinoPaint::sourcePixmapCacheKey() const
{
    // About 30 new frames a second
    return this->style_.animated ? QDateTime::currentMSecsSinceEpoch() / FRAME_MS
                                 : 0;
}

QBrush SolrinoPaint::asBrush(QColor userColor, QRectF rect) const
{
    if (rect.isEmpty())
    {
        return {userColor};
    }

    const qreal radians = this->style_.angle * std::numbers::pi / 180;
    const QPointF direction(std::cos(radians), std::sin(radians));

    if (!this->style_.stripes.isEmpty())
    {
        const qreal period =
            std::max(2.0, rect.height() * this->style_.stripeWidth * 2);
        QPointF start = rect.topLeft();
        if (this->style_.animated && this->style_.stripeSpeed != 0)
        {
            const qreal seconds = animationSeconds();
            start += direction * (fract(seconds * this->style_.stripeSpeed) *
                                  period);
        }
        QLinearGradient stripes(start, start + (direction * period));
        stripes.setSpread(QGradient::RepeatSpread);
        stripes.setStops(this->style_.stripes);
        return {stripes};
    }

    const qreal half = (std::abs(rect.width() * direction.x()) +
                        std::abs(rect.height() * direction.y())) /
                       2;
    const QPointF center = rect.center();
    QLinearGradient gradient(center - (direction * half),
                             center + (direction * half));

    const qreal seconds = this->style_.animated ? animationSeconds() : 0;
    const int samples = this->style_.animated ? ANIMATED_SAMPLES : 32;
    for (int i = 0; i < samples; ++i)
    {
        const qreal position = qreal(i) / (samples - 1);
        gradient.setColorAt(position, this->style_.color(position, seconds));
    }
    return {gradient};
}

// ------------------------------------------------------------ SolrinoPaints

SolrinoPaints &SolrinoPaints::instance()
{
    static SolrinoPaints paints;
    return paints;
}

SolrinoPaints::SolrinoPaints()
    : catalog_(buildCatalog())
{
    for (const auto &paint : this->catalog_)
    {
        this->byId_.emplace(paint->style().id, paint);
    }
    this->loadOwnPaints();
}

void SolrinoPaints::refreshAnyWorn()
{
    bool worn = !this->serverPaints_.empty();
    for (const auto &[login, paint] : this->ownPaints_)
    {
        worn = worn || !paint.isEmpty();
    }
    this->anyWorn_.store(worn, std::memory_order_relaxed);
}

const std::vector<std::shared_ptr<SolrinoPaint>> &SolrinoPaints::catalog() const
{
    return this->catalog_;
}

std::shared_ptr<SolrinoPaint> SolrinoPaints::byId(QStringView id) const
{
    // Saved ids are already bare, so most lookups skip the cleanup
    auto it = this->byId_.find(id.toString());
    if (it == this->byId_.end())
    {
        it = this->byId_.find(bareId(id));
    }
    return it == this->byId_.end() ? nullptr : it->second;
}

std::shared_ptr<SolrinoPaint> SolrinoPaints::forLogin(const QString &login) const
{
    // Chats ask this for every name many times a second
    if (login.isEmpty() || !this->anyWorn_.load(std::memory_order_relaxed))
    {
        return nullptr;
    }
    const auto key = login.isLower() ? login : login.toLower();
    std::shared_lock lock(this->mutex_);
    // What someone picked on this device wins, even before the server knows
    if (const auto own = this->ownPaints_.find(key);
        own != this->ownPaints_.end())
    {
        return this->byId(own->second);
    }
    if (const auto server = this->serverPaints_.find(key);
        server != this->serverPaints_.end())
    {
        return this->byId(server->second);
    }
    return nullptr;
}

QString SolrinoPaints::ownPaint(const QString &login) const
{
    std::shared_lock lock(this->mutex_);
    const auto key = login.toLower();
    if (const auto own = this->ownPaints_.find(key);
        own != this->ownPaints_.end())
    {
        return own->second;
    }
    const auto server = this->serverPaints_.find(key);
    return server == this->serverPaints_.end() ? QString() : server->second;
}

void SolrinoPaints::setOwnPaint(const QString &login, const QString &userId,
                                const QString &oauthToken,
                                const QString &paintId, QObject *caller,
                                std::function<void(bool)> done)
{
    const auto bare = this->byId(paintId) ? bareId(paintId) : QString();
    {
        std::unique_lock lock(this->mutex_);
        this->ownPaints_.insert_or_assign(login.toLower(), bare);
        this->refreshAnyWorn();
    }
    this->saveOwnPaints();

    if (userId.isEmpty() || oauthToken.isEmpty())
    {
        if (done)
        {
            done(false);
        }
        return;
    }

    // Twitch confirms who this is, so nobody can change someone else's paint
    NetworkRequest(PAINT_ENDPOINT, NetworkRequestType::Put)
        .header("Authorization", ("OAuth " + oauthToken).toUtf8())
        .json(QJsonObject{{"userId", userId}, {"paint", bare}})
        .timeout(10000)
        .maximumResponseSize(64 * 1024)
        .caller(caller)
        .onSuccess([done, bare](const NetworkResult &result) {
            const auto confirmed =
                result.parseJson().value("paint").toString() == bare;
            if (done)
            {
                done(confirmed);
            }
        })
        .onError([done](const NetworkResult &) {
            if (done)
            {
                done(false);
            }
        })
        .execute();
}

void SolrinoPaints::applyServerUsers(const QJsonObject &users)
{
    std::unordered_map<QString, QString> paints;
    for (auto it = users.constBegin(); it != users.constEnd(); ++it)
    {
        const auto user = it.value().toObject();
        const auto login = user.value("login").toString().trimmed().toLower();
        const auto paint = bareId(user.value("paint").toString());
        if (login.isEmpty() || paint.isEmpty() || !this->byId(paint))
        {
            continue;
        }
        paints.insert_or_assign(login, paint);
    }

    std::unique_lock lock(this->mutex_);
    this->serverPaints_ = std::move(paints);
    this->refreshAnyWorn();
}

void SolrinoPaints::loadOwnPaints()
{
    const auto saved = QJsonDocument::fromJson(
                           getSettings()->solrinoOwnPaints.getValue().toUtf8())
                           .object();
    std::unique_lock lock(this->mutex_);
    for (auto it = saved.constBegin(); it != saved.constEnd(); ++it)
    {
        this->ownPaints_.insert_or_assign(it.key().toLower(),
                                          bareId(it.value().toString()));
    }
    this->refreshAnyWorn();
}

void SolrinoPaints::saveOwnPaints() const
{
    QJsonObject saved;
    {
        std::shared_lock lock(this->mutex_);
        for (const auto &[login, paint] : this->ownPaints_)
        {
            saved.insert(login, paint);
        }
    }
    getSettings()->solrinoOwnPaints.setValue(
        QString::fromUtf8(QJsonDocument(saved).toJson(QJsonDocument::Compact)));
}

}  // namespace chatterino

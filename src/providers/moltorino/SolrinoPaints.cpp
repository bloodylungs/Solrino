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
    // A new frame about 25 times a second
    return this->style_.animated ? QDateTime::currentMSecsSinceEpoch() / 40 : 0;
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
            const qreal seconds =
                qreal(QDateTime::currentMSecsSinceEpoch() % 3600000) / 1000;
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

    const qreal seconds =
        this->style_.animated
            ? qreal(QDateTime::currentMSecsSinceEpoch() % 3600000) / 1000
            : 0;
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
    this->loadOwnPaints();
}

const std::vector<std::shared_ptr<SolrinoPaint>> &SolrinoPaints::catalog() const
{
    return this->catalog_;
}

std::shared_ptr<SolrinoPaint> SolrinoPaints::byId(QStringView id) const
{
    const auto bare = bareId(id);
    if (bare.isEmpty())
    {
        return nullptr;
    }
    for (const auto &paint : this->catalog_)
    {
        if (paint->style().id == bare)
        {
            return paint;
        }
    }
    return nullptr;
}

std::shared_ptr<SolrinoPaint> SolrinoPaints::forLogin(const QString &login) const
{
    if (login.isEmpty())
    {
        return nullptr;
    }
    const auto key = login.toLower();
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

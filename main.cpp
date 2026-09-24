/*
    SPIN THE WHEEL - SFML 3.1.0 / Code::Blocks (MinGW 64-bit)
    ============================================================
    "Aesthetic" edition: white gradient background, glow rings, a polished
    pointer, a rounded glass side-panel with clickable buttons, a pulsing
    winner highlight, a big winner pop-up with confetti, and support for a
    nicer custom font.

    FEATURES
    --------
    1. Editable contestant list, entirely by CLICKING buttons:
         - "+ Add Contestant" button opens a little text box (type the name,
           then click the checkmark or press Enter to confirm, the X or Esc
           to cancel).
         - Each contestant row has its own small "x" button to remove that
           contestant, no matter where it sits in the list.
         - "Clear All" button wipes the whole list.
         - "SPIN THE WHEEL" button (or the Space bar) starts a spin.
    2. Fair spin -> lands on a random contestant.
    3. Rigged spin -> YOU choose which contestant number wins by pressing
       R, typing their number, and pressing Enter. The wheel still spins
       normally, but the final resting angle is mathematically forced to
       land on that slice. This control is intentionally NOT shown
       anywhere in the UI - it's a keyboard-only "hidden" feature.

    HOW THE RIGGING WORKS
    -------------------------------------------------------------
    The pointer is fixed at the TOP of the wheel (screen angle 0). Each
    contestant i owns a local (un-rotated) slice [i*segAngle, (i+1)*segAngle).
    Rotating the wheel by wheelRotation degrees means the slice under the
    pointer is local = (-wheelRotation) mod 360. To force contestant
    `target` to win we solve that backwards: aim the middle of its slice at
    the pointer, then add a few extra 360 degree turns purely for show.
    See computeRiggedFinalRotation().

    FONT
    ----
    For best looks, download the free Google Font "Poppins" (Bold + Regular)
    from https://fonts.google.com/specimen/Poppins and drop these two files
    next to your .exe:
        Poppins-Bold.ttf
        Poppins-Regular.ttf
    If they aren't found, the program automatically falls back to Arial
    (or whatever system font it can find), so it still runs either way.
*/

#include <SFML/Graphics.hpp>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <optional>
#include <cstdint>
#include <algorithm>

const float PI = 3.14159265358979323846f;

const unsigned WINDOW_W = 1080;
const unsigned WINDOW_H = 760;

const sf::Vector2f WHEEL_CENTER(380.f, 390.f);
const float WHEEL_RADIUS = 300.f;

// Shared with the winner pop-up card AND its (screen-space) close-button
// hit test, so the two always agree on where the card actually is.
const float POPUP_CARD_W = 560.f;
const float POPUP_CARD_H = 320.f;

// Side panel geometry - shared between the layout calculator (used for
// mouse hit-testing) and the drawing code, so buttons are always exactly
// where they visually appear.
const float PANEL_X = 760.f;
const float PANEL_Y = 20.f;
const float PANEL_W = WINDOW_W - PANEL_X - 20.f;
const float PANEL_H = WINDOW_H - 40.f;

// ---------------------------------------------------------------------
// Palette (modern flat/vibrant tones, light theme)
// ---------------------------------------------------------------------
namespace Palette
{
    const sf::Color bgTop(255, 255, 255);
    const sf::Color bgBottom(238, 238, 245);
    const sf::Color panel(255, 255, 255, 250);
    const sf::Color panelBorder(220, 220, 230, 220);
    const sf::Color accent(124, 58, 237);       // deeper violet, reads well on white
    const sf::Color accentSoft(124, 58, 237, 60);
    const sf::Color gold(217, 119, 6);          // darkened amber, reads well on white
    const sf::Color textMain(30, 30, 40);
    const sf::Color textDim(110, 110, 125);
    const sf::Color danger(220, 38, 38);
    const sf::Color success(22, 163, 74);
    const sf::Color disabledGray(210, 210, 218);
    const sf::Color disabledText(150, 150, 160);

    // The winner pop-up card stays dark for contrast against the white
    // background, so it needs its own light text colors.
    const sf::Color cardText(245, 245, 250);
    const sf::Color cardTextDim(180, 180, 195);
    const sf::Color cardGold(250, 204, 21);

    const sf::Color slices[] = {
        sf::Color(244, 63, 94),   // rose
        sf::Color(59, 130, 246),  // blue
        sf::Color(16, 185, 129),  // emerald
        sf::Color(245, 158, 11),  // amber
        sf::Color(139, 92, 246),  // violet
        sf::Color(249, 115, 22),  // orange
        sf::Color(6, 182, 212),   // cyan
        sf::Color(236, 72, 153),  // pink
        sf::Color(132, 204, 22),  // lime
        sf::Color(99, 102, 241),  // indigo
        sf::Color(234, 88, 12),   // burnt orange
        sf::Color(20, 184, 166)   // teal
    };
    sf::Color slice(size_t i) { return slices[i % (sizeof(slices) / sizeof(slices[0]))]; }
}

// ---------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------
struct Contestant
{
    std::string name;
    sf::Color   color;
};

enum class AppState { IDLE, SPINNING, RESULT };
enum class InputMode { NONE, ADD_NAME, RIG_INDEX }; // RIG_INDEX is the hidden feature

struct Particle
{
    sf::Vector2f pos, vel;
    sf::Color color;
    float life, maxLife;
    float size;
    float rotation, rotSpeed;
};

// ---------------------------------------------------------------------
// Math / color helpers
// ---------------------------------------------------------------------
float toRad(float deg) { return deg * PI / 180.f; }

sf::Vector2f pointOnCircle(sf::Vector2f center, float localAngleDeg, float rotationDeg, float radius)
{
    float a = toRad(localAngleDeg + rotationDeg);
    return center + radius * sf::Vector2f(std::sin(a), -std::cos(a));
}

float wrap360(float deg)
{
    deg = std::fmod(deg, 360.f);
    if (deg < 0.f) deg += 360.f;
    return deg;
}

sf::Color withAlpha(sf::Color c, std::uint8_t a) { c.a = a; return c; }

sf::Color lighten(sf::Color c, float amount)
{
    auto up = [&](std::uint8_t v) { return (std::uint8_t)std::min(255.f, v + (255.f - v) * amount); };
    return sf::Color(up(c.r), up(c.g), up(c.b), c.a);
}

sf::Color darken(sf::Color c, float amount)
{
    auto down = [&](std::uint8_t v) { return (std::uint8_t)(v * (1.f - amount)); };
    return sf::Color(down(c.r), down(c.g), down(c.b), c.a);
}

// Builds a rounded-rectangle polygon (used for panel, card, and buttons)
sf::ConvexShape makeRoundedRect(sf::Vector2f pos, sf::Vector2f size, float radius)
{
    radius = std::min(radius, std::min(size.x, size.y) / 2.f);
    std::vector<sf::Vector2f> pts;
    const int STEP = 10; // degrees per arc segment

    struct Corner { float cx, cy, startDeg; };
    Corner corners[4] = {
        { pos.x + size.x - radius, pos.y + radius,          -90.f }, // top-right
        { pos.x + size.x - radius, pos.y + size.y - radius,   0.f }, // bottom-right
        { pos.x + radius,          pos.y + size.y - radius,  90.f }, // bottom-left
        { pos.x + radius,          pos.y + radius,           180.f } // top-left
    };

    for (auto& c : corners)
    {
        for (int a = 0; a <= 90; a += STEP)
        {
            float ang = toRad(c.startDeg + a);
            pts.push_back(sf::Vector2f(c.cx + radius * std::cos(ang), c.cy + radius * std::sin(ang)));
        }
    }

    sf::ConvexShape shape;
    shape.setPointCount(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) shape.setPoint(i, pts[i]);
    return shape;
}

void drawGradientBackground(sf::RenderWindow& window)
{
    sf::VertexArray quad(sf::PrimitiveType::TriangleStrip, 4);
    quad[0] = sf::Vertex{ sf::Vector2f(0.f, 0.f), Palette::bgTop };
    quad[1] = sf::Vertex{ sf::Vector2f((float)WINDOW_W, 0.f), Palette::bgTop };
    quad[2] = sf::Vertex{ sf::Vector2f(0.f, (float)WINDOW_H), Palette::bgBottom };
    quad[3] = sf::Vertex{ sf::Vector2f((float)WINDOW_W, (float)WINDOW_H), Palette::bgBottom };
    window.draw(quad);
}

// ---------------------------------------------------------------------
// Wheel drawing
// ---------------------------------------------------------------------
void drawGlowRings(sf::RenderWindow& window, float pulse)
{
    for (int i = 5; i >= 1; --i)
    {
        float r = WHEEL_RADIUS + 10.f + i * 9.f + pulse * 4.f;
        sf::CircleShape ring(r);
        ring.setOrigin(sf::Vector2f(r, r));
        ring.setPosition(WHEEL_CENTER);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(2.f);
        std::uint8_t alpha = (std::uint8_t)(30 - i * 4);
        ring.setOutlineColor(withAlpha(Palette::accent, alpha));
        window.draw(ring);
    }
}

void drawWheel(sf::RenderWindow& window, const std::vector<Contestant>& contestants,
               float wheelRotation, const sf::Font& fontBold, const sf::Font& fontRegular,
               int highlightIndex, float highlightPulse)
{
    // drop shadow
    sf::CircleShape shadow(WHEEL_RADIUS + 6.f);
    shadow.setOrigin(sf::Vector2f(WHEEL_RADIUS + 6.f, WHEEL_RADIUS + 6.f));
    shadow.setPosition(WHEEL_CENTER + sf::Vector2f(0.f, 10.f));
    shadow.setFillColor(sf::Color(0, 0, 0, 90));
    window.draw(shadow);

    if (contestants.empty())
    {
        sf::CircleShape empty(WHEEL_RADIUS);
        empty.setOrigin(sf::Vector2f(WHEEL_RADIUS, WHEEL_RADIUS));
        empty.setPosition(WHEEL_CENTER);
        empty.setFillColor(sf::Color(240, 240, 245));
        empty.setOutlineThickness(4.f);
        empty.setOutlineColor(Palette::accent);
        window.draw(empty);

        sf::Text msg(fontRegular, "Add contestants to get started", 20);
        msg.setFillColor(Palette::textDim);
        sf::FloatRect b = msg.getLocalBounds();
        msg.setOrigin(sf::Vector2f(b.position.x + b.size.x / 2.f, b.position.y + b.size.y / 2.f));
        msg.setPosition(WHEEL_CENTER);
        window.draw(msg);
        return;
    }

    size_t n = contestants.size();
    float segAngle = 360.f / (float)n;
    const int SUBDIV_STEP = 4;

    for (size_t i = 0; i < n; ++i)
    {
        float startA = i * segAngle;
        float endA   = (i + 1) * segAngle;

        sf::ConvexShape wedge;
        std::vector<sf::Vector2f> pts;
        pts.push_back(WHEEL_CENTER);
        for (float a = startA; a < endA; a += SUBDIV_STEP)
            pts.push_back(pointOnCircle(WHEEL_CENTER, a, wheelRotation, WHEEL_RADIUS));
        pts.push_back(pointOnCircle(WHEEL_CENTER, endA, wheelRotation, WHEEL_RADIUS));

        wedge.setPointCount(pts.size());
        for (size_t k = 0; k < pts.size(); ++k)
            wedge.setPoint(k, pts[k]);

        sf::Color base = contestants[i].color;
        wedge.setFillColor(base);
        wedge.setOutlineThickness(2.f);
        wedge.setOutlineColor(sf::Color(20, 20, 28));
        window.draw(wedge);

        // subtle inner highlight sliver near the outer rim for a glossy feel
        sf::ConvexShape sheen;
        std::vector<sf::Vector2f> sheenPts;
        float sheenR = WHEEL_RADIUS * 0.97f;
        sheenPts.push_back(pointOnCircle(WHEEL_CENTER, startA, wheelRotation, WHEEL_RADIUS * 0.55f));
        for (float a = startA; a < endA; a += SUBDIV_STEP)
            sheenPts.push_back(pointOnCircle(WHEEL_CENTER, a, wheelRotation, sheenR));
        sheenPts.push_back(pointOnCircle(WHEEL_CENTER, endA, wheelRotation, sheenR));
        sheen.setPointCount(sheenPts.size());
        for (size_t k = 0; k < sheenPts.size(); ++k) sheen.setPoint(k, sheenPts[k]);
        sheen.setFillColor(withAlpha(lighten(base, 0.35f), 40));
        window.draw(sheen);

        // pulsing gold outline on the winning slice
        if ((int)i == highlightIndex)
        {
            sf::ConvexShape glowOutline = wedge;
            glowOutline.setFillColor(sf::Color::Transparent);
            glowOutline.setOutlineThickness(3.5f + highlightPulse * 2.5f);
            glowOutline.setOutlineColor(withAlpha(Palette::gold, (std::uint8_t)(180 + highlightPulse * 60)));
            window.draw(glowOutline);
        }
    }

    // Labels - auto-fit: shrink font to fit the slice, and if a name still
    // won't fit running around the wheel (too many contestants and/or a
    // long name), fall back to a radial orientation (reading from the hub
    // out to the rim) which has much more room to work with.
    for (size_t i = 0; i < n; ++i)
    {
        float mid = (i + 0.5f) * segAngle;
        float screenAngle = wrap360(mid + wheelRotation);
        const std::string& name = contestants[i].name;

        // --- try TANGENTIAL layout first (reads around the wheel) ---
        const float tangentialRadius = WHEEL_RADIUS * 0.64f;
        float arcWidthAvailable = 2.f * tangentialRadius * std::sin(toRad(segAngle) / 2.f) * 0.80f;

        unsigned bestTangSize = 0;
        for (unsigned size = 18; size >= 9; --size)
        {
            sf::Text probe(fontBold, name, size);
            float w = probe.getLocalBounds().size.x;
            if (w <= arcWidthAvailable) { bestTangSize = size; break; }
        }

        if (bestTangSize >= 10)
        {
            // Fits comfortably tangentially - use the classic around-the-wheel look.
            sf::Vector2f pos = pointOnCircle(WHEEL_CENTER, mid, wheelRotation, tangentialRadius);

            sf::Text label(fontBold, name, bestTangSize);
            label.setFillColor(sf::Color(255, 255, 255));
            label.setOutlineThickness(2.f);
            label.setOutlineColor(darken(contestants[i].color, 0.55f));
            sf::FloatRect b = label.getLocalBounds();
            label.setOrigin(sf::Vector2f(b.position.x + b.size.x / 2.f, b.position.y + b.size.y / 2.f));
            label.setPosition(pos);

            float textRot = screenAngle;
            if (screenAngle > 90.f && screenAngle < 270.f) textRot += 180.f;
            label.setRotation(sf::degrees(textRot));

            window.draw(label);
        }
        else
        {
            // Too crowded / too long: switch to RADIAL orientation, reading
            // outward from near the hub toward the rim, where there's a lot
            // more length to work with than the thin tangential arc.
            const float startRadius = WHEEL_RADIUS * 0.24f;
            const float radialLenAvailable = WHEEL_RADIUS * 0.90f - startRadius;

            unsigned bestRadSize = 9;
            for (unsigned size = 16; size >= 8; --size)
            {
                sf::Text probe(fontBold, name, size);
                float w = probe.getLocalBounds().size.x;
                if (w <= radialLenAvailable) { bestRadSize = size; break; }
            }

            sf::Vector2f startPos = pointOnCircle(WHEEL_CENTER, mid, wheelRotation, startRadius);

            sf::Text label(fontBold, name, bestRadSize);
            label.setFillColor(sf::Color(255, 255, 255));
            label.setOutlineThickness(1.5f);
            label.setOutlineColor(darken(contestants[i].color, 0.55f));
            sf::FloatRect b = label.getLocalBounds();
            // origin at the left edge, vertically centered, so the text
            // starts at startPos and reads outward toward the rim
            label.setOrigin(sf::Vector2f(b.position.x, b.position.y + b.size.y / 2.f));
            label.setPosition(startPos);
            label.setRotation(sf::degrees(screenAngle - 90.f));

            window.draw(label);
        }
    }

    // thin radial divider lines for polish
    for (size_t i = 0; i < n; ++i)
    {
        float a = i * segAngle;
        sf::Vertex line[2];
        line[0] = sf::Vertex{ WHEEL_CENTER, sf::Color(20, 20, 28, 160) };
        line[1] = sf::Vertex{ pointOnCircle(WHEEL_CENTER, a, wheelRotation, WHEEL_RADIUS), sf::Color(20, 20, 28, 60) };
        window.draw(line, 2, sf::PrimitiveType::Lines);
    }

    // Outer rim (double ring for a "casino wheel" look)
    sf::CircleShape rimOuter(WHEEL_RADIUS + 4.f);
    rimOuter.setOrigin(sf::Vector2f(WHEEL_RADIUS + 4.f, WHEEL_RADIUS + 4.f));
    rimOuter.setPosition(WHEEL_CENTER);
    rimOuter.setFillColor(sf::Color::Transparent);
    rimOuter.setOutlineThickness(6.f);
    rimOuter.setOutlineColor(Palette::gold);
    window.draw(rimOuter);

    sf::CircleShape rimInner(WHEEL_RADIUS - 2.f);
    rimInner.setOrigin(sf::Vector2f(WHEEL_RADIUS - 2.f, WHEEL_RADIUS - 2.f));
    rimInner.setPosition(WHEEL_CENTER);
    rimInner.setFillColor(sf::Color::Transparent);
    rimInner.setOutlineThickness(2.f);
    rimInner.setOutlineColor(sf::Color(255, 255, 255, 110));
    window.draw(rimInner);

    // Hub (layered circles for a subtle 3D feel)
    sf::CircleShape hubOuter(28.f);
    hubOuter.setOrigin(sf::Vector2f(28.f, 28.f));
    hubOuter.setPosition(WHEEL_CENTER);
    hubOuter.setFillColor(Palette::gold);
    window.draw(hubOuter);

    sf::CircleShape hubInner(19.f);
    hubInner.setOrigin(sf::Vector2f(19.f, 19.f));
    hubInner.setPosition(WHEEL_CENTER);
    hubInner.setFillColor(sf::Color(28, 28, 40));
    hubInner.setOutlineThickness(2.f);
    hubInner.setOutlineColor(sf::Color(255, 255, 255, 60));
    window.draw(hubInner);
}

void drawPointer(sf::RenderWindow& window)
{
    sf::Vector2f tip(WHEEL_CENTER.x, WHEEL_CENTER.y - WHEEL_RADIUS + 4.f);

    // shadow
    sf::ConvexShape shadowTri;
    shadowTri.setPointCount(3);
    shadowTri.setPoint(0, tip + sf::Vector2f(2.f, 2.f));
    shadowTri.setPoint(1, sf::Vector2f(tip.x - 20.f, tip.y - 40.f) + sf::Vector2f(2.f, 2.f));
    shadowTri.setPoint(2, sf::Vector2f(tip.x + 20.f, tip.y - 40.f) + sf::Vector2f(2.f, 2.f));
    shadowTri.setFillColor(sf::Color(0, 0, 0, 90));
    window.draw(shadowTri);

    sf::ConvexShape tri;
    tri.setPointCount(3);
    tri.setPoint(0, tip);
    tri.setPoint(1, sf::Vector2f(tip.x - 20.f, tip.y - 40.f));
    tri.setPoint(2, sf::Vector2f(tip.x + 20.f, tip.y - 40.f));
    tri.setFillColor(Palette::gold);
    tri.setOutlineThickness(2.f);
    tri.setOutlineColor(sf::Color(120, 90, 10));
    window.draw(tri);

    sf::CircleShape knob(9.f);
    knob.setOrigin(sf::Vector2f(9.f, 9.f));
    knob.setPosition(sf::Vector2f(tip.x, tip.y - 40.f));
    knob.setFillColor(sf::Color(255, 240, 200));
    knob.setOutlineThickness(2.f);
    knob.setOutlineColor(sf::Color(120, 90, 10));
    window.draw(knob);
}

// ---------------------------------------------------------------------
// Confetti particle system
// ---------------------------------------------------------------------
void spawnConfetti(std::vector<Particle>& particles, sf::Vector2f origin)
{
    for (int i = 0; i < 140; ++i)
    {
        Particle p;
        float angle = toRad((float)(std::rand() % 360));
        float speed = 150.f + (std::rand() % 250);
        p.pos = origin;
        p.vel = sf::Vector2f(std::cos(angle) * speed, std::sin(angle) * speed - 220.f);
        p.color = Palette::slice(std::rand() % 12);
        p.maxLife = 1.6f + (std::rand() % 100) / 100.f;
        p.life = p.maxLife;
        p.size = 4.f + std::rand() % 5;
        p.rotation = (float)(std::rand() % 360);
        p.rotSpeed = -280.f + std::rand() % 560;
        particles.push_back(p);
    }
}

void updateAndDrawConfetti(sf::RenderWindow& window, std::vector<Particle>& particles, float dt)
{
    const float GRAVITY = 420.f;
    for (auto it = particles.begin(); it != particles.end(); )
    {
        it->vel.y += GRAVITY * dt;
        it->pos += it->vel * dt;
        it->rotation += it->rotSpeed * dt;
        it->life -= dt;

        if (it->life <= 0.f) { it = particles.erase(it); continue; }

        float alphaT = std::min(1.f, it->life / (it->maxLife * 0.4f));
        sf::RectangleShape rect(sf::Vector2f(it->size, it->size * 0.5f));
        rect.setOrigin(sf::Vector2f(it->size / 2.f, it->size * 0.25f));
        rect.setPosition(it->pos);
        rect.setRotation(sf::degrees(it->rotation));
        rect.setFillColor(withAlpha(it->color, (std::uint8_t)(255 * alphaT)));
        window.draw(rect);
        ++it;
    }
}

// ---------------------------------------------------------------------
// Winner pop-up
// ---------------------------------------------------------------------
float easeOutBack(float t)
{
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.f;
    float tm1 = t - 1.f;
    return 1.f + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1;
}

// Screen-space rect for the pop-up's little "x" close button, assuming the
// card is at (roughly) full scale. Shared by the drawer and the click test.
sf::FloatRect popupCloseButtonRect()
{
    return sf::FloatRect(
        sf::Vector2f(WINDOW_W / 2.f + POPUP_CARD_W / 2.f - 44.f, WINDOW_H / 2.f - POPUP_CARD_H / 2.f + 12.f),
        sf::Vector2f(30.f, 30.f));
}

// animT goes from 0 (just landed) to 1 (fully settled), driven by a timer
// in the caller. Draws a dimmed overlay + a big bouncy card announcing the
// winner, on top of everything else (including the confetti).
void drawWinnerPopup(sf::RenderWindow& window, const sf::Font& fontBold, const sf::Font& fontRegular,
                      const std::string& winnerName, bool wasRigged, float animT, sf::Vector2f mousePos)
{
    (void)wasRigged; // intentionally not shown anywhere in the UI

    float fadeT = std::min(1.f, animT / 0.5f);
    float scale = easeOutBack(std::min(1.f, animT / 0.55f));

    // dim the whole scene behind the card
    sf::RectangleShape overlay(sf::Vector2f((float)WINDOW_W, (float)WINDOW_H));
    overlay.setFillColor(sf::Color(8, 8, 16, (std::uint8_t)(170 * fadeT)));
    window.draw(overlay);

    sf::Transform transform;
    transform.translate(sf::Vector2f(WINDOW_W / 2.f, WINDOW_H / 2.f));
    transform.scale(sf::Vector2f(scale, scale));
    sf::RenderStates states(transform);

    // card drop shadow
    sf::ConvexShape cardShadow = makeRoundedRect(sf::Vector2f(-POPUP_CARD_W / 2.f + 6.f, -POPUP_CARD_H / 2.f + 10.f),
                                                   sf::Vector2f(POPUP_CARD_W, POPUP_CARD_H), 28.f);
    cardShadow.setFillColor(sf::Color(0, 0, 0, 120));
    window.draw(cardShadow, states);

    // card body
    sf::ConvexShape card = makeRoundedRect(sf::Vector2f(-POPUP_CARD_W / 2.f, -POPUP_CARD_H / 2.f),
                                             sf::Vector2f(POPUP_CARD_W, POPUP_CARD_H), 28.f);
    card.setFillColor(sf::Color(30, 28, 44, 250));
    card.setOutlineThickness(4.f);
    card.setOutlineColor(Palette::cardGold);
    window.draw(card, states);

    // inner accent line
    sf::RectangleShape innerLine(sf::Vector2f(POPUP_CARD_W - 60.f, 2.f));
    innerLine.setOrigin(sf::Vector2f((POPUP_CARD_W - 60.f) / 2.f, 0.f));
    innerLine.setPosition(sf::Vector2f(0.f, -70.f));
    innerLine.setFillColor(withAlpha(Palette::accent, 150));
    window.draw(innerLine, states);

    auto centeredText = [&](const std::string& s, const sf::Font& font, unsigned size,
                             sf::Color color, float yPos, bool bold)
    {
        sf::Text t(font, s, size);
        if (bold) t.setStyle(sf::Text::Bold);
        t.setFillColor(color);
        sf::FloatRect b = t.getLocalBounds();
        t.setOrigin(sf::Vector2f(b.position.x + b.size.x / 2.f, b.position.y + b.size.y / 2.f));
        t.setPosition(sf::Vector2f(0.f, yPos));
        window.draw(t, states);
    };

    centeredText("* WE HAVE A WINNER *", fontBold, 20, Palette::cardGold, -118.f, true);

    // shrink the winner's name automatically if it's a long one
    unsigned nameSize = 46;
    for (unsigned size = 46; size >= 22; size -= 2)
    {
        sf::Text probe(fontBold, winnerName, size);
        if (probe.getLocalBounds().size.x <= POPUP_CARD_W - 80.f) { nameSize = size; break; }
        nameSize = size;
    }
    centeredText(winnerName, fontBold, nameSize, Palette::cardText, -8.f, true);

    centeredText("Spin again whenever you're ready", fontRegular, 15, Palette::cardTextDim, 110.f, false);

    // little round "x" close button, top-right of the card, in screen space
    // (matches popupCloseButtonRect(), used for the click test)
    sf::FloatRect closeRect = popupCloseButtonRect();
    bool hovered = closeRect.contains(mousePos);
    sf::CircleShape closeBtn(15.f);
    closeBtn.setOrigin(sf::Vector2f(15.f, 15.f));
    closeBtn.setPosition(sf::Vector2f(closeRect.position.x + 15.f, closeRect.position.y + 15.f));
    closeBtn.setFillColor(hovered ? sf::Color(70, 68, 84) : sf::Color(50, 48, 64));
    closeBtn.setOutlineThickness(1.5f);
    closeBtn.setOutlineColor(Palette::cardTextDim);
    window.draw(closeBtn);

    sf::Text xMark(fontBold, "x", 16);
    xMark.setFillColor(Palette::cardText);
    sf::FloatRect xb = xMark.getLocalBounds();
    xMark.setOrigin(sf::Vector2f(xb.position.x + xb.size.x / 2.f, xb.position.y + xb.size.y / 2.f));
    xMark.setPosition(sf::Vector2f(closeRect.position.x + 15.f, closeRect.position.y + 15.f));
    window.draw(xMark);
}

// ---------------------------------------------------------------------
// Winner / rigging logic
// ---------------------------------------------------------------------
int winningIndexForRotation(float wheelRotation, size_t n)
{
    if (n == 0) return -1;
    float segAngle = 360.f / (float)n;
    float localAngleAtTop = wrap360(-wheelRotation);
    int idx = (int)(localAngleAtTop / segAngle);
    if (idx >= (int)n) idx = (int)n - 1;
    return idx;
}

float computeRiggedFinalRotation(float currentRotation, size_t n, int targetIndex, int extraSpins)
{
    float segAngle = 360.f / (float)n;
    float desiredLocalAngle = targetIndex * segAngle + segAngle / 2.f;
    float requiredRotationMod360 = wrap360(-desiredLocalAngle);

    float currentBase = currentRotation - std::fmod(currentRotation, 360.f);
    float finalRotation = currentBase + requiredRotationMod360 + 360.f * extraSpins;

    while (finalRotation <= currentRotation)
        finalRotation += 360.f;

    return finalRotation;
}

float computeFairFinalRotation(float currentRotation, int extraSpins)
{
    float randomOffset = (float)(std::rand() % 36000) / 100.f;
    return currentRotation + 360.f * extraSpins + randomOffset;
}

float easeOutCubic(float t)
{
    float f = 1.f - t;
    return 1.f - f * f * f;
}

// ---------------------------------------------------------------------
// Clickable button UI
// ---------------------------------------------------------------------

// Draws a rounded, clickable-looking button with centered label text.
// `filled` = solid color pill (primary actions); false = ghost/outline style
// (secondary actions like Clear All). Handles its own hover/disabled look.
void drawButton(sf::RenderWindow& window, const sf::Font& font, const sf::FloatRect& rect,
                const std::string& label, sf::Color color, bool hovered, bool disabled,
                bool filled, unsigned fontSize = 15)
{
    float radius = std::min(12.f, rect.size.y / 2.f);
    sf::ConvexShape shape = makeRoundedRect(rect.position, rect.size, radius);

    sf::Color textColor;
    if (disabled)
    {
        shape.setFillColor(filled ? Palette::disabledGray : sf::Color::Transparent);
        shape.setOutlineThickness(filled ? 0.f : 1.5f);
        shape.setOutlineColor(Palette::disabledGray);
        textColor = Palette::disabledText;
    }
    else if (filled)
    {
        shape.setFillColor(hovered ? lighten(color, 0.12f) : color);
        shape.setOutlineThickness(0.f);
        textColor = sf::Color::White;
    }
    else
    {
        shape.setFillColor(hovered ? withAlpha(color, 30) : sf::Color::Transparent);
        shape.setOutlineThickness(1.5f);
        shape.setOutlineColor(color);
        textColor = color;
    }
    window.draw(shape);

    sf::Text t(font, label, fontSize);
    t.setStyle(sf::Text::Bold);
    t.setFillColor(textColor);
    sf::FloatRect b = t.getLocalBounds();
    t.setOrigin(sf::Vector2f(b.position.x + b.size.x / 2.f, b.position.y + b.size.y / 2.f));
    t.setPosition(rect.position + rect.size / 2.f);
    window.draw(t);
}

// Small square icon button (used for the per-row delete "x" and the
// confirm/cancel buttons next to the add-contestant text box).
void drawIconButton(sf::RenderWindow& window, const sf::Font& font, const sf::FloatRect& rect,
                    const std::string& icon, sf::Color color, bool hovered)
{
    float radius = std::min(8.f, rect.size.y / 2.f);
    sf::ConvexShape shape = makeRoundedRect(rect.position, rect.size, radius);
    shape.setFillColor(hovered ? withAlpha(color, 55) : withAlpha(color, 30));
    shape.setOutlineThickness(1.2f);
    shape.setOutlineColor(color);
    window.draw(shape);

    sf::Text t(font, icon, (unsigned)(rect.size.y * 0.55f));
    t.setStyle(sf::Text::Bold);
    t.setFillColor(color);
    sf::FloatRect b = t.getLocalBounds();
    t.setOrigin(sf::Vector2f(b.position.x + b.size.x / 2.f, b.position.y + b.size.y / 2.f));
    t.setPosition(rect.position + rect.size / 2.f);
    window.draw(t);
}

// All the clickable areas in the side panel for the current frame, computed
// once up front so mouse clicks and the actual drawing always agree.
struct PanelLayout
{
    sf::FloatRect spinBtn;

    bool addModeActive = false;
    sf::FloatRect addBtn;      // valid when !addModeActive
    sf::FloatRect inputBox;    // valid when addModeActive
    sf::FloatRect confirmBtn;  // valid when addModeActive
    sf::FloatRect cancelBtn;   // valid when addModeActive

    float listStartY = 0.f;
    std::vector<sf::FloatRect> rowRects;
    std::vector<sf::FloatRect> deleteBtns;

    sf::FloatRect clearBtn;
    bool hasClearBtn = false;
};

PanelLayout computePanelLayout(size_t contestantCount, bool addModeActive)
{
    PanelLayout L;
    float innerX = PANEL_X + 16.f;
    float innerW = PANEL_W - 32.f;
    float y = PANEL_Y + 16.f;

    L.spinBtn = sf::FloatRect(sf::Vector2f(innerX, y), sf::Vector2f(innerW, 50.f));
    y += 50.f + 16.f;

    L.addModeActive = addModeActive;
    if (addModeActive)
    {
        float btnSize = 32.f;
        float boxW = innerW - (btnSize + 8.f) * 2.f;
        L.inputBox = sf::FloatRect(sf::Vector2f(innerX, y), sf::Vector2f(boxW, 36.f));
        L.confirmBtn = sf::FloatRect(sf::Vector2f(innerX + boxW + 8.f, y + 2.f), sf::Vector2f(btnSize, btnSize));
        L.cancelBtn = sf::FloatRect(sf::Vector2f(innerX + boxW + 8.f + btnSize + 8.f, y + 2.f), sf::Vector2f(btnSize, btnSize));
        y += 36.f + 16.f;
    }
    else
    {
        L.addBtn = sf::FloatRect(sf::Vector2f(innerX, y), sf::Vector2f(innerW, 42.f));
        y += 42.f + 16.f;
    }

    y += 24.f; // room for the "CONTESTANTS" header, drawn separately
    L.listStartY = y;

    for (size_t i = 0; i < contestantCount; ++i)
    {
        L.rowRects.push_back(sf::FloatRect(sf::Vector2f(innerX, y), sf::Vector2f(innerW, 28.f)));
        L.deleteBtns.push_back(sf::FloatRect(sf::Vector2f(innerX + innerW - 24.f, y + 2.f), sf::Vector2f(22.f, 22.f)));
        y += 28.f + 6.f;
    }

    if (contestantCount > 0)
    {
        y += 8.f;
        L.clearBtn = sf::FloatRect(sf::Vector2f(innerX, y), sf::Vector2f(innerW, 38.f));
        L.hasClearBtn = true;
    }

    return L;
}

// ---------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------
int main()
{
    std::srand((unsigned)std::time(nullptr));

    sf::ContextSettings settings;
    settings.antiAliasingLevel = 8;

    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(WINDOW_W, WINDOW_H)),
                             "Spin The Wheel", sf::Style::Default, sf::State::Windowed, settings);
    window.setFramerateLimit(60);

    sf::Font fontBold, fontRegular;
    bool boldOk = fontBold.openFromFile("Poppins-Bold.ttf")
               || fontBold.openFromFile("arialbd.ttf")
               || fontBold.openFromFile("C:/Windows/Fonts/arialbd.ttf")
               || fontBold.openFromFile("arial.ttf")
               || fontBold.openFromFile("C:/Windows/Fonts/arial.ttf");
    bool regOk = fontRegular.openFromFile("Poppins-Regular.ttf")
              || fontRegular.openFromFile("arial.ttf")
              || fontRegular.openFromFile("C:/Windows/Fonts/arial.ttf");
    if (!regOk) fontRegular = fontBold;
    if (!boldOk) fontBold = fontRegular;

    std::vector<Contestant> contestants = {
        {"Alice",   Palette::slice(0)},
        {"Bob",     Palette::slice(1)},
        {"Charlie", Palette::slice(2)},
        {"Diana",   Palette::slice(3)},
        {"Ethan",   Palette::slice(4)},
        {"Fiona",   Palette::slice(5)}
    };

    AppState  state = AppState::IDLE;
    InputMode inputMode = InputMode::NONE;
    std::string inputBuffer;

    float wheelRotation   = 0.f;
    float spinStartRot    = 0.f;
    float spinTargetRot   = 0.f;
    float spinTimer       = 0.f;
    const float SPIN_DURATION = 4.5f;

    int   winnerIndex = -1;
    bool  wasRigged    = false;
    int   pendingRigTarget = -1;

    std::vector<Particle> particles;
    bool confettiSpawned = false;
    float clockTime = 0.f;
    float resultTimer = 0.f;

    sf::Clock clock;

    // Starts a spin (fair, or rigged if pendingRigTarget is set). Shared by
    // both the Space-bar shortcut and the "SPIN THE WHEEL" button.
    auto startSpin = [&]()
    {
        spinStartRot = wheelRotation;
        int extraSpins = 5 + std::rand() % 4;

        if (pendingRigTarget >= 0 && pendingRigTarget < (int)contestants.size())
        {
            spinTargetRot = computeRiggedFinalRotation(wheelRotation, contestants.size(),
                                                        pendingRigTarget, extraSpins);
            wasRigged = true;
        }
        else
        {
            spinTargetRot = computeFairFinalRotation(wheelRotation, extraSpins);
            wasRigged = false;
        }

        pendingRigTarget = -1;
        spinTimer = 0.f;
        resultTimer = 0.f;
        state = AppState::SPINNING;
        confettiSpawned = false;
        particles.clear();
    };

    // Confirms whatever name is currently typed into the add-contestant box.
    auto confirmAddName = [&]()
    {
        if (!inputBuffer.empty())
        {
            contestants.push_back({ inputBuffer, Palette::slice(contestants.size()) });
            inputBuffer.clear();
        }
        inputMode = InputMode::NONE;
    };

    while (window.isOpen())
    {
        float dt = clock.restart().asSeconds();
        clockTime += dt;

        sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));

        // Compute this frame's clickable layout up front so event handling
        // and drawing always agree on exactly where each button is.
        PanelLayout ui = computePanelLayout(contestants.size(), inputMode == InputMode::ADD_NAME);

        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
                window.close();

            // ---- Typing a contestant name ----
            if (inputMode == InputMode::ADD_NAME)
            {
                if (const auto* textEntered = event->getIf<sf::Event::TextEntered>())
                {
                    if (textEntered->unicode == 8) { if (!inputBuffer.empty()) inputBuffer.pop_back(); }
                    else if (textEntered->unicode == 13) { confirmAddName(); }
                    else if (textEntered->unicode < 128 && textEntered->unicode >= 32)
                    {
                        if (inputBuffer.size() < 20) inputBuffer += (char)textEntered->unicode;
                    }
                }
            }
            // ---- Hidden rig-target typing (keyboard only, no UI trace) ----
            else if (inputMode == InputMode::RIG_INDEX)
            {
                if (const auto* textEntered = event->getIf<sf::Event::TextEntered>())
                {
                    if (textEntered->unicode == 8) { if (!inputBuffer.empty()) inputBuffer.pop_back(); }
                    else if (textEntered->unicode == 13)
                    {
                        if (!inputBuffer.empty() && !contestants.empty())
                        {
                            int idx = std::atoi(inputBuffer.c_str()) - 1;
                            if (idx >= 0 && idx < (int)contestants.size()) pendingRigTarget = idx;
                            inputBuffer.clear();
                            inputMode = InputMode::NONE;
                        }
                    }
                    else if (textEntered->unicode >= '0' && textEntered->unicode <= '9')
                    {
                        if (inputBuffer.size() < 3) inputBuffer += (char)textEntered->unicode;
                    }
                }
            }

            if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>())
            {
                using Key = sf::Keyboard::Key;

                if (keyPressed->code == Key::Escape)
                {
                    inputMode = InputMode::NONE;
                    inputBuffer.clear();
                    if (state == AppState::RESULT)
                        state = AppState::IDLE; // close the winner popup without spinning again
                }
                else if (keyPressed->code == Key::R && inputMode == InputMode::NONE
                         && state != AppState::SPINNING && !contestants.empty())
                {
                    // hidden: rig the next spin by contestant number
                    inputMode = InputMode::RIG_INDEX; inputBuffer.clear();
                }
                else if (keyPressed->code == Key::Space && inputMode == InputMode::NONE
                         && state != AppState::SPINNING && !contestants.empty())
                {
                    startSpin();
                }
            }

            // ---- Mouse clicks on the panel's buttons ----
            if (const auto* mb = event->getIf<sf::Event::MouseButtonPressed>())
            {
                if (mb->button == sf::Mouse::Button::Left)
                {
                    sf::Vector2f click = window.mapPixelToCoords(mb->position);

                    if (state == AppState::RESULT && popupCloseButtonRect().contains(click))
                    {
                        state = AppState::IDLE;
                    }
                    else if (inputMode == InputMode::ADD_NAME)
                    {
                        if (ui.confirmBtn.contains(click)) confirmAddName();
                        else if (ui.cancelBtn.contains(click)) { inputMode = InputMode::NONE; inputBuffer.clear(); }
                    }
                    else if (inputMode == InputMode::NONE)
                    {
                        if (ui.spinBtn.contains(click) && state != AppState::SPINNING && !contestants.empty())
                        {
                            startSpin();
                        }
                        else if (ui.addBtn.contains(click))
                        {
                            inputMode = InputMode::ADD_NAME;
                            inputBuffer.clear();
                        }
                        else if (ui.hasClearBtn && ui.clearBtn.contains(click) && state != AppState::SPINNING)
                        {
                            contestants.clear();
                            pendingRigTarget = -1;
                            winnerIndex = -1;
                            wasRigged = false;
                            state = AppState::IDLE;
                            resultTimer = 0.f;
                            confettiSpawned = false;
                            particles.clear();
                        }
                        else if (state != AppState::SPINNING)
                        {
                            for (size_t i = 0; i < ui.deleteBtns.size(); ++i)
                            {
                                if (ui.deleteBtns[i].contains(click))
                                {
                                    contestants.erase(contestants.begin() + i);
                                    pendingRigTarget = -1; // indices may have shifted
                                    winnerIndex = -1;
                                    if (contestants.empty())
                                    {
                                        state = AppState::IDLE;
                                        resultTimer = 0.f;
                                        confettiSpawned = false;
                                        particles.clear();
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (state == AppState::SPINNING)
        {
            spinTimer += dt;
            float t = spinTimer / SPIN_DURATION;
            if (t >= 1.f)
            {
                t = 1.f;
                wheelRotation = wrap360(spinTargetRot);
                winnerIndex = winningIndexForRotation(wheelRotation, contestants.size());
                state = AppState::RESULT;
            }
            else
            {
                float eased = easeOutCubic(t);
                wheelRotation = spinStartRot + (spinTargetRot - spinStartRot) * eased;
            }
        }

        if (state == AppState::RESULT)
        {
            if (!confettiSpawned)
            {
                spawnConfetti(particles, sf::Vector2f(WHEEL_CENTER.x, WHEEL_CENTER.y - WHEEL_RADIUS - 20.f));
                confettiSpawned = true;
            }
            resultTimer += dt;
        }

        // ---- Draw ----
        drawGradientBackground(window);

        float pulse = (std::sin(clockTime * 3.5f) + 1.f) / 2.f;
        drawGlowRings(window, pulse);

        int highlight = (state == AppState::RESULT) ? winnerIndex : -1;
        drawWheel(window, contestants, wheelRotation, fontBold, fontRegular, highlight, pulse);
        drawPointer(window);

        updateAndDrawConfetti(window, particles, dt);

        // ---- Title ----
        {
            sf::Text title(fontBold, "SPIN THE WHEEL", 34);
            title.setFillColor(Palette::textMain);
            title.setStyle(sf::Text::Bold);
            sf::FloatRect b = title.getLocalBounds();
            title.setOrigin(sf::Vector2f(b.position.x + b.size.x / 2.f, 0.f));
            title.setPosition(sf::Vector2f(WHEEL_CENTER.x, 18.f));
            window.draw(title);

            sf::RectangleShape underline(sf::Vector2f(90.f, 4.f));
            underline.setFillColor(Palette::gold);
            underline.setOrigin(sf::Vector2f(45.f, 0.f));
            underline.setPosition(sf::Vector2f(WHEEL_CENTER.x, 60.f));
            window.draw(underline);
        }

        // ---- Side panel ----
        sf::ConvexShape panelShadow = makeRoundedRect(sf::Vector2f(PANEL_X + 4.f, PANEL_Y + 6.f),
                                                        sf::Vector2f(PANEL_W, PANEL_H), 16.f);
        panelShadow.setFillColor(sf::Color(0, 0, 0, 70));
        window.draw(panelShadow);

        sf::ConvexShape panelBg = makeRoundedRect(sf::Vector2f(PANEL_X, PANEL_Y), sf::Vector2f(PANEL_W, PANEL_H), 16.f);
        panelBg.setFillColor(Palette::panel);
        panelBg.setOutlineThickness(1.5f);
        panelBg.setOutlineColor(Palette::panelBorder);
        window.draw(panelBg);

        // Big spin button - label reflects current state, disabled while spinning or empty
        {
            bool disabled = (state == AppState::SPINNING) || contestants.empty();
            std::string label = (state == AppState::SPINNING) ? "SPINNING..." : "SPIN THE WHEEL";
            bool hovered = ui.spinBtn.contains(mousePos);
            drawButton(window, fontBold, ui.spinBtn, label, Palette::success, hovered, disabled, true, 17);
        }

        // Add-contestant button OR the inline text box + confirm/cancel
        if (ui.addModeActive)
        {
            sf::ConvexShape box = makeRoundedRect(ui.inputBox.position, ui.inputBox.size, 8.f);
            box.setFillColor(sf::Color(245, 245, 250));
            box.setOutlineThickness(1.5f);
            box.setOutlineColor(Palette::accent);
            window.draw(box);

            std::string shown = inputBuffer.empty() ? "Type a name..." : (inputBuffer + "_");
            sf::Text t(fontRegular, shown, 14);
            t.setFillColor(inputBuffer.empty() ? Palette::textDim : Palette::textMain);
            sf::FloatRect b = t.getLocalBounds();
            t.setOrigin(sf::Vector2f(b.position.x, b.position.y + b.size.y / 2.f));
            t.setPosition(sf::Vector2f(ui.inputBox.position.x + 10.f, ui.inputBox.position.y + ui.inputBox.size.y / 2.f));
            window.draw(t);

            drawIconButton(window, fontBold, ui.confirmBtn, "OK", Palette::success, ui.confirmBtn.contains(mousePos));
            drawIconButton(window, fontBold, ui.cancelBtn, "x", Palette::danger, ui.cancelBtn.contains(mousePos));
        }
        else
        {
            drawButton(window, fontBold, ui.addBtn, "+ Add Contestant", Palette::accent,
                       ui.addBtn.contains(mousePos), false, true, 15);
        }

        // "CONTESTANTS" header, just above the list
        {
            sf::Text header(fontBold, "CONTESTANTS", 14);
            header.setFillColor(Palette::accent);
            header.setStyle(sf::Text::Bold);
            header.setPosition(sf::Vector2f(PANEL_X + 18.f, ui.listStartY - 22.f));
            window.draw(header);
        }

        // Contestant rows, each with a colored dot, name, and its own "x" button
        for (size_t i = 0; i < contestants.size(); ++i)
        {
            const sf::FloatRect& row = ui.rowRects[i];

            sf::CircleShape dot(5.f);
            dot.setPosition(sf::Vector2f(row.position.x, row.position.y + row.size.y / 2.f - 5.f));
            dot.setFillColor(contestants[i].color);
            window.draw(dot);

            std::ostringstream oss;
            oss << (i + 1) << ". " << contestants[i].name;
            sf::Text t(fontRegular, oss.str(), 14);
            t.setFillColor(Palette::textMain);
            sf::FloatRect tb = t.getLocalBounds();
            t.setOrigin(sf::Vector2f(tb.position.x, tb.position.y + tb.size.y / 2.f));
            t.setPosition(sf::Vector2f(row.position.x + 16.f, row.position.y + row.size.y / 2.f));
            window.draw(t);

            drawIconButton(window, fontBold, ui.deleteBtns[i], "x", Palette::danger,
                           ui.deleteBtns[i].contains(mousePos));
        }

        if (contestants.empty())
        {
            sf::Text t(fontRegular, "No contestants yet.", 13);
            t.setFillColor(Palette::textDim);
            t.setPosition(sf::Vector2f(PANEL_X + 18.f, ui.listStartY));
            window.draw(t);
        }

        // Clear-all button, right after the list
        if (ui.hasClearBtn)
        {
            drawButton(window, fontBold, ui.clearBtn, "Clear All", Palette::danger,
                       ui.clearBtn.contains(mousePos), state == AppState::SPINNING, false, 14);
        }

        // ---- Big winner pop-up (drawn last, on top of everything) ----
        if (state == AppState::RESULT && winnerIndex >= 0 && winnerIndex < (int)contestants.size())
        {
            drawWinnerPopup(window, fontBold, fontRegular, contestants[winnerIndex].name, wasRigged, resultTimer, mousePos);
        }

        window.display();
    }

    return 0;
}


// Spin the Wheel - SFML graphical version
// Build: g++ -std=c++17 spin_wheel_gfx.cpp -o spin_wheel_gfx -lsfml-graphics -lsfml-window -lsfml-system
// Run:   ./spin_wheel_gfx
//
// Controls:
//   SPACE       -> spin the wheel
//   ESC         -> quit

#include <SFML/Graphics.hpp>
#include <vector>
#include <string>
#include <cmath>
#include <random>
#include <sstream>

// ---- EDIT YOUR WHEEL OPTIONS HERE ----
static const std::vector<std::string> OPTIONS = {
    "Pizza", "Burger", "Sushi", "Tacos", "Ramen", "Salad"
};
// ---------------------------------------

const float PI = 3.14159265358979323846f;
const int   WINDOW_SIZE   = 700;
const float WHEEL_RADIUS  = 260.f;
const sf::Vector2f CENTER(WINDOW_SIZE / 2.f, WINDOW_SIZE / 2.f);

// A palette that cycles if there are more options than colors
std::vector<sf::Color> palette = {
    sf::Color(231, 76, 60),   // red
    sf::Color(52, 152, 219),  // blue
    sf::Color(241, 196, 15),  // yellow
    sf::Color(46, 204, 113),  // green
    sf::Color(155, 89, 182),  // purple
    sf::Color(230, 126, 34),  // orange
    sf::Color(26, 188, 156),  // teal
    sf::Color(236, 64, 122)   // pink
};

bool loadFontFromCommonPaths(sf::Font& font) {
    std::vector<std::string> candidates = {
        "font.ttf",                                                   // same folder as exe
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",       // Linux
        "C:/Windows/Fonts/arialbd.ttf",                                // Windows
        "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf"           // macOS
    };
    for (const auto& path : candidates) {
        if (font.loadFromFile(path)) return true;
    }
    return false;
}

int main() {
    sf::RenderWindow window(sf::VideoMode(WINDOW_SIZE, WINDOW_SIZE), "Spin the Wheel",
                             sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(60);

    sf::Font font;
    bool hasFont = loadFontFromCommonPaths(font);
    if (!hasFont) {
        // We can still run without labels, but warn in the title bar
        window.setTitle("Spin the Wheel (no font.ttf found - labels disabled)");
    }

    size_t n = OPTIONS.size();
    float sliceAngle = 360.f / n;

    // Build wheel as a triangle fan (one ConvexShape per slice)
    std::vector<sf::ConvexShape> slices(n);
    for (size_t i = 0; i < n; ++i) {
        sf::ConvexShape slice;
        slice.setPointCount(3);
        slice.setPoint(0, sf::Vector2f(0.f, 0.f));

        float a1 = (i * sliceAngle - 90.f) * PI / 180.f; // -90 so slice 0 starts at top
        float a2 = ((i + 1) * sliceAngle - 90.f) * PI / 180.f;

        slice.setPoint(1, sf::Vector2f(std::cos(a1) * WHEEL_RADIUS, std::sin(a1) * WHEEL_RADIUS));
        slice.setPoint(2, sf::Vector2f(std::cos(a2) * WHEEL_RADIUS, std::sin(a2) * WHEEL_RADIUS));
        slice.setFillColor(palette[i % palette.size()]);
        slices[i] = slice;
    }

    // Labels, positioned mid-slice, rotated to follow the wedge
    std::vector<sf::Text> labels;
    if (hasFont) {
        for (size_t i = 0; i < n; ++i) {
            sf::Text text(OPTIONS[i], font, 20);
            text.setFillColor(sf::Color::White);
            text.setStyle(sf::Text::Bold);
            sf::FloatRect bounds = text.getLocalBounds();
            text.setOrigin(bounds.width / 2.f, bounds.height / 2.f);

            float mid = (i * sliceAngle + sliceAngle / 2.f - 90.f) * PI / 180.f;
            float labelRadius = WHEEL_RADIUS * 0.62f;
            text.setPosition(std::cos(mid) * labelRadius, std::sin(mid) * labelRadius);
            text.setRotation(i * sliceAngle + sliceAngle / 2.f);
            labels.push_back(text);
        }
    }

    // Outer rim
    sf::CircleShape rim(WHEEL_RADIUS + 6.f);
    rim.setOrigin(WHEEL_RADIUS + 6.f, WHEEL_RADIUS + 6.f);
    rim.setFillColor(sf::Color::Transparent);
    rim.setOutlineColor(sf::Color(44, 62, 80));
    rim.setOutlineThickness(6.f);

    // Center hub
    sf::CircleShape hub(22.f);
    hub.setOrigin(22.f, 22.f);
    hub.setFillColor(sf::Color(44, 62, 80));

    // Pointer (triangle at the top, pointing down into the wheel)
    sf::ConvexShape pointer;
    pointer.setPointCount(3);
    pointer.setPoint(0, sf::Vector2f(-16.f, -30.f));
    pointer.setPoint(1, sf::Vector2f(16.f, -30.f));
    pointer.setPoint(2, sf::Vector2f(0.f, 6.f));
    pointer.setFillColor(sf::Color(231, 76, 60));
    pointer.setOutlineColor(sf::Color::Black);
    pointer.setOutlineThickness(2.f);
    pointer.setPosition(CENTER.x, CENTER.y - WHEEL_RADIUS);

    // Prompt text
    sf::Text prompt;
    sf::Text resultText;
    if (hasFont) {
        prompt.setFont(font);
        prompt.setString("Press SPACE to spin");
        prompt.setCharacterSize(22);
        prompt.setFillColor(sf::Color(44, 62, 80));
        sf::FloatRect pb = prompt.getLocalBounds();
        prompt.setOrigin(pb.width / 2.f, 0.f);
        prompt.setPosition(CENTER.x, WINDOW_SIZE - 40.f);

        resultText.setFont(font);
        resultText.setCharacterSize(28);
        resultText.setStyle(sf::Text::Bold);
        resultText.setFillColor(sf::Color(39, 174, 96));
    }

    // Spin state
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> extraSpins(3.f, 6.f); // full rotations
    std::uniform_int_distribution<int> pickWinner(0, (int)n - 1);

    bool spinning = false;
    float currentRotation = 0.f;
    float targetRotation = 0.f;
    float angularVelocity = 0.f;
    int winnerIndex = -1;
    bool showResult = false;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
            if (event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::Escape)
                    window.close();
                if (event.key.code == sf::Keyboard::Space && !spinning) {
                    showResult = false;
                    winnerIndex = pickWinner(gen);

                    // Compute target so that the winner slice's midpoint lands under the pointer (top, i.e. -90deg / 270deg from wheel-local 0)
                    float winnerMid = winnerIndex * sliceAngle + sliceAngle / 2.f;
                    float fullSpins = extraSpins(gen) * 360.f;

                    // We want: (currentRotation + delta) such that the wheel's winnerMid slice sits at the top.
                    // Wheel rotates clockwise by 'rotation' degrees; slice i's current angular position (top-relative) is winnerMid + rotation.
                    // We need winnerMid + finalRotation ≡ 0 (mod 360), pointer is fixed at top.
                    float finalRotationMod = std::fmod(360.f - winnerMid, 360.f);
                    float base = std::fmod(currentRotation, 360.f);
                    float delta = std::fmod((finalRotationMod - base) + 360.f, 360.f);

                    targetRotation = currentRotation + fullSpins + delta;
                    spinning = true;
                }
            }
        }

        if (spinning) {
            float remaining = targetRotation - currentRotation;
            // Ease-out: speed proportional to remaining distance, with floor/ceiling
            angularVelocity = std::max(2.5f, remaining * 0.045f);
            if (angularVelocity > remaining) angularVelocity = remaining;
            currentRotation += angularVelocity;

            if (remaining <= 0.05f) {
                currentRotation = targetRotation;
                spinning = false;
                showResult = true;
                if (hasFont) {
                    resultText.setString("Winner: " + OPTIONS[winnerIndex] + "!");
                    sf::FloatRect rb = resultText.getLocalBounds();
                    resultText.setOrigin(rb.width / 2.f, 0.f);
                    resultText.setPosition(CENTER.x, 20.f);
                }
            }
        }

        window.clear(sf::Color(236, 240, 241));

        // Draw wheel group (slices + labels) rotated around CENTER
        sf::Transform t;
        t.translate(CENTER);
        t.rotate(std::fmod(currentRotation, 360.f));

        for (auto& s : slices) window.draw(s, t);
        if (hasFont)
            for (auto& l : labels) window.draw(l, t);

        window.draw(rim);
        window.draw(hub);
        window.draw(pointer);

        if (hasFont) {
            if (!spinning) window.draw(prompt);
            if (showResult) window.draw(resultText);
        }

        window.display();
    }

    return 0;
}
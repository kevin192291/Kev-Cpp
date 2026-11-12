---
order: 11
section: drawing
description: Opens a font from assets/ and makes centred, outlined text with it
packages: sfml-graphics
sfml: 3
includes: <SFML/Graphics.hpp> <iostream>
---
// Before the game loop. sf::Text keeps a reference to its font: keep the font alive.
// Any .ttf or .otf works, e.g. a copy of /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf on Ubuntu.
sf::Font font;
if (!font.openFromFile("assets/font.ttf"))
{
    std::cerr << "Can't open assets/font.ttf (paths are relative to the working directory)\n";
    return 1;
}

sf::Text text(font, "Score: 0", 32);
text.setFillColor(sf::Color::White);
text.setOutlineColor(sf::Color::Black);
text.setOutlineThickness(2.f);
text.setStyle(sf::Text::Bold);

// Centre it on a point. The local bounds don't start at (0, 0): fonts leave space above the letters.
const sf::FloatRect bounds = text.getLocalBounds();
text.setOrigin(bounds.position + bounds.size / 2.f);
text.setPosition({400.f, 50.f});
{{cursor}}
// Later: text.setString("Score: " + std::to_string(score));
// In the loop: window.draw(text);

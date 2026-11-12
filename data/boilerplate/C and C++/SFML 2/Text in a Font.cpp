---
order: 11
description: Loads a font and writes a text with it (SFML 2), for main() before the loop
includes: <SFML/Graphics.hpp>
packages: sfml-graphics
sfml: 2
---
// A font has to live as long as the texts written in it.
sf::Font font;
if (!font.loadFromFile("font.ttf"))
{
    return 1;
}
sf::Text text("Hello", font, 24);
text.setFillColor(sf::Color::White);
text.setPosition(20.f, 20.f);
// In the loop, between clear() and display(): window.draw(text);

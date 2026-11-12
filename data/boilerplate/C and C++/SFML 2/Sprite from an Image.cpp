---
order: 10
description: Loads a texture and shows it as a sprite (SFML 2), for main() before the loop
includes: <SFML/Graphics.hpp>
packages: sfml-graphics
sfml: 2
---
// A texture has to live as long as the sprites that show it.
sf::Texture texture;
if (!texture.loadFromFile("image.png"))
{
    return 1;
}
sf::Sprite sprite(texture);
sprite.setPosition(100.f, 100.f);
// In the loop, between clear() and display(): window.draw(sprite);

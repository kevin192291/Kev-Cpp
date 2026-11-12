---
order: 10
section: drawing
description: Loads a texture from assets/ and makes a centred, scaled, rotated sprite from it
packages: sfml-graphics
sfml: 3
includes: <SFML/Graphics.hpp> <iostream>
---
// Before the game loop. The texture must outlive every sprite that shows it.
// Relative paths start from the working directory: Kev-C++ runs programs in the project folder.
sf::Texture texture;
if (!texture.loadFromFile("assets/player.png"))
{
    std::cerr << "Can't load assets/player.png (paths are relative to the working directory)\n";
    return 1;
}
texture.setSmooth(true); // false keeps pixel art crisp

sf::Sprite sprite(texture);
sprite.setOrigin(sf::Vector2f(texture.getSize()) / 2.f); // position, rotation and scale act on the centre
sprite.setPosition({400.f, 300.f});
sprite.setScale({2.f, 2.f});
sprite.setRotation(sf::degrees(15.f));
// Show part of the texture only (a sprite sheet frame): sprite.setTextureRect({{0, 0}, {32, 32}});
{{cursor}}
// In the loop: window.draw(sprite);

---
order: 14
section: drawing
description: Draws into an off-screen texture once and shows it with a sprite (backgrounds, minimaps, effects)
packages: sfml-graphics
sfml: 3
includes: <SFML/Graphics.hpp>
---
// Before the game loop. Drawing into a texture once is cheaper than redrawing many things every frame.
sf::RenderTexture canvas({400, 300});
canvas.clear(sf::Color(20, 30, 60));

sf::CircleShape star(3.f);
star.setFillColor(sf::Color(255, 255, 200));
for (int i = 0; i < 100; ++i)
{
    star.setPosition({static_cast<float>((i * 97) % 400), static_cast<float>((i * 57) % 300)});
    canvas.draw(star);
}
{{cursor}}
canvas.display(); // finishes drawing; without it the texture isn't updated

// The sprite shows the canvas's texture, so the canvas must live as long as the sprite.
sf::Sprite canvasSprite(canvas.getTexture());
canvasSprite.setPosition({200.f, 150.f});
// In the loop: window.draw(canvasSprite);
// Draw into it again any time: canvas.clear(); canvas.draw(...); canvas.display();

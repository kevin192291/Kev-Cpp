---
order: 20
section: movement
description: A function that moves a shape or sprite with WASD or the arrow keys at a steady speed on any machine
packages: sfml-graphics
sfml: 3
includes: <SFML/Graphics.hpp>
---
// Moves a shape, sprite or text with WASD or the arrow keys, at the same speed whatever the frame rate.
// Each frame:  const float dt = clock.restart().asSeconds();  movePlayer(player, dt);
void movePlayer(sf::Transformable& player, float dt)
{
    constexpr float speed = 300.f; // pixels per second

    using Key = sf::Keyboard::Key;
    const auto held = [](Key a, Key b) { return sf::Keyboard::isKeyPressed(a) || sf::Keyboard::isKeyPressed(b); };

    sf::Vector2f direction;
    if (held(Key::A, Key::Left))
    {
        direction.x -= 1.f;
    }
    if (held(Key::D, Key::Right))
    {
        direction.x += 1.f;
    }
    if (held(Key::W, Key::Up))
    {
        direction.y -= 1.f;
    }
    if (held(Key::S, Key::Down))
    {
        direction.y += 1.f;
    }

    // Normalised, or moving diagonally would be about 1.4 times faster.
    if (direction != sf::Vector2f())
    {
        player.move(direction.normalized() * speed * dt);
    }
    {{cursor}}
}

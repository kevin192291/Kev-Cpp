---
order: 2
description: An SFML 2 game loop: a shape moved with the arrow keys at the same speed at any frame rate
file: main.cpp
whole-file: yes
packages: sfml-graphics
sfml: 2
---
#include <SFML/Graphics.hpp>

int main()
{
    sf::RenderWindow window(sf::VideoMode(800, 600), "My Game");
    window.setFramerateLimit(60);

    sf::CircleShape player(30.f);
    player.setFillColor(sf::Color::Green);
    player.setPosition(370.f, 270.f);
    const float speed = 300.f; // pixels per second

    sf::Clock clock;
    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
            {
                window.close();
            }
            else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape)
            {
                window.close();
            }
        }

        // The time since the last frame, so the speed is the same at any frame rate.
        const float dt = clock.restart().asSeconds();

        sf::Vector2f direction;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Left))
        {
            direction.x -= 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Right))
        {
            direction.x += 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Up))
        {
            direction.y -= 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Down))
        {
            direction.y += 1.f;
        }
        player.move(direction * speed * dt);

        window.clear(sf::Color::Black);
        window.draw(player);
        window.display();
    }

    return 0;
}

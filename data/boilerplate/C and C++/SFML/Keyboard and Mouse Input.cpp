---
order: 5
section: programs
description: Keyboard and mouse handling both ways: events for single presses, real-time checks for held keys
file: main.cpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// Input two ways. Events report things once (a key goes down, a click, the wheel, typed text); real-time
// checks ask whether a key or button is held right now, which suits movement.
#include <SFML/Graphics.hpp>

int main()
{
    sf::RenderWindow window(sf::VideoMode({800, 600}), "Input");
    window.setFramerateLimit(60);

    sf::RectangleShape player({50.f, 50.f});
    player.setOrigin({25.f, 25.f});
    player.setPosition({400.f, 300.f});
    player.setFillColor(sf::Color(80, 160, 255));

    sf::String typed;
    sf::Clock clock;

    while (window.isOpen())
    {
        // Events: one per thing that happened since the last frame.
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
            else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
            {
                if (key->code == sf::Keyboard::Key::Escape)
                {
                    window.close();
                }
                else if (key->code == sf::Keyboard::Key::R && key->control)
                {
                    player.setPosition({400.f, 300.f});
                }
            }
            else if (const auto* click = event->getIf<sf::Event::MouseButtonPressed>())
            {
                if (click->button == sf::Mouse::Button::Left)
                {
                    // Pixels → world coordinates: they differ once the view scrolls or zooms.
                    player.setPosition(window.mapPixelToCoords(click->position));
                }
            }
            else if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>())
            {
                const float factor = wheel->delta > 0.f ? 1.1f : 0.9f;
                player.setScale(player.getScale() * factor);
            }
            else if (const auto* text = event->getIf<sf::Event::TextEntered>())
            {
                // Typed characters with the keyboard layout applied (for names and chat), not keys.
                if (text->unicode == U'\b')
                {
                    if (!typed.isEmpty())
                    {
                        typed.erase(typed.getSize() - 1);
                    }
                }
                else if (text->unicode >= U' ')
                {
                    typed += text->unicode;
                }
                window.setTitle("Input - typed: " + typed);
            }
        }

        // Real-time state: held keys move the player smoothly at a speed independent of the frame rate.
        const float dt = clock.restart().asSeconds();
        sf::Vector2f direction;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left))
        {
            direction.x -= 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right))
        {
            direction.x += 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up))
        {
            direction.y -= 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down))
        {
            direction.y += 1.f;
        }
        if (direction != sf::Vector2f())
        {
            player.move(direction.normalized() * 300.f * dt);
        }

        // Holding the right button drags the player to the mouse.
        if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Right))
        {
            player.setPosition(window.mapPixelToCoords(sf::Mouse::getPosition(window)));
        }
        {{cursor}}
        window.clear(sf::Color(30, 30, 40));
        window.draw(player);
        window.display();
    }
}

---
order: 3
section: programs
description: A game class that owns the window and runs events, update and render; main() just calls run()
file: {{ClassName}}.hpp
whole-file: yes
ask: ClassName = Game
packages: sfml-graphics
sfml: 3
---
// The game: run() handles events, updates by the frame's time and draws until the window closes.
// main.cpp:  #include "{{ClassName}}.hpp"   int main() { {{ClassName}} game; game.run(); }
#pragma once

#include <SFML/Graphics.hpp>

class {{ClassName}}
{
public:
    {{ClassName}}() : window_(sf::VideoMode({1280, 720}), "{{ClassName}}")
    {
        window_.setFramerateLimit(60);

        player_.setOrigin({20.f, 20.f});
        player_.setPosition({640.f, 360.f});
        player_.setFillColor(sf::Color(80, 160, 255));
    }

    void run()
    {
        sf::Clock clock;
        while (window_.isOpen())
        {
            processEvents();
            update(clock.restart().asSeconds());
            render();
        }
    }

private:
    void processEvents()
    {
        while (const std::optional event = window_.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window_.close();
            }
            else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
            {
                if (key->code == sf::Keyboard::Key::Escape)
                {
                    window_.close();
                }
            }
        }
    }

    // dt: seconds since the last frame.
    void update(float dt)
    {
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
            player_.move(direction.normalized() * PlayerSpeed * dt);
        }
        {{cursor}}
    }

    void render()
    {
        window_.clear(sf::Color(30, 30, 40));
        window_.draw(player_);
        window_.display();
    }

    static constexpr float PlayerSpeed = 300.f; // pixels per second

    sf::RenderWindow window_;
    sf::CircleShape player_{20.f};
};

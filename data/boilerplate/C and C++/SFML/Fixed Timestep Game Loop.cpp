---
order: 2
section: programs
description: A game loop that updates in fixed 1/60 s steps and draws in between, so physics runs the same everywhere
file: main.cpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// A fixed-timestep loop: the game always advances in steps of exactly 1/60 s, however fast frames are drawn,
// so movement and physics behave the same on every machine.
#include <SFML/Graphics.hpp>

#include <algorithm>

namespace
{
const sf::Time TimePerUpdate = sf::seconds(1.f / 60.f);
const sf::Vector2f WindowSize{800.f, 600.f};
}

int main()
{
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(WindowSize)), "Fixed Timestep");
    window.setVerticalSyncEnabled(true);

    sf::RectangleShape box({40.f, 40.f});
    box.setFillColor(sf::Color(255, 160, 0));

    sf::Vector2f position{100.f, 280.f};
    sf::Vector2f previousPosition = position;
    sf::Vector2f velocity{300.f, 0.f};

    sf::Clock clock;
    sf::Time accumulator = sf::Time::Zero;

    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
        }

        // Capped, so a long stall (dragging the window) doesn't run hundreds of catch-up steps.
        accumulator += std::min(clock.restart(), sf::seconds(0.25f));

        while (accumulator >= TimePerUpdate)
        {
            const float dt = TimePerUpdate.asSeconds();
            previousPosition = position;

            position += velocity * dt;
            if (position.x < 0.f || position.x > WindowSize.x - box.getSize().x)
            {
                position.x = std::clamp(position.x, 0.f, WindowSize.x - box.getSize().x);
                velocity.x = -velocity.x;
            }
            {{cursor}}
            accumulator -= TimePerUpdate;
        }

        // Draw between the last two steps so motion stays smooth when frames and steps don't line up.
        const float alpha = accumulator / TimePerUpdate;
        box.setPosition(previousPosition + (position - previousPosition) * alpha);

        window.clear();
        window.draw(box);
        window.display();
    }
}

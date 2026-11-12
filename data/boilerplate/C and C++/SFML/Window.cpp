---
order: 1
section: programs
description: An SFML 3 window with an event loop that clears, draws and displays 60 times a second
file: main.cpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// An SFML 3 program: each frame handles the window's events, then clears, draws and displays.
#include <SFML/Graphics.hpp>

int main()
{
    sf::RenderWindow window(sf::VideoMode({800, 600}), "SFML Window");
    window.setFramerateLimit(60);

    sf::CircleShape circle(50.f);
    circle.setOrigin({50.f, 50.f});
    circle.setPosition({400.f, 300.f});
    circle.setFillColor(sf::Color(100, 200, 120));

    while (window.isOpen())
    {
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
            }
        }
        {{cursor}}
        window.clear(sf::Color(30, 30, 40));
        window.draw(circle);
        window.display();
    }
}

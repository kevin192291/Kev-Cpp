---
order: 1
description: An SFML 2 window with an event loop and a frame rate limit
file: main.cpp
whole-file: yes
packages: sfml-graphics
sfml: 2
---
#include <SFML/Graphics.hpp>

int main()
{
    // sf::RenderWindow is the window you can draw to (sf::Window alone has no clear() or draw()).
    sf::RenderWindow window(sf::VideoMode(800, 600), "My Window");
    window.setFramerateLimit(60);

    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
            {
                window.close();
            }
        }

        window.clear(sf::Color::Black);
        // Draw here: window.draw(...);
        window.display();
    }

    return 0;
}

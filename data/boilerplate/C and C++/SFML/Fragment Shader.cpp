---
order: 15
section: drawing
description: A program that draws an animated fragment shader over the whole window, passing time and size
file: main.cpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// A fragment shader drawn over the whole window. It loads shaders/plasma.frag: save Boilerplate → SFML →
// Shaders → Plasma under that name in the project folder (Kev-C++ runs programs there).
#include <SFML/Graphics.hpp>

#include <iostream>

int main()
{
    sf::RenderWindow window(sf::VideoMode({800, 600}), "Fragment Shader");
    window.setFramerateLimit(60);

    if (!sf::Shader::isAvailable())
    {
        std::cerr << "This graphics driver doesn't support shaders\n";
        return 1;
    }
    sf::Shader shader;
    if (!shader.loadFromFile("shaders/plasma.frag", sf::Shader::Type::Fragment))
    {
        std::cerr << "Can't load shaders/plasma.frag (paths are relative to the working directory)\n";
        return 1;
    }

    // The shader runs for every pixel of whatever is drawn with it: here a rectangle covering the window.
    sf::RectangleShape screen(sf::Vector2f(window.getSize()));
    sf::Clock clock;

    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
            else if (const auto* resized = event->getIf<sf::Event::Resized>())
            {
                // Show the new area 1:1 instead of stretching the old one.
                const sf::Vector2f size(resized->size);
                window.setView(sf::View(sf::FloatRect({0.f, 0.f}, size)));
                screen.setSize(size);
            }
        }

        shader.setUniform("time", clock.getElapsedTime().asSeconds());
        shader.setUniform("resolution", sf::Vector2f(window.getSize()));
        {{cursor}}
        window.clear();
        window.draw(screen, &shader);
        window.display();
    }
}

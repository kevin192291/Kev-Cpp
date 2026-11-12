---
order: 13
section: drawing
description: A class made of shapes that window.draw() can draw and that moves, rotates and scales as one
file: {{ClassName}}.hpp
whole-file: yes
ask: ClassName = Entity
packages: sfml-graphics
sfml: 3
---
// Draw it with window.draw(object); move it with setPosition, move, setRotation and setScale like a sprite.
#pragma once

#include <SFML/Graphics.hpp>

class {{ClassName}} : public sf::Drawable, public sf::Transformable
{
public:
    {{ClassName}}()
    {
        // Parts are placed around (0, 0), the object's own origin.
        body_.setSize({60.f, 40.f});
        body_.setOrigin(body_.getSize() / 2.f);
        body_.setFillColor(sf::Color(80, 160, 255));

        eye_.setOrigin({6.f, 6.f});
        eye_.setPosition({15.f, -5.f});
        eye_.setFillColor(sf::Color::White);
        {{cursor}}
    }

    void update(sf::Time dt)
    {
        rotate(sf::degrees(45.f * dt.asSeconds()));
    }

private:
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override
    {
        // Adding this object's transform makes every part follow its position, rotation and scale.
        states.transform *= getTransform();
        target.draw(body_, states);
        target.draw(eye_, states);
    }

    sf::RectangleShape body_;
    sf::CircleShape eye_{6.f};
};

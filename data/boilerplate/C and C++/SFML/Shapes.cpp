---
order: 12
section: drawing
description: A circle, a rotated rectangle, a triangle, a convex polygon and a line, ready to draw
packages: sfml-graphics
sfml: 3
includes: <SFML/Graphics.hpp>
---
sf::CircleShape circle(40.f); // radius
circle.setPosition({80.f, 80.f});
circle.setFillColor(sf::Color(100, 200, 120));
circle.setOutlineThickness(3.f);
circle.setOutlineColor(sf::Color::White);

sf::RectangleShape rectangle({120.f, 60.f});
rectangle.setOrigin(rectangle.getSize() / 2.f); // rotate around the centre, not the top-left corner
rectangle.setPosition({400.f, 120.f});
rectangle.setRotation(sf::degrees(30.f));
rectangle.setFillColor(sf::Color(220, 80, 80));

sf::CircleShape triangle(50.f, 3); // a "circle" with 3 points; 6 makes a hexagon
triangle.setPosition({600.f, 70.f});
triangle.setFillColor(sf::Color(240, 200, 60));

// Points go round the outline in order, and the shape must be convex.
sf::ConvexShape kite(4);
kite.setPoint(0, {0.f, 0.f});
kite.setPoint(1, {40.f, 50.f});
kite.setPoint(2, {0.f, 140.f});
kite.setPoint(3, {-40.f, 50.f});
kite.setPosition({200.f, 300.f});
kite.setFillColor(sf::Color(160, 100, 220));

const sf::Vertex line[] = {
    {{100.f, 500.f}, sf::Color::Cyan},
    {{700.f, 540.f}, sf::Color::Magenta},
};
{{cursor}}
// In the loop:
// window.draw(circle);
// window.draw(rectangle);
// window.draw(triangle);
// window.draw(kite);
// window.draw(line, 2, sf::PrimitiveType::Lines);

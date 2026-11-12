---
order: 40
section: collision
description: Box collision functions: whether two bounding boxes overlap, and how far to push one out of the other
packages: sfml-graphics
sfml: 3
includes: <SFML/Graphics.hpp>
---
// Axis-aligned box collision, for getGlobalBounds() of shapes, sprites and text (unrotated ones: a rotated
// object's bounds are the box around it, larger than the object).
bool boxesOverlap(const sf::FloatRect& a, const sf::FloatRect& b)
{
    return a.findIntersection(b).has_value();
}

// How far to move box a so it stops overlapping b, along the axis where they overlap least; (0, 0) if
// they don't overlap. For walls:  player.move(pushOut(player.getGlobalBounds(), wall.getGlobalBounds()));
sf::Vector2f pushOut(const sf::FloatRect& a, const sf::FloatRect& b)
{
    const std::optional<sf::FloatRect> overlap = a.findIntersection(b);
    if (!overlap)
    {
        return {};
    }
    if (overlap->size.x < overlap->size.y)
    {
        return {a.getCenter().x < b.getCenter().x ? -overlap->size.x : overlap->size.x, 0.f};
    }
    return {0.f, a.getCenter().y < b.getCenter().y ? -overlap->size.y : overlap->size.y};
}

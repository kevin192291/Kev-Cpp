---
order: 41
section: collision
description: Circle collision functions: circle against circle, circle against box, and bouncing two balls apart
packages: sfml-graphics
sfml: 3
includes: <SFML/Graphics.hpp> <algorithm>
---
// Circle collision. A sf::CircleShape's centre is getPosition() once its origin is set
// to {radius, radius}.

// Two circles touch when their centres are closer than the sum of the radii (compared squared: no sqrt).
bool circlesOverlap(sf::Vector2f centerA, float radiusA, sf::Vector2f centerB, float radiusB)
{
    const float reach = radiusA + radiusB;
    return (centerB - centerA).lengthSquared() < reach * reach;
}

// A circle touches a box when the box's closest point to the centre is inside the circle.
bool circleOverlapsBox(sf::Vector2f center, float radius, const sf::FloatRect& box)
{
    const sf::Vector2f closest{std::clamp(center.x, box.position.x, box.position.x + box.size.x),
                               std::clamp(center.y, box.position.y, box.position.y + box.size.y)};
    return (center - closest).lengthSquared() < radius * radius;
}

// Separates two overlapping balls of the same size and mass and bounces them off each other (an elastic
// collision: they swap their speeds along the line between the centres).
void bounceBalls(sf::Vector2f& centerA, sf::Vector2f& velocityA, sf::Vector2f& centerB, sf::Vector2f& velocityB,
                 float radius)
{
    const sf::Vector2f delta = centerB - centerA;
    const float distance = delta.length();
    if (distance >= 2.f * radius || distance == 0.f)
    {
        return;
    }
    const sf::Vector2f normal = delta / distance;

    // Move each half the overlap apart so they don't stick together next frame.
    const float overlap = 2.f * radius - distance;
    centerA -= normal * (overlap / 2.f);
    centerB += normal * (overlap / 2.f);

    const float approach = (velocityA - velocityB).dot(normal);
    if (approach > 0.f)
    {
        velocityA -= normal * approach;
        velocityB += normal * approach;
    }
}

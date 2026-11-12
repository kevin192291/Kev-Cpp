---
order: 22
section: movement
description: A function that keeps the game's aspect ratio when the window is resized, with black bars
packages: sfml-graphics
sfml: 3
includes: <SFML/Graphics.hpp>
---
// A view that shows the whole world at its own aspect ratio, centred with black bars on the sides or
// top and bottom, whatever shape the window is. Use it at start-up and on every resize:
//   window.setView(letterboxView({800.f, 600.f}, window.getSize()));
//   On sf::Event::Resized:  window.setView(letterboxView({800.f, 600.f}, resized->size));
sf::View letterboxView(sf::Vector2f worldSize, sf::Vector2u windowSize)
{
    sf::View view(worldSize / 2.f, worldSize);

    const float windowRatio = static_cast<float>(windowSize.x) / static_cast<float>(windowSize.y);
    const float worldRatio = worldSize.x / worldSize.y;

    // The viewport is the part of the window the view fills, in fractions of the window (0 to 1).
    sf::FloatRect viewport({0.f, 0.f}, {1.f, 1.f});
    if (windowRatio > worldRatio)
    {
        viewport.size.x = worldRatio / windowRatio;
        viewport.position.x = (1.f - viewport.size.x) / 2.f;
    }
    else
    {
        viewport.size.y = windowRatio / worldRatio;
        viewport.position.y = (1.f - viewport.size.y) / 2.f;
    }
    view.setViewport(viewport);
    return view;
}

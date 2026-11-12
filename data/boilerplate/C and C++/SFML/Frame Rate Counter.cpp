---
order: 36
section: game parts
description: A small FpsCounter class that measures frames per second, updated twice a second
packages: sfml-graphics
sfml: 3
includes: <SFML/Graphics.hpp>
---
// Measures frames per second. Call tick() once per frame; it returns true when the value changes:
//   if (fps.tick()) { window.setTitle("My Game - " + std::to_string(fps.value()) + " FPS"); }
class FpsCounter
{
public:
    bool tick()
    {
        ++frames_;
        const sf::Time elapsed = clock_.getElapsedTime();
        if (elapsed < sf::seconds(0.5f))
        {
            return false;
        }
        value_ = static_cast<int>(static_cast<float>(frames_) / elapsed.asSeconds() + 0.5f);
        frames_ = 0;
        clock_.restart();
        return true;
    }

    int value() const { return value_; }

private:
    sf::Clock clock_;
    int frames_ = 0;
    int value_ = 0;
};

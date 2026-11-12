---
order: 4
section: programs
description: A demo of balls falling and bouncing off the window's edges; click to throw in another ball
file: main.cpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// Bouncing balls: gravity pulls them down, the edges bounce them back. Click to add a ball, Space to clear.
#include <SFML/Graphics.hpp>

#include <algorithm>
#include <random>
#include <vector>

struct Ball
{
    sf::CircleShape shape;
    sf::Vector2f velocity;
};

int main()
{
    const sf::Vector2f size{800.f, 600.f};
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(size)), "Bouncing Ball");
    window.setFramerateLimit(60);

    std::mt19937 random{std::random_device{}()};
    std::uniform_real_distribution<float> speed(-400.f, 400.f);
    std::uniform_int_distribution<int> channel(80, 255);

    std::vector<Ball> balls;
    auto addBall = [&](sf::Vector2f position) {
        Ball ball{sf::CircleShape(20.f), {speed(random), speed(random)}};
        ball.shape.setOrigin({20.f, 20.f});
        ball.shape.setPosition(position);
        const auto c = [&] { return static_cast<std::uint8_t>(channel(random)); };
        ball.shape.setFillColor(sf::Color(c(), c(), c()));
        balls.push_back(ball);
    };
    addBall(size / 2.f);

    const float gravity = 900.f; // pixels per second squared
    const float bounce = 0.85f;  // share of the speed kept by a bounce

    sf::Clock clock;
    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
            else if (const auto* click = event->getIf<sf::Event::MouseButtonPressed>())
            {
                addBall(window.mapPixelToCoords(click->position));
            }
            else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
            {
                if (key->code == sf::Keyboard::Key::Space)
                {
                    balls.clear();
                }
            }
        }

        const float dt = clock.restart().asSeconds();
        for (Ball& ball : balls)
        {
            ball.velocity.y += gravity * dt;
            sf::Vector2f position = ball.shape.getPosition() + ball.velocity * dt;
            const float radius = ball.shape.getRadius();

            // Put the ball back inside before reversing, or it can stick in the edge.
            if (position.x < radius || position.x > size.x - radius)
            {
                position.x = std::clamp(position.x, radius, size.x - radius);
                ball.velocity.x = -ball.velocity.x * bounce;
            }
            if (position.y < radius || position.y > size.y - radius)
            {
                position.y = std::clamp(position.y, radius, size.y - radius);
                ball.velocity.y = -ball.velocity.y * bounce;
            }
            ball.shape.setPosition(position);
        }

        window.clear(sf::Color(25, 25, 35));
        for (const Ball& ball : balls)
        {
            window.draw(ball.shape);
        }
        window.display();
    }
}

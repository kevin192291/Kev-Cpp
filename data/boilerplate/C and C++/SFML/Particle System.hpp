---
order: 32
section: game parts
description: A ParticleSystem class: small fading squares sprayed from an emitter, drawn in one draw call
file: ParticleSystem.hpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// Particles sprayed from an emitter point; each fades out and starts again from the emitter.
//   ParticleSystem sparks(500);
//   Each frame:  sparks.setEmitter(window.mapPixelToCoords(sf::Mouse::getPosition(window)));
//                sparks.update(dt);  window.draw(sparks);
#pragma once

#include <SFML/Graphics.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

class ParticleSystem : public sf::Drawable, public sf::Transformable
{
public:
    explicit ParticleSystem(std::size_t count) : particles_(count), vertices_(sf::PrimitiveType::Triangles, count * 6)
    {
        // Spread the first lifetimes out so the particles don't all start at once.
        std::uniform_real_distribution<float> firstLifetime(0.f, MaxLifetime.asSeconds());
        for (Particle& p : particles_)
        {
            respawn(p);
            p.lifetime = sf::seconds(firstLifetime(random_));
        }
    }

    void setEmitter(sf::Vector2f position) { emitter_ = position; }
    void setColor(sf::Color color) { color_ = color; }
    void setGravity(sf::Vector2f gravity) { gravity_ = gravity; }

    void update(sf::Time dt)
    {
        const float seconds = dt.asSeconds();
        for (std::size_t i = 0; i < particles_.size(); ++i)
        {
            Particle& p = particles_[i];
            p.lifetime -= dt;
            if (p.lifetime <= sf::Time::Zero)
            {
                respawn(p);
            }
            p.velocity += gravity_ * seconds;
            p.position += p.velocity * seconds;

            sf::Color color = color_;
            color.a = static_cast<std::uint8_t>(255.f * (p.lifetime / MaxLifetime));
            writeSquare(i, p.position, color);
        }
    }

private:
    struct Particle
    {
        sf::Vector2f position;
        sf::Vector2f velocity;
        sf::Time lifetime;
    };

    void respawn(Particle& p)
    {
        std::uniform_real_distribution<float> angle(0.f, 360.f);
        std::uniform_real_distribution<float> speed(50.f, 150.f);
        p.position = emitter_;
        p.velocity = sf::Vector2f(speed(random_), sf::degrees(angle(random_))); // length and direction
        p.lifetime = sf::seconds(lifetimeDistribution_(random_) * MaxLifetime.asSeconds());
    }

    // Two triangles per particle: a small square centred on its position.
    void writeSquare(std::size_t index, sf::Vector2f center, sf::Color color)
    {
        const float h = ParticleSize / 2.f;
        const sf::Vector2f corners[6] = {{-h, -h}, {h, -h}, {-h, h}, {-h, h}, {h, -h}, {h, h}};
        for (std::size_t i = 0; i < 6; ++i)
        {
            sf::Vertex& vertex = vertices_[index * 6 + i];
            vertex.position = center + corners[i];
            vertex.color = color;
        }
    }

    void draw(sf::RenderTarget& target, sf::RenderStates states) const override
    {
        states.transform *= getTransform();
        target.draw(vertices_, states);
    }

    static constexpr float ParticleSize = 3.f;
    static constexpr sf::Time MaxLifetime = sf::seconds(2.f);

    std::vector<Particle> particles_;
    sf::VertexArray vertices_;
    sf::Vector2f emitter_;
    sf::Vector2f gravity_;
    sf::Color color_ = sf::Color(255, 180, 60);
    std::mt19937 random_{std::random_device{}()};
    std::uniform_real_distribution<float> lifetimeDistribution_{0.5f, 1.f}; // share of MaxLifetime
};

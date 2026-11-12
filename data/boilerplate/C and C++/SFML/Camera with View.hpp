---
label: Camera with sf::View
order: 21
section: movement
description: A camera class around sf::View that follows a target smoothly, zooms and stays inside the world
file: Camera.hpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// A camera that eases towards a target, zooms within limits and never shows past the world's edges.
//   Camera camera(sf::Vector2f(window.getSize()));   camera.setBounds({{0.f, 0.f}, {3200.f, 2400.f}});
//   On sf::Event::MouseWheelScrolled:  camera.zoom(wheel->delta > 0.f ? 0.9f : 1.1f);
//   Each frame:  camera.follow(player.getPosition(), dt);  window.setView(camera.view());  draw the world,
//   then window.setView(window.getDefaultView()) to draw the score and other on-screen things.
#pragma once

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

class Camera
{
public:
    explicit Camera(sf::Vector2f viewSize) : baseSize_(viewSize), view_(viewSize / 2.f, viewSize) {}

    // The area the camera may show; without bounds it goes anywhere.
    void setBounds(const sf::FloatRect& world)
    {
        bounds_ = world;
        clampToBounds();
    }

    // How quickly it catches up: higher is snappier.
    void setSmoothing(float perSecond) { smoothing_ = perSecond; }

    void follow(sf::Vector2f target, float dt)
    {
        // Exponential easing covers the same share of the distance per second at any frame rate.
        const float t = 1.f - std::exp(-smoothing_ * dt);
        view_.setCenter(view_.getCenter() + (target - view_.getCenter()) * t);
        clampToBounds();
    }

    void centerOn(sf::Vector2f point)
    {
        view_.setCenter(point);
        clampToBounds();
    }

    // factor < 1 zooms in, > 1 zooms out.
    void zoom(float factor)
    {
        zoom_ = std::clamp(zoom_ * factor, MinZoom, MaxZoom);
        view_.setSize(baseSize_ * zoom_);
        clampToBounds();
    }

    // Call on sf::Event::Resized so the world isn't stretched.
    void resize(sf::Vector2f viewSize)
    {
        baseSize_ = viewSize;
        view_.setSize(baseSize_ * zoom_);
        clampToBounds();
    }

    const sf::View& view() const { return view_; }

private:
    void clampToBounds()
    {
        if (!bounds_)
        {
            return;
        }
        // When the world is smaller than the view, centre it instead.
        const sf::Vector2f half = view_.getSize() / 2.f;
        const sf::Vector2f min = bounds_->position + half;
        const sf::Vector2f max = bounds_->position + bounds_->size - half;
        const sf::Vector2f center = view_.getCenter();
        view_.setCenter({min.x <= max.x ? std::clamp(center.x, min.x, max.x) : bounds_->getCenter().x,
                         min.y <= max.y ? std::clamp(center.y, min.y, max.y) : bounds_->getCenter().y});
    }

    static constexpr float MinZoom = 0.25f;
    static constexpr float MaxZoom = 4.f;

    sf::Vector2f baseSize_;
    sf::View view_;
    std::optional<sf::FloatRect> bounds_;
    float zoom_ = 1.f;
    float smoothing_ = 5.f;
};

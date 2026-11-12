---
order: 35
section: game parts
description: A clickable Button class with a label, hover and pressed colours and a callback
file: Button.hpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// A clickable button with a centred label. It runs its callback when the mouse is pressed and released on it.
//   Button play(font, "Play", {300.f, 250.f}, {200.f, 60.f}, [&] { startGame(); });
//   For every event:  play.handleEvent(*event, window);
//   In the loop:      window.draw(play);
#pragma once

#include <SFML/Graphics.hpp>

#include <functional>
#include <utility>

class Button : public sf::Drawable
{
public:
    // The font must outlive the button.
    Button(const sf::Font& font, const sf::String& label, sf::Vector2f position, sf::Vector2f size,
           std::function<void()> onClick)
        : text_(font, label, 24), onClick_(std::move(onClick))
    {
        shape_.setPosition(position);
        shape_.setSize(size);
        shape_.setOutlineThickness(2.f);
        shape_.setOutlineColor(sf::Color(200, 200, 220));
        text_.setFillColor(sf::Color::White);

        const sf::FloatRect bounds = text_.getLocalBounds();
        text_.setOrigin(bounds.position + bounds.size / 2.f);
        text_.setPosition(position + size / 2.f);
        updateColor();
    }

    // Uses the window's current view to turn the mouse position into world coordinates.
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window)
    {
        if (const auto* moved = event.getIf<sf::Event::MouseMoved>())
        {
            hovered_ = contains(window.mapPixelToCoords(moved->position));
        }
        else if (const auto* pressed = event.getIf<sf::Event::MouseButtonPressed>())
        {
            pressed_ = pressed->button == sf::Mouse::Button::Left &&
                       contains(window.mapPixelToCoords(pressed->position));
        }
        else if (const auto* released = event.getIf<sf::Event::MouseButtonReleased>())
        {
            if (released->button == sf::Mouse::Button::Left)
            {
                // Clicked only if it was released over the button too, so dragging off it cancels.
                const bool clicked = pressed_ && contains(window.mapPixelToCoords(released->position));
                pressed_ = false;
                if (clicked && onClick_)
                {
                    onClick_();
                }
            }
        }
        updateColor();
    }

private:
    bool contains(sf::Vector2f point) const { return shape_.getGlobalBounds().contains(point); }

    void updateColor()
    {
        if (pressed_ && hovered_)
        {
            shape_.setFillColor(sf::Color(40, 90, 160));
        }
        else if (hovered_)
        {
            shape_.setFillColor(sf::Color(80, 140, 220));
        }
        else
        {
            shape_.setFillColor(sf::Color(60, 110, 190));
        }
    }

    void draw(sf::RenderTarget& target, sf::RenderStates states) const override
    {
        target.draw(shape_, states);
        target.draw(text_, states);
    }

    sf::RectangleShape shape_;
    sf::Text text_;
    std::function<void()> onClick_;
    bool hovered_ = false;
    bool pressed_ = false;
};

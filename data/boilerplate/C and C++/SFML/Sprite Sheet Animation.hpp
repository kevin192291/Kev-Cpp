---
order: 30
section: game parts
description: An Animation class that steps a sprite through the frames of one row of a sprite sheet
file: Animation.hpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// Plays one row of a sprite sheet whose frames are all the same size.
//   sf::Sprite hero(sheet);
//   Animation walk({32, 32}, 1, 6, sf::seconds(0.1f));   // row 1, 6 frames, 10 frames a second
//   Each frame:  walk.update(dt);  walk.applyTo(hero);  window.draw(hero);
#pragma once

#include <SFML/Graphics.hpp>

class Animation
{
public:
    // row and frames count from 0 at the top-left; frames run left to right.
    Animation(sf::Vector2i frameSize, int row, int frameCount, sf::Time frameTime, bool looping = true)
        : frameSize_(frameSize), row_(row), frameCount_(frameCount), frameTime_(frameTime), looping_(looping)
    {
    }

    void update(sf::Time dt)
    {
        if (frameCount_ <= 1 || frameTime_ <= sf::Time::Zero || isFinished())
        {
            return;
        }
        elapsed_ += dt;
        while (elapsed_ >= frameTime_)
        {
            elapsed_ -= frameTime_;
            if (frame_ + 1 < frameCount_)
            {
                ++frame_;
            }
            else if (looping_)
            {
                frame_ = 0;
            }
            else
            {
                finished_ = true;
                break;
            }
        }
    }

    void applyTo(sf::Sprite& sprite) const { sprite.setTextureRect(frameRect()); }

    sf::IntRect frameRect() const { return {{frame_ * frameSize_.x, row_ * frameSize_.y}, frameSize_}; }

    void restart()
    {
        frame_ = 0;
        elapsed_ = sf::Time::Zero;
        finished_ = false;
    }

    // Only a non-looping animation finishes, on its last frame.
    bool isFinished() const { return finished_; }

private:
    sf::Vector2i frameSize_;
    int row_;
    int frameCount_;
    sf::Time frameTime_;
    bool looping_;

    int frame_ = 0;
    sf::Time elapsed_;
    bool finished_ = false;
};

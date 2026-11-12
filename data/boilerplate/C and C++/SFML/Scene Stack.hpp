---
order: 34
section: game parts
description: Scene and SceneStack classes for game screens (title, game, pause) with safe deferred push and pop
file: SceneStack.hpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// Game screens on a stack. The top scene gets the events; scenes update from the top down until one pauses
// those below; all are drawn bottom to top, so a pause menu draws over the frozen game. push, pop, replace
// and clear take effect after the current event or update, so a scene can safely pop itself.
//
//   class TitleScene : public Scene
//   {
//   public:
//       using Scene::Scene;
//       void handleEvent(const sf::Event& event) override
//       {
//           if (event.is<sf::Event::KeyPressed>())
//           {
//               stack().replace<GameScene>();
//           }
//       }
//       void update(sf::Time) override {}
//       void draw(sf::RenderTarget& target) override { target.draw(title_); }
//   };
//
//   SceneStack scenes;
//   scenes.push<TitleScene>();
//   Each frame:  scenes.handleEvent(*event) for every event, scenes.update(dt), window.clear(),
//                scenes.draw(window), window.display(); close the window once scenes.empty().
#pragma once

#include <SFML/Graphics.hpp>

#include <memory>
#include <utility>
#include <vector>

class SceneStack;

class Scene
{
public:
    explicit Scene(SceneStack& stack) : stack_(stack) {}
    virtual ~Scene() = default;

    virtual void handleEvent(const sf::Event& event) = 0;
    virtual void update(sf::Time dt) = 0;
    virtual void draw(sf::RenderTarget& target) = 0;

    // Whether the scenes below stop updating while this one is on top (a pause menu: yes; a HUD: no).
    virtual bool pausesBelow() const { return true; }

protected:
    SceneStack& stack() { return stack_; }

private:
    SceneStack& stack_;
};

class SceneStack
{
public:
    // Extra arguments go to T's constructor after the stack: T(SceneStack&, args...).
    template <typename T, typename... Args>
    void push(Args&&... args)
    {
        pending_.push_back({Action::Push, std::make_unique<T>(*this, std::forward<Args>(args)...)});
    }

    template <typename T, typename... Args>
    void replace(Args&&... args)
    {
        pop();
        push<T>(std::forward<Args>(args)...);
    }

    void pop() { pending_.push_back({Action::Pop, nullptr}); }
    void clear() { pending_.push_back({Action::Clear, nullptr}); }

    bool empty() const { return scenes_.empty() && pending_.empty(); }

    void handleEvent(const sf::Event& event)
    {
        applyPending();
        if (!scenes_.empty())
        {
            scenes_.back()->handleEvent(event);
        }
        applyPending();
    }

    void update(sf::Time dt)
    {
        applyPending();
        for (auto it = scenes_.rbegin(); it != scenes_.rend(); ++it)
        {
            (*it)->update(dt);
            if ((*it)->pausesBelow())
            {
                break;
            }
        }
        applyPending();
    }

    void draw(sf::RenderTarget& target)
    {
        for (const auto& scene : scenes_)
        {
            scene->draw(target);
        }
    }

private:
    enum class Action
    {
        Push,
        Pop,
        Clear
    };

    struct Change
    {
        Action action;
        std::unique_ptr<Scene> scene;
    };

    void applyPending()
    {
        // Taken out first: a scene being destroyed or created may ask for more changes.
        std::vector<Change> changes = std::move(pending_);
        pending_.clear();
        for (Change& change : changes)
        {
            switch (change.action)
            {
            case Action::Push:
                scenes_.push_back(std::move(change.scene));
                break;
            case Action::Pop:
                if (!scenes_.empty())
                {
                    scenes_.pop_back();
                }
                break;
            case Action::Clear:
                scenes_.clear();
                break;
            }
        }
    }

    std::vector<std::unique_ptr<Scene>> scenes_;
    std::vector<Change> pending_;
};

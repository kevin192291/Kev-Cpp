---
order: 33
section: game parts
description: A ResourceHolder template that loads each texture, font or sound buffer once and shares it
file: ResourceHolder.hpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// Loads each file once and hands out references to it: textures, images, fonts, sound buffers (anything
// SFML can construct from a file path). References stay valid until release() or clear().
//   ResourceHolder<sf::Texture> textures;
//   sf::Sprite player(textures.get("assets/player.png"));   // later calls return the same texture
//   ResourceHolder<sf::Font> fonts;   ResourceHolder<sf::SoundBuffer> sounds;  (needs <SFML/Audio.hpp>)
// get() throws sf::Exception when a file can't be loaded (SFML prints the reason to the console):
//   try { ... } catch (const sf::Exception& e) { std::cerr << e.what() << '\n'; return 1; }
#pragma once

#include <filesystem>
#include <map>

template <typename Resource>
class ResourceHolder
{
public:
    Resource& get(const std::filesystem::path& path)
    {
        auto it = resources_.find(path);
        if (it == resources_.end())
        {
            // SFML 3's loading constructors throw on failure, so a failed load leaves nothing behind.
            it = resources_.try_emplace(path, path).first;
        }
        return it->second;
    }

    bool contains(const std::filesystem::path& path) const { return resources_.contains(path); }

    // Only when nothing uses it any more: sprites, texts and sounds keep references to their resources.
    void release(const std::filesystem::path& path) { resources_.erase(path); }
    void clear() { resources_.clear(); }

private:
    std::map<std::filesystem::path, Resource> resources_;
};

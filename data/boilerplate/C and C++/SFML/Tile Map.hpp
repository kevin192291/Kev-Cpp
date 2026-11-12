---
order: 31
section: game parts
description: A TileMap class that draws a whole grid of tiles from a tileset image in one draw call
file: TileMap.hpp
whole-file: yes
packages: sfml-graphics
sfml: 3
---
// A grid of tiles from one tileset image, drawn with a single vertex array (one draw call for the map).
//   const std::vector<int> level = {0, 0, 1, 2,  0, 3, 3, 2};   // 4 wide, 2 high; -1 leaves a gap
//   TileMap map;
//   if (!map.load("assets/tileset.png", {32, 32}, level, {4, 2})) { return 1; }
//   In the loop:  window.draw(map);
#pragma once

#include <SFML/Graphics.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

class TileMap : public sf::Drawable, public sf::Transformable
{
public:
    // tiles: size.x * size.y tile numbers, row by row. Tile n is the n-th tileSize square of the tileset,
    // counted left to right, then top to bottom. Returns false if the tileset can't be loaded.
    bool load(const std::filesystem::path& tileset, sf::Vector2u tileSize, const std::vector<int>& tiles,
              sf::Vector2u size)
    {
        if (!tileset_.loadFromFile(tileset) || tiles.size() < std::size_t{size.x} * size.y)
        {
            return false;
        }
        const unsigned columns = tileset_.getSize().x / tileSize.x;
        const sf::Vector2f tile(tileSize);

        // SFML 3 has no quads: each tile is two triangles, six vertices, made from the square's 4 corners.
        const sf::Vector2f corners[4] = {{0.f, 0.f}, {tile.x, 0.f}, {0.f, tile.y}, {tile.x, tile.y}};
        const int order[6] = {0, 1, 2, 2, 1, 3};

        vertices_.setPrimitiveType(sf::PrimitiveType::Triangles);
        vertices_.clear(); // so gaps don't keep tiles from an earlier load
        vertices_.resize(std::size_t{size.x} * size.y * 6);

        for (unsigned y = 0; y < size.y; ++y)
        {
            for (unsigned x = 0; x < size.x; ++x)
            {
                const int number = tiles[x + y * size.x];
                sf::Vertex* triangles = &vertices_[(x + y * size.x) * 6];
                if (number < 0 || columns == 0)
                {
                    continue; // all six vertices stay at (0, 0): nothing is drawn
                }
                const sf::Vector2f pos{x * tile.x, y * tile.y};
                const sf::Vector2f tex{(number % columns) * tile.x, (number / columns) * tile.y};
                for (int i = 0; i < 6; ++i)
                {
                    triangles[i].position = pos + corners[order[i]];
                    triangles[i].texCoords = tex + corners[order[i]];
                }
            }
        }
        return true;
    }

private:
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override
    {
        states.transform *= getTransform();
        states.texture = &tileset_;
        target.draw(vertices_, states);
    }

    sf::Texture tileset_;
    sf::VertexArray vertices_;
};

---
order: 110
description: A width x height grid class template stored in one flat vector, with bounds checks for neighbours
ask: ClassName = Grid
includes: <cstddef> <stdexcept> <vector>
---
// One flat vector (one allocation, rows next to each other in memory); cell (x, y) is at y * width + x.
// Coordinates are ints so neighbour checks like contains(x - 1, y) work at the edges.
// {{ClassName}}<bool> won't compile (std::vector<bool> packs bits); use {{ClassName}}<char> instead.
template <typename T>
class {{ClassName}}
{
public:
    {{ClassName}}(int width, int height, const T& fill = T{})
        : width_(width), height_(height), cells_(static_cast<std::size_t>(width) * height, fill)
    {}

    int width() const { return width_; }
    int height() const { return height_; }
    bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

    // Unchecked, like vector's []: only for coordinates you know are inside.
    T& operator()(int x, int y) { return cells_[index(x, y)]; }
    const T& operator()(int x, int y) const { return cells_[index(x, y)]; }

    T& at(int x, int y)
    {
        if (!contains(x, y))
        {
            throw std::out_of_range("{{ClassName}}::at: cell outside the grid");
        }
        return cells_[index(x, y)];
    }

    void fill(const T& value) { cells_.assign(cells_.size(), value); }

private:
    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * width_ + x; }

    int width_;
    int height_;
    std::vector<T> cells_;
};

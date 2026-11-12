---
order: 50
description: A class owning a raw resource, with destructor, copy and move constructors and assignments
ask: ClassName = Buffer
includes: <algorithm> <cstddef> <utility>
---
// Owns a heap array, so it needs all five special members. When you can, hold a std::vector or a
// std::unique_ptr instead and write none of them (the rule of zero).
class {{ClassName}}
{
public:
    explicit {{ClassName}}(std::size_t size = 0) : size_(size), data_(size ? new int[size]{} : nullptr) {}

    ~{{ClassName}}() { delete[] data_; }

    {{ClassName}}(const {{ClassName}}& other) : size_(other.size_), data_(other.size_ ? new int[other.size_] : nullptr)
    {
        std::copy(other.data_, other.data_ + other.size_, data_);
    }

    {{ClassName}}({{ClassName}}&& other) noexcept : size_(std::exchange(other.size_, 0)), data_(std::exchange(other.data_, nullptr))
    {}

    {{ClassName}}& operator=(const {{ClassName}}& other)
    {
        if (this != &other)
        {
            {{ClassName}} copy(other); // copy first: if that throws, *this is unchanged
            swap(copy);
        }
        return *this;
    }

    {{ClassName}}& operator=({{ClassName}}&& other) noexcept
    {
        {{ClassName}} moved(std::move(other)); // our old data is freed when moved goes
        swap(moved);
        return *this;
    }

    void swap({{ClassName}}& other) noexcept
    {
        std::swap(size_, other.size_);
        std::swap(data_, other.data_);
    }

    std::size_t size() const { return size_; }
    int& operator[](std::size_t i) { return data_[i]; }
    const int& operator[](std::size_t i) const { return data_[i]; }

private:
    std::size_t size_ = 0;
    int* data_ = nullptr;
};

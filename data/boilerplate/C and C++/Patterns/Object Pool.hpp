---
order: 80
description: Reuses objects instead of allocating new ones; acquire() returns a handle that gives it back
includes: <cstddef> <memory> <vector>
---
// For objects made and dropped all the time (bullets, particles, buffers). acquire() returns a
// std::unique_ptr whose deleter puts the object back in the pool instead of deleting it. The object
// keeps its old state, so reset it after acquiring, and the pool must outlive every handle.
template <typename T>
class ObjectPool
{
    struct GiveBack
    {
        ObjectPool* pool;
        void operator()(T* object) const { pool->free_.push_back(object); }
    };

public:
    using Handle = std::unique_ptr<T, GiveBack>;

    ObjectPool() = default;
    ObjectPool(const ObjectPool&) = delete; // handles point at this pool, so it must stay put
    ObjectPool& operator=(const ObjectPool&) = delete;

    Handle acquire()
    {
        if (free_.empty())
        {
            all_.push_back(std::make_unique<T>());
            free_.push_back(all_.back().get());
        }
        T* object = free_.back();
        free_.pop_back();
        return Handle(object, GiveBack{this});
    }

    std::size_t available() const { return free_.size(); }
    std::size_t created() const { return all_.size(); }

private:
    std::vector<std::unique_ptr<T>> all_; // owns every object
    std::vector<T*> free_;                // the ones not handed out
};

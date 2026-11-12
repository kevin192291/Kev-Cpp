---
order: 20
description: A class template (a stack of any type T) with push, pop, top and size
ask: ClassName = Stack
includes: <cstddef> <stdexcept> <utility> <vector>
---
// {{ClassName}}<int>, {{ClassName}}<std::string>...: T is filled in where it's used. Member functions of a
// template stay in the header, since the compiler needs their code at every use.
template <typename T>
class {{ClassName}}
{
public:
    void push(T value) { items_.push_back(std::move(value)); }

    T pop()
    {
        if (items_.empty())
        {
            throw std::out_of_range("{{ClassName}}::pop: empty");
        }
        T top = std::move(items_.back());
        items_.pop_back();
        return top;
    }

    const T& top() const { return items_.back(); } // only when !empty()
    bool empty() const { return items_.empty(); }
    std::size_t size() const { return items_.size(); }

private:
    std::vector<T> items_;
};

---
order: 20
description: Creates objects of registered derived classes by name, e.g. from a config file or user input
includes: <concepts> <functional> <map> <memory> <string> <vector>
---
//     Factory<Shape> shapes;
//     shapes.add<Circle>("circle");
//     std::unique_ptr<Shape> shape = shapes.create("circle"); // nullptr for an unknown name
template <typename Base>
class Factory
{
public:
    template <std::derived_from<Base> T>
    void add(const std::string& name)
    {
        creators_[name] = [] { return std::make_unique<T>(); };
    }

    std::unique_ptr<Base> create(const std::string& name) const
    {
        const auto it = creators_.find(name);
        return it != creators_.end() ? it->second() : nullptr;
    }

    std::vector<std::string> names() const
    {
        std::vector<std::string> result;
        for (const auto& entry : creators_)
        {
            result.push_back(entry.first);
        }
        return result;
    }

private:
    std::map<std::string, std::function<std::unique_ptr<Base>()>> creators_;
};

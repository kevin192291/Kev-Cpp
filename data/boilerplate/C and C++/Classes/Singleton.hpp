---
label: Singleton (Meyers)
order: 70
description: A class with exactly one instance, created on first use; thread-safe since C++11
ask: ClassName = Settings
---
// The instance is created the first time instance() runs; C++11 makes that thread-safe. It's global
// state, so prefer passing an object to the code that needs it when that's practical.
class {{ClassName}}
{
public:
    static {{ClassName}}& instance()
    {
        static {{ClassName}} theInstance;
        return theInstance;
    }

    {{ClassName}}(const {{ClassName}}&) = delete;
    {{ClassName}}& operator=(const {{ClassName}}&) = delete;

    {{cursor}}

private:
    {{ClassName}}() = default;
};

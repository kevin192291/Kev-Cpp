---
order: 12
description: The .cpp for a class made with Class Header: includes it and defines each member
whole-file: yes
file: {{ClassName}}.cpp
ask: ClassName = Player
---
#include "{{ClassName}}.hpp"

#include <utility>

{{ClassName}}::{{ClassName}}(std::string name) : name_(std::move(name)) {}

const std::string& {{ClassName}}::name() const
{
    return name_;
}

void {{ClassName}}::update(double dt)
{
    elapsed_ += dt;
    {{cursor}}
}

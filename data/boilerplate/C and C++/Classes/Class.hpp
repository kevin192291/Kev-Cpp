---
order: 10
description: A header-only class with a constructor, a getter and a setter, and private members
whole-file: yes
file: {{ClassName}}.hpp
ask: ClassName = Player
---
#pragma once

#include <string>
#include <utility>

class {{ClassName}}
{
public:
    explicit {{ClassName}}(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    {{cursor}}

private:
    std::string name_;
};

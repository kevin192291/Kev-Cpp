---
order: 11
description: A class declared in a header; Class Source writes the .cpp that defines its members
whole-file: yes
file: {{ClassName}}.hpp
ask: ClassName = Player
---
#pragma once

#include <string>

class {{ClassName}}
{
public:
    explicit {{ClassName}}(std::string name);

    const std::string& name() const;
    void update(double dt);

private:
    std::string name_;
    double elapsed_ = 0.0;
};

---
label: Pimpl Class Header
order: 80
description: A class whose private members live in its .cpp (pimpl); Pimpl Class Source writes that file
whole-file: yes
file: {{ClassName}}.hpp
ask: ClassName = Widget
---
#pragma once

#include <memory>
#include <string>

// Pimpl: the data lives in {{ClassName}}::Impl, defined in {{ClassName}}.cpp, so changing it doesn't
// recompile the files that include this header.
class {{ClassName}}
{
public:
    explicit {{ClassName}}(std::string name);
    ~{{ClassName}}();

    {{ClassName}}({{ClassName}}&&) noexcept;
    {{ClassName}}& operator=({{ClassName}}&&) noexcept;

    const std::string& name() const;
    void doWork();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

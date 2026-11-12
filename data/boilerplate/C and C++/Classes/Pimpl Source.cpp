---
label: Pimpl Class Source
order: 81
description: The .cpp for a class made with Pimpl Class Header: defines Impl and the members
whole-file: yes
file: {{ClassName}}.cpp
ask: ClassName = Widget
---
#include "{{ClassName}}.hpp"

#include <utility>

struct {{ClassName}}::Impl
{
    std::string name;
    int workDone = 0;
};

{{ClassName}}::{{ClassName}}(std::string name) : impl_(std::make_unique<Impl>(std::move(name))) {}

// Defined here, where Impl is complete, because std::unique_ptr<Impl> must see it to delete it.
{{ClassName}}::~{{ClassName}}() = default;
{{ClassName}}::{{ClassName}}({{ClassName}}&&) noexcept = default;
{{ClassName}}& {{ClassName}}::operator=({{ClassName}}&&) noexcept = default;

const std::string& {{ClassName}}::name() const
{
    return impl_->name;
}

void {{ClassName}}::doWork()
{
    ++impl_->workDone;
    {{cursor}}
}

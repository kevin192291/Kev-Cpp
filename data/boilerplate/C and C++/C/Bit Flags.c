---
description: An enum of one-bit flags, with how to set, clear, toggle and test them
ask: Name = Permission
order: 36
section: data
---
// One bit each, so any set of them fits in an unsigned int: unsigned int flags = A | B;
//     flags |= A;                          turn A on
//     flags &= ~A;                         turn A off
//     flags ^= A;                          toggle A
//     if (flags & A)                       A is on
//     if ((flags & (A | B)) == (A | B))    A and B are both on
typedef enum {{Name:pascal}}
{
    {{Name:snake:upper}}_NONE = 0,
    {{Name:snake:upper}}_READ = 1u << 0,
    {{Name:snake:upper}}_WRITE = 1u << 1,
    {{Name:snake:upper}}_EXECUTE = 1u << 2,
} {{Name:pascal}};

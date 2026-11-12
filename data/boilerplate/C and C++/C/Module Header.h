---
description: A C header with an include guard that C++ can include too; Module Source is its .c file
file: {{Name:snake}}.h
whole-file: yes
ask: Name = Parser
order: 13
section: starters
---
#ifndef {{Name:snake:upper}}_H
#define {{Name:snake:upper}}_H

// Lets C++ files include this header and call the C functions.
#ifdef __cplusplus
extern "C" {
#endif

void {{Name:snake}}_init(void);
{{cursor}}

#ifdef __cplusplus
}
#endif

#endif // {{Name:snake:upper}}_H

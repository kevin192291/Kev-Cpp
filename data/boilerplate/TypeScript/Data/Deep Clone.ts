---
order: 60
description: structuredClone: a deep copy of nested objects, arrays, Map, Set, Date and cycles
---
// Functions and DOM nodes can't be cloned (it throws); class instances come back as plain objects.
const copy = structuredClone({{selection}}{{cursor}});

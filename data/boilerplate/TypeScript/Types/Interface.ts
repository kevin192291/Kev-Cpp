---
order: 10
description: An interface with readonly, required and optional properties
ask: Name = User
---
export interface {{Name}} {
    readonly id: string;
    name: string;
    email?: string;
    createdAt: Date;
    {{cursor}}
}

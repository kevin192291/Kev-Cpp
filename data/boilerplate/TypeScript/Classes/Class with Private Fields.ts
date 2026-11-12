---
order: 10
description: A class with #private fields, a static counter, and getters/setters that validate
ask: Name = Account
---
export class {{Name}} {
    static #nextId = 1;

    readonly id = {{Name}}.#nextId++;
    #owner = '';
    #balance = 0;

    constructor(owner: string) {
        this.owner = owner; // through the setter, so it is validated too
    }

    get owner(): string {
        return this.#owner;
    }

    set owner(value: string) {
        if (value.trim() === '') {
            throw new RangeError('owner must not be empty');
        }
        this.#owner = value.trim();
    }

    // A getter without a setter: readable from outside, changed only by methods.
    get balance(): number {
        return this.#balance;
    }

    deposit(amount: number): void {
        if (!(amount > 0)) {
            throw new RangeError(`deposit must be positive, got ${amount}`);
        }
        this.#balance += amount;
    }

    {{cursor}}
}

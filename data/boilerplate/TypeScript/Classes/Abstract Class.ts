---
order: 20
description: An abstract base class: shared code in the base, abstract members every subclass implements
---
export abstract class Shape {
    readonly name: string;

    constructor(name: string) {
        this.name = name;
    }

    abstract area(): number;
    abstract get perimeter(): number;

    // Shared by every subclass, built on the abstract members.
    describe(): string {
        return `${this.name}: area ${this.area().toFixed(2)}, perimeter ${this.perimeter.toFixed(2)}`;
    }
}

export class Circle extends Shape {
    readonly radius: number;

    constructor(radius: number) {
        super('circle');
        this.radius = radius;
    }

    override area(): number {
        return Math.PI * this.radius ** 2;
    }

    override get perimeter(): number {
        return 2 * Math.PI * this.radius;
    }
}

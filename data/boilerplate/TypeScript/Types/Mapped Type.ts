---
order: 70
description: Mapped types that transform every property of a type, including key renaming with `as`
---
// Every property may also be null.
export type Nullable<T> = { [K in keyof T]: T[K] | null };

// Removes readonly and ? from every property (the - prefix drops a modifier).
export type Mutable<T> = { -readonly [K in keyof T]-?: T[K] };

// Renames keys: { name: string } becomes { getName: () => string }.
export type Getters<T> = {
    [K in keyof T as `get${Capitalize<string & K>}`]: () => T[K];
};

// Keeps only the properties whose values are of type V: PickByValue<User, string>.
export type PickByValue<T, V> = {
    [K in keyof T as T[K] extends V ? K : never]: T[K];
};

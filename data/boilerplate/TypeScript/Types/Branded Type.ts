---
order: 110
description: A branded type: a string at run time that can't be mixed up with other strings at compile time
ask: Name = UserId
---
declare const brand: unique symbol;

// Brand<string, 'OrderId'> and Brand<string, 'UserId'> are both strings but not interchangeable.
export type Brand<T, B extends string> = T & { readonly [brand]: B };

export type {{Name}} = Brand<string, '{{Name}}'>;

// The one place a plain string becomes a {{Name}}, so check it here.
export function to{{Name}}(value: string): {{Name}} {
    if (value.trim() === '') {
        throw new Error('{{Name}} must not be empty');
    }
    return value as {{Name}};
}

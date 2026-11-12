---
order: 40
description: A lazily created single instance behind a private constructor
ask: Name = Settings
---
// {{Name}}.instance creates the object on first use and returns the same one after that.
// Often a module is enough: modules run once, so `export const {{Name:camel}} = new {{Name}}();`
// is shared by every importer too.
export class {{Name}} {
    static #instance: {{Name}} | undefined;

    private constructor() {}

    static get instance(): {{Name}} {
        return ({{Name}}.#instance ??= new {{Name}}());
    }

    {{cursor}}
}

---
order: 60
description: TypeScript's built-in utility types (Partial, Pick, Omit, Record, ReturnType, Awaited...) on one example
file: utility-types.ts
whole-file: yes
---
// The built-in utility types, each applied to the same example. Hover a name to see the result.
export interface Todo {
    id: number;
    title: string;
    done: boolean;
    due?: Date;
}

export type TodoDraft = Partial<Todo>; // every property optional
export type CompleteTodo = Required<Todo>; // every property required, due too
export type FrozenTodo = Readonly<Todo>; // no property can be reassigned
export type TodoPreview = Pick<Todo, 'id' | 'title'>; // only these properties
export type NewTodo = Omit<Todo, 'id'>; // every property but these
export type TodosById = Record<number, Todo>; // an object type from a key type and a value type
export type TodoKey = keyof Todo; // 'id' | 'title' | 'done' | 'due'
export type TodoTitle = Todo['title']; // string: the type of one property

export type Priority = Exclude<'low' | 'high' | 'none', 'none'>; // 'low' | 'high'
export type Urgent = Extract<Priority, 'high' | 'critical'>; // 'high'
export type Title = NonNullable<string | null | undefined>; // string

export function createTodo(title: string, done = false): Todo {
    return { id: Date.now(), title, done };
}
export type CreateTodoArgs = Parameters<typeof createTodo>; // [title: string, done?: boolean]
export type CreatedTodo = ReturnType<typeof createTodo>; // Todo

export async function loadTodos(): Promise<Todo[]> {
    return [];
}
export type LoadedTodos = Awaited<ReturnType<typeof loadTodos>>; // Todo[], not Promise<Todo[]>

export class TodoList {
    readonly name: string;
    constructor(name: string) {
        this.name = name;
    }
}
export type TodoListArgs = ConstructorParameters<typeof TodoList>; // [name: string]
export type TodoListInstance = InstanceType<typeof TodoList>; // TodoList

export type Loud = Uppercase<'todo'>; // 'TODO' (also Lowercase, Capitalize, Uncapitalize)

// NoInfer: T is inferred from `items` only, so a fallback that isn't one of them is an error.
export function pickOne<T extends string>(items: T[], fallback: NoInfer<T>): T {
    return items[0] ?? fallback;
}

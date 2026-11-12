---
label: Deep $state Object and Array
order: 20
description: An array of objects in $state: push, splice and property writes all update the page
---
interface Todo {
    id: number;
    text: string;
    done: boolean;
}

// Objects and arrays in $state are deeply reactive proxies.
let todos = $state<Todo[]>([]);
let nextId = 1;
let remaining = $derived(todos.filter((todo) => !todo.done).length);

function addTodo(text: string) {
    todos.push({ id: nextId++, text, done: false });
}

function toggle(todo: Todo) {
    todo.done = !todo.done;
}

function clearDone() {
    todos = todos.filter((todo) => !todo.done);
}

// Log or post a plain copy, not the proxy:
// console.log($state.snapshot(todos));
// Large data you only ever replace (API results) can skip the proxy with $state.raw([]).

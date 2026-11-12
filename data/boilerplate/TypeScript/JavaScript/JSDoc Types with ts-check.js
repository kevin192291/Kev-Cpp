---
order: 10
description: Type-checked plain JavaScript: // @ts-check with JSDoc typedefs, parameter types, generics and casts
file: src/users.js
whole-file: yes
---
// @ts-check
// Editors check this file as if it were TypeScript (so does `tsc --allowJs --checkJs --noEmit`).
// "checkJs": true in jsconfig.json or tsconfig.json does the same for every .js file.

/**
 * @typedef {object} User
 * @property {number} id
 * @property {string} name
 * @property {string} [email] optional
 */

/** @typedef {'admin' | 'member' | 'guest'} Role */

/**
 * Finds a user by id.
 * @param {User[]} users
 * @param {number} id
 * @returns {User | undefined}
 */
export function findUser(users, id) {
    return users.find((user) => user.id === id);
}

/**
 * A generic function: the result has the same item type as the input.
 * @template T
 * @param {T[]} items
 * @param {(item: T) => boolean} keep
 * @returns {T[]}
 */
export function filterItems(items, keep) {
    return items.filter(keep);
}

/** @type {Map<Role, User[]>} Without the annotation an empty Map would be Map<any, any>. */
export const usersByRole = new Map();

/**
 * @param {string} text
 * @returns {User}
 */
export function parseUser(text) {
    // A cast: the type comment goes right before a parenthesised expression.
    return /** @type {User} */ (JSON.parse(text));
}

// Types from another module (TypeScript 5.5+): /** @import { Config } from './config.js' */

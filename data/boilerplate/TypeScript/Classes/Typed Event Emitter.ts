---
order: 30
description: An event emitter whose event names and payload types are checked at every on/emit call
---
type Listener<T> = (payload: T) => void;

// type GameEvents = { scored: { points: number }; gameOver: { winner: string } };
// const events = new TypedEmitter<GameEvents>();
// const off = events.on('scored', ({ points }) => console.log(points));
// events.emit('scored', { points: 10 }); off();
export class TypedEmitter<Events extends object> {
    #listeners: { [K in keyof Events]?: Set<Listener<Events[K]>> } = {};

    // Returns a function that removes the listener again.
    on<K extends keyof Events>(event: K, listener: Listener<Events[K]>): () => void {
        (this.#listeners[event] ??= new Set<Listener<Events[K]>>()).add(listener);
        return () => this.off(event, listener);
    }

    once<K extends keyof Events>(event: K, listener: Listener<Events[K]>): () => void {
        const off = this.on(event, (payload) => {
            off();
            listener(payload);
        });
        return off;
    }

    off<K extends keyof Events>(event: K, listener: Listener<Events[K]>): void {
        this.#listeners[event]?.delete(listener);
    }

    emit<K extends keyof Events>(event: K, payload: Events[K]): void {
        // A copy, so listeners that remove themselves (once) don't disturb the loop.
        for (const listener of [...(this.#listeners[event] ?? [])]) {
            listener(payload);
        }
    }
}

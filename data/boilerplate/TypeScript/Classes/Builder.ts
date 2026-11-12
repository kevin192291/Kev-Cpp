---
order: 60
description: A fluent builder: chained setters returning `this`, defaults, and a validating build()
---
export interface HttpRequest {
    method: 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE';
    url: string;
    headers: Record<string, string>;
    body: string | undefined;
    timeoutMs: number;
}

// const request = new RequestBuilder().url('/api/items').json({ name: 'pen' }).timeout(5000).build();
export class RequestBuilder {
    #method: HttpRequest['method'] = 'GET';
    #url = '';
    #headers: Record<string, string> = {};
    #body: string | undefined;
    #timeoutMs = 10_000;

    method(method: HttpRequest['method']): this {
        this.#method = method;
        return this;
    }

    url(url: string): this {
        this.#url = url;
        return this;
    }

    header(name: string, value: string): this {
        this.#headers[name.toLowerCase()] = value;
        return this;
    }

    // Sets a JSON body; a GET becomes a POST.
    json(data: unknown): this {
        this.#body = JSON.stringify(data);
        this.#headers['content-type'] = 'application/json';
        if (this.#method === 'GET') {
            this.#method = 'POST';
        }
        return this;
    }

    timeout(ms: number): this {
        this.#timeoutMs = ms;
        return this;
    }

    build(): HttpRequest {
        if (this.#url === '') {
            throw new Error('RequestBuilder: url() is required');
        }
        return {
            method: this.#method,
            url: this.#url,
            headers: { ...this.#headers },
            body: this.#body,
            timeoutMs: this.#timeoutMs,
        };
    }
}

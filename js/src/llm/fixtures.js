/**
 * fixtures.js — Fixture loader and MockTransport for tests.
 */

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join, dirname } from 'node:path';

import { LLMError } from './errors.js';

const _thisDir = dirname(fileURLToPath(import.meta.url));
const FIXTURES_DIR = join(_thisDir, '..', '..', '..', 'tests', 'fixtures', 'llm');

/**
 * Load a named fixture JSON file.
 * @param {string} name  e.g. "complete_basic"
 * @returns {object}
 */
export function loadFixture(name) {
    const path = join(FIXTURES_DIR, `${name}.json`);
    try {
        const raw = readFileSync(path, 'utf-8');
        return JSON.parse(raw);
    } catch (err) {
        throw new Error(`fixture not found: ${path} (${err.message})`);
    }
}

/**
 * MockTransport — intercepts HTTP calls for testing.
 *
 * It records the last request (method, url, body) and returns the
 * canned response from the fixture.
 *
 * The transport interface matches what OllamaProvider._transport expects:
 *   request(method, url, body, timeoutS) → { status, body }
 *
 * For streaming fixtures (array response body), the body is returned as-is
 * (an array); OllamaProvider.chatStream handles that.
 */
export class MockTransport {
    /**
     * @param {object} fixture  Result of loadFixture()
     */
    constructor(fixture) {
        this._fixture = fixture;
        /** @type {{method:string, url:string, body:object|null}|null} */
        this.lastRequest = null;
    }

    /**
     * @param {string} method
     * @param {string} url
     * @param {object|null} body
     * @param {number} _timeoutS
     * @returns {{status: number, body: any}}
     */
    request(method, url, body, _timeoutS) {
        this.lastRequest = { method, url, body };
        const resp = this._fixture.response;
        return { status: resp.status, body: resp.body };
    }
}

/**
 * A MockTransport that always returns a 503 HTTP error.
 * Used for error tests.
 */
export class ErrorTransport {
    /**
     * @param {number} [status=503]
     */
    constructor(status = 503) {
        this._status = status;
    }

    request(_method, _url, _body, _timeoutS) {
        return { status: this._status, body: { error: 'service unavailable' } };
    }
}

/**
 * A MockTransport that simulates a connection refusal (network unreachable).
 * Used for error tests.
 */
export class UnreachableTransport {
    request(_method, _url, _body, _timeoutS) {
        throw new LLMError('unreachable', 'connect ECONNREFUSED 127.0.0.1:11434');
    }
}

/**
 * A MockTransport that simulates a timeout.
 * Used for error tests.
 */
export class TimeoutTransport {
    request(_method, _url, _body, _timeoutS) {
        throw new LLMError('timeout', 'Request timed out');
    }
}

/**
 * A MockTransport that returns a malformed JSON response for sentiment decode error test.
 */
export class MalformedJsonTransport {
    request(_method, _url, _body, _timeoutS) {
        // Return a valid HTTP 200 but the message content has no JSON object
        return {
            status: 200,
            body: {
                model: 'llama3.1:8b',
                message: { role: 'assistant', content: 'this is not json at all' },
                done: true,
                done_reason: 'stop',
                prompt_eval_count: 10,
                eval_count: 5,
                total_duration: 123456789,
            },
        };
    }
}

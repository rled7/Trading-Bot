/**
 * ollama.js — OllamaProvider implementation.
 * Uses Node 18+ built-in fetch. Transport injection for testing.
 */

import { LLMProvider } from './provider.js';
import { LLMError } from './errors.js';

const DEFAULT_HOST = 'http://localhost:11434';
const DEFAULT_MODEL = 'llama3.1:8b';
const DEFAULT_EMBED_MODEL = 'nomic-embed-text';
const DEFAULT_TIMEOUT_S = 60.0;
const DEFAULT_SEED = 42;
const DEFAULT_TEMPERATURE = 0.2;
const DEFAULT_MAX_TOKENS = 512;

/**
 * Default HTTP transport — uses Node 18+ built-in fetch.
 * @private
 */
class FetchTransport {
    /**
     * @param {string} method
     * @param {string} url
     * @param {object|null} body
     * @param {number} timeoutS
     * @returns {Promise<{status: number, body: any}>}
     */
    async request(method, url, body, timeoutS) {
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), timeoutS * 1000);
        try {
            const init = {
                method,
                signal: controller.signal,
            };
            if (body !== null) {
                init.headers = { 'Content-Type': 'application/json' };
                init.body = JSON.stringify(body);
            }
            const resp = await fetch(url, init);
            const text = await resp.text();
            let parsed;
            try {
                parsed = JSON.parse(text);
            } catch {
                throw new LLMError('decode', `JSON decode error: ${text}`);
            }
            return { status: resp.status, body: parsed };
        } catch (err) {
            if (err instanceof LLMError) throw err;
            if (err.name === 'AbortError') {
                throw new LLMError('timeout', `Request timed out after ${timeoutS}s`);
            }
            // Connection refused or network unreachable
            throw new LLMError('unreachable', String(err.message || err));
        } finally {
            clearTimeout(timer);
        }
    }
}

export class OllamaProvider extends LLMProvider {
    /**
     * @param {object} [opts]
     * @param {string} [opts.host]
     * @param {string} [opts.model]
     * @param {string} [opts.embedModel]
     * @param {number} [opts.timeoutS]
     * @param {number|null} [opts.seed]
     * @param {number} [opts.temperature]
     * @param {number|null} [opts.maxTokens]
     * @param {object|null} [opts.transport]  MockTransport injection point
     */
    constructor({
        host = DEFAULT_HOST,
        model = DEFAULT_MODEL,
        embedModel = DEFAULT_EMBED_MODEL,
        timeoutS = DEFAULT_TIMEOUT_S,
        seed = DEFAULT_SEED,
        temperature = DEFAULT_TEMPERATURE,
        maxTokens = DEFAULT_MAX_TOKENS,
        transport = null,
    } = {}) {
        super();
        this._host = host.replace(/\/$/, '');
        this._model = model;
        this._embedModel = embedModel;
        this._timeoutS = timeoutS;
        this._seed = seed;
        this._temperature = temperature;
        this._maxTokens = maxTokens;
        this._transport = transport !== null ? transport : new FetchTransport();
    }

    /**
     * Construct an OllamaProvider from environment variables.
     * @param {object|null} [transport]  Optional transport override (for testing)
     * @returns {OllamaProvider}
     */
    static fromEnv(transport = null) {
        const host = process.env.AF_LLM_HOST ?? DEFAULT_HOST;
        const model = process.env.AF_LLM_MODEL ?? DEFAULT_MODEL;
        const embedModel = process.env.AF_LLM_EMBED_MODEL ?? DEFAULT_EMBED_MODEL;
        const timeoutS = process.env.AF_LLM_TIMEOUT
            ? parseFloat(process.env.AF_LLM_TIMEOUT)
            : DEFAULT_TIMEOUT_S;
        const seedRaw = process.env.AF_LLM_SEED;
        const seed = seedRaw !== undefined && seedRaw !== ''
            ? parseInt(seedRaw, 10)
            : DEFAULT_SEED;
        const temperature = process.env.AF_LLM_TEMPERATURE
            ? parseFloat(process.env.AF_LLM_TEMPERATURE)
            : DEFAULT_TEMPERATURE;
        const maxTokensRaw = process.env.AF_LLM_MAX_TOKENS;
        const maxTokens = maxTokensRaw !== undefined && maxTokensRaw !== ''
            ? parseInt(maxTokensRaw, 10)
            : DEFAULT_MAX_TOKENS;
        return new OllamaProvider({ host, model, embedModel, timeoutS, seed, temperature, maxTokens, transport });
    }

    /**
     * Internal: make a request through the transport.
     * @private
     * @param {string} method
     * @param {string} path
     * @param {object|null} body
     * @param {number} [timeoutS]
     * @returns {Promise<object>}
     */
    async _doRequest(method, path, body, timeoutS) {
        const url = this._host + path;
        const t = timeoutS ?? this._timeoutS;
        const result = await this._transport.request(method, url, body, t);
        if (result.status >= 400) {
            throw new LLMError('http', `HTTP ${result.status}`, result.status);
        }
        return result.body;
    }

    /**
     * @param {import('./types.js').CompletionRequest} req
     * @returns {Promise<import('./types.js').CompletionResponse>}
     */
    async complete(req) {
        /** @type {object} */
        const options = {
            temperature: req.temperature ?? DEFAULT_TEMPERATURE,
        };
        const maxTokens = req.maxTokens ?? this._maxTokens;
        if (maxTokens !== null) options.num_predict = maxTokens;
        if (req.stop && req.stop.length > 0) options.stop = [...req.stop];
        const seed = req.seed ?? this._seed;
        if (seed !== null) options.seed = seed;

        const body = {
            model: req.model,
            prompt: req.prompt,
            stream: false,
            options,
        };
        const data = await this._doRequest('POST', '/api/generate', body, req.timeoutS);
        return {
            text: data.response,
            model: data.model,
            promptTokens: data.prompt_eval_count ?? 0,
            completionTokens: data.eval_count ?? 0,
            totalDurationNs: data.total_duration ?? 0,
            finishReason: data.done_reason ?? 'stop',
        };
    }

    /**
     * @param {import('./types.js').ChatRequest} req
     * @returns {Promise<import('./types.js').ChatResponse>}
     */
    async chat(req) {
        /** @type {object} */
        const options = {
            temperature: req.temperature ?? DEFAULT_TEMPERATURE,
        };
        const maxTokens = req.maxTokens ?? this._maxTokens;
        if (maxTokens !== null) options.num_predict = maxTokens;
        const seed = req.seed ?? this._seed;
        if (seed !== null) options.seed = seed;

        const body = {
            model: req.model,
            messages: req.messages.map(m => ({ role: m.role, content: m.content })),
            stream: false,
            options,
        };
        const data = await this._doRequest('POST', '/api/chat', body, req.timeoutS);
        const msg = data.message;
        return {
            message: { role: msg.role, content: msg.content },
            model: data.model,
            promptTokens: data.prompt_eval_count ?? 0,
            completionTokens: data.eval_count ?? 0,
            totalDurationNs: data.total_duration ?? 0,
            finishReason: data.done_reason ?? 'stop',
        };
    }

    /**
     * Async generator: yields ChatChunk for each NDJSON line.
     * @param {import('./types.js').ChatRequest} req
     * @returns {AsyncGenerator<import('./types.js').ChatChunk>}
     */
    async *chatStream(req) {
        /** @type {object} */
        const options = {
            temperature: req.temperature ?? DEFAULT_TEMPERATURE,
        };
        const maxTokens = req.maxTokens ?? this._maxTokens;
        if (maxTokens !== null) options.num_predict = maxTokens;
        const seed = req.seed ?? this._seed;
        if (seed !== null) options.seed = seed;

        const body = {
            model: req.model,
            messages: req.messages.map(m => ({ role: m.role, content: m.content })),
            stream: true,
            options,
        };
        // The transport returns the parsed body (array or single object).
        // For streaming, MockTransport returns an array of chunks.
        const result = await this._transport.request(
            'POST',
            this._host + '/api/chat',
            body,
            req.timeoutS ?? this._timeoutS,
        );
        if (result.status >= 400) {
            throw new LLMError('http', `HTTP ${result.status}`, result.status);
        }
        const chunks = Array.isArray(result.body) ? result.body : [result.body];
        for (const chunk of chunks) {
            const done = chunk.done === true;
            const msg = chunk.message ?? {};
            const delta = msg.content ?? '';
            const finishReason = done ? (chunk.done_reason ?? null) : null;
            yield { delta, done, finishReason };
        }
    }

    /**
     * @param {import('./types.js').EmbedRequest} req
     * @returns {Promise<import('./types.js').EmbedResponse>}
     */
    async embed(req) {
        const inputs = Array.isArray(req.input) ? req.input : [req.input];
        const embeddings = [];
        for (const text of inputs) {
            const body = { model: req.model, prompt: text };
            const data = await this._doRequest('POST', '/api/embeddings', body);
            embeddings.push(data.embedding);
        }
        return { embeddings, model: req.model };
    }

    /**
     * @returns {Promise<import('./types.js').ModelInfo[]>}
     */
    async listModels() {
        const data = await this._doRequest('GET', '/api/tags', null);
        return (data.models ?? []).map(m => ({
            name: m.name,
            sizeBytes: m.size ?? 0,
            modifiedAt: m.modified_at ?? '',
        }));
    }

    /**
     * @returns {Promise<import('./types.js').HealthStatus>}
     */
    async health() {
        // Step 1: hit root endpoint
        try {
            const result = await this._transport.request(
                'GET',
                this._host + '/',
                null,
                this._timeoutS,
            );
            if (result.status >= 400) {
                return {
                    ok: false,
                    modelLoaded: false,
                    model: this._model,
                    error: `root endpoint returned HTTP ${result.status}`,
                };
            }
        } catch (err) {
            const msg = err instanceof LLMError ? err.message : String(err.message || err);
            return { ok: false, modelLoaded: false, model: this._model, error: msg };
        }

        // Step 2: list models
        let models;
        try {
            models = await this.listModels();
        } catch (err) {
            const msg = err instanceof LLMError ? err.message : String(err.message || err);
            return { ok: false, modelLoaded: false, model: this._model, error: msg };
        }

        const modelNames = new Set(models.map(m => m.name));
        const modelLoaded = modelNames.has(this._model);
        return { ok: true, modelLoaded, model: this._model, error: null };
    }
}

/**
 * provider.js — Abstract LLMProvider interface convention.
 *
 * JS has no abstract classes in the Python sense. This base class throws
 * on every method so subclasses are forced to override them. It documents
 * the full interface for type checkers and consumers.
 */

import { LLMError } from './errors.js';

export class LLMProvider {
    /**
     * @returns {Promise<import('./types.js').HealthStatus>}
     */
    async health() {
        throw new LLMError('unreachable', 'health() not implemented');
    }

    /**
     * @returns {Promise<import('./types.js').ModelInfo[]>}
     */
    async listModels() {
        throw new LLMError('unreachable', 'listModels() not implemented');
    }

    /**
     * @param {import('./types.js').CompletionRequest} req
     * @returns {Promise<import('./types.js').CompletionResponse>}
     */
    async complete(req) {
        throw new LLMError('unreachable', 'complete() not implemented');
    }

    /**
     * @param {import('./types.js').ChatRequest} req
     * @returns {Promise<import('./types.js').ChatResponse>}
     */
    async chat(req) {
        throw new LLMError('unreachable', 'chat() not implemented');
    }

    /**
     * Async generator that yields ChatChunk objects.
     * @param {import('./types.js').ChatRequest} req
     * @returns {AsyncGenerator<import('./types.js').ChatChunk>}
     */
    // eslint-disable-next-line require-yield
    async *chatStream(req) {
        throw new LLMError('unreachable', 'chatStream() not implemented');
    }

    /**
     * @param {import('./types.js').EmbedRequest} req
     * @returns {Promise<import('./types.js').EmbedResponse>}
     */
    async embed(req) {
        throw new LLMError('unreachable', 'embed() not implemented');
    }
}

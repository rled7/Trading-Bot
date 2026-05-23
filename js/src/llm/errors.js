/**
 * errors.js — LLMError for the LLM layer.
 * kind: "unreachable" | "timeout" | "http" | "decode" | "model_missing"
 */

export class LLMError extends Error {
    /**
     * @param {string} kind
     * @param {string} message
     * @param {number|null} [status]
     */
    constructor(kind, message, status = null) {
        super(message);
        this.name = 'LLMError';
        this.kind = kind;
        this.status = status;
    }
}

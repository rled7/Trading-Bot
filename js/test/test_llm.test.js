/**
 * test_llm.test.js — tests for the JS LLM layer.
 * Uses node:test and node:assert like test_analytics.test.js.
 *
 * Three groups:
 *   1. Provider tests — replay provider fixtures via MockTransport
 *   2. Use-case tests — replay use-case fixtures, verify prompt + output
 *   3. Error tests — 503 → http, unreachable, timeout, decode
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { OllamaProvider } from '../src/llm/ollama.js';
import { LLMError } from '../src/llm/errors.js';
import {
    loadFixture,
    MockTransport,
    ErrorTransport,
    UnreachableTransport,
    TimeoutTransport,
    MalformedJsonTransport,
} from '../src/llm/fixtures.js';
import {
    tradeRationale,
    backtestCommentary,
    newsSentiment,
    strategyDescribe,
    extractJson,
} from '../src/llm/useCases.js';

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Build an OllamaProvider whose transport is driven by a fixture.
 * @param {string} fixtureName
 * @returns {{ provider: OllamaProvider, transport: MockTransport }}
 */
function makeProviderFromFixture(fixtureName) {
    const fixture = loadFixture(fixtureName);
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });
    return { provider, transport, fixture };
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. Provider tests
// ─────────────────────────────────────────────────────────────────────────────

test('provider: complete_basic — request body matches fixture', async () => {
    const { provider, transport, fixture } = makeProviderFromFixture('complete_basic');
    const fixtureReq = fixture.request.body;

    const req = {
        model: fixtureReq.model,
        prompt: fixtureReq.prompt,
        temperature: fixtureReq.options.temperature,
        maxTokens: fixtureReq.options.num_predict,
        seed: fixtureReq.options.seed,
    };
    const resp = await provider.complete(req);

    // Verify outgoing request body
    const sent = transport.lastRequest.body;
    assert.equal(sent.model, fixtureReq.model);
    assert.equal(sent.prompt, fixtureReq.prompt);
    assert.equal(sent.stream, false);
    assert.equal(sent.options.temperature, fixtureReq.options.temperature);
    assert.equal(sent.options.num_predict, fixtureReq.options.num_predict);
    assert.equal(sent.options.seed, fixtureReq.options.seed);

    // Verify parsed response
    const body = fixture.response.body;
    assert.equal(resp.text, body.response);
    assert.equal(resp.model, body.model);
    assert.equal(resp.promptTokens, body.prompt_eval_count);
    assert.equal(resp.completionTokens, body.eval_count);
    assert.equal(resp.totalDurationNs, body.total_duration);
    assert.equal(resp.finishReason, body.done_reason);
});

test('provider: chat_basic — request body matches fixture', async () => {
    const { provider, transport, fixture } = makeProviderFromFixture('chat_basic');
    const fixtureReq = fixture.request.body;

    const req = {
        model: fixtureReq.model,
        messages: fixtureReq.messages.map(m => ({ role: m.role, content: m.content })),
        temperature: fixtureReq.options.temperature,
        maxTokens: fixtureReq.options.num_predict,
        seed: fixtureReq.options.seed,
    };
    const resp = await provider.chat(req);

    const sent = transport.lastRequest.body;
    assert.equal(sent.model, fixtureReq.model);
    assert.equal(sent.stream, false);
    assert.deepEqual(sent.messages, fixtureReq.messages);
    assert.equal(sent.options.temperature, fixtureReq.options.temperature);
    assert.equal(sent.options.num_predict, fixtureReq.options.num_predict);
    assert.equal(sent.options.seed, fixtureReq.options.seed);

    const body = fixture.response.body;
    assert.equal(resp.message.role, body.message.role);
    assert.equal(resp.message.content, body.message.content);
    assert.equal(resp.model, body.model);
    assert.equal(resp.promptTokens, body.prompt_eval_count);
    assert.equal(resp.completionTokens, body.eval_count);
    assert.equal(resp.totalDurationNs, body.total_duration);
    assert.equal(resp.finishReason, body.done_reason);
});

test('provider: chat_streaming — chunks match fixture', async () => {
    const { provider, fixture } = makeProviderFromFixture('chat_streaming');
    const fixtureReq = fixture.request.body;

    const req = {
        model: fixtureReq.model,
        messages: fixtureReq.messages.map(m => ({ role: m.role, content: m.content })),
        temperature: fixtureReq.options.temperature,
        maxTokens: fixtureReq.options.num_predict,
        seed: fixtureReq.options.seed,
    };

    const chunks = [];
    for await (const chunk of provider.chatStream(req)) {
        chunks.push(chunk);
    }

    const expected = fixture.response.body;
    assert.equal(chunks.length, expected.length);

    for (let i = 0; i < expected.length; i++) {
        const ex = expected[i];
        const ch = chunks[i];
        assert.equal(ch.delta, ex.message.content);
        assert.equal(ch.done, ex.done);
        if (ex.done) {
            assert.equal(ch.finishReason, ex.done_reason ?? null);
        } else {
            assert.equal(ch.finishReason, null);
        }
    }
});

test('provider: embed_basic — request body and response match fixture', async () => {
    const { provider, transport, fixture } = makeProviderFromFixture('embed_basic');
    const fixtureReq = fixture.request.body;

    const req = { model: fixtureReq.model, input: fixtureReq.prompt };
    const resp = await provider.embed(req);

    const sent = transport.lastRequest.body;
    assert.equal(sent.model, fixtureReq.model);
    assert.equal(sent.prompt, fixtureReq.prompt);

    const body = fixture.response.body;
    assert.equal(resp.model, fixtureReq.model);
    assert.equal(resp.embeddings.length, 1);
    assert.deepEqual(resp.embeddings[0], body.embedding);
});

test('provider: health_ok — returns ok=true with model_loaded', async () => {
    // health() does two calls: GET / then GET /api/tags
    // The health_ok fixture covers the /api/tags call.
    // We need a transport that handles both.
    const tagsFixture = loadFixture('health_ok');
    const transport = {
        calls: [],
        request(method, url, body, timeoutS) {
            this.calls.push({ method, url });
            if (url.endsWith('/')) {
                return { status: 200, body: 'Ollama is running' };
            }
            // /api/tags
            return { status: tagsFixture.response.status, body: tagsFixture.response.body };
        },
    };
    const provider = new OllamaProvider({ transport, model: 'llama3.1:8b' });
    const status = await provider.health();

    assert.equal(status.ok, true);
    assert.equal(status.model, 'llama3.1:8b');
    assert.equal(status.error, null);
    // model is in the list
    assert.equal(status.modelLoaded, true);
});

test('provider: health_down — returns ok=false on 503', async () => {
    const fixture = loadFixture('health_down');
    const transport = {
        request(method, url, body, timeoutS) {
            return { status: fixture.response.status, body: fixture.response.body };
        },
    };
    const provider = new OllamaProvider({ transport });
    const status = await provider.health();

    assert.equal(status.ok, false);
    assert.ok(status.error !== null);
});

test('provider: list_models — parses ModelInfo array', async () => {
    const { provider, fixture } = makeProviderFromFixture('list_models');

    const models = await provider.listModels();

    const expected = fixture.response.body.models;
    assert.equal(models.length, expected.length);
    for (let i = 0; i < expected.length; i++) {
        assert.equal(models[i].name, expected[i].name);
        assert.equal(models[i].sizeBytes, expected[i].size);
        assert.equal(models[i].modifiedAt, expected[i].modified_at);
    }
});

// ─────────────────────────────────────────────────────────────────────────────
// 2. Use-case tests
// ─────────────────────────────────────────────────────────────────────────────

// trade_rationale_long

test('use-case: trade_rationale_long — user prompt matches fixture', async () => {
    const fixture = loadFixture('trade_rationale_long');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    await tradeRationale(provider, {
        symbol: inp.symbol,
        side: inp.side,
        entryPrice: inp.entry_price,
        stopLoss: inp.stop_loss,
        takeProfit: inp.take_profit,
        indicators: inp.indicators,
        patterns: inp.patterns,
        timeframe: inp.timeframe,
    });

    const expectedUserContent = fixture.request.body.messages[1].content;
    const sentUserContent = transport.lastRequest.body.messages[1].content;
    assert.equal(sentUserContent, expectedUserContent);
});

test('use-case: trade_rationale_long — output fields correct', async () => {
    const fixture = loadFixture('trade_rationale_long');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    const result = await tradeRationale(provider, {
        symbol: inp.symbol,
        side: inp.side,
        entryPrice: inp.entry_price,
        stopLoss: inp.stop_loss,
        takeProfit: inp.take_profit,
        indicators: inp.indicators,
        patterns: inp.patterns,
        timeframe: inp.timeframe,
    });

    const respBody = fixture.response.body;
    assert.equal(result.explanation, respBody.message.content);
    assert.equal(result.model, respBody.model);
    assert.equal(result.tokens, respBody.eval_count);
});

// trade_rationale_short

test('use-case: trade_rationale_short — user prompt matches fixture', async () => {
    const fixture = loadFixture('trade_rationale_short');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    await tradeRationale(provider, {
        symbol: inp.symbol,
        side: inp.side,
        entryPrice: inp.entry_price,
        stopLoss: inp.stop_loss,
        takeProfit: inp.take_profit,
        indicators: inp.indicators,
        patterns: inp.patterns,
        timeframe: inp.timeframe,
    });

    const expectedUserContent = fixture.request.body.messages[1].content;
    const sentUserContent = transport.lastRequest.body.messages[1].content;
    assert.equal(sentUserContent, expectedUserContent);
});

test('use-case: trade_rationale_short — output fields correct', async () => {
    const fixture = loadFixture('trade_rationale_short');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    const result = await tradeRationale(provider, {
        symbol: inp.symbol,
        side: inp.side,
        entryPrice: inp.entry_price,
        stopLoss: inp.stop_loss,
        takeProfit: inp.take_profit,
        indicators: inp.indicators,
        patterns: inp.patterns,
        timeframe: inp.timeframe,
    });

    const respBody = fixture.response.body;
    assert.equal(result.explanation, respBody.message.content);
    assert.equal(result.model, respBody.model);
    assert.equal(result.tokens, respBody.eval_count);
});

// backtest_commentary_winning

test('use-case: backtest_commentary_winning — user prompt matches fixture', async () => {
    const fixture = loadFixture('backtest_commentary_winning');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    await backtestCommentary(provider, {
        strategyName: inp.strategy_name,
        period: inp.period,
        totalReturn: inp.total_return,
        sharpe: inp.sharpe,
        sortino: inp.sortino,
        maxDrawdown: inp.max_drawdown,
        winRate: inp.win_rate,
        tradeCount: inp.trade_count,
        bestTrade: inp.best_trade,
        worstTrade: inp.worst_trade,
    });

    const expectedUserContent = fixture.request.body.messages[1].content;
    const sentUserContent = transport.lastRequest.body.messages[1].content;
    assert.equal(sentUserContent, expectedUserContent);
});

test('use-case: backtest_commentary_winning — output fields correct', async () => {
    const fixture = loadFixture('backtest_commentary_winning');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    const result = await backtestCommentary(provider, {
        strategyName: inp.strategy_name,
        period: inp.period,
        totalReturn: inp.total_return,
        sharpe: inp.sharpe,
        sortino: inp.sortino,
        maxDrawdown: inp.max_drawdown,
        winRate: inp.win_rate,
        tradeCount: inp.trade_count,
        bestTrade: inp.best_trade,
        worstTrade: inp.worst_trade,
    });

    const respBody = fixture.response.body;
    assert.equal(result.commentary, respBody.message.content);
    assert.equal(result.model, respBody.model);
    assert.equal(result.tokens, respBody.eval_count);
});

// backtest_commentary_losing

test('use-case: backtest_commentary_losing — user prompt matches fixture', async () => {
    const fixture = loadFixture('backtest_commentary_losing');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    await backtestCommentary(provider, {
        strategyName: inp.strategy_name,
        period: inp.period,
        totalReturn: inp.total_return,
        sharpe: inp.sharpe,
        sortino: inp.sortino,
        maxDrawdown: inp.max_drawdown,
        winRate: inp.win_rate,
        tradeCount: inp.trade_count,
        bestTrade: inp.best_trade,
        worstTrade: inp.worst_trade,
    });

    const expectedUserContent = fixture.request.body.messages[1].content;
    const sentUserContent = transport.lastRequest.body.messages[1].content;
    assert.equal(sentUserContent, expectedUserContent);
});

test('use-case: backtest_commentary_losing — output fields correct', async () => {
    const fixture = loadFixture('backtest_commentary_losing');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    const result = await backtestCommentary(provider, {
        strategyName: inp.strategy_name,
        period: inp.period,
        totalReturn: inp.total_return,
        sharpe: inp.sharpe,
        sortino: inp.sortino,
        maxDrawdown: inp.max_drawdown,
        winRate: inp.win_rate,
        tradeCount: inp.trade_count,
        bestTrade: inp.best_trade,
        worstTrade: inp.worst_trade,
    });

    const respBody = fixture.response.body;
    assert.equal(result.commentary, respBody.message.content);
    assert.equal(result.model, respBody.model);
    assert.equal(result.tokens, respBody.eval_count);
});

// news_sentiment_bullish

test('use-case: news_sentiment_bullish — user prompt matches fixture', async () => {
    const fixture = loadFixture('news_sentiment_bullish');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    await newsSentiment(provider, inp.headline, { body: inp.body });

    const expectedUserContent = fixture.request.body.messages[1].content;
    const sentUserContent = transport.lastRequest.body.messages[1].content;
    assert.equal(sentUserContent, expectedUserContent);
});

test('use-case: news_sentiment_bullish — output parsed correctly', async () => {
    const fixture = loadFixture('news_sentiment_bullish');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    const result = await newsSentiment(provider, inp.headline, { body: inp.body });

    assert.equal(result.sentiment, 'bullish');
    assert.equal(typeof result.score, 'number');
    assert.equal(typeof result.confidence, 'number');
    assert.equal(typeof result.reason, 'string');
    assert.ok(result.reason.length > 0);
});

// news_sentiment_bearish

test('use-case: news_sentiment_bearish — user prompt matches fixture', async () => {
    const fixture = loadFixture('news_sentiment_bearish');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    await newsSentiment(provider, inp.headline, { body: inp.body });

    const expectedUserContent = fixture.request.body.messages[1].content;
    const sentUserContent = transport.lastRequest.body.messages[1].content;
    assert.equal(sentUserContent, expectedUserContent);
});

test('use-case: news_sentiment_bearish — output parsed correctly', async () => {
    const fixture = loadFixture('news_sentiment_bearish');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    const result = await newsSentiment(provider, inp.headline, { body: inp.body });

    assert.equal(result.sentiment, 'bearish');
    assert.ok(result.score < 0);
});

// news_sentiment_neutral

test('use-case: news_sentiment_neutral — user prompt matches fixture', async () => {
    const fixture = loadFixture('news_sentiment_neutral');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    await newsSentiment(provider, inp.headline, { body: inp.body });

    const expectedUserContent = fixture.request.body.messages[1].content;
    const sentUserContent = transport.lastRequest.body.messages[1].content;
    assert.equal(sentUserContent, expectedUserContent);
});

test('use-case: news_sentiment_neutral — output parsed correctly', async () => {
    const fixture = loadFixture('news_sentiment_neutral');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    const result = await newsSentiment(provider, inp.headline, { body: inp.body });

    assert.equal(result.sentiment, 'neutral');
});

// strategy_describe_breakout

test('use-case: strategy_describe_breakout — user prompt matches fixture', async () => {
    const fixture = loadFixture('strategy_describe_breakout');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    await strategyDescribe(provider, {
        name: inp.name,
        indicatorsUsed: inp.indicators_used,
        entryRules: inp.entry_rules,
        exitRules: inp.exit_rules,
        riskRules: inp.risk_rules,
        timeframe: inp.timeframe,
    });

    const expectedUserContent = fixture.request.body.messages[1].content;
    const sentUserContent = transport.lastRequest.body.messages[1].content;
    assert.equal(sentUserContent, expectedUserContent);
});

test('use-case: strategy_describe_breakout — output fields correct', async () => {
    const fixture = loadFixture('strategy_describe_breakout');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    const result = await strategyDescribe(provider, {
        name: inp.name,
        indicatorsUsed: inp.indicators_used,
        entryRules: inp.entry_rules,
        exitRules: inp.exit_rules,
        riskRules: inp.risk_rules,
        timeframe: inp.timeframe,
    });

    const respBody = fixture.response.body;
    assert.equal(result.description, respBody.message.content);
    assert.equal(result.model, respBody.model);
    assert.equal(result.tokens, respBody.eval_count);
});

// strategy_describe_meanrev

test('use-case: strategy_describe_meanrev — user prompt matches fixture', async () => {
    const fixture = loadFixture('strategy_describe_meanrev');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    await strategyDescribe(provider, {
        name: inp.name,
        indicatorsUsed: inp.indicators_used,
        entryRules: inp.entry_rules,
        exitRules: inp.exit_rules,
        riskRules: inp.risk_rules,
        timeframe: inp.timeframe,
    });

    const expectedUserContent = fixture.request.body.messages[1].content;
    const sentUserContent = transport.lastRequest.body.messages[1].content;
    assert.equal(sentUserContent, expectedUserContent);
});

test('use-case: strategy_describe_meanrev — output fields correct', async () => {
    const fixture = loadFixture('strategy_describe_meanrev');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    const result = await strategyDescribe(provider, {
        name: inp.name,
        indicatorsUsed: inp.indicators_used,
        entryRules: inp.entry_rules,
        exitRules: inp.exit_rules,
        riskRules: inp.risk_rules,
        timeframe: inp.timeframe,
    });

    const respBody = fixture.response.body;
    assert.equal(result.description, respBody.message.content);
    assert.equal(result.model, respBody.model);
    assert.equal(result.tokens, respBody.eval_count);
});

// ─────────────────────────────────────────────────────────────────────────────
// 3. Error tests
// ─────────────────────────────────────────────────────────────────────────────

test('error: 503 → LLMError kind="http" status=503', async () => {
    const provider = new OllamaProvider({ transport: new ErrorTransport(503) });
    await assert.rejects(
        () => provider.chat({
            model: 'llama3.1:8b',
            messages: [{ role: 'user', content: 'hi' }],
        }),
        err => {
            assert.ok(err instanceof LLMError);
            assert.equal(err.kind, 'http');
            assert.equal(err.status, 503);
            return true;
        },
    );
});

test('error: 404 → LLMError kind="http" status=404', async () => {
    const provider = new OllamaProvider({ transport: new ErrorTransport(404) });
    await assert.rejects(
        () => provider.complete({
            model: 'llama3.1:8b',
            prompt: 'test',
        }),
        err => {
            assert.ok(err instanceof LLMError);
            assert.equal(err.kind, 'http');
            assert.equal(err.status, 404);
            return true;
        },
    );
});

test('error: connection refused → LLMError kind="unreachable"', async () => {
    const provider = new OllamaProvider({ transport: new UnreachableTransport() });
    await assert.rejects(
        () => provider.chat({
            model: 'llama3.1:8b',
            messages: [{ role: 'user', content: 'hi' }],
        }),
        err => {
            assert.ok(err instanceof LLMError);
            assert.equal(err.kind, 'unreachable');
            return true;
        },
    );
});

test('error: timeout → LLMError kind="timeout"', async () => {
    const provider = new OllamaProvider({ transport: new TimeoutTransport() });
    await assert.rejects(
        () => provider.complete({
            model: 'llama3.1:8b',
            prompt: 'test',
        }),
        err => {
            assert.ok(err instanceof LLMError);
            assert.equal(err.kind, 'timeout');
            return true;
        },
    );
});

test('error: malformed JSON in sentiment → LLMError kind="decode"', async () => {
    const provider = new OllamaProvider({ transport: new MalformedJsonTransport() });
    await assert.rejects(
        () => newsSentiment(provider, 'test headline'),
        err => {
            assert.ok(err instanceof LLMError);
            assert.equal(err.kind, 'decode');
            return true;
        },
    );
});

// ─────────────────────────────────────────────────────────────────────────────
// 4. extractJson unit tests (spec §9)
// ─────────────────────────────────────────────────────────────────────────────

test('extractJson: simple object', () => {
    const result = extractJson('{"sentiment":"bullish","score":0.85}');
    assert.equal(result.sentiment, 'bullish');
    assert.equal(result.score, 0.85);
});

test('extractJson: object with prose around it', () => {
    const result = extractJson('Here is the result: {"x":1,"y":"hello"} and nothing else.');
    assert.equal(result.x, 1);
    assert.equal(result.y, 'hello');
});

test('extractJson: nested object', () => {
    const result = extractJson('prefix {"outer":{"inner":42}} suffix');
    assert.equal(result.outer.inner, 42);
});

test('extractJson: string with braces inside value', () => {
    const result = extractJson('{"reason":"rate {up} today","score":0.5}');
    assert.equal(result.reason, 'rate {up} today');
    assert.equal(result.score, 0.5);
});

test('extractJson: throws LLMError kind=decode when no JSON found', () => {
    assert.throws(
        () => extractJson('no json here at all'),
        err => {
            assert.ok(err instanceof LLMError);
            assert.equal(err.kind, 'decode');
            return true;
        },
    );
});

// ─────────────────────────────────────────────────────────────────────────────
// 5. Prompt byte-identity spot checks
// ─────────────────────────────────────────────────────────────────────────────

test('prompts: TRADE_RATIONALE_SYSTEM contains em-dash range (1–3)', async () => {
    const { TRADE_RATIONALE_SYSTEM } = await import('../src/llm/prompts.js');
    assert.ok(TRADE_RATIONALE_SYSTEM.includes('1\u20133'));
});

test('prompts: BACKTEST_COMMENTARY_SYSTEM contains em-dash range (3–5)', async () => {
    const { BACKTEST_COMMENTARY_SYSTEM } = await import('../src/llm/prompts.js');
    assert.ok(BACKTEST_COMMENTARY_SYSTEM.includes('3\u20135'));
});

test('prompts: STRATEGY_DESCRIBE_SYSTEM contains em-dash range (2–4)', async () => {
    const { STRATEGY_DESCRIBE_SYSTEM } = await import('../src/llm/prompts.js');
    assert.ok(STRATEGY_DESCRIBE_SYSTEM.includes('2\u20134'));
});

// ─────────────────────────────────────────────────────────────────────────────
// 6. OllamaProvider.fromEnv smoke test
// ─────────────────────────────────────────────────────────────────────────────

test('fromEnv: reads defaults when no env vars set', () => {
    // Remove any AF_LLM_* vars for this test
    const saved = {};
    const keys = ['AF_LLM_HOST', 'AF_LLM_MODEL', 'AF_LLM_EMBED_MODEL', 'AF_LLM_TIMEOUT',
                  'AF_LLM_SEED', 'AF_LLM_TEMPERATURE', 'AF_LLM_MAX_TOKENS'];
    for (const k of keys) {
        saved[k] = process.env[k];
        delete process.env[k];
    }
    try {
        const p = OllamaProvider.fromEnv();
        assert.equal(p._host, 'http://localhost:11434');
        assert.equal(p._model, 'llama3.1:8b');
        assert.equal(p._embedModel, 'nomic-embed-text');
        assert.equal(p._seed, 42);
        assert.equal(p._temperature, 0.2);
        assert.equal(p._maxTokens, 512);
    } finally {
        for (const k of keys) {
            if (saved[k] !== undefined) process.env[k] = saved[k];
        }
    }
});

test('fromEnv: reads custom env vars', () => {
    process.env.AF_LLM_HOST = 'http://myhost:1234';
    process.env.AF_LLM_MODEL = 'mistral:7b';
    process.env.AF_LLM_SEED = '99';
    process.env.AF_LLM_TEMPERATURE = '0.7';
    process.env.AF_LLM_MAX_TOKENS = '256';
    try {
        const p = OllamaProvider.fromEnv();
        assert.equal(p._host, 'http://myhost:1234');
        assert.equal(p._model, 'mistral:7b');
        assert.equal(p._seed, 99);
        assert.equal(p._temperature, 0.7);
        assert.equal(p._maxTokens, 256);
    } finally {
        delete process.env.AF_LLM_HOST;
        delete process.env.AF_LLM_MODEL;
        delete process.env.AF_LLM_SEED;
        delete process.env.AF_LLM_TEMPERATURE;
        delete process.env.AF_LLM_MAX_TOKENS;
    }
});

// ─────────────────────────────────────────────────────────────────────────────
// 7. Indicator serialization — sorted keys, no spaces
// ─────────────────────────────────────────────────────────────────────────────

test('indicators: sorted keys match Python json.dumps(sort_keys=True, separators=(",",":"))', async () => {
    // trade_rationale_short has {"ema_fast":189.45,"ema_slow":189.8,"rsi":31.5}
    // alphabetical order: ema_fast < ema_slow < rsi
    const fixture = loadFixture('trade_rationale_short');
    const transport = new MockTransport(fixture);
    const provider = new OllamaProvider({ transport });

    const inp = fixture.input;
    await tradeRationale(provider, {
        symbol: inp.symbol,
        side: inp.side,
        entryPrice: inp.entry_price,
        stopLoss: inp.stop_loss,
        takeProfit: inp.take_profit,
        indicators: inp.indicators,
        patterns: inp.patterns,
        timeframe: inp.timeframe,
    });

    const sentBody = transport.lastRequest.body;
    const userContent = sentBody.messages[1].content;
    // The fixture's expected user content has sorted indicators
    const expectedUserContent = fixture.request.body.messages[1].content;
    assert.equal(userContent, expectedUserContent);

    // Also verify the indicators substring directly
    assert.ok(userContent.includes('{"ema_fast":189.45,"ema_slow":189.8,"rsi":31.5}'));
});

// ─────────────────────────────────────────────────────────────────────────────
// 8. Embed batching
// ─────────────────────────────────────────────────────────────────────────────

test('embed: batched input sends two requests and gathers embeddings', async () => {
    const singleEmbedding = [0.1, 0.2, 0.3];
    const calls = [];
    const transport = {
        request(method, url, body, _timeout) {
            calls.push(body.prompt);
            return { status: 200, body: { embedding: singleEmbedding } };
        },
    };
    const provider = new OllamaProvider({ transport });
    const resp = await provider.embed({ model: 'nomic-embed-text', input: ['foo', 'bar'] });

    assert.equal(calls.length, 2);
    assert.equal(calls[0], 'foo');
    assert.equal(calls[1], 'bar');
    assert.equal(resp.embeddings.length, 2);
    assert.deepEqual(resp.embeddings[0], singleEmbedding);
    assert.deepEqual(resp.embeddings[1], singleEmbedding);
});

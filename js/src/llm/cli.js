#!/usr/bin/env node
/**
 * cli.js — CLI entrypoint for the AlgoForge LLM layer.
 * Usage: node js/src/llm/cli.js <subcommand> [flags]
 *
 * Subcommands:
 *   rationale  --signal signal.json  [--model M] [--out out.txt]
 *   commentary --backtest results.json [--model M] [--out out.txt]
 *   sentiment  --headline "…" [--body "…"]
 *   describe   --strategy strategy.json
 *   health
 *   models
 *
 * Exit codes: 0 ok, 1 user error, 2 LLM unreachable, 3 decode error.
 */

import { readFileSync, writeFileSync } from 'node:fs';
import { OllamaProvider } from './ollama.js';
import { tradeRationale, backtestCommentary, newsSentiment, strategyDescribe } from './useCases.js';
import { LLMError } from './errors.js';

function loadJson(path) {
    try {
        return JSON.parse(readFileSync(path, 'utf-8'));
    } catch (err) {
        process.stderr.write(`error: cannot read file: ${path}: ${err.message}\n`);
        process.exit(1);
    }
}

function writeOut(text, out) {
    if (out) {
        writeFileSync(out, text, 'utf-8');
    } else {
        process.stdout.write(text + '\n');
    }
}

function handleLLMError(err) {
    process.stderr.write(`error: ${err.message}\n`);
    if (err.kind === 'unreachable' || err.kind === 'timeout') {
        process.exit(2);
    }
    process.exit(3);
}

// ── Argument parser (no external deps) ───────────────────────────────────────

function parseArgs(argv) {
    const args = argv.slice(2);
    const result = { subcommand: null, flags: {} };
    if (args.length === 0) {
        process.stderr.write('usage: cli.js <subcommand> [flags]\n');
        process.exit(1);
    }
    result.subcommand = args[0];
    for (let i = 1; i < args.length; i++) {
        const a = args[i];
        if (a.startsWith('--')) {
            const key = a.slice(2);
            const val = args[i + 1] && !args[i + 1].startsWith('--') ? args[++i] : true;
            result.flags[key] = val;
        }
    }
    return result;
}

// ── Subcommand handlers ───────────────────────────────────────────────────────

async function cmdRationale(flags) {
    if (!flags.signal) {
        process.stderr.write('error: --signal is required\n');
        process.exit(1);
    }
    const data = loadJson(flags.signal);
    const inp = {
        symbol: data.symbol,
        side: data.side,
        entryPrice: parseFloat(data.entry_price),
        stopLoss: parseFloat(data.stop_loss),
        takeProfit: parseFloat(data.take_profit),
        indicators: data.indicators ?? {},
        patterns: data.patterns ?? [],
        timeframe: data.timeframe,
    };
    const provider = OllamaProvider.fromEnv();
    const model = flags.model || provider._model;
    try {
        const result = await tradeRationale(provider, inp, { model });
        writeOut(result.explanation, flags.out || null);
    } catch (err) {
        if (err instanceof LLMError) handleLLMError(err);
        throw err;
    }
}

async function cmdCommentary(flags) {
    if (!flags.backtest) {
        process.stderr.write('error: --backtest is required\n');
        process.exit(1);
    }
    const data = loadJson(flags.backtest);
    const inp = {
        strategyName: data.strategy_name,
        period: data.period,
        totalReturn: parseFloat(data.total_return),
        sharpe: parseFloat(data.sharpe),
        sortino: parseFloat(data.sortino),
        maxDrawdown: parseFloat(data.max_drawdown),
        winRate: parseFloat(data.win_rate),
        tradeCount: parseInt(data.trade_count, 10),
        bestTrade: parseFloat(data.best_trade),
        worstTrade: parseFloat(data.worst_trade),
    };
    const provider = OllamaProvider.fromEnv();
    const model = flags.model || provider._model;
    try {
        const result = await backtestCommentary(provider, inp, { model });
        writeOut(result.commentary, flags.out || null);
    } catch (err) {
        if (err instanceof LLMError) handleLLMError(err);
        throw err;
    }
}

async function cmdSentiment(flags) {
    if (!flags.headline) {
        process.stderr.write('error: --headline is required\n');
        process.exit(1);
    }
    const provider = OllamaProvider.fromEnv();
    try {
        const result = await newsSentiment(provider, flags.headline, { body: flags.body || '' });
        process.stdout.write(JSON.stringify({
            sentiment: result.sentiment,
            score: result.score,
            confidence: result.confidence,
            reason: result.reason,
        }, null, 2) + '\n');
    } catch (err) {
        if (err instanceof LLMError) handleLLMError(err);
        throw err;
    }
}

async function cmdDescribe(flags) {
    if (!flags.strategy) {
        process.stderr.write('error: --strategy is required\n');
        process.exit(1);
    }
    const data = loadJson(flags.strategy);
    const inp = {
        name: data.name,
        indicatorsUsed: data.indicators_used ?? [],
        entryRules: data.entry_rules ?? [],
        exitRules: data.exit_rules ?? [],
        riskRules: data.risk_rules ?? [],
        timeframe: data.timeframe,
    };
    const provider = OllamaProvider.fromEnv();
    try {
        const result = await strategyDescribe(provider, inp);
        process.stdout.write(result.description + '\n');
    } catch (err) {
        if (err instanceof LLMError) handleLLMError(err);
        throw err;
    }
}

async function cmdHealth() {
    const provider = OllamaProvider.fromEnv();
    try {
        const status = await provider.health();
        process.stdout.write(JSON.stringify({
            ok: status.ok,
            model_loaded: status.modelLoaded,
            model: status.model,
            error: status.error,
        }, null, 2) + '\n');
        if (!status.ok) process.exit(2);
    } catch (err) {
        if (err instanceof LLMError) handleLLMError(err);
        throw err;
    }
}

async function cmdModels() {
    const provider = OllamaProvider.fromEnv();
    try {
        const models = await provider.listModels();
        for (const m of models) {
            process.stdout.write(`${m.name}\t${m.sizeBytes}\t${m.modifiedAt}\n`);
        }
    } catch (err) {
        if (err instanceof LLMError) handleLLMError(err);
        throw err;
    }
}

// ── Main ──────────────────────────────────────────────────────────────────────

async function main() {
    const { subcommand, flags } = parseArgs(process.argv);
    switch (subcommand) {
        case 'rationale':   await cmdRationale(flags); break;
        case 'commentary':  await cmdCommentary(flags); break;
        case 'sentiment':   await cmdSentiment(flags); break;
        case 'describe':    await cmdDescribe(flags); break;
        case 'health':      await cmdHealth(); break;
        case 'models':      await cmdModels(); break;
        default:
            process.stderr.write(`error: unknown subcommand: ${subcommand}\n`);
            process.exit(1);
    }
}

main().catch(err => {
    process.stderr.write(`fatal: ${err.message}\n`);
    process.exit(1);
});

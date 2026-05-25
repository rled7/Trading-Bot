#!/usr/bin/env node
/**
 * cli.js — CLI entrypoint for the algo_gen validator + promoter.
 *
 * Usage:
 *   node js/src/algo_gen/cli.js validate <manifest.json> [--bars bars.json] [--seed 42]
 *   node js/src/algo_gen/cli.js promote  <manifest.json> [--confirm] [--out registry/]
 *
 * Exit codes: 0 ok, 1 user error, 2 validation failed, 3 promotion refused.
 *
 * No 'generate' subcommand — LLM generation is Python-only per spec §10.
 */

import { readFileSync } from 'node:fs';
import { validate } from './validator.js';
import { promote, PromotionError } from './promote.js';
import { validateManifest } from './schema.js';
import { loadBars } from './fixtures.js';
import { BTConfig } from '../backtest.js';

// ── Helpers ───────────────────────────────────────────────────────────────────

function loadJson(path) {
    try {
        return JSON.parse(readFileSync(path, 'utf-8'));
    } catch (err) {
        process.stderr.write(`error: cannot read file: ${path}: ${err.message}\n`);
        process.exit(1);
    }
}

function parseArgs(argv) {
    const args   = argv.slice(2);
    const result = { subcommand: null, flags: {}, positional: [] };
    if (args.length === 0) {
        process.stderr.write('usage: cli.js <validate|promote> [flags]\n');
        process.exit(1);
    }
    result.subcommand = args[0];
    for (let i = 1; i < args.length; i++) {
        const a = args[i];
        if (a.startsWith('--')) {
            const key = a.slice(2);
            const next = args[i + 1];
            const val  = next && !next.startsWith('--') ? args[++i] : true;
            result.flags[key] = val;
        } else {
            result.positional.push(a);
        }
    }
    return result;
}

// ── Subcommand handlers ───────────────────────────────────────────────────────

async function cmdValidate(positional, flags) {
    const manifestPath = positional[0];
    if (!manifestPath) {
        process.stderr.write('error: manifest path is required\n');
        process.exit(1);
    }

    const manifestDict = loadJson(manifestPath);

    // Load bars fixture
    const barsName = flags.bars || 'bars_eurusd_h1_2y.json';
    let bars;
    try {
        bars = loadBars(barsName);
    } catch (err) {
        process.stderr.write(`error: bar fixture '${barsName}' not found: ${err.message}\n`);
        process.exit(1);
    }

    const seed = flags.seed ? parseInt(flags.seed, 10) : 42;

    process.stdout.write(`Validating '${manifestDict.name || '?'}' ...\n`);
    const result = await validate(manifestDict, bars, seed);

    for (const stage of result.stages) {
        const status = stage.passed ? 'PASS' : 'FAIL';
        process.stdout.write(`  Stage ${stage.stage} [${status}] ${stage.name}: ${stage.reason}\n`);
    }

    if (result.passed && result.report) {
        const r = result.report;
        process.stdout.write(`\nResult: PASS — tier=${r.tier}  min_score=${r.minScore.toFixed(2)}\n`);
        process.stdout.write(`  WF=${r.walkForward.toFixed(2)}  MC=${r.mcBootstrap.toFixed(2)}  Rob=${r.robustness.toFixed(2)}\n`);
        process.stdout.write(`  size_mult=${r.sizeMult}  experimental=${r.experimental}  paper_only=${r.paperOnly}\n`);
        process.exit(0);
    } else {
        process.stdout.write(`\nResult: FAIL\n`);
        process.exit(2);
    }
}

async function cmdPromote(positional, flags) {
    const manifestPath = positional[0];
    if (!manifestPath) {
        process.stderr.write('error: manifest path is required\n');
        process.exit(1);
    }

    const manifestDict = loadJson(manifestPath);
    const barsName     = flags.bars || 'bars_eurusd_h1_2y.json';
    let bars;
    try {
        bars = loadBars(barsName);
    } catch (err) {
        process.stderr.write(`error: bar fixture '${barsName}' not found: ${err.message}\n`);
        process.exit(1);
    }

    const seed    = flags.seed    ? parseInt(flags.seed, 10) : 42;
    const confirm = Boolean(flags.confirm);

    process.stdout.write(`Validating '${manifestDict.name || '?'}' ...\n`);
    const result = await validate(manifestDict, bars, seed);

    if (!result.passed || !result.report) {
        process.stdout.write('Validation FAILED — cannot promote\n');
        for (const stage of result.stages) {
            if (!stage.passed) {
                process.stdout.write(`  Failed at stage ${stage.stage}: ${stage.reason}\n`);
            }
        }
        process.exit(2);
    }

    // Parse manifest for promote()
    let manifest;
    try {
        manifest = validateManifest(manifestDict);
    } catch (err) {
        process.stderr.write(`error: manifest parse failed: ${err.message}\n`);
        process.exit(1);
    }

    const registryDir = flags.out || './registry';
    try {
        const algo = promote(result.report, manifest, registryDir, { confirm });
        process.stdout.write(
            `Promoted '${manifest.name}' → tier=${result.report.tier}  ` +
            `size_mult=${result.report.sizeMult}\n`
        );
        process.stdout.write(`  Manifest written to: ${algo.metadata.manifest_path}\n`);
        process.exit(0);
    } catch (err) {
        if (err instanceof PromotionError) {
            process.stderr.write(`Promotion refused: ${err.message}\n`);
            process.exit(3);
        }
        throw err;
    }
}

// ── Main ──────────────────────────────────────────────────────────────────────

async function main() {
    const { subcommand, flags, positional } = parseArgs(process.argv);
    switch (subcommand) {
        case 'validate': await cmdValidate(positional, flags); break;
        case 'promote':  await cmdPromote(positional, flags);  break;
        default:
            process.stderr.write(`error: unknown subcommand: ${subcommand}\n`);
            process.stderr.write('usage: cli.js <validate|promote> [flags]\n');
            process.exit(1);
    }
}

main().catch(err => {
    process.stderr.write(`fatal: ${err.message}\n`);
    process.exit(1);
});

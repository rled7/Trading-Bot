import { test } from "node:test";
import assert from "node:assert/strict";

import { Bar, Direction, Timeframe } from "../src/types.js";
import { VERSION } from "../src/index.js";

test("version is a non-empty string", () => {
    assert.ok(typeof VERSION === "string" && VERSION.length > 0);
});

test("timeframes in seconds", () => {
    assert.equal(Timeframe.H1, 3600);
    assert.equal(Timeframe.W1, 604800);
});

test("direction values match blueprint", () => {
    assert.equal(Direction.LONG, 1);
    assert.equal(Direction.SHORT, -1);
    assert.equal(Direction.NONE, 0);
});

test("Bar derived getters", () => {
    const b = new Bar({
        timestamp: 1, open: 1.10, high: 1.12, low: 1.09,
        close: 1.11, volume: 100,
    });
    assert.ok(Math.abs(b.body - 0.01) < 1e-10);
    assert.ok(Math.abs(b.range - 0.03) < 1e-10);
    assert.equal(b.isBullish, true);
});

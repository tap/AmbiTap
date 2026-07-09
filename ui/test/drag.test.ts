import assert from 'node:assert/strict';
import { test } from 'node:test';
import { ValueGate } from '../core/drag.js';

test('gate accepts external values when idle', () => {
    const gate = new ValueGate(250);
    assert.ok(gate.acceptExternal(0));
});

test('gate blocks during drag and for holdMs after release', () => {
    const gate = new ValueGate(250);
    gate.beginDrag();
    assert.ok(gate.isDragging());
    assert.equal(gate.acceptExternal(100), false);
    gate.endDrag(1000);
    assert.equal(gate.isDragging(), false);
    assert.equal(gate.acceptExternal(1100), false);
    assert.ok(gate.acceptExternal(1250));
});

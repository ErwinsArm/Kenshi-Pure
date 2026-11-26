// Tests for session-size.js, run: `node --test .claude/hooks/session-size.test.js`.
"use strict";

const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const { historySize, noteFor, LIMIT } = require("./session-size");

const line = (usage) => JSON.stringify({ type: "assistant", message: { usage } });

test("reads the newest usage line and skips a cut first line", () => {
	const text = ['{"cut', line({ input_tokens: 5 }), line({ input_tokens: 1, cache_read_input_tokens: 10, cache_creation_input_tokens: 2 }), '{"type":"user"}'].join("\n");
	assert.equal(historySize(text), 13);
	assert.equal(historySize('{"type":"user"}'), null);
});

test("silent at or below the limit, one line above it", () => {
	assert.equal(noteFor(null), null);
	assert.equal(noteFor(LIMIT), null);
	assert.match(noteFor(LIMIT + 1), /Session size/);
});

test("end to end: prints the note for a large transcript, nothing for a small one or bad input", () => {
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), "size-"));
	const big = path.join(dir, "big.jsonl");
	const small = path.join(dir, "small.jsonl");
	fs.writeFileSync(big, line({ input_tokens: 1, cache_read_input_tokens: LIMIT }) + "\n");
	fs.writeFileSync(small, line({ input_tokens: 10 }) + "\n");
	const run = (input) => spawnSync(process.execPath, [path.join(__dirname, "session-size.js")], { input, encoding: "utf8" });
	assert.match(run(JSON.stringify({ transcript_path: big })).stdout, /Session size/);
	assert.equal(run(JSON.stringify({ transcript_path: small })).stdout, "");
	assert.equal(run(JSON.stringify({ transcript_path: path.join(dir, "missing.jsonl") })).stdout, "");
	const bad = run("not json");
	assert.equal(bad.status, 0);
	assert.match(bad.stdout, /Session size check failed/);
	fs.rmSync(dir, { recursive: true, force: true });
});

// Tests for reply-length.js, run: `node --test .claude/hooks/reply-length.test.js`.
"use strict";

const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const { lastReplyWords, noteFor, LIMIT } = require("./reply-length");

const assistant = (...blocks) => JSON.stringify({ type: "assistant", message: { content: blocks } });
const text = (n) => ({ type: "text", text: Array(n).fill("word").join(" ") });
const toolUse = { type: "tool_use", name: "Bash", input: {} };
const user = JSON.stringify({ type: "user", message: { content: "hi" } });

test("counts the words of the newest assistant message that has text", () => {
	const transcript = [assistant(text(500)), user, assistant(text(12)), assistant(toolUse)].join("\n");
	assert.equal(lastReplyWords(transcript), 12);
});

test("joins several text blocks and ignores tool calls", () => {
	assert.equal(lastReplyWords(assistant(text(3), toolUse, text(4))), 7);
});

test("no assistant text means no count; a cut first line is skipped", () => {
	assert.equal(lastReplyWords(user), null);
	assert.equal(lastReplyWords(`{"type":"assistant","mess\n${assistant(text(5))}`), 5);
});

test("a note only past the limit", () => {
	assert.equal(noteFor(null), null);
	assert.equal(noteFor(LIMIT), null);
	assert.match(noteFor(LIMIT + 1), /your last reply was \d+ words/);
});

test("the hook prints the note for a long reply, stays silent otherwise, and never fails", () => {
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), "reply-length-"));
	const run = (lines, input) => {
		const file = path.join(dir, "t.jsonl");
		fs.writeFileSync(file, lines.join("\n") + "\n");
		return spawnSync("node", [path.join(__dirname, "reply-length.js")], { input: input ?? JSON.stringify({ transcript_path: file }), encoding: "utf8" });
	};
	const long = run([user, assistant(text(LIMIT + 50))]);
	assert.equal(long.status, 0);
	assert.match(long.stdout, /Reply length/);
	assert.equal(run([user, assistant(text(20))]).stdout, "");
	const broken = run([], "not json");
	assert.equal(broken.status, 0);
	assert.equal(broken.stdout, "");
	fs.rmSync(dir, { recursive: true, force: true });
});

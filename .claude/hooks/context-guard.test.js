// Tests for context-guard.js, run: `node --test .claude/hooks/context-guard.test.js`.
"use strict";

const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const { decide, historySize, transcriptOf, SOFT, HARD } = require("./context-guard");

const line = (usage) => JSON.stringify({ type: "assistant", message: { usage } });

test("history size is the newest assistant message's input plus cache tokens", () => {
	const text = ["{cut line", line({ input_tokens: 1, cache_read_input_tokens: 100, cache_creation_input_tokens: 10 }), line({ input_tokens: 2, cache_read_input_tokens: 300, cache_creation_input_tokens: 20 }), '{"type":"user"}'].join("\n");
	assert.equal(historySize(text), 322);
	assert.equal(historySize('{"type":"user"}\n'), null);
});

test("only subagent calls are checked", () => {
	assert.equal(transcriptOf({ transcript_path: "/p/s1.jsonl" }), null);
	assert.equal(transcriptOf({ transcript_path: "/p/s1.jsonl", agent_id: "" }), null);
	assert.equal(transcriptOf({ transcript_path: "/p/s1.jsonl", agent_id: "abc" }), path.join("/p/s1", "subagents", "agent-abc.jsonl"));
});

test("below soft: allowed; soft: reminded; hard: denied except git and the note", () => {
	assert.equal(decide(null, "Bash", {}).action, "allow");
	assert.equal(decide(SOFT - 1, "Read", {}).action, "allow");
	assert.equal(decide(SOFT, "Read", {}).action, "remind");
	assert.equal(decide(HARD, "Read", {}).action, "deny");
	assert.equal(decide(HARD, "Bash", { command: "rm -rf x" }).action, "deny");
	assert.equal(decide(HARD, "Bash", { command: "git commit -m x -- a" }).action, "allow");
	assert.equal(decide(HARD, "Bash", { command: 'cd "/c/x" && git status' }).action, "allow");
	for (const command of ["git status; rm -rf x", "git status && node build.js", "git log | node x.js", "lune run tools/check", "git status\nnode x.js", "git $(node x.js)", "node x.js"]) {
		assert.equal(decide(HARD, "Bash", { command }).action, "deny", command);
	}
	assert.equal(decide(HARD, "Write", { file_path: "C:/Users/x/side-session-notes/s/handover.md" }).action, "allow");
	assert.equal(decide(HARD, "Edit", { file_path: "C:/repo/notes/AnarchyKenshi.md" }).action, "deny");
	assert.equal(decide(HARD, "mcp__rea__decompile", {}).action, "deny");
});

test("end to end: reads the subagent's transcript and prints the hook's answer", () => {
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), "guard-"));
	const sub = path.join(dir, "s1", "subagents");
	fs.mkdirSync(sub, { recursive: true });
	fs.writeFileSync(path.join(sub, "agent-a1.jsonl"), line({ input_tokens: 1, cache_read_input_tokens: HARD }) + "\n");
	const run = (event) => spawnSync(process.execPath, [path.join(__dirname, "context-guard.js")], { input: JSON.stringify(event), encoding: "utf8" });
	const base = { transcript_path: path.join(dir, "s1.jsonl"), tool_name: "Read", tool_input: {} };
	const denied = run({ ...base, agent_id: "a1" });
	assert.equal(denied.status, 0);
	assert.equal(JSON.parse(denied.stdout).hookSpecificOutput.permissionDecision, "deny");
	assert.equal(run(base).stdout, "", "main session: nothing printed");
	assert.equal(run({ ...base, agent_id: "missing" }).stdout, "", "no transcript yet: allowed");
	// A last line bigger than the first tail: the size is still found further back.
	fs.writeFileSync(path.join(sub, "agent-a2.jsonl"), line({ input_tokens: 1, cache_read_input_tokens: HARD }) + "\n" + JSON.stringify({ type: "blob", data: "x".repeat(700 * 1024) }) + "\n");
	assert.equal(JSON.parse(run({ ...base, agent_id: "a2" }).stdout).hookSpecificOutput.permissionDecision, "deny");
	// No size anywhere in a big transcript: let through, but loudly.
	fs.writeFileSync(path.join(sub, "agent-a3.jsonl"), JSON.stringify({ type: "blob", data: "x".repeat(100 * 1024) }) + "\n");
	const missed = run({ ...base, agent_id: "a3" });
	assert.equal(missed.status, 1);
	assert.match(missed.stderr, /no history size found/);
	const broken = spawnSync(process.execPath, [path.join(__dirname, "context-guard.js")], { input: "not json", encoding: "utf8" });
	assert.equal(broken.status, 1);
	fs.rmSync(dir, { recursive: true, force: true });
});

// Tests for require-model.js, run: `node --test .claude/hooks/require-model.test.js`.
"use strict";

const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const path = require("node:path");
const test = require("node:test");

const { decide, output } = require("./require-model");

const START = "mcp__ccd_session__start_session";

test("Agent needs both a model and an effort", () => {
	assert.equal(decide("Agent", { prompt: "x" }).action, "deny");
	assert.equal(decide("Agent", { prompt: "x", model: "", effort: "high" }).action, "deny");
	assert.equal(decide("Agent", { prompt: "x", model: "  ", effort: "high" }).action, "deny");
	assert.equal(decide("Agent", {}).action, "deny");
	assert.equal(decide("Agent", { prompt: "x", model: "sonnet" }).action, "deny", "model without effort");
	assert.equal(decide("Agent", { prompt: "x", effort: "high" }).action, "deny", "effort without model");
	assert.equal(decide("Agent", { prompt: "x", model: "sonnet", effort: "high" }).action, "allow");
});

test("inherit, unknown and non-string models or efforts do not count", () => {
	for (const model of ["inherit", "INHERIT", "gpt-4", "claude", 5, null]) {
		assert.equal(decide("Agent", { model, effort: "high" }).action, "deny", String(model));
		assert.equal(decide(START, { initiation: "user_asked", model, effort: "high" }).action, "deny", String(model));
	}
	for (const effort of ["inherit", "adaptive", "ultra", "", 3, null]) {
		assert.equal(decide("Agent", { model: "opus", effort }).action, "deny", String(effort));
		assert.equal(decide(START, { initiation: "user_asked", model: "claude-opus-5-5", effort }).action, "deny", String(effort));
	}
	for (const model of ["sonnet", "Opus", "haiku", "claude-opus-5-5", "claude-sonnet-5-5[1m]"]) {
		assert.equal(decide("Agent", { model, effort: "medium" }).action, "allow", model);
	}
	for (const effort of ["low", "medium", "high", "xhigh", "max", "High"]) {
		assert.equal(decide("Agent", { model: "opus", effort }).action, "allow", effort);
	}
});

test("start_session user_asked / own_initiative need a model and an effort", () => {
	for (const initiation of ["user_asked", "own_initiative"]) {
		assert.equal(decide(START, { initiation, prompt: "x" }).action, "deny");
		assert.equal(decide(START, { initiation, prompt: "x", model: "claude-sonnet-5-5" }).action, "deny");
		assert.equal(decide(START, { initiation, prompt: "x", model: "claude-sonnet-5-5", effort: "high" }).action, "allow");
	}
});

test("start_session offer is allowed with a reminder, even with a model", () => {
	assert.equal(decide(START, { initiation: "offer", title: "x" }).action, "remind");
	assert.equal(decide(START, { initiation: "offer", model: "opus", effort: "high" }).action, "remind");
});

test("start_session with a proposal_id is allowed, whatever else it carries", () => {
	assert.equal(decide(START, { proposal_id: "p1" }).action, "allow");
	assert.equal(decide(START, { proposal_id: "p1", initiation: "own_initiative" }).action, "allow");
});

test("anything else is never blocked", () => {
	assert.equal(decide("Bash", { command: "ls" }).action, "allow");
	assert.equal(decide("mcp__ccd_session__hand_off_to_session", {}).action, "allow");
	assert.equal(decide(START, undefined).action, "allow");
	assert.equal(decide(undefined, null).action, "allow");
});

test("output shapes match the Claude Code PreToolUse format", () => {
	const deny = output(decide("Agent", {})).hookSpecificOutput;
	assert.equal(deny.hookEventName, "PreToolUse");
	assert.equal(deny.permissionDecision, "deny");
	assert.match(deny.permissionDecisionReason, /model/);
	assert.match(deny.permissionDecisionReason, /effort/);
	const remind = output(decide(START, { initiation: "offer" })).hookSpecificOutput;
	assert.equal(remind.permissionDecision, undefined);
	assert.match(remind.additionalContext, /CLAUDE\.md "Model and effort"/);
	assert.match(remind.additionalContext, /effort/);
	assert.equal(output(decide("Bash", {})), null);
});

function runHook(stdin) {
	return spawnSync("node", [path.join(__dirname, "require-model.js")], { input: stdin, encoding: "utf8" });
}

test("script: denies through stdin, exits 0", () => {
	const run = runHook(JSON.stringify({ tool_name: "Agent", tool_input: { prompt: "x" } }));
	assert.equal(run.status, 0);
	assert.equal(JSON.parse(run.stdout).hookSpecificOutput.permissionDecision, "deny");
});

test("script: allowed call prints nothing", () => {
	const run = runHook(JSON.stringify({ tool_name: "Agent", tool_input: { model: "opus", effort: "medium" } }));
	assert.equal(run.status, 0);
	assert.equal(run.stdout, "");
});

test("script: bad input warns loudly (exit 1 = call goes ahead, notice shown)", () => {
	const run = runHook("not json");
	assert.equal(run.status, 1);
	assert.equal(run.stdout, "");
	assert.match(run.stderr, /let through/);
});

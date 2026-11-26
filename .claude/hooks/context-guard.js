// Claude Code PreToolUse hook (.claude/settings.json, every tool): keeps a subagent's history from
// ballooning. Every call re-reads the whole history, so cost grows with its length (measured in the
// user's Roblox project: one builder grew to 540k and re-read ~100M tokens in 70 min). Main sessions
// are left alone (they get session-size.js's warning); only calls carrying an agent_id are checked.
//   history >= SOFT (250k) -> allowed, plus a note: finish the piece if nearly done, commit, hand over
//   history >= HARD (500k) -> denied, except one plain git command and writing the hand-over note
// History size = the last assistant message's input + cache tokens in the subagent's transcript
// (<session>/subagents/agent-<id>.jsonl beside the session's transcript_path), read from a growing
// tail (single lines can pass 512 KB). No size found, or an error: a warning on stderr and exit 1,
// so Claude Code lets the call through and shows a hook-error notice (never a silent pass).
"use strict";

const fs = require("fs");
const path = require("path");

const SOFT = Number(process.env.CONTEXT_GUARD_SOFT) || 250000;
const HARD = Number(process.env.CONTEXT_GUARD_HARD) || 500000;
const TAILS = [512 * 1024, 8 * 1024 * 1024, Infinity];

const HANDOVER =
	"write a short hand-over note (what is done and verified, what is left, files touched, commits, the brief points and rules that still apply, and the exact commands, ids and setup to build and test — your successor reads only this note and the files it names) into a path containing `side-session-notes` or `scratchpad` (the only places you can still write past the limit), commit your finished pieces, then end with your report naming the note's path, so your starter continues with a fresh agent.";

function transcriptOf(event) {
	if (typeof event.agent_id !== "string" || event.agent_id === "") return null;
	if (typeof event.transcript_path !== "string") return null;
	const dir = event.transcript_path.replace(/\.jsonl$/, "");
	return path.join(dir, "subagents", `agent-${event.agent_id}.jsonl`);
}

// The context size of the newest assistant message in a JSONL transcript's tail, or null.
function historySize(text) {
	const lines = text.split("\n");
	for (let i = lines.length - 1; i >= 0; i--) {
		if (!lines[i].includes('"usage"')) continue;
		let entry;
		try {
			entry = JSON.parse(lines[i]);
		} catch {
			continue; // the tail's first line is usually cut
		}
		const usage = entry && entry.message && entry.message.usage;
		if (!usage) continue;
		return (usage.input_tokens || 0) + (usage.cache_read_input_tokens || 0) + (usage.cache_creation_input_tokens || 0);
	}
	return null;
}

function readTail(file, bytes) {
	const fd = fs.openSync(file, "r");
	try {
		const size = fs.fstatSync(fd).size;
		const length = Math.min(size, bytes);
		const buf = Buffer.alloc(length);
		fs.readSync(fd, buf, 0, length, size - length);
		return buf.toString("utf8");
	} finally {
		fs.closeSync(fd);
	}
}

// Calls still allowed past HARD: committing and the hand-over note.
function allowedPastHard(toolName, input) {
	const args = input !== null && typeof input === "object" ? input : {};
	if (toolName === "Bash" || toolName === "PowerShell") {
		// One plain git command, optionally after a cd; nothing chained after it.
		const command = String(args.command || "").trim().replace(/^cd ("[^"]*"|[^\s&;|]+)\s*&&\s*/, "");
		if (/[;&|\n`]|\$\(/.test(command)) return false;
		return /^git\s/.test(command);
	}
	if (toolName === "Write" || toolName === "Edit") {
		return /side-session-notes|scratchpad/i.test(String(args.file_path || ""));
	}
	return false;
}

// -> { action: "allow" } | { action: "remind", text } | { action: "deny", text }
function decide(size, toolName, input) {
	if (size === null || size < SOFT) return { action: "allow" };
	const k = Math.round(size / 1000);
	if (size >= HARD) {
		if (allowedPastHard(toolName, input)) return { action: "allow" };
		return { action: "deny", text: `Context guard: your history is ${k}k tokens (limit ${HARD / 1000}k), so further work costs too much. Only plain git commands and the hand-over note are allowed now: ${HANDOVER}` };
	}
	return { action: "remind", text: `Context guard: your history is ${k}k tokens (soft limit ${SOFT / 1000}k; work stops at ${HARD / 1000}k). If the piece you are on is nearly done, finish it; either way, ${HANDOVER}` };
}

function output(decision) {
	if (decision.action === "deny") {
		return { hookSpecificOutput: { hookEventName: "PreToolUse", permissionDecision: "deny", permissionDecisionReason: decision.text } };
	}
	if (decision.action === "remind") {
		return { hookSpecificOutput: { hookEventName: "PreToolUse", additionalContext: decision.text } };
	}
	return null;
}

function main() {
	try {
		const event = JSON.parse(fs.readFileSync(0, "utf8"));
		const file = transcriptOf(event);
		if (!file || !fs.existsSync(file)) return;
		let size = null;
		for (const bytes of TAILS) {
			size = historySize(readTail(file, bytes));
			if (size !== null || bytes >= fs.statSync(file).size) break;
		}
		if (size === null) {
			// A brand-new subagent has no assistant message yet; anything else is a real miss.
			if (fs.statSync(file).size > 64 * 1024) {
				console.error(`context-guard hook: no history size found in ${file}, so this call was let through`);
				process.exitCode = 1;
			}
			return;
		}
		const result = output(decide(size, event.tool_name, event.tool_input));
		if (result) console.log(JSON.stringify(result));
	} catch (err) {
		console.error(`context-guard hook: could not check this call, so it was let through (${err.message})`);
		process.exitCode = 1;
	}
}

if (require.main === module) main();

module.exports = { decide, output, historySize, transcriptOf, allowedPastHard, SOFT, HARD };

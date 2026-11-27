// Claude Code UserPromptSubmit hook (.claude/settings.json): warns when the main session's history is
// large, since every new message re-reads all of it. Taken from the Roblox project's hub hook (its
// size check only). Subagents are capped separately by context-guard.js.
//   history <= LIMIT (300k; a fresh session reportedly starts near 100k; override with SESSION_SIZE_LIMIT) -> silent
//   history >  LIMIT        -> one line: suggest a fresh session for new work
// History size = the newest assistant message's input + cache tokens in the transcript (path from the
// hook's stdin), read from a growing tail. Never blocks a prompt: an error is one printed line, exit 0.
"use strict";

const fs = require("fs");

const LIMIT = Number(process.env.SESSION_SIZE_LIMIT) > 0 ? Number(process.env.SESSION_SIZE_LIMIT) : 300000;
const TAILS = [512 * 1024, 8 * 1024 * 1024, Infinity];

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

// -> the note to print, or null
function noteFor(size) {
	if (size === null || size <= LIMIT) return null;
	return (
		`Session size: ~${Math.round(size / 1000)}k tokens (over ~${Math.round(LIMIT / 1000)}k). Each new message re-reads all of it. ` +
		"A guideline, not a stop: finish the current task here. If you haven't said so yet this session, tell the " +
		"user in one line that new work is cheaper in a fresh session (after writing what it needs into the findings " +
		"notes or a hand-over note). " +
		'Detach side sessions you start (CLAUDE.md "Side sessions").'
	);
}

function main() {
	try {
		const input = JSON.parse(fs.readFileSync(0, "utf8"));
		const file = typeof input.transcript_path === "string" ? input.transcript_path : "";
		if (file === "" || !fs.existsSync(file)) return;
		let size = null;
		for (const bytes of TAILS) {
			size = historySize(readTail(file, bytes));
			if (size !== null || bytes >= fs.statSync(file).size) break;
		}
		const note = noteFor(size);
		if (note) console.log(note);
	} catch (err) {
		console.log(`Session size check failed: ${err.message}`);
	}
}

if (require.main === module) main();

module.exports = { historySize, noteFor, LIMIT };

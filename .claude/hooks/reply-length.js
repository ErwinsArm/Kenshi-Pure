// Claude Code UserPromptSubmit hook (.claude/settings.json): when Claude's last reply was long, it
// tells Claude so at the start of the next turn, so the reply-length rule in CLAUDE.md does not
// rest on memory alone. Costs no model call and never blocks or rewrites anything: a short note is
// the only output, and an error is a warning on stderr with exit 0.
//   last reply <= LIMIT words -> silent
//   last reply >  LIMIT words -> one line with the word count and the rule
// The last reply is the newest assistant message in the session's transcript that has text; the
// transcript path comes from the hook's stdin. A long reply that was needed (a failure, a risk, a
// decision, or detail the user asked for) is fine: the note says so.
"use strict";

const fs = require("fs");

const LIMIT = Number(process.env.REPLY_LENGTH_LIMIT) > 0 ? Number(process.env.REPLY_LENGTH_LIMIT) : 140;
const TAILS = [256 * 1024, 4 * 1024 * 1024, Infinity];

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

// The words in the text blocks of the newest assistant message that has any, or null.
function lastReplyWords(text) {
	const lines = text.split("\n");
	for (let i = lines.length - 1; i >= 0; i--) {
		if (!lines[i].includes('"assistant"')) continue;
		let entry;
		try {
			entry = JSON.parse(lines[i]);
		} catch {
			continue; // the tail's first line is usually cut
		}
		if (!entry || entry.type !== "assistant" || !entry.message || !Array.isArray(entry.message.content)) continue;
		const reply = entry.message.content
			.filter((block) => block && block.type === "text" && typeof block.text === "string")
			.map((block) => block.text)
			.join(" ")
			.trim();
		if (reply !== "") return reply.split(/\s+/).length;
	}
	return null;
}

// -> the note to print, or null
function noteFor(words) {
	if (words === null || words <= LIMIT) return null;
	return `Reply length: your last reply was ${words} words; the CLAUDE.md rule is about 80 (one-line bullets). Longer is fine only for a failure, a risk or a decision that changes what the user does, or detail they asked for. Otherwise cut.`;
}

function main() {
	try {
		const input = JSON.parse(fs.readFileSync(0, "utf8"));
		const file = typeof input.transcript_path === "string" ? input.transcript_path : "";
		if (file === "" || !fs.existsSync(file)) return;
		let words = null;
		for (const bytes of TAILS) {
			words = lastReplyWords(readTail(file, bytes));
			if (words !== null || bytes >= fs.statSync(file).size) break;
		}
		const note = noteFor(words);
		if (note) console.log(note);
	} catch (err) {
		console.error(`reply-length hook: could not check the last reply (${err.message})`);
	}
}

if (require.main === module) main();

module.exports = { lastReplyWords, noteFor, LIMIT };

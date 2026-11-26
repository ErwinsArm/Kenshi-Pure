// Claude Code PreToolUse hook (.claude/settings.json): makes every session and subagent Claude
// starts name its model and effort, so the per-task choice (CLAUDE.md "Model and effort") does not
// rest on memory. Without an effort, a subagent inherits its starter's (an xhigh session spawns xhigh).
//   Agent, or start_session "user_asked" / "own_initiative", without a usable model and effort -> denied with a reason
//   start_session "offer" (the user picks model and effort on the card)                       -> allowed, plus a reminder
//   start_session with a proposal_id, anything else                                           -> allowed
// It checks that a real model and effort are named ("inherit", blank and unknown values are not),
// not that they suit the task: that stays Claude's judgement. Never blocks anything else. An
// error prints a warning and exits 1: Claude Code lets the call through and shows a hook-error
// notice (exit 0 would hide the warning).
"use strict";

const RULE = 'Pick both by CLAUDE.md "Model and effort" (already in your context).';
const AGENT_REASON = `Set \`model\` ("opus", "sonnet" or "haiku"; "inherit" does not count) and \`effort\` (low, medium, high, xhigh or max) on this call. ${RULE}`;
const SESSION_REASON = `Set \`model\` (an id such as claude-opus-5-5) and \`effort\` on this call; a session can't start a stronger model or a higher effort than its own. ${RULE}`;
const OFFER_REMINDER = `Project rule: ${RULE} On an offer card the user picks the model and effort, so tell them in your next message which to pick.`;

// An alias (sonnet, opus, haiku, fable) or a claude-… id, optionally with a [1m]-style suffix.
const MODEL_NAME = /^(sonnet|opus|haiku|fable|claude-[a-z0-9.-]+)(\[[a-z0-9]+\])?$/i;
const NAMED_MODEL = (input) => typeof input.model === "string" && MODEL_NAME.test(input.model.trim());
const EFFORTS = ["low", "medium", "high", "xhigh", "max"];
const NAMED_EFFORT = (input) => typeof input.effort === "string" && EFFORTS.includes(input.effort.trim().toLowerCase());
const NAMED_BOTH = (input) => NAMED_MODEL(input) && NAMED_EFFORT(input);

// -> { action: "deny", text } | { action: "remind", text } | { action: "allow" }
function decide(toolName, input) {
	const args = input !== null && typeof input === "object" ? input : {};
	if (toolName === "Agent") {
		return NAMED_BOTH(args) ? { action: "allow" } : { action: "deny", text: AGENT_REASON };
	}
	if (toolName === "mcp__ccd_session__start_session") {
		if (typeof args.proposal_id === "string" && args.proposal_id !== "") return { action: "allow" };
		if (args.initiation === "offer") return { action: "remind", text: OFFER_REMINDER };
		if (args.initiation === "user_asked" || args.initiation === "own_initiative") {
			return NAMED_BOTH(args) ? { action: "allow" } : { action: "deny", text: SESSION_REASON };
		}
	}
	return { action: "allow" };
}

function output(decision) {
	if (decision.action === "deny") {
		return { hookSpecificOutput: { hookEventName: "PreToolUse", permissionDecision: "deny", permissionDecisionReason: decision.text } };
	}
	if (decision.action === "remind") {
		// No permissionDecision: the normal permission flow continues.
		return { hookSpecificOutput: { hookEventName: "PreToolUse", additionalContext: decision.text } };
	}
	return null;
}

function main() {
	try {
		const event = JSON.parse(require("fs").readFileSync(0, "utf8"));
		const result = output(decide(event.tool_name, event.tool_input));
		if (result) console.log(JSON.stringify(result));
	} catch (err) {
		console.error(`require-model hook: could not check this call, so it was let through (${err.message})`);
		process.exitCode = 1;
	}
}

if (require.main === module) main();

module.exports = { decide, output };

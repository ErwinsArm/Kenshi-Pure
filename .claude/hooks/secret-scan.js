// Secret scan: stops a commit or push that would put secrets or private data into this public repo.
// One scanner, several entry points:
//   node secret-scan.js claude-hook   Claude Code PreToolUse hook on Bash / PowerShell (.claude/settings.json).
//                                     Finds `git commit` / `git push` in the command (also inside bash -c,
//                                     powershell -Command, cmd /c, eval, env …, git aliases) and scans what they
//                                     would publish. A command it cannot parse but that mentions git and
//                                     commit/push gets the whole pending state scanned (all changes, untracked
//                                     files, unpushed commits). Best effort: a script file that runs git is not seen.
//   node secret-scan.js pre-commit    git hooks in .githooks/ (per clone: git config core.hooksPath .githooks,
//   node secret-scan.js commit-msg F  docs/setup.md). Exact, since git hands them the real index, message and
//   node secret-scan.js pre-push      pushed commits; they also catch commits made outside Claude.
//   node secret-scan.js history       scans every commit on every ref; exit 1 on any finding.
// What it looks for: API keys and tokens, private key blocks, `password=`-style literals (8+ characters),
// real e-mail addresses (author and committer too), user-folder paths with a real name, this computer's user
// name, Windows executables, and files that must never be committed (the AnarchyKenshi mod files, its logs
// and login file, crash dumps, key files, archives). Only added lines are checked; files git calls binary are
// read whole (UTF-16 decoded). Merge commits' own diffs are not scanned. Findings are printed redacted.
// A false positive is fixed by changing a rule here (with a test), never by working around the check.
// Errors: the Claude hook denies the commit/push (fail closed) and says why; git hooks exit 1. If Node itself
// cannot start, Claude Code shows a hook error and lets the call through (like the other hooks here).
"use strict";

const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

// ---------------------------------------------------------------- rules

// Content rules: regex (global) and, optionally, a check on the match that returns false for allowed values.
const CONTENT_RULES = [
	{ id: "Anthropic API key", re: /sk-ant-[A-Za-z0-9_-]{20,}/g },
	{ id: "OpenAI-style API key", re: /\bsk-(?:proj-)?[A-Za-z0-9_-]{32,}/g },
	{ id: "GitHub token", re: /\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})/g },
	{ id: "npm token", re: /\bnpm_[A-Za-z0-9]{36}\b/g },
	{ id: "AWS access key id", re: /\b(?:AKIA|ASIA)[0-9A-Z]{16}\b/g },
	{ id: "AWS secret key", re: /aws_?secret_?access_?key\s*["']?\s*[:=]\s*["']?[A-Za-z0-9/+=]{40}/gi },
	{ id: "Azure account key", re: /AccountKey=[A-Za-z0-9+\/]{40,}={0,2}/g },
	{ id: "Google API key", re: /\bAIza[0-9A-Za-z_-]{35}/g },
	{ id: "Google OAuth token or secret", re: /\b(?:ya29\.[0-9A-Za-z_-]{20,}|GOCSPX-[A-Za-z0-9_-]{20,})/g },
	{ id: "Slack token", re: /\bxox[abposre]-[A-Za-z0-9-]{10,}/g },
	{ id: "Slack webhook", re: /hooks\.slack\.com\/services\/T[A-Za-z0-9]+\/B[A-Za-z0-9]+\/[A-Za-z0-9]+/g },
	{ id: "Discord webhook", re: /discord(?:app)?\.com\/api\/webhooks\/\d+\/[A-Za-z0-9_-]+/g },
	{ id: "Stripe key", re: /\b[rs]k_live_[A-Za-z0-9]{16,}/g },
	{ id: "JSON web token", re: /\beyJ[A-Za-z0-9_-]{10,}\.eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}/g },
	{ id: "bearer token", re: /\bBearer\s+([A-Za-z0-9._~+\/-]{20,}=*)/g, check: (m) => !isPlaceholder(m[1]) },
	{ id: "private key block", re: /-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY(?: BLOCK)?-----/g },
	{ id: "password in URL", re: /\b[a-z][a-z0-9+.-]*:\/\/[^\s\/:@"'<>]+:([^\s\/@"'<>]+)@[A-Za-z0-9.-]+/gi, check: (m) => !isPlaceholder(m[1]) },
	{
		id: "secret assignment",
		// Any name containing password/secret/token/… (SECRET_KEY, secretKey, password_hash, pwd), then = : or :=.
		re: /\b((?:[A-Za-z0-9_.-]*(?:password|passwd|passphrase|secret|token|api[_-]?key|apikey|access[_-]?key|private[_-]?key|account[_-]?key|credential)[A-Za-z0-9_-]*)|pwd)["']?\s*(?::=|=|:)\s*(["'`]?)([^\s"'`,;]+)\2/gi,
		check: (m) => looksLikeSecretValue(m[3], m[2] !== ""),
	},
	{ id: "e-mail address", re: /(?<![A-Za-z0-9._%+-])[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}(?![A-Za-z0-9-])/g, check: (m) => !allowedEmail(m[0]) },
	{
		id: "user folder path",
		// C:\Users\name, c:/users/name, C:\\Users\\name (JSON), /c/Users/name (Git Bash), /mnt/c/Users/name,
		// /home/name (Linux), /Users/name (macOS).
		re: /(?:\b[A-Za-z]:|(?<![A-Za-z0-9])\/(?:mnt\/)?[a-z])(?:\\{1,2}|\/)Users(?:\\{1,2}|\/)([^\\\/\s"'<>|*?:;,)\]}]+)|(?<![A-Za-z0-9~.])\/home\/([^\/\s"'<>|*?:;,)\]}]+)|(?<![A-Za-z0-9~.:\/\\])\/Users\/([^\/\s"'<>|*?:;,)\]}]+)/gi,
		check: (m) => !isPlaceholderUser(m[1] ?? m[2] ?? m[3]),
	},
];

// File names that must never be committed (checked on added, copied, modified and renamed files).
const PATH_RULES = [
	{ id: "AnarchyKenshi mod folder", re: /(^|\/)KenshiMultiplayer[^\/]*\//i },
	{ id: "AnarchyKenshi binary", re: /(^|\/)Anarchy[^\/]*\.(dll|exe|pdb)$/i },
	{ id: "AnarchyKenshi log", re: /(^|\/)AnarchyKenshi[^\/]*\.log$/i },
	{ id: "AnarchyKenshi login file", re: /(^|\/)AnarchyKenshi-logins[^\/]*$/i },
	{ id: "crash dump", re: /\.(dmp|mdmp|hdmp)$/i },
	{ id: "key or certificate file", re: /\.(pem|key|pfx|p12|keystore|jks)$|(^|\/)id_(rsa|dsa|ecdsa|ed25519)$/i },
	{ id: "environment file", re: /(^|\/)\.env(\.(?!example$|sample$|template$)[^\/]+)?$/i },
	{ id: "per-machine config", re: /(^|\/)(\.mcp\.json|settings\.local\.json)$|^tools\//i },
	{ id: "archive (could hold the mod or dumps)", re: /\.(zip|7z|rar|tar|gz|tgz|xz|bz2|cab)$/i },
];

const PLACEHOLDER_USERS = new Set([
	"x", "xx", "xxx", "user", "username", "user-name", "user_name", "name", "yourname", "your-name", "your_name", "you",
	"me", "someone", "example", "foo", "bar", "test", "public", "default", "default user", "all users", "defaultuser0",
	"runneradmin", "runner", "shared", "...", "…", "*", "**",
]);

function isPlaceholder(value) {
	const v = String(value).trim();
	if (v === "") return true;
	if (/^[<{\[$%(*]|^\.\.\.|^…/.test(v)) return true; // <name>, ${VAR}, $env:X, %VAR%, (…), ***
	if (/^(x+|\*+|\.+|-+|_+|#+|0+)$/i.test(v)) return true;
	if (/^(your|my|some|example|dummy|fake|test|sample|placeholder|changeme|redacted|secret|password|token|xxx)([-_]|$)/i.test(v)) return true;
	return false;
}

function isPlaceholderUser(name) {
	const n = String(name).trim().replace(/[.]+$/, "");
	if (isPlaceholder(n)) return true;
	return PLACEHOLDER_USERS.has(n.toLowerCase());
}

const ALLOWED_EMAIL_DOMAINS = /(^|\.)(users\.noreply\.github\.com|example\.(com|org|net)|example|invalid|test|localhost)$/i;
const ALLOWED_EMAILS = new Set(["noreply@anthropic.com", "noreply@github.com", "git@github.com", "git@gitlab.com", "git@bitbucket.org"]);
// Shaped like an address but a file name (logo@2x.png). Not .md: that is also a country domain.
const FILE_TLDS = /\.(png|jpe?g|gif|svg|webp|ico|bmp|tga|dds|js|mjs|ts|css|html?|json|txt|dll|exe|lib|pdb|log|cfg|ini)$/i;

function allowedEmail(address) {
	const a = address.toLowerCase();
	if (ALLOWED_EMAILS.has(a)) return true;
	const domain = a.slice(a.lastIndexOf("@") + 1);
	if (ALLOWED_EMAIL_DOMAINS.test(domain)) return true;
	if (FILE_TLDS.test(domain)) return true;
	return isPlaceholder(a.slice(0, a.lastIndexOf("@")));
}

// Decompiler names and constants that notes about the mod's login code assign to "token"/"password" fields.
const RE_NOTATION = /^(?:0x[0-9a-f]+|[0-9a-f]{40}|[0-9a-f]{64}|\d+|(?:FUN|DAT|LAB|PTR|SUB|UNK|thunk_FUN|switchD|caseD)_[0-9a-f_]+|(?:param|local|stack|in|unaff|extraout|auStack|uStack|iStack|lStack)_\w+|[a-z]{1,6}Var\d+)$/i;

// A value assigned to password/token/… counts as a secret when it is a literal of 8+ characters, not code,
// a placeholder or reverse-engineering notation. Quoted values are trusted more than bare ones.
function looksLikeSecretValue(value, quoted) {
	const v = value.replace(/[)\]}>.]+$/, "");
	if (v.length < 8 || isPlaceholder(v)) return false;
	if (RE_NOTATION.test(v)) return false;
	if (/[()\[\]{}<>\\…]|::|\$\{|\$\(/.test(v)) return false; // code, templates, prose
	if (!quoted && /[$%]/.test(v)) return false; // bare $VAR / %VAR% expansions
	if (/^[\/.]|[^=]=[^=]/.test(v)) return false; // a path, or prose like "password=/token="
	if (/^(true|false|null|none|undefined|required|optional|string|number|boolean|bool|integer|str)$/i.test(v)) return false;
	if (/^https?:/i.test(v)) return false;
	if (/^[a-z]+([_-][a-z]+)+$/i.test(v)) return false; // refresh_token, api-key-name: a name, not a secret
	if (/^[a-z]+$/.test(v)) return quoted && v.length >= 12; // "correcthorsebattery" yes, bare words no
	if (!quoted) {
		if (/^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)+$/.test(v)) return false; // obj.field
		if (/^[A-Za-z]+$/.test(v)) return false; // TypeName, CONSTANT
	}
	return true;
}

// This computer's user name (and home folder name) as a word anywhere in added text. Read live, so it
// works on every clone. Skipped: the repo owner's public handle, names under 4 characters, and names the
// committed tree already uses as a word (an ordinary word such as "max" or "kenshi", not a leak).
function machineNames(publicNames, cwd) {
	const names = new Set();
	const add = (n) => {
		if (typeof n !== "string") return;
		const v = n.trim();
		if (v.length < 4 || isPlaceholderUser(v) || !/^[\w.-]+$/.test(v)) return;
		if (publicNames.some((p) => p.toLowerCase() === v.toLowerCase())) return;
		if (cwd && gitOk(cwd, ["grep", "-q", "-i", "-w", "-F", "-e", v, "HEAD", "--"])) return;
		names.add(v);
	};
	try { add(os.userInfo().username); } catch {}
	add(path.basename(os.homedir()));
	add(process.env.USERNAME);
	add(process.env.USER);
	return [...names];
}

function escapeRe(s) {
	return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function redact(text) {
	const t = String(text);
	if (t.length <= 6) return `${t.slice(0, 1)}…`;
	return `${t.slice(0, 4)}…(${t.length} chars)`;
}

// ---------------------------------------------------------------- scanning

// -> [{ file, line, rule, sample }]
function scanText(text, file, options = {}) {
	const findings = [];
	const lines = String(text).split(/\r?\n/);
	const names = options.machineNames || [];
	const nameRes = names.map((n) => ({ n, re: new RegExp(`(?<![A-Za-z0-9])${escapeRe(n)}(?![A-Za-z0-9])`, "i") }));
	lines.forEach((lineText, i) => {
		const lineNo = options.lineNumbers ? options.lineNumbers[i] : i + 1;
		for (const rule of CONTENT_RULES) {
			rule.re.lastIndex = 0;
			let m;
			while ((m = rule.re.exec(lineText)) !== null) {
				if (m[0] === "") { rule.re.lastIndex++; continue; }
				if (rule.check && !rule.check(m)) continue;
				findings.push({ file, line: lineNo, rule: rule.id, sample: redact(m[0]) });
			}
		}
		for (const { n, re } of nameRes) {
			if (re.test(lineText)) findings.push({ file, line: lineNo, rule: "this computer's user name", sample: redact(n) });
		}
	});
	return findings;
}

function scanPaths(paths) {
	const findings = [];
	for (const p of paths) {
		const norm = p.replace(/\\/g, "/");
		for (const rule of PATH_RULES) {
			if (rule.re.test(norm)) findings.push({ file: norm, line: 0, rule: rule.id, sample: "" });
		}
	}
	return findings;
}

// Text of a file git calls binary: UTF-16 (BOM, or every other byte NUL) is decoded; real binary -> null.
function decodeBuffer(buf) {
	if (buf.length >= 2 && buf[0] === 0xff && buf[1] === 0xfe) return buf.subarray(2).toString("utf16le");
	if (buf.length >= 2 && buf[0] === 0xfe && buf[1] === 0xff) return Buffer.from(buf.subarray(2)).swap16().toString("utf16le");
	if (!buf.includes(0)) return buf.toString("utf8");
	const n = Math.min(buf.length & ~1, 4096);
	let oddZero = 0, evenZero = 0;
	for (let k = 0; k < n; k += 2) { if (buf[k] === 0) evenZero++; if (buf[k + 1] === 0) oddZero++; }
	const pairs = n / 2;
	if (pairs && oddZero > 0.3 * pairs && evenZero < 0.05 * pairs) return buf.subarray(0, buf.length & ~1).toString("utf16le");
	return null;
}

function scanBlob(buf, file, options) {
	const findings = [];
	if (buf.length >= 2 && buf[0] === 0x4d && buf[1] === 0x5a) findings.push({ file, line: 0, rule: "Windows executable (MZ header)", sample: "" });
	const text = decodeBuffer(buf);
	if (text !== null) findings.push(...scanText(text, file, options));
	return findings;
}

// Scans the added lines of a unified diff (git diff / git log -p output). Hunk line counts decide what is
// content, so an added line that itself starts with "++ " is not mistaken for a file header.
function scanDiff(diffText, options = {}) {
	const findings = [];
	let file = "?";
	let newLine = 0;
	let oldLeft = 0;
	let newLeft = 0;
	let added = [];
	let addedNos = [];
	const flush = () => {
		if (added.length) findings.push(...scanText(added.join("\n"), file, { ...options, lineNumbers: addedNos }));
		added = [];
		addedNos = [];
	};
	for (const raw of String(diffText).split("\n")) {
		const line = raw.replace(/\r$/, "");
		if (oldLeft > 0 || newLeft > 0) {
			if (line.startsWith("+")) { added.push(line.slice(1)); addedNos.push(newLine); newLine++; newLeft--; continue; }
			if (line.startsWith("-")) { oldLeft--; continue; }
			if (line.startsWith(" ") || line === "") { newLine++; oldLeft--; newLeft--; continue; }
			if (line.startsWith("\\")) continue; // "\ No newline at end of file"
			oldLeft = newLeft = 0; // malformed: fall through to header parsing
		}
		if (line.startsWith("diff --git ")) { flush(); file = "?"; continue; }
		if (line.startsWith("+++ ")) {
			flush();
			const p = line.slice(4);
			if (p !== "/dev/null") file = unquoteGitPath(p).replace(/^b\//, "");
			continue;
		}
		if (line.startsWith("--- ")) {
			const p = line.slice(4);
			if (p !== "/dev/null") file = unquoteGitPath(p).replace(/^a\//, "");
			continue;
		}
		const hunk = /^@@ -\d+(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/.exec(line);
		if (hunk) {
			oldLeft = hunk[1] === undefined ? 1 : Number(hunk[1]);
			newLine = Number(hunk[2]);
			newLeft = hunk[3] === undefined ? 1 : Number(hunk[3]);
			continue;
		}
		if (line.startsWith("+") && file === "?") { added.push(line.slice(1)); addedNos.push(newLine++); } // bare diff without header
	}
	flush();
	return findings;
}

function unquoteGitPath(p) {
	if (!p.startsWith('"')) return p;
	try { return JSON.parse(p); } catch { return p.slice(1, -1); }
}

// ---------------------------------------------------------------- git

// --no-optional-locks: never take .git/index.lock, so a scan cannot make another session's git add fail.
function gitRaw(cwd, args) {
	const run = spawnSync("git", ["--no-optional-locks", "-c", "core.quotepath=off", ...args], { cwd, maxBuffer: 1 << 30, windowsHide: true });
	if (run.error) throw new Error(`git could not run: ${run.error.message}`);
	return run;
}

function git(cwd, args, { allowFail = false } = {}) {
	const run = gitRaw(cwd, args);
	if (run.status !== 0) {
		if (allowFail) return null;
		throw new Error(`git ${args.join(" ")} failed: ${String(run.stderr || "").trim().split("\n")[0]}`);
	}
	return run.stdout.toString("utf8");
}

function gitOk(cwd, args) {
	try { return gitRaw(cwd, args).status === 0; } catch { return false; }
}

const EMPTY_TREE = "4b825dc642cb6eb9a060e54bf8d69288fbee4904";
const LOG_OPTS = ["--no-color", "--no-ext-diff", "--no-textconv", "-U0"]; // no --diff-filter: it would hide empty commits
const DIFF_OPTS = [...LOG_OPTS, "--diff-filter=ACMR"];

function diffBase(cwd) {
	return gitOk(cwd, ["rev-parse", "--verify", "-q", "HEAD"]) ? "HEAD" : EMPTY_TREE;
}

function topLevel(cwd) {
	return git(cwd, ["rev-parse", "--show-toplevel"]).trim();
}

function publicNames(cwd) {
	const names = [];
	const url = git(cwd, ["remote", "get-url", "origin"], { allowFail: true }) || "";
	const owner = /[:\/]([^\/:]+)\/[^\/]+?(?:\.git)?\s*$/.exec(url.trim());
	if (owner) names.push(owner[1]);
	return names;
}

function contextFor(cwd) {
	return { machineNames: machineNames(publicNames(cwd), cwd) };
}

// `--numstat -z` output -> [{ path, binary }] (renames give the new path).
function parseNumstat(out) {
	const parts = out.split("\0");
	const files = [];
	for (let k = 0; k < parts.length; k++) {
		const m = /^(-|\d+)\t(-|\d+)\t(.*)$/s.exec(parts[k].replace(/^\n+/, ""));
		if (!m) continue;
		let p = m[3];
		if (p === "") { p = parts[k + 2]; k += 2; } // rename: "a\td\t\0old\0new"
		if (p) files.push({ path: p, binary: m[1] === "-" });
	}
	return files;
}

// Scans the diff `git diff <args>` would show: file names, added lines, and binary-classified files whole.
// readBlob(path) returns the new content of a binary-classified file.
function scanGitDiff(cwd, args, options) {
	const diff = git(cwd, ["diff", ...DIFF_OPTS, ...args]);
	const files = parseNumstat(git(cwd, ["diff", "--numstat", "-z", "--diff-filter=ACMR", ...args]));
	const findings = [...scanPaths(files.map((f) => f.path)), ...scanDiff(diff, options)];
	const top = files.some((f) => f.binary) ? topLevel(cwd) : null;
	for (const f of files.filter((x) => x.binary)) {
		let buf = null;
		if (args.includes("--cached")) {
			const run = gitRaw(top, ["cat-file", "blob", `:${f.path}`]);
			if (run.status === 0) buf = run.stdout;
		} else {
			try { buf = fs.readFileSync(path.join(top, f.path)); } catch {}
		}
		if (buf) findings.push(...scanBlob(buf, f.path, options));
	}
	return findings;
}

// Untracked files among the pathspecs (from `git add` earlier in the same command line, or a full scan).
function scanUntracked(cwd, pathspecs, options) {
	if (!pathspecs.length) return [];
	const out = git(cwd, ["ls-files", "--others", "--exclude-standard", "-z", "--", ...pathspecs], { allowFail: true }) || "";
	const files = out.split("\0").filter(Boolean);
	const findings = [...scanPaths(files)];
	for (const f of files) {
		let buf;
		try { buf = fs.readFileSync(path.join(cwd, f)); } catch { continue; }
		findings.push(...scanBlob(buf, f, options));
	}
	return findings;
}

// Outgoing commits: author/committer and message, added lines, file names (also of empty files and pure
// renames), and binary-classified files whole.
function scanCommits(cwd, revArgs, options) {
	const shas = git(cwd, ["rev-list", ...revArgs]).split(/\s+/).filter(Boolean);
	const findings = [];
	for (const sha of shas) {
		const short = sha.slice(0, 10);
		const meta = git(cwd, ["show", "-s", "--format=%an <%ae>%n%cn <%ce>%n%B", sha]);
		findings.push(...scanText(meta, `commit ${short} (author/message)`, options));
		const files = parseNumstat(git(cwd, ["diff-tree", "-r", "--root", "--no-commit-id", "-M", "--numstat", "-z", "--diff-filter=ACMR", sha]));
		const tag = (f) => ({ ...f, file: `${f.file} (commit ${short})` });
		findings.push(...scanPaths(files.map((f) => f.path)).map(tag));
		findings.push(...scanDiff(git(cwd, ["diff-tree", "-r", "--root", "--no-commit-id", "-M", "-p", ...DIFF_OPTS, sha]), options).map(tag));
		for (const f of files.filter((x) => x.binary)) {
			const run = gitRaw(cwd, ["cat-file", "blob", `${sha}:${f.path}`]);
			if (run.status === 0) findings.push(...scanBlob(run.stdout, f.path, options).map(tag));
		}
	}
	return findings;
}

// Everything a commit or push could publish right now: all changes (staged or not), untracked files,
// unpushed commits on any branch. Used when the command could not be understood.
function scanPending(cwd, options) {
	const base = diffBase(cwd);
	return [
		...scanGitDiff(cwd, [base], options),
		...scanGitDiff(cwd, ["--cached", base], options),
		...scanUntracked(cwd, ["."], options),
		...scanCommits(cwd, ["--branches", "--not", "--remotes"], options),
	];
}

// ---------------------------------------------------------------- shell command parsing (Claude hook)

// Splits a Bash or PowerShell command line into simple commands (arrays of words, quotes removed).
// $(…) stays one opaque word; heredoc and here-string bodies are kept per command in `.bodies`.
function splitCommands(cmd) {
	const commands = [];
	let words = [];
	let word = null;
	let bodies = [];
	let pendingHeredocs = [];
	let skipNext = false;
	let i = 0;
	const s = String(cmd);
	const endWord = () => {
		if (word !== null) {
			if (skipNext) skipNext = false;
			else words.push(word);
		}
		word = null;
	};
	const endCommand = () => {
		endWord();
		if (words.length) { const c = words; c.bodies = bodies; commands.push(c); }
		else if (bodies.length && commands.length) commands[commands.length - 1].bodies.push(...bodies);
		words = [];
		bodies = [];
		skipNext = false;
	};
	const readHeredocBodies = () => {
		// s[i] is just after a newline
		for (const h of pendingHeredocs) {
			const lines = [];
			while (i < s.length) {
				let nl = s.indexOf("\n", i);
				if (nl < 0) nl = s.length;
				const lineText = s.slice(i, nl).replace(/\r$/, "");
				i = nl + 1;
				if ((h.strip ? lineText.replace(/^\t+/, "") : lineText) === h.tag) break;
				lines.push(lineText);
			}
			(commands.length ? commands[commands.length - 1].bodies : bodies).push(lines.join("\n"));
		}
		pendingHeredocs = [];
	};
	// Finds the end of $( … ) starting after "$(", honouring quotes, heredocs and nesting.
	const skipSubst = (j) => {
		let depth = 1;
		while (j < s.length && depth > 0) {
			const c = s[j];
			if (c === "'") { const e = s.indexOf("'", j + 1); j = e < 0 ? s.length : e + 1; continue; }
			if (c === '"') { j++; while (j < s.length && s[j] !== '"') { if (s[j] === "\\") j++; j++; } j++; continue; }
			if (c === "<" && s[j + 1] === "<") {
				const hm = /^<<(-?)\s*(['"]?)([A-Za-z_][A-Za-z0-9_]*)\2/.exec(s.slice(j));
				if (hm) {
					const lineEnd = s.indexOf("\n", j);
					if (lineEnd >= 0) {
						const re = new RegExp(`\\n${hm[1] ? "\\t*" : ""}${escapeRe(hm[3])}\\r?(?=\\n|$|\\))`);
						const m = re.exec(s.slice(lineEnd));
						j = m ? lineEnd + m.index + m[0].length : s.length;
						continue;
					}
				}
			}
			if (c === "(") depth++;
			else if (c === ")") depth--;
			j++;
		}
		return j;
	};
	while (i < s.length) {
		const c = s[i];
		if ((c === "\\" || c === "`") && (s[i + 1] === "\n" || s[i + 1] === "\r" && s[i + 2] === "\n")) { i += s[i + 1] === "\r" ? 3 : 2; continue; } // line continuation (Bash \, PowerShell `)
		if (c === "\n") {
			endCommand();
			i++;
			if (pendingHeredocs.length) readHeredocBodies();
			continue;
		}
		if (c === " " || c === "\t" || c === "\r") { endWord(); i++; continue; }
		if (c === "#" && word === null) { const nl = s.indexOf("\n", i); i = nl < 0 ? s.length : nl; continue; }
		if (c === ";" || c === "|" || c === "&" || c === "(" || c === ")" || (c === "{" || c === "}") && word === null) {
			endCommand();
			i++;
			continue;
		}
		if ((c === ">" || c === "<") && s[i + 1] !== "<") {
			if (word !== null && /^\d+$/.test(word)) word = null;
			endWord();
			i++;
			if (s[i] === ">" || s[i] === "&") i++;
			if (s[i - 1] === "&" && /\d/.test(s[i] || "")) { i++; continue; } // 2>&1
			skipNext = true;
			continue;
		}
		if (c === "<" && s[i + 1] === "<") {
			const hm = /^<<(-?)\s*(['"]?)([A-Za-z_][A-Za-z0-9_]*)\2/.exec(s.slice(i));
			endWord();
			if (hm) { pendingHeredocs.push({ tag: hm[3], strip: hm[1] === "-" }); i += hm[0].length; }
			else i += 2;
			continue;
		}
		if (c === "@" && word === null && (s[i + 1] === "'" || s[i + 1] === '"') && /^\r?\n/.test(s.slice(i + 2))) {
			// PowerShell here-string: @' … '@ / @" … "@ (closing at line start)
			const q = s[i + 1];
			const m = new RegExp(`\\n${q}@`).exec(s.slice(i + 2));
			const end = m ? i + 2 + m.index : s.length;
			const body = s.slice(i + 2, end).replace(/^\r?\n/, "");
			word = body;
			bodies.push(body);
			i = m ? end + 3 : s.length;
			continue;
		}
		if (c === "'") {
			const e = s.indexOf("'", i + 1);
			word = (word || "") + s.slice(i + 1, e < 0 ? s.length : e);
			i = e < 0 ? s.length : e + 1;
			continue;
		}
		if (c === '"') {
			let j = i + 1;
			let out = "";
			while (j < s.length && s[j] !== '"') {
				if ((s[j] === "\\" || s[j] === "`") && (s[j + 1] === '"' || s[j + 1] === "\\" || s[j + 1] === "`" || s[j + 1] === "$")) { out += s[j + 1]; j += 2; continue; }
				if (s[j] === "$" && s[j + 1] === "(") { const e = skipSubst(j + 2); out += s.slice(j, e); j = e; continue; }
				out += s[j];
				j++;
			}
			word = (word || "") + out;
			i = j + 1;
			continue;
		}
		if (c === "$" && s[i + 1] === "(") {
			const e = skipSubst(i + 2);
			word = (word || "") + s.slice(i, e);
			i = e;
			continue;
		}
		word = (word || "") + c;
		i++;
	}
	endCommand();
	return commands;
}

const SHELLS = /^(bash|sh|zsh|dash|ksh|powershell|pwsh|cmd)(\.exe)?$/i;
const PREFIXES = new Set(["env", "command", "builtin", "nohup", "exec", "time", "sudo", "nice", "xargs", "call", "start-process", "invoke-expression", "iex"]);

// Strips wrappers (env, nohup, sudo …) and returns { words } or, for bash -c / powershell -Command / cmd /c /
// eval, { script } with the inner command line to parse again.
function unwrap(words) {
	let w = words.slice();
	for (let guard = 0; guard < 10 && w.length; guard++) {
		const head = path.basename(w[0]).toLowerCase();
		if (/^[A-Za-z_][A-Za-z0-9_]*=/.test(w[0])) { w = w.slice(1); continue; }
		if (head === "eval") return { script: w.slice(1).join(" ") };
		if (head === "invoke-expression" || head === "iex") return { script: w.slice(1).filter((x) => !/^-command$/i.test(x)).join(" ") };
		if (PREFIXES.has(head)) {
			w = w.slice(1);
			while (w.length && (w[0].startsWith("-") || /^[A-Za-z_][A-Za-z0-9_]*=/.test(w[0]))) w = w.slice(/^-(u|n|p|g|C|I|L|s|E)$/.test(w[0]) && w.length > 1 && !w[1].startsWith("-") ? 2 : 1);
			continue;
		}
		const shell = SHELLS.exec(head);
		if (shell) {
			const name = shell[1].toLowerCase();
			for (let k = 1; k < w.length; k++) {
				const a = w[k];
				if (name === "cmd" && /^\/[ck]$/i.test(a)) return { script: w.slice(k + 1).join(" ") };
				if ((name === "powershell" || name === "pwsh") && /^-enc(odedcommand)?$/i.test(a) && w[k + 1]) {
					try { return { script: Buffer.from(w[k + 1], "base64").toString("utf16le") }; } catch { return { script: "" }; }
				}
				if ((name === "powershell" || name === "pwsh") && /^-c(o(m(m(a(n(d)?)?)?)?)?)?$/i.test(a)) return { script: w.slice(k + 1).join(" ") };
				if (name !== "cmd" && name !== "powershell" && name !== "pwsh" && /^-[a-z]*c[a-z]*$/.test(a)) return { script: w[k + 1] || "" };
			}
			return { words: [] };
		}
		break;
	}
	return { words: w };
}

const GIT_GLOBAL_WITH_VALUE = new Set(["-C", "-c", "--git-dir", "--work-tree", "--namespace", "--exec-path", "--config-env", "--super-prefix"]);

// -> { sub, args, dir, config: ["k=v"] } for a git invocation, or null
function parseGit(words) {
	let k = 0;
	if (k >= words.length || !/(^|[\\\/])git(\.exe)?$/i.test(words[k])) return null;
	k++;
	let dir = null;
	const config = [];
	while (k < words.length && words[k].startsWith("-")) {
		const w = words[k];
		if (w === "-C") { dir = dir ? path.join(dir, words[k + 1] || "") : words[k + 1]; k += 2; continue; }
		if (w === "-c") { if (words[k + 1]) config.push(words[k + 1]); k += 2; continue; }
		if (GIT_GLOBAL_WITH_VALUE.has(w)) { k += 2; continue; }
		k++;
	}
	if (k >= words.length) return null;
	return { sub: words[k], args: words.slice(k + 1), dir, config };
}

const COMMIT_SHORT_WITH_VALUE = "mFCct";
const COMMIT_LONG_WITH_VALUE = new Set(["--message", "--file", "--reuse-message", "--reedit-message", "--author", "--date", "--template", "--cleanup", "--fixup", "--squash", "--trailer", "--pathspec-from-file"]);

function parseCommit(args) {
	const r = { all: false, include: false, paths: [], messages: [], messageFiles: [], fromFile: false };
	for (let k = 0; k < args.length; k++) {
		const w = args[k];
		if (w === "--") { r.paths.push(...args.slice(k + 1)); break; }
		if (w.startsWith("--")) {
			const eq = w.indexOf("=");
			const name = eq >= 0 ? w.slice(0, eq) : w;
			const val = eq >= 0 ? w.slice(eq + 1) : undefined;
			if (name === "--all") r.all = true;
			else if (name === "--include") r.include = true;
			const v = val !== undefined ? val : COMMIT_LONG_WITH_VALUE.has(name) ? args[++k] : undefined;
			if (name === "--message" && v !== undefined) r.messages.push(v);
			if (name === "--author" && v !== undefined) r.messages.push(v);
			if (name === "--file" && v !== undefined) r.messageFiles.push(v);
			if (name === "--pathspec-from-file") r.fromFile = true;
			continue;
		}
		if (w.startsWith("-") && w.length > 1) {
			for (let j = 1; j < w.length; j++) {
				const ch = w[j];
				if (ch === "a") r.all = true;
				else if (ch === "i") r.include = true;
				if (COMMIT_SHORT_WITH_VALUE.includes(ch)) {
					const v = j + 1 < w.length ? w.slice(j + 1) : args[++k];
					if (ch === "m" && v !== undefined) r.messages.push(v);
					if (ch === "F" && v !== undefined) r.messageFiles.push(v);
					break;
				}
			}
			continue;
		}
		r.paths.push(w);
	}
	return r;
}

function parseAdd(args) {
	const paths = [];
	let all = false;
	for (let k = 0; k < args.length; k++) {
		const w = args[k];
		if (w === "--") { paths.push(...args.slice(k + 1)); break; }
		if (w === "-A" || w === "--all" || w === "-u" || w === "--update") { all = true; continue; }
		if (w === "--chmod" || w === "--pathspec-from-file") { k++; all = true; continue; }
		if (w.startsWith("-")) continue;
		paths.push(w);
	}
	return all && !paths.length ? ["."] : paths;
}

const PUSH_WITH_VALUE = new Set(["--repo", "-o", "--push-option", "--receive-pack", "--exec"]);

// -> revisions whose not-yet-pushed commits the push would send ([] for a pure delete)
function parsePush(args) {
	const r = { branches: false, tags: false, del: false, positional: [] };
	for (let k = 0; k < args.length; k++) {
		const w = args[k];
		if (w === "--") { r.positional.push(...args.slice(k + 1)); break; }
		if (w === "--all" || w === "--branches" || w === "--mirror") { r.branches = true; continue; }
		if (w === "--tags" || w === "--follow-tags") { r.tags = true; continue; }
		if (w === "--delete" || w === "-d") { r.del = true; continue; }
		if (PUSH_WITH_VALUE.has(w)) { k++; continue; }
		if (w.startsWith("-")) continue;
		r.positional.push(w);
	}
	if (r.del) return [];
	const revs = [];
	for (const spec of r.positional.slice(1)) {
		const src = spec.replace(/^\+/, "").split(":")[0];
		if (src) revs.push(src);
	}
	if (r.branches) revs.push("--branches");
	if (r.tags) revs.push("--tags");
	if (!revs.length && !r.positional.slice(1).length) revs.push("HEAD");
	return revs;
}

function toLocalPath(p) {
	if (process.platform === "win32") {
		const m = /^\/(?:mnt\/)?([a-zA-Z])(\/.*)?$/.exec(p);
		if (m) return `${m[1].toUpperCase()}:${m[2] || "/"}`;
	}
	return p;
}

function isWorkTree(dir) {
	try {
		if (!fs.statSync(dir).isDirectory()) return false;
	} catch { return false; }
	return (git(dir, ["rev-parse", "--is-inside-work-tree"], { allowFail: true }) || "").trim() === "true";
}

const GIT_COMMANDS = new Set(["add", "commit", "push", "status", "diff", "log", "show", "pull", "fetch", "rev-parse", "rev-list", "config", "remote", "branch", "checkout", "switch", "restore", "reset", "rm", "mv", "stash", "tag", "merge", "rebase", "grep", "ls-files", "cat-file", "init", "clone", "worktree", "update-index", "blame", "describe", "cherry-pick", "revert", "clean", "notes", "submodule", "apply", "am", "format-patch", "help", "version"]);

// The real subcommand behind a git alias (alias.ci=commit), "!" for a shell alias, or the name itself.
function resolveAlias(dir, g) {
	if (GIT_COMMANDS.has(g.sub)) return g.sub;
	const inline = g.config.map((c) => /^alias\.([^=]+)=(.*)$/i.exec(c)).find((m) => m && m[1] === g.sub);
	const value = inline ? inline[2] : (git(dir, ["config", "--get", `alias.${g.sub}`], { allowFail: true }) || "").trim();
	if (!value) return g.sub;
	if (value.startsWith("!")) return "!";
	return value.split(/\s+/)[0];
}

// Works out what a Bash/PowerShell command would commit or push and scans it.
// -> { relevant: bool, findings: [] }
function scanCommand(command, startCwd) {
	const state = { cwd: startCwd, relevant: false, unclear: false, findings: [], added: [], startCwd };
	walk(command, state, 0);
	const mentions = /git/i.test(command) && /\b(commit|push)\b/i.test(command);
	if (!state.relevant && mentions) state.unclear = true;
	if (state.unclear && isWorkTree(startCwd)) {
		state.relevant = true;
		state.findings.push(...scanPending(startCwd, contextFor(startCwd)));
	}
	return { relevant: state.relevant, findings: dedupe(state.findings) };
}

function walk(command, state, depth) {
	if (depth > 4) { state.unclear = true; return; }
	for (const raw of splitCommands(command)) {
		const u = unwrap(raw);
		if (u.script !== undefined) { walk(u.script, state, depth + 1); continue; }
		const words = u.words;
		words.bodies = raw.bodies;
		const head = (words[0] || "").toLowerCase();
		if (["cd", "set-location", "sl", "chdir", "pushd", "push-location"].includes(head)) {
			const target = words.slice(1).find((w) => !w.startsWith("-"));
			if (target) state.cwd = path.resolve(state.cwd, toLocalPath(target.replace(/^~(?=$|[\\\/])/, os.homedir())));
			continue;
		}
		const g = parseGit(words);
		if (!g) continue;
		// A folder the parser cannot resolve ($VAR, a cd that failed): scan the session's own repo instead.
		let dir = g.dir ? path.resolve(state.cwd, toLocalPath(g.dir)) : state.cwd;
		if (!isWorkTree(dir)) dir = state.startCwd;
		if (!isWorkTree(dir)) {
			if (g.sub === "commit" || g.sub === "push") throw new Error(`no git repository at ${dir}`);
			continue;
		}
		const sub = resolveAlias(dir, g);
		if (sub === "!") { state.unclear = true; continue; }
		if (sub === "add") { state.added.push({ dir, paths: parseAdd(g.args) }); continue; }
		if (sub !== "commit" && sub !== "push") continue;
		state.relevant = true;
		const options = contextFor(dir);
		if (sub === "push") {
			let revs = parsePush(g.args);
			if (!revs.length) continue;
			revs = revs.filter((r) => r.startsWith("--") || gitOk(dir, ["rev-parse", "--verify", "-q", `${r}^{commit}`]));
			if (!revs.length) revs = ["HEAD"];
			state.findings.push(...scanCommits(dir, [...revs, "--not", "--remotes"], options));
			continue;
		}
		const c = parseCommit(g.args);
		const base = diffBase(dir);
		if (c.paths.some((p) => /[$%`]/.test(p))) { state.unclear = true; continue; } // paths held in variables
		if (c.fromFile || c.all) state.findings.push(...scanGitDiff(dir, [base], options));
		if (c.paths.length) state.findings.push(...scanGitDiff(dir, [base, "--", ...c.paths], options));
		if (!c.paths.length || c.include) state.findings.push(...scanGitDiff(dir, ["--cached", base], options));
		const addPaths = state.added.filter((a) => a.dir === dir).flatMap((a) => a.paths);
		if (addPaths.length) {
			state.findings.push(...scanGitDiff(dir, [base, "--", ...addPaths], options));
			state.findings.push(...scanUntracked(dir, addPaths, options));
		}
		const message = [...c.messages, ...(words.bodies || [])];
		for (const f of c.messageFiles) {
			if (f === "-") continue; // heredoc body is in words.bodies
			try { message.push(fs.readFileSync(path.resolve(dir, toLocalPath(f)), "utf8")); } catch {}
		}
		if (message.length) state.findings.push(...scanText(message.join("\n"), "commit message", options));
	}
}

function dedupe(findings) {
	const seen = new Set();
	return findings.filter((f) => {
		const key = `${f.file}|${f.line}|${f.rule}|${f.sample}`;
		if (seen.has(key)) return false;
		seen.add(key);
		return true;
	});
}

const FIX_HINT = "Remove it before committing: use placeholders (C:/Users/x, you@example.com, <token>), unstage or delete forbidden files, and for an unpushed commit rewrite it locally first. If a hit is a false positive, fix the rule in .claude/hooks/secret-scan.js with a test; do not get around the check.";

function report(findings, what) {
	const lines = findings.slice(0, 25).map((f) => `  ${f.file}${f.line ? `:${f.line}` : ""}  ${f.rule}${f.sample ? `  [${f.sample}]` : ""}`);
	if (findings.length > 25) lines.push(`  … and ${findings.length - 25} more`);
	return `Secret scan blocked this ${what}: it would publish secrets or private data to the public repo.\n${lines.join("\n")}\n${FIX_HINT}`;
}

// ---------------------------------------------------------------- entry points

function claudeHook(event) {
	const command = event && event.tool_input && event.tool_input.command;
	if (typeof command !== "string" || !/git|-enc/i.test(command)) return null; // -enc: base64 PowerShell
	const cwd = event.cwd || process.env.CLAUDE_PROJECT_DIR || process.cwd();
	let result;
	try {
		result = scanCommand(command, cwd);
	} catch (err) {
		return deny(`Secret scan could not check this git commit/push, so it is blocked (fail closed): ${err.message}. Fix the cause (or the scanner in .claude/hooks/secret-scan.js) and try again.`);
	}
	if (!result.relevant || !result.findings.length) return null;
	return deny(report(result.findings, "git commit/push"));
}

function deny(reason) {
	return { hookSpecificOutput: { hookEventName: "PreToolUse", permissionDecision: "deny", permissionDecisionReason: reason } };
}

function readStdin() {
	try { return fs.readFileSync(0, "utf8"); } catch { return ""; }
}

function main(argv) {
	const mode = argv[2];
	if (mode === "claude-hook") {
		let event;
		try { event = JSON.parse(readStdin()); } catch (err) {
			// Cannot tell what the command is: let it through with a visible notice (exit 1), as the other hooks do.
			console.error(`secret-scan hook: unreadable hook input (${err.message}); call let through`);
			return 1;
		}
		const out = claudeHook(event);
		if (out) console.log(JSON.stringify(out));
		return 0;
	}
	const cwd = process.cwd();
	try {
		const options = contextFor(cwd);
		let findings = [];
		let what = mode;
		if (mode === "pre-commit") {
			findings = scanGitDiff(cwd, ["--cached", diffBase(cwd)], options);
			what = "commit";
		} else if (mode === "commit-msg") {
			const text = fs.readFileSync(argv[3], "utf8").split(/\r?\n/).filter((l) => !l.startsWith("#")).join("\n");
			findings = [...scanText(text, "commit message", options), ...scanText(git(cwd, ["var", "GIT_AUTHOR_IDENT"]).replace(/>.*$/s, ">"), "commit author", options)];
			what = "commit message";
		} else if (mode === "pre-push") {
			const revs = [];
			for (const line of readStdin().split(/\r?\n/)) {
				const [, localSha] = line.trim().split(/\s+/);
				if (localSha && !/^0+$/.test(localSha)) revs.push(localSha);
			}
			if (revs.length) findings = scanCommits(cwd, [...revs, "--not", "--remotes"], options);
			what = "push";
		} else if (mode === "history") {
			findings = scanCommits(cwd, ["--all"], options);
			what = "history";
			if (!findings.length) console.log("secret-scan: history clean");
		} else {
			console.error("usage: node secret-scan.js claude-hook | pre-commit | commit-msg <file> | pre-push | history");
			return 2;
		}
		findings = dedupe(findings);
		if (findings.length) {
			console.error(report(findings, what));
			return 1;
		}
		return 0;
	} catch (err) {
		console.error(`secret-scan: could not check (${err.message}); blocked. Fix the cause, or skip once with --no-verify only if you are sure.`);
		return 1;
	}
}

if (require.main === module) process.exitCode = main(process.argv);

module.exports = { scanText, scanDiff, scanPaths, scanBlob, decodeBuffer, splitCommands, unwrap, parseGit, parseCommit, parsePush, parseAdd, scanCommand, claudeHook, machineNames, looksLikeSecretValue, allowedEmail, isPlaceholderUser };

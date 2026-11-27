// Tests for secret-scan.js, run: `node --test .claude/hooks/secret-scan.test.js`.
// Fake secrets are built from pieces so this file itself never contains one (and passes the scan).
"use strict";

const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const s = require("./secret-scan");

const J = (...p) => p.join("");
const FAKE = {
	anthropic: J("sk-", "ant-api03-", "A".repeat(30), "bC9_-"),
	github: J("gh", "p_", "a1B2c3D4e5F6g7H8i9J0k1L2m3N4o5P6q7R8"),
	githubPat: J("github", "_pat_", "11ABCDEFG0123456789_", "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUV"),
	aws: J("AKIA", "IOSFODNN7EXAMPL1"),
	google: J("AI", "za", "SyA-1234567890abcdefghijklmnopqrstu"),
	slack: J("xo", "xb-", "123456789012-abcdefABCDEF"),
	pem: J("-----BEGIN ", "RSA PRIVATE", " KEY-----"),
	email: J("jane.doe", "@", "gmail.com"),
	userPath: J("C:", "\\Users\\", "jdoe", "\\AppData\\Local\\x"),
};

const rules = (text, opts) => s.scanText(text, "f", opts).map((f) => f.rule);

test("detects each kind of token and key", () => {
	assert.deepEqual(rules(`key = "${FAKE.anthropic}"`).includes("Anthropic API key"), true);
	assert.ok(rules(FAKE.github).includes("GitHub token"));
	assert.ok(rules(FAKE.githubPat).includes("GitHub token"));
	assert.ok(rules(FAKE.aws).includes("AWS access key id"));
	assert.ok(rules(FAKE.google).includes("Google API key"));
	assert.ok(rules(FAKE.slack).includes("Slack token"));
	assert.ok(rules(FAKE.pem).includes("private key block"));
	assert.ok(rules(J("https://bob:", "hunter2x9", "@example.org/repo")).includes("password in URL"));
});

test("generic password/token assignments: literals yes, code and placeholders no", () => {
	for (const line of [J("SECRET_KEY = '", "Tr0ub4dor&3", "'"), J("secretKey: ", "Ab12Cd34Ef56"), J("password_hash=", "Zx9Qw8Er7Ty6"),
		J("PASSWORD_PROD=", "Zx9Qw8Er7Ty6"), J('password="', "Pa$$w0rd!x", '"'), J('password="', "correcthorsebattery", '"'), J("pwd=", "Zx9Qw8Er7Ty6")]) {
		assert.ok(rules(line).includes("secret assignment"), line);
	}
	assert.ok(rules(J("Authorization: Bearer ", "0123456789abcdef0123456789abcdef")).includes("bearer token"));
	assert.ok(rules(J("npm", "_", "a".repeat(36))).includes("npm token"));
	assert.ok(rules(J("AccountKey=", "A".repeat(86), "==")).includes("Azure account key"));
	for (const line of ["token = 0x53d380", "password: 0x18018c0a0", "Token: FUN_180123456", "token = local_28", "password = DAT_1800abcd",
		"password: std::string", "token = uVar12", "input_tokens: 123456789", "token: TokenKind", 'kind: "refresh_token"', "token = param_1"]) {
		assert.ok(!rules(line).includes("secret assignment"), line);
	}
	for (const line of [J("password", "=", "Tr0ub4dor&3"), J('"api_key": "', "9f8e7d6c5b4a", '"'), J("TOKEN", "=", "abc123def456ghi"), J("db_password: ", "'S3cretPass'")]) {
		assert.ok(rules(line).includes("secret assignment"), line);
	}
	for (const line of [
		"password = getPassword()", "token = data.token", "token: string", "password=<your password>", "TOKEN=${TOKEN}",
		"$env:API_KEY = $key", "password=/token=… assignments", "token = process.env.GH_TOKEN", "secret = None",
		"tokenizer = splitCommands(cmd)", "password: required", "token = TOKEN_NAME", "token=xxxxxxxxxx",
	]) {
		assert.ok(!rules(line).includes("secret assignment"), line);
	}
});

test("e-mail addresses: real ones flagged, noreply / example / attribution allowed", () => {
	assert.ok(rules(`contact ${FAKE.email}`).includes("e-mail address"));
	for (const ok of ["157850246+ErwinsArm@users.noreply.github.com", "Co-Authored-By: Claude <noreply@anthropic.com>",
		"you@example.com", "git@github.com:ErwinsArm/Kenshi-Pure.git", "logo@2x.png", "rea-agents@6.10.1", "@anthropic-ai/sdk"]) {
		assert.ok(!rules(ok).includes("e-mail address"), ok);
	}
});

test("user folder paths: real names flagged in every spelling, placeholders allowed", () => {
	for (const p of [FAKE.userPath, J("C:/Users/", "jdoe/Documents"), J('"C:\\\\Users\\\\', 'jdoe\\\\x"'), J("/c/Users/", "jdoe/x"), J("/mnt/c/Users/", "jdoe"), J("/home/", "jdoe/.ssh"),
		J("c:\\users\\", "bob\\x"), J("C:\\USERS\\", "BOB"), J("/Users/", "jdoe/Library")]) {
		assert.ok(rules(p).includes("user folder path"), p);
	}
	for (const p of ["C:/Users/x/side-session-notes", "C:\\Users\\<name>\\AppData", "C:\\Users\\%USERNAME%", "C:\\Users\\$env:USERNAME",
		"C:\\Users\\Public", "C:/Users/you/x", "%APPDATA%\\ghidra", "%LOCALAPPDATA%\\ghidra", "/home/user/x", "~/home/x"]) {
		assert.ok(!rules(p).includes("user folder path"), p);
	}
});

test("this computer's user name is flagged as a word, the public handle is not", () => {
	const opts = { machineNames: ["jdoe"] };
	assert.ok(rules("profile of jdoe here", opts).includes("this computer's user name"));
	assert.ok(rules("JDOE", opts).includes("this computer's user name"));
	assert.ok(!rules("jdoes jdoe2 xjdoe", opts).includes("this computer's user name"));
	// machineNames drops names equal to the repo owner's public handle
	const real = s.machineNames(["ErwinsArm"]);
	assert.ok(!real.some((n) => n.toLowerCase() === "erwinsarm"));
});

test("no false positives on this repo's typical content", () => {
	const text = [
		"RVA 0x53d380, image base 0x180000000", "SHA-256 a596ab4e407c67b58599c54ffb32dc1bf2b64510cdebd3fa9359ef05a576aeb1",
		"https://claude.ai/artifact/3BN86bz1A5bWUYGGpA9BRz", "ErwinsArm/Kenshi-Pure", "protocol 0x88, ABI 0x60",
		"Plugin=mods/AnarchyKenshi/AnarchyLoader", "?startPlugin@@YAXXZ", "const token = tokens[0];",
	].join("\n");
	assert.deepEqual(s.scanText(text, "f", { machineNames: [] }), []);
});

test("forbidden file names", () => {
	const hit = (p) => s.scanPaths([p]).length > 0;
	for (const p of ["KenshiMultiplayer 0.133.1/AnarchyKenshi/AnarchyKenshi.dll", "x/AnarchyCore.dll", "AnarchyKenshi_2026.log",
		"mods/AnarchyKenshi/AnarchyKenshi-logins.txt", "crash/kenshi.dmp", "a.mdmp", "id_ed25519", "cert.pem", ".env", ".env.local", ".mcp.json",
		".claude/settings.local.json", "tools/rea/x.js", "mods\\AnarchyKenshi\\AnarchyKenshi.log"]) {
		assert.ok(hit(p), p);
	}
	for (const p of ["docs/findings/AnarchyKenshi.dll.md", "client/KenshiPure.cpp", ".env.example", "scripts/setup-rea.ps1", "docs/tools.md"]) {
		assert.ok(!hit(p), p);
	}
});

test("diff scan: only added lines, with file and line number", () => {
	const diff = [
		"diff --git a/doc.md b/doc.md", "--- a/doc.md", "+++ b/doc.md", "@@ -1,2 +10,3 @@",
		` old ${FAKE.email}`, `-removed ${FAKE.email}`, "+fine line", `+mail ${FAKE.email}`,
	].join("\n");
	const f = s.scanDiff(diff, { machineNames: [] });
	assert.equal(f.length, 1);
	assert.equal(f[0].file, "doc.md");
	assert.equal(f[0].line, 12);
	assert.ok(!f[0].sample.includes("gmail"), "redacted");
});

test("command parsing: Bash and PowerShell commit/push forms", () => {
	const cmds = s.splitCommands(`cd "x y" && git -C repo commit -m "$(cat <<'EOF'\nmsg (with paren)\nEOF\n)" -- a.md "b c.md" 2>&1 | tail -3; git push`);
	const gits = cmds.map(s.parseGit).filter(Boolean);
	assert.equal(gits.length, 2);
	assert.equal(gits[0].sub, "commit");
	assert.equal(gits[0].dir, "repo");
	const c = s.parseCommit(gits[0].args);
	assert.deepEqual(c.paths, ["a.md", "b c.md"]);
	assert.match(c.messages[0], /msg \(with paren\)/);
	assert.equal(gits[1].sub, "push");

	const ps = s.splitCommands("git commit -m @'\nTitle\nBody line\n'@ -- docs/setup.md; if ($?) { git push origin main }");
	const pg = ps.map(s.parseGit).filter(Boolean);
	assert.deepEqual(s.parseCommit(pg[0].args).paths, ["docs/setup.md"]);
	assert.match(s.parseCommit(pg[0].args).messages[0], /Body line/);
	assert.deepEqual(s.parsePush(pg[1].args), ["main"]);

	assert.deepEqual(s.parseCommit(["-am", "x"]).all, true);
	assert.deepEqual(s.parseCommit(["-am", "x"]).paths, []);
	assert.deepEqual(s.parsePush(["origin", "+HEAD:refs/heads/x", ":gone"]), ["HEAD"]);
	assert.deepEqual(s.parsePush(["--all"]), ["--branches"]);
	assert.deepEqual(s.parsePush([]), ["HEAD"]);
	assert.deepEqual(s.parseAdd(["-A"]), ["."]);
	assert.equal(s.parseGit(["git", "status"]).sub, "status");
	assert.equal(s.parseGit(["echo", "git", "commit"]), null);
});

// ---------------------------------------------------------------- against a real temporary repo

function tempRepo() {
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), "secret-scan-test-"));
	const g = (...args) => {
		const r = spawnSync("git", args, { cwd: dir, encoding: "utf8" });
		assert.equal(r.status, 0, r.stderr);
		return r.stdout;
	};
	g("init", "-q", "-b", "main");
	g("config", "user.name", "t");
	g("config", "user.email", "t@users.noreply.github.com");
	g("config", "commit.gpgsign", "false");
	fs.writeFileSync(path.join(dir, "a.md"), "clean\n");
	g("add", "a.md");
	g("commit", "-q", "-m", "init");
	return { dir, g, write: (f, t) => fs.writeFileSync(path.join(dir, f), t) };
}

const hook = (command, cwd) => s.claudeHook({ tool_name: "Bash", tool_input: { command }, cwd });
const denied = (out) => out !== null && out.hookSpecificOutput.permissionDecision === "deny";

test("hook: path commit scans the named files' working-tree content", (t) => {
	const r = tempRepo();
	t.after(() => fs.rmSync(r.dir, { recursive: true, force: true }));
	r.write("a.md", `clean\n${FAKE.userPath}\n`);
	r.write("other.md", FAKE.email); // untracked, not named: not this commit's business
	const out = hook('git commit -m "x" -- a.md', r.dir);
	assert.ok(denied(out));
	assert.match(out.hookSpecificOutput.permissionDecisionReason, /a\.md:2\s+user folder path/);
	assert.doesNotMatch(out.hookSpecificOutput.permissionDecisionReason, /jdoe/, "redacted");
	r.write("a.md", "clean\nC:/Users/x/fine\n");
	assert.equal(hook('git commit -m "x" -- a.md', r.dir), null);
});

test("hook: staged commit, add-then-commit in one line, forbidden file, message", (t) => {
	const r = tempRepo();
	t.after(() => fs.rmSync(r.dir, { recursive: true, force: true }));
	r.write("new.txt", FAKE.github);
	assert.ok(denied(hook("git add new.txt && git commit -m x", r.dir)), "untracked file added in the same line");
	r.g("add", "new.txt");
	assert.ok(denied(hook("git commit -m x", r.dir)), "staged");
	r.g("rm", "-q", "--cached", "new.txt");
	fs.rmSync(path.join(r.dir, "new.txt"));
	r.write("AnarchyKenshi-logins.txt", "x");
	r.g("add", "AnarchyKenshi-logins.txt");
	assert.match(hook("git commit -m x", r.dir).hookSpecificOutput.permissionDecisionReason, /login file/);
	r.g("rm", "-q", "--cached", "AnarchyKenshi-logins.txt");
	assert.ok(denied(hook("git status && echo commit", r.dir)), "unclear mention: whole pending state scanned, untracked login file found");
	fs.rmSync(path.join(r.dir, "AnarchyKenshi-logins.txt"));
	assert.ok(denied(hook(`git commit --allow-empty -m "mail me at ${FAKE.email}"`, r.dir)), "message");
	assert.equal(hook("git commit --allow-empty -m fine", r.dir), null);
	assert.equal(hook("git status && echo commit", r.dir), null, "unclear mention, clean state");
	assert.equal(hook("git status", r.dir), null, "no commit or push");
});

test("hook: wrapped git (bash -c, powershell -Command, cmd /c, env, eval, alias, continuation) is still scanned", (t) => {
	const r = tempRepo();
	t.after(() => fs.rmSync(r.dir, { recursive: true, force: true }));
	r.write("a.md", `x ${FAKE.email}\n`);
	for (const cmd of [
		'bash -c "git commit -am x"', "sh -lc 'git commit -a -m x'", 'powershell -NoProfile -Command "git commit -m x -- a.md"',
		"cmd /c git commit -m x -- a.md", "env GIT_TRACE=0 git commit -m x -- a.md", "nohup git commit -m x -- a.md",
		'eval "git commit -m x -- a.md"', "git -c alias.zz=commit zz -m x -- a.md", "git commit -m x `\n -- a.md",
		"$g = 'git'; & $g commit -m x -- a.md", 'f=a.md; git commit -m x -- "$f"',
		`powershell -EncodedCommand ${Buffer.from("git commit -m x -- a.md", "utf16le").toString("base64")}`,
	]) {
		assert.ok(denied(hook(cmd, r.dir)), cmd);
	}
	r.g("config", "alias.ci", "commit");
	assert.ok(denied(hook("git ci -m x -- a.md", r.dir)), "alias from config");
});

test("hook: pure delete push and unknown refs do not error", (t) => {
	const r = tempRepo();
	t.after(() => fs.rmSync(r.dir, { recursive: true, force: true }));
	assert.equal(hook("git push origin --delete old-branch", r.dir), null);
	assert.equal(hook("git push origin :old-branch", r.dir), null);
	assert.equal(hook("git push origin no-such-ref", r.dir), null);
});

test("binary-classified files: UTF-16 text decoded, MZ executables flagged", (t) => {
	const r = tempRepo();
	t.after(() => fs.rmSync(r.dir, { recursive: true, force: true }));
	fs.writeFileSync(path.join(r.dir, "out.txt"), Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(`log ${FAKE.userPath}\r\n`, "utf16le")]));
	r.g("add", "out.txt");
	assert.match(hook("git commit -m x", r.dir).hookSpecificOutput.permissionDecisionReason, /out\.txt:1\s+user folder path/);
	r.g("commit", "-q", "--no-verify", "-m", "utf16");
	assert.match(hook("git push", r.dir).hookSpecificOutput.permissionDecisionReason, /out\.txt \(commit [0-9a-f]+\):1\s+user folder path/);
	assert.ok(s.scanBlob(Buffer.from("MZ\x90\x00\x03\x00", "latin1"), "x.bin", {}).some((f) => /MZ/.test(f.rule)));
	assert.equal(s.decodeBuffer(Buffer.from([1, 0, 0, 2, 0, 0, 0, 7])), null, "real binary");
	assert.equal(s.decodeBuffer(Buffer.from("hi", "utf16le")), "hi", "UTF-16 without BOM");
});

test("push: empty forbidden file and pure rename to a forbidden name are caught", (t) => {
	const r = tempRepo();
	t.after(() => fs.rmSync(r.dir, { recursive: true, force: true }));
	r.write("AnarchyKenshi.log", "");
	r.g("add", "AnarchyKenshi.log");
	r.g("commit", "-q", "-m", "empty");
	assert.match(hook("git push", r.dir).hookSpecificOutput.permissionDecisionReason, /AnarchyKenshi log/);
	r.g("reset", "-q", "--hard", "HEAD~1");
	r.g("mv", "a.md", "AnarchyKenshi-logins.txt");
	r.g("commit", "-q", "-m", "rename");
	assert.match(hook("git push", r.dir).hookSpecificOutput.permissionDecisionReason, /login file/);
});

test("diff parser: an added line starting with '++ ' is content, not a header", () => {
	const secret = J("token=", "Zx9Qw8Er7Ty6");
	const diff = ["diff --git a/f b/f", "--- a/f", "+++ b/f", "@@ -0,0 +1,2 @@", `+++ ${secret}`, "+ok"].join("\n");
	const f = s.scanDiff(diff, {});
	assert.equal(f.length, 1);
	assert.equal(f[0].file, "f");
	assert.equal(f[0].line, 1);
});

test("hook: push scans outgoing commits including author e-mail; history mode too", (t) => {
	const r = tempRepo();
	t.after(() => fs.rmSync(r.dir, { recursive: true, force: true }));
	assert.equal(hook("git push origin main", r.dir), null, "clean history, nothing on a remote: all commits scanned, clean");
	r.write("b.md", `token=${J("Zx9", "Qw8Er7Ty6")}\n`);
	r.g("add", "b.md");
	r.g("commit", "-q", "-m", "b");
	assert.match(hook("git push", r.dir).hookSpecificOutput.permissionDecisionReason, /b\.md:1 \(commit|b\.md \(commit [0-9a-f]+\):1\s+secret assignment/);
	const hist = spawnSync("node", [path.join(__dirname, "secret-scan.js"), "history"], { cwd: r.dir, encoding: "utf8" });
	assert.equal(hist.status, 1);
	r.g("reset", "-q", "--hard", "HEAD~1");
	r.g("-c", `user.email=${FAKE.email}`, "commit", "-q", "--allow-empty", "-m", "c");
	assert.match(hook("git push", r.dir).hookSpecificOutput.permissionDecisionReason, /author\/message.*e-mail address/);
});

test("hook: fails closed when git cannot answer, ignores non-git commands", (t) => {
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), "secret-scan-norepo-"));
	t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
	assert.match(hook("git commit -m x", dir).hookSpecificOutput.permissionDecisionReason, /fail closed/);
	assert.equal(hook("ls -la", dir), null);
});

test("script: claude-hook mode over stdin; unreadable input exits 1", () => {
	const script = path.join(__dirname, "secret-scan.js");
	const ok = spawnSync("node", [script, "claude-hook"], { input: JSON.stringify({ tool_name: "Bash", tool_input: { command: "ls" } }), encoding: "utf8" });
	assert.equal(ok.status, 0);
	assert.equal(ok.stdout, "");
	const bad = spawnSync("node", [script, "claude-hook"], { input: "nope", encoding: "utf8" });
	assert.equal(bad.status, 1);
});

test("hook: an unresolvable cd target falls back to the session's repo", (t) => {
	const r = tempRepo();
	t.after(() => fs.rmSync(r.dir, { recursive: true, force: true }));
	r.write("a.md", FAKE.email);
	assert.ok(denied(hook('cd "$NOPE/missing" && git commit -m x -- a.md', r.dir)));
});

test("git hooks: pre-commit and commit-msg block through git itself", (t) => {
	const r = tempRepo();
	t.after(() => fs.rmSync(r.dir, { recursive: true, force: true }));
	const hooks = path.join(r.dir, ".githooks");
	fs.mkdirSync(hooks);
	for (const h of ["pre-commit", "commit-msg"]) {
		const src = fs.readFileSync(path.join(__dirname, "..", "..", ".githooks", h), "utf8");
		fs.writeFileSync(path.join(hooks, h), src.replace('"$root/.claude/hooks/secret-scan.js"', JSON.stringify(path.join(__dirname, "secret-scan.js").split(path.sep).join("/"))), { mode: 0o755 });
	}
	r.g("config", "core.hooksPath", ".githooks");
	r.write("b.md", FAKE.userPath);
	r.g("add", "b.md");
	const blocked = spawnSync("git", ["commit", "-q", "-m", "b"], { cwd: r.dir, encoding: "utf8" });
	assert.notEqual(blocked.status, 0);
	assert.match(blocked.stderr, /user folder path/);
	r.write("b.md", "fine\n");
	r.g("add", "b.md");
	const msg = spawnSync("git", ["commit", "-q", "-m", `b ${FAKE.email}`], { cwd: r.dir, encoding: "utf8" });
	assert.notEqual(msg.status, 0);
	assert.match(msg.stderr, /e-mail address/);
	r.g("commit", "-q", "-m", "b fine");
});

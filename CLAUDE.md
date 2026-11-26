# CLAUDE.md

Reverse engineering **AnarchyKenshi**, a Kenshi multiplayer mod (v0.103.2), with Claude doing the
analysis and the user directing it. The mod is four native x64 C++ DLLs in
`KenshiMultiplayer 0.103.2 …/AnarchyKenshi/`: `AnarchyKenshi.dll` (~17 MB, main logic),
`AnarchyCore.dll`, `AnarchyLauncher.dll`, `AnarchyLoader.dll`, loaded as an Ogre plugin via the
game's `Plugins_x64.cfg`; plus enable/disable scripts and `AnarchyKenshi.cfg` (game server address).
The goal and what comes after understanding the mod are the user's call; ask when it changes the work.

Everything we write is English: code, names, notes, commits.

## Replies

Reply in a few one-line bullets (~80 words; a hook notes replies past 140): what was found or
changed, what isn't done, what the user must do or decide. Plain words; intent and effect before
mechanics. Link at most one file to open. Longer only for a failure, a risk, a decision that
changes what they do, or detail they asked for. Say whether each claim is measured, documented or
inferred (see "Evidence").

## Start of every session

1. Do what the hooks ask first.
2. Read `WORK.md` (open work, one line each; create it when there is any) and the index of
   `docs/findings/` if they exist. Delete `WORK.md` entries git log shows done; never mark one "fixed".
3. The user's latest message picks the next step.

## Layout

- `KenshiMultiplayer …/AnarchyKenshi/`: **the original mod files. Evidence: never edit, rebuild,
  rename or "fix" them.** Work on copies outside the repo if a tool must write next to a binary.
- `tools/` (gitignored): local tooling, owned by its setup — REA (MCP server for agent-driven RE)
  with portable Ghidra, JDK and Node in `tools/rea/`, wired up as the `rea` server in `.mcp.json`.
  How to use it: the `reverse-engineer-anything` skill (load it before RE work with REA). Don't
  edit `tools/`, `.mcp.json` or `.gitignore` without the user asking.
- `docs/findings/`: what we know, one file per binary or topic (e.g. `AnarchyKenshi.dll.md`,
  `protocol.md`, `loader.md`), plus `docs/findings/README.md` as the index (create both with the
  first finding).
- `.claude/`: hooks, critics, the `critique` skill. Hook tests: `node --test .claude/hooks/*.test.js`.

## Safety with the mod

- The DLLs and scripts are untrusted third-party code. **Static analysis is the default.** Never
  load, inject or run them, run the enable/disable scripts, or launch Kenshi with the mod, unless
  the user asks for that run in words; then say what it will touch (game folder, `Plugins_x64.cfg`).
- Never contact the server in `AnarchyKenshi.cfg` or its server browser (no pings, probes,
  logins, captures you start) without the user's OK for that exact action.
- No secrets in the repo (account names, passwords, tokens from configs or captures).
- The GitHub repo is public. Pushing the mod's binaries, decompiled code or large dumps there is
  redistribution: the user's call, ask first. Ghidra projects and dumps stay in gitignored folders.
- No cheats, anti-cheat bypasses or tools aimed at other players or the mod's live servers.

## Evidence (findings and notes)

- Every finding names: the file (name + SHA-256, or mod version), the address as an **RVA**
  (image base stated once per file), and how it is known: *measured* (seen in disassembly,
  strings, imports, xrefs or a run), *documented* (source and date read), or *inferred*.
- Decompiler names and types are guesses until a second piece of evidence backs them; mark them.
- A conclusion others will build on (struct layout, packet format, call flow) cites its 2–3
  strongest pieces of evidence.
- Notes hold facts, not history: no logs, dates of decisions, or who said what. Rewrite a note
  when a finding changes; don't append "update:" paragraphs.
- A new rule goes into a doc only when the user asks for that rule in words (a yes to a plan
  doesn't count); one-time feedback never does.

## Who decides

- Decide *how* yourself; ask only when the answer changes the work. *What* (goal, scope, anything
  outward-facing: pushes of third-party code, posting, contacting servers) is the user's call.
- Build or analyse what the user names. Think another way is better? Say so with evidence first.
- What the user saw (in game, in a tool) is data; never overrule it with an inference. Stop when
  their bar is met; further ideas are proposals.
- Told to work alone ("use up my usage"): don't wait for answers; take the sensible default, note
  the open question in `WORK.md`, keep going.

## Model and effort

Every subagent and started session names `model` and `effort` (a hook checks).
- **Opus 5.5 high:** load-bearing analysis (protocol, struct layouts, call flow the rest builds
  on), anything touching safety rules, hooks, permissions, hard bugs (xhigh if high fails).
- **Opus medium:** coordinating sessions, planning.
- **Sonnet 5.5 high:** everyday work: sweeping decompiles for strings/xrefs, writing up notes,
  fully specified code with an automatic check. Critics: Sonnet, medium quick, high full.
- **Haiku 5.5 medium:** bulk text work checked by script (listing exports, formatting tables).
- Unsure → Sonnet high. Never Sonnet above high or Haiku at max. Per token Opus ≈ 2× Sonnet ≈ 40×
  Haiku; most cost is a session re-reading its own history.

## Side sessions

- A long job the user asks for goes to one session whose brief carries it to the end (analysis,
  notes, commit). Detach it right after it starts, else each report re-reads the starter's history.
- A handed-off job stays in its scope; anything outside it becomes a `WORK.md` line.
- Past ~200k context (a hook warns), suggest a fresh session for new work, after the findings and
  `WORK.md` hold what it needs.
- Subagents past 250k get a hand-over request, past 500k only git and the note (context guard).

## Critic review

The `critique` skill (critic-builder, critic-skeptic in parallel; quick and full are defined
there). Run it: before a conclusion others will build on is written down as fact (both full); on a
reimplementation or tooling plan (builder full, skeptic quick); on a `.claude/` or setup change
(quick, full if it touches permissions, hooks or security); before anything is published; when the
user asks. Not for small notes or fixes. Can't spawn them → ask the user to type `/critique`.
Runtime evidence outranks critics.

## Fixing problems

- Any problem (a wrong finding, a broken tool, a repeated mistake): fix the cause, not the symptom.
  Ask why until you know what would have prevented it — or that nothing cheap would.
- Too big for this round? Patch, say so, add one `WORK.md` line for the real fix.
- Fix commits name the cause, and whether it was reproduced or guessed.

## End of every task

1. Changed `.claude/hooks/`? `node --test .claude/hooks/*.test.js` passes.
2. Fix notes the work made false; delete finished `WORK.md` entries; delete scratch files you made
   (only yours: other sessions share this checkout).
3. Commit by path in plain English without asking: `git commit -m "<msg>" -- <files>`, never a
   plain `git commit` (it takes what another session staged). Push only when the user says so.
4. Edit this file only for rules every session needs; keep it under 200 lines.

---
name: tech-debt
description: A rare, whole-project technical-debt review (anything that keeps costing work or risk, not only mess) - several Opus scanners plus both critics, so it runs only when the user types /tech-debt or asks for a whole-project technical-debt review, never for a small cleanup.
argument-hint: "[optional: an area or worry to focus on]"
---

# Technical debt review

**A rare, whole-project review: thoroughness before speed and cost.** Every area is read, none
sampled. Run it from a fresh session.

## 1. Measure

`node .claude/skills/tech-debt/upkeep.js` (the area map, change history, biggest notes and code),
`node --test .claude/hooks/*.test.js`, and `git status` (untracked or half-done work). If the
`get_usage` tool is available, note the plan usage before and after.

## 2. Scan: all in parallel, one message

Each scanner is `model: "opus"`, `effort: "high"`, and gets `brief.md` plus its share of the
measurements.
- **Big picture:** the definition from `brief.md` without its examples, `CLAUDE.md`, `WORK.md`,
  `docs/findings/README.md`, the area map and the measurements. It asks what running this whole
  setup costs us, and what we would not build this way today. It returns the same findings.
- **Detail scanners:** split the area map so that every row has one owner and no scanner reads
  more than about 40,000 lines of our own text. As a start: `.claude/` (hooks, critics, skills;
  skip `reverse-engineer-anything`, it is REA's and updated by REA), process (root files,
  `CLAUDE.md`, `WORK.md`), `docs/findings/`, and each folder of our own code or scripts. The
  original mod folder is judged only for how we handle it (untouched? hashes recorded?), never read
  as our code. `tools/` is gitignored local tooling: judge the setup it needs, not its contents.

## 3. Judge, rank, check coverage

Every area-map row must come back marked *read* or *partly read*, with what was left out and why.
A gap or a *skipped* row gets one more scanner. Check each finding (open the files, confirm the
numbers), and delegate the checks if there are many. Drop what has no ongoing cost and no risk.
Merge findings that share a cause.

Rank by cost or risk, analysis-slowing first. Show the size beside each finding; never divide by
it. "Leave it" is a valid verdict.

## 4. Critique

The `critique` skill, both critics at full depth. They get the ranked list, the dropped and cut
findings with reasons, the coverage table and the measurements. Their requirement: no blind
spots, nothing left over, the bigger picture rather than the details. A "missing" finding gets one
targeted scanner. Skip the critique skill's doc line: no review findings go into the notes.

## 5. Report and stop

In chat, plain words.
- First line: **My pick**, which numbers and why.
- At most 8 numbered cards. Each card has three lines:
  - **N. Headline**: what it does to us today.
  - **Cause:** one line.
  - **Size · verdict** (fix now / when it next bites / leave) **· cost or risk** (measured or
    guessed) **· slows analysis?**
- Then one line for each remaining finding, and the run's usage if measured.

**Stop: the user picks** ("1, 3"). Each pick becomes a normal task under CLAUDE.md, and its brief
carries the fix and the "keeps it away" step.

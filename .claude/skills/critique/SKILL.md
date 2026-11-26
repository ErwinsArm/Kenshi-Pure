---
name: critique
description: Run the project's 2-critic review (critic-builder, critic-skeptic), then judge each finding. Use only at the moments listed in CLAUDE.md "Critic review" or when the user types /critique; otherwise not while working.
argument-hint: "[what to review — files, finding, decision, or milestone]"
---

# 2-critic review

The user has authorised these subagents as a standing instruction; `/critique` is the explicit
request if a session is unsure.

## 1. One neutral brief

Same text for every critic, except the QUICK CHECK line. It contains:
- **What is decided or delivered**, in one or two sentences.
- **Where:** exact file paths (line ranges for code), and binary + addresses for findings.
- **The user's requirements**, quoted.
- **Context:** reverse engineering the AnarchyKenshi mod with Claude; tools and rules in
  `CLAUDE.md`; findings notes in `docs/findings/`.
- **Out of scope.**
- **Today's date** (for their web checks).

Never include why you think it's good, rejected alternatives, or what you expect them to find —
that anchors them.

## 2. Spawn in parallel, one message

`critic-builder`, `critic-skeptic`; wait for both. Which critics run and how deep (quick or full)
comes from CLAUDE.md "Critic review". Each call's `model` and `effort` come from CLAUDE.md "Model
and effort" (critics line).

A quick critic's brief starts with:
"QUICK CHECK: about 10 file reads or binary lookups, at most 5 web searches, at most 5 findings,
under 250 words; keep only the verdict and findings. If this needs a full review, answer 'needs
full: <reason>'."
A full critic gets no such line and works to its own limits. Rerun a "needs full" critic full.

## 3. Judge every finding — neither appease nor defend

- **Accept** → change the work.
- **Reject** → one-line reason from evidence or the user's requirements ("I disagree" isn't one).
- **Needs data** → name the check (an xref, a second call site, a hash), run it if cheap, then decide.
- A question a critic says the user should answer waits for the user.
- Verify a critic's factual claims before acting; critics can be wrong.
- Critics conflict → decide on evidence; say which way and why.
- The user's calls (goal, scope, anything outward-facing) go to the user as a question.

## 4. Apply and report

1. Apply accepted changes. A rejected alternative a later session might bring back gets one line
   in the note it concerns: "Not X — Y" (reason ≤ 10 words, no history).
2. Tell the user in a few lines: verdicts, changes, rejections with reasons, questions only they
   can answer.

A second round only if the first found a blocker that changed the work substantially. Never loop
until the critics are happy.

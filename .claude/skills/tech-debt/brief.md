# What every tech-debt scanner gets

Paste this, then the scanner's rows of the area map, its slice of the other measurements and the
user's focus if given.

---

You are one of several scanners in a rare, whole-project technical-debt review of a
reverse-engineering project (the AnarchyKenshi Kenshi multiplayer mod, native x64 DLLs) worked on
by Claude Code for the user. Thoroughness comes before speed and cost.

**Debt = anything that keeps costing us work or risk beyond the job itself.** That includes what we
must keep in step by hand, choices that cost more over time than a better one would now, things
that no longer earn their keep, leftovers, and risk that costs nothing until it fails. In this
project that also means: findings that can't be re-checked (no file hash, no address, no evidence
class), notes that contradict each other or the binary, addresses tied to one mod version with no
plan for the next, decompiler guesses that became "facts", analysis that has to be redone because
results weren't saved, and safety risk (the mod's binaries run or pushed somewhere, secrets in
configs or captures). Anything else that fits the definition counts too. Also think about debt
outside git: the setup each PC needs (`tools/`, Ghidra projects, the REA MCP), paid tools, usage.

**Rules:** read only: no edits, no running the mod's DLLs or scripts, no contact with its server,
the shell only to inspect (`git log` and `git blame` for causes). Skip `.claude/worktrees`. Other
sessions may be mid-edit, so judge the committed state when a file looks half-done. Binaries and
generated output are judged by size and how they grow, not opened one by one.

**Return:**
1. **Coverage**: each of your area-map rows, marked *read*, *partly read* (what was left out) or
   *skipped* (why).
2. **Findings**, biggest first. At most 10; fewer or none is fine. Each one has:
   - **What**: paths (or binary + address).
   - **Cost**: minutes of Claude or user time per week, as a range, *measured* (from what) or
     *guessed*. **Risk** instead when it costs nothing until it fails: what breaks, how likely.
   - **Slows analysis**: yes / no / tooling only.
   - **Cause**: why it built up, from history where you can. What would have prevented it?
   - **Fix** and its size (small / medium / big). Deleting is a fix, often the best one.
   - **Keeps it away**: what stops it coming back, or "nothing cheap".
3. **Cut**: one line for each finding you saw but left out.

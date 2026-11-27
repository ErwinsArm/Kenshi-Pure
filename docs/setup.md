# Setup: REA reverse-engineering toolchain

## Secret scan (recommended for everyone who commits)

The repo is public. `.claude/hooks/secret-scan.js` blocks a commit or push that adds API keys or
tokens, private keys, `password=`-style values, real e-mail addresses, user-folder paths with a real
name (`C:\Users\<name>`), your computer's user name, or files that must never be committed (the
AnarchyKenshi mod files, its logs and `AnarchyKenshi-logins.txt`, crash dumps, archives). Commit
author addresses are checked too, so set a GitHub noreply address (`git config user.email
<id>+<login>@users.noreply.github.com`). Claude Code runs the check on its own git commands; it is
best effort for wrapped commands, so also turn on the exact git hooks once per clone (needs Node.js;
they also cover commits from other tools such as GitHub Desktop):

```powershell
git config core.hooksPath .githooks
```

Check the whole history at any time with `node .claude/hooks/secret-scan.js history`. A false
positive is fixed in the rules of `.claude/hooks/secret-scan.js` (with a test), not bypassed.

## The mod files

The repo holds only our own work, not AnarchyKenshi. Put your own copy of the mod (v0.133.1, the
version the findings are based on) at `KenshiMultiplayer 0.133.1/AnarchyKenshi/` in the repo folder
(gitignored). Check it against the SHA-256 values in `docs/findings/`; another version will differ.

## The toolchain

Windows 10 (1803+) or 11, x64. About 1.3 GB of disk space and a few minutes of downloading.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup-rea.ps1
```

Then restart Claude Code in the repo folder and approve the `rea` MCP server when it asks.

## What it does

Downloads pinned portable builds into `tools\rea\` (gitignored), each checked against its official SHA-256:

| Folder | What | Version |
|---|---|---|
| `node` | Node.js, used only to run REA | 24.22.3 |
| `jdk` | Eclipse Temurin JDK, used only by Ghidra | 25.0.5.1 |
| `ghidra` | Ghidra | 12.2 |
| `app` | [`rea-agents`](https://github.com/morluto/rea) MCP server + CLI (npm, local install) | 6.10.1 |

Then it writes `.mcp.json` (gitignored), which registers `rea` for this project only with your machine's paths and a Ghidra heap of half your RAM (at most 8G; set `GHIDRA_HEADLESS_MAXMEM`, e.g. `4G`, before running to choose another), and runs `rea doctor` to check Ghidra and Java. After changing the script, re-run it and restart Claude Code so the new `.mcp.json` is used.

It does **not** run installers, change PATH or system environment variables, install global npm packages, or touch global configs (it deliberately skips `rea setup`, which edits `~/.claude.json`, Claude Desktop, Codex, VS Code and Windsurf configs). Your own Node or Java installs are not used or changed. Re-running is safe: finished parts are skipped.

## Using it by hand

```powershell
$env:GHIDRA_INSTALL_DIR="$PWD\tools\rea\ghidra"; $env:JAVA_HOME="$PWD\tools\rea\jdk"
.\tools\rea\node\node.exe .\tools\rea\app\node_modules\rea-agents\scripts\rea.mjs doctor
.\tools\rea\ghidra\ghidraRun.bat   # Ghidra GUI
```

## Removal

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup-rea.ps1 -Uninstall
```

This deletes `tools\rea\` and `.mcp.json`. Outside the repo, once Ghidra has run, there may also be `%APPDATA%\ghidra` and `%LOCALAPPDATA%\ghidra` (settings and cache) and leftover session folders in `%TEMP%`. Claude Code remembers your approval of the project's MCP server in `~/.claude.json`.

## Upgrading a tool

Edit the URL and SHA-256 together in `scripts\setup-rea.ps1` (official checksum sources are listed there), delete that tool's folder in `tools\rea\`, and re-run the script.

# Setup: building the KenshiPure client plugin

Needs git, Windows 10/11 x64, about 2.5 GB free while setting up (1.7 GB afterwards). Nothing is
installed into Windows: the VC++ 2010 compiler is unpacked from Microsoft's Windows SDK 7.1 ISO into
`tools\build\` (gitignored), with KenshiLib 0.5.0 headers and address map, Boost 1.60 headers,
MinHook 1.3.4 sources and the iced-x86 1.22.0 decoder, all pinned by SHA-256 (KenshiLib's headers by
git commit). The build also needs Node (the portable one from `scripts\setup-rea.ps1`, or Node on PATH).

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup-build.ps1
powershell -ExecutionPolicy Bypass -File scripts\build-plugin.ps1
```

Install into a Kenshi with RE_Kenshi 0.3.5 (switches AnarchyKenshi off; `-Uninstall` switches back):

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install-client.ps1 -GameDir "<Kenshi folder>"
```

The plugin writes `<Kenshi>\mods\KenshiPure\KenshiPure.log`. Kenshi's own crash dumps land in the
game folder as `crashDump<version>_x64.dmp`. Tests of the install script:
`powershell -ExecutionPolicy Bypass -Command "Import-Module Pester -RequiredVersion 3.4.0; Invoke-Pester scripts\tests"`.
Removal: `scripts\setup-build.ps1 -Uninstall`.

## Live game probe (Claude reads the running game)

The plugin listens on `127.0.0.1:47800` (this PC only; port via `probe_port=N` in
`<Kenshi>\mods\KenshiPure\KenshiPure.cfg`, `0` = off). Register the MCP server per machine, then
restart Claude Code here and approve `kenshi-probe`:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup-probe-mcp.ps1
```

Tools: `kenshi_status`, `kenshi_characters`, `kenshi_nearby`, `kenshi_health`, `kenshi_inventory`,
`kenshi_find_items`, `kenshi_log_tail`; with `install-client.ps1 -ProbeWrite` also `kenshi_heal`,
`kenshi_teleport`, `kenshi_spawn_item`. Tests (no game needed): `node --test mcp/*.test.mjs`.
How it works: `docs/client.md`.

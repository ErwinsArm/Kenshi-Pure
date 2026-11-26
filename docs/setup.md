# Setup: REA reverse-engineering toolchain

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

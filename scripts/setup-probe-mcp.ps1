# Registers the "kenshi-probe" MCP server (mcp\kenshi-probe.mjs) in this repo's .mcp.json
# (gitignored, per machine), next to other servers such as "rea". Uses the portable Node from
# scripts\setup-rea.ps1 if present, else `node` from PATH. Re-running is safe.
#
#   powershell -ExecutionPolicy Bypass -File scripts\setup-probe-mcp.ps1
param([int]$Port = 47800)
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$McpJson = Join-Path $Repo ".mcp.json"
$server = Join-Path $Repo "mcp\kenshi-probe.mjs"
$node = Join-Path $Repo "tools\rea\node\node.exe"
if (-not (Test-Path -LiteralPath $node)) {
    $cmd = Get-Command node -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "No Node found. Run scripts\setup-rea.ps1 first (it installs a portable Node) or install Node." }
    $node = $cmd.Source
}

$servers = [ordered]@{}
if (Test-Path -LiteralPath $McpJson) {
    $old = Get-Content -Raw -LiteralPath $McpJson | ConvertFrom-Json
    if ($old.mcpServers) { foreach ($p in $old.mcpServers.PSObject.Properties) { $servers[$p.Name] = $p.Value } }
}
$servers["kenshi-probe"] = [ordered]@{
    command = $node
    args    = @($server)
    env     = [ordered]@{ KENSHI_PROBE_PORT = "$Port" }
}
$json = ([ordered]@{ mcpServers = $servers } | ConvertTo-Json -Depth 8)
[IO.File]::WriteAllText($McpJson, $json + "`n", (New-Object Text.UTF8Encoding $false))
"Wrote $McpJson (kenshi-probe, port $Port). Restart Claude Code in this folder and approve the server."

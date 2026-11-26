<#
.SYNOPSIS
  Sets up the REA reverse-engineering toolchain for this repo (Windows x64).

.DESCRIPTION
  Downloads pinned, SHA-256-verified portable builds into tools\rea\ (gitignored):
    node\    Node.js 24.22.3      (runs REA only)
    jdk\     Temurin JDK 25.0.5.1 (runs Ghidra only)
    ghidra\  Ghidra 12.2
    app\     rea-agents 6.10.1 (npm)
  Then writes .mcp.json at the repo root, registering the "rea" MCP server for this
  project with this machine's absolute paths.

  Nothing is installed system-wide: no installers, no PATH or system environment
  changes, no global npm packages, no edits to global Claude/Codex/VS Code configs.
  Re-running is safe: finished parts are skipped. Remove everything with -Uninstall.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\setup-rea.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\setup-rea.ps1 -Uninstall
#>
[CmdletBinding()]
param(
  # Where the toolchain goes. Default: <repo>\tools\rea
  [string]$Root,
  # Delete tools\rea and the generated .mcp.json.
  [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is very slow with the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Repo = Split-Path -Parent $PSScriptRoot
if (-not $Root) { $Root = Join-Path $Repo 'tools\rea' }
$McpJson = Join-Path $Repo '.mcp.json'

# Pinned versions. To upgrade, change URL and SHA-256 together, from the official source:
#   Node:   https://nodejs.org/dist/<ver>/SHASUMS256.txt
#   JDK:    https://api.adoptium.net/v3/assets/latest/25/hotspot?os=windows&architecture=x64&image_type=jdk
#   Ghidra: https://github.com/NationalSecurityAgency/ghidra/releases (SHA-256 in release notes)
$Parts = @(
  @{ Name = 'node';   Dir = 'node-v24.22.3-win-x64'
     Url  = 'https://nodejs.org/dist/v24.22.3/node-v24.22.3-win-x64.zip'
     Sha  = 'e612af99f626ab415ea225af6bce5c971feb448cb5cae250bb1c3fa50c5f7d9f' },
  @{ Name = 'jdk';    Dir = 'jdk-25*'
     Url  = 'https://github.com/adoptium/temurin25-binaries/releases/download/jdk-25.0.5.1%2B2/OpenJDK25U-jdk_x64_windows_hotspot_25.0.5.1_2.zip'
     Sha  = '849d380ab0a64d9098e7d3a2de28beb73841c3b255c8284f019ccd8748c5eb5b' },
  @{ Name = 'ghidra'; Dir = 'ghidra_12.2_PUBLIC'
     Url  = 'https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.2_build/ghidra_12.2_PUBLIC_20261016.zip'
     Sha  = '2c2cd776db3c1f6a097ba418922378ac3649cadd5e844a6f5f088ae628a7be67' }
)
$ReaVersion = '6.10.1'

if ($Uninstall) {
  if (Test-Path $Root)    { Remove-Item -Recurse -Force $Root;    "Removed $Root" }
  if (Test-Path $McpJson) { Remove-Item -Force $McpJson;          "Removed $McpJson" }
  "Possibly left outside the repo (created only once Ghidra runs): %APPDATA%\ghidra, %LOCALAPPDATA%\ghidra."
  return
}

if (-not [Environment]::Is64BitOperatingSystem) { throw 'Needs 64-bit Windows.' }
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
if (-not (Test-Path $tar)) { throw "Needs $tar (Windows 10 1803 or newer)." }

New-Item -ItemType Directory -Force $Root | Out-Null
$dl = Join-Path $Root '_download'

foreach ($p in $Parts) {
  $dest = Join-Path $Root $p.Name
  if (Test-Path $dest) { "$($p.Name): already present, skipped"; continue }
  New-Item -ItemType Directory -Force $dl | Out-Null
  $zip = Join-Path $dl "$($p.Name).zip"
  "$($p.Name): downloading $($p.Url)"
  Invoke-WebRequest -UseBasicParsing -Uri $p.Url -OutFile $zip
  $hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
  if ($hash -ne $p.Sha) { Remove-Item -Force $zip; throw "$($p.Name): SHA-256 mismatch (got $hash, expected $($p.Sha)). Not extracted." }
  "$($p.Name): checksum OK, extracting"
  $x = Join-Path $dl "$($p.Name)-x"
  New-Item -ItemType Directory -Force $x | Out-Null
  & $tar -xf $zip -C $x
  if ($LASTEXITCODE -ne 0) { throw "$($p.Name): extract failed" }
  $inner = Get-ChildItem -Directory $x -Filter $p.Dir | Select-Object -First 1
  if (-not $inner) { throw "$($p.Name): expected folder $($p.Dir) not in archive" }
  Move-Item $inner.FullName $dest
  Remove-Item -Recurse -Force $x, $zip
}
if (Test-Path $dl) { Remove-Item -Recurse -Force $dl }

$node = Join-Path $Root 'node\node.exe'
$npm  = Join-Path $Root 'node\npm.cmd'
$app  = Join-Path $Root 'app'
$rea  = Join-Path $app 'node_modules\rea-agents\scripts\rea.mjs'
$pkg  = Join-Path $app 'node_modules\rea-agents\package.json'
$have = if (Test-Path $pkg) { (Get-Content -Raw $pkg | ConvertFrom-Json).version } else { $null }
if ($have -ne $ReaVersion) {
  "rea-agents: installing $ReaVersion (local to $app)"
  $env:Path = "$(Join-Path $Root 'node');$env:Path"   # this process only
  & $npm install --prefix $app --cache (Join-Path $Root 'npm-cache') --no-fund --no-audit --no-update-notifier "rea-agents@$ReaVersion"
  if ($LASTEXITCODE -ne 0) { throw 'npm install failed' }
} else { "rea-agents: $ReaVersion already present, skipped" }

$config = [ordered]@{ mcpServers = [ordered]@{ rea = [ordered]@{
  command = $node
  args    = @($rea, 'mcp')
  env     = [ordered]@{
    GHIDRA_INSTALL_DIR = (Join-Path $Root 'ghidra')
    JAVA_HOME          = (Join-Path $Root 'jdk')
  }
} } }
$json = $config | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText($McpJson, $json + "`n", (New-Object Text.UTF8Encoding $false))
"Wrote $McpJson"

"Checking with rea doctor..."
$env:GHIDRA_INSTALL_DIR = Join-Path $Root 'ghidra'
$env:JAVA_HOME = Join-Path $Root 'jdk'
$doc = & $node $rea doctor --json 2>$null | Out-String
try {
  $checks = ($doc | ConvertFrom-Json).scope_checks | Where-Object { $_.name -like 'ghidra*' -or $_.name -eq 'node' }
  $checks | ForEach-Object { '  {0,-26} {1}' -f $_.name, $(if ($_.ok) { 'ok' } else { "FAIL: $($_.detail)" }) }
  if ($checks | Where-Object { -not $_.ok }) { throw 'Some checks failed (see above).' }
} catch { throw "rea doctor did not pass: $_" }

''
'Done. Restart Claude Code in this folder and approve the "rea" MCP server when asked.'

# Pure helpers for scripts/install-client.ps1: read and write Kenshi's line-based cfg files
# without changing anything but the one line asked for. Dot-source this file.

# A cfg file as lines plus its line ending and whether it ended with one.
function Read-CfgFile([string]$Path) {
    $text = [System.IO.File]::ReadAllText($Path)
    $eol = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $endsWithEol = $text.EndsWith("`n")
    $lines = @()
    if ($text.Length -gt 0) {
        $body = if ($endsWithEol) { $text.Substring(0, $text.Length - $eol.Length) } else { $text }
        $lines = @($body -split [regex]::Escape($eol), -1)
    }
    return [pscustomobject]@{ Lines = $lines; Eol = $eol; EndsWithEol = $endsWithEol }
}

function Write-CfgFile([string]$Path, $Cfg) {
    $text = ($Cfg.Lines -join $Cfg.Eol)
    if ($Cfg.EndsWithEol -and $Cfg.Lines.Count -gt 0) { $text += $Cfg.Eol }
    # ASCII without BOM, like the game's own files.
    [System.IO.File]::WriteAllText($Path, $text, [System.Text.Encoding]::ASCII)
}

# Index of the first line equal to $Line (ignoring surrounding spaces), or -1.
function Find-CfgLine([string[]]$Lines, [string]$Line) {
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i].Trim() -eq $Line) { return $i }
    }
    return -1
}

function Remove-CfgLineAt([string[]]$Lines, [int]$Index) {
    $out = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $Lines.Count; $i++) { if ($i -ne $Index) { $out.Add($Lines[$i]) } }
    return , $out.ToArray()
}

function Insert-CfgLineAt([string[]]$Lines, [int]$Index, [string]$Line) {
    if ($Index -lt 0 -or $Index -gt $Lines.Count) { $Index = $Lines.Count }
    $out = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($i -eq $Index) { $out.Add($Line) }
        $out.Add($Lines[$i])
    }
    if ($Index -eq $Lines.Count) { $out.Add($Line) }
    return , $out.ToArray()
}

# Copy $Path to $Path$Suffix once; never overwrite an existing backup.
function Backup-Once([string]$Path, [string]$Suffix) {
    $backup = "$Path$Suffix"
    if (-not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $Path -Destination $backup }
    return $backup
}

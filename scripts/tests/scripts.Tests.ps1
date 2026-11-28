# Pester 3.4: every PowerShell script of the repo parses. A script nothing runs in the build or the tests
# (run-game-test.ps1 needs the game) can otherwise stay broken after an edit or a merge.
$scripts = Join-Path $PSScriptRoot ".."

Describe "repo PowerShell scripts" {
    foreach ($f in @(Get-ChildItem -LiteralPath $scripts -Recurse -Filter *.ps1)) {
        It "parses: $($f.Name)" {
            $errors = $null
            [System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$null, [ref]$errors) | Out-Null
            ($errors | ForEach-Object { "line $($_.Extent.StartLineNumber): $($_.Message)" }) -join "; " | Should Be ""
        }
    }
}

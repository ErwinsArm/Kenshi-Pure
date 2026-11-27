# Pester 3.4 tests (ships with Windows 10) for scripts\install-client.ps1, run on a fake game folder:
#   powershell -ExecutionPolicy Bypass -Command "Import-Module Pester -RequiredVersion 3.4.0; Invoke-Pester scripts\tests"
$script = Join-Path $PSScriptRoot "..\install-client.ps1"

function New-FakeGame([string]$Root, [bool]$WithAnarchy = $true) {
    New-Item -ItemType Directory -Force (Join-Path $Root "data") | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $Root "mods") | Out-Null
    $plugins = "# Defines plugins to load`r`n`r`n# Define plugin folder`r`nPluginFolder=.\`r`n`r`n# Define plugins`r`n"
    if ($WithAnarchy) { $plugins += "Plugin=mods/AnarchyKenshi/AnarchyLoader`r`n" }
    $plugins += "Plugin=RE_Kenshi`r`nPlugin=RenderSystem_Direct3D11_x64`r`nPlugin=Plugin_ParticleUniverse_x64`r`nPlugin=Plugin_Terrain_x64`r`n"
    [IO.File]::WriteAllText((Join-Path $Root "Plugins_x64.cfg"), $plugins, [Text.Encoding]::ASCII)
    [IO.File]::WriteAllText((Join-Path $Root "Plugins_x64.cfg.before-anarchy"), "old backup", [Text.Encoding]::ASCII)
    [IO.File]::WriteAllText((Join-Path $Root "data\mods.cfg"), "AnarchyKenshi.mod`r`nMinimal UI.mod`r`nShops regenerate .mod`r`n", [Text.Encoding]::ASCII)
    $dll = Join-Path $Root "fake\KenshiPure.dll"
    New-Item -ItemType Directory -Force (Split-Path $dll) | Out-Null
    [IO.File]::WriteAllBytes($dll, [byte[]](1, 2, 3))
    return $dll
}

function Get-Bytes([string]$Path) { return [Convert]::ToBase64String([IO.File]::ReadAllBytes($Path)) }

Describe "install-client.ps1" {
    It "switches AnarchyKenshi off and KenshiPure on, and back, byte for byte" {
        $g = Join-Path $TestDrive "g1"
        $dll = New-FakeGame $g
        $plugins0 = Get-Bytes "$g\Plugins_x64.cfg"
        $mods0 = Get-Bytes "$g\data\mods.cfg"

        & $script -GameDir $g -DllPath $dll -NoGameCheck | Out-Null
        $p = [IO.File]::ReadAllText("$g\Plugins_x64.cfg")
        $p | Should Not Match "AnarchyLoader"
        $p | Should Match "Plugin=RE_Kenshi`r`nPlugin=RenderSystem_Direct3D11_x64`r`n"
        [IO.File]::ReadAllText("$g\data\mods.cfg") | Should Be "AnarchyKenshi.mod`r`nMinimal UI.mod`r`nShops regenerate .mod`r`nKenshiPure.mod`r`n"
        Test-Path "$g\mods\KenshiPure\KenshiPure.dll" | Should Be $true
        Test-Path "$g\mods\KenshiPure\RE_Kenshi.json" | Should Be $true
        (Get-Item "$g\mods\KenshiPure\KenshiPure.mod").Length | Should Be 46
        Get-Bytes "$g\Plugins_x64.cfg.kenshipure-backup" | Should Be $plugins0
        Get-Bytes "$g\data\mods.cfg.kenshipure-backup" | Should Be $mods0
        [IO.File]::ReadAllText("$g\Plugins_x64.cfg.before-anarchy") | Should Be "old backup"

        # Installing twice changes nothing more and still remembers AnarchyKenshi was on.
        & $script -GameDir $g -DllPath $dll -NoGameCheck | Out-Null
        ([IO.File]::ReadAllText("$g\data\mods.cfg") -split "KenshiPure.mod").Count | Should Be 2

        & $script -GameDir $g -Uninstall -NoGameCheck | Out-Null
        Get-Bytes "$g\Plugins_x64.cfg" | Should Be $plugins0
        Get-Bytes "$g\data\mods.cfg" | Should Be $mods0
        Test-Path "$g\mods\KenshiPure" | Should Be $false
        [IO.File]::ReadAllText("$g\Plugins_x64.cfg.before-anarchy") | Should Be "old backup"
    }

    It "never adds AnarchyKenshi back if it was not on" {
        $g = Join-Path $TestDrive "g2"
        $dll = New-FakeGame $g $false
        $plugins0 = Get-Bytes "$g\Plugins_x64.cfg"
        & $script -GameDir $g -DllPath $dll -NoGameCheck | Out-Null
        & $script -GameDir $g -Uninstall -NoGameCheck | Out-Null
        Get-Bytes "$g\Plugins_x64.cfg" | Should Be $plugins0
    }

    It "keeps the first backup and the logs" {
        $g = Join-Path $TestDrive "g3"
        $dll = New-FakeGame $g
        & $script -GameDir $g -DllPath $dll -NoGameCheck | Out-Null
        $backup = Get-Bytes "$g\Plugins_x64.cfg.kenshipure-backup"
        Set-Content "$g\mods\KenshiPure\KenshiPure.log" "a run"
        & $script -GameDir $g -Uninstall -NoGameCheck | Out-Null
        & $script -GameDir $g -DllPath $dll -NoGameCheck | Out-Null
        Get-Bytes "$g\Plugins_x64.cfg.kenshipure-backup" | Should Be $backup
        Test-Path "$g\mods\KenshiPure\KenshiPure.log" | Should Be $true
    }

    It "refuses without RE_Kenshi and changes nothing" {
        $g = Join-Path $TestDrive "g4"
        $dll = New-FakeGame $g
        $txt = [IO.File]::ReadAllText("$g\Plugins_x64.cfg") -replace "Plugin=RE_Kenshi`r`n", ""
        [IO.File]::WriteAllText("$g\Plugins_x64.cfg", $txt, [Text.Encoding]::ASCII)
        { & $script -GameDir $g -DllPath $dll -NoGameCheck } | Should Throw
        [IO.File]::ReadAllText("$g\Plugins_x64.cfg") | Should Be $txt
        Test-Path "$g\mods\KenshiPure" | Should Be $false
    }

    It "refuses a folder that is not a Kenshi install" {
        { & $script -GameDir (Join-Path $TestDrive "nothing") -NoGameCheck } | Should Throw
    }
}

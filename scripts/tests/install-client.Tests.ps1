# Pester 3.4 tests (ships with Windows 10) for scripts\install-client.ps1, run on a fake game folder:
#   powershell -ExecutionPolicy Bypass -Command "Import-Module Pester -RequiredVersion 3.4.0; Invoke-Pester scripts\tests"
$script = Join-Path $PSScriptRoot "..\install-client.ps1"

function New-FakeGame([string]$Root, [bool]$WithAnarchy = $true, [bool]$WithReKenshi = $true) {
    New-Item -ItemType Directory -Force (Join-Path $Root "data") | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $Root "mods") | Out-Null
    $plugins = "# Defines plugins to load`r`n`r`n# Define plugin folder`r`nPluginFolder=.\`r`n`r`n# Define plugins`r`n"
    if ($WithAnarchy) { $plugins += "Plugin=mods/AnarchyKenshi/AnarchyLoader`r`n" }
    if ($WithReKenshi) { $plugins += "Plugin=RE_Kenshi`r`n" }
    $plugins += "Plugin=RenderSystem_Direct3D11_x64`r`nPlugin=Plugin_ParticleUniverse_x64`r`nPlugin=Plugin_Terrain_x64`r`n"
    [IO.File]::WriteAllText((Join-Path $Root "Plugins_x64.cfg"), $plugins, [Text.Encoding]::ASCII)
    [IO.File]::WriteAllText((Join-Path $Root "Plugins_x64.cfg.before-anarchy"), "old backup", [Text.Encoding]::ASCII)
    [IO.File]::WriteAllText((Join-Path $Root "data\mods.cfg"), "AnarchyKenshi.mod`r`nMinimal UI.mod`r`nShops regenerate .mod`r`n", [Text.Encoding]::ASCII)
    $build = Join-Path $Root "fake-build"
    New-Item -ItemType Directory -Force $build | Out-Null
    foreach ($d in @("KenshiPureLoader.dll", "KenshiPure.dll", "KenshiPureCore.dll")) { [IO.File]::WriteAllBytes((Join-Path $build $d), [byte[]](1, 2, 3)) }
    return $build
}

function Get-Bytes([string]$Path) { return [Convert]::ToBase64String([IO.File]::ReadAllBytes($Path)) }

Describe "install-client.ps1" {
    It "switches AnarchyKenshi off and KenshiPure on after RE_Kenshi, and back, byte for byte" {
        $g = Join-Path $TestDrive "g1"
        $build = New-FakeGame $g
        $plugins0 = Get-Bytes "$g\Plugins_x64.cfg"
        $mods0 = Get-Bytes "$g\data\mods.cfg"

        & $script -GameDir $g -BuildDir $build -CoreDir $build -NoGameCheck | Out-Null
        $p = [IO.File]::ReadAllText("$g\Plugins_x64.cfg")
        $p | Should Not Match "AnarchyLoader"
        $p | Should Match "Plugin=RE_Kenshi`r`nPlugin=mods/KenshiPure/KenshiPureLoader`r`nPlugin=RenderSystem_Direct3D11_x64`r`n"
        Get-Bytes "$g\data\mods.cfg" | Should Be $mods0
        Test-Path "$g\mods\KenshiPure\KenshiPure.dll" | Should Be $true
        Test-Path "$g\mods\KenshiPure\KenshiPureLoader.dll" | Should Be $true
        Test-Path "$g\mods\KenshiPure\KenshiPureCore.dll" | Should Be $true
        [IO.File]::ReadAllText("$g\mods\KenshiPure\KenshiPure.cfg") | Should Be "host=127.0.0.1`r`nport=27850`r`nname=player`r`n"
        Test-Path "$g\mods\KenshiPure\RE_Kenshi.json" | Should Be $false
        Get-Bytes "$g\Plugins_x64.cfg.kenshipure-backup" | Should Be $plugins0
        [IO.File]::ReadAllText("$g\Plugins_x64.cfg.before-anarchy") | Should Be "old backup"

        # Installing twice changes nothing more and still remembers AnarchyKenshi was on.
        & $script -GameDir $g -BuildDir $build -CoreDir $build -NoGameCheck | Out-Null
        ([IO.File]::ReadAllText("$g\Plugins_x64.cfg") -split "KenshiPureLoader").Count | Should Be 2

        & $script -GameDir $g -Uninstall -NoGameCheck | Out-Null
        Get-Bytes "$g\Plugins_x64.cfg" | Should Be $plugins0
        Get-Bytes "$g\data\mods.cfg" | Should Be $mods0
        Test-Path "$g\mods\KenshiPure" | Should Be $false
        [IO.File]::ReadAllText("$g\Plugins_x64.cfg.before-anarchy") | Should Be "old backup"
    }

    It "works without RE_Kenshi: the line goes before the first plugin" {
        $g = Join-Path $TestDrive "g4"
        $build = New-FakeGame $g $false $false
        $plugins0 = Get-Bytes "$g\Plugins_x64.cfg"
        & $script -GameDir $g -BuildDir $build -CoreDir $build -NoGameCheck | Out-Null
        [IO.File]::ReadAllText("$g\Plugins_x64.cfg") | Should Match "# Define plugins`r`nPlugin=mods/KenshiPure/KenshiPureLoader`r`nPlugin=RenderSystem_Direct3D11_x64`r`n"
        & $script -GameDir $g -Uninstall -NoGameCheck | Out-Null
        Get-Bytes "$g\Plugins_x64.cfg" | Should Be $plugins0
    }

    It "removes what an older (RE_Kenshi-started) install left" {
        $g = Join-Path $TestDrive "g5"
        $build = New-FakeGame $g $false
        $mods0 = Get-Bytes "$g\data\mods.cfg"
        [IO.File]::AppendAllText("$g\data\mods.cfg", "KenshiPure.mod`r`n", [Text.Encoding]::ASCII)
        New-Item -ItemType Directory -Force "$g\mods\KenshiPure" | Out-Null
        Set-Content "$g\mods\KenshiPure\RE_Kenshi.json" "{}"
        Set-Content "$g\mods\KenshiPure\KenshiPure.mod" "x"
        & $script -GameDir $g -BuildDir $build -CoreDir $build -NoGameCheck | Out-Null
        Get-Bytes "$g\data\mods.cfg" | Should Be $mods0
        Test-Path "$g\mods\KenshiPure\RE_Kenshi.json" | Should Be $false
        Test-Path "$g\mods\KenshiPure\KenshiPure.mod" | Should Be $false
    }

    It "never adds AnarchyKenshi back if it was not on" {
        $g = Join-Path $TestDrive "g2"
        $build = New-FakeGame $g $false
        $plugins0 = Get-Bytes "$g\Plugins_x64.cfg"
        & $script -GameDir $g -BuildDir $build -CoreDir $build -NoGameCheck | Out-Null
        & $script -GameDir $g -Uninstall -NoGameCheck | Out-Null
        Get-Bytes "$g\Plugins_x64.cfg" | Should Be $plugins0
    }

    It "keeps the first backup and the logs" {
        $g = Join-Path $TestDrive "g3"
        $build = New-FakeGame $g
        & $script -GameDir $g -BuildDir $build -CoreDir $build -NoGameCheck | Out-Null
        $backup = Get-Bytes "$g\Plugins_x64.cfg.kenshipure-backup"
        Set-Content "$g\mods\KenshiPure\KenshiPure.log" "a run"
        & $script -GameDir $g -Uninstall -NoGameCheck | Out-Null
        & $script -GameDir $g -BuildDir $build -CoreDir $build -NoGameCheck | Out-Null
        Get-Bytes "$g\Plugins_x64.cfg.kenshipure-backup" | Should Be $backup
        Test-Path "$g\mods\KenshiPure\KenshiPure.log" | Should Be $true
    }

    It "keeps the user's network lines and switches probe_write with -ProbeWrite" {
        $g = Join-Path $TestDrive "g6"
        $build = New-FakeGame $g
        New-Item -ItemType Directory -Force "$g\mods\KenshiPure" | Out-Null
        [IO.File]::WriteAllText("$g\mods\KenshiPure\KenshiPure.cfg", "host=100.64.0.7`r`nname=Gecko`r`nprobe_write=1`r`n", [Text.Encoding]::ASCII)
        & $script -GameDir $g -BuildDir $build -CoreDir $build -NoGameCheck | Out-Null
        [IO.File]::ReadAllText("$g\mods\KenshiPure\KenshiPure.cfg") | Should Be "host=100.64.0.7`r`nname=Gecko`r`nport=27850`r`n"
        & $script -GameDir $g -BuildDir $build -CoreDir $build -ProbeWrite -NoGameCheck | Out-Null
        [IO.File]::ReadAllText("$g\mods\KenshiPure\KenshiPure.cfg") | Should Be "host=100.64.0.7`r`nname=Gecko`r`nport=27850`r`nprobe_write=1`r`n"
        & $script -GameDir $g -Uninstall -NoGameCheck | Out-Null
        Test-Path "$g\mods\KenshiPure\KenshiPureCore.dll" | Should Be $false
    }

    It "removes the stand-in test settings, which would turn the other players' bodies off" {
        $g = Join-Path $TestDrive "g11"
        $build = New-FakeGame $g
        New-Item -ItemType Directory -Force "$g\mods\KenshiPure" | Out-Null
        [IO.File]::WriteAllText("$g\mods\KenshiPure\KenshiPure.cfg", "host=100.64.0.7`r`nstandin=cycle`r`n standin_snap = 200`r`nremote_range=3000`r`n#standin=cycle`r`n", [Text.Encoding]::ASCII)
        & $script -GameDir $g -BuildDir $build -CoreDir $build -NoGameCheck | Out-Null
        [IO.File]::ReadAllText("$g\mods\KenshiPure\KenshiPure.cfg") | Should Be "host=100.64.0.7`r`nremote_range=3000`r`n#standin=cycle`r`nport=27850`r`nname=player`r`n"
    }

    It "refuses to install without the core DLL" {
        $g = Join-Path $TestDrive "g7"
        $build = New-FakeGame $g
        Remove-Item "$build\KenshiPureCore.dll"
        { & $script -GameDir $g -BuildDir $build -CoreDir $build -NoGameCheck } | Should Throw
    }

    It "refuses a folder that is not a Kenshi install" {
        { & $script -GameDir (Join-Path $TestDrive "nothing") -NoGameCheck } | Should Throw
    }

    It "sets the server address, port and player name it is given, in place" {
        $g = Join-Path $TestDrive "g8"
        $build = New-FakeGame $g
        New-Item -ItemType Directory -Force "$g\mods\KenshiPure" | Out-Null
        [IO.File]::WriteAllText("$g\mods\KenshiPure\KenshiPure.cfg", "# mine`r`nhost=127.0.0.1`r`nname=player`r`nremote_range=3000`r`n", [Text.Encoding]::ASCII)
        & $script -GameDir $g -BuildDir $build -CoreDir $build -ServerHost 100.64.0.9 -Port 27900 -PlayerName "Nei Two" -NoGameCheck | Out-Null
        [IO.File]::ReadAllText("$g\mods\KenshiPure\KenshiPure.cfg") | Should Be "# mine`r`nhost=100.64.0.9`r`nname=Nei Two`r`nremote_range=3000`r`nport=27900`r`n"
        { & $script -GameDir $g -BuildDir $build -CoreDir $build -PlayerName "a=b" -NoGameCheck } | Should Throw
        { & $script -GameDir $g -BuildDir $build -CoreDir $build -ServerHost "bad host" -NoGameCheck } | Should Throw
        { & $script -GameDir $g -BuildDir $build -CoreDir $build -PlayerName ("x" * 33) -NoGameCheck } | Should Throw
    }

    It "refuses -Cheats when there is no cheat DLL (the multiplayer package)" {
        $g = Join-Path $TestDrive "g9"
        $build = New-FakeGame $g
        { & $script -GameDir $g -BuildDir $build -CoreDir $build -Cheats -NoGameCheck } | Should Throw
        Test-Path "$g\mods\KenshiPure" | Should Be $false
    }

    It "runs from a package folder and installs only Kenshi builds it knows" {
        $g = Join-Path $TestDrive "g10"
        $build = New-FakeGame $g $true $false
        $pkg = Join-Path $TestDrive "pkg"
        New-Item -ItemType Directory -Force "$pkg\lib" | Out-Null
        Copy-Item $script "$pkg\install-client.ps1"
        Copy-Item (Join-Path $PSScriptRoot "..\lib\CfgEdit.ps1") "$pkg\lib\CfgEdit.ps1"
        foreach ($d in @("KenshiPureLoader.dll", "KenshiPure.dll", "KenshiPureCore.dll")) { Copy-Item "$build\$d" "$pkg\$d" }
        [IO.File]::WriteAllBytes("$g\kenshi_x64.exe", [byte[]](7, 7, 7))
        [IO.File]::WriteAllText("$pkg\builds.txt", "#build 1.0.99 " + ("0" * 64) + " not this one`r`n", [Text.Encoding]::ASCII)
        $plugins0 = Get-Bytes "$g\Plugins_x64.cfg"
        { & "$pkg\install-client.ps1" -GameDir $g -ServerHost 100.64.0.9 -PlayerName Friend } | Should Throw
        Get-Bytes "$g\Plugins_x64.cfg" | Should Be $plugins0
        Test-Path "$g\mods\KenshiPure" | Should Be $false

        $sha = (Get-FileHash -Algorithm SHA256 "$g\kenshi_x64.exe").Hash.ToLowerInvariant()
        [IO.File]::WriteAllText("$pkg\builds.txt", "#build 1.0.68 $sha Steam 1.0.68`r`n", [Text.Encoding]::ASCII)
        & "$pkg\install-client.ps1" -GameDir $g -ServerHost 100.64.0.9 -PlayerName Friend | Out-Null
        Test-Path "$g\mods\KenshiPure\KenshiPureCore.dll" | Should Be $true
        Test-Path "$g\mods\KenshiPure\KenshiPureCheats.dll" | Should Be $false
        [IO.File]::ReadAllText("$g\mods\KenshiPure\KenshiPure.cfg") | Should Be "host=100.64.0.9`r`nport=27850`r`nname=Friend`r`n"
        [IO.File]::ReadAllText("$g\Plugins_x64.cfg") | Should Not Match "AnarchyLoader"
        & "$pkg\install-client.ps1" -GameDir $g -Uninstall | Out-Null
        Get-Bytes "$g\Plugins_x64.cfg" | Should Be $plugins0
    }
}

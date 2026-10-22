param(
    [string]$ReferenceInstall = 'C:\Program Files\Firestorm-Releasex64'
)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskBuild = Join-Path $taskRoot 'build-vc'
$taskTarget = Join-Path $taskBuild 'newview/Release'
$taskExe = Join-Path $taskTarget 'firestorm-bin.exe'
$taskObserver = Join-Path $PSScriptRoot 'build/v03/Release/firestorm_scene_observer.addon64'
if (!(Test-Path -LiteralPath $taskExe) -or !(Test-Path -LiteralPath $taskObserver)) {
    throw 'Build the modified viewer and real v0.3 observer successfully before preparing a run.'
}
if (Get-Process -Name 'firestorm-bin' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $taskExe }) {
    throw 'Close the separate test viewer before preparing its directory.'
}
$taskReference = (Resolve-Path -LiteralPath $ReferenceInstall).Path
if ($taskTarget -eq $taskReference) { throw 'Test and reference directories must differ.' }
if (Test-Path -LiteralPath (Join-Path $taskTarget 'ReShade.ini')) {
    throw 'Test injection is already prepared; preserve its existing preset/settings before updating it.'
}
foreach ($taskName in @('opengl32.dll', 'dlss5-feed.addon64', 'renodx-dlss5.addon64', 'nvngx_dlss.dll', 'nvngx_dlssnr.dll', 'ReShade.ini', 'ReShadePreset.ini')) {
    $taskSource = Join-Path $taskReference $taskName
    if (!(Test-Path -LiteralPath $taskSource)) { throw "Reference component missing: $taskName" }
}
# The executable-only CMake target stages dependency assets, but leaves the
# viewer's core assets to the installer manifest. Merge source assets without
# removing the third-party dictionaries, fonts, and plugins already staged.
foreach ($taskAsset in @('app_settings', 'character', 'skins', 'fonts', 'fs_resources')) {
    $taskAssetSource = Join-Path $taskRoot "indra/newview/$taskAsset"
    $taskAssetTarget = Join-Path $taskTarget $taskAsset
    & robocopy $taskAssetSource $taskAssetTarget /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) { throw "Asset staging failed: $taskAsset ($LASTEXITCODE)" }
}
foreach ($taskName in @('opengl32.dll', 'dlss5-feed.addon64', 'renodx-dlss5.addon64', 'nvngx_dlss.dll', 'nvngx_dlssnr.dll', 'ReShade.ini', 'ReShadePreset.ini')) {
    Copy-Item -LiteralPath (Join-Path $taskReference $taskName) -Destination (Join-Path $taskTarget $taskName)
}
Copy-Item -LiteralPath $taskObserver -Destination (Join-Path $taskTarget 'firestorm_scene_observer.addon64')
# CMake could not stage this CRT DLL from System32; the working viewer supplies it.
if (!(Test-Path -LiteralPath (Join-Path $taskTarget 'vcruntime140_1.dll'))) {
    Copy-Item -LiteralPath (Join-Path $taskReference 'vcruntime140_1.dll') -Destination $taskTarget
}
Copy-Item -LiteralPath (Join-Path $taskReference 'reshade-shaders') -Destination $taskTarget -Recurse
foreach ($taskName in @('ReShade.ini', 'ReShadePreset.ini')) {
    $taskPath = Join-Path $taskTarget $taskName
    $taskText = [IO.File]::ReadAllText($taskPath)
    $taskText = [regex]::Replace($taskText, [regex]::Escape($taskReference), { $taskTarget }, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    [IO.File]::WriteAllText($taskPath, $taskText)
}

# A private per-process APPDATA/LOCALAPPDATA also isolates per-account data.
$taskRoaming = Join-Path $taskBuild 'scene-signal-profile/roaming'
$taskLocal = Join-Path $taskBuild 'scene-signal-profile/local'
$taskSettingsDir = Join-Path $taskRoaming 'Firestorm_x64/user_settings'
New-Item -ItemType Directory -Force -Path $taskSettingsDir,$taskLocal | Out-Null
$taskProfileSettings = Join-Path $taskSettingsDir 'settings.xml'
if (!(Test-Path -LiteralPath $taskProfileSettings)) {
    # Fresh settings avoid carrying absolute cache/session paths from the normal profile.
    [IO.File]::WriteAllText($taskProfileSettings, '<llsd><map><key>RenderShaderCacheEnabled</key><map><key>Type</key><string>Boolean</string><key>Value</key><boolean>0</boolean></map></map></llsd>')
}
$taskLauncher = Join-Path $taskBuild 'Launch-SceneSignal.cmd'
$taskLaunchText = @'
@echo off
setlocal
set "APPDATA=%~dp0scene-signal-profile\roaming"
set "LOCALAPPDATA=%~dp0scene-signal-profile\local"
start "" /D "%~dp0newview\Release" "%~dp0newview\Release\firestorm-bin.exe"
endlocal
'@
[IO.File]::WriteAllText($taskLauncher, $taskLaunchText.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.Encoding]::ASCII)
Write-Output "Separate test launcher: $taskLauncher"
Write-Output 'The test viewer uses fresh private settings/cache. Log in manually; the normal installation/profile are unchanged.'

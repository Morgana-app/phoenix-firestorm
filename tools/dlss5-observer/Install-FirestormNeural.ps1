param(
    [Parameter(Mandatory=$true)][string]$ViewerDirectory,
    [Parameter(Mandatory=$true)][string]$HelperBuildManifest,
    [Parameter(Mandatory=$true)][string]$HelperHostPath
)
$ErrorActionPreference = 'Stop'
$taskTarget = (Resolve-Path -LiteralPath $ViewerDirectory).Path
if ((Get-Process -Name 'firestorm-bin','dlss5-feed-host64' -ErrorAction SilentlyContinue)) { throw 'Close Firestorm and its helper before installing.' }
$taskManifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
if ($taskManifest.Version -ne '0.9-helper-preview' -or $taskManifest.Mode -ne 'HelperRestart') { throw 'Expected the helper-restart package.' }
foreach ($taskEntry in $taskManifest.Files) {
    $taskSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $taskEntry.Path))
    if (!$taskSource.StartsWith($PSScriptRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid package manifest path.' }
    if ((Get-FileHash -LiteralPath $taskSource).Hash -ne $taskEntry.SHA256) { throw "Package hash mismatch: $($taskEntry.Path)" }
}
$taskBuild = Get-Content -LiteralPath $HelperBuildManifest -Raw | ConvertFrom-Json
if ($taskBuild.Version -ne $taskManifest.HelperClientVersion -or $taskBuild.BaseCommit -ne '3c09eab') { throw 'Supply a manifest from Build-FeederHelper.ps1 for Feeder 1.19.0-fsresize1.' }
$taskClientPath = $taskBuild.Binary
if (![IO.Path]::IsPathRooted($taskClientPath)) { $taskClientPath = Join-Path (Split-Path ([IO.Path]::GetFullPath($HelperBuildManifest))) $taskClientPath }
$taskClient = (Resolve-Path -LiteralPath $taskClientPath).Path
if ((Get-FileHash -LiteralPath $taskClient).Hash -ne $taskBuild.SHA256) { throw 'Local helper client does not match its build manifest.' }
foreach ($taskInput in $taskBuild.Inputs) {
    if ((Get-FileHash -LiteralPath $taskInput.Path).Hash -ne $taskInput.SHA256) { throw "Helper input changed; rebuild it: $($taskInput.Path)" }
}
$taskHost = (Resolve-Path -LiteralPath $HelperHostPath).Path
if ((Get-FileHash -LiteralPath $taskHost).Hash -ne $taskManifest.HostSHA256) { throw 'Supply the unchanged official Feeder v1.19.0 host64 executable.' }
$taskExe = Join-Path $taskTarget 'firestorm-bin.exe'
if (!(Test-Path -LiteralPath $taskExe)) { throw 'Select the folder containing the patched firestorm-bin.exe.' }
$taskBytes = [IO.File]::ReadAllBytes($taskExe)
if (![Text.Encoding]::ASCII.GetString($taskBytes).Contains('FirestormSceneReadyV1') -or
    ![Text.Encoding]::Unicode.GetString($taskBytes).Contains('firestorm_scene_observer.addon64')) {
    throw 'Viewer scene-signal patch not detected. Stock Firestorm is not supported.'
}
foreach ($taskName in @('opengl32.dll','dlss5-feed.addon64','renodx-dlss5.addon64','nvngx_dlss.dll','nvngx_dlssnr.dll','ReShade.ini','ReShadePreset.ini','dlss5-feed.cfg')) {
    if (!(Test-Path -LiteralPath (Join-Path $taskTarget $taskName))) { throw "Existing RHI/OpenGL baseline is missing $taskName." }
}
$taskIni = [IO.File]::ReadAllText((Join-Path $taskTarget 'ReShade.ini'))
if ($taskIni -notmatch '(?m)^PreprocessorDefinitions=[^\r\n]*DLSS5_MV_PROVIDER=3(?:,|\r?$)') { throw 'Configure motion provider DLSS5_MV_PROVIDER=3 first.' }
$taskControls = Join-Path $taskTarget 'firestorm-neural'
$taskRuns = Join-Path $taskControls 'runs'
if (Test-Path -LiteralPath $taskRuns) {
    if ((Get-ChildItem -LiteralPath $taskRuns -Recurse -Force | Where-Object { $_.Name -in @('running.lock','held-helper-session','held-copy-settings') })) {
        throw 'Recover held files or an interrupted launcher session before installing.'
    }
}
$taskOwner = Join-Path $taskTarget 'host64/firestorm-helper-test.txt'
if ((Test-Path -LiteralPath (Join-Path $taskTarget 'host64')) -and !(Test-Path -LiteralPath $taskOwner)) { throw 'An existing host64 folder is not owned by this integration; choose a separate prepared viewer folder.' }
New-Item -ItemType Directory -Force -Path $taskControls,(Join-Path $taskControls 'dependencies') | Out-Null
foreach ($taskName in @('Run-HelperNeural.ps1','Run-LifecycleControl.ps1','Preset.ps1','FS_P1Identity.fx','firestorm_scene_observer.addon64')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $taskName) -Destination $taskControls -Force
}
Copy-Item -LiteralPath $taskClient -Destination (Join-Path $taskControls 'dependencies/dlss5-feed-helper.addon64') -Force
Copy-Item -LiteralPath $taskHost -Destination (Join-Path $taskControls 'dependencies/dlss5-feed-host64.exe') -Force
$taskObserver = Join-Path $taskTarget 'firestorm_scene_observer.addon64'
# Existing baseline observer stays intact; the helper launcher swaps it temporarily.
$taskCreatedObserver = !(Test-Path -LiteralPath $taskObserver)
if ($taskCreatedObserver) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'firestorm_scene_observer.addon64') -Destination $taskObserver }
$taskPriorComponents = Join-Path $taskControls 'helper-components.json'
if (Test-Path -LiteralPath $taskPriorComponents) {
    $taskPrior = Get-Content -LiteralPath $taskPriorComponents -Raw | ConvertFrom-Json
    if ($taskPrior.CreatedBaselineObserver) { $taskCreatedObserver = $true }
}
$taskObserverHash = (Get-FileHash -LiteralPath (Join-Path $taskControls 'firestorm_scene_observer.addon64')).Hash
[pscustomobject]@{Version=$taskBuild.Version;SHA256=$taskBuild.SHA256;HostSHA256=$taskManifest.HostSHA256;ObserverSHA256=$taskObserverHash;CreatedBaselineObserver=$taskCreatedObserver;InstalledUTC=[DateTime]::UtcNow.ToString('o')} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskControls 'helper-components.json')
$taskCommand = '@echo off' + "`r`n" + 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0firestorm-neural\Run-HelperNeural.ps1" -ViewerDirectory "%~dp0."' + "`r`npause`r`n"
[IO.File]::WriteAllText((Join-Path $taskTarget 'Launch-FirestormNeural.cmd'), $taskCommand)
foreach ($taskCheck in @(
    @{Path=(Join-Path $taskControls 'dependencies/dlss5-feed-helper.addon64');Hash=$taskBuild.SHA256},
    @{Path=(Join-Path $taskControls 'dependencies/dlss5-feed-host64.exe');Hash=$taskManifest.HostSHA256}
)) {
    if ((Get-FileHash -LiteralPath $taskCheck.Path).Hash -ne $taskCheck.Hash) { throw 'Installed helper component hash mismatch.' }
}
Write-Output "Installed. Use $(Join-Path $taskTarget 'Launch-FirestormNeural.cmd')."
Write-Output 'Resizing pauses for about four seconds; neural output returns after roughly eight seconds. Keep the console open until normal exit restores the baseline.'

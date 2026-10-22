param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('ObserverAbsent', 'ObservedDisconnect', 'ImageCopy', 'IdentityEffects', 'WorldIdentity', 'InputAudit', 'EarlyNeural')]
    [string]$Mode,
    [switch]$PrepareOnly,
    [switch]$HelperResizeTest,
    [switch]$HelperSession,
    [string]$ViewerDirectory,
    [string]$ProfileDirectory,
    [string]$EvidenceDirectory
)
$ErrorActionPreference = 'Stop'
if (($HelperResizeTest -or $HelperSession) -and $Mode -ne 'EarlyNeural') { throw 'Helper mode requires EarlyNeural.' }
. (Join-Path $PSScriptRoot 'Preset.ps1')
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskBuild = Join-Path $taskRoot 'build-vc'
$taskTarget = Join-Path $taskBuild 'newview/Release'
if ($ViewerDirectory) { $taskTarget = (Resolve-Path -LiteralPath $ViewerDirectory).Path }
$taskExe = Join-Path $taskTarget 'firestorm-bin.exe'
$taskObserver = Join-Path $taskTarget 'firestorm_scene_observer.addon64'
$taskProfile = Join-Path $taskBuild 'scene-signal-profile'
if ($ViewerDirectory) { $taskProfile = $null }
if ($ProfileDirectory) { $taskProfile = [IO.Path]::GetFullPath($ProfileDirectory) }
$taskControls = Join-Path $taskBuild 'scene-signal-controls'
if ($ViewerDirectory) { $taskControls = Join-Path $taskTarget 'firestorm-neural/runs' }
if ($EvidenceDirectory) { $taskControls = [IO.Path]::GetFullPath($EvidenceDirectory) }
$taskHeld = Join-Path $taskControls 'held-observer/firestorm_scene_observer.addon64'
$taskLockPath = Join-Path $taskControls 'running.lock'
$taskCopyBackup = Join-Path $taskControls 'held-copy-settings'
if (!(Test-Path -LiteralPath $taskExe) -or !(Test-Path -LiteralPath $taskObserver)) {
    throw 'Prepared viewer and observer must both be present. If a prior control was interrupted, restore its held observer first.'
}
# Conservative: do not move an add-on while any test-named viewer may use it.
if (Get-Process -Name 'firestorm-bin' -ErrorAction SilentlyContinue) {
    throw 'Close the test viewer before starting this control.'
}
$taskIniText = [IO.File]::ReadAllText((Join-Path $taskTarget 'ReShade.ini'))
if ($Mode -ne 'EarlyNeural' -and $taskIniText -notmatch '(?m)^NRStyle=0\r?$') { throw 'This control requires the prepared style-0 baseline.' }
New-Item -ItemType Directory -Force -Path $taskControls,(Split-Path $taskHeld) | Out-Null
if (Test-Path -LiteralPath $taskHeld) { throw 'A held observer already exists; preserve and restore it before starting.' }
if (Test-Path -LiteralPath $taskCopyBackup) { throw 'Copy-test settings are held from a previous run. Restore those files before starting.' }
# CreateNew prevents two control launchers from manipulating the same directory.
$taskLock = [IO.File]::Open($taskLockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
$taskMoved = $false
$taskStarted = $false
$taskCopySettings = $false
$taskObserverHash = (Get-FileHash -LiteralPath $taskObserver).Hash
try {
    if ($Mode -eq 'ObserverAbsent') {
        Move-Item -LiteralPath $taskObserver -Destination $taskHeld
        $taskMoved = $true
        Write-Output 'Observer-absent control: log in, view the world for 15 seconds, then quit normally.'
    } elseif ($Mode -eq 'ObservedDisconnect') {
        Write-Output 'Observed disconnect control: log in and wait 15 seconds.'
        Write-Output 'Choose Developer > Force an Error > Force Disconnect Viewer. Wait 10 seconds on the disconnected screen, then close the viewer normally.'
    } else {
        New-Item -ItemType Directory -Path $taskCopyBackup | Out-Null
        foreach ($taskName in @('ReShade.ini', 'dlss5-feed.cfg')) {
            Copy-Item -LiteralPath (Join-Path $taskTarget $taskName) -Destination $taskCopyBackup
        }
        $taskCopySettings = $true
        $taskPresetName = if ($Mode -in @('IdentityEffects', 'WorldIdentity')) {'FS-P1-Identity.ini'} else {'FS-P0-Empty.ini'}
        if ($Mode -eq 'InputAudit') { $taskPresetName = 'FS-InputAudit.ini' }
        if ($Mode -eq 'EarlyNeural') { $taskPresetName = 'FS-EarlyNeural.ini' }
        $taskCopyIni = [IO.File]::ReadAllText((Join-Path $taskTarget 'ReShade.ini'))
        foreach ($taskPattern in @('(?m)^PresetPath=[^\r\n]*', '(?m)^StartupPresetPath=[^\r\n]*', '(?m)^NeuralUplift=[^\r\n]*')) {
            if ([regex]::Matches($taskCopyIni, $taskPattern).Count -ne 1) { throw "Expected one copy-test configuration key: $taskPattern" }
        }
        $taskCopyIni = [regex]::Replace($taskCopyIni, '(?m)^PresetPath=[^\r\n]*', "PresetPath=.\$taskPresetName")
        $taskCopyIni = [regex]::Replace($taskCopyIni, '(?m)^StartupPresetPath=[^\r\n]*', "StartupPresetPath=.\$taskPresetName")
        $taskCopyIni = [regex]::Replace($taskCopyIni, '(?m)^NeuralUplift=[^\r\n]*', 'NeuralUplift=0')
        if ($Mode -eq 'EarlyNeural') { $taskCopyIni = [regex]::Replace($taskCopyIni, '(?m)^NeuralUplift=[^\r\n]*', 'NeuralUplift=1') }
        if ($Mode -eq 'EarlyNeural') {
            foreach ($taskSetting in @('NRStyle=0', 'NRHookPoint=0', 'NREnableUpscaling=0')) {
                $taskKey = $taskSetting.Split('=')[0]
                $taskPattern = '(?m)^' + $taskKey + '=[^\r\n]*'
                if ([regex]::Matches($taskCopyIni, $taskPattern).Count -ne 1) { throw "Expected one $taskKey in ReShade.ini." }
                $taskCopyIni = [regex]::Replace($taskCopyIni, $taskPattern, $taskSetting)
            }
        }
        $taskCopyCfg = [IO.File]::ReadAllText((Join-Path $taskTarget 'dlss5-feed.cfg'))
        if ([regex]::Matches($taskCopyCfg, '(?m)^enabled=1\r?$').Count -ne 1) { throw 'Expected enabled Feeder before copy test.' }
        $taskCopyCfg = [regex]::Replace($taskCopyCfg, '(?m)^enabled=1(?=\r?$)', 'enabled=0')
        if ($Mode -eq 'EarlyNeural') { $taskCopyCfg = [regex]::Replace($taskCopyCfg, '(?m)^enabled=0(?=\r?$)', 'enabled=1') }
        if ($Mode -in @('IdentityEffects', 'WorldIdentity')) {
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'FS_P1Identity.fx') -Destination (Join-Path $taskTarget 'reshade-shaders/Shaders/FS_P1Identity.fx') -Force
            [IO.File]::WriteAllText((Join-Path $taskTarget $taskPresetName), "Techniques=FS_P1Identity@FS_P1Identity.fx`r`nTechniqueSorting=FS_P1Identity@FS_P1Identity.fx`r`n")
        } elseif ($Mode -in @('InputAudit', 'EarlyNeural')) {
            $taskAuditPreset = [IO.File]::ReadAllText((Join-Path $taskTarget 'ReShadePreset.ini'))
            $taskAuditPreset = New-FirestormControlPreset -Text $taskAuditPreset -Techniques 'Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@DLSS5_Feed.fx'
            [IO.File]::WriteAllText((Join-Path $taskTarget $taskPresetName), $taskAuditPreset)
        } else {
            [IO.File]::WriteAllText((Join-Path $taskTarget $taskPresetName), "Techniques=`r`nTechniqueSorting=`r`n")
        }
        [IO.File]::WriteAllText((Join-Path $taskTarget 'ReShade.ini'), $taskCopyIni)
        [IO.File]::WriteAllText((Join-Path $taskTarget 'dlss5-feed.cfg'), $taskCopyCfg)
        if ($Mode -in @('IdentityEffects', 'WorldIdentity')) { Write-Output 'P1 test: only FS_P1Identity enabled; Feeder disabled and neural uplift off. Three successful early calls maximum (world target may need warm-up).' }
        elseif ($Mode -eq 'InputAudit') { Write-Output 'Input audit: Kernel + Feed shaders enabled; Feeder submission and neural uplift OFF. No early effects.' }
        elseif ($Mode -eq 'EarlyNeural') { Write-Output 'Early neural prototype: Kernel + Feed and neural uplift ON, scene composed before UI. World is resampled through the existing client-sized pipeline.' }
        else { Write-Output 'P0 copy test: empty effects preset, Feeder disabled, neural uplift off for this run.' }
        if ($HelperResizeTest) { Write-Output 'Helper resize test: log in, wait 15 seconds, resize smaller, wait 15 seconds, restore the original size, wait 30 seconds, then quit normally. Check that neural rendering returns and UI stays sharp at each size.' }
        elseif ($HelperSession) { Write-Output 'Use the viewer normally. Resizing pauses for about four seconds; neural output returns after roughly eight seconds. Quit normally to restore settings.' }
        elseif ($Mode -eq 'EarlyNeural') { Write-Output 'Working baseline: keep the window size fixed. Resize recovery has a known consumer fault and is not validated. Quit normally to restore settings.' }
        elseif ($Mode -eq 'InputAudit') { Write-Output 'Keep UI visible. After login, pan the camera slowly for 15 seconds, wait 5 seconds, then quit normally. Do not resize or toggle UI.' }
        elseif ($Mode -eq 'WorldIdentity') { Write-Output 'World-rectangle test: keep UI visible, log in, wait 30 seconds, then quit normally. Do not resize or toggle UI.' }
        else { Write-Output 'Log in; wait 10 seconds with UI visible. Hide UI with Ctrl+Alt+F1 for 5 seconds, restore it for 10 seconds, then quit.' }
    }
    Write-Output 'Leave this console open; it restores held test files and archives logs after the viewer exits.'
    if ($PrepareOnly) { Write-Output 'Preparation completed without launching the viewer.'; return }
    $taskInfo = New-Object Diagnostics.ProcessStartInfo
    $taskInfo.FileName = $taskExe
    $taskInfo.WorkingDirectory = $taskTarget
    $taskInfo.UseShellExecute = $false
    # The viewer gates its Developer menu on QAMode. Expose that menu only in
    # this private-profile control, via the supported two-argument --set option.
    if ($Mode -eq 'ObservedDisconnect') { $taskInfo.Arguments = '--set QAMode true' }
    if ($Mode -eq 'ImageCopy') { $taskInfo.EnvironmentVariables['FS_SCENE_COPY_PROBE'] = '1' }
    else { $taskInfo.EnvironmentVariables.Remove('FS_SCENE_COPY_PROBE') }
    if ($Mode -in @('IdentityEffects', 'WorldIdentity')) { $taskInfo.EnvironmentVariables['FS_SCENE_IDENTITY_PROBE'] = '1' }
    else { $taskInfo.EnvironmentVariables.Remove('FS_SCENE_IDENTITY_PROBE') }
    if ($Mode -eq 'WorldIdentity') { $taskInfo.EnvironmentVariables['FS_SCENE_WORLD_PROBE'] = '1' }
    else { $taskInfo.EnvironmentVariables.Remove('FS_SCENE_WORLD_PROBE') }
    if ($Mode -eq 'InputAudit') { $taskInfo.EnvironmentVariables['FS_SCENE_INPUT_AUDIT'] = '1' }
    else { $taskInfo.EnvironmentVariables.Remove('FS_SCENE_INPUT_AUDIT') }
    if ($Mode -eq 'EarlyNeural') { $taskInfo.EnvironmentVariables['FS_SCENE_EARLY_NEURAL'] = '1' }
    else { $taskInfo.EnvironmentVariables.Remove('FS_SCENE_EARLY_NEURAL') }
    if ($HelperResizeTest -or $HelperSession) {
        $taskHistory = Join-Path $taskControls 'host64/sessions'
        New-Item -ItemType Directory -Force -Path $taskHistory | Out-Null
        $taskInfo.EnvironmentVariables['FS_FEEDER_RESTART_ON_RESIZE'] = '1'
        $taskInfo.EnvironmentVariables['FS_FEEDER_HISTORY_DIR'] = $taskHistory
    } else {
        $taskInfo.EnvironmentVariables.Remove('FS_FEEDER_RESTART_ON_RESIZE')
        $taskInfo.EnvironmentVariables.Remove('FS_FEEDER_HISTORY_DIR')
    }
    if ($taskProfile) {
        $taskInfo.EnvironmentVariables['APPDATA'] = Join-Path $taskProfile 'roaming'
        $taskInfo.EnvironmentVariables['LOCALAPPDATA'] = Join-Path $taskProfile 'local'
    }
    $taskProcess = New-Object Diagnostics.Process
    $taskProcess.StartInfo = $taskInfo
    $taskStartUtc = [DateTime]::UtcNow
    if (!$taskProcess.Start()) { throw 'Viewer process did not start.' }
    $taskStarted = $true
    $taskProcess.WaitForExit()
    Write-Output "Viewer exited with code $($taskProcess.ExitCode)."
    $taskRun = Join-Path $taskControls ($Mode + '-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + 'Z')
    New-Item -ItemType Directory -Path $taskRun | Out-Null
    foreach ($taskName in @('ReShade.log', 'dlss5-feed.log', 'ReShade.ini', 'ReShadePreset.ini', 'dlss5-feed.cfg', 'FS-P0-Empty.ini', 'FS-P1-Identity.ini', 'FS-InputAudit.ini', 'FS-EarlyNeural.ini')) {
        $taskFile = Join-Path $taskTarget $taskName
        if (Test-Path -LiteralPath $taskFile) { Copy-Item -LiteralPath $taskFile -Destination $taskRun }
    }
    if ($Mode -in @('IdentityEffects', 'WorldIdentity')) {
        Copy-Item -LiteralPath (Join-Path $taskTarget 'reshade-shaders/Shaders/FS_P1Identity.fx') -Destination $taskRun
    }
    $taskViewerLog = if ($taskProfile) { Join-Path $taskProfile 'roaming/Firestorm_x64/logs/Firestorm.log' } else { Join-Path $env:APPDATA 'Firestorm_x64/logs/Firestorm.log' }
    if (Test-Path -LiteralPath $taskViewerLog) { Copy-Item -LiteralPath $taskViewerLog -Destination $taskRun }
    [pscustomobject]@{Mode=$Mode;ExitCode=$taskProcess.ExitCode;ObserverSHA256=$taskObserverHash;StartUTC=$taskStartUtc.ToString('o');EndUTC=[DateTime]::UtcNow.ToString('o')} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskRun 'control.json')
    Write-Output "Control logs saved: $taskRun"
    $taskFeedLog = Join-Path $taskRun 'dlss5-feed.log'
    if (Test-Path -LiteralPath $taskFeedLog) {
        if (Select-String -LiteralPath $taskFeedLog -Pattern 'CreateFeature raised|not calling CreateFeature again|stopped: creating the DLSS feature' -Quiet) {
            Write-Warning 'Neural feature creation failed in this run. A moving picture or composed-frame counter does not establish neural delivery. Review the archived Feeder log; restart before trying neural processing again.'
        }
    }
} finally {
    # Do not restore into a live process after an interrupted wait. Leave the held
    # file and lock as explicit recovery evidence instead of guessing at process IDs.
    $taskStillRunning = $taskStarted -and !$taskProcess.HasExited
    if ($taskStillRunning) {
        Write-Warning 'Viewer is still running. Close it first, then restore the held observer; retain the lock until recovery is complete.'
        $taskLock.Dispose()
    } else {
        try {
            if ($taskMoved) {
                Move-Item -LiteralPath $taskHeld -Destination $taskObserver
                if ((Get-FileHash -LiteralPath $taskObserver).Hash -ne $taskObserverHash) { throw 'Restored observer hash mismatch.' }
                Write-Output 'Observer restored and hash verified.'
            }
            if ($taskCopySettings) {
                foreach ($taskName in @('ReShade.ini', 'dlss5-feed.cfg')) {
                    $taskSaved = Join-Path $taskCopyBackup $taskName
                    $taskRestored = Join-Path $taskTarget $taskName
                    Copy-Item -LiteralPath $taskSaved -Destination $taskRestored -Force
                    if ((Get-FileHash -LiteralPath $taskSaved).Hash -ne (Get-FileHash -LiteralPath $taskRestored).Hash) {
                        throw "Settings restoration failed: $taskName"
                    }
                }
                # Exact, non-recursive removals only after both files are verified.
                Remove-Item -LiteralPath (Join-Path $taskCopyBackup 'ReShade.ini'),(Join-Path $taskCopyBackup 'dlss5-feed.cfg')
                Remove-Item -LiteralPath $taskCopyBackup
                Write-Output 'ReShade/Feeder settings restored and hash verified.'
            }
        } finally {
            $taskLock.Dispose()
        }
        Remove-Item -LiteralPath $taskLockPath
    }
}

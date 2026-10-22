param([switch]$PrepareOnly,[string]$ViewerDirectory,[string]$ProfileDirectory,[string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$target = Join-Path $root 'build-vc/newview/Release'
$deps = Join-Path $PSScriptRoot '.deps/lifecycle/feeder-release-1.19.0'
$observer = Join-Path $PSScriptRoot 'build/v05/Release/firestorm_scene_observer.addon64'
$helperManifestPath = Join-Path $PSScriptRoot 'build/feeder-helper/manifest.json'
if ($ViewerDirectory) {
    $target = (Resolve-Path -LiteralPath $ViewerDirectory).Path
    $observer = Join-Path $PSScriptRoot 'firestorm_scene_observer.addon64'
    $helperManifestPath = Join-Path $PSScriptRoot 'helper-components.json'
}
$helperManifest = Get-Content -LiteralPath $helperManifestPath -Raw | ConvertFrom-Json
if ($helperManifest.Version -ne '1.19.0-fsresize1') { throw 'Build the fsresize1 helper client first.' }
$helperClient = $helperManifest.Binary
$hostSource = Join-Path $deps 'host64/dlss5-feed-host64.exe'
if ($ViewerDirectory) {
    $helperClient = Join-Path $PSScriptRoot 'dependencies/dlss5-feed-helper.addon64'
    $hostSource = Join-Path $PSScriptRoot 'dependencies/dlss5-feed-host64.exe'
} else {
    foreach ($inputFile in $helperManifest.Inputs) {
        if ((Get-FileHash -LiteralPath $inputFile.Path).Hash -ne $inputFile.SHA256) { throw "Helper source changed; rebuild: $($inputFile.Path)" }
    }
}
$controls = Join-Path $root 'build-vc/scene-signal-controls'
if ($ViewerDirectory) { $controls = Join-Path $target 'firestorm-neural/runs' }
if ($EvidenceDirectory) { $controls = [IO.Path]::GetFullPath($EvidenceDirectory) }
$held = Join-Path $controls 'held-helper-session'
$hostDir = Join-Path $target 'host64'
$hostExe = Join-Path $hostDir 'dlss5-feed-host64.exe'
$gpuKeyPath = 'Software\Microsoft\DirectX\UserGpuPreferences'
$gpuChanged = $false
$gpuExisted = $false
$gpuPrevious = $null
$saved = @()
$hashes = @{}

if (Get-Process -Name 'firestorm-bin' -ErrorAction SilentlyContinue) { throw 'Close the test viewer first.' }
if (Get-Process -Name 'dlss5-feed-host64' -ErrorAction SilentlyContinue) { throw 'A Feeder helper is already running. Close its session first.' }
foreach ($path in @($held, (Join-Path $controls 'running.lock'), (Join-Path $controls 'held-copy-settings'))) {
    if (Test-Path -LiteralPath $path) { throw "An earlier session needs recovery: $path" }
}
$expected = @{
    $helperClient = $helperManifest.SHA256
    $observer = '362D0C75F64B4E96DF4B358287AD89690290B2566F19ABC29B840B6B26973FFB'
}
if ($ViewerDirectory) { $expected[$observer] = $helperManifest.ObserverSHA256 }
$expected[$hostSource] = '79E427430C5C0102F6C7A557155B706AD51B0C40933F6E245FCE922BF5B1401E'
foreach ($path in $expected.Keys) {
    if ((Get-FileHash -LiteralPath $path).Hash -ne $expected[$path]) { throw "Unexpected test component: $path" }
}
foreach ($name in @('dlss5-feed.addon64','renodx-dlss5.addon64','firestorm_scene_observer.addon64','dlss5-feed.cfg','ReShade.ini','opengl32.dll','nvngx_dlss.dll','nvngx_dlssnr.dll')) {
    $hashes[$name] = (Get-FileHash -LiteralPath (Join-Path $target $name)).Hash
}
# This helper folder is ours; never overwrite an unrelated host installation.
$owner = Join-Path $hostDir 'firestorm-helper-test.txt'
if ((Test-Path -LiteralPath $hostDir) -and !(Test-Path -LiteralPath $owner)) { throw "Unrecognized helper folder: $hostDir" }
New-Item -ItemType Directory -Force -Path $hostDir,$controls | Out-Null
[IO.File]::WriteAllText($owner, 'Firestorm isolated helper resize experiment; third-party files are local copies, not a distribution.')
Copy-Item -LiteralPath $hostSource -Destination $hostExe -Force
foreach ($name in @('renodx-dlss5.addon64','nvngx_dlss.dll','nvngx_dlssnr.dll')) {
    Copy-Item -LiteralPath (Join-Path $target $name) -Destination $hostDir -Force
}
Copy-Item -LiteralPath (Join-Path $target 'opengl32.dll') -Destination (Join-Path $hostDir 'dxgi.dll') -Force
$ini = [IO.File]::ReadAllText((Join-Path $target 'ReShade.ini'))
$consumer = [regex]::Match($ini, '(?ms)^\[RenoDX\.DLSS5\].*?(?=^\[|\z)').Value
if (!$consumer) { throw 'No existing neural consumer configuration found.' }
foreach ($setting in @('NRStyle=0','NRHookPoint=0','NREnableUpscaling=0','NeuralUplift=1','NRToggleKey=0','NRScreenshotKey=0')) {
    $key = $setting.Split('=')[0]
    if ([regex]::Matches($consumer, '(?m)^'+$key+'=[^\r\n]*').Count -ne 1) { throw "Expected one consumer setting: $key" }
    $consumer = [regex]::Replace($consumer, '(?m)^'+$key+'=[^\r\n]*', $setting)
}
$hostIni = "[GENERAL]`r`nEffectSearchPaths=.\no-shaders`r`nTextureSearchPaths=.\no-textures`r`nPresetPath=.\Empty.ini`r`n[INPUT]`r`nKeyOverlay=0,0,0,0`r`n[OVERLAY]`r`nShowFPS=0`r`n" + $consumer
[IO.File]::WriteAllText((Join-Path $hostDir 'ReShade.ini'), $hostIni)
[IO.File]::WriteAllText((Join-Path $hostDir 'Empty.ini'), "Techniques=`r`n")
$run = Join-Path $controls ('HelperNeural-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + 'Z')
New-Item -ItemType Directory -Path $held,$run | Out-Null
$hashes | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $held 'original-hashes.json')
Copy-Item -LiteralPath $helperManifestPath -Destination (Join-Path $run 'helper-build.json')
try {
    foreach ($name in @('dlss5-feed.addon64','renodx-dlss5.addon64','firestorm_scene_observer.addon64')) {
        Move-Item -LiteralPath (Join-Path $target $name) -Destination $held
        $saved += $name
    }
    Copy-Item -LiteralPath (Join-Path $target 'dlss5-feed.cfg') -Destination $held
    $saved += 'dlss5-feed.cfg'
    # Keep the usual filename: the helper client finds host64 relative to its own module.
    Copy-Item -LiteralPath $helperClient -Destination (Join-Path $target 'dlss5-feed.addon64')
    Copy-Item -LiteralPath $observer -Destination (Join-Path $target 'firestorm_scene_observer.addon64')
    $cfg = [IO.File]::ReadAllText((Join-Path $target 'dlss5-feed.cfg'))
    foreach ($setting in @('host_window=2','async_home=0','work_resolution=100','work_upscale=0','settle_evals=0','hold_strength=0.000')) {
        $key = $setting.Split('=')[0]
        $pattern = '(?m)^'+$key+'=[^\r\n]*'
        if ([regex]::IsMatch($cfg,$pattern)) { $cfg = [regex]::Replace($cfg,$pattern,$setting) }
        else { $cfg += "`r`n$setting`r`n" }
    }
    [IO.File]::WriteAllText((Join-Path $target 'dlss5-feed.cfg'),$cfg)
    if (Test-Path -LiteralPath (Join-Path $target 'renodx-dlss5.addon64')) { throw 'Consumer must run only in the helper.' }
    if (!$PrepareOnly) {
        $gpuKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($gpuKeyPath)
        try {
            $gpuExisted = $gpuKey.GetValueNames() -contains $hostExe
            if ($gpuExisted) { $gpuPrevious = $gpuKey.GetValue($hostExe) }
            $gpuKey.SetValue($hostExe,'GpuPreference=2;',[Microsoft.Win32.RegistryValueKind]::String)
            $gpuChanged = $true
        } finally { $gpuKey.Dispose() }
    }
    Write-Output 'Experimental helper mode fsresize1: a fresh consumer process is started for each settled size. A brief pause and several seconds without neural rendering are expected during restart.'
    $controlArgs = @{Mode='EarlyNeural';PrepareOnly=$PrepareOnly;EvidenceDirectory=$run}
    if ($ViewerDirectory) { $controlArgs.ViewerDirectory = $target; $controlArgs.HelperSession = $true }
    else { $controlArgs.HelperResizeTest = $true }
    if ($ProfileDirectory) { $controlArgs.ProfileDirectory = $ProfileDirectory }
    & (Join-Path $PSScriptRoot 'Run-LifecycleControl.ps1') @controlArgs
} finally {
    try {
        $live = @(Get-Process -Name 'firestorm-bin' -ErrorAction SilentlyContinue)
        if ($live.Count -gt 0) {
            Write-Warning "Viewer is still running. Original files remain in $held. Close the viewer before restoring them."
        } else {
            foreach ($name in $saved) {
                Copy-Item -LiteralPath (Join-Path $held $name) -Destination (Join-Path $target $name) -Force
                if ((Get-FileHash -LiteralPath (Join-Path $target $name)).Hash -ne $hashes[$name]) { throw "Restoration hash mismatch: $name" }
            }
            foreach ($name in $saved) { Remove-Item -LiteralPath (Join-Path $held $name) }
            Remove-Item -LiteralPath (Join-Path $held 'original-hashes.json')
            Remove-Item -LiteralPath $held
            Write-Output 'Original Feeder, consumer, observer and configuration restored; hashes verified.'
        }
        $hostArchive = Join-Path $run 'host64'
        New-Item -ItemType Directory -Force -Path $hostArchive | Out-Null
        foreach ($file in Get-ChildItem -LiteralPath $hostDir -File | Where-Object { $_.Extension -in @('.log','.ini') -or $_.Name -like 'ReShade.log*' }) {
            Copy-Item -LiteralPath $file.FullName -Destination $hostArchive
        }
        Write-Output "Helper session evidence: $run"
        if (!$PrepareOnly) {
            $hostLogs = @(Get-ChildItem -LiteralPath $hostArchive -Recurse -Filter 'dlss5-feed-host.log' -File | Select-Object -ExpandProperty FullName)
            if ($hostLogs.Count -gt 0 -and (Select-String -LiteralPath $hostLogs -Pattern 'CreateFeature raised|not calling CreateFeature again' -Quiet)) {
                Write-Warning 'Helper neural processing FAILED. Firestorm can keep rendering after the consumer stops; FPS and composition counts do not prove neural recovery. The baseline is restored, but this resize experiment did not pass.'
            }
        }
    } finally {
        if ($gpuChanged) {
            $gpuKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($gpuKeyPath,$true)
            try {
                if ($gpuExisted) { $gpuKey.SetValue($hostExe,$gpuPrevious,[Microsoft.Win32.RegistryValueKind]::String) }
                else { $gpuKey.DeleteValue($hostExe,$false) }
            } finally { $gpuKey.Dispose() }
            Write-Output 'Helper GPU preference restored.'
        }
    }
}

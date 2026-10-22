param(
    [Parameter(Mandatory=$true)][string]$PackageZip,
    [string]$HelperBuildManifest=(Join-Path $PSScriptRoot 'build/feeder-helper/manifest.json'),
    [string]$HelperHostPath=(Join-Path $PSScriptRoot '.deps/lifecycle/feeder-release-1.19.0/host64/dlss5-feed-host64.exe'),
    [string]$PatchedViewerDirectory=(Join-Path $PSScriptRoot '../../build-vc/newview/Release')
)
$ErrorActionPreference='Stop'
$fixture=Join-Path $PSScriptRoot ('build/packages/smoke-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
$package=Join-Path $fixture 'package'
$viewer=Join-Path $fixture 'viewer'
New-Item -ItemType Directory -Force -Path $package,$viewer | Out-Null
Expand-Archive -LiteralPath $PackageZip -DestinationPath $package
$privateFiles=@(Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object {
    $_.FullName.Substring($package.Length+1).Replace('\','/') -match '(^|/)(plans|evidence)/'
})
if($privateFiles.Count){throw 'Private planning notes or raw evidence are in the ZIP.'}
$unexpected=@(Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object {$_.Extension -in @('.dll','.exe') -or ($_.Extension -eq '.addon64' -and $_.Name -ne 'firestorm_scene_observer.addon64')})
if($unexpected.Count){throw 'Third-party runtime/helper binary is in the ZIP.'}
$manifest=Get-Content (Join-Path $package 'manifest.json') -Raw | ConvertFrom-Json
foreach($entry in $manifest.Files){if((Get-FileHash (Join-Path $package $entry.Path)).Hash -ne $entry.SHA256){throw "Extracted ZIP hash mismatch: $($entry.Path)"}}
foreach($name in @('firestorm-bin.exe','firestorm_scene_observer.addon64','ReShade.ini','ReShadePreset.ini','dlss5-feed.cfg')) {
    Copy-Item -LiteralPath (Join-Path $PatchedViewerDirectory $name) -Destination $viewer
}
# Intentionally inert RHI placeholders: never load the viewer in this fixture.
foreach($name in @('opengl32.dll','dlss5-feed.addon64','renodx-dlss5.addon64','nvngx_dlss.dll','nvngx_dlssnr.dll')) {
    [IO.File]::WriteAllText((Join-Path $viewer $name),'PACKAGE TEST PLACEHOLDER - NOT A RUNTIME')
}
$baseline=@{}
Get-ChildItem -LiteralPath $viewer -File | ForEach-Object {$baseline[$_.Name]=(Get-FileHash $_.FullName).Hash}
$installer=Join-Path $package 'Install-FirestormNeural.ps1'
$installArgs=@{ViewerDirectory=$viewer;HelperBuildManifest=$HelperBuildManifest;HelperHostPath=$HelperHostPath}
function Assert-Baseline {
    foreach($name in $baseline.Keys){if((Get-FileHash (Join-Path $viewer $name)).Hash -ne $baseline[$name]){throw "Baseline changed: $name"}}
}
function Expect-Rejection([scriptblock]$Action,[string]$Pattern) {
    $rejected=$false
    try{& $Action}catch{if($_.Exception.Message -notmatch $Pattern){throw};$rejected=$true}
    if(!$rejected){throw "Expected rejection: $Pattern"}
}
try {
    & $installer @installArgs
    Assert-Baseline
    $launcher=[IO.File]::ReadAllText((Join-Path $viewer 'Launch-FirestormNeural.cmd'))
    if($launcher -notmatch 'Run-HelperNeural.ps1' -or $launcher -notmatch '-ViewerDirectory'){throw 'Installed launcher does not select the helper route.'}
    & (Join-Path $viewer 'firestorm-neural/Run-HelperNeural.ps1') -ViewerDirectory $viewer -PrepareOnly
    Assert-Baseline
    if(Test-Path (Join-Path $viewer 'firestorm-neural/runs/held-helper-session')){throw 'Held originals remain after preparation.'}
    $badHost=Join-Path $fixture 'wrong-host.exe'
    [IO.File]::WriteAllText($badHost,'NOT THE OFFICIAL HOST')
    Expect-Rejection { & $installer -ViewerDirectory $viewer -HelperBuildManifest $HelperBuildManifest -HelperHostPath $badHost } 'unchanged official'
    $stock=Join-Path $fixture 'unpatched'
    New-Item -ItemType Directory -Path $stock | Out-Null
    [IO.File]::WriteAllText((Join-Path $stock 'firestorm-bin.exe'),'unpatched viewer fixture')
    Expect-Rejection { & $installer -ViewerDirectory $stock -HelperBuildManifest $HelperBuildManifest -HelperHostPath $HelperHostPath } 'scene-signal patch'
    $tamper=Join-Path $package 'Run-HelperNeural.ps1'
    Add-Content -LiteralPath $tamper -Value '# intentional package-integrity failure'
    Expect-Rejection { & $installer @installArgs } 'Package hash mismatch'
    Assert-Baseline
    [pscustomobject]@{PackageSHA256=(Get-FileHash $PackageZip).Hash;ManifestFiles=@($manifest.Files).Count;Result='PASS';Checks=@('ZIP hashes and excluded third-party binaries','helper launcher installed','existing observer and RHI/config/EXE preserved','helper PrepareOnly exact restoration','wrong official-host hash rejected','unpatched viewer rejected','tampered package rejected');RuntimeLaunched=$false} |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $fixture 'result.json')
    Write-Output "Package checks passed: $fixture"
} finally {
    # The fixture is not a runnable viewer installation; prevent accidental launch.
    Remove-Item -LiteralPath (Join-Path $viewer 'firestorm-bin.exe') -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText((Join-Path $fixture 'DO-NOT-LAUNCH.txt'),'Isolated package checks only. RHI files are inert placeholders; copied viewer EXE removed. No viewer or helper runtime was launched.')
}

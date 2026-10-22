param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build/v05'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build/packages'),
    [string]$ViewerPatchBase = '0de5043af00b1b7dd3201d3b878bb7033e4bf067',
    [string]$FeederSourceDirectory = (Join-Path $PSScriptRoot '.deps/lifecycle/DLSS5-Feeder-1.19.0')
)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskBinary = Join-Path $BuildDirectory 'Release/firestorm_scene_observer.addon64'
if (!(Test-Path -LiteralPath $taskBinary)) { throw 'Build the real Release observer first.' }
$taskBase = & git -C $taskRoot rev-parse --verify ($ViewerPatchBase + '^{commit}')
if ($LASTEXITCODE -ne 0) { throw 'Viewer patch base revision is unavailable; supply -ViewerPatchBase.' }
$taskBase = $taskBase.Trim()
$taskFeederLicense = Join-Path $FeederSourceDirectory 'LICENSE'
if (!(Test-Path -LiteralPath $taskFeederLicense)) { throw 'Supply -FeederSourceDirectory pointing to the official Feeder v1.19.0 source.' }
$taskStamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$taskPackage = Join-Path $OutputDirectory ('firestorm-neural-helper-v0.9-' + $taskStamp)
if (Test-Path -LiteralPath $taskPackage) { throw 'Package directory already exists.' }
New-Item -ItemType Directory -Force -Path $taskPackage | Out-Null
foreach ($taskName in @('Install-FirestormNeural.ps1','Run-HelperNeural.ps1','Run-LifecycleControl.ps1','Preset.ps1','FS_P1Identity.fx','ReShade-LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $taskName) -Destination $taskPackage
}
Copy-Item -LiteralPath $taskBinary -Destination $taskPackage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'PACKAGE-README.md') -Destination (Join-Path $taskPackage 'README.md')
Copy-Item -LiteralPath (Join-Path $taskRoot 'LICENSE') -Destination (Join-Path $taskPackage 'LICENSE-LGPL-2.1.txt')
Copy-Item -LiteralPath $taskFeederLicense -Destination (Join-Path $taskPackage 'Feeder-LICENSE.txt')
$taskSource = Join-Path $taskPackage 'source/tools/dlss5-observer'
New-Item -ItemType Directory -Force -Path $taskSource,(Join-Path $taskPackage 'source/indra/newview'),(Join-Path $taskPackage 'source/doc') | Out-Null
Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in @('.cpp','.hpp','.inc','.ps1','.fx','.md','.txt','.c') -or $_.Name -eq 'CMakeLists.txt' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $taskSource
}
foreach ($taskName in @('fsscenesignal.cpp','fsscenesignal.h')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot "indra/newview/$taskName") -Destination (Join-Path $taskPackage 'source/indra/newview')
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'doc/dlss5.md') -Destination (Join-Path $taskPackage 'source/doc')
$taskCommit = (& git -C $taskRoot rev-parse HEAD).Trim()
$taskPatch = & git -C $taskRoot diff --binary $taskBase -- indra/newview/CMakeLists.txt indra/newview/llviewerdisplay.cpp indra/newview/llviewerdisplay.h indra/newview/llviewerwindow.cpp indra/newview/pipeline.cpp indra/newview/pipeline.h indra/newview/fsscenesignal.cpp indra/newview/fsscenesignal.h
if ($LASTEXITCODE -ne 0) { throw 'Viewer patch generation failed.' }
[IO.File]::WriteAllText((Join-Path $taskPackage 'viewer-signal.patch'), ($taskPatch -join "`n") + "`n")
$taskFiles = Get-ChildItem -LiteralPath $taskPackage -File -Recurse | ForEach-Object {
    [pscustomobject]@{Path=$_.FullName.Substring(([IO.Path]::GetFullPath($taskPackage)).Length + 1).Replace('\','/'); SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}
}
[pscustomobject]@{Version='0.9-helper-preview';Mode='HelperRestart';HelperClientVersion='1.19.0-fsresize1';HostSHA256='79E427430C5C0102F6C7A557155B706AD51B0C40933F6E245FCE922BF5B1401E';BuildUTC=[DateTime]::UtcNow.ToString('o');SourceCommit=$taskCommit;ViewerPatchBase=$taskBase;WorkingTreeDirty=[bool](& git -C $taskRoot status --porcelain);Files=@($taskFiles)} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskPackage 'manifest.json') -Encoding UTF8
$taskZip = $taskPackage + '.zip'
Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $taskPackage | Select-Object -ExpandProperty FullName) -DestinationPath $taskZip
Write-Output "Package: $taskZip"
Write-Output "SHA256: $((Get-FileHash -LiteralPath $taskZip).Hash)"

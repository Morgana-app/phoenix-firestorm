param([string]$SourceDirectory = (Join-Path $PSScriptRoot '.deps/lifecycle/DLSS5-Feeder-1.19.0'))
$ErrorActionPreference = 'Stop'
$original = Join-Path $SourceDirectory 'src/dlss5-feed32.cpp'
if ((Get-FileHash -LiteralPath $original).Hash -ne '0E35EFFFCDD07CDF342A80046153CE082790AA0A2BE590584870ADB560E144FB') {
    throw 'Expected pristine official Feeder v1.19.0 helper source.'
}
$buildRoot = Join-Path $PSScriptRoot 'build/feeder-helper'
$fork = Join-Path $buildRoot 'source'
New-Item -ItemType Directory -Force -Path $fork | Out-Null
foreach ($name in @('src','external','tools')) {
    Copy-Item -LiteralPath (Join-Path $SourceDirectory $name) -Destination $fork -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'resize_policy.hpp'),(Join-Path $PSScriptRoot 'feeder_helper_restart.inc') -Destination (Join-Path $fork 'src') -Force
$cpp = [IO.File]::ReadAllText($original).Replace("`r`n","`n")
function Replace-ExactlyOnce([string]$From,[string]$To) {
    if (($script:cpp.Split(@($From),[StringSplitOptions]::None)).Count -ne 2) { throw "Patch anchor not unique: $From" }
    $script:cpp = $script:cpp.Replace($From,$To)
}
Replace-ExactlyOnce '#include "feed_ipc.h"' "#include `"resize_policy.hpp`"`n#include `"feed_ipc.h`""
Replace-ExactlyOnce 'static void HostApplySettings()' "#include `"feeder_helper_restart.inc`"`n`nstatic void HostApplySettings()"
Replace-ExactlyOnce "    Breadcrumb(`"building the shared textures (OpenGL)`");`n    if (!HostConnectReady()) { g_build_pending = true; return false; }`n    ReleaseShared();" "    Breadcrumb(`"building the shared textures (OpenGL)`");`n    if (!HostConnectReady()) { g_build_pending = true; return false; }`n    if (!FsBeforeGlBuild(w, h)) { g_build_pending = true; return false; }`n    ReleaseShared();"
Replace-ExactlyOnce "    g_build_pending = true;`n    Log(`"[feed32] building: %ux%u backbuffer fmt=%u (OpenGL" "    FsGlBuildSubmitted(w, h);`n    g_build_pending = true;`n    Log(`"[feed32] building: %ux%u backbuffer fmt=%u (OpenGL"
Replace-ExactlyOnce "                                         : DXGI_FORMAT_R8G8B8A8_UNORM;`n    bool ok = true;" "                                         : DXGI_FORMAT_R8G8B8A8_UNORM;`n    if (!FsGlExtentReady(w, h)) return;`n    bool ok = true;"
Replace-ExactlyOnce "    if (ok && g.built)`n    {`n        if (!HostAlive()) { HostLost(`"process died`"); }`n        else`n        {`n            FeedGlStateGuard guard(&g.gl);" "    // A completed background build can belong to the previous window extent.`n    // Do not copy new-size inputs into that set or composite its old-size output.`n    if (ok && g.built && (w != g.width || h != g.height || bbf != g.bb_fmt)) return;`n    if (ok && g.built)`n    {`n        if (!HostAlive()) { HostLost(`"process died`"); }`n        else`n        {`n            FeedGlStateGuard guard(&g.gl);"
[IO.File]::WriteAllText((Join-Path $fork 'src/dlss5-feed32.cpp'),$cpp)
$batchPath = Join-Path $fork 'tools/build-helper64.bat'
$batch = [IO.File]::ReadAllText($batchPath)
$batch = [regex]::Replace($batch,'(?m)^for /f .*FEED_BUILD_ID=.*$','set FEED_BUILD_ID=3c09eab-fsresize1')
[IO.File]::WriteAllText($batchPath,$batch)
& $batchPath 2>&1 | Tee-Object -FilePath (Join-Path $buildRoot 'build.log') | Write-Output
if ($LASTEXITCODE -ne 0) { throw 'Helper client build failed; see build/feeder-helper/build.log.' }
$binary = Join-Path $fork 'build/helper64/dlss5-feed-helper.addon64'
$inputs = @($original,(Join-Path $PSScriptRoot 'resize_policy.hpp'),(Join-Path $PSScriptRoot 'feeder_helper_restart.inc'),(Join-Path $fork 'src/dlss5-feed32.cpp')) | ForEach-Object {
    [pscustomobject]@{Path=[IO.Path]::GetFullPath($_);SHA256=(Get-FileHash -LiteralPath $_).Hash}
}
[pscustomobject]@{Version='1.19.0-fsresize1';BaseCommit='3c09eab';Binary=[IO.Path]::GetFullPath($binary);SHA256=(Get-FileHash -LiteralPath $binary).Hash;Inputs=@($inputs)} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $buildRoot 'manifest.json')
Write-Output "Local opt-in helper client: $binary"

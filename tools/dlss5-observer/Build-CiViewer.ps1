$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$logs=Join-Path $PSScriptRoot 'build/ci-viewer-logs'
$output=Join-Path $PSScriptRoot 'build/ci-viewer-download'
New-Item -ItemType Directory -Force -Path $logs,$output | Out-Null
Push-Location $root
try {
    # Fixed channel avoids embedding the runner's computer name in the viewer identity.
    # configure_firestorm.sh exports its own lowercase revision. Preseeding it
    # from PowerShell can create a revision/REVISION collision through MSYS.
    Remove-Item Env:\revision -ErrorAction SilentlyContinue
    $configureArgs=@('configure','-A','64','-c','ReleaseFS_open','--','--package','--chan','DLSS5Preview','--no-opensim',
        '-DINSTALL_PROPRIETARY:BOOL=OFF','-DUSE_FMODSTUDIO:BOOL=OFF','-DFMODSTUDIO:BOOL=OFF','-DUSE_KDU:BOOL=OFF',
        '-DUSE_OPENAL:BOOL=ON','-DUSE_BUGSPLAT:BOOL=OFF','-DUSE_VELOPACK:BOOL=OFF','-DLL_TESTS:BOOL=OFF')
    autobuild @configureArgs 2>&1 | Tee-Object (Join-Path $logs 'configure.log')
    if($LASTEXITCODE){throw 'Viewer configuration failed.'}
    # Apply native compiler options after Bash has finished its path conversion.
    $env:_CL_='/MP2'
    cmake --build build-vc170-64 --config Release --parallel 2 -- /nodeReuse:false 2>&1 | Tee-Object (Join-Path $logs 'build.log')
    if($LASTEXITCODE){throw 'Viewer build failed.'}
    $installers=@(Get-ChildItem (Join-Path $root 'build-vc170-64/newview') -Filter '*Setup*.exe' -File -Recurse)
    if(!$installers.Count){throw 'Build completed without a viewer installer.'}
    foreach($file in $installers){Copy-Item $file.FullName $output}
    Copy-Item (Join-Path $PSScriptRoot 'CI-DOWNLOAD-README.md') (Join-Path $output 'README.md')
    Get-ChildItem $output -File | ForEach-Object {'{0}  {1}' -f (Get-FileHash $_.FullName).Hash,$_.Name} | Set-Content (Join-Path $output 'SHA256SUMS.txt')
} finally {Pop-Location}

param(
    [string]$DependencyDirectory=(Join-Path $PSScriptRoot '.deps/ci'),
    [string]$Generator='Visual Studio 17 2022'
)
$ErrorActionPreference='Stop'
$deps=Get-Content (Join-Path $DependencyDirectory 'dependencies.json') -Raw | ConvertFrom-Json
$observerBuild=Join-Path $PSScriptRoot 'build/ci-observer'
$output=Join-Path $PSScriptRoot 'build/ci-download'
if(Test-Path $output){throw 'CI output already exists; use a clean checkout/build folder.'}
New-Item -ItemType Directory -Force -Path $output | Out-Null
cmake -S $PSScriptRoot -B $observerBuild -G $Generator -A x64 "-DRESHADE_INCLUDE_DIR=$($deps.ReShade)/include" "-DGL_HEADERS_DIR=$($deps.GlHeaders)" "-DIDENTITY_SHADER_INCLUDE_DIR=$($deps.ShaderHeaders)"
if($LASTEXITCODE){throw 'Observer configure failed.'}
cmake --build $observerBuild --config Release --parallel 2 -- /nodeReuse:false
if($LASTEXITCODE){throw 'Observer build failed.'}
ctest --test-dir $observerBuild -C Release --output-on-failure --output-junit (Join-Path $observerBuild 'Testing/results.xml')
if($LASTEXITCODE){throw 'Observer/helper policy tests failed.'}
& (Join-Path $PSScriptRoot 'Build-FeederHelper.ps1') -SourceDirectory $deps.Feeder
& (Join-Path $PSScriptRoot 'Build-Package.ps1') -BuildDirectory $observerBuild -OutputDirectory (Join-Path $PSScriptRoot 'build/ci-packages') -FeederSourceDirectory $deps.Feeder
$package=Get-ChildItem (Join-Path $PSScriptRoot 'build/ci-packages') -Filter '*.zip' -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Copy-Item $package.FullName $output
$helper=Join-Path $output 'helper'
New-Item -ItemType Directory -Path $helper | Out-Null
$manifest=Get-Content (Join-Path $PSScriptRoot 'build/feeder-helper/manifest.json') -Raw | ConvertFrom-Json
Copy-Item $manifest.Binary (Join-Path $helper 'dlss5-feed-helper.addon64')
Copy-Item (Join-Path $deps.FeederRelease 'host64/dlss5-feed-host64.exe') $helper
# Portable download metadata contains no CI machine paths; payload hashes remain mandatory.
[pscustomobject]@{Version=$manifest.Version;BaseCommit=$manifest.BaseCommit;Binary='dlss5-feed-helper.addon64';SHA256=$manifest.SHA256;SourceCommit=(& git rev-parse HEAD).Trim();Inputs=@()} |
    ConvertTo-Json | Set-Content (Join-Path $helper 'manifest.json')
$licenses=Join-Path $helper 'licenses'
New-Item -ItemType Directory -Path $licenses | Out-Null
foreach($entry in @(@('LICENSE','Feeder-MIT.txt'),@('external/minhook/LICENSE.txt','MinHook.txt'),@('external/imgui/LICENSE.txt','ImGui.txt'))) {
    Copy-Item (Join-Path $deps.Feeder $entry[0]) (Join-Path $licenses $entry[1])
}
Copy-Item (Join-Path $PSScriptRoot 'ReShade-LICENSE.txt') $licenses
Copy-Item (Join-Path $PSScriptRoot 'CI-DOWNLOAD-README.md') (Join-Path $output 'README.md')
Get-ChildItem $output -File -Recurse | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash $_.FullName).Hash,$_.FullName.Substring($output.Length+1).Replace('\','/')
} | Set-Content (Join-Path $output 'SHA256SUMS.txt')
Write-Output 'Integration binaries and portable helper download are ready.'

param([string]$DependencyDirectory=(Join-Path $PSScriptRoot '.deps/ci'))
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $DependencyDirectory | Out-Null
$DependencyDirectory=(Resolve-Path $DependencyDirectory).Path
function Download([string]$Url,[string]$Path,[string]$SHA256='') {
    New-Item -ItemType Directory -Force -Path (Split-Path $Path) | Out-Null
    if (!(Test-Path $Path)) { Invoke-WebRequest -Uri $Url -OutFile $Path }
    if ($SHA256 -and (Get-FileHash $Path).Hash -ne $SHA256) { throw "Dependency hash mismatch: $Url" }
}
function Archive([string]$Name,[string]$Url,[string]$SHA256) {
    $zip=Join-Path $DependencyDirectory "$Name.zip"
    Download $Url $zip $SHA256
    $folder=Join-Path $DependencyDirectory $Name
    if (!(Test-Path $folder)) { Expand-Archive -LiteralPath $zip -DestinationPath $folder }
    return $folder
}
$rs=Archive 'reshade' 'https://codeload.github.com/crosire/reshade/zip/refs/tags/v6.8.0' '936D3E9CC0506F16E635266299F43A6B9DB49023AF1573FAE572195506E5B764'
$feeder=Archive 'feeder' 'https://codeload.github.com/jlrouzies-fr/DLSS5-Feeder/zip/refs/tags/v1.19.0' '72E6F64FD9B705E0282D3A51908C39978F3CB3F260CBE0DD830B6DB8C55C4574'
$vk=Archive 'vulkan' 'https://codeload.github.com/KhronosGroup/Vulkan-Headers/zip/refs/tags/v1.3.290' '918A8BB31B8312FD649FD1D1826443DA870F33F3187C16C4528AA94927409BAB'
$release=Archive 'feeder-release' 'https://github.com/jlrouzies-fr/DLSS5-Feeder/releases/download/v1.19.0/DLSS5-Feeder-1.19.0.zip' '9CC19E28F8FB4E216EA61621FDF6443E207E338EC639979AB335503C75F48F36'
$feederSource=Join-Path $feeder 'DLSS5-Feeder-1.19.0'
Copy-Item -LiteralPath (Join-Path $vk 'Vulkan-Headers-1.3.290/include/vulkan'),(Join-Path $vk 'Vulkan-Headers-1.3.290/include/vk_video') -Destination (Join-Path $feederSource 'external/vulkan') -Recurse -Force
$gl=Join-Path $DependencyDirectory 'gl-headers'
Download 'https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/6af574a14089ccfee87efe230ebcdd8742859813/api/GL/glext.h' (Join-Path $gl 'GL/glext.h')
Download 'https://raw.githubusercontent.com/KhronosGroup/EGL-Registry/e964211ff5f7c2bcada786954a16d5b813379395/api/KHR/khrplatform.h' (Join-Path $gl 'KHR/khrplatform.h')
$shader=Join-Path $DependencyDirectory 'shaders'
Download 'https://raw.githubusercontent.com/crosire/reshade-shaders/fd0022170615ce0d8162d219bff07232fa6dd84f/Shaders/ReShade.fxh' (Join-Path $shader 'ReShade.fxh')
[pscustomobject]@{ReShade=(Join-Path $rs 'reshade-6.8.0');Feeder=$feederSource;FeederRelease=$release;GlHeaders=$gl;ShaderHeaders=$shader} |
    ConvertTo-Json | Set-Content (Join-Path $DependencyDirectory 'dependencies.json')
Write-Output 'Pinned integration dependencies are ready.'

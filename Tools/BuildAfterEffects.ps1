param([string]$SDKRoot = $env:AE_SDK_ROOT)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\InvokeNative.ps1"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SDKRoot)) {
    $candidates = @()
    $cache = Join-Path $root 'build-ae\CMakeCache.txt'
    if (Test-Path -LiteralPath $cache -PathType Leaf) {
        $cachedRoot = Select-String -LiteralPath $cache -Pattern '^AE_SDK_ROOT:PATH=(.+)$' | Select-Object -First 1
        if ($cachedRoot) { $candidates += $cachedRoot.Matches[0].Groups[1].Value }
    }
    $workspace = Split-Path -Parent (Split-Path -Parent $root)
    $candidates += Join-Path $workspace 'work\AdobeSDK26_5\AfterEffectsSDK_26.5_win'
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'Examples\Headers\AE_Effect.h') -PathType Leaf) {
            $SDKRoot = $candidate
            break
        }
    }
}
if ([string]::IsNullOrWhiteSpace($SDKRoot)) {
    throw 'Adobe SDK was not found. Supply -SDKRoot with your actual extracted SDK folder (or set AE_SDK_ROOT). It must contain Examples\Headers\AE_Effect.h.'
}
if (!(Test-Path -LiteralPath (Join-Path $SDKRoot 'Examples\Headers\AE_Effect.h') -PathType Leaf)) {
    throw "Adobe SDK header was not found under '$SDKRoot'. Use your actual extracted SDK folder, not the example C:\path\to path. Omit -SDKRoot to use the previously configured SDK or the workspace SDK."
}
$SDKRoot = (Resolve-Path -LiteralPath $SDKRoot).Path
Write-Host "Adobe SDK: $SDKRoot"
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmake = $cmakeCommand.Source }
else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
# CMake PiPL generation uses this same PowerShell 7 executable.
$env:PATH = (Split-Path (Get-Process -Id $PID).Path) + ';' + $env:PATH
Invoke-LinkNative $cmake @('-S',$root,'-B',"$root\build-ae",'-G','Visual Studio 17 2022','-A','x64',"-DAE_SDK_ROOT=$SDKRoot",'-DBUILD_TESTING=OFF')
Invoke-LinkNative $cmake @('--build',"$root\build-ae",'--config','Release','--target','UnrealAELinkEffect','AdobeRenderTests','--parallel')
if (!(Test-Path -LiteralPath "$root\AfterEffectsPlugin\UnrealAELink\Binaries\Win64\UnrealAELink.aex")) { throw 'Build finished without producing the expected .aex artifact' }
Invoke-LinkNative "$root\build-ae\AfterEffectsPlugin\UnrealAELink\Release\AdobeRenderTests.exe" @()
Write-Host "Built: $root\AfterEffectsPlugin\UnrealAELink\Binaries\Win64\UnrealAELink.aex"

param([string]$SDKRoot = $env:AE_SDK_ROOT)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\InvokeNative.ps1"
$root = Split-Path -Parent $PSScriptRoot
if (!$SDKRoot -or !(Test-Path -LiteralPath "$SDKRoot\Examples\Headers\AE_Effect.h")) {
    throw 'Supply -SDKRoot pointing to the extracted official Adobe SDK folder containing Examples/Headers/AE_Effect.h'
}
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

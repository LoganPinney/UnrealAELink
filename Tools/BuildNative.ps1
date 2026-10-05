param([string]$Configuration = 'Release', [switch]$Fresh)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\InvokeNative.ps1"
$root = Split-Path -Parent $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmake = $cmakeCommand.Source }
else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools with CMake support; see docs/build.md.' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$configureArgs = @('-S', $root, '-B', "$root\build", '-G', 'Visual Studio 17 2022', '-A', 'x64')
if ($Fresh) { $configureArgs += '--fresh' }
Invoke-LinkNative $cmake $configureArgs
Invoke-LinkNative $cmake @('--build', "$root\build", '--config', $Configuration, '--parallel')
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
Invoke-LinkNative $ctest @('--test-dir', "$root\build", '-C', $Configuration, '--output-on-failure')

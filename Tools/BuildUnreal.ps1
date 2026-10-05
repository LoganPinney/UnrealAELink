param([string]$EngineRoot)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\InvokeNative.ps1"
$engine = & "$PSScriptRoot\FindEngine.ps1" -EngineRoot $EngineRoot
$root = Split-Path -Parent $PSScriptRoot
$project = "$root\Tests\UnrealAELinkTest\UnrealAELinkTest.uproject"
# Installed engines ship a prebuilt UBT and bundled .NET; no engine source rebuild.
$dotnet = "$engine\Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe"
if (!(Test-Path -LiteralPath $dotnet)) { throw 'This build script targets the discovered UE 5.8 toolchain; bundled .NET 10.0 was not found.' }
$ubt = "$engine\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"
Invoke-LinkNative $dotnet @($ubt, 'UnrealAELinkTestEditor', 'Win64', 'Development', "-Project=$project", '-WaitMutex', '-NoHotReloadFromIDE', '-NoUBA') "$engine\Engine\Source"

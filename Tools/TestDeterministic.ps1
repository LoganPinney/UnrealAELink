param([string]$EngineRoot, [string]$AfterEffectsRoot = 'C:\Program Files\Adobe\Adobe After Effects 2026', [int]$TimeoutSeconds = 360, [switch]$NativeOnly)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\InvokeNative.ps1"
. "$PSScriptRoot\InspectAdobeImage.ps1"
$root = Split-Path -Parent $PSScriptRoot
$engine = & "$PSScriptRoot\FindEngine.ps1" -EngineRoot $EngineRoot
if (Get-Process UnrealEditor,AfterFX,ReceiverTest,GpuTests -ErrorAction SilentlyContinue) { throw 'Close editors, Adobe and GPU receivers before the isolated deterministic host acceptance test' }
$out = Join-Path $root $(if ($NativeOnly) {'artifacts\sequencer-native'} else {'artifacts\deterministic'})
New-Item -ItemType Directory -Force -Path $out | Out-Null
# Remove only exact test-owned outputs inside the resolved artifacts directory.
$names = @('editor.log','ae-native.log','ae-request.log','AfterEffectsDeterministicError.aep','evidence.csv')
foreach ($n in @(0,30,60)) { $names += "known-$n.$('{0:D5}' -f $n).tif" }
foreach ($n in 0..30) { $names += "sequential.$('{0:D5}' -f $n).tif" }
foreach ($n in @(20,3,17,0,29)) { $names += "random-$n.$('{0:D5}' -f $n).tif" }
$names += 'subframe.00001.tif','ntsc.00017.tif'
# Adobe's NTSC filename suffix can round down even while its actual render time
# is correct. Clear this test-owned prefix so the validator cannot see stale files.
$names += @(Get-ChildItem -LiteralPath $out -Filter 'ntsc.*.tif' -File | ForEach-Object { $_.Name })
foreach ($name in $names) { $path = Join-Path $out $name; if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path } }
$editor = $null; $adobe = $null
try {
    $editorArgs = @("$root\Tests\UnrealAELinkTest\UnrealAELinkTest.uproject",'-unattended','-nosplash','-NoSound','-RenderOffscreen','-dx12',
        "-ExecCmds=UnrealAELink.DeterministicTestSeconds $TimeoutSeconds,UnrealAELink.LogEvaluatedBindings 1,Automation RunTests UnrealAELink.Sequencer.HostFixture", "-abslog=$out\editor.log")
    $editor = [Diagnostics.Process]::Start((New-LinkProcessInfo "$engine\Engine\Binaries\Win64\UnrealEditor.exe" $editorArgs $root))
    $editorOut = $editor.StandardOutput.ReadToEndAsync(); $editorErr = $editor.StandardError.ReadToEndAsync()
    $begin = [DateTime]::UtcNow
    while (!(Test-Path -LiteralPath "$out\editor.log") -or (Get-Content -LiteralPath "$out\editor.log" -Raw) -notmatch 'UnrealAELink deterministic fixture ready') {
        if ($editor.HasExited -or ([DateTime]::UtcNow - $begin).TotalSeconds -gt 120) { throw 'Unreal deterministic fixture did not become ready' }
        Start-Sleep -Milliseconds 500
    }
    if ($NativeOnly) {
        $env:UNREAL_AE_LINK_DIAGNOSTIC_DIR = $out
        Invoke-LinkNative "$root\build\Release\RequestTests.exe" @('--unreal') $root
        return
    }
    $info = New-LinkProcessInfo "$AfterEffectsRoot\Support Files\AfterFX.exe" @('-r',"$root\Tests\AfterEffectsDeterministic.jsx") $root
    $info.Environment['UNREAL_AE_LINK_DIAGNOSTIC_DIR'] = $out
    $adobe = [Diagnostics.Process]::Start($info)
    $adobeOut = $adobe.StandardOutput.ReadToEndAsync(); $adobeErr = $adobe.StandardError.ReadToEndAsync()
    while (!$adobe.HasExited) {
        if (([DateTime]::UtcNow - $begin).TotalSeconds -gt $TimeoutSeconds) { throw 'Deterministic AE host acceptance timed out' }
        Start-Sleep -Milliseconds 500
    }
    $adobe.WaitForExit()
    $errorProject = "$out\AfterEffectsDeterministicError.aep"
    if (Test-Path -LiteralPath $errorProject) {
        $errorText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($errorProject))
        $message = [regex]::Match($errorText, 'FAIL ([^\x00]{1,180}?)(?:\x00|LIST)')
        if ($message.Success) { throw "After Effects deterministic script failed: $($message.Groups[1].Value)" }
        throw 'After Effects deterministic script failed; inspect its saved error project'
    }
    if ($adobe.ExitCode -ne 0) { throw "After Effects deterministic process failed: exit=$($adobe.ExitCode); inspect artifacts/deterministic logs" }
    & "$PSScriptRoot\ValidateDeterministic.ps1" -Artifacts $out
} finally {
    foreach ($child in @($adobe,$editor)) { if ($null -ne $child -and !$child.HasExited) { $child.Kill(); $child.WaitForExit() } }
    if ($null -ne $adobe) { ($adobeOut.GetAwaiter().GetResult() + $adobeErr.GetAwaiter().GetResult()) | Set-Content "$out\adobe-console.log"; $adobe.Dispose() }
    if ($null -ne $editor) { ($editorOut.GetAwaiter().GetResult() + $editorErr.GetAwaiter().GetResult()) | Set-Content "$out\editor-console.log"; $editor.Dispose() }
}

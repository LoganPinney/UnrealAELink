param([string]$EngineRoot, [string]$AfterEffectsRoot = 'C:\Program Files\Adobe\Adobe After Effects 2026', [int]$TimeoutSeconds = 180)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\InvokeNative.ps1"
$root = Split-Path -Parent $PSScriptRoot
$engine = & "$PSScriptRoot\FindEngine.ps1" -EngineRoot $EngineRoot
if (Get-Process UnrealEditor,AfterFX,ReceiverTest,GpuTests -ErrorAction SilentlyContinue) {
    throw 'Close running Unreal editors, After Effects and GPU receivers before this isolated acceptance test'
}
$installed = "$AfterEffectsRoot\Support Files\Plug-ins\UnrealAELink\UnrealAELink.aex"
if (!(Test-Path -LiteralPath $installed)) { throw 'InstallAfterEffects must succeed first' }
New-Item -ItemType Directory -Force -Path "$root\artifacts" | Out-Null
# Only test-owned previous artifacts are removed; exact paths, no recursive delete.
foreach ($name in @('ae-native.log','ae-disconnected.png','ae-beauty-8.png','ae-beauty-16.png','ae-beauty-32.png','ae-half.png','ae-frozen-a.png','ae-frozen-b.png','ae-disconnected-final.png','AfterEffectsHostError.aep')) {
    $path = Join-Path "$root\artifacts" $name
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
}
$editor = $null; $adobe = $null
try {
    $editorArgs = @("$root\Tests\UnrealAELinkTest\UnrealAELinkTest.uproject",'-unattended','-nosplash','-NoSound','-RenderOffscreen','-dx12',
        '-ExecCmds=UnrealAELink.TestSceneSeconds 120,Automation RunTests UnrealAELink.Beauty.FrameTransfer',"-abslog=$root\artifacts\editor-adobe.log")
    $editor = [System.Diagnostics.Process]::Start((New-LinkProcessInfo "$engine\Engine\Binaries\Win64\UnrealEditor.exe" $editorArgs $root))
    $editorOut = $editor.StandardOutput.ReadToEndAsync(); $editorErr = $editor.StandardError.ReadToEndAsync()
    $begin = [DateTime]::UtcNow
    while (!(Test-Path -LiteralPath "$root\artifacts\editor-adobe.log") -or
        (Get-Content -LiteralPath "$root\artifacts\editor-adobe.log" -Raw) -notmatch 'UnrealAELink test scene ready') {
        if ($editor.HasExited -or ([DateTime]::UtcNow - $begin).TotalSeconds -gt 90) { throw 'Unreal test fixture did not become ready' }
        Start-Sleep -Milliseconds 500
    }
    $info = New-LinkProcessInfo "$AfterEffectsRoot\Support Files\AfterFX.exe" @('-noui','-r',"$root\Tests\AfterEffectsHost.jsx") $root
    $info.Environment['UNREAL_AE_LINK_DIAGNOSTIC_DIR'] = "$root\artifacts"
    $adobe = [System.Diagnostics.Process]::Start($info)
    $adobeOut = $adobe.StandardOutput.ReadToEndAsync(); $adobeErr = $adobe.StandardError.ReadToEndAsync()
    while (!$adobe.HasExited) {
        if (([DateTime]::UtcNow - $begin).TotalSeconds -gt $TimeoutSeconds) { throw 'Adobe host test timed out; inspect application/startup status and artifacts logs' }
        Start-Sleep -Milliseconds 500
    }
    $adobe.WaitForExit()
    ($adobeOut.GetAwaiter().GetResult() + $adobeErr.GetAwaiter().GetResult()) | Set-Content "$root\artifacts\adobe-console.log"
    if ($adobe.ExitCode -ne 0 -or (Test-Path -LiteralPath "$root\artifacts\AfterEffectsHostError.aep")) { throw "Adobe host script failed: exit=$($adobe.ExitCode)" }
    $log = Get-Content "$root\artifacts\ae-native.log" -Raw
    if ($log -notmatch 'REGISTER_OK' -or $log -notmatch 'GLOBAL_SETUP' -or $log -notmatch 'RENDER .*sequence=[1-9]') { throw 'Native effect did not report a received frame rendered by Adobe' }
    foreach ($name in @('ae-beauty-8.png','ae-beauty-16.png','ae-beauty-32.png','ae-frozen-a.png','ae-frozen-b.png','ae-half.png','ae-disconnected-final.png')) {
        if (!(Test-Path -LiteralPath "$root\artifacts\$name")) { throw "Missing Adobe rendered image $name" }
    }
    if ((Get-FileHash "$root\artifacts\ae-frozen-a.png").Hash -ne (Get-FileHash "$root\artifacts\ae-frozen-b.png").Hash) { throw 'Frozen frames differ' }
    Write-Host $log
    Write-Host 'PASS: native .aex loaded and rendered GPU-received Beauty inside Adobe at 8/16/32 bpc, freeze/resume and disconnect.'
} finally {
    foreach ($child in @($adobe,$editor)) {
        if ($null -ne $child -and !$child.HasExited) { $child.Kill(); $child.WaitForExit() }
    }
    if ($null -ne $adobe) {
        ($adobeOut.GetAwaiter().GetResult() + $adobeErr.GetAwaiter().GetResult()) | Set-Content "$root\artifacts\adobe-console.log"
        $adobe.Dispose()
    }
    if ($null -ne $editor) {
        ($editorOut.GetAwaiter().GetResult() + $editorErr.GetAwaiter().GetResult()) | Set-Content "$root\artifacts\editor-adobe-console.log"
        $editor.Dispose()
    }
}

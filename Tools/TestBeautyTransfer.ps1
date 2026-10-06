param([string]$EngineRoot, [int]$TimeoutSeconds = 180)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\InvokeNative.ps1"
$root = Split-Path -Parent $PSScriptRoot
$engine = & "$PSScriptRoot\FindEngine.ps1" -EngineRoot $EngineRoot
$project = "$root\Tests\UnrealAELinkTest\UnrealAELinkTest.uproject"
$receiverPath = "$root\build\Tools\ReceiverTest\Release\ReceiverTest.exe"
if (!(Test-Path -LiteralPath $receiverPath)) { throw 'BuildNative.ps1 must succeed first' }
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { throw 'Close existing Unreal editors before this isolated acceptance test' }
New-Item -ItemType Directory -Force -Path "$root\artifacts" | Out-Null
$receiver = $null
$editor = $null
$editorExitObserved = $null
try {
    $receiver = [System.Diagnostics.Process]::Start((New-LinkProcessInfo $receiverPath @('--gpu','--frames','30','--expect-change','--expect-motion','--snapshot',"$root\artifacts\beauty.bmp",'--timeout',"$TimeoutSeconds") $root))
    $receiverOut = $receiver.StandardOutput.ReadToEndAsync()
    $receiverErr = $receiver.StandardError.ReadToEndAsync()
    $editorArgs = @($project, '-unattended', '-nosplash', '-NoSound', '-RenderOffscreen', '-dx12',
        '-ExecCmds=Automation RunTests UnrealAELink.Beauty.FrameTransfer',
        '-TestExit=Automation Test Queue Empty', "-abslog=$root\artifacts\editor-beauty.log")
    $editor = [System.Diagnostics.Process]::Start((New-LinkProcessInfo "$engine\Engine\Binaries\Win64\UnrealEditor.exe" $editorArgs $root))
    $editorOut = $editor.StandardOutput.ReadToEndAsync()
    $editorErr = $editor.StandardError.ReadToEndAsync()
    $begin = [DateTime]::UtcNow
    while (!$editor.HasExited -or !$receiver.HasExited) {
        if (([DateTime]::UtcNow - $begin).TotalSeconds -gt $TimeoutSeconds + 15) { throw 'Editor camera acceptance test timed out' }
        if ($editor.HasExited -and !$receiver.HasExited) {
            if ($null -eq $editorExitObserved) { $editorExitObserved = [DateTime]::UtcNow }
            if (([DateTime]::UtcNow - $editorExitObserved).TotalSeconds -gt 3) {
                throw 'Unreal exited before ReceiverTest received the required changing Beauty frames'
            }
        }
        Start-Sleep -Milliseconds 500
    }
    $receiver.WaitForExit()
    $editor.WaitForExit()
    $receiverText = $receiverOut.GetAwaiter().GetResult() + $receiverErr.GetAwaiter().GetResult()
    $receiverText | Set-Content -LiteralPath "$root\artifacts\receiver-beauty.log" -Encoding utf8
    ($editorOut.GetAwaiter().GetResult() + $editorErr.GetAwaiter().GetResult()) | Set-Content -LiteralPath "$root\artifacts\beauty-console.log" -Encoding utf8
    Write-Host $receiverText
    if ($receiver.ExitCode -ne 0 -or $editor.ExitCode -ne 0) {
        throw "Acceptance failed: Receiver=$($receiver.ExitCode), Editor=$($editor.ExitCode). See artifacts logs."
    }
    $editorLog = Get-Content -LiteralPath "$root\artifacts\editor-beauty.log" -Raw
    if ($editorLog -notmatch 'Result=\{Success\}.*FrameTransfer' -and $editorLog -notmatch 'Test Completed.*Success.*FrameTransfer') {
        throw 'Receiver succeeded but Unreal automation success was not found; inspect editor-beauty.log'
    }
    Write-Host 'PASS: Unreal SceneCapture -> shared DX12 textures -> separate ReceiverTest process. Logs are in artifacts/.'
}
finally {
    # Only terminate processes this script itself created.
    foreach ($child in @($receiver, $editor)) {
        if ($null -ne $child) {
            if (!$child.HasExited) { $child.Kill(); $child.WaitForExit() }
        }
    }
    # Preserve output on failures/timeouts too, after our children are stopped.
    if ($null -ne $receiver) {
        ($receiverOut.GetAwaiter().GetResult() + $receiverErr.GetAwaiter().GetResult()) | Set-Content -LiteralPath "$root\artifacts\receiver-beauty.log" -Encoding utf8
        $receiver.Dispose()
    }
    if ($null -ne $editor) {
        ($editorOut.GetAwaiter().GetResult() + $editorErr.GetAwaiter().GetResult()) | Set-Content -LiteralPath "$root\artifacts\beauty-console.log" -Encoding utf8
        $editor.Dispose()
    }
}


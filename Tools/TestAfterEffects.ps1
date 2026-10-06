param([string]$EngineRoot, [string]$AfterEffectsRoot = 'C:\Program Files\Adobe\Adobe After Effects 2026', [int]$TimeoutSeconds = 180)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\InvokeNative.ps1"
. "$PSScriptRoot\InspectAdobeImage.ps1"
$root = Split-Path -Parent $PSScriptRoot
$engine = & "$PSScriptRoot\FindEngine.ps1" -EngineRoot $EngineRoot
if (Get-Process UnrealEditor,AfterFX,ReceiverTest,GpuTests -ErrorAction SilentlyContinue) {
    throw 'Close running Unreal editors, After Effects and GPU receivers before this isolated acceptance test'
}
$installed = "$AfterEffectsRoot\Support Files\Plug-ins\UnrealAELink\UnrealAELink.aex"
if (!(Test-Path -LiteralPath $installed)) { throw 'InstallAfterEffects must succeed first' }
New-Item -ItemType Directory -Force -Path "$root\artifacts" | Out-Null
$frameNames = @('ae-disconnected','ae-beauty-8','ae-beauty-16','ae-beauty-32','ae-frozen-a','ae-frozen-b','ae-half','ae-disconnected-final')
# Only test-owned previous artifacts are removed; exact paths, no recursive delete.
$oldFiles = @('editor-adobe.log','ae-native.log','AfterEffectsHostError.aep','UnrealAELinkDemo.aep')
for ($i = 0; $i -lt $frameNames.Count; ++$i) {
    $oldFiles += "$($frameNames[$i]).png"
    $oldFiles += "$($frameNames[$i]).$('{0:D5}' -f $i).tif"
}
$oldFiles += 'ae-disconnected.tif00000' # failed export from the original harness
foreach ($name in $oldFiles) {
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
    # Adobe 26.5 crashes during -noui startup on this machine, even with a
    # minimal script that never applies the plugin. Use normal startup with
    # the window hidden by New-LinkProcessInfo; the JSX exits its own instance.
    $info = New-LinkProcessInfo "$AfterEffectsRoot\Support Files\AfterFX.exe" @('-r',"$root\Tests\AfterEffectsHost.jsx") $root
    $info.Environment['UNREAL_AE_LINK_DIAGNOSTIC_DIR'] = "$root\artifacts"
    $adobe = [System.Diagnostics.Process]::Start($info)
    $adobeOut = $adobe.StandardOutput.ReadToEndAsync(); $adobeErr = $adobe.StandardError.ReadToEndAsync()
    while (!$adobe.HasExited) {
        if (([DateTime]::UtcNow - $begin).TotalSeconds -gt $TimeoutSeconds) { throw 'Adobe host test timed out; inspect application/startup status and artifacts logs' }
        Start-Sleep -Milliseconds 500
    }
    $adobe.WaitForExit()
    ($adobeOut.GetAwaiter().GetResult() + $adobeErr.GetAwaiter().GetResult()) | Set-Content "$root\artifacts\adobe-console.log"
    if ($adobe.ExitCode -ne 0) {
        $hex = '{0:X8}' -f ([BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$adobe.ExitCode), 0))
        throw "Adobe process failed: exit=$($adobe.ExitCode) (0x$hex). Inspect artifacts\adobe-console.log and artifacts\ae-native.log."
    }
    $errorProject = "$root\artifacts\AfterEffectsHostError.aep"
    if (Test-Path -LiteralPath $errorProject) {
        $errorText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($errorProject))
        $message = [regex]::Match($errorText, 'FAIL ([^\x00]{1,150}?)(?:\x00|LIST)')
        if ($message.Success) { throw "Adobe host script failed: $($message.Groups[1].Value)" }
        throw 'Adobe host script failed; open artifacts\AfterEffectsHostError.aep and inspect its FAIL composition.'
    }
    $log = Get-Content "$root\artifacts\ae-native.log" -Raw
    # Adobe may use its plugin discovery cache without calling registration.
    if ($log -notmatch 'GLOBAL_SETUP' -or $log -notmatch 'RENDER .*sequence=[1-9]') { throw 'Native effect did not report a received frame rendered by Adobe' }
    foreach ($format in @(1650946657,909206881,842229089)) {
        if ($log -notmatch "RENDER format=$format sequence=[1-9]") { throw "Missing received-frame render at Adobe pixel format $format" }
    }
    $images = @{}
    for ($i = 0; $i -lt $frameNames.Count; ++$i) {
        $name = $frameNames[$i]
        $source = "$root\artifacts\$name.$('{0:D5}' -f $i).tif"
        if (!(Test-Path -LiteralPath $source)) { throw "Missing Adobe rendered image $source" }
        $image = Read-LinkAdobeImage -Path $source -PreviewPath "$root\artifacts\$name.png"
        $width = if ($name -eq 'ae-half') {640} else {1280}
        $height = if ($name -eq 'ae-half') {360} else {720}
        if ($image.Width -ne $width -or $image.Height -ne $height -or !$image.Opaque) { throw "Incorrect output dimensions/alpha in $name" }
        if ($name -like 'ae-disconnected*') {
            if ($image.ColoredSamples) { throw "Disconnected output is not black: $name" }
        } elseif ($image.ColoredSamples -lt 10) { throw "Adobe output has no usable Beauty image: $name" }
        $images[$name] = $image
        Write-Host "$name`: $($image.Width)x$($image.Height), colored samples=$($image.ColoredSamples), pixels=$($image.Hash)"
    }
    if ($images['ae-frozen-a'].Hash -ne $images['ae-frozen-b'].Hash) { throw 'Frozen frame pixels differ' }
    if ($images['ae-disconnected'].Hash -ne $images['ae-disconnected-final'].Hash) { throw 'Disconnect did not restore the initial black image' }
    if (!(Test-Path -LiteralPath "$root\artifacts\UnrealAELinkDemo.aep")) { throw 'Adobe demo project was not saved' }
    Write-Host (($log -split '\r?\n' | Where-Object { $_ -match '^(REGISTER|GLOBAL|RENDER)' }) -join [Environment]::NewLine)
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

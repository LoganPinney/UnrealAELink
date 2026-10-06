param([Parameter(Mandatory)][string]$Artifacts)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\InspectAdobeImage.ps1"
$ue = Get-Content -LiteralPath "$Artifacts\editor.log" -Raw
$ae = Get-Content -LiteralPath "$Artifacts\ae-native.log" -Raw
$requests = Get-Content -LiteralPath "$Artifacts\ae-request.log" -Raw
if ($ae -match 'REQUEST FAILED' -or $requests -match 'TIMEOUT|CANCEL') { throw 'A deterministic request failed or timed out' }
$renders = [regex]::Matches($ae,'AE RENDER id=(\d+) time=(-?\d+)/(\d+) beauty=(\d+) hash=(\d+)')
if ($renders.Count -lt 41) { throw "Only $($renders.Count) actual deterministic Adobe render callbacks; expected at least 41" }
$identities = @()
foreach ($render in $renders) {
    $id = $render.Groups[1].Value; $value = $render.Groups[2].Value; $scale = $render.Groups[3].Value; $beauty = $render.Groups[4].Value
    $time = [double]$value / [double]$scale
    if ($requests -notmatch "AE REQUEST id=$id time=$value/$scale " -or
        $requests -notmatch "AE RESPONSE id=$id time=$value/$scale beauty=$beauty status=1") { throw "AE request/response identity mismatch id=$id" }
    $evaluation = [regex]::Match($ue,"SEQUENCER id=$id time=$value/$scale evaluated=([^ ]+) rate=([^ ]+) seconds=([0-9.]+)")
    $state = [regex]::Match($ue,"STATE id=$id binding=DeterministicCube position=\(([0-9.-]+),([0-9.-]+),([0-9.-]+)\)")
    $publish = [regex]::Match($ue,"PUBLISH id=$id beauty=$beauty session=(\d+)")
    $capture = [regex]::Match($ue,"CAPTURE id=$id(?:\r?\n|\s)")
    if (!$evaluation.Success -or !$state.Success -or !$publish.Success -or !$capture.Success) { throw "Missing UE chain for id=$id" }
    if ($capture.Index -le $state.Index -or $publish.Index -le $capture.Index) { throw "Evaluation/capture/publication ordering incorrect id=$id" }
    if ([Math]::Abs([double]$evaluation.Groups[3].Value - $time) -gt 0.000001) { throw "UE evaluated wrong time id=$id" }
    $expectedY = -240 + 240*$time
    if ([Math]::Abs([double]$state.Groups[2].Value - $expectedY) -gt 0.001 -or
        [Math]::Abs([double]$state.Groups[1].Value - 600) -gt 0.001) { throw "Sequencer bound actor has wrong transform id=$id" }
    $identities += [pscustomobject]@{RequestId=$id;TimeValue=$value;TimeScale=$scale;Seconds=$time;EvaluatedFrame=$evaluation.Groups[1].Value;EvaluatedRate=$evaluation.Groups[2].Value;CubeY=$state.Groups[2].Value;BeautySequence=$beauty;BeautySession=$publish.Groups[1].Value;SourcePixelFnv=$render.Groups[5].Value}
}
# Check actual decoded Adobe pixels, beyond the count of exported image files.
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
public static class LinkCubePixels {
    public static ulong Hash(byte[] pixels) {
        ulong hash=14695981039346656037UL;
        unchecked {
            for(int p=0;p<pixels.Length;p+=4)
                for(int c=2;c>=0;--c) { hash^=pixels[p+c]; hash*=1099511628211UL; }
        }
        return hash;
    }
    public static double[] Measure(byte[] pixels, int width, int height) {
                double sumX=0, sumY=0, count=0;
                for(int y=0;y<height;++y) {
                    for(int x=0;x<width;++x) {
                        int p=(y*width+x)*4;
                        if(pixels[p]+pixels[p+1]+pixels[p+2]>90) {sumX+=x;sumY+=y;++count;}
                    }
                }
                return new double[]{count, count>0?sumX/count:-1, count>0?sumY/count:-1};
    }
}
'@
$outputs = @()
foreach ($n in @(0,30,60)) { $outputs += [pscustomobject]@{Name="known-$n";Frame=$n;Seconds=$n/30.0;File="known-$n.$('{0:D5}' -f $n).tif"} }
foreach ($n in 0..30) { $outputs += [pscustomobject]@{Name="sequential-$n";Frame=$n;Seconds=$n/30.0;File="sequential.$('{0:D5}' -f $n).tif"} }
foreach ($n in @(20,3,17,0,29)) { $outputs += [pscustomobject]@{Name="random-$n";Frame=$n;Seconds=$n/30.0;File="random-$n.$('{0:D5}' -f $n).tif"} }
$outputs += [pscustomobject]@{Name='subframe';Frame=0.5;Seconds=1/60.0;File='subframe.00001.tif'}
$ntscFiles = @(Get-ChildItem -LiteralPath $Artifacts -Filter 'ntsc.*.tif' -File)
if ($ntscFiles.Count -ne 1) { throw 'Expected exactly one freshly rendered NTSC test image' }
$outputs += [pscustomobject]@{Name='ntsc';Frame=17.017;Seconds=17*1001/30000.0;File=$ntscFiles[0].Name}
$evidence = @(); $positions = @{}; $hashes = @{}
foreach ($output in $outputs) {
    $path = Join-Path $Artifacts $output.File
    if (!(Test-Path -LiteralPath $path)) { throw "Missing AE Render Queue image: $path" }
    $image = Read-LinkAdobeImage -Path $path -PreviewPath "$Artifacts\$($output.Name).png" -IncludePixels
    if ($image.Width -ne 1280 -or $image.Height -ne 720 -or !$image.Opaque) { throw "Invalid Adobe image dimensions/alpha: $($output.Name)" }
    $pixels = [LinkCubePixels]::Measure($image.Pixels, $image.Width, $image.Height)
    # The front face sits at x=550cm. A perspective projection provides an
    # independent position oracle, with tolerance for the cube's visible side.
    $expectedX = 640 + (-240+240*$output.Seconds) * (640/[Math]::Tan(35*[Math]::PI/180)) / 550
    # Lighting biases the bright-pixel centroid toward the cube's upper face.
    # Its projected 50cm half-height at the 550cm front face is about 83px.
    if ($pixels[0] -lt 1000 -or [Math]::Abs($pixels[1]-$expectedX) -gt 45 -or [Math]::Abs($pixels[2]-360) -gt 85) {
        throw "Wrong rendered cube state $($output.Name): count=$($pixels[0]) centroid=($($pixels[1]),$($pixels[2])) expectedX=$expectedX"
    }
    $sourceHash = [LinkCubePixels]::Hash($image.Pixels).ToString()
    $identity = $identities | Where-Object { [Math]::Abs($_.Seconds-$output.Seconds) -lt 0.000001 -and $_.SourcePixelFnv -eq $sourceHash } | Select-Object -Last 1
    if (!$identity) { throw "No AE/UE identity with matching full-image RGB hash $sourceHash for output time $($output.Seconds)" }
    $positions[$output.Name] = $pixels[1]; $hashes[$output.Name] = $image.Hash
    $evidence += [pscustomobject]@{Output=$output.Name;Seconds=$output.Seconds;TimeValue=$identity.TimeValue;TimeScale=$identity.TimeScale;RequestId=$identity.RequestId;EvaluatedFrame=$identity.EvaluatedFrame;EvaluatedRate=$identity.EvaluatedRate;CubeY=$identity.CubeY;BeautySequence=$identity.BeautySequence;BeautySession=$identity.BeautySession;PixelCentroidX=$pixels[1];SourcePixelFnv=$sourceHash;Sha256=$image.Hash}
    Write-Host "$($output.Name): AE=$($identity.TimeValue)/$($identity.TimeScale) request=$($identity.RequestId) UE=$($identity.EvaluatedFrame) Beauty=$($identity.BeautySequence) cubeY=$($identity.CubeY) pixelX=$([Math]::Round($pixels[1],3))"
}
foreach ($n in 1..30) {
    if ($positions["sequential-$n"] -le $positions["sequential-$($n-1)"] + 5) { throw "Sequential animation failed to advance at frame $n" }
}
foreach ($n in @(20,3,17,0,29)) {
    if ([Math]::Abs($positions["random-$n"]-$positions["sequential-$n"]) -gt 1) { throw "Random access disagrees with sequential render at frame $n" }
}
if ($positions['known-0'] -ge 400 -or [Math]::Abs($positions['known-30']-640) -gt 30 -or $positions['known-60'] -le 880) { throw 'Known left/center/right renders failed' }
if ($hashes['known-0'] -eq $hashes['known-30'] -or $hashes['known-30'] -eq $hashes['known-60']) { throw 'Different requested times returned identical pixels' }
$evidence | Export-Csv -LiteralPath "$Artifacts\evidence.csv" -NoTypeInformation
$identities | Export-Csv -LiteralPath "$Artifacts\request-identities.csv" -NoTypeInformation
Write-Host 'PASS: actual AE -> rational request -> direct Sequencer evaluation -> completed tagged DX12 Beauty -> native AE render; known times, 31-frame temporal progression, random access, subframe and NTSC.'

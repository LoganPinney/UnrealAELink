param([Parameter(Mandatory)][string]$Compiler, [Parameter(Mandatory)][string]$SDKRoot,
    [Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\InvokeNative.ps1"
function Write-Preprocessed([string[]]$Arguments, [string]$Destination) {
    $child = [System.Diagnostics.Process]::Start((New-LinkProcessInfo $Compiler $Arguments))
    try {
        $stdout = $child.StandardOutput.ReadToEndAsync()
        $stderr = $child.StandardError.ReadToEndAsync()
        $child.WaitForExit()
        if ($child.ExitCode) { throw ($stderr.GetAwaiter().GetResult()) }
        [System.IO.File]::WriteAllText($Destination, $stdout.GetAwaiter().GetResult(), [System.Text.Encoding]::ASCII)
    } finally { $child.Dispose() }
}
$rr = [System.IO.Path]::ChangeExtension($Output, '.rr')
$rrc = [System.IO.Path]::ChangeExtension($Output, '.rrc')
Write-Preprocessed @('/nologo','/EP','/TC',"/I$SDKRoot\Examples\Headers",$Source) $rr
Invoke-LinkNative "$SDKRoot\Examples\Resources\PiPLtool.exe" @($rr,$rrc)
Write-Preprocessed @('/nologo','/EP','/TC','/DMSWindows', $rrc) $Output

# Windows normally treats environment names as case-insensitive. Some launchers
# supply both PATH and Path; MSBuild/.NET Framework cannot consume that block.
# Normalize only the CHILD environment. Do not change machine or user settings.
function New-LinkProcessInfo {
    param([string]$Executable, [string[]]$Arguments, [string]$WorkingDirectory)
    $processInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $processInfo.FileName = $Executable
    $processInfo.UseShellExecute = $false
    $processInfo.CreateNoWindow = $true
    $processInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    if ($WorkingDirectory) { $processInfo.WorkingDirectory = $WorkingDirectory }
    $normalized = [System.Collections.Generic.Dictionary[string,string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()) {
        $normalized[$entry.Key] = $entry.Value
    }
    $processInfo.Environment.Clear()
    foreach ($entry in $normalized.GetEnumerator()) { $processInfo.Environment[$entry.Key] = $entry.Value }
    foreach ($argument in $Arguments) { $processInfo.ArgumentList.Add($argument) }
    return $processInfo
}

function Invoke-LinkNative {
    param([string]$Executable, [string[]]$Arguments, [string]$WorkingDirectory)
    $child = [System.Diagnostics.Process]::Start((New-LinkProcessInfo $Executable $Arguments $WorkingDirectory))
    $stdoutTask = $child.StandardOutput.ReadToEndAsync()
    $stderrTask = $child.StandardError.ReadToEndAsync()
    $child.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    if ($stdout) { Write-Host $stdout }
    if ($stderr) { Write-Host $stderr }
    $exitCode = $child.ExitCode
    $child.Dispose()
    if ($exitCode) { throw "$Executable failed with exit code $exitCode" }
}

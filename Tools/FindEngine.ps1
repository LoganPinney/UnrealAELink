param([string]$EngineRoot)
$ErrorActionPreference = 'Stop'
if ($EngineRoot) {
    if (!(Test-Path -LiteralPath "$EngineRoot\Engine\Build\Build.version")) { throw "Not an Unreal install: $EngineRoot" }
    return (Resolve-Path -LiteralPath $EngineRoot).Path
}
$candidates = [System.Collections.Generic.List[string]]::new()
$manifest = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat'
if (Test-Path -LiteralPath $manifest) {
    foreach ($entry in (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).InstallationList) {
        if ($entry.AppName -match '^UE_' -and $entry.InstallLocation) { $candidates.Add($entry.InstallLocation) }
    }
}
$registered = Get-ItemProperty 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
if ($registered) {
    foreach ($property in $registered.PSObject.Properties) {
        if ($property.Name -notmatch '^PS' -and $property.Value -is [string]) { $candidates.Add($property.Value) }
    }
}
$engines = foreach ($candidate in ($candidates | Select-Object -Unique)) {
    $versionPath = Join-Path $candidate 'Engine\Build\Build.version'
    if (Test-Path -LiteralPath $versionPath) {
        $version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
        [pscustomobject]@{ Root=$candidate; Preferred=($version.MajorVersion -eq 5 -and $version.MinorVersion -eq 8); Version=[version]::new($version.MajorVersion,$version.MinorVersion,$version.PatchVersion) }
    }
}
$selected = $engines | Sort-Object Preferred,Version -Descending | Select-Object -First 1
if (!$selected) { throw 'No registered Unreal Engine found. Pass -EngineRoot with the installation directory.' }
$selected.Root

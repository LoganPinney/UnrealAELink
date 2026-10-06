param([string]$AfterEffectsRoot = 'C:\Program Files\Adobe\Adobe After Effects 2026')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = "$root\AfterEffectsPlugin\UnrealAELink\Binaries\Win64\UnrealAELink.aex"
$directory = Join-Path $AfterEffectsRoot 'Support Files\Plug-ins\UnrealAELink'
$destination = Join-Path $directory 'UnrealAELink.aex'
if (!(Test-Path -LiteralPath $source)) { throw 'BuildAfterEffects must succeed first' }
if (!(Test-Path -LiteralPath "$AfterEffectsRoot\Support Files\AfterFX.exe")) { throw 'AfterFX.exe was not found at the selected installation' }
if (Get-Process AfterFX -ErrorAction SilentlyContinue) { throw 'Close After Effects before installing/updating the prototype .aex' }
if (Test-Path -LiteralPath $destination) {
    if ((Get-FileHash $source).Hash -eq (Get-FileHash $destination).Hash) { Write-Host 'The same plugin build is already installed'; exit 0 }
    $backup = "$root\artifacts\ae-plugin-backup-$(Get-Date -Format yyyyMMdd-HHmmss).aex"
    New-Item -ItemType Directory -Force -Path "$root\artifacts" | Out-Null
    Copy-Item -LiteralPath $destination -Destination $backup
    Write-Host "Existing prototype build backed up to $backup"
}
New-Item -ItemType Directory -Force -Path $directory | Out-Null
Copy-Item -LiteralPath $source -Destination $destination
if ((Get-FileHash $source).Hash -ne (Get-FileHash $destination).Hash) { throw 'Installed plugin hash differs from built artifact' }
Write-Host "Installed one prototype plugin: $destination"

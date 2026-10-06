# Read Adobe-rendered test artifacts with Windows' image decoder. PNGs are
# previews created after host rendering, never part of the GPU transport.
function Read-LinkAdobeImage {
    param([string]$Path, [string]$PreviewPath, [switch]$IncludePixels)
    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::new($Path)
    try {
        $rectangle = [System.Drawing.Rectangle]::new(0, 0, $bitmap.Width, $bitmap.Height)
        $data = $bitmap.LockBits($rectangle, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $rowBytes = $bitmap.Width * 4
            $pixels = [byte[]]::new($rowBytes * $bitmap.Height)
            for ($y = 0; $y -lt $bitmap.Height; ++$y) {
                [System.Runtime.InteropServices.Marshal]::Copy(
                    [IntPtr]::Add($data.Scan0, $y * $data.Stride), $pixels, $y * $rowBytes, $rowBytes)
            }
        } finally { $bitmap.UnlockBits($data) }
        $hash = [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($pixels))
        # A grid catches blank/transparent exports; the native log independently
        # records the full received frame's colored-pixel count.
        $colored = 0; $opaque = $true
        for ($y = 0; $y -lt $bitmap.Height; $y += 10) {
            for ($x = 0; $x -lt $bitmap.Width; $x += 10) {
                $color = $bitmap.GetPixel($x, $y)
                if ($color.R -or $color.G -or $color.B) { ++$colored }
                if ($color.A -ne 255) { $opaque = $false }
            }
        }
        $bitmap.Save($PreviewPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $result = [pscustomobject]@{ Width=$bitmap.Width; Height=$bitmap.Height; Hash=$hash; ColoredSamples=$colored; Opaque=$opaque; Pixels=$null }
        if ($IncludePixels) { $result.Pixels = $pixels }
        return $result
    } finally { $bitmap.Dispose() }
}

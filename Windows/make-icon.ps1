[CmdletBinding()]
param(
    # Monk frame from Resources\faces.png (0..29, same numbering as the app;
    # 20 is mid-song with the mouth open).
    [ValidateRange(0, 29)]
    [int]$Frame = 20,
    [string]$OutputPath = ''
)

# Regenerates delay_lama.ico, the exe/window icon, from the monk artwork.
# The result is checked in, so the build does not need this script.

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing

$windowsRoot = $PSScriptRoot
$facesPath = Join-Path $windowsRoot "..\Resources\faces.png"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $windowsRoot "delay_lama.ico"
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)

# Sheet layout as in app.h: 314x311 cells, 6 per column, column-major; the
# 311x291 frame is the cell minus its 3-px right border and the nameplate.
$cellWidth = 314
$cellHeight = 311
$rowsPerColumn = 6
$cellX = [Math]::Floor($Frame / $rowsPerColumn) * $cellWidth
$cellY = ($Frame % $rowsPerColumn) * $cellHeight

# Frame-relative squares: the whole monk for large icons, his head (and
# praying hands) for the small ones, where the full figure turns to mush.
# The full square stops short of the patch of sky in the frame's bottom-right
# corner (269..284, 266..290).
$fullCrop = New-Object System.Drawing.Rectangle(4, 0, 264, 264)
$headCrop = New-Object System.Drawing.Rectangle(52, 0, 176, 176)
$headCropMaxSize = 32

# Sizes cover 100%..250% DPI for the shell, taskbar and title bars. 256 is
# stored as PNG (the usual convention); the rest are 32-bit DIBs.
$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 256)

function New-IconImage {
    param(
        [Parameter(Mandatory = $true)][System.Drawing.Image]$Sheet,
        [Parameter(Mandatory = $true)][System.Drawing.Rectangle]$Crop,
        [Parameter(Mandatory = $true)][int]$Size
    )

    # Scale first (HighQualityBicubic prefilters), then fill an anti-aliased
    # rounded square with the scaled picture.
    $scaled = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($scaled)
    $attributes = New-Object System.Drawing.Imaging.ImageAttributes
    try {
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        # Keeps the edge pixels from blending with transparent black.
        $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
        $destination = New-Object System.Drawing.Rectangle(0, 0, $Size, $Size)
        $graphics.DrawImage($Sheet, $destination, $cellX + $Crop.X, $cellY + $Crop.Y,
            $Crop.Width, $Crop.Height, [System.Drawing.GraphicsUnit]::Pixel, $attributes)
    } finally {
        $attributes.Dispose()
        $graphics.Dispose()
    }

    $icon = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($icon)
    $brush = New-Object System.Drawing.TextureBrush($scaled)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    try {
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $diameter = [Math]::Max(4.0, $Size / 4.0)
        $far = $Size - $diameter
        $path.AddArc(0, 0, $diameter, $diameter, 180, 90)
        $path.AddArc($far, 0, $diameter, $diameter, 270, 90)
        $path.AddArc($far, $far, $diameter, $diameter, 0, 90)
        $path.AddArc(0, $far, $diameter, $diameter, 90, 90)
        $path.CloseFigure()
        $graphics.FillPath($brush, $path)
    } finally {
        $path.Dispose()
        $brush.Dispose()
        $graphics.Dispose()
        $scaled.Dispose()
    }
    return $icon
}

function Get-PngBytes {
    param([Parameter(Mandatory = $true)][System.Drawing.Bitmap]$Bitmap)
    $stream = New-Object System.IO.MemoryStream
    try {
        $Bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return ,$stream.ToArray()
    } finally {
        $stream.Dispose()
    }
}

# BITMAPINFOHEADER (height doubled for the AND mask), bottom-up BGRA rows,
# then a 1-bpp AND mask that marks fully transparent pixels.
function Get-DibBytes {
    param([Parameter(Mandatory = $true)][System.Drawing.Bitmap]$Bitmap)
    $size = $Bitmap.Width
    $bounds = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
    $data = $Bitmap.LockBits($bounds, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $pixels = New-Object byte[] ($size * $size * 4)
    try {
        for ($y = 0; $y -lt $size; $y++) {
            $row = [IntPtr]::Add($data.Scan0, $y * $data.Stride)
            [System.Runtime.InteropServices.Marshal]::Copy($row, $pixels, $y * $size * 4, $size * 4)
        }
    } finally {
        $Bitmap.UnlockBits($data)
    }

    $maskStride = [Math]::Floor(($size + 31) / 32) * 4
    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter($stream)
    try {
        $writer.Write([UInt32]40)
        $writer.Write([Int32]$size)
        $writer.Write([Int32]($size * 2))
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]0)
        $writer.Write([UInt32]($size * $size * 4 + $maskStride * $size))
        $writer.Write([Int32]0)
        $writer.Write([Int32]0)
        $writer.Write([UInt32]0)
        $writer.Write([UInt32]0)
        for ($y = $size - 1; $y -ge 0; $y--) {
            $writer.Write($pixels, $y * $size * 4, $size * 4)
        }
        for ($y = $size - 1; $y -ge 0; $y--) {
            $mask = New-Object byte[] $maskStride
            for ($x = 0; $x -lt $size; $x++) {
                if ($pixels[($y * $size + $x) * 4 + 3] -eq 0) {
                    $mask[$x -shr 3] = $mask[$x -shr 3] -bor (0x80 -shr ($x -band 7))
                }
            }
            $writer.Write($mask)
        }
        $writer.Flush()
        return ,$stream.ToArray()
    } finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

$sheet = [System.Drawing.Image]::FromFile([IO.Path]::GetFullPath($facesPath))
$images = @()
try {
    foreach ($size in $sizes) {
        $crop = if ($size -le $headCropMaxSize) { $headCrop } else { $fullCrop }
        $bitmap = New-IconImage -Sheet $sheet -Crop $crop -Size $size
        try {
            $bytes = if ($size -ge 256) { Get-PngBytes -Bitmap $bitmap } else { Get-DibBytes -Bitmap $bitmap }
        } finally {
            $bitmap.Dispose()
        }
        $images += ,@($size, $bytes)
    }
} finally {
    $sheet.Dispose()
}

# ICONDIR, one ICONDIRENTRY per image, then the image data.
$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($stream)
try {
    $writer.Write([UInt16]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($image in $images) {
        $size = $image[0]
        $bytes = $image[1]
        $dimension = if ($size -ge 256) { 0 } else { $size }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$bytes.Length)
        $writer.Write([UInt32]$offset)
        $offset += $bytes.Length
    }
    foreach ($image in $images) {
        $writer.Write([byte[]]$image[1])
    }
    $writer.Flush()
    [IO.File]::WriteAllBytes($OutputPath, $stream.ToArray())
} finally {
    $writer.Dispose()
    $stream.Dispose()
}

Write-Host ("Wrote {0} (frame {1}, {2} sizes)" -f $OutputPath, $Frame, $images.Count)

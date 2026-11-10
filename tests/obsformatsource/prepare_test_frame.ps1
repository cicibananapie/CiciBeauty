# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSES/GPL-2.0-or-later.txt for the full license text.

param(
    [string]$InputImage = (Join-Path $PSScriptRoot "..\..\test-artifacts\video-frames\1.mp4.png"),
    [string]$OutputPath = (Join-Path $PSScriptRoot "format-test-frame-1280x720.bgra"),
    [ValidateRange(16, 8192)]
    [int]$Width = 1280,
    [ValidateRange(16, 4320)]
    [int]$Height = 720,
    [ValidateRange(1, 36000)]
    [int]$FrameCount = 1,
    [switch]$Blank
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

if (-not $Blank -and -not (Test-Path -LiteralPath $InputImage -PathType Leaf)) {
    throw "Input image not found: $InputImage"
}

$outputDirectory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    [void][System.IO.Directory]::CreateDirectory($outputDirectory)
}

$source = $null
$stream = $null
try {
    if (-not $Blank) {
        $source = [System.Drawing.Image]::FromFile($InputImage)
    }
    $stream = [System.IO.File]::Open(
        $OutputPath,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::Read)

    for ($frameIndex = 0; $frameIndex -lt $FrameCount; ++$frameIndex) {
        $bitmap = [System.Drawing.Bitmap]::new(
            $Width,
            $Height,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::FromArgb(255, 24, 27, 31))
            $graphics.InterpolationMode =
                [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode =
                [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

            if ($null -ne $source) {
                $scale = [Math]::Min(
                    ([Math]::Max(1, $Width - 160)) / $source.Width,
                    ([Math]::Max(1, $Height - 24)) / $source.Height)
                $drawWidth = [int][Math]::Round($source.Width * $scale)
                $drawHeight = [int][Math]::Round($source.Height * $scale)
                $drawX = [int](($Width - $drawWidth) / 2)
                $drawY = [int](($Height - $drawHeight) / 2)
                $graphics.DrawImage(
                    $source, $drawX, $drawY, $drawWidth, $drawHeight)
            }

            $markerSize = [Math]::Min(
                72, [Math]::Min([int]($Width / 4), [int]($Height / 4)))
            $graphics.FillRectangle(
                [System.Drawing.Brushes]::Red, 0, 0, $markerSize, $markerSize)
            $graphics.FillRectangle(
                [System.Drawing.Brushes]::LimeGreen,
                $Width - $markerSize,
                0,
                $markerSize,
                $markerSize)
            $graphics.FillRectangle(
                [System.Drawing.Brushes]::Blue,
                0,
                $Height - $markerSize,
                $markerSize,
                $markerSize)
            $graphics.FillRectangle(
                [System.Drawing.Brushes]::Gold,
                $Width - $markerSize,
                $Height - $markerSize,
                $markerSize,
                $markerSize)

            $fontSize = [Math]::Max(
                10, [Math]::Min(24, [int]($Height / 20)))
            $font = [System.Drawing.Font]::new(
                "Segoe UI",
                $fontSize,
                [System.Drawing.FontStyle]::Bold,
                [System.Drawing.GraphicsUnit]::Pixel)
            try {
                $textX = $markerSize + 18
                $graphics.DrawString(
                    "TOP", $font, [System.Drawing.Brushes]::White, $textX, 18)
                $graphics.DrawString(
                    "BOTTOM",
                    $font,
                    [System.Drawing.Brushes]::White,
                    $textX,
                    [Math]::Max(0, $Height - $fontSize - 20))
                $graphics.DrawString(
                    ("FRAME {0:D4}" -f $frameIndex),
                    $font,
                    [System.Drawing.Brushes]::White,
                    $textX,
                    [Math]::Max(0, $Height - $fontSize * 2 - 30))
            } finally {
                $font.Dispose()
            }

            if ($FrameCount -gt 1) {
                $barWidth = [Math]::Max(4, [int]($Width / 160))
                $travel = [Math]::Max(0, $Width - $barWidth)
                $barX = [int][Math]::Round(
                    $travel * $frameIndex / [Math]::Max(1, $FrameCount - 1))
                $graphics.FillRectangle(
                    [System.Drawing.Brushes]::Cyan,
                    $barX,
                    [Math]::Max(0, [int]($Height / 2) - 20),
                    $barWidth,
                    40)
            }

            $graphics.Flush()
            $rectangle = [System.Drawing.Rectangle]::new(0, 0, $Width, $Height)
            $data = $bitmap.LockBits(
                $rectangle,
                [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try {
                $rowBytes = $Width * 4
                $row = [byte[]]::new($rowBytes)
                $stride = $data.Stride
                for ($y = 0; $y -lt $Height; ++$y) {
                    $memoryRow = if ($stride -ge 0) { $y } else { $Height - 1 - $y }
                    $pointer = [IntPtr]::Add(
                        $data.Scan0,
                        $memoryRow * [Math]::Abs($stride))
                    [Runtime.InteropServices.Marshal]::Copy(
                        $pointer,
                        $row,
                        0,
                        $rowBytes)
                    $stream.Write($row, 0, $rowBytes)
                }
            } finally {
                $bitmap.UnlockBits($data)
            }
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally {
    if ($null -ne $stream) {
        $stream.Dispose()
    }
    if ($null -ne $source) {
        $source.Dispose()
    }
}

$file = Get-Item -LiteralPath $OutputPath
$expectedLength = [int64]$Width * $Height * 4 * $FrameCount
if ($file.Length -ne $expectedLength) {
    throw "Unexpected BGRA output size: $($file.Length), expected $expectedLength"
}

Write-Host (
    "Created {0} ({1} bytes, {2}x{3}, {4} frame(s))" -f
        $file.FullName, $file.Length, $Width, $Height, $FrameCount)

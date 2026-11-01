$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$iconDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'backend\web\icons'
New-Item -ItemType Directory -Force -Path $iconDirectory | Out-Null

function New-ScaledPoints {
    param(
        [float[]]$Coordinates,
        [int]$Size
    )

    $scale = $Size / 512.0
    $points = [System.Collections.Generic.List[System.Drawing.PointF]]::new()

    for ($index = 0; $index -lt $Coordinates.Count; $index += 2) {
        $points.Add([System.Drawing.PointF]::new(
            $Coordinates[$index] * $scale,
            $Coordinates[$index + 1] * $scale
        ))
    }

    return $points.ToArray()
}

function New-WeatherScopeIcon {
    param(
        [int]$Size,
        [string]$OutputPath
    )

    $scale = $Size / 512.0
    $bitmap = [System.Drawing.Bitmap]::new(
        $Size,
        $Size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    )
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

    $canvas = [System.Drawing.Rectangle]::new(0, 0, $Size, $Size)
    $background = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        $canvas,
        [System.Drawing.Color]::FromArgb(7, 13, 26),
        [System.Drawing.Color]::FromArgb(22, 52, 83),
        45.0
    )
    $graphics.FillRectangle($background, $canvas)

    $haloBrush = [System.Drawing.SolidBrush]::new(
        [System.Drawing.Color]::FromArgb(35, 77, 180, 225)
    )
    $graphics.FillEllipse(
        $haloBrush,
        [System.Drawing.RectangleF]::new(58 * $scale, 58 * $scale, 396 * $scale, 396 * $scale)
    )

    $orbitPen = [System.Drawing.Pen]::new(
        [System.Drawing.Color]::FromArgb(155, 135, 223, 255),
        2.4 * $scale
    )
    $graphics.TranslateTransform($Size / 2.0, $Size / 2.0)
    $graphics.RotateTransform(-24)
    $graphics.DrawEllipse(
        $orbitPen,
        [System.Drawing.RectangleF]::new(-190 * $scale, -80 * $scale, 380 * $scale, 160 * $scale)
    )
    $graphics.ResetTransform()

    $earthBounds = [System.Drawing.RectangleF]::new(
        112 * $scale,
        112 * $scale,
        288 * $scale,
        288 * $scale
    )
    $earthPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $earthPath.AddEllipse($earthBounds)
    $earthBrush = [System.Drawing.Drawing2D.PathGradientBrush]::new($earthPath)
    $earthBrush.CenterPoint = [System.Drawing.PointF]::new(190 * $scale, 180 * $scale)
    $earthBrush.CenterColor = [System.Drawing.Color]::FromArgb(132, 218, 255)
    $earthBrush.SurroundColors = [System.Drawing.Color[]]@(
        [System.Drawing.Color]::FromArgb(8, 29, 57)
    )
    $graphics.FillPath($earthBrush, $earthPath)

    $clip = $graphics.Save()
    $graphics.SetClip($earthBounds)

    $landBrush = [System.Drawing.SolidBrush]::new(
        [System.Drawing.Color]::FromArgb(188, 133, 211, 157)
    )
    $graphics.FillPolygon($landBrush, (New-ScaledPoints @(166, 175, 194, 151, 223, 158, 239, 176, 226, 193, 244, 211, 228, 229, 207, 222, 193, 203, 174, 196) $Size))
    $graphics.FillPolygon($landBrush, (New-ScaledPoints @(226, 222, 252, 230, 269, 252, 260, 279, 247, 302, 235, 330, 222, 311, 217, 281, 207, 256) $Size))
    $graphics.FillPolygon($landBrush, (New-ScaledPoints @(276, 175, 302, 162, 326, 174, 344, 169, 364, 186, 351, 201, 331, 196, 316, 210, 297, 198, 280, 202, 266, 190) $Size))
    $graphics.FillPolygon($landBrush, (New-ScaledPoints @(168, 228, 188, 219, 199, 235, 193, 255, 182, 265, 169, 252) $Size))
    $graphics.FillPolygon($landBrush, (New-ScaledPoints @(305, 260, 321, 253, 334, 266, 330, 281, 315, 282) $Size))

    $gridPen = [System.Drawing.Pen]::new(
        [System.Drawing.Color]::FromArgb(80, 192, 230, 255),
        1.4 * $scale
    )
    $graphics.DrawEllipse($gridPen, $earthBounds)
    $graphics.DrawEllipse(
        $gridPen,
        [System.Drawing.RectangleF]::new(188 * $scale, 112 * $scale, 136 * $scale, 288 * $scale)
    )
    $graphics.DrawArc(
        $gridPen,
        [System.Drawing.RectangleF]::new(114 * $scale, 184 * $scale, 284 * $scale, 140 * $scale),
        0,
        360
    )
    $graphics.Restore($clip)

    $atmospherePen = [System.Drawing.Pen]::new(
        [System.Drawing.Color]::FromArgb(180, 107, 218, 255),
        3 * $scale
    )
    $graphics.DrawEllipse($atmospherePen, $earthBounds)

    $markerGlow = [System.Drawing.SolidBrush]::new(
        [System.Drawing.Color]::FromArgb(90, 224, 255, 244)
    )
    $marker = [System.Drawing.SolidBrush]::new(
        [System.Drawing.Color]::FromArgb(255, 235, 255, 247)
    )
    $graphics.FillEllipse($markerGlow, 304 * $scale, 205 * $scale, 28 * $scale, 28 * $scale)
    $graphics.FillEllipse($marker, 312 * $scale, 213 * $scale, 12 * $scale, 12 * $scale)

    $satellite = [System.Drawing.SolidBrush]::new(
        [System.Drawing.Color]::FromArgb(255, 255, 218, 153)
    )
    $graphics.FillEllipse($satellite, 96 * $scale, 215 * $scale, 11 * $scale, 11 * $scale)

    $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    $background.Dispose()
    $haloBrush.Dispose()
    $orbitPen.Dispose()
    $earthBrush.Dispose()
    $earthPath.Dispose()
    $landBrush.Dispose()
    $gridPen.Dispose()
    $atmospherePen.Dispose()
    $markerGlow.Dispose()
    $marker.Dispose()
    $satellite.Dispose()
}

New-WeatherScopeIcon 180 (Join-Path $iconDirectory 'icon-180.png')
New-WeatherScopeIcon 192 (Join-Path $iconDirectory 'icon-192.png')
New-WeatherScopeIcon 512 (Join-Path $iconDirectory 'icon-512.png')

Write-Host "WeatherScope app icons created in $iconDirectory"


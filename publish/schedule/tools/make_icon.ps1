# 生成应用图标 src\app.ico：圆角蓝底 + 白色"日"字，含 16/32/48/256 多尺寸 BMP 帧
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot   # 项目根目录
$outIco = Join-Path $root 'src\app.ico'

function New-Frame([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size)
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $gfx.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias

    # 圆角矩形背景（蓝色渐变）
    $r = [Math]::Max(2, [int]($size * 0.22))
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($size - $d, 0, $d, $d, 270, 90)
    $path.AddArc($size - $d, $size - $d, $d, $d, 0, 90)
    $path.AddArc(0, $size - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Point(0, 0)), (New-Object System.Drawing.Point(0, $size)),
        [System.Drawing.Color]::FromArgb(255, 68, 122, 246),
        [System.Drawing.Color]::FromArgb(255, 47, 84, 205))
    $gfx.FillPath($brush, $path)

    # 白色日历顶条
    $barH = [Math]::Max(2, [int]($size * 0.08))
    $white = [System.Drawing.Brushes]::White
    $gfx.FillRectangle($white, [float]($size*0.18), [float]($size*0.24), [float]($size*0.64), [float]$barH)

    # 白色"日"字
    $fontSz = [Math]::Max(6, [int]($size * 0.46))
    $font = New-Object System.Drawing.Font('Microsoft YaHei UI', $fontSz, [System.Drawing.FontStyle]::Bold,
        [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = New-Object System.Drawing.RectangleF(0, ($size*0.04), $size, $size)
    $gfx.DrawString('日', $font, $white, $rect, $fmt)

    $gfx.Dispose(); $font.Dispose(); $brush.Dispose(); $path.Dispose()
    return $bmp
}

# 手工拼 ICO 容器：ICONDIR + 多个 ICONDIRENTRY + 各尺寸 BMP 数据
$sizes = @(16, 32, 48, 256)
$frames = @()
foreach ($s in $sizes) {
    $bmp = New-Frame $s
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)

    # BITMAPINFOHEADER (biHeight = 2x，含 AND mask)
    $bw.Write([UInt32]40); $bw.Write([Int32]$s); $bw.Write([Int32]($s * 2))
    $bw.Write([UInt16]1); $bw.Write([UInt16]32); $bw.Write([UInt32]0)
    $bw.Write([UInt32]($s * $s * 4)); $bw.Write([Int32]0); $bw.Write([Int32]0)
    $bw.Write([UInt32]0); $bw.Write([UInt32]0)

    $rect = New-Object System.Drawing.Rectangle(0, 0, $s, $s)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $row = New-Object byte[] ($s * 4)
    for ($y = $s - 1; $y -ge 0; $y--) {           # BMP 自底向上
        [System.Runtime.InteropServices.Marshal]::Copy(
            [IntPtr]($data.Scan0.ToInt64() + $y * $stride), $row, 0, $s * 4)
        $bw.Write($row)
    }
    $bmp.UnlockBits($data)
    $bw.Write((New-Object byte[] ($s * ($s / 4))))  # AND mask（全 0，alpha 通道生效）
    $bw.Flush()
    $frames += , @{ Size = $s; Bytes = $ms.ToArray() }
    $bw.Dispose(); $ms.Dispose(); $bmp.Dispose()
}

$fs = [System.IO.File]::Create($outIco)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$frames.Count)  # ICONDIR
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $s = $f.Size
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))   # width
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))   # height
    $bw.Write([byte]0); $bw.Write([byte]0)                     # 调色板
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)                # 平面/位深
    $bw.Write([UInt32]$f.Bytes.Length)
    $bw.Write([UInt32]$offset)
    $offset += $f.Bytes.Length
}
foreach ($f in $frames) { $bw.Write($f.Bytes) }
$bw.Flush(); $bw.Dispose(); $fs.Dispose()

Write-Host "图标已生成: $outIco ($(($frames | ForEach-Object { $_.Size }) -join '/'))"

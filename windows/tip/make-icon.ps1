# 產生輸入法圖示（16～256 像素，PNG 格式存進 ICO）：
#   ShunDa.ico：深綠色圓角方塊上白色「懂」字（輸入法清單、右下角中文模式）
#   ModeEn.ico：灰色圓角方塊上白色「英」字（右下角英文模式）
# 用法：powershell -ExecutionPolicy Bypass -File windows\tip\make-icon.ps1
Add-Type -AssemblyName System.Drawing
$sizes = 16, 20, 24, 32, 40, 48, 64, 256
function Make-Icon($char, $color, $file, $png) {
$pngs = @()
foreach ($s in $sizes) {
  $bmp = New-Object System.Drawing.Bitmap $s, $s
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = 'AntiAlias'
  $g.TextRenderingHint = 'AntiAliasGridFit'
  $r = [Math]::Max(2, [int]($s * 0.22))
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $path.AddArc(0, 0, $r * 2, $r * 2, 180, 90)
  $path.AddArc($s - $r * 2 - 1, 0, $r * 2, $r * 2, 270, 90)
  $path.AddArc($s - $r * 2 - 1, $s - $r * 2 - 1, $r * 2, $r * 2, 0, 90)
  $path.AddArc(0, $s - $r * 2 - 1, $r * 2, $r * 2, 90, 90)
  $path.CloseFigure()
  $g.FillPath((New-Object System.Drawing.SolidBrush $color), $path)
  $font = New-Object System.Drawing.Font 'Microsoft JhengHei', ([single]($s * 0.68)), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
  $fmt = New-Object System.Drawing.StringFormat
  $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
  $rect = New-Object System.Drawing.RectangleF 0, ([single]($s * 0.04)), $s, $s
  $g.DrawString($char, $font, [System.Drawing.Brushes]::White, $rect, $fmt)
  $g.Dispose()
  $ms = New-Object System.IO.MemoryStream
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  $pngs += , $ms.ToArray()
  $bmp.Dispose()
}
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $s = $sizes[$i]; $b = if ($s -ge 256) { 0 } else { $s }
  $w.Write([byte]$b); $w.Write([byte]$b); $w.Write([byte]0); $w.Write([byte]0)
  $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$pngs[$i].Length); $w.Write([uint32]$offset)
  $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
[System.IO.File]::WriteAllBytes((Join-Path $PSScriptRoot $file), $out.ToArray())
# 另外輸出一張 256 像素 PNG，網站和說明用
if ($png) { [System.IO.File]::WriteAllBytes((Join-Path $PSScriptRoot $png), $pngs[-1]) }
}
Make-Icon '懂' ([System.Drawing.Color]::FromArgb(255, 11, 110, 99)) 'ShunDa.ico' 'icon-256.png'
Make-Icon '英' ([System.Drawing.Color]::FromArgb(255, 95, 107, 122)) 'ModeEn.ico' $null
Write-Host "完成：ShunDa.ico、ModeEn.ico（$($sizes -join '、') 像素）"

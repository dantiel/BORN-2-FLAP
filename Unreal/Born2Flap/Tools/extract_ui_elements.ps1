param([string]$Source=(Join-Path $PSScriptRoot '../../../Art/UI/Reference/born2flap-ui-elements.png'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$dest=Join-Path $PSScriptRoot '../../../Art/UI/Elements'
New-Item -ItemType Directory -Force $dest | Out-Null
$sheet=[Drawing.Bitmap]::FromFile($Source)
# Pixel-exact cuts from the supplied atlas, with geometric alpha outside frames.
# Keep the painted interiors; no regenerated artwork or baked-in labels.
$cuts=@(
 @('PanelWide',29,319,647,257,'panel'), @('Panel',690,313,335,265,'panel'),
 @('Banner',1034,322,380,80,'panel'), @('Field',1040,414,188,77,'panel'),
 @('FieldCloud',1243,414,170,77,'panel'), @('FieldMountain',1040,502,188,72,'panel'),
 @('FieldSky',1243,502,170,72,'panel'),
 @('ButtonNormal',30,589,365,44,'button'), @('ButtonHover',30,638,365,46,'button'),
 @('ButtonPressed',30,688,365,44,'button'), @('ButtonDisabled',30,738,365,44,'button'),
 @('TabHover',416,597,172,38,'button'), @('TabSelected',416,644,172,38,'button'),
 @('TabNormal',606,597,165,38,'button'), @('TabDisabled',606,644,165,38,'button'),
 @('ArrowLeft',430,698,57,56,'circle'), @('ArrowRight',522,698,57,56,'circle'),
 @('ArrowFast',616,698,57,56,'circle'), @('ArrowNext',706,698,57,56,'circle'),
 @('ToggleOn',416,765,81,40,'pill'), @('ToggleOff',516,765,73,40,'pill'),
 @('RadioOff',617,769,30,30,'circle'), @('RadioOn',668,769,30,30,'circle'),
 @('SliderThumb',716,769,28,30,'circle'), @('SliderThumbGold',762,769,28,30,'circle'),
 @('CheckOn',836,769,35,34,'rect'), @('CheckOff',901,769,35,34,'rect'),
 @('Sun',1439,294,70,67,'circle'), @('Cloud',1439,365,70,66,'circle'),
 @('Storm',1439,434,70,66,'circle'), @('Rain',1439,505,70,66,'circle'),
 @('Wind',1439,577,70,66,'circle'), @('Moon',1439,648,70,66,'circle'),
 @('Turbulence',1439,721,70,66,'circle'),
 @('Coast',47,819,234,128,'panel'), @('Mountains',290,819,234,128,'panel'),
 @('Volcano',536,819,228,128,'panel'), @('Islands',775,819,230,128,'panel'),
 @('Desert',1012,819,234,128,'panel'), @('Valley',1257,819,231,128,'panel'),
 @('SliderTrack',815,608,154,10,'pill'), @('SliderTrackDark',999,608,146,10,'pill'),
 @('Divider',831,650,142,5,'rect')
)
$manifest=@()
foreach($c in $cuts){
 $name,$x,$y,$w,$h,$shape=$c
 $bmp=New-Object Drawing.Bitmap($w,$h,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
 $g=[Drawing.Graphics]::FromImage($bmp)
 $g.Clear([Drawing.Color]::Transparent)
 $path=New-Object Drawing.Drawing2D.GraphicsPath
 if($shape -eq 'circle'){$path.AddEllipse(0,0,$w-1,$h-1)}
 elseif($shape -eq 'pill'){
  $d=[Math]::Min($w,$h)-1
  $path.AddArc(0,0,$d,$d,90,180);$path.AddArc($w-$d-1,0,$d,$d,270,180);$path.CloseFigure()
 }elseif($shape -eq 'button' -or $shape -eq 'panel'){
  $k=if($shape -eq 'button'){[int]($h/2)}else{[Math]::Min(19,[int]($h/5))}
  [Drawing.Point[]]$points=@([Drawing.Point]::new($k,0),[Drawing.Point]::new($w-$k-1,0),[Drawing.Point]::new($w-1,$k),[Drawing.Point]::new($w-1,$h-$k-1),[Drawing.Point]::new($w-$k-1,$h-1),[Drawing.Point]::new($k,$h-1),[Drawing.Point]::new(0,$h-$k-1),[Drawing.Point]::new(0,$k))
  $path.AddPolygon($points)
 }else{$path.AddRectangle([Drawing.Rectangle]::new(0,0,$w,$h))}
 $g.SetClip($path)
 $g.DrawImage($sheet,[Drawing.Rectangle]::new(0,0,$w,$h),$x,$y,$w,$h,[Drawing.GraphicsUnit]::Pixel)
 $bmp.Save((Join-Path $dest ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
 $g.Dispose();$path.Dispose();$bmp.Dispose()
 $manifest+=@{name=$name;rect=@($x,$y,$w,$h);mask=$shape}
}
$sheet.Dispose()
$manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $dest 'manifest.json')
Write-Output "Extracted $($cuts.Count) UI assets into $dest"

param([string]$Dir = "$env:TEMP\d3c_snaps")
Add-Type -AssemblyName System.Drawing
Get-ChildItem $Dir -Filter *.bmp | ForEach-Object {
  $b = [System.Drawing.Bitmap]::FromFile($_.FullName); $b.RotateFlip([System.Drawing.RotateFlipType]::RotateNoneFlipY)
  $p = [IO.Path]::ChangeExtension($_.FullName, ".png"); $b.Save($p, [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose(); Remove-Item $_.FullName
  $p
}

# Deterministic, original Win32 icon: three white usage bars on a blue tile.
$ErrorActionPreference = 'Stop'
$destination = Join-Path (Split-Path $PSScriptRoot -Parent) 'resources\app.ico'
$stream = [IO.File]::Create($destination)
$writer = New-Object IO.BinaryWriter($stream)
try {
    $sizes = @(16,32,48)
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    foreach ($size in $sizes) {
        $maskRow = [int]([Math]::Ceiling($size / 32.0) * 4)
        $length = 40 + $size*$size*4 + $maskRow*$size
        $writer.Write([byte]$size); $writer.Write([byte]$size); $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$length); $writer.Write([uint32]$offset)
        $offset += $length
    }
    foreach ($size in $sizes) {
        $maskRow = [int]([Math]::Ceiling($size / 32.0) * 4)
        $writer.Write([uint32]40); $writer.Write([int32]$size); $writer.Write([int32]($size*2)); $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]0); $writer.Write([uint32]($size*$size*4)); $writer.Write([int32]0); $writer.Write([int32]0); $writer.Write([uint32]0); $writer.Write([uint32]0)
        for ($y=$size-1; $y -ge 0; $y--) {
            for ($x=0; $x -lt $size; $x++) {
                $nx=($x+0.5)/$size; $ny=($y+0.5)/$size
                $visible=($nx -ge .08 -and $nx -le .92 -and $ny -ge .08 -and $ny -le .92)
                $bar=($ny -le .75 -and (($nx -ge .24 -and $nx -le .36 -and $ny -ge .51) -or ($nx -ge .44 -and $nx -le .56 -and $ny -ge .36) -or ($nx -ge .64 -and $nx -le .76 -and $ny -ge .22)))
                if ($bar) { $writer.Write([byte]255); $writer.Write([byte]255); $writer.Write([byte]255) }
                else { $writer.Write([byte]218); $writer.Write([byte]99); $writer.Write([byte]42) }
                $writer.Write([byte]($(if($visible){255}else{0})))
            }
        }
        $writer.Write((New-Object byte[] ($maskRow*$size)))
    }
} finally { $writer.Dispose(); $stream.Dispose() }

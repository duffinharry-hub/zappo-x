# Zappo - fixes GarlicOS's video player (the FFmpeg RetroArch core) on the card.
#
# GarlicOS's copy passes file positions to the system with the two halves of the
# 64-bit number the wrong way round, so skipping (and any MP4 with its index at
# the end) closes the player. The fix is 47 bytes in one function. This script
# changes those bytes in YOUR copy of the core, on the card, after checking it
# is the exact build the fix was made for; nothing else in the file changes.
#   player-fix.ps1            apply (the original is kept as ffmpeg_libretro.so.garlic)
#   player-fix.ps1 -Revert    put the original back
param([string]$Card = '', [switch]$Revert, [switch]$Quiet)
. (Join-Path $PSScriptRoot 'common.ps1')
Log-Start 'Video player fix'
$card = Find-Card $Card
$core = Join-Path $card 'CFW\retroarch\.retroarch\cores\ffmpeg_libretro.so'
$keep = $core + '.garlic'
$STOCK = '4104da876f4b459fead4dd16305c0e47'   # GarlicOS 1.4.x ffmpeg_libretro.so
$FIXED = '7a4fc34e20db1fcebfef24cd073d38d3'
$OFF = 2825541
$OLD = '109de5040090e5010851e30d00000a00108de56d24f7ebc01fa0e1000050e30030d1e2010000ba64d08de204f09de4'   # GarlicOS's bytes
$NEW = 'c09de5040090e501085ce30d00000a0210a0e10c20a0e16c24f7ebc01fb0e10200004a64d08de204f09de40000a0e1'   # the fix
function Hex2Bytes([string]$h) { $b = New-Object byte[] ($h.Length / 2); for ($i = 0; $i -lt $b.Length; $i++) { $b[$i] = [Convert]::ToByte($h.Substring(2 * $i, 2), 16) }; return $b }

if (-not (Test-Path -LiteralPath $core)) { Say "No video player core on $card (CFW\retroarch\.retroarch\cores\ffmpeg_libretro.so) - is this a GarlicOS card?"; exit 1 }
$have = Md5 $core
if ($Revert) {
	if ($have -eq $STOCK) { Say 'The video player is already GarlicOS''s original.'; exit 0 }
	if ((Test-Path -LiteralPath $keep) -and (Md5 $keep) -eq $STOCK) { Copy-Verified $keep $core; Say 'GarlicOS''s original video player is back.'; exit 0 }
	if ($have -eq $FIXED) {
		$bytes = [IO.File]::ReadAllBytes($core)
		[Array]::Copy((Hex2Bytes $OLD), 0, $bytes, $OFF, $OLD.Length / 2)
		[IO.File]::WriteAllBytes($core + '.part', $bytes)
		if ((Md5 ($core + '.part')) -ne $STOCK) { Remove-Item -LiteralPath ($core + '.part'); throw 'undo did not produce the original - nothing changed' }
		Move-Item -LiteralPath ($core + '.part') -Destination $core -Force
		Say 'GarlicOS''s original video player is back.'; exit 0
	}
	Say 'This video player is not one this kit knows, so it was left alone.'; exit 1
}
if ($have -eq $FIXED) { if (-not $Quiet) { Say 'The video player is already fixed.' }; exit 0 }
if ($have -ne $STOCK) { Say 'This video player is not the GarlicOS build the fix was made for, so it was left alone (skipping may close it).'; exit 1 }
if (-not (Test-Path -LiteralPath $keep)) { Copy-Verified $core $keep }
$bytes = [IO.File]::ReadAllBytes($core)
$cur = [BitConverter]::ToString($bytes, $OFF, $OLD.Length / 2).Replace('-', '').ToLowerInvariant()
if ($cur -ne $OLD) { throw 'the bytes to change are not where they should be - nothing changed' }
[Array]::Copy((Hex2Bytes $NEW), 0, $bytes, $OFF, $NEW.Length / 2)
[IO.File]::WriteAllBytes($core + '.part', $bytes)
if ((Md5 ($core + '.part')) -ne $FIXED) { Remove-Item -LiteralPath ($core + '.part'); throw 'the fixed file did not come out as expected - nothing changed' }
Move-Item -LiteralPath ($core + '.part') -Destination $core -Force
Say 'Video player fixed (skipping works; B leaves cleanly). The original is kept beside it as ffmpeg_libretro.so.garlic.'

# Zappo - puts Zappo's boot logo, charging screen and charging animation on
# GarlicOS's boot partition (the small ~10 MB partition with dmenu.bin), and
# takes them off again. Everything replaced is kept in backups\boot-partition.
#   boot-art.ps1            apply
#   boot-art.ps1 -Revert    GarlicOS's originals back
#
# The boot logo, charging screen and battery frames are plain .bmp.gz files the
# bootloader shows. The charging animation itself is drawn by a small program
# inside ramdisk.img, from PNGs packed in it: this rewrites YOUR ramdisk.img
# with Zappo's PNGs in place of those eight pictures, everything else staying
# byte for byte - and only when it is a ramdisk this kit knows (stock GarlicOS,
# or one it made before), never one from somewhere else.
param([string]$Boot = '', [switch]$Revert, [int64]$FreeBytes = -1)   # -FreeBytes: pretend this much room (tests)
. (Join-Path $PSScriptRoot 'common.ps1')
Log-Start 'Boot pictures'
$boot = Find-Boot $Boot
if (-not $boot) { Say 'The boot partition is not showing in Windows (it is the small drive with dmenu.bin and ramdisk.img on it). Put the GarlicOS card in and try again.'; exit 1 }
$src = Join-Path $script:kit 'boot'
$bk = Backup-Dir 'boot-partition'
# Ramdisks are recognised by what is inside them (the names and bytes of the files
# in the archive), not the whole file: GarlicOS builds differ only in the archive's
# dates and padding, and all carry the same charging program and pictures.
$KNOWN = @('e539d2e3f6583382e6a6244ada8fb4bf',   # stock GarlicOS 1.3 / 1.4 ramdisk.img
           'a4f0e52af651de3db4a2e06a8fa26594',   # earlier Zappo charging pictures (swapped for the current ones)
           '216881482dbfcbbd670d62f4ff89eee3',
           'd67a5618aa0d17f76cd9d3fc60466354',
           'a0760fdf7755836e3c6406be057029b7',
           '0093a5f15f461b704b405fad5e52493a',   # drawn Zappo and fire Zappo
           'f9b3d845c6dacef9331513e9aeb40c2f')   # Zappo's current charging pictures (the animated scene, battery filling)
$knownFile = Join-Path $bk 'ramdisk.known'
if (Test-Path -LiteralPath $knownFile) { $KNOWN += @(Get-Content -LiteralPath $knownFile | Where-Object { $_ }) }
$pics = @('boot_logo.bmp.gz', 'charger_logo.bmp.gz', 'battery_low.bmp.gz') + (0..5 | ForEach-Object { "$($_)_charger_frame.bmp.gz" })

# ---- u-boot image + newc cpio, enough to swap files inside the ramdisk
$crcTable = New-Object uint32[] 256
for ($i = 0; $i -lt 256; $i++) { $c = [uint32]$i; for ($k = 0; $k -lt 8; $k++) { if ($c -band 1) { $c = [uint32]3988292384 -bxor ($c -shr 1) } else { $c = $c -shr 1 } }; $crcTable[$i] = $c }
function Crc32([byte[]]$d, [int]$off, [int]$len) {
	$c = [uint32]::MaxValue
	for ($i = $off; $i -lt $off + $len; $i++) { $c = $crcTable[($c -bxor $d[$i]) -band 0xFF] -bxor ($c -shr 8) }
	return $c -bxor [uint32]::MaxValue
}
function BE32([byte[]]$d, [int]$o) { return ([uint32]$d[$o] -shl 24) -bor ([uint32]$d[$o + 1] -shl 16) -bor ([uint32]$d[$o + 2] -shl 8) -bor [uint32]$d[$o + 3] }
function PutBE32([byte[]]$d, [int]$o, [uint32]$v) { $d[$o] = ($v -shr 24) -band 0xFF; $d[$o + 1] = ($v -shr 16) -band 0xFF; $d[$o + 2] = ($v -shr 8) -band 0xFF; $d[$o + 3] = $v -band 0xFF }
function Ramdisk-Fingerprint([string]$in) {
	$d = [IO.File]::ReadAllBytes($in)
	if ($d.Length -lt 64 -or (BE32 $d 0) -ne 0x27051956) { return 'not-a-uimage' }
	$size = [Math]::Min([int](BE32 $d 12), $d.Length - 64)
	$ascii = [Text.Encoding]::ASCII
	$md5 = [Security.Cryptography.MD5]::Create()
	$p = 0
	while ($p + 110 -le $size) {
		if ($ascii.GetString($d, 64 + $p, 6) -ne '070701') { return 'not-a-cpio' }
		$namesz = [Convert]::ToInt32($ascii.GetString($d, 64 + $p + 94, 8), 16); $fsz = [Convert]::ToInt32($ascii.GetString($d, 64 + $p + 54, 8), 16)
		$name = $ascii.GetString($d, 64 + $p + 110, $namesz - 1)
		if ($name -eq 'TRAILER!!!') { break }
		$hs = (110 + $namesz + 3) -band (-bnot 3)
		$nb = $ascii.GetBytes($name); [void]$md5.TransformBlock($nb, 0, $nb.Length, $null, 0)
		[void]$md5.TransformBlock(@([byte]0), 0, 1, $null, 0)
		[void]$md5.TransformBlock($d, 64 + $p + $hs, $fsz, $null, 0)
		$p += ($hs + $fsz + 3) -band (-bnot 3)
	}
	[void]$md5.TransformFinalBlock(@(), 0, 0)
	return ([BitConverter]::ToString($md5.Hash)).Replace('-', '').ToLowerInvariant()
}
function Patch-Ramdisk([string]$in, [string]$out, [string]$pngDir) {
	$d = [IO.File]::ReadAllBytes($in)
	if ((BE32 $d 0) -ne 0x27051956) { throw 'ramdisk.img is not a u-boot image' }
	$size = [int](BE32 $d 12)
	$cpio = New-Object byte[] $size; [Array]::Copy($d, 64, $cpio, 0, $size)
	$ascii = [Text.Encoding]::ASCII
	$want = @{}; foreach ($n in @('battery_0', 'battery_1', 'battery_2', 'battery_3', 'battery_4', 'battery_5', 'battery_charge', 'battery_fail')) { $want[$n] = $true }
	$outS = New-Object IO.MemoryStream
	$p = 0; $done = 0
	while ($p + 110 -le $cpio.Length) {
		if ($ascii.GetString($cpio, $p, 6) -ne '070701') { throw "not a newc cpio archive at $p" }
		$f = New-Object string[] 13
		for ($i = 0; $i -lt 13; $i++) { $f[$i] = $ascii.GetString($cpio, $p + 6 + $i * 8, 8) }
		$namesz = [Convert]::ToInt32($f[11], 16); $fsz = [Convert]::ToInt32($f[6], 16)
		$name = $ascii.GetString($cpio, $p + 110, $namesz - 1)
		$hs = (110 + $namesz + 3) -band (-bnot 3)
		$data = New-Object byte[] $fsz; [Array]::Copy($cpio, $p + $hs, $data, 0, $fsz)
		$p += ($hs + $fsz + 3) -band (-bnot 3)
		$stem = [IO.Path]::GetFileNameWithoutExtension($name)
		if ($name.StartsWith('res/images/charger/') -and $want.ContainsKey($stem)) {
			$png = Join-Path $pngDir ($stem + '.png')
			if (Test-Path -LiteralPath $png) { $data = [IO.File]::ReadAllBytes($png); $done++ }
		}
		$f[6] = ('{0:X8}' -f $data.Length)
		$hdr = $ascii.GetBytes('070701' + ($f -join '') + $name + "`0")
		$outS.Write($hdr, 0, $hdr.Length); while ($outS.Length % 4) { $outS.WriteByte(0) }
		$outS.Write($data, 0, $data.Length); while ($outS.Length % 4) { $outS.WriteByte(0) }
		if ($name -eq 'TRAILER!!!') { break }
	}
	if ($done -ne 8) { throw "only $done of the 8 charger pictures were found in the ramdisk - not touching it" }
	while ($outS.Length % 512) { $outS.WriteByte(0) }
	$body = $outS.ToArray()
	$h = New-Object byte[] 64; [Array]::Copy($d, 0, $h, 0, 64)
	PutBE32 $h 12 ([uint32]$body.Length)
	PutBE32 $h 24 (Crc32 $body 0 $body.Length)
	PutBE32 $h 4 0
	PutBE32 $h 4 (Crc32 $h 0 64)
	$fs = [IO.File]::Create($out); try { $fs.Write($h, 0, 64); $fs.Write($body, 0, $body.Length) } finally { $fs.Dispose() }
}

if ($Revert) {
	$n = 0
	foreach ($f in $pics + @('ramdisk.img')) {
		$b = Join-Path $bk $f
		if (Test-Path -LiteralPath $b) { Copy-Verified $b (Join-Path $boot $f); $n++ }
	}
	if ($n) { Say "GarlicOS's own boot pictures are back ($n files)." } else { Say 'Nothing to put back: this kit never changed the boot partition.' }
	exit 0
}

# keep the originals (only the first time; later runs must not overwrite them with Zappo's)
foreach ($f in $pics + @('ramdisk.img')) {
	$cur = Join-Path $boot $f; $b = Join-Path $bk $f
	if ((Test-Path -LiteralPath $cur) -and -not (Test-Path -LiteralPath $b)) { Copy-Verified $cur $b }
}
$need = 0; foreach ($f in $pics) { $need += (Get-Item -LiteralPath (Join-Path $src $f)).Length }
foreach ($f in $pics) { Copy-Verified (Join-Path $src $f) (Join-Path $boot $f) }
Say 'Boot logo, charging screen and battery frames updated.'

$rd = Join-Path $boot 'ramdisk.img'
if (-not (Test-Path -LiteralPath $rd)) { Say 'No ramdisk.img on the boot partition; the charging animation was left alone.'; exit 0 }
$have = Ramdisk-Fingerprint $rd
if ($KNOWN -notcontains $have) { Say 'ramdisk.img is not the GarlicOS one this kit knows, so the charging animation was left alone (the pictures above still apply).'; exit 0 }
$tmp = Join-Path ([IO.Path]::GetTempPath()) 'zappo-ramdisk.img'
Patch-Ramdisk (Join-Path $bk 'ramdisk.img') $tmp (Join-Path $src 'charger')
$new = Ramdisk-Fingerprint $tmp
if ($new -eq $have) { Say 'Charging animation already in place.'; Remove-Item -LiteralPath $tmp -Force; exit 0 }
$extra = (Get-Item -LiteralPath $tmp).Length - (Get-Item -LiteralPath $rd).Length
if ($extra -gt 0 -and $(if ($FreeBytes -ge 0) { $FreeBytes } else { Free $boot }) -lt $extra + 262144) { Say 'Not enough room on the boot partition for the charging animation; skipped.'; Remove-Item -LiteralPath $tmp -Force; exit 0 }
Copy-Verified $tmp $rd
Remove-Item -LiteralPath $tmp -Force
Add-Content -LiteralPath $knownFile -Value $new
Say 'Charging animation updated (ramdisk.img rewritten; the original is in backups\boot-partition).'

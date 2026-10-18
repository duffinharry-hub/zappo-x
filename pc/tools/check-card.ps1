# Zappo - reads the card back and checks it: every Zappo file is there and
# intact, the video player is the fixed one, GarlicOS's launcher hand-over is
# in place, and how much room is left. Nothing is changed.
param([string]$Card = '')
. (Join-Path $PSScriptRoot 'common.ps1')
Log-Start 'Check card'
$card = Find-Card $Card
$src = Join-Path $script:kit 'card'
$bad = 0; $ok = 0
foreach ($f in Get-ChildItem -LiteralPath $src -File -Recurse -Force) {
	$rel = Rel $src $f.FullName
	$dst = Join-Path $card $rel
	if (-not (Test-Path -LiteralPath $dst)) { Say "missing:  $rel"; $bad++; continue }
	if ((Md5 $dst) -ne (Md5 $f.FullName)) { Say "differs:  $rel"; $bad++; continue }
	$ok++
}
Say "Zappo files: $ok intact, $bad missing or damaged."
$core = Join-Path $card 'CFW\retroarch\.retroarch\cores\ffmpeg_libretro.so'
if (Test-Path -LiteralPath $core) {
	switch (Md5 $core) {
		'7a4fc34e20db1fcebfef24cd073d38d3' { Say 'Video player: fixed (skipping works).' }
		'4104da876f4b459fead4dd16305c0e47' { Say 'Video player: GarlicOS''s original (skipping closes it) - Zappo.bat -> 1 fixes it.' }
		default { Say 'Video player: not a build this kit knows.' }
	}
} else { Say 'Video player: no ffmpeg_libretro.so on the card.' }
$main = Join-Path $card 'CFW\main'
if ((Test-Path -LiteralPath $main) -and (Select-String -LiteralPath $main -Pattern 'CLOVE_WRAPPER' -Quiet)) { Say 'Start-up: Zappo (GarlicOS''s own menu kept as CFW\main.garlic).' }
elseif (Test-Path -LiteralPath (Join-Path $card 'CFW\autostart\clove-install.sh')) { Say 'Start-up: GarlicOS''s menu; Zappo takes over at the handheld''s next start.' }
else { Say 'Start-up: GarlicOS''s menu (Zappo is not installed).' }
foreach ($d in 'Roms', 'Music', 'Roms\VIDEOS') {
	$p = Join-Path $card $d
	if (Test-Path -LiteralPath $p) { $n = @(Get-ChildItem -LiteralPath $p -File -Recurse | Where-Object { $_.Name -notlike '.*' -and $_.Directory.Name -ne 'Imgs' }).Count; Say ("{0}: {1} files" -f $d, $n) }
}
Say ('Room left on the card: {0:N1} GB.' -f ((Free $card) / 1GB))
$tr = Join-Path $card 'CFW\clove\trouble.txt'
if (Test-Path -LiteralPath $tr) { Say 'The handheld noted a restart while something was running (CFW\clove\trouble.txt); CFW\clove\runs.log and last-kmsg.log have the details.' }
Say $(if ($bad) { 'CHECK FAILED - run Install / update again.' } else { 'ALL GOOD.' })

# Zappo - installs or updates Zappo on a GarlicOS card that is in this PC.
#   install.ps1              install / update (safe to run again any time)
#   install.ps1 -Remove      take Zappo off: GarlicOS's own menu and video player back
# What it does:
#   1. copies the card\ folder of this kit onto the card (CFW\clove, CFW\autostart,
#      the BLOCKBOX core and worlds, the Import, Music and Roms\VIDEOS folders).
#      Your settings, games, music, films and saves are never touched.
#   2. fixes the video player (tools\player-fix.ps1; undoable).
#   3. if the boot partition shows up too: Zappo's boot logo and charging
#      animation (tools\boot-art.ps1; undoable). Skipped if it doesn't.
# Zappo finishes installing itself on the handheld's first start (GarlicOS runs
# CFW\autostart\clove-install.sh, which keeps GarlicOS's launcher as CFW\main.garlic).
param([string]$Card = '', [switch]$Remove, [switch]$NoBoot)
. (Join-Path $PSScriptRoot 'common.ps1')
Log-Start $(if ($Remove) { 'Remove Zappo' } else { 'Install / update Zappo' })
$card = Find-Card $Card
$src = Join-Path $script:kit 'card'
if (-not (Test-Path -LiteralPath (Join-Path $src 'CFW\clove\clove'))) { throw 'this kit is missing its card folder - unzip Zappo again' }

if ($Remove) {
	& (Join-Path $PSScriptRoot 'player-fix.ps1') -Card $card -Revert
	$main = Join-Path $card 'CFW\main'; $garlic = Join-Path $card 'CFW\main.garlic'
	if ((Test-Path -LiteralPath $garlic) -and (Select-String -LiteralPath $main -Pattern 'CLOVE_WRAPPER' -Quiet)) {
		Copy-Verified $garlic $main; Remove-Item -LiteralPath $garlic -Force
		Say 'GarlicOS''s own menu is back (CFW\main).'
	}
	$auto = Join-Path $card 'CFW\autostart\clove-install.sh'
	if (Test-Path -LiteralPath $auto) { Remove-Item -LiteralPath $auto -Force }
	$bk = Backup-Dir ('removed-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
	foreach ($rel in 'CFW\clove', 'Roms\APPS\Switch to Clove.sh') {
		$p = Join-Path $card $rel
		if (Test-Path -LiteralPath $p) { Move-Item -LiteralPath $p -Destination (Join-Path $bk (Split-Path -Leaf $rel)) -Force }
	}
	Say "Zappo's own folder (CFW\clove: settings, playlists, themes) was moved to $bk, not deleted. Games, music, films and saves are untouched."
	if (-not $NoBoot -and (Find-Boot '')) { & (Join-Path $PSScriptRoot 'boot-art.ps1') -Revert }
	Say 'DONE - Zappo removed.'
	exit 0
}

# 1. the card files
$files = @(Get-ChildItem -LiteralPath $src -File -Recurse -Force)
$need = 0; foreach ($f in $files) { $need += $f.Length }
$free = Free $card
if ($free -ge 0 -and $free -lt $need + 16MB) { throw ('the card needs {0:N0} MB free for Zappo and has {1:N0} MB' -f (($need + 16MB) / 1MB), ($free / 1MB)) }
$copied = 0; $same = 0
foreach ($f in $files) {
	$rel = Rel $src $f.FullName
	$dst = Join-Path $card $rel
	if ((Test-Path -LiteralPath $dst) -and (Get-Item -LiteralPath $dst).Length -eq $f.Length -and (Md5 $dst) -eq (Md5 $f.FullName)) { $same++; continue }
	Copy-Verified $f.FullName $dst
	$copied++
}
Say "Zappo files on the card: $copied copied, $same already up to date."
# the start script GarlicOS runs: once Zappo is installed, keep it the current one
$main = Join-Path $card 'CFW\main'
if ((Test-Path -LiteralPath $main) -and (Select-String -LiteralPath $main -Pattern 'CLOVE_WRAPPER' -Quiet)) {
	Copy-Verified (Join-Path $src 'CFW\clove\main.sh') $main
}
# folders Zappo expects (empty ones don't travel in a zip)
foreach ($d in 'Import', 'Music', 'Roms\VIDEOS', 'Saves', 'Screenshots') { [void][IO.Directory]::CreateDirectory((Join-Path $card $d)) }

# 2. the video player
& (Join-Path $PSScriptRoot 'player-fix.ps1') -Card $card -Quiet

# 3. the boot partition, when Windows shows it
if (-not $NoBoot) {
	if (Find-Boot '') { & (Join-Path $PSScriptRoot 'boot-art.ps1') }
	else { Say 'Boot partition not showing in Windows, so the boot logo and charging animation were skipped (Zappo.bat -> B when it is).' }
}
Say 'DONE. Eject the card, put it in the handheld and switch on: the first start finishes the install and opens Zappo.'

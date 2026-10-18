# Zappo X - reads back what the kit put on the card (changes nothing) and shows
# what the handheld did with it (zappo-x-kit\log.txt).
param([string]$Card = '')
. (Join-Path $PSScriptRoot 'common.ps1')
Log-Start 'Check the card'
$card = Find-Card $Card
$K = Join-Path $card 'zappo-x-kit'
$board = (Get-Content -LiteralPath (Join-Path $card 'boot/batocera.board') -ErrorAction SilentlyContinue | Select-Object -First 1)
Say ("Knulli card on {0} (board: {1})" -f $card, $board)
$hook = Join-Path $card 'postshare.sh'
if (Is-Our-Hook $hook) {
	$ok = (Md5 $hook) -eq (Md5 (Join-Path $script:kit 'card/postshare.sh'))
	Say ('Start-up hook (postshare.sh): ' + $(if ($ok) { 'present and as in this kit' } else { 'present, from another version of the kit' }))
} elseif (Test-Path -LiteralPath $hook) { Say 'postshare.sh is the card''s own, not Zappo X''s.' }
else { Say 'No start-up hook: Zappo X has not been put on this card by the kit (or was taken off).' }
foreach ($what in 'install', 'installed', 'test-pack') {
	$p = Join-Path $K $what
	if (-not (Test-Path -LiteralPath $p)) { continue }
	$bad = 0; $n = 0
	$ref = if ($what -eq 'test-pack') { Join-Path $script:kit 'test-pack' } else { Join-Path $script:kit 'card/zappo-x-kit/install' }
	foreach ($f in @(Get-ChildItem -LiteralPath $p -File -Recurse -Force)) {
		$n++
		$mine = Join-Path $ref (Rel $p $f.FullName)	# (not $k: PowerShell names ignore case, and $K is the kit folder)
		if ((Test-Path -LiteralPath $mine) -and (Md5 $mine) -ne (Md5 $f.FullName)) { $bad++ }
	}
	$state = @{ 'install' = 'waiting for the next start'; 'installed' = 'installed by the handheld'; 'test-pack' = 'waiting for the next start' }[$what]
	Say ("zappo-x-kit\{0}: {1} files, {2} ({3})" -f $what, $n, $(if ($bad) { "$bad differ from this kit" } else { 'all read back fine' }), $state)
}
if (Test-Path -LiteralPath (Join-Path $K 'remove')) { Say 'Taking Zappo X off is waiting for the next start.' }
$log = Join-Path $K 'log.txt'
if (Test-Path -LiteralPath $log) {
	Say 'What the handheld did (the end of zappo-x-kit\log.txt):'
	foreach ($l in @(Get-Content -LiteralPath $log -Tail 25)) { Say ('  ' + $l) }
}
Say 'DONE - nothing was changed.'

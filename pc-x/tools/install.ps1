# Zappo X - puts Zappo X on a Knulli card that is in this PC. The handheld does
# the rest at its next start (postshare.sh on the card's BATOCERA partition runs
# Zappo X's own installer as soon as the card's data partition is ready).
#   install.ps1              install / update (safe to run again any time)
#   install.ps1 -TestPack    also the test pack: songs and films to try things with
#   install.ps1 -Remove      take Zappo X off: Knulli's own menu at the next start
# What changes on the card's BATOCERA partition: postshare.sh (a postshare.sh the
# card already had is kept as postshare.before-zappo-x.sh, and still runs) and the
# zappo-x-kit folder. Knulli's own system files are never touched.
param([string]$Card = '', [switch]$Remove, [switch]$TestPack, [switch]$OnlyTestPack)
. (Join-Path $PSScriptRoot 'common.ps1')
Log-Start $(if ($Remove) { 'Take Zappo X off' } elseif ($OnlyTestPack) { 'Test pack' } else { 'Install / update Zappo X' })
$card = Find-Card $Card
$src = Join-Path $script:kit 'card'
$payload = Join-Path $src 'zappo-x-kit/install'
if (-not (Test-Path -LiteralPath (Join-Path $payload 'install.sh'))) { throw 'this kit is missing its card folder - unzip the Zappo X kit again' }
$K = Join-Path $card 'zappo-x-kit'
$hook = Join-Path $card 'postshare.sh'
$theirs = Join-Path $card 'postshare.before-zappo-x.sh'

# the start-up hook, keeping one the card already had
if ((Test-Path -LiteralPath $hook) -and -not (Is-Our-Hook $hook)) {
	if (Test-Path -LiteralPath $theirs) { throw "the card has a postshare.sh of its own and already a postshare.before-zappo-x.sh: sort those out first (both are on $card)" }
	Copy-Verified $hook (Join-Path (Backup-Dir 'card') 'postshare.sh')
	Move-Item -LiteralPath $hook -Destination $theirs
	Say 'The card''s own postshare.sh is kept as postshare.before-zappo-x.sh (it still runs at every start; a copy is in backups\card).'
}
Copy-Verified (Join-Path $src 'postshare.sh') $hook

[void][IO.Directory]::CreateDirectory((Join-Path $K 'done'))
if ($Remove) {
	if (Test-Path -LiteralPath (Join-Path $K 'install')) {
		Move-Item -LiteralPath (Join-Path $K 'install') -Destination (Join-Path $K ('done/install-not-used-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
	}
	[IO.File]::WriteAllText((Join-Path $K 'remove'), ('asked for ' + (Get-Date -Format 'yyyy-MM-dd HH:mm') + "`n"), $script:utf8)
	Say 'DONE. Eject the card, put it in the handheld and switch on: Zappo X steps aside and Knulli''s own menu starts.'
	Say 'Zappo X''s folder on the handheld (your settings) stays; your games, music, films and saves are untouched.'
	exit 0
}
if (Test-Path -LiteralPath (Join-Path $K 'remove')) {
	Move-Item -LiteralPath (Join-Path $K 'remove') -Destination (Join-Path $K ('done/remove-cancelled-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
	Say 'A "take Zappo X off" that was waiting for the next start is cancelled.'
}

$need = 16MB
if (-not $OnlyTestPack) { $need += Tree-Size $payload }
$pack = Join-Path $script:kit 'test-pack'
if (($TestPack -or $OnlyTestPack) -and (Test-Path -LiteralPath $pack)) { $need += Tree-Size $pack }
$free = Free $card
if ($free -ge 0 -and $free -lt $need) { throw ('the card''s BATOCERA partition needs {0:N0} MB free and has {1:N0} MB' -f ($need / 1MB), ($free / 1MB)) }

if (-not $OnlyTestPack) {
	$r = Copy-Tree $payload (Join-Path $K 'install')
	$ver = (Get-Content -LiteralPath (Join-Path $payload 'zappo-x/VERSION') -ErrorAction SilentlyContinue | Select-Object -First 1)
	Say ("Zappo {0} on the card: {1} files copied, {2} already there." -f $ver, $r[0], $r[1])
}
if ($TestPack -or $OnlyTestPack) {
	if (-not (Test-Path -LiteralPath $pack)) { throw 'this kit has no test-pack folder' }
	$r = Copy-Tree $pack (Join-Path $K 'test-pack')
	Say ("Test pack on the card: {0} files copied, {1} already there (they go into Music and Videos at the next start)." -f $r[0], $r[1])
}
Say 'DONE. Eject the card, put it in the handheld and switch on.'
Say 'A new Knulli card first sets itself up (a few minutes, it restarts once); then Zappo X opens.'
Say 'What the handheld did is written to zappo-x-kit\log.txt on this partition (menu 3 shows it).'

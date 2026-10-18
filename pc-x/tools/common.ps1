# Zappo X kit - shared bits, dot-sourced by the other scripts.
$ErrorActionPreference = 'Stop'
$script:kit = Split-Path -Parent $PSScriptRoot
$script:utf8 = New-Object Text.UTF8Encoding($false)
$script:logDir = Join-Path $script:kit 'logs'
[void][IO.Directory]::CreateDirectory($script:logDir)
$script:logFile = Join-Path $script:logDir 'zappo-x-kit-log.txt'

function Say([string]$m) { Write-Host ('  ' + $m); [IO.File]::AppendAllText($script:logFile, $m + "`r`n", $script:utf8) }
function Log-Start([string]$what) { [IO.File]::AppendAllText($script:logFile, "`r`n== $what - " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + "`r`n", $script:utf8) }
function Md5([string]$p) { return (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash.ToLowerInvariant() }
function Rel([string]$root, [string]$full) { return $full.Substring($root.TrimEnd('\', '/').Length + 1) }
function Free([string]$root) {
	# free bytes on the drive that holds $root; -1 when it can't be told (then the check is skipped)
	try {
		$best = $null
		foreach ($d in [IO.DriveInfo]::GetDrives()) {
			$r = $d.RootDirectory.FullName
			if ($root.StartsWith($r, [StringComparison]::OrdinalIgnoreCase) -and ($null -eq $best -or $r.Length -gt $best.RootDirectory.FullName.Length)) { $best = $d }
		}
		if ($best -and $best.IsReady -and $best.AvailableFreeSpace -gt 0) { return $best.AvailableFreeSpace }
	} catch {}
	return -1
}

# A Knulli card's BATOCERA partition: the drive with boot\batocera (Knulli's system)
# and boot\batocera.board. -Card on the command line wins; otherwise the one found
# is used, and two or more means you have to say which.
function Is-Knulli([string]$r) {
	return (Test-Path -LiteralPath (Join-Path $r 'boot/batocera')) -and (Test-Path -LiteralPath (Join-Path $r 'boot/batocera.board'))
}
function Find-Card([string]$Card) {
	if ($Card) {
		$Card = $Card.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
		if (-not (Is-Knulli $Card)) { throw "$Card is not a Knulli card's BATOCERA partition (no boot\batocera there)" }
		return $Card
	}
	$found = @()
	foreach ($d in [IO.DriveInfo]::GetDrives()) {
		try {
			if (-not $d.IsReady) { continue }
			$r = $d.RootDirectory.FullName
			if (Is-Knulli $r) { $found += $r }
		} catch {}
	}
	if ($found.Count -eq 1) { return $found[0] }
	if ($found.Count -eq 0) { throw 'No Knulli card found. Put the card in the PC: its BATOCERA partition (with the boot folder) must show up as a drive.' }
	throw ('More than one Knulli card is in this PC (' + ($found -join ', ') + '). Run the script with -Card <letter>:\ to say which.')
}

function Copy-Verified([string]$from, [string]$to) {
	[void][IO.Directory]::CreateDirectory((Split-Path -Parent $to))
	Copy-Item -LiteralPath $from -Destination ($to + '.part') -Force
	if ((Md5 ($to + '.part')) -ne (Md5 $from)) { Remove-Item -LiteralPath ($to + '.part') -Force; throw "could not copy $from to $to" }
	Move-Item -LiteralPath ($to + '.part') -Destination $to -Force
}

# a folder copied file by file, each one checked; files already the same are left
function Copy-Tree([string]$from, [string]$to) {
	$copied = 0; $same = 0
	foreach ($f in @(Get-ChildItem -LiteralPath $from -File -Recurse -Force)) {
		$dst = Join-Path $to (Rel $from $f.FullName)
		if ((Test-Path -LiteralPath $dst) -and (Get-Item -LiteralPath $dst).Length -eq $f.Length -and (Md5 $dst) -eq (Md5 $f.FullName)) { $same++; continue }
		Copy-Verified $f.FullName $dst
		$copied++
	}
	return @($copied, $same)
}

function Tree-Size([string]$p) {
	$n = 0
	foreach ($f in @(Get-ChildItem -LiteralPath $p -File -Recurse -Force)) { $n += $f.Length }
	return $n
}

# Where the kit keeps what it replaced on a card, so everything can be undone.
function Backup-Dir([string]$what) {
	$d = Join-Path $script:kit ('backups/' + $what)
	[void][IO.Directory]::CreateDirectory($d)
	return $d
}

$script:HOOK_MARK = 'Zappo X kit: Knulli runs this'
function Is-Our-Hook([string]$p) { return (Test-Path -LiteralPath $p) -and (Select-String -LiteralPath $p -SimpleMatch -Pattern $script:HOOK_MARK -Quiet) }

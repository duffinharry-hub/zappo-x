# Zappo kit - shared bits, dot-sourced by the other scripts.
$ErrorActionPreference = 'Stop'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
$script:kit = Split-Path -Parent $PSScriptRoot
$script:utf8 = New-Object Text.UTF8Encoding($false)
$script:logDir = Join-Path $script:kit 'logs'
[void][IO.Directory]::CreateDirectory($script:logDir)
$script:logFile = Join-Path $script:logDir (([IO.Path]::GetFileNameWithoutExtension($MyInvocation.PSCommandPath)) + '-log.txt')
if (-not $MyInvocation.PSCommandPath) { $script:logFile = Join-Path $script:logDir 'zappo-log.txt' }

function Say([string]$m) { Write-Host ('  ' + $m); [IO.File]::AppendAllText($script:logFile, $m + "`r`n", $script:utf8) }
function Log-Start([string]$what) { [IO.File]::AppendAllText($script:logFile, "`r`n== $what - " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + "`r`n", $script:utf8) }
function Md5([string]$p) { return (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash.ToLowerInvariant() }
function Rel([string]$root, [string]$full) { return $full.Substring($root.TrimEnd('\', '/').Length + 1) }
function Free([string]$root) {
	# free bytes on the drive that holds $root; -1 when Windows can't say (then the check is skipped)
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

# The GarlicOS card: the drive that holds the CFW folder (GarlicOS's ROMS partition,
# or a second card set up to run GarlicOS from). -Card on the command line wins;
# otherwise the one card found is used, and two or more means you have to say which.
function Find-Card([string]$Card) {
	if ($Card) {
		$Card = $Card.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
		if (-not (Test-Path -LiteralPath (Join-Path $Card 'CFW'))) { throw "$Card has no CFW folder, so it is not a GarlicOS card" }
		return $Card
	}
	$found = @()
	foreach ($d in [IO.DriveInfo]::GetDrives()) {
		try {
			if (-not $d.IsReady -or $d.DriveType -eq 'Fixed' -and $d.Name -eq $env:SystemDrive + '\') { continue }
			$r = $d.RootDirectory.FullName
			if ((Test-Path -LiteralPath (Join-Path $r 'CFW\retroarch')) -and (Test-Path -LiteralPath (Join-Path $r 'Roms'))) { $found += $r }
		} catch {}
	}
	if ($found.Count -eq 1) { return $found[0] }
	if ($found.Count -eq 0) { throw 'No GarlicOS card found. Put the card in the PC (the partition with the CFW, BIOS and Roms folders must show up as a drive).' }
	throw ('More than one GarlicOS card is in this PC (' + ($found -join ', ') + '). Run the script with -Card <letter>:\ to say which.')
}

# GarlicOS's small boot partition (the one with dmenu.bin and ramdisk.img), if Windows shows it.
function Find-Boot([string]$Boot) {
	if ($Boot) { $Boot = $Boot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar; if (Test-Path -LiteralPath (Join-Path $Boot 'ramdisk.img')) { return $Boot } ; throw "$Boot has no ramdisk.img" }
	foreach ($d in [IO.DriveInfo]::GetDrives()) {
		try {
			if (-not $d.IsReady) { continue }
			$r = $d.RootDirectory.FullName
			if ((Test-Path -LiteralPath (Join-Path $r 'dmenu.bin')) -and (Test-Path -LiteralPath (Join-Path $r 'ramdisk.img'))) { return $r }
		} catch {}
	}
	return ''
}

function Copy-Verified([string]$from, [string]$to) {
	[void][IO.Directory]::CreateDirectory((Split-Path -Parent $to))
	Copy-Item -LiteralPath $from -Destination ($to + '.part') -Force
	if ((Md5 ($to + '.part')) -ne (Md5 $from)) { Remove-Item -LiteralPath ($to + '.part') -Force; throw "could not copy $from to $to" }
	Move-Item -LiteralPath ($to + '.part') -Destination $to -Force
}

# Where the kit keeps what it replaced on a card, so everything can be undone.
function Backup-Dir([string]$what) {
	$d = Join-Path $script:kit ('backups\' + $what)
	[void][IO.Directory]::CreateDirectory($d)
	return $d
}

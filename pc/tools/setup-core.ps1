# Zappo Setup - the working parts behind the setup window ("Zappo Setup.ps1"):
# finding the card, the list of jobs, running one of the kit's scripts with its
# output coming back line by line, and putting the kit on the PC (Start menu,
# Apps & features) or taking it off again. No window code in here, so it can be
# tested anywhere PowerShell runs.
$ErrorActionPreference = 'Stop'
$script:kit = Split-Path -Parent $PSScriptRoot
$script:onWindows = ($env:OS -eq 'Windows_NT')
$script:ps = if ($script:onWindows) { 'powershell.exe' } else { 'pwsh' }
$script:version = '0.3.5'
$script:kitName = 'Zappo for RG35XX'

# ---------------------------------------------------------------- the card
# Every GarlicOS card in the PC (its ROMS partition: CFW, Roms) and the boot
# partition (dmenu.bin, ramdisk.img), as paths. Drives Windows can't read are skipped.
function Get-CardDrives {
	$cards = @(); $boots = @()
	foreach ($d in [IO.DriveInfo]::GetDrives()) {
		try {
			if (-not $d.IsReady) { continue }
			$r = $d.RootDirectory.FullName
			if ($script:onWindows -and $d.DriveType -eq 'Fixed' -and $r -eq ($env:SystemDrive + '\')) { continue }
			if ((Test-Path -LiteralPath (Join-Path $r 'CFW')) -and (Test-Path -LiteralPath (Join-Path $r 'Roms'))) { $cards += $r }
			if ((Test-Path -LiteralPath (Join-Path $r 'dmenu.bin')) -and (Test-Path -LiteralPath (Join-Path $r 'ramdisk.img'))) { $boots += $r }
		} catch {}
	}
	return @{ cards = $cards; boots = $boots }
}

# What is on a card already: Zappo's version there (or ''), and whether it is GarlicOS.
function Get-CardInfo([string]$card) {
	$info = @{ garlic = $false; zappo = ''; label = '' }
	if (-not $card) { return $info }
	$info.garlic = Test-Path -LiteralPath (Join-Path $card 'CFW\retroarch')
	$v = Join-Path $card 'CFW\clove\version.txt'
	if (Test-Path -LiteralPath $v) { $info.zappo = (Get-Content -LiteralPath $v -TotalCount 1).Trim() }
	elseif (Test-Path -LiteralPath (Join-Path $card 'CFW\clove\clove')) { $info.zappo = 'installed' }
	try { $info.label = (New-Object IO.DriveInfo($card)).VolumeLabel } catch {}
	return $info
}

# ---------------------------------------------------------------- the jobs
# Each job is one of the kit's scripts with its arguments. Order matters:
# unpacking before adding media, media before art, the check last.
function Get-Jobs([hashtable]$want, [string]$card, [string]$boot) {
	$t = Join-Path $script:kit 'tools'
	$jobs = @()
	if ($want['install'])    { $jobs += @{ id = 'install'; title = 'Install / update Zappo on the card'; file = (Join-Path $t 'install.ps1'); args = @('-Card', $card, '-NoBoot') } }
	if ($want['boot'])       { $jobs += @{ id = 'boot'; title = 'Boot logo and charging animation'; file = (Join-Path $t 'boot-art.ps1'); args = $(if ($boot) { @('-Boot', $boot) } else { @() }) } }
	if ($want['unpack'])     { $jobs += @{ id = 'unpack'; title = 'Unpack downloaded video collections'; file = (Join-Path $t 'unpack-videos.ps1'); args = @() } }
	if ($want['media'])      { $jobs += @{ id = 'media'; title = 'Films and music from Downloads to the card'; file = (Join-Path $t 'add-media.ps1'); args = @('-Card', $card) } }
	if ($want['art'])        { $jobs += @{ id = 'art'; title = 'Box art for games'; file = (Join-Path $t 'get-game-art.ps1'); args = @('-Roms', (Join-Path $card 'Roms')) }
	                        $jobs += @{ id = 'covers'; title = 'Film posters and music covers'; file = (Join-Path $t 'get-cover-art.ps1'); args = @('-Card', $card) } }
	if ($want['cores'])      { $jobs += @{ id = 'cores'; title = 'Extra cores pack from Downloads'; file = (Join-Path $t 'extra-cores.ps1'); args = @('-Card', $card) } }
	if ($want['check'])      { $jobs += @{ id = 'check'; title = 'Check the card'; file = (Join-Path $t 'check-card.ps1'); args = @('-Card', $card) } }
	# undo
	if ($want['undoCores'])  { $jobs += @{ id = 'undoCores'; title = "GarlicOS's own cores back"; file = (Join-Path $t 'extra-cores.ps1'); args = @('-Card', $card, '-Revert') } }
	if ($want['undoPlayer']) { $jobs += @{ id = 'undoPlayer'; title = "GarlicOS's video player back"; file = (Join-Path $t 'player-fix.ps1'); args = @('-Card', $card, '-Revert') } }
	if ($want['undoBoot'])   { $jobs += @{ id = 'undoBoot'; title = 'GarlicOS boot logo and charging screen back'; file = (Join-Path $t 'boot-art.ps1'); args = @('-Revert') + $(if ($boot) { @('-Boot', $boot) } else { @() }) } }
	if ($want['remove'])     { $jobs += @{ id = 'remove'; title = 'Remove Zappo from the card'; file = (Join-Path $t 'install.ps1'); args = @('-Card', $card, '-Remove', '-NoBoot') } }
	return ,$jobs
}

# Starts one job in its own PowerShell, output going to a file. Returns a
# handle for Read-JobOutput / Wait-Job to poll, so a window stays responsive.
function Start-KitJob([hashtable]$job) {
	$dir = Join-Path $script:kit 'logs'
	[void][IO.Directory]::CreateDirectory($dir)
	$out = Join-Path $dir ('setup-' + $job.id + '.out.txt')
	$err = Join-Path $dir ('setup-' + $job.id + '.err.txt')
	foreach ($f in $out, $err) { if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force } }
	$argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-NonInteractive', '-File', ('"' + $job.file + '"')) + @($job.args | ForEach-Object { if ("$_" -match '[ "]') { '"' + ("$_" -replace '"', '\"') + '"' } else { "$_" } })
	$sp = @{ FilePath = $script:ps; ArgumentList = $argList; WorkingDirectory = $script:kit; PassThru = $true; RedirectStandardOutput = $out; RedirectStandardError = $err }
	if ($script:onWindows) { $sp.WindowStyle = 'Hidden' } else { $sp.NoNewWindow = $true }
	$p = Start-Process @sp
	return @{ proc = $p; file = $out; err = $err; read = 0L; title = $job.title; id = $job.id }
}

# New lines the job has written since last time (so a window can show them).
function Read-JobOutput([hashtable]$h) {
	$lines = @()
	try {
		$fs = [IO.File]::Open($h.file, 'Open', 'Read', 'ReadWrite')
		try {
			if ($fs.Length -gt $h.read) {
				[void]$fs.Seek($h.read, 'Begin')
				$buf = New-Object byte[] ($fs.Length - $h.read)
				$n = $fs.Read($buf, 0, $buf.Length)
				$text = [Text.Encoding]::UTF8.GetString($buf, 0, $n)
				$cut = $text.LastIndexOf("`n")
				if ($cut -ge 0) {
					$h.read += [Text.Encoding]::UTF8.GetByteCount($text.Substring(0, $cut + 1))
					$lines = $text.Substring(0, $cut).Replace("`r", '').Split("`n")
				}
			}
		} finally { $fs.Dispose() }
	} catch {}
	return ,$lines
}

function Test-JobDone([hashtable]$h) { return $h.proc.HasExited }

# Exit code once it has finished (0 = fine). What it said on its error stream
# is added to the output file, so Read-JobOutput shows that too.
function Complete-KitJob([hashtable]$h) {
	$h.proc.WaitForExit()
	try {
		if ((Test-Path -LiteralPath $h.err) -and (Get-Item -LiteralPath $h.err).Length -gt 0) {
			$e = [IO.File]::ReadAllText($h.err)
			if ($e.Trim()) { [IO.File]::AppendAllText($h.file, "`r`n" + $e.Trim() + "`r`n") }
		}
	} catch {}
	return $h.proc.ExitCode
}

# ---------------------------------------------------------------- on the PC
# Where the kit lives when installed on the PC, Start menu entry and all.
function Get-KitHome { return (Join-Path $env:LOCALAPPDATA 'Programs\Zappo Kit') }
function Test-KitInstalled { return (Test-Path -LiteralPath (Join-Path (Get-KitHome) 'Zappo Setup.ps1')) }
function Test-RunningFromHome { return ($script:kit.TrimEnd('\') -ieq (Get-KitHome).TrimEnd('\')) }

# Copies this kit to its home (backups\ and logs\ included, so nothing is lost),
# puts "Zappo for RG35XX" in the Start menu (and on the desktop if asked) and
# lists it under Apps & features with an uninstall that runs this kit's own
# uninstall. Returns the new home.
function Install-Kit([bool]$desktop) {
	$home_ = Get-KitHome
	if (-not (Test-RunningFromHome)) {
		[void][IO.Directory]::CreateDirectory($home_)
		foreach ($item in Get-ChildItem -LiteralPath $script:kit -Force) {
			$dst = Join-Path $home_ $item.Name
			if ($item.PSIsContainer) {
				if ($item.Name -in 'backups', 'logs') {
					# merge, keeping what is already there
					foreach ($f in Get-ChildItem -LiteralPath $item.FullName -Recurse -File) {
						$rel = $f.FullName.Substring($item.FullName.Length).TrimStart('\')
						$to = Join-Path $dst $rel
						if (-not (Test-Path -LiteralPath $to)) { [void][IO.Directory]::CreateDirectory((Split-Path -Parent $to)); Copy-Item -LiteralPath $f.FullName -Destination $to }
					}
				} else {
					if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Recurse -Force }
					Copy-Item -LiteralPath $item.FullName -Destination $dst -Recurse -Force
				}
			} else { Copy-Item -LiteralPath $item.FullName -Destination $dst -Force }
		}
	}
	if (-not $script:onWindows) { return $home_ }
	$ico = Join-Path $home_ 'zappo.ico'
	$target = Join-Path $home_ 'Zappo Setup.bat'
	$sh = New-Object -ComObject WScript.Shell
	$menu = Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs'
	foreach ($lnk in @((Join-Path $menu ($script:kitName + '.lnk'))) + $(if ($desktop) { @((Join-Path ([Environment]::GetFolderPath('Desktop')) ($script:kitName + '.lnk'))) } else { @() })) {
		$s = $sh.CreateShortcut($lnk)
		$s.TargetPath = $target
		$s.WorkingDirectory = $home_
		$s.IconLocation = $ico
		$s.Description = 'Zappo for the Anbernic RG35XX: install, films and music, box art, undo'
		$s.Save()
	}
	$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ZappoKit'
	if (-not (Test-Path $key)) { [void](New-Item -Path $key -Force) }
	Set-ItemProperty -Path $key -Name DisplayName -Value $script:kitName
	Set-ItemProperty -Path $key -Name DisplayVersion -Value $script:version
	Set-ItemProperty -Path $key -Name Publisher -Value 'CovGNG'
	Set-ItemProperty -Path $key -Name InstallLocation -Value $home_
	Set-ItemProperty -Path $key -Name DisplayIcon -Value $ico
	Set-ItemProperty -Path $key -Name URLInfoAbout -Value 'https://github.com/duffinharry-hub/zappo-x'
	Set-ItemProperty -Path $key -Name UninstallString -Value ('powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "' + (Join-Path $home_ 'Zappo Setup.ps1') + '" -Uninstall')
	Set-ItemProperty -Path $key -Name NoModify -Value 1 -Type DWord
	Set-ItemProperty -Path $key -Name NoRepair -Value 1 -Type DWord
	$size = 0; Get-ChildItem -LiteralPath $home_ -Recurse -File | ForEach-Object { $size += $_.Length }
	Set-ItemProperty -Path $key -Name EstimatedSize -Value ([int]($size / 1024)) -Type DWord
	return $home_
}

# Takes the kit off the PC. The card is not touched (that is "Remove Zappo
# from the card" in the window). backups\ and logs\ are kept in Documents.
function Uninstall-Kit {
	$home_ = Get-KitHome
	$kept = ''
	if ($script:onWindows) {
		foreach ($lnk in (Join-Path (Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs') ($script:kitName + '.lnk')), (Join-Path ([Environment]::GetFolderPath('Desktop')) ($script:kitName + '.lnk'))) {
			if (Test-Path -LiteralPath $lnk) { Remove-Item -LiteralPath $lnk -Force }
		}
		$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ZappoKit'
		if (Test-Path $key) { Remove-Item -Path $key -Recurse -Force }
	}
	if (Test-Path -LiteralPath $home_) {
		$my = [Environment]::GetFolderPath('MyDocuments'); if (-not $my) { $my = $HOME }
		$docs = Join-Path $my 'Zappo kit backups'
		foreach ($n in 'backups', 'logs') {
			$d = Join-Path $home_ $n
			if ((Test-Path -LiteralPath $d) -and (Get-ChildItem -LiteralPath $d -Recurse -File | Select-Object -First 1)) {
				[void][IO.Directory]::CreateDirectory($docs)
				$to = Join-Path $docs ($n + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
				Move-Item -LiteralPath $d -Destination $to -Force
				$kept = $docs
			}
		}
		# this script may be running from in there: leave the folder's removal to a moment later
		if ($script:onWindows -and (Test-RunningFromHome)) {
			Start-Process -FilePath 'cmd.exe' -ArgumentList ('/c ping -n 3 127.0.0.1 >nul & rmdir /s /q "' + $home_ + '"') -WindowStyle Hidden
		} else { Remove-Item -LiteralPath $home_ -Recurse -Force }
	}
	return $kept
}

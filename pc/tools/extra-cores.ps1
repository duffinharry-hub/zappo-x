# Zappo - installs a RetroArch cores pack onto the card, keeping GarlicOS's
# originals so it can be undone.
#   extra-cores.ps1            finds the pack in Downloads (a drive-download-*.zip,
#                              or a folder with a "cores" folder in it) and installs it
#   extra-cores.ps1 -Path X    installs from that zip or folder
#   extra-cores.ps1 -Revert    GarlicOS's originals back
# Known pack: the community "extra and updated rg35xx cores" (r/RG35XX, "FYI I'm
# still keeping RetroArch cores updated"): 12 newer builds of GarlicOS's cores
# compiled for the RG35XX's own CPU, plus 5 alternatives. Only 32-bit ARM cores
# are accepted; anything else in a pack is skipped. The pack's own licences
# apply to its cores (libretro cores are mostly GPL).
param([string]$Card = '', [string]$Path = '', [switch]$Revert)
. (Join-Path $PSScriptRoot 'common.ps1')
Log-Start 'Extra cores'
$card = Find-Card $Card
$ra = Join-Path $card 'CFW\retroarch\.retroarch'
$cores = Join-Path $ra 'cores'
if (-not (Test-Path -LiteralPath $cores)) { throw "$card has no CFW\retroarch\.retroarch\cores folder" }
$bk = Backup-Dir 'stock-cores'
$addedList = Join-Path $bk 'added.txt'

if ($Revert) {
	$n = 0
	foreach ($f in Get-ChildItem -LiteralPath $bk -File -Filter '*.so') { Copy-Verified $f.FullName (Join-Path $cores $f.Name); $n++ }
	if (Test-Path -LiteralPath $addedList) {
		foreach ($rel in (Get-Content -LiteralPath $addedList | Where-Object { $_ })) { $p = Join-Path $ra $rel; if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force; $n++ } }
	}
	if ($n) { Say "GarlicOS's own cores are back ($n files)." } else { Say 'Nothing to put back: no cores were replaced by this kit.' }
	exit 0
}

if (-not $Path) {
	$dl = Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Downloads'
	$cand = @(Get-ChildItem -LiteralPath $dl -ErrorAction SilentlyContinue | Where-Object {
		($_.PSIsContainer -and (Test-Path -LiteralPath (Join-Path $_.FullName 'cores'))) -or
		(-not $_.PSIsContainer -and $_.Extension -eq '.zip' -and $_.Name -like 'drive-download-*')
	} | Sort-Object LastWriteTime -Descending)
	if (-not $cand) { Say 'No cores pack found in Downloads (a drive-download-....zip, or a folder with a cores folder in it).'; exit 1 }
	$Path = $cand[0].FullName
}
Say "Pack: $Path"
$src = $Path
if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
	$src = Join-Path $script:kit 'backups\cores-pack-unpacked'
	if (Test-Path -LiteralPath $src) { Remove-Item -LiteralPath $src -Recurse -Force }
	Expand-Archive -LiteralPath $Path -DestinationPath $src -Force
}
if (-not (Test-Path -LiteralPath (Join-Path $src 'cores'))) {
	$inner = Get-ChildItem -LiteralPath $src -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'cores') } | Select-Object -First 1
	if ($inner) { $src = $inner.FullName } else { Say 'That pack has no cores folder - nothing done.'; exit 1 }
}
function Is-ArmCore([string]$f) {
	$b = New-Object byte[] 20
	$s = [IO.File]::OpenRead($f); try { $r = $s.Read($b, 0, 20) } finally { $s.Dispose() }
	return ($r -eq 20 -and $b[0] -eq 0x7F -and $b[1] -eq 0x45 -and $b[2] -eq 0x4C -and $b[3] -eq 0x46 -and $b[4] -eq 1 -and $b[5] -eq 1 -and $b[18] -eq 0x28 -and $b[19] -eq 0)
}
$added = @(); if (Test-Path -LiteralPath $addedList) { $added = @(Get-Content -LiteralPath $addedList | Where-Object { $_ }) }
$upd = 0; $new = 0; $skipped = 0
foreach ($f in Get-ChildItem -LiteralPath (Join-Path $src 'cores') -File -Filter '*_libretro.so') {
	if (-not (Is-ArmCore $f.FullName)) { Say "skipped (not an RG35XX core): $($f.Name)"; $skipped++; continue }
	$dst = Join-Path $cores $f.Name
	if (Test-Path -LiteralPath $dst) {
		if ((Md5 $dst) -eq (Md5 $f.FullName)) { continue }
		$b = Join-Path $bk $f.Name
		if (-not (Test-Path -LiteralPath $b)) { Copy-Verified $dst $b }
		$upd++; Say "updated: $($f.Name)"
	} else { $new++; Say "added:   $($f.Name)"; $rel = 'cores\' + $f.Name; if ($added -notcontains $rel) { $added += $rel } }
	Copy-Verified $f.FullName $dst
}
foreach ($sub in 'config', 'core_info') {
	$from = Join-Path $src $sub
	if (-not (Test-Path -LiteralPath $from)) { continue }
	foreach ($f in Get-ChildItem -LiteralPath $from -Recurse -File) {
		$rel = $sub + '\' + (Rel $from $f.FullName)
		$dst = Join-Path $ra $rel
		if (Test-Path -LiteralPath $dst) { continue }   # the card's own settings win
		Copy-Verified $f.FullName $dst
		if ($added -notcontains $rel) { $added += $rel }
	}
}
Set-Content -LiteralPath $addedList -Value $added
Say "DONE: $upd cores updated, $new added, $skipped skipped. GarlicOS's originals are in backups\stock-cores (Zappo.bat -> U puts them back)."

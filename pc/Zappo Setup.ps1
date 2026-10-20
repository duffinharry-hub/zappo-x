# Zappo Setup - the setup window for the Zappo kit (Windows).
# Run it with "Zappo Setup.bat" (or: powershell -ExecutionPolicy Bypass -STA -File "Zappo Setup.ps1").
# It walks through: the card -> what to do -> the kit on this PC -> doing it -> done,
# running the same scripts as Zappo.bat's text menu (tools\*.ps1).
#   -Uninstall   takes the kit off this PC (Apps & features calls this); the card is not touched
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'tools\setup-core.ps1')
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

# ---------------------------------------------------------------- look
$C = @{
	bg = [Drawing.Color]::FromArgb(18, 26, 51); panel = [Drawing.Color]::FromArgb(26, 34, 66); panelHi = [Drawing.Color]::FromArgb(34, 44, 84)
	line = [Drawing.Color]::FromArgb(58, 68, 110); gold = [Drawing.Color]::FromArgb(242, 191, 47); text = [Drawing.Color]::FromArgb(243, 241, 232)
	dim = [Drawing.Color]::FromArgb(162, 168, 200); ok = [Drawing.Color]::FromArgb(95, 211, 141); bad = [Drawing.Color]::FromArgb(255, 122, 107)
	navy = [Drawing.Color]::FromArgb(10, 14, 32)
}
$F = @{
	h1 = New-Object Drawing.Font('Segoe UI Semibold', 24); h2 = New-Object Drawing.Font('Segoe UI Semibold', 12)
	body = New-Object Drawing.Font('Segoe UI', 10); small = New-Object Drawing.Font('Segoe UI', 9); mono = New-Object Drawing.Font('Consolas', 9)
	eyebrow = New-Object Drawing.Font('Segoe UI', 8, [Drawing.FontStyle]::Bold)
}

function L([string]$text, $font, $color, $x, $y, $w = 0, $h = 0) {
	$l = New-Object Windows.Forms.Label
	$l.Text = $text; $l.Font = $font; $l.ForeColor = $color; $l.BackColor = [Drawing.Color]::Transparent
	$l.Location = New-Object Drawing.Point($x, $y)
	if ($w) { $l.AutoSize = $false; $l.Size = New-Object Drawing.Size($w, $(if ($h) { $h } else { 24 })) } else { $l.AutoSize = $true }
	return $l
}
function B([string]$text, $x, $y, $w = 110, [bool]$primary = $false) {
	$b = New-Object Windows.Forms.Button
	$b.Text = $text; $b.Font = $F.body; $b.FlatStyle = 'Flat'; $b.Size = New-Object Drawing.Size($w, 34)
	$b.Location = New-Object Drawing.Point($x, $y)
	if ($primary) { $b.BackColor = $C.gold; $b.ForeColor = $C.navy; $b.FlatAppearance.BorderColor = $C.gold }
	else { $b.BackColor = $C.panel; $b.ForeColor = $C.text; $b.FlatAppearance.BorderColor = $C.line }
	$b.FlatAppearance.BorderSize = 1
	return $b
}
# a choice as a card: a tick, a title and a line under it; clicking anywhere on it toggles
function Card([string]$title, [string]$desc, $x, $y, $w, [bool]$on, [string]$id) {
	$p = New-Object Windows.Forms.Panel
	$p.Location = New-Object Drawing.Point($x, $y); $p.Size = New-Object Drawing.Size($w, 64)
	$p.BackColor = $C.panel; $p.BorderStyle = 'FixedSingle'; $p.Cursor = 'Hand'; $p.Tag = $id
	$cb = New-Object Windows.Forms.CheckBox
	$cb.Location = New-Object Drawing.Point(12, 22); $cb.Size = New-Object Drawing.Size(20, 20); $cb.Checked = $on
	$cb.BackColor = [Drawing.Color]::Transparent; $cb.ForeColor = $C.text; $cb.Name = 'tick'
	$t = L $title $F.h2 $C.text 40 8 ($w - 52) 22
	$d = L $desc $F.small $C.dim 40 31 ($w - 52) 28
	foreach ($c in $t, $d) { $c.Cursor = 'Hand'; $c.Add_Click({ $this.Parent.Controls['tick'].Checked = -not $this.Parent.Controls['tick'].Checked }) }
	$p.Add_Click({ $this.Controls['tick'].Checked = -not $this.Controls['tick'].Checked })
	$cb.Add_CheckedChanged({ $this.Parent.BackColor = $(if ($this.Checked) { $C.panelHi } else { $C.panel }) })
	if ($on) { $p.BackColor = $C.panelHi }
	$p.Controls.AddRange(@($cb, $t, $d))
	return $p
}

# ---------------------------------------------------------------- window
$form = New-Object Windows.Forms.Form
$form.Text = "Zappo Setup - $($script:kitName)"
$form.Size = New-Object Drawing.Size(900, 620); $form.MinimumSize = $form.Size
$form.StartPosition = 'CenterScreen'; $form.BackColor = $C.bg; $form.ForeColor = $C.text
$form.FormBorderStyle = 'FixedSingle'; $form.MaximizeBox = $false
try { $form.Icon = New-Object Drawing.Icon((Join-Path $script:kit 'zappo.ico')) } catch {}

$head = New-Object Windows.Forms.Panel
$head.Dock = 'Top'; $head.Height = 64; $head.BackColor = $C.navy
try {
	$pic = New-Object Windows.Forms.PictureBox
	$pic.Image = [Drawing.Image]::FromFile((Join-Path $script:kit 'zappo.png')); $pic.SizeMode = 'Zoom'
	$pic.Location = New-Object Drawing.Point(24, 10); $pic.Size = New-Object Drawing.Size(44, 44)
	$head.Controls.Add($pic)
} catch {}
$head.Controls.Add((L 'ZAPPO  -  ANBERNIC RG35XX (ORIGINAL)  -  GARLICOS' $F.eyebrow $C.gold 80 14))
$head.Controls.Add((L "Zappo $($script:version) setup" $F.h2 $C.text 80 32))
$stepLbl = L '' $F.small $C.dim 700 24 170 20; $stepLbl.TextAlign = 'MiddleRight'
$head.Controls.Add($stepLbl)

$foot = New-Object Windows.Forms.Panel
$foot.Dock = 'Bottom'; $foot.Height = 62; $foot.BackColor = $C.navy
$btnBack = B 'Back' 530 14; $btnNext = B 'Next' 650 14 110 $true; $btnCancel = B 'Cancel' 770 14
$foot.Controls.AddRange(@($btnBack, $btnNext, $btnCancel))

$body = New-Object Windows.Forms.Panel
$body.Dock = 'Fill'; $body.BackColor = $C.bg
$form.Controls.AddRange(@($body, $head, $foot))

$pages = @{}
function Page([string]$name) { $p = New-Object Windows.Forms.Panel; $p.Dock = 'Fill'; $p.BackColor = $C.bg; $p.Visible = $false; $pages[$name] = $p; $body.Controls.Add($p); return $p }

# ---- 1. the card
$pg = Page 'card'
$pg.Controls.Add((L 'Your GarlicOS card' $F.h1 $C.text 40 28))
$pg.Controls.Add((L "Put the handheld's card in this PC. Windows shows its ROMS partition (the one with the CFW, BIOS and Roms folders) as a drive, and sometimes the small boot partition too." $F.body $C.dim 40 80 790 44))
$pg.Controls.Add((L 'Card (ROMS partition)' $F.h2 $C.text 40 140))
$cardBox = New-Object Windows.Forms.ComboBox
$cardBox.Location = New-Object Drawing.Point(40, 168); $cardBox.Size = New-Object Drawing.Size(420, 30); $cardBox.Font = $F.body
$cardBox.DropDownStyle = 'DropDownList'; $cardBox.BackColor = $C.panel; $cardBox.ForeColor = $C.text; $cardBox.FlatStyle = 'Flat'
$btnScan = B 'Look again' 472 166 120
$btnBrowse = B 'Choose a folder...' 604 166 150
$cardNote = L '' $F.small $C.dim 40 206 790 40
$pg.Controls.Add((L 'Boot partition' $F.h2 $C.text 40 256))
$bootLbl = L '' $F.body $C.dim 40 284 790 44
$pg.Controls.Add((L 'Before anything else: back the card up (Win32 Disk Imager or balenaEtcher saves an image of the whole card). Everything this setup changes can be undone from here, but a backup is the real safety net.' $F.small $C.dim 40 350 790 44))
$pg.Controls.AddRange(@($cardBox, $btnScan, $btnBrowse, $cardNote, $bootLbl))
$undoLink = New-Object Windows.Forms.LinkLabel
$undoLink.Text = 'Undo something, or take Zappo off the card instead'; $undoLink.Font = $F.body; $undoLink.LinkColor = $C.gold; $undoLink.ActiveLinkColor = $C.text
$undoLink.Location = New-Object Drawing.Point(40, 420); $undoLink.AutoSize = $true; $undoLink.BackColor = [Drawing.Color]::Transparent
$pg.Controls.Add($undoLink)

$state = @{ card = ''; boot = ''; mode = 'do'; jobs = @(); results = @(); i = -1; cur = $null; home = ''; page = ''; wants = @{}; paths = @() }
function Scan-Cards {
	$d = Get-CardDrives
	$cardBox.Items.Clear(); $state.paths = @()
	foreach ($c in $d.cards) {
		$state.paths += $c
		$info = Get-CardInfo $c
		$txt = $c + '  ' + $(if ($info.label) { "($($info.label))  " } else { '' }) + $(if ($info.zappo) { 'Zappo already on it' } else { 'GarlicOS, no Zappo yet' })
		[void]$cardBox.Items.Add($txt)
	}
	if ($cardBox.Items.Count) { $cardBox.SelectedIndex = 0; $cardNote.Text = $(if ($cardBox.Items.Count -gt 1) { 'More than one card is in: pick the right one.' } else { '' }) }
	else { $cardNote.Text = 'No GarlicOS card found yet. Put the card in and press Look again, or choose its drive with Choose a folder.' }
	$state.boot = $(if ($d.boots.Count) { $d.boots[0] } else { '' })
	$bootLbl.Text = $(if ($state.boot) { "Found at $($state.boot): the Zappo boot logo and charging animation can go on." } else { "Not showing in Windows, so the boot logo and charging animation will be skipped this time (they are optional; Zappo works without them)." })
	$bootLbl.ForeColor = $(if ($state.boot) { $C.ok } else { $C.dim })
}
function Selected-Card { if ($cardBox.SelectedIndex -lt 0 -or $cardBox.SelectedIndex -ge $state.paths.Count) { return '' }; return $state.paths[$cardBox.SelectedIndex] }
$btnScan.Add_Click({ Scan-Cards })
$btnBrowse.Add_Click({
	$dlg = New-Object Windows.Forms.FolderBrowserDialog
	$dlg.Description = 'The GarlicOS card: the drive with the CFW, BIOS and Roms folders'
	if ($dlg.ShowDialog($form) -eq 'OK') {
		$r = $dlg.SelectedPath.TrimEnd('\') + '\'
		if (Test-Path -LiteralPath (Join-Path $r 'CFW')) { $state.paths = @($r) + $state.paths; $cardBox.Items.Insert(0, $r + '  (chosen)'); $cardBox.SelectedIndex = 0; $cardNote.Text = ''; $cardNote.ForeColor = $C.dim }
		else { $cardNote.Text = "$r has no CFW folder, so it isn't a GarlicOS card."; $cardNote.ForeColor = $C.bad }
	}
})
$undoLink.Add_LinkClicked({ $state.mode = 'undo'; Show-Page 'undo' })

# ---- 2. what to do
$pg = Page 'what'
$pg.Controls.Add((L 'What would you like done?' $F.h1 $C.text 40 28))
$pg.Controls.Add((L 'Tick what you want. Each one is the same job as a number on the Zappo.bat menu; they run in order, and anything they replace on the card is kept in backups\ so it can be undone.' $F.body $C.dim 40 80 790 44))
$cw = 395; $x1 = 40; $x2 = 455
$pg.Controls.Add((Card 'Install / update Zappo' 'Zappo onto the card and the video player fix. Safe to run again any time.' $x1 136 $cw $true 'install'))
$pg.Controls.Add((Card 'Boot logo and charging animation' "Zappo's pictures on the boot partition, when Windows shows it." $x2 136 $cw $false 'boot'))
$pg.Controls.Add((Card 'Films and music from Downloads' 'Downloads, Downloads\videos and Downloads\music onto the card; big films shrunk to fit.' $x1 210 $cw $false 'media'))
$pg.Controls.Add((Card 'Unpack downloaded video zips first' 'Zipped series in Downloads become folders on the Videos tab.' $x2 210 $cw $false 'unpack'))
$pg.Controls.Add((Card 'Box art, film posters and music covers' 'From the libretro library, TheGamesDB, Wikipedia, Deezer and iTunes. Needs the internet.' $x1 284 $cw $false 'art'))
$pg.Controls.Add((Card 'Extra cores pack from Downloads' "The community's newer emulator builds, if the pack is in Downloads." $x2 284 $cw $false 'cores'))
$pg.Controls.Add((Card 'Check the card afterwards' 'Reads everything back and reports; changes nothing.' $x1 358 $cw $true 'check'))
$whatNote = L '' $F.small $C.dim 40 440 790 40
$pg.Controls.Add($whatNote)

# ---- 2b. undo
$pg = Page 'undo'
$pg.Controls.Add((L 'Undo, or take Zappo off' $F.h1 $C.text 40 28))
$pg.Controls.Add((L "Each of these puts back what the kit kept in backups\. Your games, saves, music and films are never touched. Taking Zappo off gives GarlicOS its own menu back; Zappo's settings folder is moved into backups\, not deleted." $F.body $C.dim 40 80 790 60))
$pg.Controls.Add((Card "GarlicOS's video player back" 'Undoes the 47-byte player fix.' $x1 150 $cw $false 'undoPlayer'))
$pg.Controls.Add((Card "GarlicOS's own cores back" 'Undoes the extra cores pack.' $x2 150 $cw $false 'undoCores'))
$pg.Controls.Add((Card 'GarlicOS boot pictures back' 'Undoes the boot logo and charging animation.' $x1 224 $cw $false 'undoBoot'))
$pg.Controls.Add((Card 'Remove Zappo from the card' "GarlicOS's menu back at the next start." $x2 224 $cw $false 'remove'))
$pg.Controls.Add((Card 'Take the Zappo kit off this PC too' 'Start menu entry and Apps & features listing go; backups and logs are kept in Documents.' $x1 298 $cw $false 'uninstallKit'))

# ---- 3. on this PC
$pg = Page 'pc'
$pg.Controls.Add((L 'The kit on this PC' $F.h1 $C.text 40 28))
$pg.Controls.Add((L 'The setup can stay on this PC like any program, so adding films or box art later is one click from the Start menu. It keeps its backups and logs with it. Nothing else on the PC is changed, and it is only for you (no administrator needed).' $F.body $C.dim 40 80 790 60))
$rbInstall = New-Object Windows.Forms.RadioButton; $rbHere = New-Object Windows.Forms.RadioButton
foreach ($rb in $rbInstall, $rbHere) { $rb.Font = $F.h2; $rb.ForeColor = $C.text; $rb.BackColor = [Drawing.Color]::Transparent; $rb.AutoSize = $true }
$rbInstall.Text = 'Install the kit on this PC (Start menu, Apps & features)'; $rbInstall.Location = New-Object Drawing.Point(44, 160); $rbInstall.Checked = $true
$pg.Controls.Add((L ('Goes to ' + (Get-KitHome)) $F.small $C.dim 68 188 700 20))
$cbDesktop = New-Object Windows.Forms.CheckBox; $cbDesktop.Text = 'A shortcut on the desktop as well'; $cbDesktop.Font = $F.body; $cbDesktop.ForeColor = $C.text
$cbDesktop.BackColor = [Drawing.Color]::Transparent; $cbDesktop.AutoSize = $true; $cbDesktop.Location = New-Object Drawing.Point(68, 214)
$rbHere.Text = 'Just run it from this folder'; $rbHere.Location = New-Object Drawing.Point(44, 262)
$pg.Controls.Add((L 'Nothing is copied; the unzipped folder is the kit. You can install it later from here.' $F.small $C.dim 68 290 700 20))
$pg.Controls.AddRange(@($rbInstall, $cbDesktop, $rbHere))
if (Test-RunningFromHome) { $rbHere.Checked = $true; $rbInstall.Text = 'Refresh the Start menu entry'; }

# ---- 4. doing it
$pg = Page 'run'
$runTitle = L 'Working...' $F.h1 $C.text 40 28
$runStep = L '' $F.h2 $C.gold 40 84 790 24
$bar = New-Object Windows.Forms.ProgressBar; $bar.Location = New-Object Drawing.Point(40, 116); $bar.Size = New-Object Drawing.Size(790, 8); $bar.Style = 'Continuous'
$logBox = New-Object Windows.Forms.TextBox
$logBox.Multiline = $true; $logBox.ReadOnly = $true; $logBox.ScrollBars = 'Vertical'; $logBox.Font = $F.mono
$logBox.BackColor = $C.navy; $logBox.ForeColor = $C.text; $logBox.BorderStyle = 'FixedSingle'
$logBox.Location = New-Object Drawing.Point(40, 136); $logBox.Size = New-Object Drawing.Size(790, 330)
$pg.Controls.AddRange(@($runTitle, $runStep, $bar, $logBox))

# ---- 5. done
$pg = Page 'done'
$doneTitle = L 'All done' $F.h1 $C.text 40 28
$doneList = New-Object Windows.Forms.TextBox
$doneList.Multiline = $true; $doneList.ReadOnly = $true; $doneList.Font = $F.body; $doneList.BackColor = $C.bg; $doneList.ForeColor = $C.text; $doneList.BorderStyle = 'None'
$doneList.Location = New-Object Drawing.Point(40, 84); $doneList.Size = New-Object Drawing.Size(790, 230)
$doneNote = L '' $F.body $C.dim 40 320 790 70
$btnOpenCard = B 'Open the card' 40 400 140; $btnOpenLogs = B 'Open the logs' 192 400 140; $btnOpenKit = B 'Open the kit folder' 344 400 160
$pg.Controls.AddRange(@($doneTitle, $doneList, $doneNote, $btnOpenCard, $btnOpenLogs, $btnOpenKit))
$btnOpenCard.Add_Click({ if ($state.card) { Start-Process explorer.exe $state.card } })
$btnOpenLogs.Add_Click({ Start-Process explorer.exe (Join-Path $(if ($state.home) { $state.home } else { $script:kit }) 'logs') })
$btnOpenKit.Add_Click({ Start-Process explorer.exe $(if ($state.home) { $state.home } else { $script:kit }) })

# ---------------------------------------------------------------- flow
$order = @('card', 'what', 'pc', 'run', 'done')
function Show-Page([string]$name) {
	foreach ($k in $pages.Keys) { $pages[$k].Visible = ($k -eq $name) }
	$state.page = $name
	$n = switch ($name) { 'card' { 1 } 'what' { 2 } 'undo' { 2 } 'pc' { 3 } 'run' { 4 } 'done' { 5 } }
	$stepLbl.Text = "Step $n of 5"
	$btnBack.Enabled = $name -in 'what', 'undo', 'pc'
	$btnNext.Enabled = $true
	$btnNext.Text = switch ($name) { 'pc' { 'Start' } 'run' { 'Working...' } 'done' { 'Finish' } default { 'Next' } }
	$btnCancel.Visible = $name -ne 'done'
	if ($name -eq 'run') { $btnNext.Enabled = $false; $btnCancel.Text = 'Stop' } else { $btnCancel.Text = 'Cancel' }
	if ($name -eq 'what') {
		# what makes sense for this card
		$info = Get-CardInfo $state.card
		$whatNote.Text = $(if ($info.zappo) { "This card already has Zappo on it: Install / update brings it up to $($script:version) and keeps your settings." } else { 'First time on this card: Install / update puts Zappo on; the handheld finishes the install at its next start.' })
		foreach ($c in $pages['what'].Controls) { if ($c.Tag -eq 'boot') { $c.Controls['tick'].Checked = [bool]$state.boot; $c.Enabled = [bool]$state.boot } }
	}
}
function Collect-Wants {
	$w = @{}
	$pg = $pages[$(if ($state.mode -eq 'undo') { 'undo' } else { 'what' })]
	foreach ($c in $pg.Controls) { if ($c.Tag -and $c.Controls['tick']) { $w[$c.Tag] = [bool]$c.Controls['tick'].Checked } }
	return $w
}

function Start-Run {
	$wants = Collect-Wants
	$state.results = @(); $state.i = -1; $state.cur = $null; $state.wants = $wants
	$logBox.Clear(); $bar.Value = 0; $bar.Maximum = 1
	Show-Page 'run'
	# the kit on the PC first: the jobs then run from there, so backups\ and logs\ live with it
	if ($state.mode -eq 'do' -and $rbInstall.Checked) {
		$runStep.Text = 'Putting the kit on this PC...'
		[Windows.Forms.Application]::DoEvents()
		try { $state.home = Install-Kit $cbDesktop.Checked; $script:kit = $state.home; $logBox.AppendText("Kit installed to $($state.home) - 'Zappo for RG35XX' is in the Start menu.`r`n") }
		catch { $logBox.AppendText("Couldn't install the kit on this PC: $($_.Exception.Message)`r`n") }
	}
	$state.jobs = @(Get-Jobs $wants $state.card $state.boot)
	$bar.Maximum = [Math]::Max(1, $state.jobs.Count + 1); $bar.Value = 1
	$timer.Start()
}
function Next-Job {
	$state.i++
	if ($state.i -ge $state.jobs.Count) { Finish-Run; return }
	$j = $state.jobs[$state.i]
	$runStep.Text = "$($state.i + 1) of $($state.jobs.Count): $($j.title)"
	$logBox.AppendText("`r`n== $($j.title)`r`n")
	try { $state.cur = Start-KitJob $j }
	catch { $logBox.AppendText("Couldn't start it: $($_.Exception.Message)`r`n"); $state.results += @{ title = $j.title; ok = $false }; $state.cur = $null; Next-Job }
}
$timer = New-Object Windows.Forms.Timer; $timer.Interval = 250
$timer.Add_Tick({
	if ($null -eq $state.cur) { $timer.Stop(); Next-Job; if ($state.cur) { $timer.Start() }; return }
	foreach ($line in (Read-JobOutput $state.cur)) { if ($line.Length) { $logBox.AppendText($line + "`r`n") } }
	if (Test-JobDone $state.cur) {
		$timer.Stop()
		$code = Complete-KitJob $state.cur
		foreach ($line in (Read-JobOutput $state.cur)) { if ($line.Length) { $logBox.AppendText($line + "`r`n") } }
		$state.results += @{ title = $state.cur.title; ok = ($code -eq 0) }
		$state.cur = $null
		$bar.Value = [Math]::Min($bar.Maximum, $bar.Value + 1)
		Next-Job
		if ($state.cur) { $timer.Start() }
	}
})
function Finish-Run {
	$timer.Stop()
	if ($state.mode -eq 'undo' -and $state.wants.uninstallKit) {
		try { $kept = Uninstall-Kit; $state.results += @{ title = 'Zappo kit taken off this PC' + $(if ($kept) { " (backups and logs kept in $kept)" } else { '' }); ok = $true } }
		catch { $state.results += @{ title = "Taking the kit off this PC: $($_.Exception.Message)"; ok = $false } }
	}
	$bad = @($state.results | Where-Object { -not $_.ok }).Count
	$doneTitle.Text = $(if ($bad) { 'Done, with something to look at' } else { 'All done' })
	$doneList.Text = (($state.results | ForEach-Object { $(if ($_.ok) { [char]0x2713 } else { [char]0x2717 }) + '  ' + $_.title }) -join "`r`n")
	$doneNote.Text = $(if ($state.mode -eq 'undo') { 'Eject the card before taking it out.' }
		elseif ($bad) { "Something didn't finish: the log on the previous screen says what, and logs\ in the kit keeps it. The rest went ahead." }
		else { "Eject the card, put it in the handheld and switch on. The first start finishes the install and opens Zappo. `"Zappo manual.txt`" in the kit is the full guide." })
	Show-Page 'done'
}

$btnNext.Add_Click({
	switch ($state.page) {
		'card' {
			$state.card = Selected-Card
			if (-not $state.card) { $cardNote.Text = 'Choose the card first (or put it in and press Look again).'; $cardNote.ForeColor = $C.bad; return }
			$state.mode = 'do'; Show-Page 'what'
		}
		'what' { if (-not ((Collect-Wants).Values -contains $true)) { $whatNote.Text = 'Tick at least one thing.'; $whatNote.ForeColor = $C.bad; return }; Show-Page 'pc' }
		'undo' {
			$w = Collect-Wants
			if (-not ($w.Values -contains $true)) { return }
			$state.card = Selected-Card
			if (-not $state.card -and ($w.undoPlayer -or $w.undoCores -or $w.remove)) { [void][Windows.Forms.MessageBox]::Show($form, 'Those need the card: put it in and choose it on the first screen.', 'Zappo Setup'); return }
			if ($w.remove -and [Windows.Forms.MessageBox]::Show($form, "Take Zappo off the card? GarlicOS's own menu comes back at the next start. Zappo's settings are kept in backups\.", 'Zappo Setup', 'YesNo', 'Question') -ne 'Yes') { return }
			Start-Run
		}
		'pc' { Start-Run }
		'done' { $form.Close() }
	}
})
$btnBack.Add_Click({ switch ($state.page) { 'what' { Show-Page 'card' } 'undo' { Show-Page 'card' } 'pc' { Show-Page 'what' } } })
$btnCancel.Add_Click({
	if ($state.page -eq 'run') {
		if ([Windows.Forms.MessageBox]::Show($form, 'Stop after the current job? (Stopping a copy halfway is not safe for the card.)', 'Zappo Setup', 'YesNo', 'Question') -eq 'Yes') { $state.jobs = $(if ($state.i -ge 0) { @($state.jobs[0..$state.i]) } else { @() }) }
	} else { $form.Close() }
})
$form.Add_FormClosing({ if ($state.page -eq 'run' -and $state.cur -and -not (Test-JobDone $state.cur)) { $_.Cancel = $true } })

# ---------------------------------------------------------------- go
if ($Uninstall) {
	if ([Windows.Forms.MessageBox]::Show("Take the Zappo kit off this PC? The handheld's card is not touched. Backups and logs are kept in Documents\Zappo kit backups.", 'Zappo Setup', 'YesNo', 'Question') -eq 'Yes') {
		$kept = Uninstall-Kit
		[void][Windows.Forms.MessageBox]::Show('The Zappo kit is off this PC.' + $(if ($kept) { " Backups and logs: $kept" } else { '' }), 'Zappo Setup')
	}
	return
}
Scan-Cards
Show-Page 'card'
[void]$form.ShowDialog()

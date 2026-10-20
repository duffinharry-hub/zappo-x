# Zappo Setup - the setup window for the Zappo kit (Windows).
# Run it with "Zappo Setup.bat" (or: powershell -ExecutionPolicy Bypass -STA -File "Zappo Setup.ps1").
# It walks through: the card -> what to do -> the kit on this PC -> doing it -> done,
# running the same scripts as Zappo.bat's text menu (tools\*.ps1).
#   -Uninstall   takes the kit off this PC (Apps & features calls this); the card is not touched
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
try {
. (Join-Path $PSScriptRoot 'tools\setup-core.ps1')
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

# ---------------------------------------------------------------- look
# ($UI, not $C: PowerShell variable names ignore case, and $c is a handy loop name)
$UI = @{
	bg = [Drawing.Color]::FromArgb(18, 26, 51); panel = [Drawing.Color]::FromArgb(26, 34, 66); panelHi = [Drawing.Color]::FromArgb(34, 44, 84)
	line = [Drawing.Color]::FromArgb(58, 68, 110); gold = [Drawing.Color]::FromArgb(242, 191, 47); text = [Drawing.Color]::FromArgb(243, 241, 232)
	dim = [Drawing.Color]::FromArgb(162, 168, 200); ok = [Drawing.Color]::FromArgb(95, 211, 141); bad = [Drawing.Color]::FromArgb(255, 122, 107)
	navy = [Drawing.Color]::FromArgb(10, 14, 32)
}
$FONT = @{
	h1 = New-Object Drawing.Font('Segoe UI Semibold', 24); h2 = New-Object Drawing.Font('Segoe UI Semibold', 12)
	body = New-Object Drawing.Font('Segoe UI', 10); small = New-Object Drawing.Font('Segoe UI', 9); mono = New-Object Drawing.Font('Consolas', 9)
	eyebrow = New-Object Drawing.Font('Segoe UI', 8, [Drawing.FontStyle]::Bold)
}

function L([string]$text, $font, $color, $x, $y, $w = 0, $h = 0) {
	$l = New-Object Windows.Forms.Label
	$l.Text = $text; $l.Font = $font; $l.ForeColor = $color; $l.BackColor = [Drawing.Color]::Transparent; $l.UseMnemonic = $false
	$l.Location = New-Object Drawing.Point($x, $y)
	if ($w) { $l.AutoSize = $false; $l.Size = New-Object Drawing.Size($w, $(if ($h) { $h } else { 24 })) } else { $l.AutoSize = $true }
	return $l
}
function B([string]$text, $x, $y, $w = 110, [bool]$primary = $false) {
	$b = New-Object Windows.Forms.Button
	$b.Text = $text; $b.Font = $script:FONT.body; $b.FlatStyle = 'Flat'; $b.Size = New-Object Drawing.Size($w, 34)
	$b.Location = New-Object Drawing.Point($x, $y)
	if ($primary) { $b.BackColor = $script:UI.gold; $b.ForeColor = $script:UI.navy; $b.FlatAppearance.BorderColor = $script:UI.gold }
	else { $b.BackColor = $script:UI.panel; $b.ForeColor = $script:UI.text; $b.FlatAppearance.BorderColor = $script:UI.line }
	$b.FlatAppearance.BorderSize = 1
	return $b
}
# a choice as a card: a tick, a title and a line under it; clicking anywhere on it toggles
function Card([string]$title, [string]$desc, $x, $y, $w, [bool]$on, [string]$id) {
	$p = New-Object Windows.Forms.Panel
	$p.Location = New-Object Drawing.Point($x, $y); $p.Size = New-Object Drawing.Size($w, 66)
	$p.BackColor = $script:UI.panel; $p.BorderStyle = 'FixedSingle'; $p.Cursor = 'Hand'; $p.Tag = $id
	$cb = New-Object Windows.Forms.CheckBox
	$cb.Location = New-Object Drawing.Point(12, 22); $cb.Size = New-Object Drawing.Size(20, 20); $cb.Checked = $on
	$cb.BackColor = [Drawing.Color]::Transparent; $cb.ForeColor = $script:UI.text; $cb.Name = 'tick'
	$t = L $title $script:FONT.h2 $script:UI.text 40 8 ($w - 52) 22
	$d = L $desc $script:FONT.small $script:UI.dim 40 31 ($w - 52) 32
	foreach ($ctl in $t, $d) { $ctl.Cursor = 'Hand'; $ctl.Add_Click({ try { $this.Parent.Controls['tick'].Checked = -not $this.Parent.Controls['tick'].Checked } catch {} }) }
	$p.Add_Click({ try { $this.Controls['tick'].Checked = -not $this.Controls['tick'].Checked } catch {} })
	$cb.Add_CheckedChanged({ try { $this.Parent.BackColor = $(if ($this.Checked) { $script:UI.panelHi } else { $script:UI.panel }) } catch {} })
	if ($on) { $p.BackColor = $script:UI.panelHi }
	$p.Controls.AddRange(@($cb, $t, $d))
	return $p
}
function Open-Folder([string]$path) { try { if ($path) { [void][IO.Directory]::CreateDirectory($path); Start-Process -FilePath $path } } catch {} }
function Oops([string]$where, $err) {
	$m = "$where`: $($err.Exception.Message)"
	try { if ($script:logBox) { $script:logBox.AppendText($m + "`r`n") } } catch {}
	try { if ($script:form) { [void][Windows.Forms.MessageBox]::Show($script:form, $m, 'Zappo Setup') } else { [void][Windows.Forms.MessageBox]::Show($m, 'Zappo Setup') } } catch {}
}

# ---------------------------------------------------------------- window
$form = New-Object Windows.Forms.Form
$form.Text = "Zappo Setup - $($script:kitName)"
$form.Size = New-Object Drawing.Size(900, 660); $form.MinimumSize = $form.Size
$form.StartPosition = 'CenterScreen'; $form.BackColor = $UI.bg; $form.ForeColor = $UI.text
$form.FormBorderStyle = 'FixedSingle'; $form.MaximizeBox = $false
try { $form.Icon = New-Object Drawing.Icon((Join-Path $script:kit 'zappo.ico')) } catch {}

$head = New-Object Windows.Forms.Panel
$head.Dock = 'Top'; $head.Height = 64; $head.BackColor = $UI.navy
try {
	$pic = New-Object Windows.Forms.PictureBox
	$pic.Image = [Drawing.Image]::FromFile((Join-Path $script:kit 'zappo.png')); $pic.SizeMode = 'Zoom'
	$pic.Location = New-Object Drawing.Point(24, 10); $pic.Size = New-Object Drawing.Size(44, 44)
	$head.Controls.Add($pic)
} catch {}
$head.Controls.Add((L 'ZAPPO  -  ANBERNIC RG35XX (ORIGINAL)  -  GARLICOS' $FONT.eyebrow $UI.gold 80 14))
$head.Controls.Add((L "Zappo $($script:version) setup" $FONT.h2 $UI.text 80 32))
$stepLbl = L '' $FONT.small $UI.dim 700 24 170 20; $stepLbl.TextAlign = 'MiddleRight'
$head.Controls.Add($stepLbl)

$foot = New-Object Windows.Forms.Panel
$foot.Dock = 'Bottom'; $foot.Height = 62; $foot.BackColor = $UI.navy
$btnBack = B 'Back' 530 14; $btnNext = B 'Next' 650 14 110 $true; $btnCancel = B 'Cancel' 770 14
$foot.Controls.AddRange(@($btnBack, $btnNext, $btnCancel))

$body = New-Object Windows.Forms.Panel
$body.Dock = 'Fill'; $body.BackColor = $UI.bg
$form.Controls.AddRange(@($body, $head, $foot))

$pages = @{}
function Page([string]$name) { $p = New-Object Windows.Forms.Panel; $p.Dock = 'Fill'; $p.BackColor = $script:UI.bg; $p.Visible = $false; $script:pages[$name] = $p; $script:body.Controls.Add($p); return $p }

# ---- 1. the card
$pg = Page 'card'
$pg.Controls.Add((L 'Your GarlicOS card' $FONT.h1 $UI.text 40 28))
$pg.Controls.Add((L "Put the handheld's card in this PC. Windows shows its ROMS partition (the one with the CFW, BIOS and Roms folders) as a drive, and sometimes the small boot partition too." $FONT.body $UI.dim 40 80 790 44))
$pg.Controls.Add((L 'Card (ROMS partition)' $FONT.h2 $UI.text 40 140))
$cardBox = New-Object Windows.Forms.ComboBox
$cardBox.Location = New-Object Drawing.Point(40, 168); $cardBox.Size = New-Object Drawing.Size(420, 30); $cardBox.Font = $FONT.body
$cardBox.DropDownStyle = 'DropDownList'; $cardBox.BackColor = $UI.panel; $cardBox.ForeColor = $UI.text; $cardBox.FlatStyle = 'Flat'
$btnScan = B 'Look again' 472 166 120
$btnBrowse = B 'Choose a folder...' 604 166 150
$cardNote = L '' $FONT.small $UI.dim 40 206 790 40
$pg.Controls.Add((L 'Boot partition' $FONT.h2 $UI.text 40 256))
$bootLbl = L '' $FONT.body $UI.dim 40 284 790 44
$pg.Controls.Add((L 'Before anything else: back the card up (Win32 Disk Imager or balenaEtcher saves an image of the whole card). Nothing here deletes your games, saves, music or films - what the kit replaces is kept in backups\ and can be put back from this window - but an image is the real safety net.' $FONT.small $UI.dim 40 350 790 60))
$pg.Controls.AddRange(@($cardBox, $btnScan, $btnBrowse, $cardNote, $bootLbl))
$undoLink = New-Object Windows.Forms.LinkLabel
$undoLink.Text = 'Undo something, or take Zappo off the card instead'; $undoLink.Font = $FONT.body; $undoLink.LinkColor = $UI.gold; $undoLink.ActiveLinkColor = $UI.text
$undoLink.Location = New-Object Drawing.Point(40, 430); $undoLink.AutoSize = $true; $undoLink.BackColor = [Drawing.Color]::Transparent
$pg.Controls.Add($undoLink)

$state = @{ card = ''; boot = ''; mode = 'do'; jobs = @(); results = @(); i = -1; cur = $null; home = ''; page = ''; wants = @{}; paths = @(); busy = $false }
function Scan-Cards {
	$d = Get-CardDrives
	$script:cardBox.Items.Clear(); $script:state.paths = @()
	foreach ($cardPath in $d.cards) {
		$script:state.paths += $cardPath
		$info = Get-CardInfo $cardPath
		$txt = $cardPath + '  ' + $(if ($info.label) { "($($info.label))  " } else { '' }) + $(if ($info.zappo) { 'Zappo already on it' } else { 'GarlicOS, no Zappo yet' })
		[void]$script:cardBox.Items.Add($txt)
	}
	$script:cardNote.ForeColor = $script:UI.dim
	if ($script:cardBox.Items.Count) { $script:cardBox.SelectedIndex = 0; $script:cardNote.Text = $(if ($script:cardBox.Items.Count -gt 1) { 'More than one card is in: pick the right one.' } else { '' }) }
	else { $script:cardNote.Text = 'No GarlicOS card found yet. Put the card in and press Look again, or choose its drive with Choose a folder.' }
	$script:state.boot = $(if ($d.boots.Count) { $d.boots[0] } else { '' })
	$script:bootLbl.Text = $(if ($script:state.boot) { "Found at $($script:state.boot): the Zappo boot logo and charging animation can go on." } else { 'Not showing in Windows, so the boot logo and charging animation will be skipped this time (they are optional; Zappo works without them).' })
	$script:bootLbl.ForeColor = $(if ($script:state.boot) { $script:UI.ok } else { $script:UI.dim })
}
function Selected-Card { if ($script:cardBox.SelectedIndex -lt 0 -or $script:cardBox.SelectedIndex -ge $script:state.paths.Count) { return '' }; return $script:state.paths[$script:cardBox.SelectedIndex] }
$btnScan.Add_Click({ try { Scan-Cards } catch { Oops 'Looking for the card' $_ } })
$btnBrowse.Add_Click({
	try {
		$dlg = New-Object Windows.Forms.FolderBrowserDialog
		$dlg.Description = 'The GarlicOS card: the drive with the CFW, BIOS and Roms folders'
		if ($dlg.ShowDialog($script:form) -eq 'OK') {
			$r = $dlg.SelectedPath.TrimEnd('\')
			if ($r.Length -eq 2 -and $r[1] -eq ':') { $r += '\' }   # (a drive root keeps its backslash; a folder doesn't need one)
			if (Test-Path -LiteralPath (Join-Path $r 'CFW')) { $script:state.paths = @($r) + $script:state.paths; $script:cardBox.Items.Insert(0, $r + '  (chosen)'); $script:cardBox.SelectedIndex = 0; $script:cardNote.Text = ''; $script:cardNote.ForeColor = $script:UI.dim }
			else { $script:cardNote.Text = "$r has no CFW folder, so it isn't a GarlicOS card."; $script:cardNote.ForeColor = $script:UI.bad }
		}
	} catch { Oops 'Choosing a folder' $_ }
})
$undoLink.Add_LinkClicked({ try { $script:state.mode = 'undo'; Show-Page 'undo' } catch { Oops 'Undo page' $_ } })

# ---- 2. what to do
$pg = Page 'what'
$pg.Controls.Add((L 'What would you like done?' $FONT.h1 $UI.text 40 28))
$pg.Controls.Add((L 'Tick what you want. Each one is the same job as a number on the Zappo.bat menu; they run in order, and anything they replace on the card is kept in backups\ so it can be undone.' $FONT.body $UI.dim 40 80 790 44))
$cw = 395; $x1 = 40; $x2 = 455
$pg.Controls.Add((Card 'Install / update Zappo' 'Zappo onto the card and the video player fix. Safe to run again any time.' $x1 136 $cw $true 'install'))
$pg.Controls.Add((Card 'Boot logo and charging animation' "Zappo's pictures on the boot partition, when Windows shows it." $x2 136 $cw $false 'boot'))
$pg.Controls.Add((Card 'Films and music from Downloads' 'Downloads, Downloads\videos and Downloads\music onto the card; big films shrunk to fit.' $x1 212 $cw $false 'media'))
$pg.Controls.Add((Card 'Unpack downloaded video zips first' 'Zipped series in Downloads become folders on the Videos tab.' $x2 212 $cw $false 'unpack'))
$pg.Controls.Add((Card 'Box art, film posters and music covers' 'From the libretro library, TheGamesDB, Wikipedia, Deezer and iTunes. Needs the internet.' $x1 288 $cw $false 'art'))
$pg.Controls.Add((Card 'Extra cores pack from Downloads' "The community's newer emulator builds, if the pack is in Downloads." $x2 288 $cw $false 'cores'))
$pg.Controls.Add((Card 'Check the card afterwards' 'Reads everything back and reports; changes nothing.' $x1 364 $cw $true 'check'))
$whatNote = L '' $FONT.small $UI.dim 40 444 790 40
$pg.Controls.Add($whatNote)

# ---- 2b. undo
$pg = Page 'undo'
$pg.Controls.Add((L 'Undo, or take Zappo off' $FONT.h1 $UI.text 40 28))
$pg.Controls.Add((L "Each of these puts back what the kit kept in backups\. Your games, saves, music and films are never touched. Taking Zappo off gives GarlicOS its own menu back; Zappo's settings folder is moved into backups\, not deleted." $FONT.body $UI.dim 40 80 790 60))
$pg.Controls.Add((Card "GarlicOS's video player back" 'Undoes the 47-byte player fix.' $x1 150 $cw $false 'undoPlayer'))
$pg.Controls.Add((Card "GarlicOS's own cores back" 'Undoes the extra cores pack.' $x2 150 $cw $false 'undoCores'))
$pg.Controls.Add((Card 'GarlicOS boot pictures back' 'Undoes the boot logo and charging animation.' $x1 226 $cw $false 'undoBoot'))
$pg.Controls.Add((Card 'Remove Zappo from the card' "GarlicOS's menu back at the next start." $x2 226 $cw $false 'remove'))
$pg.Controls.Add((Card 'Take the Zappo kit off this PC too' 'Start menu entry and Apps & features listing go; backups and logs are kept in Documents.' $x1 302 $cw $false 'uninstallKit'))

# ---- 3. on this PC
$pg = Page 'pc'
$pg.Controls.Add((L 'The kit on this PC' $FONT.h1 $UI.text 40 28))
$pg.Controls.Add((L 'The setup can stay on this PC like any program, so adding films or box art later is one click from the Start menu. It keeps its backups and logs with it. Nothing else on the PC is changed, and it is only for you (no administrator needed).' $FONT.body $UI.dim 40 80 790 60))
$rbInstall = New-Object Windows.Forms.RadioButton; $rbHere = New-Object Windows.Forms.RadioButton
foreach ($rb in $rbInstall, $rbHere) { $rb.Font = $FONT.h2; $rb.ForeColor = $UI.text; $rb.BackColor = [Drawing.Color]::Transparent; $rb.AutoSize = $true; $rb.UseMnemonic = $false }
$rbInstall.Text = 'Install the kit on this PC (Start menu, Apps & features)'; $rbInstall.Location = New-Object Drawing.Point(44, 160); $rbInstall.Checked = $true
$pg.Controls.Add((L ('Goes to ' + (Get-KitHome)) $FONT.small $UI.dim 68 188 700 20))
$cbDesktop = New-Object Windows.Forms.CheckBox; $cbDesktop.Text = 'A shortcut on the desktop as well'; $cbDesktop.Font = $FONT.body; $cbDesktop.ForeColor = $UI.text
$cbDesktop.BackColor = [Drawing.Color]::Transparent; $cbDesktop.AutoSize = $true; $cbDesktop.Location = New-Object Drawing.Point(68, 214)
$rbHere.Text = 'Just run it from this folder'; $rbHere.Location = New-Object Drawing.Point(44, 262)
$pg.Controls.Add((L 'Nothing is copied; the unzipped folder is the kit. You can install it later from here.' $FONT.small $UI.dim 68 290 700 20))
$pg.Controls.AddRange(@($rbInstall, $cbDesktop, $rbHere))
if (Test-RunningFromHome) { $rbHere.Checked = $true; $rbInstall.Text = 'Refresh the Start menu entry' }

# ---- 4. doing it
$pg = Page 'run'
$runTitle = L 'Working...' $FONT.h1 $UI.text 40 28
$runStep = L '' $FONT.h2 $UI.gold 40 84 790 24
$bar = New-Object Windows.Forms.ProgressBar; $bar.Location = New-Object Drawing.Point(40, 116); $bar.Size = New-Object Drawing.Size(790, 8); $bar.Style = 'Continuous'
$logBox = New-Object Windows.Forms.TextBox
$logBox.Multiline = $true; $logBox.ReadOnly = $true; $logBox.ScrollBars = 'Vertical'; $logBox.Font = $FONT.mono
$logBox.BackColor = $UI.navy; $logBox.ForeColor = $UI.text; $logBox.BorderStyle = 'FixedSingle'
$logBox.Location = New-Object Drawing.Point(40, 136); $logBox.Size = New-Object Drawing.Size(790, 330)
$pg.Controls.AddRange(@($runTitle, $runStep, $bar, $logBox))

# ---- 5. done
$pg = Page 'done'
$doneTitle = L 'All done' $FONT.h1 $UI.text 40 28
$doneList = New-Object Windows.Forms.TextBox
$doneList.Multiline = $true; $doneList.ReadOnly = $true; $doneList.Font = $FONT.body; $doneList.BackColor = $UI.bg; $doneList.ForeColor = $UI.text; $doneList.BorderStyle = 'None'
$doneList.Location = New-Object Drawing.Point(40, 84); $doneList.Size = New-Object Drawing.Size(790, 230)
$doneNote = L '' $FONT.body $UI.dim 40 320 790 70
$btnOpenCard = B 'Open the card' 40 400 140; $btnOpenLogs = B 'Open the logs' 192 400 140; $btnOpenKit = B 'Open the kit folder' 344 400 160
$pg.Controls.AddRange(@($doneTitle, $doneList, $doneNote, $btnOpenCard, $btnOpenLogs, $btnOpenKit))
$btnOpenCard.Add_Click({ Open-Folder $script:state.card })
$btnOpenLogs.Add_Click({ Open-Folder (Join-Path $(if ($script:state.home) { $script:state.home } else { $script:kit }) 'logs') })
$btnOpenKit.Add_Click({ Open-Folder $(if ($script:state.home) { $script:state.home } else { $script:kit }) })

# ---------------------------------------------------------------- flow
function Show-Page([string]$name) {
	foreach ($k in @($script:pages.Keys)) { $script:pages[$k].Visible = ($k -eq $name) }
	$script:state.page = $name
	$n = switch ($name) { 'card' { 1 } 'what' { 2 } 'undo' { 2 } 'pc' { 3 } 'run' { 4 } 'done' { 5 } }
	$script:stepLbl.Text = "Step $n of 5"
	$script:btnBack.Enabled = $name -in 'what', 'undo', 'pc'
	$script:btnNext.Enabled = $true
	$script:btnNext.Text = switch ($name) { 'pc' { 'Start' } 'run' { 'Working...' } 'done' { 'Finish' } default { 'Next' } }
	$script:btnCancel.Visible = $name -ne 'done'
	if ($name -eq 'run') { $script:btnNext.Enabled = $false; $script:btnCancel.Text = 'Stop' } else { $script:btnCancel.Text = 'Cancel' }
	if ($name -eq 'what') {
		# what makes sense for this card
		$info = Get-CardInfo $script:state.card
		$script:whatNote.ForeColor = $script:UI.dim
		$script:whatNote.Text = $(if ($info.zappo) { "This card already has Zappo on it: Install / update brings it up to $($script:version) and keeps your settings." } else { 'First time on this card: Install / update puts Zappo on; the handheld finishes the install at its next start.' })
		foreach ($card in @($script:pages['what'].Controls)) { if ($card.Tag -eq 'boot') { $card.Controls['tick'].Checked = [bool]$script:state.boot; $card.Enabled = [bool]$script:state.boot } }
	}
}
function Collect-Wants {
	$w = @{}
	$pg = $script:pages[$(if ($script:state.mode -eq 'undo') { 'undo' } else { 'what' })]
	foreach ($card in @($pg.Controls)) { if ($card.Tag -and $card.Controls['tick']) { $w[[string]$card.Tag] = [bool]$card.Controls['tick'].Checked } }
	return $w
}
function Any-Want([hashtable]$w) { foreach ($v in $w.Values) { if ($v) { return $true } }; return $false }

function Start-Run {
	$wants = Collect-Wants
	$script:state.results = @(); $script:state.i = -1; $script:state.cur = $null; $script:state.wants = $wants
	$script:logBox.Clear(); $script:bar.Value = 0; $script:bar.Maximum = 1
	Show-Page 'run'
	# the kit on the PC first: the jobs then run from there, so backups\ and logs\ live with it
	if ($script:state.mode -eq 'do' -and $script:rbInstall.Checked) {
		$script:state.busy = $true; $script:btnCancel.Enabled = $false
		$script:runStep.Text = 'Putting the kit on this PC...'
		[Windows.Forms.Application]::DoEvents()
		try { $script:state.home = Install-Kit $script:cbDesktop.Checked; $script:kit = $script:state.home; $script:logBox.AppendText("Kit installed to $($script:state.home) - '$($script:kitName)' is in the Start menu.`r`n") }
		catch { $script:logBox.AppendText("Couldn't install the kit on this PC: $($_.Exception.Message)`r`n") }
		$script:state.busy = $false; $script:btnCancel.Enabled = $true
	}
	$jj = Get-Jobs $wants $script:state.card $script:state.boot
	$script:state.jobs = @($jj)
	$script:bar.Maximum = [Math]::Max(1, $script:state.jobs.Count + 1); $script:bar.Value = 1
	$script:timer.Start()
}
function Next-Job {
	$script:state.i++
	if ($script:state.i -ge $script:state.jobs.Count) { Finish-Run; return }
	$j = $script:state.jobs[$script:state.i]
	$script:runStep.Text = "$($script:state.i + 1) of $($script:state.jobs.Count): $($j.title)"
	$script:logBox.AppendText("`r`n== $($j.title)`r`n")
	try { $script:state.cur = Start-KitJob $j }
	catch { $script:logBox.AppendText("Couldn't start it: $($_.Exception.Message)`r`n"); $script:state.results += @{ title = $j.title; ok = $false }; $script:state.cur = $null; Next-Job }
}
$timer = New-Object Windows.Forms.Timer; $timer.Interval = 250
$timer.Add_Tick({
	try {
		if ($null -eq $script:state.cur) { $script:timer.Stop(); Next-Job; if ($script:state.cur) { $script:timer.Start() }; return }
		foreach ($line in (Read-JobOutput $script:state.cur)) { if ($line.Length) { $script:logBox.AppendText($line + "`r`n") } }
		if (Test-JobDone $script:state.cur) {
			$script:timer.Stop()
			$code = Complete-KitJob $script:state.cur
			foreach ($line in (Read-JobOutput $script:state.cur)) { if ($line.Length) { $script:logBox.AppendText($line + "`r`n") } }
			$script:state.results += @{ title = $script:state.cur.title; ok = ($code -eq 0) }
			$script:state.cur = $null
			$script:bar.Value = [Math]::Min($script:bar.Maximum, $script:bar.Value + 1)
			Next-Job
			if ($script:state.cur) { $script:timer.Start() }
		}
	} catch {
		$script:timer.Stop()
		Oops 'Running the jobs' $_
		try { $script:state.cur = $null; Finish-Run } catch {}
	}
})
function Finish-Run {
	$script:timer.Stop()
	if ($script:state.mode -eq 'undo' -and $script:state.wants['uninstallKit']) {
		try { $kept = Uninstall-Kit; $script:state.results += @{ title = 'Zappo kit taken off this PC' + $(if ($kept) { " (backups and logs kept in $kept)" } else { '' }); ok = $true } }
		catch { $script:state.results += @{ title = "Taking the kit off this PC: $($_.Exception.Message)"; ok = $false } }
	}
	$bad = @($script:state.results | Where-Object { -not $_.ok }).Count
	$script:doneTitle.Text = $(if ($bad) { 'Done, with something to look at' } else { 'All done' })
	$script:doneList.Text = (@($script:state.results | ForEach-Object { $(if ($_.ok) { [string][char]0x2713 } else { [string][char]0x2717 }) + '  ' + $_.title }) -join "`r`n")
	$script:doneNote.Text = $(if ($script:state.mode -eq 'undo') { 'Eject the card before taking it out.' }
		elseif ($bad) { "Something didn't finish: the log on the previous screen says what, and logs\ in the kit keeps it. The rest went ahead." }
		else { "Eject the card, put it in the handheld and switch on. The first start finishes the install and opens Zappo. `"Zappo manual.txt`" in the kit is the full guide." })
	Show-Page 'done'
}

$btnNext.Add_Click({
	try {
		switch ($script:state.page) {
			'card' {
				$script:state.card = Selected-Card
				if (-not $script:state.card) { $script:cardNote.Text = 'Choose the card first (or put it in and press Look again).'; $script:cardNote.ForeColor = $script:UI.bad; return }
				$script:state.mode = 'do'; Show-Page 'what'
			}
			'what' { if (-not (Any-Want (Collect-Wants))) { $script:whatNote.Text = 'Tick at least one thing.'; $script:whatNote.ForeColor = $script:UI.bad; return }; Show-Page 'pc' }
			'undo' {
				$w = Collect-Wants
				if (-not (Any-Want $w)) { return }
				$script:state.card = Selected-Card
				if (-not $script:state.card -and ($w['undoPlayer'] -or $w['undoCores'] -or $w['remove'])) { [void][Windows.Forms.MessageBox]::Show($script:form, 'Those need the card: put it in and choose it on the first screen.', 'Zappo Setup'); return }
				if ($w['remove'] -and [Windows.Forms.MessageBox]::Show($script:form, "Take Zappo off the card? GarlicOS's own menu comes back at the next start. Zappo's settings are kept in backups\.", 'Zappo Setup', 'YesNo', 'Question') -ne 'Yes') { return }
				Start-Run
			}
			'pc' { Start-Run }
			'done' { $script:form.Close() }
		}
	} catch { Oops 'Next' $_ }
})
$btnBack.Add_Click({ try { switch ($script:state.page) { 'what' { Show-Page 'card' } 'undo' { Show-Page 'card' } 'pc' { Show-Page 'what' } } } catch { Oops 'Back' $_ } })
$btnCancel.Add_Click({
	try {
		if ($script:state.page -eq 'run') {
			if ([Windows.Forms.MessageBox]::Show($script:form, 'Stop after the current job? (Stopping a copy halfway is not safe for the card.)', 'Zappo Setup', 'YesNo', 'Question') -eq 'Yes') {
				$script:state.jobs = $(if ($script:state.i -ge 0) { @($script:state.jobs[0..$script:state.i]) } else { @() })
			}
		} else { $script:form.Close() }
	} catch { Oops 'Cancel' $_ }
})
$form.Add_FormClosing({ try { if ($script:state.busy -or ($script:state.page -eq 'run' -and $script:state.cur -and -not (Test-JobDone $script:state.cur))) { $_.Cancel = $true } } catch {} })

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
} catch {
	try { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show("Zappo Setup couldn't start: $($_.Exception.Message)`r`n`r`nZappo.bat (the text menu) does the same jobs.", 'Zappo Setup') } catch { Write-Host $_ }
}

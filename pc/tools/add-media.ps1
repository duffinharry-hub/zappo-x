# Zappo - puts films and music from your Downloads folder onto the card.
#   Songs  (.mp3 .flac .ogg .wav .m4a .aac .opus) in Downloads or Downloads\music -> Music\
#   Videos (.mp4 .mkv .avi .mov .webm .m4v)       in Downloads or Downloads\videos -> Roms\VIDEOS\
#          a folder inside Downloads\videos (a series) becomes a folder on the Videos tab
#          ("Unpack video zips" makes those folders from downloaded collections).
# Films the handheld can't play smoothly - HD (over 480p), over the player's 2 GB
# limit, more than one sound track, odd codecs - are shrunk first with ffmpeg
# (fetched once, about 90 MB, from gyan.dev): 640 pixels wide, English sound,
# ready to start straight away. The originals in Downloads are left alone.
# tools\video-names.txt gives a film a nicer name on the card, or skips it.
# Nothing already on the card is replaced. Run it again whenever you download more.
param([string]$Card = '', [string]$Downloads = '', [switch]$NoShrink, [switch]$NoCovers)
. (Join-Path $PSScriptRoot 'common.ps1')
Log-Start 'Add films and music'
$card = Find-Card $Card
$dl = if ($Downloads) { $Downloads } else { Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Downloads' }
if (-not (Test-Path -LiteralPath $dl)) { throw "no Downloads folder at $dl" }

# ---- ffmpeg
$ffDir = Join-Path $PSScriptRoot 'ffmpeg'
$ffmpeg = Join-Path $ffDir 'ffmpeg.exe'
$ffprobe = Join-Path $ffDir 'ffprobe.exe'
function Get-FFmpeg {
	if ((Test-Path -LiteralPath $ffmpeg) -and (Test-Path -LiteralPath $ffprobe)) { return $true }
	try {
		[void][IO.Directory]::CreateDirectory($ffDir)
		$zipf = Join-Path $ffDir 'ffmpeg-release-essentials.zip'
		Say 'fetching ffmpeg (about 90 MB, from gyan.dev; GPL, its licence is saved beside it) to shrink films and grab posters...'
		(New-Object Net.WebClient).DownloadFile('https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip', $zipf)
		Add-Type -AssemblyName System.IO.Compression.FileSystem
		$z = [IO.Compression.ZipFile]::OpenRead($zipf)
		try { foreach ($e in $z.Entries) { if ($e.Name -in @('ffmpeg.exe', 'ffprobe.exe', 'LICENSE', 'README.txt')) { [IO.Compression.ZipFileExtensions]::ExtractToFile($e, (Join-Path $ffDir $e.Name), $true) } } }
		finally { $z.Dispose() }
		Remove-Item -LiteralPath $zipf -Force
		return (Test-Path -LiteralPath $ffmpeg)
	} catch { Say ('could not fetch ffmpeg (' + $_.Exception.Message + ') - films are copied as they are'); return $false }
}
function Video-Info([string]$path) {
	$j = & $ffprobe -v error -print_format json -show_format -show_streams -- $path 2>$null | ConvertFrom-Json
	$v = @($j.streams | Where-Object { $_.codec_type -eq 'video' }) | Select-Object -First 1
	$tracks = @(); $i = 0
	foreach ($t in @($j.streams | Where-Object { $_.codec_type -eq 'audio' })) { $lang = ''; if ($t.tags -and $t.tags.language) { $lang = [string]$t.tags.language }; $tracks += ,@($i, $lang.ToLowerInvariant()); $i++ }
	return @{ height = $(if ($v) { [int]$v.height } else { 0 }); width = $(if ($v) { [int]$v.width } else { 0 }); vcodec = $(if ($v) { [string]$v.codec_name } else { '' })
	          seconds = $(if ($j.format.duration) { [double]$j.format.duration } else { 0 }); tracks = $tracks }
}
function English-Track($tracks) {
	if (-not $tracks.Count) { return -1 }
	foreach ($t in $tracks) { if ($t[1] -in @('eng', 'en', 'english')) { return $t[0] } }
	if ($tracks.Count -eq 1) { return $tracks[0][0] }
	foreach ($t in $tracks) { if ($t[1] -in @('', 'und')) { return $t[0] } }
	return $tracks[0][0]
}
function Shrink-Video([string]$from, [string]$to, $info) {
	$aidx = English-Track $info.tracks
	$vf = if ($info.width -gt 640) { 'scale=640:-2' } else { 'scale=trunc(iw/2)*2:trunc(ih/2)*2' }
	$a = @('-y', '-hide_banner', '-loglevel', 'error', '-stats', '-i', $from, '-map', '0:v:0')
	if ($aidx -ge 0) { $a += @('-map', ('0:a:' + $aidx)) }
	$a += @('-sn', '-dn', '-map_metadata', '-1', '-vf', $vf, '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '23', '-profile:v', 'main', '-level', '3.1', '-pix_fmt', 'yuv420p',
	        '-c:a', 'aac', '-b:a', '128k', '-ac', '2', '-movflags', '+faststart', ($to + '.part.mp4'))
	& $ffmpeg @a
	if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath ($to + '.part.mp4'))) { throw 'ffmpeg could not convert it' }
	Move-Item -LiteralPath ($to + '.part.mp4') -Destination $to -Force
}
function Needs-Shrink([IO.FileInfo]$f, $info) {
	if ($f.Length -ge 1900MB) { return 'over the player''s 2 GB limit' }
	if ($info.height -gt 480) { return ('{0}p; the handheld plays 480p and under smoothly' -f $info.height) }
	if ($info.tracks.Count -gt 1) { return 'more than one sound track (keeping English)' }
	if ($info.vcodec -and $info.vcodec -ne 'h264' -and $info.vcodec -ne 'mpeg4') { return ($info.vcodec + ' video, which the player struggles with') }
	if ($f.Extension.ToLowerInvariant() -ne '.mp4') { return 'not an MP4' }
	return ''
}

# ---- music
$exts = @('.mp3', '.flac', '.ogg', '.oga', '.wav', '.m4a', '.aac', '.opus', '.wma')
$music = Join-Path $card 'Music'
[void][IO.Directory]::CreateDirectory($music)
$have = @{}
foreach ($f in Get-ChildItem -LiteralPath $music -File -Recurse) { $have[$f.Name.ToLowerInvariant()] = $f.Length }
$newSongs = 0
foreach ($d in @($dl, (Join-Path $dl 'music'))) {
	if (-not (Test-Path -LiteralPath $d)) { continue }
	foreach ($f in Get-ChildItem -LiteralPath $d -File) {
		if ($exts -notcontains $f.Extension.ToLowerInvariant()) { continue }
		$k = $f.Name.ToLowerInvariant()
		if ($have.ContainsKey($k)) { continue }
		$b = (($f.BaseName -replace '\s*\(\d+\)$', '') + $f.Extension).ToLowerInvariant()   # "Song (1).mp3" = "Song.mp3" downloaded twice
		if ($b -ne $k -and $have.ContainsKey($b) -and $have[$b] -eq $f.Length) { continue }
		Copy-Verified $f.FullName (Join-Path $music $f.Name)
		$have[$k] = $f.Length; $newSongs++
		Say ('+ Music\' + $f.Name)
	}
}
Say "$newSongs new song(s)."

# ---- videos
$vexts = @('.mp4', '.m4v', '.mkv', '.avi', '.mov', '.webm')
$vnames = @{}
$vnf = Join-Path $PSScriptRoot 'video-names.txt'
if (Test-Path -LiteralPath $vnf) {
	foreach ($l in [IO.File]::ReadAllLines($vnf, $script:utf8)) { $i = $l.IndexOf(' = '); if ($i -gt 0 -and -not $l.StartsWith('#')) { $vnames[$l.Substring(0, $i).Trim().ToLowerInvariant()] = $l.Substring($i + 3).Trim() } }
}
function Video-Name([string]$base) {
	$b = $base
	if ($b -match '^YTDown\.com_YouTube_Media_[^_]+_(.+?)(_\d{3})?(_\d{3,4}p)?$') { $b = $Matches[1] -replace '-', ' ' }   # a YouTube downloader's file names
	if ($vnames.ContainsKey($base.ToLowerInvariant())) { return $vnames[$base.ToLowerInvariant()] }
	if ($vnames.ContainsKey($b.ToLowerInvariant())) { return $vnames[$b.ToLowerInvariant()] }
	if ($b -notmatch ' ' -and $b -match '^(.+?)\.((?:19|20)\d{2})(?:\.|$)') { $b = ($Matches[1] -replace '\.', ' ') + ' (' + $Matches[2] + ')' }   # Film.Name.2018.1080p-grp
	return ($b -replace '[\\/:*?"<>|]', ' ' -replace '\s+', ' ').Trim()
}
$vroot = Join-Path $card 'Roms\VIDEOS'
[void][IO.Directory]::CreateDirectory($vroot)
$haveFF = if ($NoShrink) { $false } else { Get-FFmpeg }
$vnew = 0; $vskip = 0
$sets = New-Object Collections.Generic.List[object]
foreach ($d in @($dl, (Join-Path $dl 'videos'))) { if (Test-Path -LiteralPath $d) { $sets.Add(@($d, '')) } }
if (Test-Path -LiteralPath (Join-Path $dl 'videos')) { foreach ($sd in Get-ChildItem -LiteralPath (Join-Path $dl 'videos') -Directory) { $sets.Add(@($sd.FullName, $sd.Name)) } }
foreach ($set in $sets) {
	$d = $set[0]; $sub = $set[1]
	$target = if ($sub) { Join-Path $vroot $sub } else { $vroot }
	foreach ($f in Get-ChildItem -LiteralPath $d -File) {
		if ($vexts -notcontains $f.Extension.ToLowerInvariant() -or $f.Length -lt 5MB) { continue }
		$name = Video-Name $f.BaseName
		if (-not $name -or $name -eq 'skip') { $vskip++; continue }
		$shown = 'VIDEOS\' + $(if ($sub) { $sub + '\' } else { '' }) + $name
		$dest = Join-Path $target ($name + $f.Extension.ToLowerInvariant())
		$why = ''; $info = $null
		if ($haveFF) { try { $info = Video-Info $f.FullName; $why = Needs-Shrink $f $info } catch { $info = $null; $why = '' } }
		if ($why) {
			$dest = Join-Path $target ($name + '.mp4')
			$done = Join-Path $target ('.shrunk-' + $name + '.txt')   # which download this copy was made from
			if ((Test-Path -LiteralPath $dest) -and (Test-Path -LiteralPath $done) -and ((Get-Content -LiteralPath $done -Raw).Trim() -eq ($f.FullName + '|' + $f.Length))) { continue }
			[void][IO.Directory]::CreateDirectory($target)
			Say ('shrinking ' + $name + ' (' + $why + '; about ' + [int]($info.seconds / 60) + ' min of film - this takes a while)...')
			try {
				Shrink-Video $f.FullName $dest $info
				[IO.File]::WriteAllText($done, ($f.FullName + '|' + $f.Length), $script:utf8)
				Say ('+ ' + $shown + '.mp4  ({0:N0} MB)' -f ((Get-Item -LiteralPath $dest).Length / 1MB)); $vnew++
			} catch { Say ('could not shrink ' + $f.Name + ': ' + $_.Exception.Message); $vskip++ }
			continue
		}
		if ((Test-Path -LiteralPath $dest) -and (Get-Item -LiteralPath $dest).Length -eq $f.Length) { continue }
		if ($f.Length -ge 2GB) { Say ('skipped ' + $f.Name + ': 2 GB or more, too big for the video player (ffmpeg would shrink it)'); $vskip++; continue }
		$fr = Free $card; if ($fr -ge 0 -and $fr -lt $f.Length + 64MB) { Say ('skipped ' + $f.Name + ': no room left on the card'); $vskip++; continue }
		Write-Progress -Activity 'Copying to the card' -Status $name
		Copy-Verified $f.FullName $dest
		Say ('+ ' + $shown + $f.Extension.ToLowerInvariant()); $vnew++
	}
}
Write-Progress -Activity 'Copying to the card' -Completed
Say ("$vnew new video(s)" + $(if ($vskip) { " ($vskip skipped)" } else { '' }) + '.')
$partial = @(Get-ChildItem -LiteralPath $dl -File | Where-Object { $_.Extension -in @('.crdownload', '.part', '.partial') })
if ($partial.Count) { Say ('{0} download(s) still in progress - run this again when they finish.' -f $partial.Count) }

# ---- cover art for the music (needs the list the handheld writes when it reads the Music folder)
if (-not $NoCovers -and $newSongs -gt 0) {
	if (Test-Path -LiteralPath (Join-Path $card 'CFW\clove\music-list.tsv')) {
		try { & (Join-Path $PSScriptRoot 'get-cover-art.ps1') -Card $card | Out-Host } catch { Say ('cover art skipped: ' + $_.Exception.Message) }
	} else { Say 'Cover art: start the handheld and open the Music tab once, then run this again (or Zappo.bat -> 4) and the covers are fetched.' }
}
Say 'DONE. Posters and box art: Zappo.bat -> 4.'

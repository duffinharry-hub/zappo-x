# Clove - fetches cover art for the music on the TF2 card.
#
# Reads CFW\clove\music-list.tsv (Clove writes it every time it reads the
# Music folder) and saves, for every song that has no picture yet:
#   Music\Covers\<song file name>.jpg   the single's or album's cover
#   Music\Artists\<Artist>.jpg          a picture of the artist
# Pictures come from the free Deezer and iTunes search services. Songs that
# can't be matched for certain are left alone (Clove draws its own cover).
# Pictures this tool saved are listed in CFW\clove\cover-art.tsv; -Redo looks
# those up again (pictures you put there yourself are never replaced).
param([string]$Card = '', [switch]$Redo, [switch]$NoRun,
      [string]$List = '', [string]$OutMusic = '', [string]$Manifest = '')

$ErrorActionPreference = 'Stop'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

$kit = Split-Path -Parent $PSScriptRoot
[void][IO.Directory]::CreateDirectory((Join-Path $kit 'logs'))
$log = Join-Path $kit 'logs\cover-art-log.txt'
if (-not $Card) { . (Join-Path $PSScriptRoot 'common.ps1'); $Card = Find-Card '' }

function Say([string]$m) { Write-Host ('  ' + $m); [IO.File]::AppendAllText($log, $m + "`r`n", [Text.Encoding]::UTF8) }

function Norm([string]$s) {
    if (-not $s) { return '' }
    $d = $s.ToLowerInvariant().Replace('$', 's').Replace('&', 'and').Normalize([Text.NormalizationForm]::FormD)
    $sb = New-Object Text.StringBuilder
    foreach ($c in $d.ToCharArray()) {
        if ([Globalization.CharUnicodeInfo]::GetUnicodeCategory($c) -eq [Globalization.UnicodeCategory]::NonSpacingMark) { continue }
        if ([char]::IsLetterOrDigit($c)) { [void]$sb.Append($c) }
    }
    $r = $sb.ToString()
    if ($r.Length -gt 6 -and $r.StartsWith('the')) { $r = $r.Substring(3) }
    return $r
}

function PrimaryArtist([string]$a) {
    $p = [regex]::Split($a, '(?i)\s+(?:&|x|and|feat\.?|ft\.?|featuring|with)\s+|,\s*')[0]
    return $p.Trim()
}

# the title without "(feat. X)", "(Live ...)", "(2017 Remaster)", "(Slowed + Reverb)" and the like
function CleanTitle([string]$t) {
    $t = [regex]::Replace($t, '(?i)\s*[\(\[][^\)\]]*(feat|ft\.|featuring|live|remaster|slowed|reverb|sped|instrumental|bonus|concert|festival|glastonbury|later\.\.\.|version|visuali[sz]er|audio|video|\b(19|20)\d\d\b)[^\)\]]*[\)\]]', '')
    $t = [regex]::Replace($t, '(?i)\s+(feat\.?|ft\.?|featuring)\s+.*$', '')
    return ([regex]::Replace($t, '\s+', ' ')).Trim()
}
function BareTitle([string]$t) {
    $t = [regex]::Replace($t, '\s*[\(\[][^\)\]]*[\)\]]', '')
    $t = [regex]::Replace($t, '(?i)\s+(feat\.?|ft\.?|featuring)\s+.*$', '')
    $t = [regex]::Replace($t, '\s+-\s+.*$', '')   # "Drive - 2017 Remaster"
    return ([regex]::Replace($t, '\s+', ' ')).Trim()
}

# whole words, so "Kano" doesn't match "Kanon" and "Drive" doesn't match "Drive By"
function Words([string]$s) {
    if (-not $s) { return @() }
    $d = $s.ToLowerInvariant().Replace('$', 's').Replace('&', ' and ').Normalize([Text.NormalizationForm]::FormD)
    $sb = New-Object Text.StringBuilder
    foreach ($c in $d.ToCharArray()) {
        if ([Globalization.CharUnicodeInfo]::GetUnicodeCategory($c) -eq [Globalization.UnicodeCategory]::NonSpacingMark) { continue }
        if ([char]::IsLetterOrDigit($c)) { [void]$sb.Append($c) }
        elseif ($c -ne [char]"'" -and $c -ne [char]'.' -and $c -ne [char]0x2019 -and $c -ne [char]0x2018) { [void]$sb.Append(' ') }
    }
    $w = @($sb.ToString().Split(' ', [StringSplitOptions]::RemoveEmptyEntries))
    if ($w.Count -gt 1 -and $w[0] -eq 'the') { $w = $w[1..($w.Count - 1)] }
    return $w
}
function SameWords([string]$a, [string]$b) {
    $x = (Words $a) -join ' '; $y = (Words $b) -join ' '
    return $x -and ($x -eq $y)
}
function TitleMatch([string]$want, [string]$got) {
    return (SameWords $want $got) -or (SameWords $want (BareTitle $got)) -or (SameWords (BareTitle $want) (BareTitle $got))
}
function ArtistMatch([string]$want, [string]$got) {
    $a = @(Words (PrimaryArtist $want)); $b = @(Words $got)
    if ($a.Count -eq 0 -or $b.Count -eq 0) { return $false }
    if (($a -join ' ') -eq ($b -join ' ')) { return $true }
    # "Wretch 32" for "Wretch 32 & Avelino", "Rufus" for "Rufus Du Sol" - but not
    # "Black Eyed Peas Karaoke Band" for "Black Eyed Peas"
    if ($b.Count -ge $a.Count) { return $false }
    for ($i = 0; $i -lt $b.Count; $i++) { if ($a[$i] -ne $b[$i]) { return $false } }
    return (($b -join '').Length -ge 4)
}

# Windows-safe file name, the same rule Clove uses to find the picture
function SafeName([string]$n) {
    $s = [regex]::Replace($n, '[\\/:*?"<>|\x00-\x1F]', '_').TrimEnd('.', ' ')
    if (-not $s) { $s = '_' }
    return $s
}

$script:web = New-Object Net.WebClient
$script:web.Encoding = [Text.Encoding]::UTF8
$script:web.Headers.Add('User-Agent', 'Clove-cover-art/1.0 (RG35XX launcher)')

function Get-Json([string]$url) {
    for ($i = 0; $i -lt 4; $i++) {
        try {
            Start-Sleep -Milliseconds 150
            $script:web.Headers['User-Agent'] = 'Clove-cover-art/1.0 (RG35XX launcher)'
            $j = $script:web.DownloadString($url) | ConvertFrom-Json
            if ($j.error) { Start-Sleep -Seconds 3; continue }   # Deezer's "too many requests"
            return $j
        } catch { Start-Sleep -Seconds 2 }
    }
    return $null
}

function Is-Compilation([string]$album) {
    return $album -match "(?i)now that|hits|best of|greatest|collection|anthems|workout|party|playlist|ministry|essential|classics|ultimate|the very best|vol\.|volume|we are the|'\d0s|\b\d0s\b|decade|summer 20|chart"
}
function Is-Imitation([string]$text) {
    return $text -match '(?i)karaoke|tribute|made famous|in the style of|originally performed|cover version|as made|lullaby|piano version of|8.?bit'
}
$script:VERSIONS = 'symphonic|acoustic|instrumental|\binst\b|remix|\bmix\b|live|edit|sped|slowed|reverb|demo|orchestral|unplugged|a cappella|acapella|karaoke|extended|clean|radio'

# a number for how well a search result fits the song; $null = not this song
function Score-Result([string]$artist, [string]$title, [string]$gotArtist, [string]$gotTitle, [string]$gotAlbum) {
    if (Is-Imitation ($gotArtist + ' ' + $gotTitle + ' ' + $gotAlbum)) { return $null }
    if (-not (ArtistMatch $artist $gotArtist)) { return $null }
    $ct = CleanTitle $title; $bt = BareTitle $title
    if (-not $ct) { $ct = $title }
    if (-not $bt) { $bt = $ct }
    $sc = 0
    if ((SameWords $title $gotTitle) -or (SameWords $ct $gotTitle) -or (SameWords $ct (CleanTitle $gotTitle))) { $sc += 6 }
    elseif ((SameWords $ct (BareTitle $gotTitle)) -or (SameWords $bt $gotTitle)) { $sc += 4 }
    elseif (TitleMatch $bt $gotTitle) { $sc += 2 }
    else { return $null }
    if (SameWords (PrimaryArtist $artist) $gotArtist) { $sc += 1 }
    if (Is-Compilation $gotAlbum) { $sc -= 3 }
    # a "Symphonic Version" / remix the song itself doesn't mention
    foreach ($m in [regex]::Matches($gotTitle, '(?i)' + $script:VERSIONS)) {
        if ($title -notmatch ('(?i)' + [regex]::Escape($m.Value))) { $sc -= 2; break }
    }
    if ($gotAlbum -match ('(?i)' + $script:VERSIONS) -and $title -notmatch ('(?i)' + $script:VERSIONS)) { $sc -= 1 }
    return $sc
}

$script:artistPics = @{}   # artist words -> @{url; from} from songs that matched

function Find-Cover([string]$artist, [string]$title) {
    $pa = PrimaryArtist $artist
    $ct = CleanTitle $title
    $bt = BareTitle $title
    if (-not $ct) { $ct = $title }
    if (-not $bt) { $bt = $ct }
    $queries = New-Object Collections.Generic.List[string]
    $queries.Add('artist:"' + $pa + '" track:"' + $ct + '"')
    if ($bt -ne $ct) { $queries.Add('artist:"' + $pa + '" track:"' + $bt + '"') }
    if ($pa -match '^(?i)the\s+(.+)$') { $queries.Add('artist:"' + $Matches[1] + '" track:"' + $bt + '"') }   # "Black Eyed Peas"
    $queries.Add($pa + ' ' + $bt)
    $best = $null; $bestScore = -999
    foreach ($q in $queries) {
        $r = Get-Json ('https://api.deezer.com/search?limit=25&q=' + [uri]::EscapeDataString($q))
        if (-not $r -or -not $r.data) { continue }
        foreach ($d in $r.data) {
            if (-not $d.album -or -not $d.album.cover_big) { continue }
            $sc = Score-Result $artist $title ([string]$d.artist.name) ([string]$d.title) ([string]$d.album.title)
            if ($null -eq $sc) { continue }
            if ($sc -gt $bestScore) { $best = $d; $bestScore = $sc }
        }
        if ($bestScore -ge 6) { break }   # an exact title on a proper album: no need to look further
    }
    if ($best -and $bestScore -ge 5) {
        $k = (Words $pa) -join ' '
        $pic = [string]$best.artist.picture_big
        if ($pic -and $pic -notmatch '/artist//' -and -not $script:artistPics.ContainsKey($k)) {
            $script:artistPics[$k] = @{ url = $pic; from = ('Deezer artist: ' + $best.artist.name + ' (from ' + $best.title + ')') }
        }
        return @{ url = [string]$best.album.cover_big; from = ('Deezer: ' + $best.artist.name + ' - ' + $best.title + ' [' + $best.album.title + ']') }
    }
    # not a sure match on Deezer: see whether iTunes does better
    $deezer = $best; $deezerScore = $bestScore
    $r = Get-Json ('https://itunes.apple.com/search?entity=song&limit=25&country=GB&term=' + [uri]::EscapeDataString($pa + ' ' + $bt))
    $best = $null; $bestScore = -999
    if ($r -and $r.results) {
        foreach ($d in $r.results) {
            if (-not $d.artworkUrl100) { continue }
            $sc = Score-Result $artist $title ([string]$d.artistName) ([string]$d.trackName) ([string]$d.collectionName)
            if ($null -eq $sc) { continue }
            if ($sc -gt $bestScore) { $best = $d; $bestScore = $sc }
        }
    }
    if ($best -and $bestScore -ge 1 -and $bestScore -gt $deezerScore) {
        return @{ url = ([string]$best.artworkUrl100 -replace '100x100bb', '600x600bb'); from = ('iTunes: ' + $best.artistName + ' - ' + $best.trackName + ' [' + $best.collectionName + ']') }
    }
    if ($deezer -and $deezerScore -ge 1) {
        return @{ url = [string]$deezer.album.cover_big; from = ('Deezer: ' + $deezer.artist.name + ' - ' + $deezer.title + ' [' + $deezer.album.title + ']') }
    }
    return $null
}

function Find-ArtistPicture([string]$artist) {
    # best: the artist of a song that matched (so it's the right "Slim" or "Chip")
    $k = (Words (PrimaryArtist $artist)) -join ' '
    if ($script:artistPics.ContainsKey($k)) { return $script:artistPics[$k] }
    foreach ($name in @($artist, (PrimaryArtist $artist)) | Select-Object -Unique) {
        $r = Get-Json ('https://api.deezer.com/search/artist?limit=10&q=' + [uri]::EscapeDataString($name))
        if (-not $r -or -not $r.data) { continue }
        $pick = $null
        foreach ($d in $r.data) { if ((SameWords ([string]$d.name) $name) -and -not (Is-Imitation ([string]$d.name))) { $pick = $d; break } }
        if (-not $pick) { continue }
        $u = [string]$pick.picture_big
        if (-not $u -or $u -match '/artist//') { continue }   # Deezer's blank placeholder
        return @{ url = $u; from = ('Deezer artist: ' + $pick.name) }
    }
    return $null
}

function Save-Image([string]$url, [string]$dest) {
    $tmp = $dest + '.part'
    try {
        $script:web.Headers['User-Agent'] = 'Clove-cover-art/1.0 (RG35XX launcher)'
        $script:web.DownloadFile($url, $tmp)
        $b = [IO.File]::ReadAllBytes($tmp)
        $ok = $b.Length -gt 2000 -and (($b[0] -eq 0xFF -and $b[1] -eq 0xD8) -or ($b[0] -eq 0x89 -and $b[1] -eq 0x50))
        if (-not $ok) { [IO.File]::Delete($tmp); return $false }
        [IO.File]::Copy($tmp, $dest, $true)
        [IO.File]::Delete($tmp)
        return $true
    } catch {
        if ([IO.File]::Exists($tmp)) { [IO.File]::Delete($tmp) }
        return $false
    }
}

function Has-Picture([string]$dir, [string]$stem) {
    foreach ($e in '.jpg', '.jpeg', '.png') { if ([IO.File]::Exists((Join-Path $dir ($stem + $e)))) { return $true } }
    return $false
}

if ($NoRun) { return }

[IO.File]::WriteAllText($log, ('Clove cover art - ' + (Get-Date) + "`r`n"), [Text.Encoding]::UTF8)
$list = if ($List) { $List } else { Join-Path $Card 'CFW\clove\music-list.tsv' }
if (-not (Test-Path -LiteralPath (Join-Path $Card 'Music'))) { Say ('No Music folder on ' + $Card + ' - is this the GarlicOS card?'); Add-Content -LiteralPath $log 'FAILED'; exit 1 }
if (-not (Test-Path -LiteralPath $list)) { Say 'No song list yet: start the handheld once and open the Music tab, then run this again.'; Add-Content -LiteralPath $log 'FAILED'; exit 1 }

$music = Join-Path $Card 'Music'
$outRoot = if ($OutMusic) { $OutMusic } else { $music }
$covers = Join-Path $outRoot 'Covers'
$artistsDir = Join-Path $outRoot 'Artists'
[void][IO.Directory]::CreateDirectory($covers)
[void][IO.Directory]::CreateDirectory($artistsDir)

$rows = [IO.File]::ReadAllLines($list, [Text.Encoding]::UTF8) | Select-Object -Skip 1 | Where-Object { $_ }
$songs = @(); $artists = @()
foreach ($row in $rows) {
    $c = $row.Split("`t")
    if ($c.Length -lt 4) { continue }
    if ($c[0] -eq 'song') { $songs += , @($c[1], $c[2], $c[3]) }
    elseif ($c[0] -eq 'artist') { $artists += $c[2] }
}
Say ('{0} songs and {1} artists in the list' -f $songs.Count, $artists.Count)

# pictures this tool saved before: "Covers/x.jpg<TAB>url"
$manifestPath = if ($Manifest) { $Manifest } else { Join-Path $Card 'CFW\clove\cover-art.tsv' }
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $manifestPath))
$mine = @{}
if (Test-Path -LiteralPath $manifestPath) {
    foreach ($l in [IO.File]::ReadAllLines($manifestPath, [Text.Encoding]::UTF8)) {
        $c = $l.Split("`t")
        if ($c.Length -ge 2 -and $c[0]) { $mine[$c[0]] = $c[1] }
    }
}
function Save-Manifest {
    $sb = New-Object Text.StringBuilder
    foreach ($k in ($mine.Keys | Sort-Object)) { [void]$sb.Append($k + "`t" + $mine[$k] + "`r`n") }
    [IO.File]::WriteAllText($manifestPath, $sb.ToString(), (New-Object Text.UTF8Encoding($false)))
}

$got = 0; $have = 0; $same = 0; $miss = New-Object Collections.Generic.List[string]
$i = 0
foreach ($s in $songs) {
    $i++
    $file = $s[0]; $artist = $s[1]; $title = $s[2]
    Write-Progress -Activity 'Getting cover art' -Status ($artist + ' - ' + $title) -PercentComplete ([int](100 * $i / ($songs.Count + $artists.Count)))
    $stem = [IO.Path]::GetFileNameWithoutExtension($file)
    $sub = [IO.Path]::GetDirectoryName($file)
    $songDir = if ($sub) { Join-Path $music $sub } else { $music }
    $rel = 'Covers/' + $stem + '.jpg'
    $dest = Join-Path $covers ($stem + '.jpg')
    if (Has-Picture $songDir $stem) { $have++; continue }                     # your own picture next to the song
    $exists = Has-Picture $covers $stem
    if ($exists -and -not $mine.ContainsKey($rel)) { $have++; continue }        # one you put in Covers yourself
    if ($exists -and -not $Redo) { $have++; continue }
    $hit = Find-Cover $artist $title
    if ($hit) {
        if ($exists -and $mine[$rel] -eq $hit.url) { $same++; continue }
        if (Save-Image $hit.url $dest) {
            $mine[$rel] = $hit.url
            $got++
            Say ('cover   ' + $artist + ' - ' + $title + '   <- ' + $hit.from)
            if ($got % 10 -eq 0) { Save-Manifest }
            continue
        }
    }
    if ($exists) { $have++; continue }
    $miss.Add($artist + ' - ' + $title)
    Say ('none    ' + $artist + ' - ' + $title)
}
$agot = 0; $ahave = 0; $amiss = New-Object Collections.Generic.List[string]
foreach ($a in $artists) {
    $i++
    Write-Progress -Activity 'Getting artist pictures' -Status $a -PercentComplete ([int](100 * $i / ($songs.Count + $artists.Count)))
    $safe = SafeName $a
    $rel = 'Artists/' + $safe + '.jpg'
    $exists = Has-Picture $artistsDir $safe
    if ($exists -and (-not $mine.ContainsKey($rel) -or -not $Redo)) { $ahave++; continue }
    $hit = Find-ArtistPicture $a
    if ($hit) {
        if ($exists -and $mine[$rel] -eq $hit.url) { $same++; continue }
        if (Save-Image $hit.url (Join-Path $artistsDir ($safe + '.jpg'))) {
            $mine[$rel] = $hit.url
            $agot++
            Say ('artist  ' + $a + '   <- ' + $hit.from)
            continue
        }
    }
    if ($exists) { $ahave++; continue }
    $amiss.Add($a)
    Say ('none    artist ' + $a)
}
Save-Manifest
Write-Progress -Activity 'Getting cover art' -Completed
Say ''
Say ('Covers: {0} new or replaced, {1} kept, {2} not found' -f $got, ($have + $same), $miss.Count)
Say ('Artist pictures: {0} new or replaced, {1} kept, {2} not found' -f $agot, $ahave, $amiss.Count)
Add-Content -LiteralPath $log 'DONE'

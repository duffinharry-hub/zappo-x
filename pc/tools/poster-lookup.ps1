# Clove - finding a poster for a film, a TV series or an album, with no account
# needed: Wikipedia's page picture first (the poster or the cover, nearly
# always), then Apple's catalogue. Dot-sourced by get-game-art.ps1.
#   Find-Poster -Kind movie|tv|album -Title 'Four Lions' -> URL of a picture, or $null
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
$script:posterUA = 'Clove box art (RG35XX launcher; github)'
$script:posterNotes = New-Object Collections.Generic.List[string]   # what the look-up tried (for the log)
function Squash([string]$t) {
    $d = $t.ToLowerInvariant().Normalize([Text.NormalizationForm]::FormD)
    $sb = New-Object Text.StringBuilder
    foreach ($c in $d.ToCharArray()) { if ([Globalization.CharUnicodeInfo]::GetUnicodeCategory($c) -ne [Globalization.UnicodeCategory]::NonSpacingMark) { [void]$sb.Append($c) } }
    return [regex]::Replace($sb.ToString(), '[^a-z0-9]', '')
}
function Split-Year([string]$title) {
    $year = ''
    if ($title -match '\((19|20)\d{2}\)') { $year = $Matches[0].Trim('()'); $title = ($title -replace '\((19|20)\d{2}\)', '').Trim() }
    return @($title, $year)
}
function Wiki-Art([string]$kind, [string]$title) {
    $t = Split-Year $title; $title = $t[0]; $year = $t[1]
    $hint = @{ movie = 'film'; tv = 'television series'; album = 'album' }[$kind]
    $want = Squash $title
    $q = $title + ' ' + $hint + $(if ($year) { ' ' + $year } else { '' })
    $url = 'https://en.wikipedia.org/w/api.php?action=query&list=search&format=json&srlimit=6&srsearch=' + [Uri]::EscapeDataString($q)
    Start-Sleep -Milliseconds 1500   # Wikipedia asks automated callers to go gently
    try { $j = Invoke-RestMethod -Uri $url -TimeoutSec 30 -Headers @{ 'User-Agent' = $script:posterUA } } catch { $script:posterNotes.Add('wiki search failed: ' + $_.Exception.Message); return $null }
    foreach ($r in @($j.query.search)) {
        $page = [string]$r.title
        $base = Squash ($page -replace '\s*\(.*?\)\s*$', '')
        $near = ($base -eq $want) -or ($base.Length -ge 6 -and ($base.StartsWith($want) -or $want.StartsWith($base) -or $want.EndsWith($base))) -or ($want.Length -ge 6 -and $base.EndsWith($want))
        if (-not $near) { $script:posterNotes.Add('wiki: "' + $page + '" is not ' + $title); continue }
        $surl = 'https://en.wikipedia.org/api/rest_v1/page/summary/' + [Uri]::EscapeDataString($page.Replace(' ', '_'))
        Start-Sleep -Milliseconds 1200
        try { $s = Invoke-RestMethod -Uri $surl -TimeoutSec 30 -Headers @{ 'User-Agent' = $script:posterUA } } catch { $script:posterNotes.Add('wiki: "' + $page + '" summary failed: ' + $_.Exception.Message); continue }
        $desc = ([string]$s.description + ' ' + [string]$s.extract).ToLowerInvariant()
        $ok = switch ($kind) { 'movie' { $desc -match 'film|movie' } 'tv' { $desc -match 'television|tv series|anime|animated series|cartoon' } 'album' { $desc -match 'album|mixtape' } }
        if (-not $ok) { $script:posterNotes.Add('wiki: "' + $page + '" is not a ' + $kind + ' (' + [string]$s.description + ')'); continue }
        if (-not ($s.originalimage -and $s.originalimage.source)) { $script:posterNotes.Add('wiki: "' + $page + '" has no picture'); continue }
        if ($s.originalimage -and $s.originalimage.source) {
            return [string]$s.originalimage.source
        }
    }
    return $null
}
function Itunes-Art([string]$kind, [string]$title) {
    $t = Split-Year $title; $title = $t[0]; $year = $t[1]
    $media = @{ movie = 'movie'; tv = 'tvShow'; album = 'music' }[$kind]
    $entity = @{ movie = 'movie'; tv = 'tvSeason'; album = 'album' }[$kind]
    $want = Squash $title
    foreach ($country in @('gb', 'us')) {
        $url = 'https://itunes.apple.com/search?term=' + [Uri]::EscapeDataString($title).Replace('%20', '+') + '&media=' + $media + '&entity=' + $entity + '&country=' + $country + '&limit=10'
        try { $j = Invoke-RestMethod -Uri $url -TimeoutSec 30 -Headers @{ 'User-Agent' = $script:posterUA } } catch { continue }
        if (-not $j.results) { continue }
        $best = $null; $bestScore = 0
        foreach ($r in $j.results) {
            $name = if ($kind -eq 'movie') { [string]$r.trackName } else { [string]$r.collectionName }
            if (-not $name) { continue }
            $have = Squash ($name -replace '\(.*?\)', '')
            $score = 0
            if ($have -eq $want) { $score = 3 } elseif ($have.StartsWith($want) -or $want.StartsWith($have)) { $score = 2 } elseif ($have.Contains($want)) { $score = 1 }
            if ($kind -eq 'album' -and $r.artistName -and $want.Contains((Squash $r.artistName))) { $score += 1 }
            if ($score -and $year -and $r.releaseDate -and ([string]$r.releaseDate).StartsWith($year)) { $score += 1 }
            if ($score -gt $bestScore) { $best = $r; $bestScore = $score }
        }
        if ($best -and $best.artworkUrl100) { return ([string]$best.artworkUrl100) -replace '100x100bb', '600x600bb' }
    }
    return $null
}
function Find-Poster([string]$Kind, [string]$Title) {
    $a = Wiki-Art $Kind $Title
    if ($a) { return $a }
    return Itunes-Art $Kind $Title
}

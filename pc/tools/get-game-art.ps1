# Clove - box art for your games, from the libretro thumbnail collection.
#
# For every game in the Roms folders given (-Roms, one or more) that has no
# picture yet, looks the game up in the free libretro thumbnail library
# (thumbnails.libretro.com, the pictures RetroArch itself uses) and saves
#   Roms\<console>\Imgs\<game file name>.png
# which is where Clove (and GarlicOS) look for box art. Pictures are made
# smaller (at most 300 x 300) so they open quickly on the handheld.
#
# A picture is only saved when the game's name matches a name in the library
# (ignoring the region and version tags); ROM hacks and homebrew usually have
# none and keep Clove's own tile. Pictures already there are never replaced.
# Games the library doesn't know (ROM hacks, homebrew) are looked up on
# TheGamesDB (thegamesdb.net), a community database that has covers for most
# well-known hacks. tools\game-art-names.txt can give a game a better search
# name:  file name without extension = name to search for
param([string[]]$Roms = @(), [string]$Source = 'https://thumbnails.libretro.com', [int]$Max = 300,
      [string]$Log = '', [string]$Cache = '', [switch]$DryRun, [switch]$NoTgdb)

$ErrorActionPreference = 'Stop'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
$kit = Split-Path -Parent $PSScriptRoot
[void][IO.Directory]::CreateDirectory((Join-Path $kit 'logs'))
if (-not $Log) { $Log = Join-Path $kit 'logs\game-art-log.txt' }
if (-not $Cache) { $Cache = Join-Path $kit 'backups\game-art-cache' }
if (-not $Roms.Count) { . (Join-Path $PSScriptRoot 'common.ps1'); $Roms = @((Join-Path (Find-Card '') 'Roms')) }
$utf8 = New-Object Text.UTF8Encoding($false)
function Say([string]$m) { Write-Host ('  ' + $m); [IO.File]::AppendAllText($Log, $m + "`r`n", $utf8) }

# Roms folder -> the libretro thumbnail collections to look in
$SYSTEMS = @{
    'GB' = @('Nintendo - Game Boy'); 'GBC' = @('Nintendo - Game Boy Color'); 'SGB' = @('Nintendo - Game Boy', 'Nintendo - Game Boy Color')
    'GBA' = @('Nintendo - Game Boy Advance'); 'GPSP' = @('Nintendo - Game Boy Advance')
    'FC' = @('Nintendo - Nintendo Entertainment System'); 'NESTOPIA' = @('Nintendo - Nintendo Entertainment System')
    'FDS' = @('Nintendo - Family Computer Disk System'); 'SFC' = @('Nintendo - Super Nintendo Entertainment System')
    'SUFAMI' = @('Nintendo - Sufami Turbo'); 'SATELLAVIEW' = @('Nintendo - Satellaview'); 'VB' = @('Nintendo - Virtual Boy')
    'POKE' = @('Nintendo - Pokemon Mini')
    'MD' = @('Sega - Mega Drive - Genesis'); 'MS' = @('Sega - Master System - Mark III'); 'GG' = @('Sega - Game Gear')
    'SEGACD' = @('Sega - Mega-CD - Sega CD'); 'THIRTYTWOX' = @('Sega - 32X'); 'SEGASGONE' = @('Sega - SG-1000')
    'PS' = @('Sony - PlayStation')
    'PCE' = @('NEC - PC Engine - TurboGrafx 16'); 'PCECD' = @('NEC - PC Engine CD - TurboGrafx-CD')
    'SGFX' = @('NEC - PC Engine SuperGrafx'); 'PCFX' = @('NEC - PC-FX')
    'NEOCD' = @('SNK - Neo Geo CD'); 'NGP' = @('SNK - Neo Geo Pocket', 'SNK - Neo Geo Pocket Color')
    'WS' = @('Bandai - WonderSwan', 'Bandai - WonderSwan Color')
    'LYNX' = @('Atari - Lynx'); 'ATARI' = @('Atari - 2600'); 'FIFTYTWOHUNDRED' = @('Atari - 5200')
    'SEVENTYEIGHTHUNDRED' = @('Atari - 7800'); 'JAGUAR' = @('Atari - Jaguar'); 'ATARIST' = @('Atari - ST')
    'PANASONIC' = @('The 3DO Company - 3DO'); 'COLECO' = @('Coleco - ColecoVision'); 'INTELLIVISION' = @('Mattel - Intellivision')
    'VECTREX' = @('GCE - Vectrex'); 'ODYSSEY' = @('Magnavox - Odyssey2'); 'VIDEOPAC' = @('Magnavox - Odyssey2')
    'FAIRCHILD' = @('Fairchild - Channel F'); 'SUPERVISION' = @('Watara - Supervision'); 'MEGADUCK' = @('Welback - Mega Duck')
    'COMMODORE' = @('Commodore - 64'); 'AMIGA' = @('Commodore - Amiga'); 'AMIGACD' = @('Commodore - CD32')
    'CPC' = @('Amstrad - CPC'); 'ZXS' = @('Sinclair - ZX Spectrum'); 'MSX' = @('Microsoft - MSX', 'Microsoft - MSX2')
    'X68000' = @('Sharp - X68000')
}
# TheGamesDB platform numbers, for the fallback search
$TGDB = @{ 'GB' = 4; 'GBC' = 41; 'GBA' = 5; 'GPSP' = 5; 'SGB' = 4; 'FC' = 7; 'NESTOPIA' = 7; 'FDS' = 4936; 'SFC' = 6; 'VB' = 4918
           'MD' = 18; 'MS' = 35; 'GG' = 20; 'SEGACD' = 21; 'THIRTYTWOX' = 33; 'PS' = 10; 'PCE' = 34; 'NGP' = 4922; 'WS' = 4925
           'LYNX' = 4924; 'ATARI' = 22; 'SEVENTYEIGHTHUNDRED' = 27; 'JAGUAR' = 28; 'COLECO' = 31; 'INTELLIVISION' = 32; 'VECTREX' = 4939
           'COMMODORE' = 40; 'AMIGA' = 4911; 'CPC' = 4914; 'ZXS' = 4913; 'MSX' = 4929; 'PANASONIC' = 25; 'NEOCD' = 4956; 'PCFX' = 4930 }
$names = @{}
$nf = Join-Path $PSScriptRoot 'game-art-names.txt'
if (Test-Path -LiteralPath $nf) {
    foreach ($l in [IO.File]::ReadAllLines($nf, $utf8)) {
        $i = $l.IndexOf(' = ')
        if ($i -gt 0 -and -not $l.StartsWith('#')) { $names[$l.Substring(0, $i).Trim().ToLowerInvariant()] = $l.Substring($i + 3).Trim() }
    }
}
# "PokemonQuetzalAlpha6v9" -> "Pokemon Quetzal Alpha", "pokemon_gaia_v3.2" -> "pokemon gaia"
function SearchName([string]$stem) {
    $k = $stem.ToLowerInvariant()
    if ($names.ContainsKey($k)) { return $names[$k] }
    $t = [regex]::Replace($stem, '\s*[\(\[][^\)\]]*[\)\]]', ' ')
    $t = $t.Replace('_', ' ').Replace('-', ' ')
    $t = [regex]::Replace($t, '(?<=[a-z])(?=[A-Z])', ' ')
    $t = [regex]::Replace($t, '(?i)\b(v|ver|version)?\d+(\.\d+)*[a-z]?\b', ' ')
    $t = [regex]::Replace($t, '(?i)\b(alpha|beta|final|demo|fix|fixed|patched|rom|hack|gba|english|eng)\b', ' ')
    return ([regex]::Replace($t, '\s+', ' ')).Trim()
}
function Strip-Tags([string]$h) { return [Net.WebUtility]::HtmlDecode([regex]::Replace($h, '<[^>]+>', ' ')) }
# "Pokemon" with an accent -> "pokemon": accents off, lower case
function Plain([string]$t) {
    $d = $t.ToLowerInvariant().Normalize([Text.NormalizationForm]::FormD)
    $sb = New-Object Text.StringBuilder
    foreach ($c in $d.ToCharArray()) {
        if ([Globalization.CharUnicodeInfo]::GetUnicodeCategory($c) -ne [Globalization.UnicodeCategory]::NonSpacingMark) { [void]$sb.Append($c) }
    }
    return $sb.ToString()
}
# the cover from TheGamesDB, or $null
function Tgdb-Find([string]$code, [string]$stem, [string]$out) {
    if ($NoTgdb -or -not $TGDB.ContainsKey($code)) { return $null }
    $q = SearchName $stem
    if (-not $q) { return $null }
    # "tgdb:12345" in game-art-names.txt names the entry outright
    $ids = @()
    if ($q -match '^tgdb:(\d+)$') { $ids = @($Matches[1]); $q = $stem }
    $words = @([regex]::Split((Plain $q), '[^a-z0-9]+') | Where-Object { $_.Length -gt 2 })
    if (-not $words.Count -and -not $ids.Count) { return $null }
    $url = 'https://thegamesdb.net/search.php?name=' + [Uri]::EscapeDataString($q) + '&platform_id%5B%5D=' + $TGDB[$code]
    $html = ''
    if (-not $ids.Count) {
        try { $html = (Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 40 -Headers @{ 'User-Agent' = 'Clove box art' }).Content } catch { return $null }
    }
    $cands = New-Object Collections.Generic.List[string]
    foreach ($i in $ids) { $cands.Add($i) }
    $tried = @{}
    foreach ($m in [regex]::Matches($html, 'game\.php\?id=(\d+)')) {
        $id = $m.Groups[1].Value
        if ($tried.ContainsKey($id)) { continue }
        $tried[$id] = $true
        # the words of the name must appear near the link
        $near = Plain (Strip-Tags $html.Substring($m.Index, [Math]::Min(700, $html.Length - $m.Index)))
        $ok = $true
        foreach ($w in $words) { if ($near.IndexOf($w) -lt 0) { $ok = $false; break } }
        if ($ok) { $cands.Add($id) }
    }
    $n = 0
    foreach ($id in $cands) {
        if (++$n -gt 4) { break }
        foreach ($img in @(('https://cdn.thegamesdb.net/images/thumb/boxart/front/' + $id + '-1.jpg'), ('https://cdn.thegamesdb.net/images/thumb/boxart/front/' + $id + '-1.png'))) {
            try {
                Invoke-WebRequest -Uri $img -OutFile $out -UseBasicParsing -TimeoutSec 40 -Headers @{ 'User-Agent' = 'Clove box art' }
                if ((Get-Item -LiteralPath $out).Length -gt 2000) { return ('TheGamesDB #' + $id + ' "' + $q + '"') }
            } catch {}
        }
    }
    return $null
}

# not games: saves, pictures, notes, the second half of a CD image ...
$SKIP_EXT = @('.png', '.jpg', '.jpeg', '.bmp', '.txt', '.nfo', '.pdf', '.srm', '.sav', '.state', '.cfg', '.xml', '.dat',
              '.ips', '.bps', '.ups', '.json', '.db', '.ini', '.sh', '.log', '.md', '.htm', '.html', '.world', '.mp4', '.mkv', '.avi')

# "Legend of Zelda, The - A Link to the Past (USA) (Rev 1)" -> "legendofzeldaalinktothepast"
function Key([string]$name) {
    $t = [regex]::Replace($name, '\s*[\(\[][^\)\]]*[\)\]]', '')
    $t = $t.ToLowerInvariant().Replace('&', ' and ').Normalize([Text.NormalizationForm]::FormD)
    $sb = New-Object Text.StringBuilder
    foreach ($w in [regex]::Split($t, '[^\p{L}\p{Nd}\p{Mn}]+')) {
        if (-not $w -or $w -eq 'the') { continue }
        foreach ($c in $w.ToCharArray()) {
            if ([Globalization.CharUnicodeInfo]::GetUnicodeCategory($c) -ne [Globalization.UnicodeCategory]::NonSpacingMark) { [void]$sb.Append($c) }
        }
    }
    return $sb.ToString()
}
function Region([string]$name) {
    $tags = ([regex]::Matches($name, '[\(\[]([^\)\]]*)[\)\]]') | ForEach-Object { $_.Groups[1].Value }) -join ','
    if ($tags -match '\bUSA\b|^U$|\bU\b') { return 'usa' }
    if ($tags -match '\bWorld\b') { return 'world' }
    if ($tags -match '\bEurope\b|\bUK\b|^E$|\bE\b') { return 'eur' }
    if ($tags -match '\bJapan\b|^J$|\bJ\b') { return 'jpn' }
    return ''
}
# lower is better
function Badness([string]$cand, [string]$want) {
    $b = 0
    if ($cand -match '(?i)\((Beta|Proto|Demo|Sample|Kiosk|Pirate|Unl|Hack|Aftermarket)[^\)]*\)') { $b += 20 }
    $r = Region $cand
    if ($want -and $r -eq $want) { } elseif ($want -and $r -eq 'world') { $b += 1 }
    else { $b += @{ 'usa' = 2; 'world' = 2; 'eur' = 3; 'jpn' = 6; '' = 5 }[$r] }
    if ($cand -match '\(Rev') { $b += 1 }
    return $b
}
# the name libretro uses for a file name: these characters become "_"
function Safe([string]$n) { return [regex]::Replace($n, '[&\*/:`<>\?\\\|"]', '_') }

function Fetch([string]$url, [string]$out) {
    if ($Source -notmatch '^https?://') {
        # a local copy of the library (tests)
        $p = [Uri]::UnescapeDataString($url.Substring($Source.Length)).TrimStart('/').Replace('/', [IO.Path]::DirectorySeparatorChar)
        $full = Join-Path $Source $p
        if ($full.EndsWith([IO.Path]::DirectorySeparatorChar)) { $full = Join-Path $full 'index.html' }
        Copy-Item -LiteralPath $full -Destination $out -Force
        return
    }
    Invoke-WebRequest -Uri $url -OutFile $out -UseBasicParsing -TimeoutSec 60 -Headers @{ 'User-Agent' = 'Clove box art' }
}

$lists = @{}
function Names([string]$sys) {
    if ($lists.ContainsKey($sys)) { return $lists[$sys] }
    [void][IO.Directory]::CreateDirectory($Cache)
    $cf = Join-Path $Cache ((Safe $sys) + '.txt')
    $names = @()
    if ((Test-Path -LiteralPath $cf) -and ((Get-Date) - (Get-Item -LiteralPath $cf).LastWriteTime).TotalDays -lt 14) {
        $names = @([IO.File]::ReadAllLines($cf, $utf8) | Where-Object { $_ })
    } else {
        $tmp = Join-Path $Cache 'listing.tmp'
        try {
            Fetch ($Source.TrimEnd('/') + '/' + [Uri]::EscapeDataString($sys) + '/Named_Boxarts/') $tmp
            $html = [IO.File]::ReadAllText($tmp, $utf8)
            $names = @([regex]::Matches($html, 'href="([^"?/]+)\.png"') | ForEach-Object { [Uri]::UnescapeDataString($_.Groups[1].Value) })
            if ($names.Count) { [IO.File]::WriteAllLines($cf, $names, $utf8) }
        } catch { Say ('could not read the list of ' + $sys + ' pictures (' + $_.Exception.Message + ')') }
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
    }
    $byKey = @{}
    foreach ($n in $names) {
        $k = Key $n
        if (-not $k) { continue }
        if (-not $byKey.ContainsKey($k)) { $byKey[$k] = New-Object Collections.Generic.List[string] }
        $byKey[$k].Add($n)
    }
    $exact = @{}
    foreach ($n in $names) { $exact[$n.ToLowerInvariant()] = $n }
    $lists[$sys] = @{ 'byKey' = $byKey; 'exact' = $exact; 'count' = $names.Count }
    return $lists[$sys]
}

# the best picture name for a game, and which collection it's in
function Find([string]$code, [string]$stem) {
    $want = Region $stem
    $k = Key $stem
    $best = $null; $bestSys = $null; $bestBad = 1e9
    foreach ($sys in $SYSTEMS[$code]) {
        $L = Names $sys
        $e = (Safe $stem).ToLowerInvariant()
        if ($L.exact.ContainsKey($e)) { return @($sys, $L.exact[$e]) }
        if ($k -and $L.byKey.ContainsKey($k)) {
            foreach ($c in $L.byKey[$k]) {
                $b = Badness $c $want
                if ($b -lt $bestBad) { $best = $c; $bestSys = $sys; $bestBad = $b }
            }
        }
    }
    if ($best) { return @($bestSys, $best) }
    return $null
}

# Pictures are made smaller with System.Drawing (part of Windows); without it
# they're saved as they come. (Built from text so other systems can still run this.)
$shrink = $null
try {
    Add-Type -AssemblyName System.Drawing
    $shrink = [scriptblock]::Create(@'
param($from, $to, $max)
$img = [System.Drawing.Image]::FromFile($from)
try {
    $s = [Math]::Min(1.0, [Math]::Min($max / $img.Width, $max / $img.Height))
    $w = [Math]::Max(1, [int][Math]::Round($img.Width * $s)); $h = [Math]::Max(1, [int][Math]::Round($img.Height * $s))
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($img, 0, 0, $w, $h)
    $g.Dispose()
    $bmp.Save($to, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
} finally { $img.Dispose() }
'@)
    $probe = New-Object System.Drawing.Bitmap(1, 1); $probe.Dispose()
} catch { $shrink = $null }
function Save-Small([string]$from, [string]$to) {
    if ($shrink) {
        try { & $shrink $from $to $Max; return } catch {}
    }
    Copy-Item -LiteralPath $from -Destination $to -Force
}

[IO.File]::AppendAllText($Log, ("`r`n==== " + (Get-Date -Format 'yyyy-MM-dd HH:mm') + "`r`n"), $utf8)
$got = 0; $had = 0; $none = New-Object Collections.Generic.List[string]
$saved = New-Object Collections.Generic.List[string]
foreach ($root in $Roms) {
    if (-not (Test-Path -LiteralPath $root)) { Say ('not found: ' + $root); continue }
    foreach ($sd in Get-ChildItem -LiteralPath $root -Directory) {
        $code = $sd.Name.ToUpperInvariant()
        if (-not $SYSTEMS.ContainsKey($code)) { continue }
        # games: files in the console folder, and in its sub-folders (multi-file games)
        $stems = [ordered]@{}
        foreach ($f in Get-ChildItem -LiteralPath $sd.FullName -File -Recurse -Depth 1 -ErrorAction SilentlyContinue) {
            if ($f.DirectoryName -match '[\\/](Imgs|images|Documentation|files)$') { continue }
            if ($f.Name.StartsWith('.')) { continue }
            if ($SKIP_EXT -contains $f.Extension.ToLowerInvariant()) { continue }
            $st = [IO.Path]::GetFileNameWithoutExtension($f.Name)
            if (-not $stems.Contains($st)) { $stems[$st] = $f.FullName }
        }
        if (-not $stems.Count) { continue }
        $imgs = Join-Path $sd.FullName 'Imgs'
        foreach ($st in $stems.Keys) {
            if ((Test-Path -LiteralPath (Join-Path $imgs ($st + '.png'))) -or (Test-Path -LiteralPath (Join-Path $imgs ($st + '.jpg')))) { $had++; continue }
            $m = Find $code $st
            $tmp = Join-Path $Cache 'art.tmp'
            if (-not $m) {
                # not in the library: ask TheGamesDB
                if ($DryRun) { $none.Add($code + '/' + $st); continue }
                [void][IO.Directory]::CreateDirectory($Cache)
                $from = Tgdb-Find $code $st $tmp
                if (-not $from) { $none.Add($code + '/' + $st); continue }
                try {
                    [void][IO.Directory]::CreateDirectory($imgs)
                    $dest = Join-Path $imgs ($st + '.png')
                    Save-Small $tmp $dest
                    $saved.Add($dest)
                    Say ('{0}/{1}  <- {2}' -f $code, $st, $from)
                    $got++
                } catch { Say ('{0}/{1}: could not save ({2})' -f $code, $st, $_.Exception.Message); $none.Add($code + '/' + $st) }
                finally { if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force } }
                continue
            }
            if ($DryRun) { Say ('{0}/{1}  <- {2}' -f $code, $st, $m[1]); $got++; continue }
            try {
                Fetch ($Source.TrimEnd('/') + '/' + [Uri]::EscapeDataString($m[0]) + '/Named_Boxarts/' + [Uri]::EscapeDataString($m[1]) + '.png') $tmp
                [void][IO.Directory]::CreateDirectory($imgs)
                $dest = Join-Path $imgs ($st + '.png')
                Save-Small $tmp $dest
                $saved.Add($dest)
                Say ('{0}/{1}  <- {2}' -f $code, $st, $m[1])
                $got++
            } catch { Say ('{0}/{1}: download failed ({2})' -f $code, $st, $_.Exception.Message); $none.Add($code + '/' + $st) }
            finally { if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force } }
        }
    }
}
# Videos: a picture from the film itself (the same thumbnail Windows shows in
# File Explorer), saved as Roms\VIDEOS\Imgs\<file name>.png for Zappo's Videos tab.
$vgot = 0; $vhad = 0; $vfail = 0
$thumb = $null
if ($shrink -and $env:OS -eq 'Windows_NT' -and -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'ffmpeg\ffmpeg.exe'))) {
    try {
        Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Drawing;
public static class ZappoShellThumb {
    [ComImport, Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IShellItemImageFactory { [PreserveSig] int GetImage(SIZE size, int flags, out IntPtr phbm); }
    [StructLayout(LayoutKind.Sequential)] struct SIZE { public int cx, cy; public SIZE(int x, int y) { cx = x; cy = y; } }
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    static extern void SHCreateItemFromParsingName(string path, IntPtr pbc, ref Guid riid, [MarshalAs(UnmanagedType.Interface)] out IShellItemImageFactory f);
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr h);
    public static Bitmap Get(string path, int w, int h) {
        Guid g = new Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b");
        IShellItemImageFactory f;
        SHCreateItemFromParsingName(path, IntPtr.Zero, ref g, out f);
        IntPtr hbm = IntPtr.Zero;
        int hr = unchecked((int)0x8000000A);   // E_PENDING: the thumbnail is still being made
        // a big film on a slow card can take Windows a while: ask a few times
        for (int tries = 0; tries < 8 && hr != 0; tries++) {
            if (tries > 0) System.Threading.Thread.Sleep(1500);
            hr = f.GetImage(new SIZE(w, h), 0x8 | 0x2, out hbm);   // thumbnail only, bigger size ok
        }
        if (hr != 0) {
            // ask again letting the shell make it here and now (it may hand back an icon instead:
            // those are small, a frame from the film is not)
            hr = f.GetImage(new SIZE(w, h), 0x2, out hbm);
            if (hr != 0) Marshal.ThrowExceptionForHR(hr);
            Bitmap t = Bitmap.FromHbitmap(hbm);
            DeleteObject(hbm);
            if (t.Width <= 256 && t.Height <= 256) { t.Dispose(); throw new Exception("Windows has no thumbnail for this file (only an icon)"); }
            return t;
        }
        Bitmap b = Bitmap.FromHbitmap(hbm);
        DeleteObject(hbm);
        return b;
    }
}
'@
        $thumb = [scriptblock]::Create(@'
param($from, $to, $w, $h)
$src = [ZappoShellThumb]::Get($from, 640, 640)
try {
    # trim the black bars Explorer adds round a wide picture
    $bmp = New-Object System.Drawing.Bitmap($src)
    $x0 = 0; $y0 = 0; $x1 = $bmp.Width - 1; $y1 = $bmp.Height - 1
    function Dark($c) { return ($c.R + $c.G + $c.B) -lt 30 }
    while ($y0 -lt $y1 -and (Dark $bmp.GetPixel([int]($bmp.Width / 2), $y0)) -and (Dark $bmp.GetPixel([int]($bmp.Width / 4), $y0))) { $y0++ }
    while ($y1 -gt $y0 -and (Dark $bmp.GetPixel([int]($bmp.Width / 2), $y1)) -and (Dark $bmp.GetPixel([int]($bmp.Width / 4), $y1))) { $y1-- }
    while ($x0 -lt $x1 -and (Dark $bmp.GetPixel($x0, [int]($bmp.Height / 2))) -and (Dark $bmp.GetPixel($x0, [int]($bmp.Height / 4)))) { $x0++ }
    while ($x1 -gt $x0 -and (Dark $bmp.GetPixel($x1, [int]($bmp.Height / 2))) -and (Dark $bmp.GetPixel($x1, [int]($bmp.Height / 4)))) { $x1-- }
    if (($x1 - $x0) -lt 32 -or ($y1 - $y0) -lt 32) { $x0 = 0; $y0 = 0; $x1 = $bmp.Width - 1; $y1 = $bmp.Height - 1 }
    $cw = $x1 - $x0 + 1; $ch = $y1 - $y0 + 1
    # fill w x h, cropping a little rather than stretching
    $s = [Math]::Max($w / $cw, $h / $ch)
    $sw = [Math]::Min($cw, [int]($w / $s)); $sh = [Math]::Min($ch, [int]($h / $s))
    $sx = $x0 + [int](($cw - $sw) / 2); $sy = $y0 + [int](($ch - $sh) / 2)
    $out = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($out)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($bmp, (New-Object System.Drawing.Rectangle(0, 0, $w, $h)), (New-Object System.Drawing.Rectangle($sx, $sy, $sw, $sh)), [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose()
    $out.Save($to, [System.Drawing.Imaging.ImageFormat]::Png)
    $out.Dispose(); $bmp.Dispose()
} finally { $src.Dispose() }
'@)
    } catch { $thumb = $null; Say ('(video pictures need Windows'' thumbnail service: ' + $_.Exception.Message + ')') }
}
# with ffmpeg in tools\ffmpeg (the update script fetches it), a frame a little way into
# the film is used instead: it works for every file and picks a brighter moment
$ffmpeg = Join-Path $PSScriptRoot 'ffmpeg\ffmpeg.exe'
$ffprobe = Join-Path $PSScriptRoot 'ffmpeg\ffprobe.exe'
if ((Test-Path -LiteralPath $ffmpeg) -and (Test-Path -LiteralPath $ffprobe)) {
    $thumb = [scriptblock]::Create(@'
param($from, $to, $w, $h)
$secs = 0
try { $secs = [double](& $ffprobe -v error -show_entries format=duration -of csv=p=0 -- $from 2>$null) } catch {}
if (-not $secs) { $secs = 600 }
# a few candidate moments between 10% and 40% in; keep the one with the most going on
$best = $null; $bestScore = -1
foreach ($frac in @(0.12, 0.2, 0.3, 0.4)) {
    $tmp = $to + ('.c{0}.png' -f [int]($frac * 100))
    & $ffmpeg -v error -y -ss ([int]($secs * $frac)) -i $from -frames:v 1 -vf ('scale={0}:{1}:force_original_aspect_ratio=increase,crop={0}:{1}' -f $w, $h) $tmp 2>$null
    if (-not (Test-Path -LiteralPath $tmp)) { continue }
    try {
        $bmp = [System.Drawing.Bitmap]::FromFile($tmp)
        $sum = 0.0; $sq = 0.0; $n = 0
        for ($y = 4; $y -lt $bmp.Height; $y += 12) { for ($x = 4; $x -lt $bmp.Width; $x += 12) { $c = $bmp.GetPixel($x, $y); $l = ($c.R + $c.G + $c.B) / 3.0; $sum += $l; $sq += $l * $l; $n++ } }
        $bmp.Dispose()
        $mean = $sum / [Math]::Max(1, $n); $sd = [Math]::Sqrt([Math]::Max(0, $sq / [Math]::Max(1, $n) - $mean * $mean))
        $score = $sd + [Math]::Min($mean, 110) * 0.5
    } catch { $score = 0 }
    if ($score -gt $bestScore) { if ($best) { Remove-Item -LiteralPath $best -Force }; $best = $tmp; $bestScore = $score } else { Remove-Item -LiteralPath $tmp -Force }
}
if (-not $best) { throw 'ffmpeg could not read a frame' }
Move-Item -LiteralPath $best -Destination $to -Force
'@)
}
# Real posters, from Apple's free film / TV / music catalogue (no account needed).
# A film is looked up by its name (the year in brackets helps), a folder as a TV
# series. tools\video-posters.txt can steer it:
#   <video or folder name> = movie:<title>  |  tv:<title>  |  album:<artist title>  |  none
# and an optional  | label:<text>  draws big text (a number, "Ep 4") on the poster.
$posters = @{}
$ppf = Join-Path $PSScriptRoot 'video-posters.txt'
if (Test-Path -LiteralPath $ppf) {
    foreach ($l in [IO.File]::ReadAllLines($ppf, $utf8)) {
        $i = $l.IndexOf(' = ')
        if ($i -gt 0 -and -not $l.StartsWith('#')) { $posters[$l.Substring(0, $i).Trim().ToLowerInvariant()] = $l.Substring($i + 3).Trim() }
    }
}
. (Join-Path $PSScriptRoot 'poster-lookup.ps1')
$label = $null
if ($shrink) {
    $label = [scriptblock]::Create(@'
param($file, $text)
$img = [System.Drawing.Image]::FromFile($file)
$bmp = New-Object System.Drawing.Bitmap($img); $img.Dispose()
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
$size = [Math]::Max(18, [int]($bmp.Height * $(if ($text.Length -le 2) { 0.22 } else { 0.11 })))
$font = New-Object System.Drawing.Font('Arial', $size, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$m = $g.MeasureString($text, $font)
$pad = [int]($size * 0.25)
$x = $bmp.Width - $m.Width - $pad * 2; $y = $bmp.Height - $m.Height - $pad * 2
$back = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(170, 0, 0, 0))
$g.FillRectangle($back, [float]($x - $pad), [float]($y - $pad), [float]($m.Width + $pad * 2), [float]($m.Height + $pad * 2))
$g.DrawString($text, $font, [System.Drawing.Brushes]::White, [float]$x, [float]$y)
$g.Dispose()
$bmp.Save($file + '.tmp.png', [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
Move-Item -LiteralPath ($file + '.tmp.png') -Destination $file -Force
'@)
}
function Poster-For([string]$stem, [bool]$isFolder, [string]$out) {
    # saves a real poster to $out; 1 when it did
    $rule = $posters[$stem.ToLowerInvariant()]
    $kind = if ($isFolder) { 'tv' } else { 'movie' }
    $title = $stem -replace '^\d+\.\s*', ''          # "1. Pokémon The First Movie" -> ...
    $text = ''
    if ($rule) {
        foreach ($part in $rule.Split('|')) {
            $part = $part.Trim()
            if ($part -eq 'none') { return $false }
            if ($part -match '^(movie|tv|album):(.+)$') { $kind = $Matches[1]; $title = $Matches[2].Trim() }
            elseif ($part -match '^label:(.+)$') { $text = $Matches[1].Trim() }
        }
    }
    $tries = @($title)
    if ($title.Contains(' - ')) { $tries += $title.Substring(0, $title.IndexOf(' - ')).Trim() }
    $art = $null
    foreach ($t in $tries) { $art = Find-Poster $kind $t; if ($art) { break } }
    if (-not $art) { return $false }
    $tmp = $out + '.dl'
    try {
        Invoke-WebRequest -Uri $art -OutFile $tmp -UseBasicParsing -TimeoutSec 60 -Headers @{ 'User-Agent' = $script:posterUA }
        Save-Small $tmp $out
        if ($text -and $label) { try { & $label $out $text } catch {} }
        return $true
    } catch { return $false }
    finally { if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force } }
}
$VIDEO_EXT = @('.mp4', '.m4v', '.mkv', '.avi', '.mov', '.webm', '.mpg', '.mpeg', '.wmv', '.flv', '.ts')
foreach ($root in $Roms) {
    $vd = Join-Path $root 'VIDEOS'
    if (-not (Test-Path -LiteralPath $vd)) { continue }
    foreach ($f in Get-ChildItem -LiteralPath $vd -File -Recurse -Depth 2 -ErrorAction SilentlyContinue) {
        if ($f.DirectoryName -match '[\\/]Imgs$') { continue }
        if ($VIDEO_EXT -notcontains $f.Extension.ToLowerInvariant()) { continue }
        $imgs = Join-Path $f.DirectoryName 'Imgs'
        $st = [IO.Path]::GetFileNameWithoutExtension($f.Name)
        $isEpisode = $f.DirectoryName -ne $vd   # files inside a series folder keep a frame from the episode
        # Imgs\.frames.txt lists the pictures that are only a frame from the film: a real poster may still replace those
        $framesFile = Join-Path $imgs '.frames.txt'
        $frames = @(); if (Test-Path -LiteralPath $framesFile) { $frames = @([IO.File]::ReadAllLines($framesFile, $utf8)) }
        $dest = Join-Path $imgs ($st + '.png')
        $has = (Test-Path -LiteralPath $dest) -or (Test-Path -LiteralPath (Join-Path $imgs ($st + '.jpg')))
        if ($has -and -not ($frames -contains $st -and -not $isEpisode -and -not $NoTgdb)) { $vhad++; continue }
        if ($DryRun) { $vfail++; continue }
        [void][IO.Directory]::CreateDirectory($imgs)
        if (-not $isEpisode -and -not $NoTgdb -and (Poster-For $st $false $dest)) {
            $saved.Add($dest); Say ('VIDEOS/{0}  <- poster' -f $st); $vgot++
            if ($frames -contains $st) { [IO.File]::WriteAllLines($framesFile, @($frames | Where-Object { $_ -ne $st }), $utf8) }
            continue
        }
        if ($has) { $vhad++; continue }
        if (-not $thumb) { $vfail++; continue }
        try {
            & $thumb $f.FullName $dest 320 180
            $saved.Add($dest)
            [IO.File]::AppendAllText($framesFile, $st + "`r`n", $utf8)
            Say ('VIDEOS/{0}  <- picture from the film' -f $st)
            $vgot++
        } catch { Say ('VIDEOS/{0}: no picture ({1})' -f $st, $_.Exception.Message); $vfail++ }
    }
    # a poster for each series / collection folder (shown on the folder's tile)
    foreach ($sd in Get-ChildItem -LiteralPath $vd -Directory) {
        if ($sd.Name -eq 'Imgs') { continue }
        $imgs = Join-Path $vd 'Imgs'
        $dest = Join-Path $imgs ($sd.Name + '.png')
        if ((Test-Path -LiteralPath $dest) -or $DryRun -or $NoTgdb) { continue }
        [void][IO.Directory]::CreateDirectory($imgs)
        if (Poster-For $sd.Name $true $dest) { $saved.Add($dest); Say ('VIDEOS/{0}/  <- series poster' -f $sd.Name); $vgot++ }
    }
}
Say ''
Say ('Box art: {0} new, {1} already had a picture, {2} not in the library' -f $got, $had, $none.Count)
if ($vgot -or $vhad -or $vfail) { Say ('Video pictures: {0} new, {1} already there, {2} without one' -f $vgot, $vhad, $vfail) }
if ($none.Count) {
    Say 'No picture found for these - add your own as Roms\<console>\Imgs\<game>.png, or give the game a search name in tools\game-art-names.txt:'
    foreach ($n in $none) { Say ('   ' + $n) }
}
# the pictures saved this time, for the update script
$lp = Join-Path $Cache 'saved.txt'
[void][IO.Directory]::CreateDirectory($Cache)
[IO.File]::WriteAllLines($lp, $saved, $utf8)

# Clove - unpacks downloaded video collections (zip files in Downloads) into
# Downloads\videos\<collection name>\ so the update script can put them on the
# card as folders (Zappo.bat -> 2). Zips are left where they are. Run it again any time: a
# collection that is already unpacked is skipped.
param([string]$Downloads = '')
$ErrorActionPreference = 'Stop'
$kit = Split-Path -Parent $PSScriptRoot
$dl = if ($Downloads) { $Downloads } else { Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Downloads' }
[void][IO.Directory]::CreateDirectory((Join-Path $kit 'logs'))
$log = Join-Path $kit 'logs\unpack-videos-log.txt'
Start-Transcript -Path $log -Force | Out-Null
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $vexts = @('.mp4', '.m4v', '.mkv', '.avi', '.mov', '.webm')
    $out = Join-Path $dl 'videos'
    [void][IO.Directory]::CreateDirectory($out)
    function Nice([string]$n) {
        $n = $n -replace '\s*\(\d+\)$', ''           # "name (2)" -> "name"
        $n = ($n -replace '[-_.]+', ' ').Trim()
        return [regex]::Replace($n, '\b[a-z]', { param($m) $m.Value.ToUpper() })   # pokemon chronicles -> Pokemon Chronicles
    }
    $total = 0
    foreach ($z in Get-ChildItem -LiteralPath $dl -File -Filter '*.zip') {
        $zip = $null
        try { $zip = [IO.Compression.ZipFile]::OpenRead($z.FullName) } catch { Write-Host ('  ' + $z.Name + ': not a zip file I can open (' + $_.Exception.Message + ')'); continue }
        try {
            $vids = @($zip.Entries | Where-Object { $_.Name -and $vexts -contains [IO.Path]::GetExtension($_.Name).ToLowerInvariant() })
            if (-not $vids.Count) { continue }    # not a video collection
            $folder = Join-Path $out (Nice $z.BaseName)
            [void][IO.Directory]::CreateDirectory($folder)
            $n = 0; $had = 0
            foreach ($e in $vids) {
                $dest = Join-Path $folder $e.Name
                if ((Test-Path -LiteralPath $dest) -and (Get-Item -LiteralPath $dest).Length -eq $e.Length) { $had++; continue }
                Write-Progress -Activity ('Unpacking ' + $z.Name) -Status $e.Name -PercentComplete ([int](100 * ($n + $had) / $vids.Count))
                [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dest + '.part', $true)
                Move-Item -LiteralPath ($dest + '.part') -Destination $dest -Force
                $n++
            }
            Write-Progress -Activity ('Unpacking ' + $z.Name) -Completed
            Write-Host ('  {0}: {1} video(s) unpacked, {2} already there -> videos\{3}' -f $z.Name, $n, $had, (Split-Path -Leaf $folder))
            $total += $n
        } finally { $zip.Dispose() }
    }
    Write-Host ('  {0} video(s) unpacked in all. ALL DONE' -f $total)
} catch {
    Write-Host ('  SOMETHING WENT WRONG: ' + $_.Exception.Message)
} finally { Stop-Transcript | Out-Null }

<#
.SYNOPSIS
  Create the deterministic 100k-entry folder used by docs/overhaul/perf.md.

.DESCRIPTION
  100,000 files across 24 extensions plus 200 subfolders, with sizes 0-4095 bytes and modification
  times spread over five years, all derived from the index so every run builds the same folder.
  About 400 MB on disk. Re-running with the same -Count is a no-op when the folder is complete.
#>
param(
    [string] $Path = 'C:\fsperf\100k',
    [int] $Count = 100000,
    [int] $Folders = 200
)
$ErrorActionPreference = 'Stop'
$extensions = 'txt','log','md','json','xml','csv','cs','cpp','h','py','js','ts','html','css',
              'png','jpg','gif','pdf','docx','xlsx','zip','7z','mp3','mp4'
New-Item -ItemType Directory -Force $Path | Out-Null
$existing = [IO.Directory]::GetFileSystemEntries($Path).Length
if ($existing -eq $Count + $Folders) { "complete: $existing entries in $Path"; return }

$sw = [Diagnostics.Stopwatch]::StartNew()
$base = [datetime]::new(2021, 1, 1, 0, 0, 0, [DateTimeKind]::Utc)
$buffer = New-Object byte[] 4096
for ($i = 0; $i -lt $Folders; $i++) {
    [void][IO.Directory]::CreateDirectory((Join-Path $Path ('folder_{0:D3}' -f $i)))
}
for ($i = 0; $i -lt $Count; $i++) {
    $ext = $extensions[$i % $extensions.Length]
    # Names mix a word stem with the index so name sorting is not the creation order.
    $stem = ('alpha','bravo','charlie','delta','echo','foxtrot','golf','hotel')[($i * 7) % 8]
    $file = Join-Path $Path ('{0}_{1:D6}.{2}' -f $stem, (($i * 7919) % $Count), $ext)
    $size = ($i * 2654435761) % 4096
    $stream = [IO.File]::Create($file)
    $stream.Write($buffer, 0, [int]$size)
    $stream.Dispose()
    [IO.File]::SetLastWriteTimeUtc($file, $base.AddMinutes(($i * 104729) % 2628000))
}
"created {0} files + {1} folders in {2:n0}s" -f $Count, $Folders, $sw.Elapsed.TotalSeconds

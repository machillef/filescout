#Requires -Version 7.0
<#
.SYNOPSIS
    Parses FileScout's startup trace log and reports per-session timing metrics.

.DESCRIPTION
    Reads %TEMP%\FileScout.Native.trace.log, splits it into sessions by the
    SESSION START markers written by LogSessionStart() (SP1), extracts [TIMER]
    values for key startup phases, and renders a table with a 7-day rolling
    average row at the bottom.

    Columns:
      Session              - date/time of the SESSION START marker
      BuildSidebarFavorites - ms for sidebar Quick Access build
      InitPane(0)          - ms for left-pane initialisation
      LoadDirectory        - ms for the first (home-folder) directory load
      TotalStartup         - wall-clock ms from SESSION START to sidebar done

    Missing values (timer line absent or session incomplete) are shown as  --.

.PARAMETER LogPath
    Path to the trace log. Defaults to %TEMP%\FileScout.Native.trace.log.

.PARAMETER Days
    Number of calendar days to include in the rolling average. Default: 7.

.EXAMPLE
    pwsh tools/Get-StartupProfile.ps1

.EXAMPLE
    pwsh tools/Get-StartupProfile.ps1 -LogPath C:\Temp\my.log -Days 14
#>
[CmdletBinding()]
param(
    [string] $LogPath = (Join-Path $env:TEMP 'FileScout.Native.trace.log'),
    [int]    $Days    = 7
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Get-TimerMs {
    param([string[]] $Lines, [string] $Label)
    # Matches: HH:MM:SS.mmm [tick] [TIMER] <Label>: done in <N>ms
    $pattern = '^\S+ \[\d+\] \[TIMER\] ' + [regex]::Escape($Label) + ': done in (\d+)ms$'
    foreach ($line in $Lines) {
        if ($line -match $pattern) {
            return [int]$Matches[1]
        }
    }
    return $null
}

function Get-FirstLoadDirectoryMs {
    param([string[]] $Lines)
    # First [TIMER] LoadDirectory: line in the session = home-folder startup load.
    $pattern = '^\S+ \[\d+\] \[TIMER\] LoadDirectory: .+: done in (\d+)ms$'
    foreach ($line in $Lines) {
        if ($line -match $pattern) {
            return [int]$Matches[1]
        }
    }
    return $null
}

function Get-WallClockMs {
    param([string] $StartLine, [string] $EndLine)
    # Timestamps are HH:MM:SS.mmm at the start of each log line.
    $tsPattern = '^(\d{2}):(\d{2}):(\d{2})\.(\d{3})'
    if ($StartLine -match $tsPattern) {
        $sh = [int]$Matches[1]; $sm = [int]$Matches[2]
        $ss = [int]$Matches[3]; $sms = [int]$Matches[4]
        $startTotalMs = (($sh * 3600) + ($sm * 60) + $ss) * 1000 + $sms
    } else { return $null }

    if ($EndLine -match $tsPattern) {
        $eh = [int]$Matches[1]; $em = [int]$Matches[2]
        $es = [int]$Matches[3]; $ems = [int]$Matches[4]
        $endTotalMs = (($eh * 3600) + ($em * 60) + $es) * 1000 + $ems
    } else { return $null }

    # Handle midnight rollover.
    $delta = $endTotalMs - $startTotalMs
    if ($delta -lt 0) { $delta += 86400000 }
    return $delta
}

function Format-Ms {
    param($Value)
    if ($null -eq $Value) { return '--' }
    return "$Value ms"
}

# ---------------------------------------------------------------------------
# Read log
# ---------------------------------------------------------------------------

if (-not (Test-Path $LogPath)) {
    Write-Host "Log file not found: $LogPath" -ForegroundColor Yellow
    Write-Host "Launch FileScout at least once to generate it."
    exit 0
}

$allLines = Get-Content $LogPath -Encoding Unicode -ErrorAction SilentlyContinue
if (-not $allLines) {
    # Fallback: some systems write UTF-8 without BOM.
    $allLines = Get-Content $LogPath -Encoding UTF8 -ErrorAction SilentlyContinue
}
if (-not $allLines -or $allLines.Count -eq 0) {
    Write-Host "Log file is empty: $LogPath" -ForegroundColor Yellow
    exit 0
}

# ---------------------------------------------------------------------------
# Split into sessions
# ---------------------------------------------------------------------------

# Session-start pattern from LogSessionStart():
#   HH:MM:SS.mmm [tick] === SESSION START | YYYY-MM-DD | PID:N | Windows ... ===
$sessionStartPattern = '^\S+ \[\d+\] === SESSION START \| (\d{4}-\d{2}-\d{2}) \|'

$sessions = [System.Collections.Generic.List[hashtable]]::new()
$currentLines = [System.Collections.Generic.List[string]]::new()
$currentDate  = $null
$currentStartLine = $null

foreach ($line in $allLines) {
    if ($line -match $sessionStartPattern) {
        if ($currentDate -and $currentLines.Count -gt 0) {
            $sessions.Add(@{
                Date      = $currentDate
                StartLine = $currentStartLine
                Lines     = $currentLines.ToArray()
            })
        }
        $currentDate      = $Matches[1]
        $currentStartLine = $line
        $currentLines     = [System.Collections.Generic.List[string]]::new()
        $currentLines.Add($line)
    } else {
        $currentLines.Add($line)
    }
}
# Flush final session.
if ($currentDate -and $currentLines.Count -gt 0) {
    $sessions.Add(@{
        Date      = $currentDate
        StartLine = $currentStartLine
        Lines     = $currentLines.ToArray()
    })
}

if ($sessions.Count -eq 0) {
    Write-Host "No SESSION START markers found in log." -ForegroundColor Yellow
    Write-Host "Ensure you are running a build that includes SP1 (feat(profiling): add ScopedTimer helper)."
    exit 0
}

# ---------------------------------------------------------------------------
# Extract metrics per session
# ---------------------------------------------------------------------------

$rows = foreach ($s in $sessions) {
    $lines = $s.Lines

    $sidebar  = Get-TimerMs          -Lines $lines -Label 'BuildSidebarFavorites'
    $pane0    = Get-TimerMs          -Lines $lines -Label 'InitPane(0)'
    $loadDir  = Get-FirstLoadDirectoryMs -Lines $lines

    # TotalStartup = wall-clock from SESSION START line to BuildSidebarFavorites [TIMER] line.
    $sidebarLine = $lines | Where-Object { $_ -match '\[TIMER\] BuildSidebarFavorites: done in' } |
                   Select-Object -First 1
    $total = if ($sidebarLine) {
        Get-WallClockMs -StartLine $s.StartLine -EndLine $sidebarLine
    } else { $null }

    # Session label: date + time extracted from the SESSION START timestamp.
    $timeStamp = if ($s.StartLine -match '^(\d{2}:\d{2}:\d{2})') { $Matches[1] } else { '??' }
    $label = "$($s.Date) $timeStamp"

    [pscustomobject]@{
        Session               = $label
        Date                  = [datetime]::ParseExact($s.Date, 'yyyy-MM-dd', $null)
        BuildSidebarFavorites = $sidebar
        'InitPane(0)'         = $pane0
        LoadDirectory         = $loadDir
        TotalStartup          = $total
    }
}

# ---------------------------------------------------------------------------
# 7-day rolling average (calendar days, not rolling window)
# ---------------------------------------------------------------------------

$cutoff = (Get-Date).Date.AddDays(-$Days)
$recent = @($rows | Where-Object { $_.Date -ge $cutoff })

function Get-Avg {
    param([object[]] $Rows, [string] $Prop)
    $vals = @($Rows | Where-Object { $null -ne $_.$Prop } | ForEach-Object { $_.$Prop })
    if ($vals.Count -eq 0) { return $null }
    return [int][math]::Round(($vals | Measure-Object -Sum).Sum / $vals.Count)
}

$avgRow = [pscustomobject]@{
    Session               = "$Days-day avg ($($recent.Count) sessions)"
    Date                  = [datetime]::MinValue
    BuildSidebarFavorites = Get-Avg -Rows $recent -Prop 'BuildSidebarFavorites'
    'InitPane(0)'         = Get-Avg -Rows $recent -Prop 'InitPane(0)'
    LoadDirectory         = Get-Avg -Rows $recent -Prop 'LoadDirectory'
    TotalStartup          = Get-Avg -Rows $recent -Prop 'TotalStartup'
}

# ---------------------------------------------------------------------------
# Render table
# ---------------------------------------------------------------------------

$col1W = [math]::Max(23, ($rows | ForEach-Object { $_.Session.Length } | Measure-Object -Maximum).Maximum + 2)

function Pad  { param([string] $s, [int] $w) $s.PadRight($w) }
function PadL { param([string] $s, [int] $w) $s.PadLeft($w)  }

$headers = @('Session', 'Sidebar', 'InitPane(0)', 'LoadDir', 'TotalStartup')
$widths  = @($col1W, 10, 12, 10, 13)

$header = ''
$divider = ''
for ($i = 0; $i -lt $headers.Count; $i++) {
    $header  += (Pad $headers[$i] $widths[$i]) + '  '
    $divider += ('-' * $widths[$i]) + '  '
}

Write-Host ''
Write-Host $header.TrimEnd()
Write-Host $divider.TrimEnd()

function Write-Row {
    param([object] $Row, [switch] $IsAvg)
    $cols = @(
        Pad  $Row.Session               $widths[0]
        PadL (Format-Ms $Row.BuildSidebarFavorites) ($widths[1] - 2)
        PadL (Format-Ms $Row.'InitPane(0)')          ($widths[2] - 2)
        PadL (Format-Ms $Row.LoadDirectory)          ($widths[3] - 2)
        PadL (Format-Ms $Row.TotalStartup)           ($widths[4] - 2)
    )
    $line = ''
    for ($i = 0; $i -lt $cols.Count; $i++) {
        $line += $cols[$i] + '  '
    }
    if ($IsAvg) {
        Write-Host $line.TrimEnd() -ForegroundColor Cyan
    } else {
        Write-Host $line.TrimEnd()
    }
}

foreach ($row in $rows) {
    Write-Row -Row $row
}

Write-Host ($divider.TrimEnd()) -ForegroundColor DarkGray
Write-Row -Row $avgRow -IsAvg
Write-Host ''
Write-Host "Log: $LogPath  |  Total sessions: $($rows.Count)  |  In last $Days days: $($recent.Count)" -ForegroundColor DarkGray
Write-Host ''

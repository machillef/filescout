<#
.SYNOPSIS
  Alternating before/after performance runs of FileScout in the interactive session.

.DESCRIPTION
  For each run and each build (order alternates every run: A B, B A, A B, ...):
    load100k  - start with the 100k folder; the app's bench mode (FILESCOUT_BENCH_OUT, Bench.h)
                reports first_rows, full_list and the frame intervals of a 300-frame scroll.
                Then this script reads private bytes and samples CPU for -IdleSeconds.
    coldstart - start with a small folder; window_first_frame and first_rows are the cold start.
  Holds C:\fsdev\build.lock for the whole run, so no build competes for the CPU.
  Writes <Out>\results.csv (one row per metric) and prints medians per build.
#>
param(
    [Parameter(Mandatory)] [string[]] $Names,
    [Parameter(Mandatory)] [string[]] $Exes,
    [int] $Runs = 5,
    [string] $Target = 'C:\fsperf\100k',
    [string] $SmallTarget = 'C:\Windows\Web',
    [int] $IdleSeconds = 20,
    [string] $Out = 'C:\fsdev\gui-out\bench'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
$lock = $null
while (-not $lock) {
    try { $lock = [IO.File]::Open('C:\fsdev\build.lock', 'OpenOrCreate', 'ReadWrite', 'None') }
    catch [IO.IOException] { Write-Output 'waiting for build lock'; Start-Sleep 10 }
}
$rows = New-Object System.Collections.ArrayList
function Add-Row($build, $run, $scenario, $metric, $value) {
    [void]$rows.Add([pscustomobject]@{ build = $build; run = $run; scenario = $scenario; metric = $metric; value = [double]$value })
}

function Start-Bench([string]$exe, [string]$folder, [string]$json) {
    Remove-Item $json -ErrorAction SilentlyContinue
    $psi = New-Object System.Diagnostics.ProcessStartInfo $exe
    $psi.Arguments = "`"$folder`""
    $psi.UseShellExecute = $false
    $psi.EnvironmentVariables['FILESCOUT_BENCH_OUT'] = $json
    $psi.EnvironmentVariables['FILESCOUT_BENCH_PATH'] = $folder
    return [System.Diagnostics.Process]::Start($psi)
}
function Wait-Json([string]$json, $proc, [int]$timeoutSec) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $json) { Start-Sleep -Milliseconds 300; return Get-Content $json -Raw | ConvertFrom-Json }
        if ($proc.HasExited) { return $null }
        Start-Sleep -Milliseconds 250
    }
    return $null
}
function Stop-App { Get-Process FileScoutNative -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Seconds 2 }

try {
    Stop-App
    for ($run = 1; $run -le $Runs; $run++) {
        $order = 0..($Names.Count - 1)
        if ($run % 2 -eq 0) { [array]::Reverse($order) }
        foreach ($i in $order) {
            $name = $Names[$i]; $exe = $Exes[$i]

            # load100k
            $json = Join-Path $Out "$name-run$run-load.json"
            $p = Start-Bench $exe $Target $json
            $r = Wait-Json $json $p 240
            if ($r) {
                foreach ($m in $r.marks.PSObject.Properties) { Add-Row $name $run 'load100k' $m.Name $m.Value }
                foreach ($k in 'frame_p50_ms', 'frame_p95_ms', 'frame_p99_ms', 'frame_p100_ms', 'frames_over_33ms', 'items') {
                    Add-Row $name $run 'load100k' $k $r.$k
                }
                $p.Refresh()
                Add-Row $name $run 'load100k' 'private_mb_after_scroll' ($p.PrivateMemorySize64 / 1MB)
                Start-Sleep -Seconds 3
                $p.Refresh(); $cpu0 = $p.TotalProcessorTime.TotalMilliseconds; $t0 = Get-Date
                Start-Sleep -Seconds $IdleSeconds
                $p.Refresh(); $cpu1 = $p.TotalProcessorTime.TotalMilliseconds; $t1 = Get-Date
                Add-Row $name $run 'load100k' 'idle_cpu_pct_one_core' (100.0 * ($cpu1 - $cpu0) / ($t1 - $t0).TotalMilliseconds)
                Add-Row $name $run 'load100k' 'private_mb_idle' ($p.PrivateMemorySize64 / 1MB)
            }
            else { Write-Output "[warn] $name run $run load100k produced no report (exited=$($p.HasExited))" }
            Stop-App

            # coldstart (small folder)
            $json = Join-Path $Out "$name-run$run-cold.json"
            $p = Start-Bench $exe $SmallTarget $json
            $r = Wait-Json $json $p 120
            if ($r) { foreach ($m in $r.marks.PSObject.Properties) { Add-Row $name $run 'coldstart' $m.Name $m.Value } }
            else { Write-Output "[warn] $name run $run coldstart produced no report" }
            $p.Refresh()
            Add-Row $name $run 'coldstart' 'private_mb' ($p.PrivateMemorySize64 / 1MB)
            Stop-App
            Write-Output ("[run {0}] {1} done" -f $run, $name)
        }
    }
}
finally {
    $lock.Dispose()
    Stop-App
}
$rows | Export-Csv (Join-Path $Out 'results.csv') -NoTypeInformation
$rows | Group-Object scenario, metric, build | ForEach-Object {
    $v = $_.Group.value | Sort-Object
    $median = if ($v.Count % 2) { $v[[int][math]::Floor($v.Count / 2)] } else { ($v[$v.Count / 2 - 1] + $v[$v.Count / 2]) / 2 }
    [pscustomobject]@{ scenario = $_.Group[0].scenario; metric = $_.Group[0].metric; build = $_.Group[0].build
        median = [math]::Round($median, 1); min = [math]::Round($v[0], 1); max = [math]::Round($v[-1], 1); n = $v.Count }
} | Sort-Object scenario, metric, build | Format-Table -AutoSize | Out-String -Width 200

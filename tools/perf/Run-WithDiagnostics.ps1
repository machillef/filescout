<#
.SYNOPSIS
  Launch FileScout (Release x64 + NavDiag + PDB symbols) with full diagnostics.
.EXAMPLE
  .\Run-WithDiagnostics.ps1
  .\Run-WithDiagnostics.ps1 -StartPath "$env:USERPROFILE\Downloads"
  .\Run-WithDiagnostics.ps1 -WithEtw   # Run PowerShell as Administrator to also capture WPR ETW (.etl)
#>
param(
    [string] $StartPath = '',
    [switch] $WithEtw
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe  = Join-Path $here 'FileScoutNative.exe'
$logs = Join-Path $here 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null

$ts = Get-Date -Format 'yyyyMMdd-HHmmss'
$navOut   = Join-Path $logs "navdiag-$ts.jsonl"
$traceOut = Join-Path $logs "trace-$ts.log"
$etlOut   = Join-Path $logs "filescout-$ts.etl"
$tempTrace = Join-Path $env:TEMP 'FileScout.Native.trace.log'
$traceOffset = if (Test-Path $tempTrace) { (Get-Item $tempTrace).Length } else { 0 }

$etwStarted = $false
if ($WithEtw) {
    Write-Host "[diag] Starting WPR ETW capture (GeneralProfile + CPU + DiskIO)..."
    & wpr.exe -start GeneralProfile -start CPU -start DiskIO -filemode
    if ($LASTEXITCODE -eq 0) { $etwStarted = $true }
    else { Write-Warning "wpr.exe failed (run PowerShell as Administrator for ETW). Continuing with NavDiag + TraceLog." }
}

try {
    $psi = New-Object System.Diagnostics.ProcessStartInfo $exe
    if ($StartPath) {
        $escapedStartPath = ($StartPath -replace '(\\+)(?=$|")', '$1$1') -replace '"', '\"'
        $psi.Arguments = "`"$escapedStartPath`""
    }
    $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $here
    $psi.EnvironmentVariables['FILESCOUT_NAVDIAG_OUT'] = $navOut

    Write-Host "[diag] Launching FileScoutNative.exe (NavDiag -> $navOut)"
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.WaitForExit()
}
finally {
    if ($etwStarted) {
        Write-Host "[diag] Stopping WPR ETW capture -> $etlOut ..."
        & wpr.exe -stop $etlOut "FileScout sidebar nav diagnostic"
    }
    if (Test-Path $tempTrace) {
        $fs = [IO.File]::OpenRead($tempTrace)
        try {
            if ($traceOffset -gt 0 -and $fs.Length -ge $traceOffset) { $fs.Seek($traceOffset, [IO.SeekOrigin]::Begin) | Out-Null }
            $outFs = [IO.File]::Create($traceOut)
            try { $fs.CopyTo($outFs) } finally { $outFs.Dispose() }
        } finally { $fs.Dispose() }
    }
}

Write-Host ""
Write-Host "=== Diagnostics saved ==="
if (Test-Path $navOut)   { Write-Host "  NavDiag JSONL : $navOut" }
if (Test-Path $traceOut) { Write-Host "  TraceLog      : $traceOut" }
if (Test-Path $etlOut)   { Write-Host "  ETW Trace     : $etlOut (open with WPA; PDB is next to exe)" }

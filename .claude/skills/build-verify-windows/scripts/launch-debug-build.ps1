param(
    [switch]$Wait
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$exe = Join-Path $repoRoot "native\x64\Debug\FileScout.Native\FileScoutNative.exe"
$startupLog = Join-Path $env:TEMP "FileScout.Native.startup.log"
$traceLog = Join-Path $env:TEMP "FileScout.Native.trace.log"

if (-not (Test-Path $exe)) {
    throw "Built app not found: $exe. Run restore-build-debug.ps1 first."
}

$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
Write-Host "Started FileScoutNative.exe (PID $($process.Id))"
Write-Host "Startup log: $startupLog"
Write-Host "Trace log: $traceLog"

if ($Wait) {
    $process.WaitForExit()
    Write-Host "Process exited with code $($process.ExitCode)"
}

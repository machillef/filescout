param(
    [int]$Tail = 40
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$startupLog = Join-Path $env:TEMP "FileScout.Native.startup.log"
$traceLog = Join-Path $env:TEMP "FileScout.Native.trace.log"
$artifact = Join-Path $repoRoot "native\x64\Debug\FileScout.Native\FileScoutNative.exe"

Push-Location $repoRoot
try {
    Write-Host "== Repo root =="
    Write-Host $repoRoot

    Write-Host ""
    Write-Host "== Git status =="
    git status --short

    Write-Host ""
    Write-Host "== Built artifact =="
    if (Test-Path $artifact) {
        Get-Item $artifact | Select-Object FullName, LastWriteTime, Length | Format-List
    }
    else {
        Write-Host "Missing: $artifact"
    }

    foreach ($log in @($startupLog, $traceLog)) {
        Write-Host ""
        Write-Host "== $log =="
        if (Test-Path $log) {
            Get-Item $log | Select-Object FullName, LastWriteTime, Length | Format-List
            Get-Content $log -Tail $Tail
        }
        else {
            Write-Host "Not found."
        }
    }
}
finally {
    Pop-Location
}

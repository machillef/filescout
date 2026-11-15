<#
.SYNOPSIS
  Run a PowerShell command inside the interactive console session of the GUI test user.

.DESCRIPTION
  SSH and WinRM land in session 0, where WinUI windows never reach the screen. This registers a
  one-shot scheduled task with an Interactive logon for the test user (no password needed; the task
  only runs while that user is logged on, which autologon guarantees on win1), runs it, waits, and
  prints the command's output. A lock file serialises GUI runs between agents.
  Install: copy to C:\fsdev\tools\ on win1 (tools/win1/win1.sh gui expects it there).
#>
param(
    [Parameter(Mandatory)] [string] $EncodedCommand,
    [int] $TimeoutSeconds = 600,
    [string] $User = 'runetest'
)
$ErrorActionPreference = 'Stop'
$lock = 'C:\fsdev\gui.lock'
$busy = Get-ScheduledTask -TaskPath '\' | Where-Object {
    $_.TaskName -like 'HermesRune*' -and $_.TaskName -ne 'HermesRune-DesktopGuard' -and $_.State -eq 'Running' }
if ($busy) { Write-Error "Hermes GUI task running: $($busy.TaskName -join ',')"; exit 3 }
if (Test-Path $lock) {
    $age = (Get-Date) - (Get-Item $lock).LastWriteTime
    if ($age.TotalMinutes -lt 30) { Write-Error "gui.lock held: $(Get-Content $lock)"; exit 4 }
}
"$PID $(Get-Date -Format o)" | Set-Content $lock

$out = 'C:\fsdev\gui-out'
New-Item -ItemType Directory -Force $out | Out-Null
$id = [guid]::NewGuid().ToString('N').Substring(0, 8)
$script = Join-Path $out "run-$id.ps1"
$log = Join-Path $out "run-$id.log"
$command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($EncodedCommand))
@"
`$ErrorActionPreference = 'Continue'
& { $command } *>&1 | Out-File -FilePath '$log' -Encoding utf8 -Width 400
"@ | Set-Content -Path $script -Encoding utf8
icacls $out /grant "${User}:(OI)(CI)M" /T /Q | Out-Null

$taskName = "FileScout-Session1-$id"
try {
    $pwsh = (Get-Command pwsh).Source
    $action = New-ScheduledTaskAction -Execute $pwsh -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$script`""
    $principal = New-ScheduledTaskPrincipal -UserId $User -LogonType Interactive -RunLevel Limited
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -ExecutionTimeLimit (New-TimeSpan -Seconds ($TimeoutSeconds + 60))
    Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings | Out-Null
    Start-ScheduledTask -TaskName $taskName
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    Start-Sleep -Milliseconds 500
    while ((Get-ScheduledTask -TaskName $taskName).State -eq 'Running' -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }
    $state = (Get-ScheduledTask -TaskName $taskName).State
    if ($state -eq 'Running') { Stop-ScheduledTask -TaskName $taskName; Write-Warning "timed out after $TimeoutSeconds s" }
    $info = Get-ScheduledTaskInfo -TaskName $taskName
    if (Test-Path $log) { Get-Content $log }
    "[session1] task result 0x{0:X}" -f $info.LastTaskResult
}
finally {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Remove-Item $lock -ErrorAction SilentlyContinue
}

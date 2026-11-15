<#
.SYNOPSIS
  Run a long PowerShell command on win1 outside the SSH session and wait for it.

.DESCRIPTION
  Windows OpenSSH puts every child of a session in a job object that dies with the session, and
  long builds over one SSH connection were cut without an error. This runs the command as a
  one-shot scheduled task (the calling admin via S4U, session 0), so it survives, and writes its console output and
  exit code next to each other. tools/win1/win1.sh starts it and polls the .exit file.
  Install: copy to C:\fsdev\tools\ on win1.
#>
param(
    [Parameter(Mandatory)] [string] $EncodedCommand,
    [Parameter(Mandatory)] [string] $LogPath
)
$ErrorActionPreference = 'Stop'
Remove-Item $LogPath, "$LogPath.exit" -ErrorAction SilentlyContinue
$command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($EncodedCommand))
$script = "$LogPath.ps1"
@"
`$ErrorActionPreference = 'Continue'
`$code = 1
try { & { $command } *>&1 | Out-File -FilePath '$LogPath' -Encoding utf8 -Width 400; `$code = `$LASTEXITCODE }
finally { [int]`$code | Set-Content '$LogPath.exit'; Unregister-ScheduledTask -TaskName '$(Split-Path $LogPath -Leaf)' -Confirm:`$false -ErrorAction SilentlyContinue }
"@ | Set-Content -Path $script -Encoding utf8
$name = Split-Path $LogPath -Leaf
$pwsh = (Get-Command pwsh).Source
$action = New-ScheduledTaskAction -Execute $pwsh -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$script`""
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType S4U -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Hours 2)
Register-ScheduledTask -TaskName $name -Action $action -Principal $principal -Settings $settings -Force | Out-Null
Start-ScheduledTask -TaskName $name
"started $name"

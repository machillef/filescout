# Screenshots of the prototype looks (proto/looks build): current, a, b, c, same folder, maximised,
# one row selected with a real click.
param([string]$Exe, [string]$Folder = 'C:\Windows', [string]$Out = 'C:\fsdev\gui-out\looks')
Import-Module C:\fsdev\tools\Win1Ui.psm1 -DisableNameChecking
New-Item -ItemType Directory -Force $Out | Out-Null
Get-Process FileScoutNative -ErrorAction SilentlyContinue | Stop-Process -Force
foreach ($look in 'current', 'a', 'b', 'c') {
    $psi = New-Object System.Diagnostics.ProcessStartInfo $Exe
    $psi.Arguments = "`"$Folder`""
    $psi.UseShellExecute = $false
    $psi.EnvironmentVariables['FILESCOUT_LOOK'] = $(if ($look -eq 'current') { '' } else { $look })
    $p = [System.Diagnostics.Process]::Start($psi)
    Start-Sleep 6
    $w = Wait-Window { param($x) $x.Current.ProcessId -eq $p.Id } 10000
    try { $w.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).SetWindowVisualState('Maximized') } catch { }
    Start-Sleep 3
    Invoke-Click 700 330
    Start-Sleep 2
    Save-Screenshot (Join-Path $Out "look-$look.png")
    "shot $look"
    if (-not $p.HasExited) { $p.Kill() } else { "[warn] $look exited early" }; Start-Sleep 2
}

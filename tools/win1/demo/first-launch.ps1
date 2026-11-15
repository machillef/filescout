Import-Module C:\fsdev\tools\Win1Ui.psm1
$p = Start-Process C:\FileScout\FileScoutNative.exe -PassThru
Start-Sleep 8
Save-Screenshot C:\fsdev\gui-out\first-launch.png
"exited=$($p.HasExited)"
[void]$p.CloseMainWindow()

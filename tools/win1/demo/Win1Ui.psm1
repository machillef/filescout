# UI helpers for scripted runs in the interactive session (Windows PowerShell 5.1: UI Automation
# client assemblies are .NET Framework only). Real input via SendInput-style user32 calls.
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W1 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(int f, int dx, int dy, int d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte v, byte s, int f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[void][W1]::SetProcessDPIAware()
$script:AE = [System.Windows.Automation.AutomationElement]

function Invoke-Click([double]$x, [double]$y, [switch]$Double, [switch]$Right) {
    [void][W1]::SetCursorPos([int]$x, [int]$y); Start-Sleep -Milliseconds 250
    $down = if ($Right) { 0x08 } else { 0x02 }; $up = if ($Right) { 0x10 } else { 0x04 }
    $n = if ($Double) { 2 } else { 1 }
    for ($i = 0; $i -lt $n; $i++) { [W1]::mouse_event($down, 0, 0, 0, 0); [W1]::mouse_event($up, 0, 0, 0, 0); Start-Sleep -Milliseconds 60 }
}
function Click-Element($el, [switch]$Double, [switch]$Right) {
    $r = $el.Current.BoundingRectangle
    Invoke-Click ($r.X + $r.Width / 2) ($r.Y + $r.Height / 2) -Double:$Double -Right:$Right
}
function Send-Keys([byte[]]$vks) {
    foreach ($v in $vks) { [W1]::keybd_event($v, 0, 0, 0) }
    [array]::Reverse($vks); foreach ($v in $vks) { [W1]::keybd_event($v, 0, 2, 0) }
    Start-Sleep -Milliseconds 150
}
function Send-Text([string]$text) { [System.Windows.Forms.SendKeys]::SendWait($text) }
function Find-Element($root, [string]$name, [string]$automationId, [int]$timeoutMs = 8000) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    $cond = if ($automationId) {
        New-Object System.Windows.Automation.PropertyCondition($script:AE::AutomationIdProperty, $automationId)
    } else {
        New-Object System.Windows.Automation.PropertyCondition($script:AE::NameProperty, $name)
    }
    while ((Get-Date) -lt $deadline) {
        $el = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($el) { return $el }
        Start-Sleep -Milliseconds 300
    }
    return $null
}
function Get-TopWindows { $script:AE::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition) }
function Wait-Window([scriptblock]$match, [int]$timeoutMs = 15000) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    while ((Get-Date) -lt $deadline) {
        foreach ($w in Get-TopWindows) { if (& $match $w) { return $w } }
        Start-Sleep -Milliseconds 300
    }
    return $null
}
function Save-Screenshot([string]$path) {
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
}
function Start-Recording([string]$path, [int]$fps = 10) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo 'C:\fsdev\tools\ffmpeg\ffmpeg.exe'
    $psi.Arguments = "-y -loglevel error -f gdigrab -framerate $fps -draw_mouse 1 -i desktop -fps_mode cfr -r $fps -c:v libx264 -preset veryfast -crf 30 -pix_fmt yuv420p `"$path`""
    $psi.UseShellExecute = $false; $psi.RedirectStandardInput = $true; $psi.CreateNoWindow = $true
    return [System.Diagnostics.Process]::Start($psi)
}
function Stop-Recording($proc) { $proc.StandardInput.Write('q'); $proc.StandardInput.Close(); [void]$proc.WaitForExit(20000) }

# A small always-on-top caption at the bottom of the screen, so the video explains each step.
function Show-Caption([string]$text) {
    if (-not $script:CaptionForm) {
        $f = New-Object System.Windows.Forms.Form
        $f.FormBorderStyle = 'None'; $f.TopMost = $true; $f.ShowInTaskbar = $false; $f.StartPosition = 'Manual'
        $f.BackColor = [System.Drawing.Color]::FromArgb(20, 20, 20); $f.Opacity = 0.9
        $s = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $f.Bounds = New-Object System.Drawing.Rectangle 0, ($s.Height - 92), $s.Width, 44
        $l = New-Object System.Windows.Forms.Label
        $l.Dock = 'Fill'; $l.ForeColor = [System.Drawing.Color]::White; $l.TextAlign = 'MiddleCenter'
        $l.Font = New-Object System.Drawing.Font('Segoe UI', 14, [System.Drawing.FontStyle]::Bold)
        $f.Controls.Add($l); $f.Show(); $script:CaptionForm = $f; $script:CaptionLabel = $l
    }
    $script:CaptionLabel.Text = $text; $script:CaptionForm.Refresh(); [System.Windows.Forms.Application]::DoEvents()
    Write-Output "[caption] $text"
}
function Close-Caption { if ($script:CaptionForm) { $script:CaptionForm.Close(); $script:CaptionForm = $null } }
function Pause-Ui([int]$ms) { $end = (Get-Date).AddMilliseconds($ms); while ((Get-Date) -lt $end) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 50 } }

Export-ModuleMember -Function *

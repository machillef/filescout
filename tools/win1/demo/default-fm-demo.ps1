# Recorded end-to-end demo of "FileScout as the default file manager" on win1.
# Runs in runetest's console session (tools/win1/win1.sh gui), Windows PowerShell 5.1.
# Output: C:\fsdev\gui-out\default-fm-demo.mp4, default-fm-demo.json (per-step evidence),
# registry exports before/after.
param(
    [string] $Exe = 'C:\FileScout\FileScoutNative.exe',
    [string] $DownloadUrl = 'http://192.168.1.20:18765/filescout-demo-download.bin',
    [string] $Out = 'C:\fsdev\gui-out'
)
$ErrorActionPreference = 'Stop'
Import-Module C:\fsdev\tools\Win1Ui.psm1 -DisableNameChecking
$AE = [System.Windows.Automation.AutomationElement]
$steps = New-Object System.Collections.ArrayList
$touched = @('HKCU\Software\Classes\Directory', 'HKCU\Software\Classes\Drive',
    'HKCU\Software\Classes\Folder', 'HKCU\Software\Classes\CLSID\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}', 'HKCU\Software\FileScout')

function Export-Touched([string]$tag) {
    $text = foreach ($k in $touched) {
        "### $k"
        $tmp = Join-Path $env:TEMP "fsdemo-$tag.reg"
        # reg export returns 1 for any failure, so check absence separately and stop on real errors.
        cmd.exe /c "reg query `"$k`" >nul 2>&1"
        if ($LASTEXITCODE -ne 0) { '(absent)'; continue }
        $err = cmd.exe /c "reg export `"$k`" `"$tmp`" /y 2>&1"
        if ($LASTEXITCODE -ne 0) { throw "reg export failed for ${k}: $err" }
        Get-Content $tmp | Select-Object -Skip 1
    }
    $path = Join-Path $Out "registry-$tag.txt"
    $text | Set-Content $path -Encoding UTF8
    return (Get-FileHash $path -Algorithm SHA256).Hash
}
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public static class FW {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string w);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr w, EnumProc p, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  public static IntPtr FindDesktopListView() {
    IntPtr prog = FindWindowEx(IntPtr.Zero, IntPtr.Zero, "Progman", null), found = IntPtr.Zero;
    EnumChildWindows(prog, (h, l) => { var sb = new StringBuilder(64); GetClassName(h, sb, 64);
      if (sb.ToString() == "SysListView32") { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
  // The desktop list view does not expose its items to UI Automation here, so read the item text
  // and position with LVM_* messages through a buffer inside explorer's address space.
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, int s, uint t, uint pr);
  [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr p, IntPtr a, int s, uint t);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] b, int s, out IntPtr w);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int s, out IntPtr r);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  public struct POINT { public int X, Y; }
  public static int[] FindDesktopIcon(string name) {
    IntPtr lv = FindDesktopListView(); if (lv == IntPtr.Zero) return null;
    uint pid; GetWindowThreadProcessId(lv, out pid);
    IntPtr proc = OpenProcess(0x0008 | 0x0010 | 0x0020 | 0x0400, false, pid);
    IntPtr mem = VirtualAllocEx(proc, IntPtr.Zero, 4096, 0x3000, 0x04);
    try {
      int count = (int)SendMessage(lv, 0x1004, IntPtr.Zero, IntPtr.Zero);
      for (int i = 0; i < count; i++) {
        // LVITEMW (x64): mask@0, iItem@4, iSubItem@8, state@12, stateMask@16, pszText@24, cchTextMax@32
        byte[] item = new byte[88];
        BitConverter.GetBytes(0x1).CopyTo(item, 0);
        BitConverter.GetBytes(i).CopyTo(item, 4);
        BitConverter.GetBytes((long)mem + 512).CopyTo(item, 24);
        BitConverter.GetBytes(256).CopyTo(item, 32);
        IntPtr n; WriteProcessMemory(proc, mem, item, item.Length, out n);
        SendMessage(lv, 0x1073, (IntPtr)i, mem);
        byte[] text = new byte[512]; ReadProcessMemory(proc, mem + 512, text, 512, out n);
        string t = Encoding.Unicode.GetString(text); t = t.Substring(0, Math.Max(0, t.IndexOf('\0')));
        if (t != name) continue;
        SendMessage(lv, 0x1010, (IntPtr)i, mem + 1024);
        byte[] pt = new byte[8]; ReadProcessMemory(proc, mem + 1024, pt, 8, out n);
        POINT p = new POINT { X = BitConverter.ToInt32(pt, 0) + 37, Y = BitConverter.ToInt32(pt, 4) + 24 };
        ClientToScreen(lv, ref p);
        return new int[] { p.X, p.Y };
      }
      return null;
    } finally { VirtualFreeEx(proc, mem, 0, 0x8000); CloseHandle(proc); }
  }
}
"@
function ProcName($w) { try { (Get-Process -Id $w.Current.ProcessId).ProcessName } catch { '?' } }
function Snapshot { @(Get-TopWindows | ForEach-Object { $_.Current.NativeWindowHandle }) }
function Wait-NewFolderWindow($before, [int]$timeoutMs = 20000) {
    Wait-Window -timeoutMs $timeoutMs {
        param($w)
        $h = $w.Current.NativeWindowHandle
        ($before -notcontains $h) -and ($w.Current.ClassName -eq 'CabinetWClass' -or (ProcName $w) -eq 'FileScoutNative')
    }
}
function Record-Step([string]$name, $window, [string]$expect) {
    $who = if ($window) { if ($window.Current.ClassName -eq 'CabinetWClass') { 'Explorer' } else { 'FileScout' } } else { 'none' }
    $entry = [ordered]@{ step = $name; opened = $who; expected = $expect; ok = ($who -eq $expect);
        title = if ($window) { $window.Current.Name } else { '' }; time = (Get-Date -Format o) }
    [void]$steps.Add($entry)
    Write-Output ("[step] {0}: opened={1} expected={2} ok={3} title='{4}'" -f $name, $who, $expect, $entry.ok, $entry.title)
}
function Close-Win($w) {
    if (-not $w) { return }
    try { $w.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close() } catch { }
    Pause-Ui 800
}
function Maximize($w) {
    try { $w.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).SetWindowVisualState('Maximized') } catch { }
    Pause-Ui 600
}
function Use-FileScoutMenu([string]$caption, [string]$expectText) {
    $before = Snapshot
    Start-Process $Exe | Out-Null
    $w = Wait-NewFolderWindow $before
    Maximize $w
    Pause-Ui 1500
    Show-Caption $caption
    $theme = Find-Element $w -name 'Theme'
    Click-Element $theme
    $item = Find-Element $AE::RootElement -automationId 'DefaultFileManagerMenuItem'
    if (-not $item) { throw 'menu item not found' }
    Write-Output "[menu] '$($item.Current.Name)'"
    if ($item.Current.Name -notlike "*$expectText*") { throw "unexpected menu text '$($item.Current.Name)'" }
    Pause-Ui 1200
    Click-Element $item
    Pause-Ui 2500
    Close-Win $w
}

# Clean stage: no leftover windows, a folder on the desktop to double-click.
Get-Process FileScoutNative -ErrorAction SilentlyContinue | Stop-Process -Force
(New-Object -ComObject Shell.Application).Windows() | ForEach-Object { $_.Quit() }
$desktopFolder = Join-Path ([Environment]::GetFolderPath('Desktop')) 'FileScout Demo'
New-Item -ItemType Directory -Force $desktopFolder | Out-Null
Remove-Item (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Downloads\filescout-demo-download*') -ErrorAction SilentlyContinue
$hashBefore = Export-Touched 'before'

$rec = Start-Recording (Join-Path $Out 'default-fm-demo.mp4')
$enabledHere = $false
try {
Pause-Ui 1000
Show-Caption 'FileScout as the default file manager  -  win1, user runetest'
Pause-Ui 2500

# 1. Before
Show-Caption '1. Before: Win+E opens Explorer'
$before = Snapshot; Send-Keys 0x5B, 0x45
$w = Wait-NewFolderWindow $before; Record-Step 'Win+E before' $w 'Explorer'; Pause-Ui 2500; Close-Win $w

# 2. One action inside FileScout
Use-FileScoutMenu '2. In FileScout: Theme menu > "Make FileScout the default file manager"' 'Make FileScout'
$enabledHere = $true

# 3. Win+E
Show-Caption '3. Win+E now opens FileScout'
$before = Snapshot; Send-Keys 0x5B, 0x45
$w = Wait-NewFolderWindow $before; Record-Step 'Win+E' $w 'FileScout'; Pause-Ui 3000; Close-Win $w

# 4. Desktop double-click
Show-Caption '4. Double-click a folder on the desktop'
Send-Keys 0x5B, 0x44; Pause-Ui 1500
$icon = [FW]::FindDesktopIcon('FileScout Demo')
$before = Snapshot
if ($icon) { Write-Output "[desktop] icon at $($icon -join ',')"; Invoke-Click $icon[0] $icon[1] -Double } else { Write-Output '[warn] desktop icon not found' }
$w = Wait-NewFolderWindow $before; Record-Step 'Desktop folder double-click' $w 'FileScout'; Pause-Ui 3000; Close-Win $w
Send-Keys 0x5B, 0x44; Pause-Ui 800

# 5. Another app: the Run dialog (ShellExecute on a folder path)
Show-Caption '5. Another app opens a folder: Run dialog, C:\Windows\Web'
$before = Snapshot
Send-Keys 0x5B, 0x52; Pause-Ui 1500; Send-Text 'C:\Windows\Web'; Pause-Ui 1200; Send-Text '{ENTER}'
$w = Wait-NewFolderWindow $before; Record-Step 'Run dialog folder' $w 'FileScout'; Pause-Ui 3000; Close-Win $w

# 6. Browser download > Show in folder
Show-Caption '6. Browser: download a file, then "Open downloads folder"'
$edge = Start-Process 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe' -PassThru -ArgumentList @(
    '--no-first-run', '--no-default-browser-check', '--guest', '--start-maximized', $DownloadUrl)
$before = Snapshot
$clicked = 'none'
$open = Find-Element $AE::RootElement -name 'Open downloads folder' -timeoutMs 30000
if ($open) {
    Pause-Ui 1500
    # Hovering the download row reveals its own "Show in folder" button.
    $r = $open.Current.BoundingRectangle
    [void][W1]::SetCursorPos([int]($r.X - 40), [int]($r.Y + 60)); Pause-Ui 1500
    $show = Find-Element $AE::RootElement -name 'Show in folder' -timeoutMs 4000
    if ($show) { $clicked = 'Show in folder'; Click-Element $show }
    else { $clicked = 'Open downloads folder'; Click-Element $open }
}
Write-Output "[edge] clicked '$clicked'"
$w = Wait-NewFolderWindow $before; Record-Step "Edge download: $clicked" $w 'FileScout'; Pause-Ui 3500; Close-Win $w
Get-Process msedge -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $edge.SessionId } | Stop-Process -Force
Pause-Ui 1000

# 7. Restore from inside FileScout
Use-FileScoutMenu '7. In FileScout: Theme menu > "Restore Explorer as default file manager"' 'Restore Explorer'
$enabledHere = $false

# 8. After restore
Show-Caption '8. After restore: Win+E opens Explorer again'
$before = Snapshot; Send-Keys 0x5B, 0x45
$w = Wait-NewFolderWindow $before; Record-Step 'Win+E after restore' $w 'Explorer'; Pause-Ui 2500; Close-Win $w
$before = Snapshot; Start-Process $desktopFolder
$w = Wait-NewFolderWindow $before; Record-Step 'Folder open after restore' $w 'Explorer'; Pause-Ui 2000; Close-Win $w

# 9. No traces
$hashAfter = Export-Touched 'after'
$same = $hashBefore -eq $hashAfter
Show-Caption ("9. Registry keys FileScout touches, before vs after: {0} (SHA-256 {1}...)" -f ($(if ($same) { 'IDENTICAL' } else { 'DIFFERENT' })), $hashAfter.Substring(0, 12))
Pause-Ui 5000
}
finally {
    # A failed step must not leave FileScout registered or the recorder running.
    if ($enabledHere) {
        try {
            $p = Start-Process $Exe -ArgumentList '--restore-explorer' -Wait -PassThru
            if ($p.ExitCode -ne 0) { Write-Output "[error] --restore-explorer exited with $($p.ExitCode)" }
        }
        catch { Write-Output "[error] restore failed: $_" }
    }
    try { Close-Caption } catch { }
    try { Stop-Recording $rec } catch { Write-Output "[error] recorder stop failed: $_" }
}

[ordered]@{ steps = $steps; registryBefore = $hashBefore; registryAfter = $hashAfter; registryIdentical = $same } |
    ConvertTo-Json -Depth 4 | Set-Content (Join-Path $Out 'default-fm-demo.json') -Encoding UTF8
"registry identical: $same"

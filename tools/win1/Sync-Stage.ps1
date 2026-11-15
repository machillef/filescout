<#
.SYNOPSIS
  Mirror a staged checkout into a win1 worktree by content, so MSBuild sees exactly the edited files.

.DESCRIPTION
  tar keeps the Linux mtimes. Copying them as they are let a file edited before the last win1 build
  look older than its .obj, so MSBuild skipped it; stamping every copied file with "now" made the next
  sync see a time mismatch for the same files again. Rule used here:
    - content differs (or file is new): copy and stamp with now  -> MSBuild rebuilds it
    - content equal: nothing is touched (the destination time is never moved backwards, which could
      hide a copied-but-not-yet-built edit from an older .obj)
  Destination files that no longer exist in the stage are deleted (mirror), except build outputs.
#>
param([Parameter(Mandatory)] [string] $Stage, [Parameter(Mandatory)] [string] $Dest)
$ErrorActionPreference = 'Stop'
$excludeDirs = @('packages', 'bin', 'obj', 'x64', 'Generated Files', '.vs')
$stageRoot = (Resolve-Path -LiteralPath $Stage).Path.TrimEnd('\')
New-Item -ItemType Directory -Force $Dest | Out-Null
$destRoot = (Resolve-Path -LiteralPath $Dest).Path.TrimEnd('\')
$now = Get-Date
$copied = 0; $removed = 0

function Test-Excluded([string]$relative) {
    foreach ($part in $relative.Split('\')) { if ($excludeDirs -contains $part) { return $true } }
    return $false
}

$staged = @{}
foreach ($file in Get-ChildItem -LiteralPath $stageRoot -Recurse -File -Force) {
    $relative = $file.FullName.Substring($stageRoot.Length + 1)
    $staged[$relative] = $true
    $target = Join-Path $destRoot $relative
    # A directory where the stage now has a file (or a file where it now has a directory) is replaced.
    if (Test-Path -LiteralPath $target -PathType Container) { Remove-Item -LiteralPath $target -Recurse -Force }
    $parent = Split-Path $target
    while ($parent.Length -gt $destRoot.Length) {
        if (Test-Path -LiteralPath $parent -PathType Leaf) { Remove-Item -LiteralPath $parent -Force; break }
        $parent = Split-Path $parent
    }
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        $existing = Get-Item -LiteralPath $target -Force
        # Always compare content when the sizes match: equal size and time do not prove equal content
        # (an edit that keeps the size and mtime, or a copy whose re-timestamp failed last time).
        if ($existing.Length -eq $file.Length -and
            (Get-FileHash -LiteralPath $target).Hash -eq (Get-FileHash -LiteralPath $file.FullName).Hash) {
            # Same content: leave the destination time alone. Moving it back to the (older) Linux
            # time could hide an edit that was copied but not yet built from an older .obj.
            continue
        }
    }
    else {
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    }
    Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    (Get-Item -LiteralPath $target -Force).LastWriteTime = $now
    $copied++
}

# Mirror deletions without descending into build outputs or the packages junction.
$queue = New-Object System.Collections.Generic.Queue[string]
$queue.Enqueue($destRoot)
while ($queue.Count -gt 0) {
    $dir = $queue.Dequeue()
    foreach ($entry in Get-ChildItem -LiteralPath $dir -Force) {
        $relative = $entry.FullName.Substring($destRoot.Length + 1)
        if ($entry.PSIsContainer) {
            if ($excludeDirs -contains $entry.Name -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
            $queue.Enqueue($entry.FullName)
        }
        elseif (-not (Test-Excluded $relative) -and -not $staged.ContainsKey($relative) -and $entry.Name -notlike 'build-*.log') {
            Remove-Item -LiteralPath $entry.FullName -Force
            $removed++
        }
    }
}
"synced $destRoot (copied $copied, removed $removed)"

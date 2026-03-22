$ErrorActionPreference = "Stop"

$root = Join-Path $env:TEMP ("FileScout.UiSandbox." + (Get-Date -Format "yyyyMMdd-HHmmss"))

New-Item -ItemType Directory -Path $root | Out-Null
New-Item -ItemType Directory -Path (Join-Path $root "PaneA") | Out-Null
New-Item -ItemType Directory -Path (Join-Path $root "PaneB") | Out-Null
New-Item -ItemType Directory -Path (Join-Path $root "PaneB\\DropTarget") | Out-Null

Set-Content -Path (Join-Path $root "readme.txt") -Value "FileScout UI sandbox"
Set-Content -Path (Join-Path $root "PaneA\\alpha.txt") -Value "alpha"
Set-Content -Path (Join-Path $root "PaneA\\beta.txt") -Value "beta"
Set-Content -Path (Join-Path $root "PaneB\\gamma.txt") -Value "gamma"

Write-Host "Created UI sandbox:"
Write-Host $root

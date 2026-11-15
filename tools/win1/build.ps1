<#
.SYNOPSIS
  Restore, build and (optionally) test a FileScout checkout on a Windows dev box.

.DESCRIPTION
  Used by tools/win1/win1.sh from ubuntu-containers and by hand on win1. NuGet packages are shared
  between checkouts through a junction (native\packages -> $PackagesCache) so each worktree does not
  download the Windows App SDK again.

.EXAMPLE
  pwsh -File tools\win1\build.ps1 -Root C:\fsdev\wt\master -Configuration Debug -Test
#>
param(
    [Parameter(Mandatory)] [string] $Root,
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [switch] $Test,
    [string] $TestFilter = '*',
    [string] $PackagesCache = 'C:\fsdev\packages',
    [string] $Target = 'Build'
)

$ErrorActionPreference = 'Stop'
$vsRoot = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsRoot) { throw 'No Visual Studio / Build Tools installation with MSBuild found.' }

$native = Join-Path $Root 'native'
$packages = Join-Path $native 'packages'
New-Item -ItemType Directory -Force $PackagesCache | Out-Null
if (-not (Test-Path $packages)) {
    New-Item -ItemType Junction -Path $packages -Target $PackagesCache | Out-Null
}

$sln = Join-Path $native 'FileScout.Native.sln'
$devCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
$log = Join-Path $Root "build-$Configuration.log"
$cmdLine = "call `"$devCmd`" -arch=x64 -no_logo && msbuild `"$sln`" /t:$Target /restore " +
    "/p:RestorePackagesConfig=true /p:Configuration=$Configuration /p:Platform=x64 /m /nologo /v:minimal " +
    "/flp:LogFile=`"$log`";Verbosity=normal"
$sw = [Diagnostics.Stopwatch]::StartNew()
& cmd.exe /d /c $cmdLine
$buildExit = $LASTEXITCODE
Write-Host ("[build] exit={0} in {1:n0}s, log {2}" -f $buildExit, $sw.Elapsed.TotalSeconds, $log)
if ($buildExit -ne 0) { exit $buildExit }

if ($Test) {
    $exe = Join-Path $native "bin\x64\$Configuration\Tests\FileScout.Tests.exe"
    & $exe --gtest_color=no "--gtest_filter=$TestFilter"
    $testExit = $LASTEXITCODE
    Write-Host "[test] exit=$testExit"
    exit $testExit
}

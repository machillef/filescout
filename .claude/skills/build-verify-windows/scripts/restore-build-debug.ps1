$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
$solution = "native\FileScout.Native.sln"
$artifact = Join-Path $repoRoot "native\x64\Debug\FileScout.Native\FileScoutNative.exe"

foreach ($path in @($vsDevCmd, $msbuild, (Join-Path $repoRoot $solution))) {
    if (-not (Test-Path $path)) {
        throw "Required path not found: $path"
    }
}

Push-Location $repoRoot
try {
    $command = "`"$vsDevCmd`" -arch=x64 && `"$msbuild`" $solution /restore /p:RestorePackagesConfig=true && `"$msbuild`" $solution /p:Configuration=Debug /p:Platform=x64"
    Write-Host "Running MSBuild restore + Debug x64 build..."
    cmd /c $command
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if (-not (Test-Path $artifact)) {
        throw "Build completed but expected artifact was not found: $artifact"
    }

    Write-Host "Built artifact: $artifact"
}
finally {
    Pop-Location
}

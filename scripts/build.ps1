# scripts/build.ps1
param(
    [string]$Config = "Release",
    [switch]$Test = $false,
    [switch]$Clean = $false
)

$ErrorActionPreference = "Stop"

$vsBuildTools = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$vsDevCmd = "$vsBuildTools\Common7\Tools\VsDevCmd.bat"

if (!(Test-Path $vsDevCmd)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $instPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($instPath) {
            $vsDevCmd = "$instPath\Common7\Tools\VsDevCmd.bat"
        }
    }
}

if (!(Test-Path $vsDevCmd)) {
    Write-Error "Could not find VsDevCmd.bat. Visual Studio C++ build tools required."
}

$buildDir = "build"
if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}

Write-Host "==> Building Axiom [$Config]..."

$cmd = "call `"$vsDevCmd`" -arch=x64 && cmake -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=$Config && cmake --build $buildDir --config $Config -- -j 4"
cmd.exe /c $cmd

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
}

Write-Host "==> Build completed successfully!"

if ($Test) {
    Write-Host "==> Running Axiom unit & integration tests..."
    $testCmd = "call `"$vsDevCmd`" -arch=x64 && ctest --test-dir $buildDir --output-on-failure"
    cmd.exe /c $testCmd
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Tests failed with exit code $LASTEXITCODE"
    }
}

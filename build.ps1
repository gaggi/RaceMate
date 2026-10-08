param([ValidateSet('x64','Win32')][string]$Architecture = 'x64', [switch]$Diagnostics, [switch]$NoPackage, [string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$buildRoot = if ($BuildDirectory) { $BuildDirectory } else { "build/$Architecture" }
if (-not [IO.Path]::IsPathRooted($buildRoot)) { $buildRoot = Join-Path $PSScriptRoot $buildRoot }
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source }
else {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Install Visual Studio Build Tools with Desktop development with C++ and CMake.' }
    $installation = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
function Invoke-BuildTool([string]$Tool, [string[]]$Arguments) {
    # Piping makes PowerShell wait for GUI programs such as the diagnostics, too.
    & $Tool @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Tool failed with exit code $LASTEXITCODE" }
}
Push-Location $PSScriptRoot
try {
    Invoke-BuildTool $cmakePath @('-S', '.', '-B', $buildRoot, '-G', 'Visual Studio 17 2022', '-A', $Architecture)
    Invoke-BuildTool $cmakePath @('--build', $buildRoot, '--config', 'Release')
    if ($Diagnostics) { Invoke-BuildTool $cmakePath @('--build', $buildRoot, '--config', 'Release', '--target', 'RaceMateDiagnostics') }
    $ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
    Invoke-BuildTool $ctestPath @('--test-dir', $buildRoot, '-C', 'Release', '--output-on-failure')
    if ($Diagnostics) { Invoke-BuildTool (Join-Path $buildRoot 'Release/RaceMateDiagnostics.exe') @('--diagnostics') }
    if ($NoPackage) { return }
    $outputPath = Join-Path $PSScriptRoot "dist/$Architecture"
    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
    $builtExe = Join-Path $buildRoot 'Release/RaceMate.exe'
    $releaseVersion = (Get-Item -LiteralPath $builtExe).VersionInfo.FileVersion
    # The updater picks the release asset ending in windows-x64.exe / windows-x86.exe.
    $platform = if ($Architecture -eq 'x64') { 'x64' } else { 'x86' }
    $versionedExe = Join-Path $outputPath "RaceMate-$releaseVersion-windows-$platform.exe"
    try { Copy-Item -LiteralPath $builtExe -Destination $versionedExe }
    catch [System.IO.IOException] {
        $versionedExe = Join-Path $outputPath ("RaceMate-{0}-windows-{1}-{2}.exe" -f $releaseVersion, $platform, (Get-Date -Format 'yyyyMMdd-HHmmss'))
        Copy-Item -LiteralPath $builtExe -Destination $versionedExe
        Write-Warning "The previous versioned executable is in use. A separate executable was created."
    }
    try { Copy-Item -LiteralPath $builtExe -Destination $outputPath }
    catch [System.IO.IOException] { Write-Warning "RaceMate.exe is in use. Close the running app and start $versionedExe." }
    Write-Host "RaceMate ready: $versionedExe"
}
finally { Pop-Location }

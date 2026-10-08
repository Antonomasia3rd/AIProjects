# Compatibility launcher. Release archives keep the executable beside this
# file; a source checkout builds it in the local build directory.
$ErrorActionPreference = 'Stop'
$implementation = Join-Path $PSScriptRoot 'ChromeProfileCounter.exe'
if (-not (Test-Path -LiteralPath $implementation -PathType Leaf)) {
    $implementation = Join-Path $PSScriptRoot 'build/ChromeProfileCounter.exe'
}
if (-not (Test-Path -LiteralPath $implementation -PathType Leaf)) {
    throw 'ChromeProfileCounter.exe is missing. Run BuildChromeProfileCounter.cmd or use the compiled release archive.'
}
$launcher = Join-Path $PSScriptRoot 'powershell_native_launcher.ps1'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    $launcher = Join-Path $PSScriptRoot 'build/powershell_native_launcher.ps1'
}
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    $launcher = Join-Path $PSScriptRoot '../../dependencies/powershell_native_launcher.ps1'
}
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw 'The optional PowerShell launcher helper is missing; run ChromeProfileCounter.exe directly.'
}
. $launcher
$nativeExitCode = Invoke-AipNativeExecutable -ExecutablePath $implementation -ArgumentList $args
exit $nativeExitCode

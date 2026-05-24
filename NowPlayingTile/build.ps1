$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
& $env:ComSpec /d /c "`"$root\build.cmd`""
exit $LASTEXITCODE

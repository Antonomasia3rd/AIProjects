$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'build\NowPlayingTile.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    & $env:ComSpec /d /c "`"$root\build.cmd`""
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Start-Process -FilePath $exe -ArgumentList '--widget', '--allow-multiple'

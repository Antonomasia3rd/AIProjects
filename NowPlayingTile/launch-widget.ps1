$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'build\NowPlayingTile.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    & (Join-Path $root 'build.ps1')
}

Start-Process -FilePath $exe -ArgumentList '--widget', '--allow-multiple'

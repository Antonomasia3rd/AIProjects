[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Invoke-SmokeProcess {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [string[]]$ArgumentList = @(),
        [int[]]$AllowedExitCodes = @(0),
        [int]$TimeoutSeconds = 30,
        [string]$Name = (Split-Path -Leaf $FilePath)
    )

    if (-not (Test-Path -LiteralPath $FilePath -PathType Leaf)) {
        Write-Host "skip - $Name not built: $FilePath"
        return
    }

    $outFile = Join-Path $env:RUNNER_TEMP ("smoke-$([IO.Path]::GetFileNameWithoutExtension($FilePath))-$([Guid]::NewGuid().ToString('N')).out")
    $errFile = Join-Path $env:RUNNER_TEMP ("smoke-$([IO.Path]::GetFileNameWithoutExtension($FilePath))-$([Guid]::NewGuid().ToString('N')).err")
    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -NoNewWindow -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile

    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $process.Kill() } catch { }
        throw "Smoke test timed out: $Name $($ArgumentList -join ' ')"
    }

    $stdout = if (Test-Path -LiteralPath $outFile) { Get-Content -LiteralPath $outFile -Raw } else { '' }
    $stderr = if (Test-Path -LiteralPath $errFile) { Get-Content -LiteralPath $errFile -Raw } else { '' }
    Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue

    if ($AllowedExitCodes -notcontains $process.ExitCode) {
        throw "Smoke test failed: $Name $($ArgumentList -join ' ') exited $($process.ExitCode).`nSTDOUT:`n$stdout`nSTDERR:`n$stderr"
    }

    Write-Host "ok - $Name $($ArgumentList -join ' ')"
}

function New-TestWallpaper {
    param([Parameter(Mandatory)] [string]$Path)

    Add-Type -AssemblyName System.Drawing
    $bitmap = New-Object System.Drawing.Bitmap 16, 16
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::FromArgb(20, 40, 80))
            $graphics.FillRectangle([System.Drawing.Brushes]::White, 2, 2, 12, 12)
        } finally {
            $graphics.Dispose()
        }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bitmap.Dispose()
    }
}

$projectMap = @(Get-Content -LiteralPath (Join-Path $RepositoryRoot '.github\project-map.json') -Raw | ConvertFrom-Json)
Write-Host "Running Windows build smoke tests..."

$desktopStub = $projectMap | Where-Object { $_.key -eq 'DesktopStub' } | Select-Object -First 1
if ($desktopStub) {
    $desktopStubSmokePath = $desktopStub.artifactPath
    if (($desktopStub.PSObject.Properties.Name -contains 'smokePath') -and -not [string]::IsNullOrWhiteSpace($desktopStub.smokePath)) {
        $desktopStubSmokePath = $desktopStub.smokePath
    }

    $exe = Join-Path $RepositoryRoot $desktopStubSmokePath
    if (Test-Path -LiteralPath $exe -PathType Leaf) {
        $tempRoot = Join-Path $env:RUNNER_TEMP ("DesktopStubSmoke-" + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $ini = Join-Path $tempRoot 'DesktopStub.ini'
        $wallpaper = Join-Path $tempRoot 'wallpaper.png'
        New-TestWallpaper -Path $wallpaper

        Invoke-SmokeProcess -FilePath $exe -ArgumentList @('--help') -Name 'DesktopStub help'
        Invoke-SmokeProcess -FilePath $exe -ArgumentList @('--ini', $ini, '--no-tray', '--console', '--logging', '--notifications', '--live-tile-mode', 'Auto', '--scales', 'auto', '--asset', 'MediumTile=1', '--regenerate-manifest') -Name 'DesktopStub settings and manifest'
        Invoke-SmokeProcess -FilePath $exe -ArgumentList @('--ini', $ini, '--wallpaper', $wallpaper, '--once', '--no-tray', '--no-monitor') -Name 'DesktopStub one-shot generation'
        Invoke-SmokeProcess -FilePath $exe -ArgumentList @('--ini', $ini, '--wallpaper', (Join-Path $tempRoot 'missing.png'), '--no-tray') -AllowedExitCodes @(2) -Name 'DesktopStub invalid wallpaper guard'

        if (-not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $exe) 'AppxManifest.xml') -PathType Leaf)) {
            throw 'DesktopStub smoke test did not create AppxManifest.xml.'
        }
    }
}

foreach ($project in $projectMap | Where-Object { $_.key -ne 'DesktopStub' }) {
    $path = Join-Path $RepositoryRoot $project.artifactPath
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $item = Get-Item -LiteralPath $path
        if ($item.Length -le 0) {
            throw "Smoke test failed: empty artifact $($project.artifactPath)"
        }
        Write-Host "ok - $($project.label) artifact exists ($($item.Length) bytes)"
    } else {
        Write-Host "skip - $($project.label) not built"
    }
}

Write-Host 'Windows build smoke tests completed.'

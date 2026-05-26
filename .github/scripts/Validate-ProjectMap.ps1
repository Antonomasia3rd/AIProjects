[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$mapPath = Join-Path $RepositoryRoot '.github\project-map.json'
$buildScriptPath = Join-Path $RepositoryRoot '.github\scripts\build-windows.cmd'
$readmePath = Join-Path $RepositoryRoot 'README.md'

if (-not (Test-Path -LiteralPath $mapPath -PathType Leaf)) {
    throw "Missing project map: $mapPath"
}
if (-not (Test-Path -LiteralPath $buildScriptPath -PathType Leaf)) {
    throw "Missing build script: $buildScriptPath"
}
if (-not (Test-Path -LiteralPath $readmePath -PathType Leaf)) {
    throw "Missing README: $readmePath"
}

$projects = @(Get-Content -LiteralPath $mapPath -Raw | ConvertFrom-Json)
if (-not $projects -or $projects.Count -eq 0) {
    throw 'Project map must contain at least one project.'
}

$requiredFields = @('key', 'label', 'folder', 'buildOutput', 'artifactName', 'artifactPath', 'skipKey')
$seen = @{}
$buildScript = Get-Content -LiteralPath $buildScriptPath -Raw
$readme = Get-Content -LiteralPath $readmePath -Raw

foreach ($project in $projects) {
    foreach ($field in $requiredFields) {
        $value = [string]$project.$field
        if ([string]::IsNullOrWhiteSpace($value)) {
            throw "Project map entry is missing '$field': $($project | ConvertTo-Json -Compress)"
        }
    }

    foreach ($uniqueField in @('key', 'buildOutput', 'artifactName', 'artifactPath', 'skipKey')) {
        $value = [string]$project.$uniqueField
        $uniqueKey = "$uniqueField=$value".ToLowerInvariant()
        if ($seen.ContainsKey($uniqueKey)) {
            throw "Duplicate project map value for ${uniqueField}: $value"
        }
        $seen[$uniqueKey] = $true
    }

    $folderPath = Join-Path $RepositoryRoot ([string]$project.folder)
    if (-not (Test-Path -LiteralPath $folderPath -PathType Container)) {
        throw "Project folder does not exist: $($project.folder)"
    }

    if ($buildScript -notmatch [regex]::Escape(":Build$($project.skipKey)") -and
        $buildScript -notmatch [regex]::Escape("call :IsSkipped $($project.skipKey)")) {
        throw "Build script does not appear to handle skip key: $($project.skipKey)"
    }

    $readmeFolderToken = '`' + [string]$project.folder + '`'
    if ($readme -notmatch [regex]::Escape($readmeFolderToken)) {
        throw "README project table does not appear to mention folder: $($project.folder)"
    }
}

Write-Host "Project map validation passed ($($projects.Count) projects)."

[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$projectMap = @(Get-Content -LiteralPath (Join-Path $RepositoryRoot '.github\project-map.json') -Raw | ConvertFrom-Json)
if (-not $projectMap) {
    throw 'Project map is empty.'
}

$projectKeys = @($projectMap | ForEach-Object { [string]$_.key })
$workflowPath = Join-Path $RepositoryRoot '.github\workflows\build-windows.yml'
$workflow = Get-Content -LiteralPath $workflowPath -Raw

foreach ($key in $projectKeys) {
    if ($workflow -notmatch "(?m)^\s{10}- $([regex]::Escape($key))\s*$") {
        throw "workflow_dispatch project selector is missing option: $key"
    }
}

foreach ($project in $projectMap) {
    $onlySelected = @($projectMap | Where-Object { $_.key -eq $project.key })
    if ($onlySelected.Count -ne 1) {
        throw "Project selector simulation failed for $($project.key)."
    }

    $skip = @()
    foreach ($candidate in $projectMap) {
        if ($candidate.key -ne $project.key) {
            $skip += "/skip:$($candidate.skipKey)"
        }
    }

    if ($skip -contains "/skip:$($project.skipKey)") {
        throw "Project selector would skip selected project: $($project.key)"
    }
}

Write-Host "Workflow project selector validation passed ($($projectMap.Count) projects plus All)."

[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Continue'
Set-StrictMode -Version 2.0

$projectMap = @(Get-Content -LiteralPath (Join-Path $RepositoryRoot '.github\project-map.json') -Raw | ConvertFrom-Json)
$rootReadmePath = Join-Path $RepositoryRoot 'README.md'
$rootReadme = Get-Content -LiteralPath $rootReadmePath -Raw
$warnings = New-Object System.Collections.Generic.List[string]

foreach ($project in $projectMap) {
    $folder = [string]$project.folder
    $label = [string]$project.label
    $projectReadme = Join-Path $RepositoryRoot "$folder\README.md"

    if ($rootReadme -notmatch [regex]::Escape('`' + $folder + '`')) {
        $warnings.Add("Root README project table does not mention $folder.") | Out-Null
    }

    if (-not (Test-Path -LiteralPath $projectReadme -PathType Leaf)) {
        $warnings.Add("Project README is missing: $folder/README.md") | Out-Null
        continue
    }

    $text = Get-Content -LiteralPath $projectReadme -Raw
    if ($text -notmatch '(?im)^##?\s+(build|building)\b' -and $text -notmatch '(?i)build') {
        $warnings.Add("$folder/README.md may be missing build instructions.") | Out-Null
    }
    if ($text -notmatch '(?im)^##?\s+(usage|running|install|quick start|how to)\b' -and $text -notmatch '(?i)(usage|run|install|start)') {
        $warnings.Add("$folder/README.md may be missing usage instructions.") | Out-Null
    }
    if ($text -notmatch '(?i)(admin|elevat|service|registry|secure desktop|appx|com|scheduled task|warning|safety|privilege)') {
        $warnings.Add("$folder/README.md may be missing safety/privilege notes.") | Out-Null
    }

}

if ($warnings.Count -eq 0) {
    Write-Host 'README consistency scan found no warnings.'
    exit 0
}

Write-Warning "README consistency scan found $($warnings.Count) warning(s). These are warnings only and do not fail CI."
foreach ($warning in $warnings) {
    Write-Warning $warning
}

if ($env:GITHUB_STEP_SUMMARY) {
    @(
        '## README consistency warnings',
        '',
        "Found $($warnings.Count) warning item(s). These are informational and do not fail CI.",
        '',
        '<details><summary>Warnings</summary>',
        '',
        '```text',
        ($warnings -join "`n"),
        '```',
        '',
        '</details>'
    ) -join "`n" | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Encoding utf8 -Append
}

exit 0

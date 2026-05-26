[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Continue'
Set-StrictMode -Version 2.0

$patterns = @(
    @{ Name = 'Profile storage'; Pattern = '%APPDATA%|%LOCALAPPDATA%|%PROGRAMDATA%|ApplicationData|LocalApplicationData|CommonApplicationData|SpecialFolder' },
    @{ Name = 'Access-control mutation'; Pattern = 'SetAccessControl|FileSecurity|DirectorySecurity|icacls|takeown|SetNamedSecurityInfo|SetSecurityInfo' },
    @{ Name = 'Credential-like literal'; Pattern = 'password\s*=|token\s*=|secret\s*=|apikey\s*=|api_key\s*=' },
    @{ Name = 'Deprecated marker'; Pattern = 'TODO|FIXME|HACK|obsolete|deprecated|workaround' }
)

$include = @('*.cs', '*.cpp', '*.h', '*.hpp', '*.inc', '*.ps1', '*.cmd', '*.bat', '*.md', '*.json', '*.yml', '*.yaml')
$excludePathRegex = '\\(\.git|build|bin|obj)\\|/\.git/|/build/|/bin/|/obj/'
$files = Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -File -Include $include |
    Where-Object { $_.FullName -notmatch $excludePathRegex }

$warnings = New-Object System.Collections.Generic.List[string]
foreach ($file in $files) {
    $relative = Resolve-Path -LiteralPath $file.FullName -Relative
    $lineNo = 0
    foreach ($line in Get-Content -LiteralPath $file.FullName -ErrorAction SilentlyContinue) {
        $lineNo++
        foreach ($rule in $patterns) {
            if ($line -match $rule.Pattern) {
                $warnings.Add("$relative:$lineNo [$($rule.Name)] $($line.Trim())") | Out-Null
            }
        }
    }
}

if ($warnings.Count -eq 0) {
    Write-Host 'Policy warning scan found no suspicious patterns.'
    exit 0
}

Write-Warning "Policy warning scan found $($warnings.Count) item(s). These are warnings only and do not fail CI."
foreach ($warning in $warnings) {
    Write-Warning $warning
}

if ($env:GITHUB_STEP_SUMMARY) {
    @(
        '## Policy warning scan',
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

[CmdletBinding()]
param([string]$RepositoryRoot)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) { $RepositoryRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) }
$paths = @(
    'legacy/YouTubeMusicMigrate/youtube_music_tidy.ps1',
    'legacy/YouTubeMusicMigrate/cleanup_youtube_music_tidy_directory.ps1',
    'dependencies/YouTubeMusicMigrate/youtube_music_tidy_app.ps1',
    'dependencies/YouTubeMusicMigrate/cleanup_youtube_music_tidy_directory_app.ps1',
    'dependencies/YouTubeMusicMigrate/youtube_music_legacy_frontend.ps1',
    'dependencies/YouTubeMusicMigrate/youtube_music_worker_bridge.ps1'
)
foreach ($relativePath in $paths) {
    $path = Join-Path $RepositoryRoot $relativePath
    $tokens = $null; $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw ("Parse failure in {0}: {1}" -f $relativePath, ($errors | Out-String)) }
    $functions = @($ast.FindAll({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst]
    }, $true))
    if ($relativePath.StartsWith('legacy/')) {
        if ($functions.Count) { throw "Product entry still owns function bodies: $relativePath" }
        if (-not $ast.Extent.Text.Contains('dependencies/YouTubeMusicMigrate/')) {
            throw "Product entry does not select its shared implementation: $relativePath"
        }
    } elseif (-not $functions.Count) {
        throw "Shared implementation is missing its functions: $relativePath"
    }
    if ($relativePath.EndsWith('/youtube_music_tidy_app.ps1')) {
        $implicitLocations = @($ast.FindAll({ param($node)
            $node -is [Management.Automation.Language.VariableExpressionAst] -and
            $node.VariablePath.UserPath -in @('PSScriptRoot', 'PSBoundParameters')
        }, $true))
        if ($implicitLocations.Count) {
            throw 'The shared engine must use the product data root and original invocation arguments.'
        }
    }
}
$profile = [IO.File]::ReadAllText((Join-Path $RepositoryRoot 'dependencies/YouTubeMusicMigrate/youtube_music_profile.cs'))
$catalog = @([regex]::Matches($profile, 'new Parameter\("([A-Za-z0-9]+)"') | ForEach-Object { $_.Groups[1].Value })
$entry = [Management.Automation.Language.Parser]::ParseFile((Join-Path $RepositoryRoot 'legacy/YouTubeMusicMigrate/youtube_music_tidy.ps1'), [ref]$tokens, [ref]$errors)
$declared = @($entry.ParamBlock.Parameters | ForEach-Object { $_.Name.VariablePath.UserPath } | Where-Object { $_ -ne 'IniFile' })
if ($catalog.Count -ne 47 -or (($catalog | Sort-Object) -join ',') -ne (($declared | Sort-Object) -join ',')) { throw 'Compiled catalog differs from the 47 legacy parameters.' }
Write-Output 'YouTubeMusicMigrate source checks passed (scripts parsed, 47-parameter catalog matched, no engine evaluated).'

[CmdletBinding()]
param([string]$RepositoryRoot = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))))

$ErrorActionPreference = 'Stop'
$paths = @(
    'legacy/YouTubeMusicMigrate/youtube_music_tidy.ps1',
    'legacy/YouTubeMusicMigrate/cleanup_youtube_music_tidy_directory.ps1',
    'dependencies/YouTubeMusicMigrate/youtube_music_tidy_app.ps1',
    'dependencies/YouTubeMusicMigrate/cleanup_youtube_music_tidy_directory_app.ps1'
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
Write-Output 'YouTubeMusicMigrate source checks passed (four scripts parsed without evaluation).'

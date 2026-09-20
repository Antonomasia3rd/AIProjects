[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$specPath = Join-Path $repositoryRoot 'dependencies/DesktopStub/tile_text_layout_spec.inc'
if (-not (Test-Path -LiteralPath $specPath -PathType Leaf)) {
    throw "TileText layout specification not found: $specPath"
}

$pattern = '^\s*TILE_TEXT_REGION\(\s*(Medium|Wide|Large)\s*,\s*([1-7])\s*,\s*(Title|Body|Badge)\s*,\s*([0-2])\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*(Near|Far)\s*,\s*(Character|Word)\s*,\s*([1-9][0-9]*)\s*\)\s*$'
$regions = @()
$lineNumber = 0
foreach ($line in Get-Content -LiteralPath $specPath) {
    ++$lineNumber
    $trimmed = $line.Trim()
    if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('//')) {
        continue
    }
    if ($line -notmatch $pattern) {
        throw "Unrecognized TileText specification row at ${specPath}:${lineNumber}: $line"
    }
    $regions += [pscustomobject]@{
        Size = $Matches[1]
        Mask = [int]$Matches[2]
        Role = $Matches[3]
        Source = [int]$Matches[4]
        X = [double]$Matches[5]
        Y = [double]$Matches[6]
        Width = [double]$Matches[7]
        Height = [double]$Matches[8]
        Align = $Matches[9]
        Trim = $Matches[10]
        MaxLines = [int]$Matches[11]
    }
}

$failures = [System.Collections.Generic.List[string]]::new()
function Test-Condition {
    param([bool]$Condition, [string]$Description)
    if ($Condition) {
        Write-Host "ok - $Description"
    } else {
        Write-Host "FAIL - $Description"
        $script:failures.Add($Description)
    }
}

$dimensions = @{
    Medium = @(150.0, 150.0)
    Wide = @(310.0, 150.0)
    Large = @(310.0, 310.0)
}

foreach ($size in @('Medium', 'Wide', 'Large')) {
    Test-Condition (@($regions | Where-Object { $_.Size -eq $size -and $_.Mask -eq 0 }).Count -eq 0) "$size empty input has no regions"
    for ($mask = 1; $mask -le 7; ++$mask) {
        $caseRegions = @($regions | Where-Object { $_.Size -eq $size -and $_.Mask -eq $mask })
        Test-Condition ($caseRegions.Count -gt 0) "$size mask $mask has at least one region"

        for ($i = 0; $i -lt $caseRegions.Count; ++$i) {
            $region = $caseRegions[$i]
            $fieldBit = 1 -shl $region.Source
            Test-Condition (($mask -band $fieldBit) -ne 0) "$size mask $mask region $i references a configured field"
            $withinBounds = (
                $region.Width -gt 0 -and $region.Height -gt 0 -and
                $region.X -ge 0 -and $region.Y -ge 0 -and
                $region.X + $region.Width -le $dimensions[$size][0] -and
                $region.Y + $region.Height -le $dimensions[$size][1])
            Test-Condition $withinBounds "$size mask $mask region $i is positive and within tile bounds"
            Test-Condition ($region.MaxLines -le 4) "$size mask $mask region $i has a supported line limit"

            for ($j = $i + 1; $j -lt $caseRegions.Count; ++$j) {
                $other = $caseRegions[$j]
                $separate = $region.X + $region.Width -le $other.X -or
                    $other.X + $other.Width -le $region.X -or
                    $region.Y + $region.Height -le $other.Y -or
                    $other.Y + $other.Height -le $region.Y
                Test-Condition $separate "$size mask $mask regions $i and $j do not overlap"
                Test-Condition ($region.Source -ne $other.Source) "$size mask $mask regions $i and $j do not draw a field twice"
            }
        }

        $expectedSources = @()
        for ($source = 0; $source -le 2; ++$source) {
            if (($mask -band (1 -shl $source)) -ne 0) {
                if (-not ($size -eq 'Medium' -and $mask -eq 7 -and $source -eq 1)) {
                    $expectedSources += $source
                }
            }
        }
        $actualSources = @($caseRegions | ForEach-Object Source | Sort-Object -Unique)
        $routesExpectedSources = (Compare-Object $expectedSources $actualSources).Count -eq 0
        Test-Condition $routesExpectedSources "$size mask $mask routes exactly the fields supported by its Windows template"
    }
}

$wideAll = @($regions | Where-Object { $_.Size -eq 'Wide' -and $_.Mask -eq 7 })
$wideCaption = @($wideAll | Where-Object {
    $_.Source -eq 1 -and $_.Align -eq 'Far' -and $_.Trim -eq 'Character' -and $_.MaxLines -eq 1
})
Test-Condition ($wideCaption.Count -eq 1) 'Wide BlockAndText02 has one right-side secondary caption'

$largeAll = @($regions | Where-Object { $_.Size -eq 'Large' -and $_.Mask -eq 7 })
$largeBadge = @($largeAll | Where-Object { $_.Source -eq 2 -and $_.Role -eq 'Badge' -and $_.Align -eq 'Near' })
$largeHeaders = @($largeAll | Where-Object {
    $_.Source -in @(0, 1) -and $_.Role -eq 'Title' -and $_.Trim -eq 'Character' -and $_.MaxLines -eq 1
})
$largeBlockStructureMatches = $largeBadge.Count -eq 1 -and $largeHeaders.Count -eq 2
Test-Condition $largeBlockStructureMatches 'Large BlockAndText02 has a leading block and two unwrapped header lines'

if ($failures.Count -ne 0) {
    Write-Host "`n$($failures.Count) TileText layout checks failed."
    exit 1
}

Write-Host "`nTileText portable layout checks passed ($($regions.Count) declarative regions)."
exit 0

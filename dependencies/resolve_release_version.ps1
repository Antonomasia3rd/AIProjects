param(
    [Parameter(Mandatory = $true)]
    [string]$TagPrefix,

    [Parameter(Mandatory = $true)]
    [string]$VersionEnvironment,

    [Parameter(Mandatory = $true)]
    [string]$ReleaseTagEnvironment
)

$ErrorActionPreference = "Stop"

function Parse-Version([string]$Value) {
    $parts = $Value.Split(".")
    if ($parts.Count -ne 4) {
        throw "Version must contain four dot-separated numbers."
    }

    $parsed = @()
    foreach ($part in $parts) {
        [int]$number = 0
        if (-not [int]::TryParse($part, [ref]$number) -or $number -lt 0 -or $number -gt 65535) {
            throw "Version parts must be numbers from 0 through 65535."
        }
        $parsed += $number
    }
    return ,$parsed
}

function Parse-ReleaseTag([string]$Value) {
    $pattern = "^" + [regex]::Escape($TagPrefix) + "([0-9]+)$"
    $match = [regex]::Match($Value, $pattern, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $match.Success) {
        return $null
    }

    [int]$number = 0
    if (-not [int]::TryParse($match.Groups[1].Value, [ref]$number) -or
        $number -lt 1 -or
        $number -gt 65535) {
        return $null
    }
    return $number
}

if ([string]::IsNullOrWhiteSpace($TagPrefix) -or $TagPrefix -match "[\s~^:?*\[\\]") {
    throw "TagPrefix is empty or contains characters that are unsafe in a Git tag."
}

$versionOverride = [Environment]::GetEnvironmentVariable($VersionEnvironment, "Process")
$releaseTagOverride = [Environment]::GetEnvironmentVariable($ReleaseTagEnvironment, "Process")
$releaseTag = $null
$versionParts = $null

if (-not [string]::IsNullOrWhiteSpace($versionOverride)) {
    $versionParts = Parse-Version $versionOverride
    if ($versionParts[0] -lt 1) {
        throw "$VersionEnvironment major version must be from 1 through 65535."
    }
}

if (-not [string]::IsNullOrWhiteSpace($releaseTagOverride)) {
    $tagNumber = Parse-ReleaseTag $releaseTagOverride
    if ($null -eq $tagNumber) {
        throw "$ReleaseTagEnvironment must look like ${TagPrefix}N with N from 1 through 65535."
    }
    $releaseTag = $releaseTagOverride
    if ($null -eq $versionParts) {
        $versionParts = @($tagNumber, 0, 0, 0)
    }
    elseif ($versionParts[0] -ne $tagNumber) {
        throw "$VersionEnvironment major version must match the numeric release tag."
    }
}

if ($null -eq $versionParts) {
    $exactTags = @()
    $allTags = @()
    try {
        $exactTags = @(& git tag --points-at HEAD --list "${TagPrefix}*" 2>$null)
        if ($LASTEXITCODE -ne 0) {
            $exactTags = @()
        }
        $allTags = @(& git tag --list "${TagPrefix}*" 2>$null)
        if ($LASTEXITCODE -ne 0) {
            $allTags = @()
        }
    }
    catch {
        $exactTags = @()
        $allTags = @()
    }

    $exactVersions = @(
        foreach ($tag in $exactTags) {
            $number = Parse-ReleaseTag $tag
            if ($null -ne $number) {
                [pscustomobject]@{ Tag = $tag; Number = $number }
            }
        }
    )

    if ($exactVersions.Count -gt 0) {
        $selected = $exactVersions | Sort-Object Number -Descending | Select-Object -First 1
        $releaseTag = $selected.Tag
        $versionParts = @($selected.Number, 0, 0, 0)
    }
    else {
        $maxVersion = 0
        foreach ($tag in $allTags) {
            $number = Parse-ReleaseTag $tag
            if ($null -ne $number -and $number -gt $maxVersion) {
                $maxVersion = $number
            }
        }
        $nextVersion = $maxVersion + 1
        if ($nextVersion -gt 65535) {
            throw "The next release version exceeds the Win32/AppX version limit."
        }
        $releaseTag = "${TagPrefix}${nextVersion}"
        $versionParts = @($nextVersion, 0, 0, 0)
    }
}

if ([string]::IsNullOrWhiteSpace($releaseTag)) {
    $releaseTag = "${TagPrefix}$($versionParts[0])"
}

$version = ($versionParts -join ".")
Write-Output "RELEASE_TAG=$releaseTag"
Write-Output "VERSION=$version"
Write-Output "VERSION_MAJOR=$($versionParts[0])"
Write-Output "VERSION_MINOR=$($versionParts[1])"
Write-Output "VERSION_BUILD=$($versionParts[2])"
Write-Output "VERSION_REVISION=$($versionParts[3])"

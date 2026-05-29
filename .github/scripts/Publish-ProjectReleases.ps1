[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactsRoot = 'artifacts',
    [string]$FullSha = $env:FULL_SHA,
    [string]$Repository = $env:REPO,
    [string]$ProjectList = $env:PROJECT_LIST
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

if ([string]::IsNullOrWhiteSpace($FullSha)) {
    throw 'FullSha was not provided and FULL_SHA is empty.'
}
if ([string]::IsNullOrWhiteSpace($Repository)) {
    throw 'Repository was not provided and REPO is empty.'
}

$projectMapPath = Join-Path $RepositoryRoot '.github\project-map.json'
$projectMap = @(Get-Content -LiteralPath $projectMapPath -Raw | ConvertFrom-Json)
if (-not $projectMap) {
    throw 'Project map is empty.'
}

if (-not [System.IO.Path]::IsPathRooted($ArtifactsRoot)) {
    $ArtifactsRoot = Join-Path $RepositoryRoot $ArtifactsRoot
}
if (-not (Test-Path -LiteralPath $ArtifactsRoot -PathType Container)) {
    throw "Artifacts directory not found: $ArtifactsRoot"
}

Push-Location $RepositoryRoot
try {
    git fetch --force --tags
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $projectsWithArtifacts = @()
    foreach ($project in $projectMap) {
        $artifactRoot = Join-Path $ArtifactsRoot ([string]$project.artifactName)
        if (Test-Path -LiteralPath $artifactRoot -PathType Container) {
            $files = @(Get-ChildItem -LiteralPath $artifactRoot -Recurse -File)
            if ($files.Count -gt 0) {
                $projectsWithArtifacts += [pscustomobject]@{
                    Project = $project
                    ArtifactRoot = $artifactRoot
                    Files = $files
                }
            }
        }
    }

    if ($projectsWithArtifacts.Count -eq 0) {
        throw 'No downloaded project artifacts were found for release upload.'
    }

    $summary = @(
        '## Automatic project releases',
        '',
        'Each built project is released in its own `Project-vN` tag family. ZIP build outputs are expanded before upload so release assets contain the actual binaries.',
        '',
        '| Project | Release | Uploaded assets |',
        '| --- | --- | --- |'
    )

    foreach ($entry in $projectsWithArtifacts) {
        $project = $entry.Project
        $releaseBase = [string]$project.key
        $label = [string]$project.label
        $artifactRoot = [string]$entry.ArtifactRoot

        $matchingTags = @(git tag --list "$releaseBase-v*" --sort=-v:refname)
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        $tag = $null
        foreach ($candidate in $matchingTags) {
            $candidateSha = (git rev-list -n 1 $candidate).Trim()
            if ($LASTEXITCODE -eq 0 -and $candidateSha -eq $FullSha) {
                $tag = $candidate
                break
            }
        }

        $previousTag = $null
        foreach ($candidate in $matchingTags) {
            if ($candidate -ne $tag) {
                $previousTag = $candidate
                break
            }
        }

        if (-not $tag) {
            $maxVersion = 0
            $pattern = '^' + [regex]::Escape($releaseBase) + '-v(?<version>\d+)$'
            foreach ($candidate in $matchingTags) {
                if ($candidate -match $pattern) {
                    $version = [int]$Matches.version
                    if ($version -gt $maxVersion) {
                        $maxVersion = $version
                    }
                }
            }
            $tag = "$releaseBase-v$($maxVersion + 1)"
        }

        if (-not $previousTag) {
            $previousTag = @($matchingTags | Where-Object { $_ -ne $tag } | Select-Object -First 1)
        }

        $releaseWorkRoot = Join-Path $env:RUNNER_TEMP ("release-assets-" + $releaseBase)
        if (Test-Path -LiteralPath $releaseWorkRoot) {
            Remove-Item -LiteralPath $releaseWorkRoot -Recurse -Force
        }
        New-Item -ItemType Directory -Path $releaseWorkRoot -Force | Out-Null

        $expandedRoot = Join-Path $releaseWorkRoot '_expanded'
        New-Item -ItemType Directory -Path $expandedRoot -Force | Out-Null
        $uploadRoot = Join-Path $releaseWorkRoot 'upload'
        New-Item -ItemType Directory -Path $uploadRoot -Force | Out-Null

        $usedNames = @{}
        $releaseAssets = New-Object System.Collections.Generic.List[string]

        function Add-ReleaseAsset {
            param(
                [Parameter(Mandatory=$true)][string]$SourcePath,
                [Parameter(Mandatory=$true)][string]$PreferredName,
                [string]$FallbackName
            )

            $name = [System.IO.Path]::GetFileName($PreferredName)
            if ([string]::IsNullOrWhiteSpace($name)) {
                $name = [System.IO.Path]::GetFileName($SourcePath)
            }

            if ($usedNames.ContainsKey($name)) {
                $fallback = if ([string]::IsNullOrWhiteSpace($FallbackName)) { $name } else { $FallbackName }
                $fallback = ($fallback -replace '[\\/]+', '-')
                $fallback = [System.IO.Path]::GetFileName($fallback)
                if (-not [string]::IsNullOrWhiteSpace($fallback) -and -not $usedNames.ContainsKey($fallback)) {
                    $name = $fallback
                } else {
                    $base = [System.IO.Path]::GetFileNameWithoutExtension($name)
                    $ext = [System.IO.Path]::GetExtension($name)
                    $index = 2
                    do {
                        $candidate = "$base-$index$ext"
                        $index++
                    } while ($usedNames.ContainsKey($candidate))
                    $name = $candidate
                }
            }

            $destination = Join-Path $uploadRoot $name
            Copy-Item -LiteralPath $SourcePath -Destination $destination -Force
            $usedNames[$name] = $true
            $releaseAssets.Add($destination)
        }

        $zipIndex = 0
        foreach ($file in @($entry.Files | Sort-Object FullName)) {
            if ($file.Extension -ieq '.zip') {
                $zipIndex++
                $destination = Join-Path $expandedRoot ("zip-$zipIndex")
                New-Item -ItemType Directory -Path $destination -Force | Out-Null
                Expand-Archive -LiteralPath $file.FullName -DestinationPath $destination -Force
                $expandedFiles = @(Get-ChildItem -LiteralPath $destination -Recurse -File | Sort-Object FullName)
                foreach ($expandedFile in $expandedFiles) {
                    $relative = $expandedFile.FullName.Substring($destination.Length).TrimStart('\', '/')
                    Add-ReleaseAsset -SourcePath $expandedFile.FullName -PreferredName $expandedFile.Name -FallbackName $relative
                }
            } else {
                Add-ReleaseAsset -SourcePath $file.FullName -PreferredName $file.Name
            }
        }

        if ($releaseAssets.Count -eq 0) {
            throw "No release assets were prepared for $label."
        }

        $commitSubjects = @()
        if ($previousTag) {
            $commitSubjects = @(git log --format='%h %s' "$previousTag..$FullSha")
        } else {
            $commitSubjects = @(git log --format='%h %s' -n 10 $FullSha)
        }
        if ($LASTEXITCODE -ne 0 -or -not $commitSubjects) {
            $commitSubjects = @($FullSha)
        }

        $checksumLines = @(
            '## SHA256 checksums',
            '',
            '| File | SHA256 |',
            '| --- | --- |'
        )
        foreach ($asset in @($releaseAssets | Sort-Object)) {
            $name = Split-Path -Leaf $asset
            $hash = (Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant()
            $checksumLines += "| ``$name`` | ``$hash`` |"
        }

        $changeLines = @('## Changes', '')
        foreach ($subject in $commitSubjects) {
            $changeLines += "- $subject"
        }

        $notes = @(
            "Automated Windows build for $label on main.",
            "",
            "Commit: ``$FullSha``",
            "Built projects: $ProjectList",
            "Release family: ``$releaseBase-vN``",
            "",
            "ZIP build outputs are expanded before upload, so release assets contain the actual binaries/files.",
            "",
            ($changeLines -join "`n"),
            "",
            ($checksumLines -join "`n")
        ) -join "`n"

        $notesPath = Join-Path $releaseWorkRoot 'release-notes.md'
        $notes | Out-File -FilePath $notesPath -Encoding utf8

        gh release view $tag --repo $Repository *> $null
        if ($LASTEXITCODE -eq 0) {
            gh release edit $tag --repo $Repository --title $tag --notes-file $notesPath
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        } else {
            gh release create $tag --repo $Repository --target $FullSha --title $tag --notes-file $notesPath
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }

        $assetPaths = @($releaseAssets | Sort-Object)
        gh release upload $tag @assetPaths --repo $Repository --clobber
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        $assetNames = ($assetPaths | ForEach-Object { Split-Path -Leaf $_ }) -join ', '
        $summary += "| $label | ``$tag`` | $assetNames |"
    }

    $summary -join "`n" | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Encoding utf8 -Append
}
finally {
    Pop-Location
}

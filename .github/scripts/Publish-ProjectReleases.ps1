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

function Get-ArtifactTreeSummary {
    param(
        [Parameter(Mandatory=$true)][string]$Root,
        [int]$MaxEntries = 120
    )

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        return @("<missing: $Root>")
    }

    $items = @(Get-ChildItem -LiteralPath $Root -Force -Recurse | Sort-Object FullName)
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($item in @($items | Select-Object -First $MaxEntries)) {
        $relative = $item.FullName.Substring($Root.Length).TrimStart('\', '/')
        if ([string]::IsNullOrWhiteSpace($relative)) {
            $relative = '.'
        }

        if ($item.PSIsContainer) {
            $lines.Add("DIR  $relative")
        } else {
            $lines.Add("FILE $relative ($($item.Length) bytes)")
        }
    }

    if ($items.Count -gt $MaxEntries) {
        $lines.Add("... $($items.Count - $MaxEntries) more item(s) omitted")
    }

    return @($lines)
}

function Get-ProjectArtifactLeafNames {
    param([Parameter(Mandatory=$true)]$Project)

    $names = New-Object System.Collections.Generic.List[string]
    foreach ($artifactPath in @($Project.artifactPath)) {
        if (-not [string]::IsNullOrWhiteSpace([string]$artifactPath)) {
            $leaf = Split-Path -Leaf ([string]$artifactPath)
            if (-not [string]::IsNullOrWhiteSpace($leaf)) {
                $names.Add($leaf)
            }
        }
    }

    return @($names | Select-Object -Unique)
}

function Resolve-DownloadedProjectArtifact {
    param(
        [Parameter(Mandatory=$true)]$Project,
        [Parameter(Mandatory=$true)][string]$ArtifactsRoot
    )

    $artifactName = [string]$Project.artifactName
    $namedRoot = Join-Path $ArtifactsRoot $artifactName
    if (Test-Path -LiteralPath $namedRoot -PathType Container) {
        $files = @(Get-ChildItem -LiteralPath $namedRoot -Recurse -File)
        if ($files.Count -gt 0) {
            return [pscustomobject]@{
                Project = $Project
                ArtifactRoot = $namedRoot
                Files = $files
                Source = "named artifact directory: $artifactName"
            }
        }
    }

    # Some download-artifact versions/options can leave or produce a single zip
    # payload instead of an expanded <artifact name> directory. Accept that too.
    $zipPath = Join-Path $ArtifactsRoot ($artifactName + '.zip')
    if (Test-Path -LiteralPath $zipPath -PathType Leaf) {
        $expandedRoot = Join-Path $env:RUNNER_TEMP ('release-artifact-' + $artifactName)
        if (Test-Path -LiteralPath $expandedRoot) {
            Remove-Item -LiteralPath $expandedRoot -Recurse -Force
        }
        New-Item -ItemType Directory -Path $expandedRoot -Force | Out-Null
        Expand-Archive -LiteralPath $zipPath -DestinationPath $expandedRoot -Force
        $files = @(Get-ChildItem -LiteralPath $expandedRoot -Recurse -File)
        if ($files.Count -gt 0) {
            return [pscustomobject]@{
                Project = $Project
                ArtifactRoot = $expandedRoot
                Files = $files
                Source = "zip artifact: $artifactName.zip"
            }
        }
    }

    # When only one artifact is downloaded, newer action behavior may extract
    # the payload directly into the requested download path instead of creating
    # artifacts/<artifactName>. Treat that as this project only when expected
    # project output file names are present at the download root.
    $expectedLeafNames = @(Get-ProjectArtifactLeafNames -Project $Project)
    if ($expectedLeafNames.Count -gt 0) {
        $allFiles = @(Get-ChildItem -LiteralPath $ArtifactsRoot -Recurse -File)
        $matchingFiles = @($allFiles | Where-Object { $expectedLeafNames -contains $_.Name })
        if ($matchingFiles.Count -gt 0) {
            $rootFiles = @(Get-ChildItem -LiteralPath $ArtifactsRoot -File -ErrorAction SilentlyContinue)
            $childDirectories = @(Get-ChildItem -LiteralPath $ArtifactsRoot -Directory -ErrorAction SilentlyContinue)

            if ($rootFiles.Count -gt 0 -and $childDirectories.Count -eq 0) {
                return [pscustomobject]@{
                    Project = $Project
                    ArtifactRoot = $ArtifactsRoot
                    Files = $rootFiles
                    Source = 'direct artifact payload at download root'
                }
            }

            # Fallback for an unexpected nested layout: release the files that
            # match the project map rather than failing the whole release job.
            return [pscustomobject]@{
                Project = $Project
                ArtifactRoot = $ArtifactsRoot
                Files = $matchingFiles
                Source = 'matched project artifact files in unexpected layout'
            }
        }
    }

    return $null
}

Push-Location $RepositoryRoot
try {
    git fetch --force --tags
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $projectsWithArtifacts = @()
    foreach ($project in $projectMap) {
        $entry = Resolve-DownloadedProjectArtifact -Project $project -ArtifactsRoot $ArtifactsRoot
        if ($entry) {
            Write-Host "Found release artifact payload for $($project.label): $($entry.Source)"
            $projectsWithArtifacts += $entry
        }
    }

    if ($projectsWithArtifacts.Count -eq 0) {
        Write-Host 'Downloaded artifact tree:'
        foreach ($line in @(Get-ArtifactTreeSummary -Root $ArtifactsRoot)) {
            Write-Host "  $line"
        }
        throw 'No downloaded project artifacts were found for release upload.'
    }

    $summary = @(
        '## Automatic project releases',
        '',
        'Each built project is released in its own `Project-vN` tag family. Workflow artifact payload files are uploaded to the matching project release as individual assets.',
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
                $fallback = ($fallback -replace '[\/]+', '-')
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

        foreach ($file in @($entry.Files | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($artifactRoot.Length).TrimStart('\', '/')
            Add-ReleaseAsset -SourcePath $file.FullName -PreferredName $file.Name -FallbackName $relative
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
            "Release assets are direct files from the workflow artifact payload for this project.",
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

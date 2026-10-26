$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskWorkflow=Get-Content -LiteralPath (Join-Path $taskRoot '.github/workflows/build-windows.yml') -Raw
$taskMap=Get-Content -LiteralPath (Join-Path $taskRoot '.github/project-map.json') -Raw | ConvertFrom-Json
$taskBuild=Get-Content -LiteralPath (Join-Path $taskRoot '.github/scripts/build-windows.cmd') -Raw
$script:taskChecks=0
function Require([bool]$Condition,[string]$Message){$script:taskChecks++;if(!$Condition){throw $Message}}
$taskProject=@($taskMap | Where-Object key -eq 'DesktopStub')
Require ($taskProject.Count -eq 1) 'DesktopStub must have exactly one project-map entry.'
foreach($taskName in @('DesktopStub.exe','DesktopStubLiveTileBroker.exe','DesktopStubTileLauncher.exe')){
 $taskPath='DesktopStub/build/'+$taskName
 Require ($taskProject[0].artifactPaths -contains $taskPath) ('Missing artifact/checksum/release inventory: '+$taskName)
 Require ($taskWorkflow.Contains('            '+$taskPath)) ('Missing workflow payload: '+$taskName)
 Require ($taskBuild.Contains('call :RecordArtifact "%REPO%\DesktopStub\build\'+$taskName+'"')) ('Missing build checksum record: '+$taskName)
}
$taskStable=[regex]::Match($taskWorkflow,'(?ms)^  auto-release:\r?\n(.*?)(?=^  [a-z][a-z0-9-]*:|\z)').Groups[1].Value
Require ($taskStable.Contains("vars.AUTO_RELEASE_ENABLED == 'true'")) 'Stable publication must require explicit repository opt-in.'
Require ($taskStable.Contains("!contains(github.event.head_commit.message, '[ci-artifacts-only]')")) 'Consolidation marker must override stable-release opt-in.'
Require ($taskStable.Contains('- attest-build-provenance')) 'Stable publication must wait for attestation.'
Require ([regex]::Matches($taskWorkflow,[regex]::Escape("vars.AUTO_RELEASE_ENABLED == 'true' && !contains(github.event.head_commit.message, '[ci-artifacts-only]')")).Count -eq 2) 'Version reuse and stable publication must share the release guard.'
$taskDraft=[regex]::Match($taskWorkflow,'(?ms)^  desktopstub-draft-beta:\r?\n(.*)$').Groups[1].Value
Require ($taskDraft.Contains("github.event_name == 'workflow_dispatch' && inputs.desktopstub_draft_beta && inputs.project == 'DesktopStub'")) 'Draft test must be explicit and DesktopStub-only.'
Require ($taskDraft.Contains('needs: [build-windows, attest-build-provenance]')) 'Draft release must wait for build and attestation.'
Require ($taskDraft.Contains('--draft --prerelease --latest=false')) 'Beta test must remain a draft prerelease and never become latest.'
Require ($taskDraft -notmatch 'release (edit|delete|upload)|draft=false|clobber') 'Draft test must not overwrite, delete or publish another release.'
Require ($taskDraft.Contains('GITHUB_SHA') -and $taskDraft.Contains('FileVersion')) 'Draft assets must be version-checked and targeted to the built commit.'
Require ($taskWorkflow.Contains("'-actions-' + "+'$env:GITHUB_RUN_ID'+" + '-' + "+'$env:GITHUB_RUN_ATTEMPT')) 'Draft test tags must be unique per workflow attempt.'
Require ($taskWorkflow.Contains("contains(github.event.head_commit.message, '[ci-artifacts-only]') || inputs.desktopstub_draft_beta")) 'Artifacts-only consolidation must keep DesktopStub beta version metadata.'
Write-Output ($script:taskChecks.ToString()+' DesktopStub release safety checks passed; no workflow dispatch or release API used.')

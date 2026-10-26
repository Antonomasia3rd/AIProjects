# DesktopStub Actions release safety, October 26

The previous main-push workflow could publish stable Project-vN releases after
building; its publisher ultimately changed draft=false and did not wait for the
separate provenance job. DesktopStub's artifact and checksum/release inventory
also omitted DesktopStubTileLauncher.exe.

The payload now includes all three EXEs in the workflow upload, project map and
batch checksum records. Release discovery/checksum summary use that same map.
Both stable and optional draft publication wait for successful attestation.

Stable automatic publication and zero-download version reuse now require the
repository variable AUTO_RELEASE_ENABLED to be exactly true. Unset/false means
build artifacts and attestation only. A main-push head commit containing
**[ci-artifacts-only]** suppresses both even if that variable is enabled. The
consolidation push must carry this marker. Existing stable version-selection
and replacement policies otherwise remain unchanged.

When stable publication is disabled or the marker is present, DesktopStub
artifacts retain beta2 metadata: version 27.0.0.2, tag DesktopStub-v27-beta2.
To test the Actions release path, manually dispatch Build Windows binaries,
choose DesktopStub and enable desktopstub_draft_beta. The build uses a unique
DesktopStub-v27-beta2-actions-RUNID-ATTEMPT tag. After normal build/smoke and
attestation, the job validates all three FileVersions and creates a checksum
manifest, then creates a **draft prerelease**, with latest=false. It does not
edit/delete/clobber existing releases or publish the draft.

The safety fixture is .github/scripts/Test-DesktopStubReleaseSafety.ps1. It
checks the payload inventories and gate/dependency/publication contracts without
calling GitHub. This audit does not establish a successful hosted build or
release: the parent reports the actual Actions run separately.

Primary contracts: [GitHub job dependencies](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#jobsjob_idneeds)
and [GitHub CLI release creation](https://cli.github.com/manual/gh_release_create).

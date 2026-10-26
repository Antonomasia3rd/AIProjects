# Windows 10 1809 Desktop image investigation

The supplied VM screenshot reports Windows 10 1809/build 17763. Desktop displays
its original static icon/name instead of wallpaper; Prayer Times displays live
text, without its expected corner image. The configured Desktop delivery mode,
the exact DesktopStub build and the VM's registration/log/asset state have not
yet been collected. The screenshot alone does not establish one shared cause.

## Confirmed compatibility gap

The independent Registration manifest in `dependencies/tile_registration.h`
previously declared `MinVersion="10.0.19041.0"` and used `uap10:Parameters` to
pass its profile/host/tile arguments to the small launcher. Microsoft documents
that **uap10 starts with Windows 10 2004/build 19041**, and a package with that
minimum cannot install on 17763. This is a concrete restriction on the current
independent Registration backend, not a general restriction on Live Tiles.

The ordinary main manifest instead defaults to a 15063 minimum. This distinction
can allow a main host and live secondary tiles to operate on 1809 while an
independent Registration publication fails. Existing main pins also do not turn
into the separate Registration identity in place. The supplied screenshot's
unknown delivery settings mean this is not yet proof of its actual failure.

The compatibility repair removes the uap10 dependency and uses a Windows 10
15063 manifest floor. It also supplies the older-compatible activation mechanism
described below; lowering the version alone would lose the launcher arguments.

Primary reference: [Application manifest activation requirements](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-f-application).

## Older-compatible independent activation

`registration.activation` is a versioned, bounded public-data record in each
owned independent package. It records the existing user/profile/tile scopes,
package name/publisher, canonical host/INI paths, tile ID, product and optional
main AUMID. The host atomically publishes it before writing the new manifest;
unchanged records are retained. Current-user ownership and ordinary-file checks
apply to the new record and the existing owner marker. There is no account copy
or command-line evaluation.

When Windows starts the small launcher without arguments, it verifies the
current installed package's name and full publisher distinguished name using
`PackageIdFromFullName(PACKAGE_INFORMATION_FULL)`, its own family/PRAID `Tile`,
package root and executable, the current SID-derived scopes, exact existing V1
owner record, current-user ownership of the INI, and ordinary local paths without
reparse/network traversal. The INI is checked for ownership, not read for content.
Only then can the existing resident IPC, main-package activation or verified
unpackaged-host fallback be used. Missing or unsupported context fails clearly
before activation. Unknown or malformed explicit arguments do not become a
fallback record; the prior explicit-argument parser and `--validate-only` remain
compatible.

The V1 owner marker binds its original user/profile/tile identity, not a main
AUMID. The new current-user-owned record supplies the current host/AUMID settings;
the AUMID is not confused with the independent package's own `!Tile` identity.
This is configuration validation, not an anti-tampering boundary against the
same Windows user who can edit the owned files. Existing authorization markers,
package identities and pin destinations are preserved. No extra authority copy
or profile sidecar is needed.

Relevant contracts: [full package identity data](https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-packageidfromfullname),
[current application ID](https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-getcurrentapplicationusermodelid).

## Primary Live Tile route and image contract

Source review compared the current builder with `DesktopStub-v25`:

- The main frame has an empty secondary ID. The shared delivery adapter chooses
  `CreateTileUpdaterForApplication`; source tiles use their secondary IDs.
- Image-only Desktop faces use Medium/Wide/Large adaptive bindings with a
  packaged image, background placement and explicit zero black overlay. No text
  is required to admit the wallpaper image. The basic image contract also exists
  in v25; no proven 1809-specific XML defect was found in that comparison.
- Current code validates file existence and exact expected PNG dimensions before
  admitting an image URI. Missing/wrong-size files must fail publication rather
  than be described as accepted wallpaper.

Microsoft documents primary and secondary local notifications separately, and
desktop secondary notifications require build 17134.81 or later—older than 1809.
Images may come from package or local storage. None of this proves that the VM's
actual generated paths, package root, permissions or cached pin are correct.

References: [local tile delivery](https://learn.microsoft.com/en-us/windows/uwp/controls-and-patterns/tiles-and-notifications-sending-a-local-tile-notification),
[Desktop Bridge secondary tiles](https://learn.microsoft.com/en-us/windows/apps/design/shell/tiles-and-notifications/secondary-tiles-desktop-pinning),
[image schema](https://learn.microsoft.com/en-us/uwp/schemas/tiles/tilesschema/element-image).

## Focused evidence and VM collection

`PrimaryImageTileHostTests.inc::TestPrimaryImageTileHost` checks actual private
PNGs through the production XML builder and shared delivery adapter. It covers
the three image-only sizes, percent-encoded spaces/Unicode, equal primary versus
secondary image faces with different routing, and missing/wrong-size refusal.
A focused optimized host passed **18 checks with no failures**, after a clean
Defender scan. Logs are under `build/review-validation/primary-image-oct26-*`.
The combined host can include the same function; these injected contracts are
not native 1809 acceptance. The historical four-pixel caption assertion is
unchanged.

The standalone read-only collector `tools/InspectDesktopStubTileImages.ps1` can
be copied to the affected VM and run in Windows PowerShell:

```powershell
powershell.exe -NoProfile -File .\InspectDesktopStubTileImages.ps1 `
  -Directory 'C:\Path\To\DesktopStub' -OutputPath '.\DesktopStub-tile-images.json'
```

It emits the OS build, public manifest identity and image paths, matching
current-user registration, an allowlist of delivery/image settings, and bounded
PNG header/hash/ACL metadata, including existing Prayer topic assets. It does
not copy image contents or account settings, edit INIs/ACLs, start DesktopStub,
pin tiles or deploy packages. Linked/network paths and XML DTDs are rejected.
PowerShell syntax and a private synthetic fixture passed; the fixture also
verified secret-field exclusion and unchanged INI bytes. It has not been run
against the user's VM.

// ==WindhawkMod==
// @id              logonui-scale
// @name            LogonUI Custom Scale
// @description     Set an independent XAML scale for the Windows sign-in screen without changing system DPI, with input-coordinate compensation and fail-closed Windows 10/11 compatibility checks.
// @version         1.0.0
// @author          Antonomasia
// @github          https://github.com/Antonomasia3rd
// @include         logonui.exe
// @architecture    amd64
// @compilerOptions -luser32
// @license         MIT
// ==/WindhawkMod==


// ==WindhawkModReadme==
/*
# LogonUI Custom Scale

Set the Windows sign-in screen to an independent XAML scale without changing
the system DPI. The mod also compensates pointer coordinates so buttons remain
aligned with their scaled visuals.

The default target is **150%**. Arbitrary integer values from **50% to 500%**
are accepted. Upscaling is the normal supported path; downscaling is
experimental and requires **Strict compatibility** to be disabled.

## Important setup

`logonui.exe` is a critical Windows process and is excluded from Windhawk
injection by default. Before using this mod:

1. Open **Windhawk > Settings > Advanced settings > More advanced settings**.
2. Add `logonui.exe` to the **Process inclusion list**.
3. Install/enable this mod.

The mod explicitly targets only `logonui.exe`.

## Upgrading from development builds

The development versions used the ID `logonui-dpi-lab`. This Marketplace
release uses the permanent ID `logonui-scale`.

**Disable or uninstall `logonui-dpi-lab` before enabling this version.**
Do not run both versions at the same time.

## Compatibility and safety

This mod hooks private functions in `Windows.UI.Xaml.dll`, so compatibility is
validated conservatively:

- Exact known builds are preferred.
- Unknown builds must uniquely match a validated implementation family.
- `DXamlCore::QueryScalePercentage` and
  `ButtonBase::IsValidPointerPosition` must resolve to the **same** family.
- The private ButtonBase ABI is checked for that family.
- Ambiguous or structurally different implementations are rejected instead of
  being guessed.

In other words, unsupported XAML implementations are intended to **fail
closed** and leave the normal LogonUI scaling path untouched.

Runtime-tested on **Windows 11 build 26200.6854**. The resolver also contains
PDB/static-validation coverage for Windows 10 families back to **1507 /
10240**, through later Windows 10 releases and validated Windows 11 families,
including the 28000 family. Not every covered build has been runtime-tested.

Because this mod runs inside a critical system process, Windows updates can
still change private internals. If the sign-in UI behaves unexpectedly after
an update, disable the mod (or remove `logonui.exe` from Windhawk's process
inclusion list) until the new build can be validated.

## Settings

- **LogonUI scale percent** - Target XAML scale, from 50% to 500%.
- **Enable LogonUI scale override** - Enables the private XAML scale override.
- **Enable input coordinate fix** - Keeps ButtonBase hit-testing aligned with
  independently scaled visuals.
- **Strict compatibility** - Recommended. Allows equal/upscale ratios and
  refuses experimental downscaling.
- **Discover compatible XAML builds** - Allows conservative signature-based
  matching for builds that aren't in the exact-build table.
- **Diagnostic logging** - Logs resolver, scale, and compensated hit-test
  information for troubleshooting.

## Notes

The mod changes LogonUI's XAML scale; it does **not** change the monitor DPI,
desktop scaling setting, or global Windows DPI configuration.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- rootScalePercent: 150
  $name: LogonUI scale percent
  $description: Target XAML scale from 50% to 500%. Arbitrary integer upscales such as 125%, 175%, and 200% are supported. Downscaling below the native/source scale is experimental and requires Strict compatibility to be disabled.

- enableScaleOverride: true
  $name: Enable LogonUI scale override
  $description: Feeds the target percentage into XAML's native QueryScalePercentage path.

- enableInputCoordinateFix: true
  $name: Enable input coordinate fix
  $description: Corrects the ButtonBase hit-test coordinate mismatch caused by scaling XAML independently from the real window DPI. Upscaling uses promotion-only correction; experimental downscaling can also demote false-positive hits.

- strictCompatibility: true
  $name: Strict compatibility
  $description: Recommended. Keeps arbitrary 50-500% equal/upscales enabled while refusing downscaling below the native/source XAML scale. Disable only to opt into experimental downscaling, which requires bidirectional hit-test correction. Private-XAML build validation remains fail-closed regardless.

- allowCompatibleBuildDiscovery: true
  $name: Discover compatible XAML builds
  $description: Recommended. If the exact Windows.UI.Xaml build is unknown, scans only for validated Win10 1507, Win10 1511, Win10 1607, Win10 1703, Win10 1709, Win10 1803, Win10 1809, Win10 1903/18362, newer Win10, early Win11 22000/22621, Win11 26100-modern, and Win11 28000 function families and refuses ambiguous or structurally different implementations.

- diagnosticLogging: false
  $name: Diagnostic logging
  $description: Logs scale decisions and compensated ButtonBase hit tests. Leave off for normal use.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <winternl.h>
#include <stdint.h>
#include <string.h>

static constexpr PCWSTR BUILD =
    L"LogonUI Custom Scale v1.0.0 - fail-closed multi-build XAML scaling";

// -------------------------------------------------------------------------
// Windows.UI.Xaml build resolution.
//
// Exact/statically validated members:
//   * Windows 10 1507 sampled: 10240.16384, .17071, .17609, .17741,
//     .17797, .18036, .18818, .20345, .20401, .21072
//   * Windows 10 1511: 10586.0, .494, .545, .589, .672, .839, .842,
//     .873, .916, .962, .965, .1007, .1358, .1417, .1478
//   * Windows 10 1607: 14393.0, .594, .726, .1378, .1715, .2125,
//     .2758, .4104, .4169, .4467, .6451, .6529, .6795, .9507
//   * Windows 10 1703: 15063.540, .608, .1418, .1781, .2679
//   * Windows 10 1709: 16299.214, .492, .755, .936, .1653, .1685
//   * Windows 10 1803: 17134.1, .81, .376, .556, .799, .1038, .1098
//   * Windows 10 1809 / LTSC 2019: 17763.1, 17763.379, 17763.2090
//   * Windows 10 1903 / 18362: 27 SHA-verified harvested builds (.1 through .1714)
//     (18362.418 is intentionally family-scan-only due a Winbindex SHA vs symbol-server payload mismatch)
//   * Windows 10 22H2 19045.7725 (PDB-verified reference)
//   * Windows 11 21H2 22000.318 (PDB-verified reference)
//   * Windows 11 24H2/25H2-shared XAML reference (validated modern-family reference)
//   * Windows 11 26H1 / 28000.2525 and 28000.2605
//
// 15063 is deliberately a separate implementation family. Its PDB-verified
// ButtonBase::IsValidPointerPosition uses:
//   * size interface: this + 0x110
//   * cached pointer X/Y: this + 0x1C0 / +0x1C4
//   * exact zero-edge comparisons (no +/-0.05 tolerance constants)
// QueryScalePercentage shares the validated 16299 implementation shape.
//
// 16299 is deliberately a separate implementation family. Its PDB-verified
// ButtonBase::IsValidPointerPosition uses:
//   * size interface: this + 0x120
//   * cached pointer X/Y: this + 0x1E0 / +0x1E4
//   * exact zero-edge comparisons (no +/-0.05 tolerance constants)
//
// 17134 is deliberately a separate implementation family. Its PDB-verified
// ButtonBase::IsValidPointerPosition uses:
//   * size interface: this + 0x138
//   * cached pointer X/Y: this + 0x1F8 / +0x1FC
//   * exact zero-edge comparisons (no +/-0.05 tolerance constants)
//
// 17763 is also deliberately a separate implementation family. Its PDB-verified
// ButtonBase::IsValidPointerPosition uses:
//   * size interface: this + 0x148
//   * cached pointer X/Y: this + 0x228 / +0x22C
//   * exact zero-edge comparisons (no +/-0.05 tolerance constants)
//
// Windows 10 1903 / 18362 is also separate: its ButtonBase already uses
// the modern private layout and +/-0.05 edge tolerance, while its
// QueryScalePercentage implementation differs from later modern Win10.
//
// The early Windows 11 code-generation family (22000 and sampled 22621)
// shares the modern private ButtonBase layout/edge semantics but remains
// separate from 26100-modern and 28000 QueryScalePercentage implementations.
//
// The 18362 and newer Win10/Win11 ButtonBase implementations sampled so far use:
//   * size interface: this + 0x160
//   * cached pointer X/Y: this + 0x240 / +0x244
//   * +/-0.05 DIP edge tolerance
//
// Unknown builds are accepted only when BOTH target functions uniquely match
// the same validated family. Unsupported implementations fail closed.
// -------------------------------------------------------------------------

static constexpr int XAML_FAMILY_WIN10_1507 = 1507;
static constexpr int XAML_FAMILY_WIN10_1511 = 1511;
static constexpr int XAML_FAMILY_WIN10_1607 = 1607;
static constexpr int XAML_FAMILY_WIN10_1703 = 1703;
static constexpr int XAML_FAMILY_WIN10_1709 = 1709;
static constexpr int XAML_FAMILY_WIN10_1803 = 1803;
static constexpr int XAML_FAMILY_WIN10_1809 = 1809;
static constexpr int XAML_FAMILY_WIN10_1903 = 1903;
static constexpr int XAML_FAMILY_WIN10_MODERN = 10;
static constexpr int XAML_FAMILY_WIN11_MODERN = 11;
static constexpr int XAML_FAMILY_WIN11_22000 = 22000;
static constexpr int XAML_FAMILY_WIN11_28000 = 28000;

struct KnownXamlBuild {
    PCWSTR name;
    DWORD timestamp;
    DWORD sizeOfImage;
    DWORD queryScalePercentageRva;
    DWORD buttonIsValidPointerPositionRva;
    int family;
};

static constexpr KnownXamlBuild KNOWN_XAML_BUILDS[] = {
    {
        L"Windows 10 1507 10240.16384",
        0x559F3E90,
        0x00FF7000,
        0x000E0670,
        0x0072E6AC,
        XAML_FAMILY_WIN10_1507,
    },
    {
        L"Windows 10 1507 10240.17071",
        0x57A17B0E,
        0x00FF6000,
        0x0013EC74,
        0x0072CDC8,
        XAML_FAMILY_WIN10_1507,
    },
    {
        L"Windows 10 1507 10240.17609",
        0x59AE2CD5,
        0x00FF6000,
        0x0013EC74,
        0x0072C868,
        XAML_FAMILY_WIN10_1507,
    },
    {
        L"Windows 10 1507 10240.17741",
        0x5A5BBA07,
        0x00FF7000,
        0x002C91F4,
        0x0072E124,
        XAML_FAMILY_WIN10_1507,
    },
    {
        L"Windows 10 1507 10240.17797",
        0x5A979C72,
        0x00FF7000,
        0x002C91F4,
        0x0072E46C,
        XAML_FAMILY_WIN10_1507,
    },
    {
        L"Windows 10 1507 10240.18036",
        0x5BD14608,
        0x00FF7000,
        0x002C91F4,
        0x0072D75C,
        XAML_FAMILY_WIN10_1507,
    },
    {
        L"Windows 10 1507 10240.18818",
        0x5FF7B5FD,
        0x00FF7000,
        0x002C93F4,
        0x0072D7BC,
        XAML_FAMILY_WIN10_1507,
    },
    {
        L"Windows 10 1507 10240.20345",
        0x655C1E69,
        0x00FF7000,
        0x002C26A4,
        0x0072DB6C,
        XAML_FAMILY_WIN10_1507,
    },
    {
        L"Windows 10 1507 10240.20401",
        0x65813288,
        0x00FF7000,
        0x002C26A4,
        0x0072DA4C,
        XAML_FAMILY_WIN10_1507,
    },
    {
        L"Windows 10 1507 10240.21072",
        0x6863817E,
        0x00FF7000,
        0x002C25D4,
        0x0072E00C,
        XAML_FAMILY_WIN10_1507,
    },
    {
        L"Windows 10 1511 10586.0",
        0x5632D920,
        0x01039000,
        0x001B68F0,
        0x00780F74,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.494",
        0x5775E900,
        0x01039000,
        0x001B6CD0,
        0x00780980,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.545",
        0x57A1BCA1,
        0x01039000,
        0x001B6CD0,
        0x00780980,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.589",
        0x57CF9AFF,
        0x0103A000,
        0x001B6CD0,
        0x007806B8,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.672",
        0x580EEB60,
        0x01039000,
        0x001B6CD0,
        0x00780614,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.839",
        0x58BA3979,
        0x01039000,
        0x001B6CD0,
        0x00780614,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.842",
        0x58CD6A71,
        0x01039000,
        0x001B6CD0,
        0x00780614,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.873",
        0x58D9F7D8,
        0x01039000,
        0x001B6CD0,
        0x00780614,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.916",
        0x59028EA2,
        0x0103A000,
        0x001B6CD0,
        0x00780F88,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.962",
        0x59328595,
        0x0103A000,
        0x001B6CD0,
        0x00780F88,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.965",
        0x5944C233,
        0x0103A000,
        0x001B6CD0,
        0x00780F88,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.1007",
        0x595F2DE2,
        0x0103A000,
        0x001B6CD0,
        0x00780F88,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.1358",
        0x5A5BD2D8,
        0x0103A000,
        0x001B72C0,
        0x00781344,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.1417",
        0x5A7E77F9,
        0x0103B000,
        0x001B7940,
        0x00780B34,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1511 10586.1478",
        0x5A979927,
        0x0103B000,
        0x001B72C0,
        0x00781488,
        XAML_FAMILY_WIN10_1511,
    },
    {
        L"Windows 10 1607 14393.0",
        0x57899A84,
        0x0106B000,
        0x002A386C,
        0x00747A54,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.594",
        0x5850CCD3,
        0x0106C000,
        0x001B492C,
        0x00748674,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.726",
        0x58785A51,
        0x0106E000,
        0x002AD7DC,
        0x00747ED8,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.1378",
        0x594A17BE,
        0x0106E000,
        0x002AD7DC,
        0x00748EE8,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.1715",
        0x59B0D4D8,
        0x0106E000,
        0x002AD70C,
        0x007480B4,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.2125",
        0x5A9909E5,
        0x0106F000,
        0x002ADD8C,
        0x007491A4,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.2758",
        0x5C304C37,
        0x0106F000,
        0x002ADD8C,
        0x00748FC8,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.4104",
        0x5FC86EE6,
        0x0106F000,
        0x002ADD8C,
        0x007495B8,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.4169",
        0x5FF78F41,
        0x0106F000,
        0x002ADF8C,
        0x00748928,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.4467",
        0x60BB0016,
        0x0106F000,
        0x002ADF8C,
        0x00747C98,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.6451",
        0x6545B7F2,
        0x0106F000,
        0x002ADF8C,
        0x00747E08,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.6529",
        0x656A98E0,
        0x0106F000,
        0x002ADF8C,
        0x00749B50,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.6795",
        0x65DD6811,
        0x0106F000,
        0x002ADF8C,
        0x00749064,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1607 14393.9507",
        0x6A979670,
        0x0106F000,
        0x002ADE1C,
        0x007495C0,
        XAML_FAMILY_WIN10_1607,
    },
    {
        L"Windows 10 1703 15063.540",
        0xD330C8C8,
        0x01096000,
        0x003684E0,
        0x007A6A7C,
        XAML_FAMILY_WIN10_1703,
    },
    {
        L"Windows 10 1703 15063.608",
        0xCADDDD38,
        0x01097000,
        0x00368850,
        0x007A6494,
        XAML_FAMILY_WIN10_1703,
    },
    {
        L"Windows 10 1703 15063.1418",
        0xFB6A8D52,
        0x01098000,
        0x00242830,
        0x007A8328,
        XAML_FAMILY_WIN10_1703,
    },
    {
        L"Windows 10 1703 15063.1781",
        0xAC262A76,
        0x01098000,
        0x00242830,
        0x007A86E8,
        XAML_FAMILY_WIN10_1703,
    },
    {
        L"Windows 10 1703 15063.2679",
        0xD8CA3CE9,
        0x01097000,
        0x002429F0,
        0x007A80B4,
        XAML_FAMILY_WIN10_1703,
    },
    {
        L"Windows 10 1709 16299.214",
        0xBD22870B,
        0x01067000,
        0x002C108C,
        0x00760C3C,
        XAML_FAMILY_WIN10_1709,
    },
    {
        L"Windows 10 1709 16299.492",
        0x69544390,
        0x01067000,
        0x002C138C,
        0x007605DC,
        XAML_FAMILY_WIN10_1709,
    },
    {
        L"Windows 10 1709 16299.755",
        0x4E08E87C,
        0x01067000,
        0x002C138C,
        0x007609DC,
        XAML_FAMILY_WIN10_1709,
    },
    {
        L"Windows 10 1709 16299.936",
        0xEFE4C827,
        0x0106A000,
        0x002C12EC,
        0x007619BC,
        XAML_FAMILY_WIN10_1709,
    },
    {
        L"Windows 10 1709 16299.1653",
        0x7486A7A4,
        0x01069000,
        0x002C134C,
        0x0076239C,
        XAML_FAMILY_WIN10_1709,
    },
    {
        L"Windows 10 1709 16299.1685",
        0x216D2C11,
        0x0106A000,
        0x002C134C,
        0x00761A9C,
        XAML_FAMILY_WIN10_1709,
    },
    {
        L"Windows 10 1803 17134.1",
        0x46278CB1,
        0x00FDC000,
        0x000E44A0,
        0x007160C8,
        XAML_FAMILY_WIN10_1803,
    },
    {
        L"Windows 10 1803 17134.81",
        0x4F4899F8,
        0x00FDC000,
        0x000E44C0,
        0x00716E18,
        XAML_FAMILY_WIN10_1803,
    },
    {
        L"Windows 10 1803 17134.376",
        0x35659A8D,
        0x00FDC000,
        0x000E44C0,
        0x007172A8,
        XAML_FAMILY_WIN10_1803,
    },
    {
        L"Windows 10 1803 17134.556",
        0xD94C4E1E,
        0x00FDD000,
        0x000E4480,
        0x007183E8,
        XAML_FAMILY_WIN10_1803,
    },
    {
        L"Windows 10 1803 17134.799",
        0x4C4AD50A,
        0x00FDD000,
        0x000E4480,
        0x00718328,
        XAML_FAMILY_WIN10_1803,
    },
    {
        L"Windows 10 1803 17134.1038",
        0xF4B8BED8,
        0x00FDE000,
        0x000E4480,
        0x00717E58,
        XAML_FAMILY_WIN10_1803,
    },
    {
        L"Windows 10 1803 17134.1098",
        0x5B0689CD,
        0x00FDE000,
        0x000E44E0,
        0x00718368,
        XAML_FAMILY_WIN10_1803,
    },
    {
        L"Windows 10 1809 / LTSC 2019 17763.1",
        0xA69151FC,
        0x010C0000,
        0x00127B94,
        0x0077C7A4,
        XAML_FAMILY_WIN10_1809,
    },
    {
        L"Windows 10 1809 17763.379",
        0x1DCF1D23,
        0x010C1000,
        0x00127B14,
        0x0077D614,
        XAML_FAMILY_WIN10_1809,
    },
    {
        L"Windows 10 1809 17763.2090",
        0x59F836E4,
        0x010B9000,
        0x002857C4,
        0x007767E4,
        XAML_FAMILY_WIN10_1809,
    },
    {
        L"Windows 10 1903 18362.1",
        0x59570CF9,
        0x010FD000,
        0x000B26FC,
        0x0078F678,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.145",
        0x4AD4B406,
        0x010FD000,
        0x000B26FC,
        0x0078F678,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.207",
        0xBF30FB52,
        0x010FD000,
        0x000B26FC,
        0x0078F678,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.267",
        0x3E916038,
        0x010FD000,
        0x000B303C,
        0x0078F648,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.295",
        0x1A3CCF7C,
        0x010FD000,
        0x000B303C,
        0x0078F648,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.329",
        0x39F9435F,
        0x010FD000,
        0x000B303C,
        0x0078FC48,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.356",
        0x0825B5B0,
        0x010FD000,
        0x000B303C,
        0x0078FC48,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.387",
        0xD6182D63,
        0x010FD000,
        0x000B2FAC,
        0x0078FD18,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.449",
        0xB90DB728,
        0x010FD000,
        0x000B2FAC,
        0x0078FE18,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.657",
        0xDA02D3EB,
        0x010FD000,
        0x000B2FAC,
        0x0078FE18,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.752",
        0x458B530D,
        0x010FE000,
        0x00161E1C,
        0x007902C8,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.815",
        0x9FA806F2,
        0x010FE000,
        0x00161E7C,
        0x00790518,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.959",
        0xDE2822EB,
        0x010FE000,
        0x00161E7C,
        0x007904E8,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.997",
        0xE85F9394,
        0x010FE000,
        0x00161E7C,
        0x007904E8,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1110",
        0x8161C5C4,
        0x010FE000,
        0x000B2FAC,
        0x00790238,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1139",
        0x97B8F732,
        0x010FE000,
        0x000B2FAC,
        0x00790238,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1171",
        0x3065FCA2,
        0x010FE000,
        0x000B2FAC,
        0x007901B8,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1316",
        0xAEFA6537,
        0x010FE000,
        0x000B309C,
        0x0078FBA8,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1350",
        0xE38B3B0E,
        0x010FE000,
        0x0016262C,
        0x00790648,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1411",
        0xF4E8C8EA,
        0x010FE000,
        0x0016262C,
        0x00790648,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1440",
        0xBD0593DB,
        0x010FE000,
        0x0016262C,
        0x00790648,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1474",
        0xCE787F95,
        0x010FF000,
        0x0016273C,
        0x007907D8,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1556",
        0xB3747CC2,
        0x010FF000,
        0x0016273C,
        0x007907D8,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1593",
        0xFB6A0FE3,
        0x010FF000,
        0x00161F5C,
        0x00790258,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1621",
        0x0F06332E,
        0x010FF000,
        0x00161F5C,
        0x00790258,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1645",
        0xE7EA9551,
        0x010FF000,
        0x00161F5C,
        0x00790258,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 1903 18362.1714",
        0xB81C2D40,
        0x010FE000,
        0x00161E9C,
        0x0078FA78,
        XAML_FAMILY_WIN10_1903,
    },
    {
        L"Windows 10 22H2 19045.7725",
        0xBDC32168,
        0x010C0000,
        0x0017BC9C,
        0x007A0A0C,
        XAML_FAMILY_WIN10_MODERN,
    },
    {
        L"Windows 11 21H2 22000.318",
        0xE4273F56,
        0x01209000,
        0x00277820,
        0x008B50FC,
        XAML_FAMILY_WIN11_22000,
    },
    {
        L"Windows 11 24H2/25H2-shared XAML reference",
        0x2C3BCE81,
        0x01108000,
        0x0043C864,
        0x0060CB0C,
        XAML_FAMILY_WIN11_MODERN,
    },
    {
        L"Windows 11 26H1 28000.2525",
        0x157B771E,
        0x01128000,
        0x00397B28,
        0x00554960,
        XAML_FAMILY_WIN11_28000,
    },
    {
        L"Windows 11 26H1 28000.2605",
        0xE6DC0A65,
        0x01129000,
        0x003917E8,
        0x00554510,
        XAML_FAMILY_WIN11_28000,
    },
};

// Long masked signatures. '?' bytes are relocations, relative branches/calls,
// and other build-dependent displacements. The 17763 signatures are derived
// from the PDB-resolved 17763.2090 functions and were independently matched,
// uniquely, against 17763.1 and 17763.379 harvested DLLs.
// Windows 10 1507 / 10240 shares QueryScalePercentage with the later
// 10586/14393/15063/16299 implementation shape, but ButtonBase has an older
// PDB-verified private layout. 10240.21072 anchors this body at RVA
// 0x0072E00C with a PDB procedure length of 0xE6. The conservative signature
// below matched uniquely across all 10 boundary-oriented sampled payloads.
//   * size interface: this + 0x50
//   * cached pointer X/Y: this + 0x190 / +0x194
//   * exact zero-edge comparisons (no +/-0.05 tolerance constants)
static const BYTE PAT_BUTTON_WIN10_1507[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x00,
    0x00, 0x00, 0x00, 0x4C, 0x8D, 0x71, 0x50, 0x40, 0x88, 0x3A, 0x49, 0x8B, 0x06, 0x48, 0x8B, 0x58,
    0x68, 0x48, 0x8B, 0xCB, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x48, 0x49,
    0x8B, 0xCE, 0xFF, 0xD3, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x74, 0x49, 0x8B, 0x06, 0x48, 0x8B, 0x58,
    0x70, 0x48, 0x8B, 0xCB, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x50, 0x49,
    0x8B, 0xCE, 0xFF, 0xD3, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x4B, 0xF3, 0x0F, 0x10, 0x8D, 0x90, 0x01,
    0x00, 0x00, 0x0F, 0x57, 0xD2, 0x0F, 0x5A, 0xC1, 0x66, 0x0F, 0x2F, 0xC2, 0x72, 0x32, 0xF2, 0x0F,
    0x10, 0x44, 0x24, 0x48, 0x0F, 0x5A, 0xC9, 0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x23, 0xF3, 0x0F, 0x10,
    0x8D, 0x94, 0x01, 0x00, 0x00, 0x0F, 0x5A, 0xC1, 0x66, 0x0F, 0x2F, 0xC2, 0x72, 0x12, 0xF2, 0x0F,
    0x10, 0x44, 0x24, 0x50, 0x0F, 0x5A, 0xC9, 0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x03, 0x40, 0xB7, 0x01,
    0x40, 0x88, 0x3E, 0xEB, 0x1C, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x13, 0x8B, 0xC8,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x0A, 0xBB, 0x03, 0x40, 0x00, 0x80, 0xE8, 0x00, 0x00, 0x00,
    0x00, 0x48, 0x8B, 0x6C, 0x24, 0x58, 0x8B, 0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x40, 0x48, 0x83, 0xC4,
    0x20, 0x41, 0x5E, 0x5F, 0x5E, 0xC3,
};
static constexpr char MASK_BUTTON_WIN10_1507[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxx"
    "x????xxxxxxxx????xxxxxxxxxxxxxxxxxxxxx";

// Windows 10 1511 / 10586 shares QueryScalePercentage with the later
// 14393/15063/16299 implementation shape, but ButtonBase has its own
// PDB-verified private layout and older call sequence. 10586.1478 anchors this
// body at RVA 0x00781488 with a PDB procedure length of 0xE9.
//   * size interface: this + 0x110
//   * cached pointer X/Y: this + 0x1B0 / +0x1B4
//   * exact zero-edge comparisons (no +/-0.05 tolerance constants)
static const BYTE PAT_BUTTON_WIN10_1511[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x00,
    0x00, 0x00, 0x00, 0x4C, 0x8D, 0xB1, 0x10, 0x01, 0x00, 0x00, 0x40, 0x88, 0x3A, 0x49, 0x8B, 0x06,
    0x48, 0x8B, 0x58, 0x68, 0x48, 0x8B, 0xCB, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x54,
    0x24, 0x48, 0x49, 0x8B, 0xCE, 0xFF, 0xD3, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x74, 0x49, 0x8B, 0x06,
    0x48, 0x8B, 0x58, 0x70, 0x48, 0x8B, 0xCB, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x54,
    0x24, 0x50, 0x49, 0x8B, 0xCE, 0xFF, 0xD3, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x4B, 0xF3, 0x0F, 0x10,
    0x8D, 0xB0, 0x01, 0x00, 0x00, 0x0F, 0x57, 0xD2, 0x0F, 0x5A, 0xC1, 0x66, 0x0F, 0x2F, 0xC2, 0x72,
    0x32, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x48, 0x0F, 0x5A, 0xC9, 0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x23,
    0xF3, 0x0F, 0x10, 0x8D, 0xB4, 0x01, 0x00, 0x00, 0x0F, 0x5A, 0xC1, 0x66, 0x0F, 0x2F, 0xC2, 0x72,
    0x12, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x50, 0x0F, 0x5A, 0xC9, 0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x03,
    0x40, 0xB7, 0x01, 0x40, 0x88, 0x3E, 0xEB, 0x1C, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB,
    0x13, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x0A, 0xBB, 0x03, 0x40, 0x00, 0x80, 0xE8,
    0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x6C, 0x24, 0x58, 0x8B, 0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x40,
    0x48, 0x83, 0xC4, 0x20, 0x41, 0x5E, 0x5F, 0x5E, 0xC3,
};
static constexpr char MASK_BUTTON_WIN10_1511[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????x"
    "xxxx????xxxxxxxx????xxxxxxxxxxxxxxxxxxxxx";

// Windows 10 1607 / 14393 shares QueryScalePercentage with the later
// 15063/16299 implementation shape, but ButtonBase has its own PDB-verified
// private layout. 14393.9507 anchors this body at RVA 0x007495C0 with a PDB
// procedure length of 0xDF. The conservative semantic signature below matched
// uniquely across all 14 SHA-verified sampled 14393 payloads.
//   * size interface: this + 0x118
//   * cached pointer X/Y: this + 0x1C0 / +0x1C4
//   * exact zero-edge comparisons (no +/-0.05 tolerance constants)
static const BYTE PAT_BUTTON_WIN10_1607[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x00,
    0x00, 0x00, 0x00, 0x4C, 0x8D, 0xB1, 0x18, 0x01, 0x00, 0x00, 0x40, 0x88, 0x3A, 0x49, 0x8B, 0x06,
    0x48, 0x8D, 0x54, 0x24, 0x48, 0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x68, 0xFF, 0x15, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x6F, 0x49, 0x8B, 0x06, 0x48, 0x8D, 0x54, 0x24, 0x50,
    0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x70, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85,
    0xC0, 0x78, 0x4B, 0xF3, 0x0F, 0x10, 0x8D, 0xC0, 0x01, 0x00, 0x00, 0x0F, 0x57, 0xD2, 0x0F, 0x5A,
    0xC1, 0x66, 0x0F, 0x2F, 0xC2, 0x72, 0x32, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x48, 0x0F, 0x5A, 0xC9,
    0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x23, 0xF3, 0x0F, 0x10, 0x8D, 0xC4, 0x01, 0x00, 0x00, 0x0F, 0x5A,
    0xC1, 0x66, 0x0F, 0x2F, 0xC2, 0x72, 0x12, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x50, 0x0F, 0x5A, 0xC9,
    0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x03, 0x40, 0xB7, 0x01, 0x40, 0x88, 0x3E, 0xEB, 0x1C, 0x8B, 0xC8,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x13, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x0A,
    0xBB, 0x03, 0x40, 0x00, 0x80, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x6C, 0x24, 0x58, 0x8B,
    0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x40, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x5E, 0x5F, 0x5E, 0xC3,
};
static constexpr char MASK_BUTTON_WIN10_1607[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxx????xxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxx????xx"
    "xxxxxx????xxxxxxxxxxxxxxxxxxxxx";

// Windows 10 1703 / 15063 keeps the same QueryScalePercentage body as
// the validated 1709 family, but ButtonBase predates the 16299 private layout.
// The Button signature below is PDB-anchored at 15063.1418
// (RVA 0x7A8328, PDB function length 0xDF) and matched uniquely across all
// ten sampled 15063 payloads. Its private layout is:
//   * size interface: this + 0x110
//   * cached pointer X/Y: this + 0x1C0 / +0x1C4
//   * exact zero-edge comparisons (no +/-0.05 tolerance constants)
static const BYTE PAT_BUTTON_WIN10_1703[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x00,
    0x00, 0x00, 0x00, 0x4C, 0x8D, 0xB1, 0x10, 0x01, 0x00, 0x00, 0x40, 0x88, 0x3A, 0x49, 0x8B, 0x06,
    0x48, 0x8D, 0x54, 0x24, 0x48, 0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x68, 0xFF, 0x15, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x6F, 0x49, 0x8B, 0x06, 0x48, 0x8D, 0x54, 0x24, 0x50,
    0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x70, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85,
    0xC0, 0x78, 0x4B, 0xF3, 0x0F, 0x10, 0x8D, 0xC0, 0x01, 0x00, 0x00, 0x0F, 0x57, 0xD2, 0x0F, 0x5A,
    0xC1, 0x66, 0x0F, 0x2F, 0xC2, 0x72, 0x32, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x48, 0x0F, 0x5A, 0xC9,
    0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x23, 0xF3, 0x0F, 0x10, 0x8D, 0xC4, 0x01, 0x00, 0x00, 0x0F, 0x5A,
    0xC1, 0x66, 0x0F, 0x2F, 0xC2, 0x72, 0x12, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x50, 0x0F, 0x5A, 0xC9,
    0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x03, 0x40, 0xB7, 0x01, 0x40, 0x88, 0x3E, 0xEB, 0x1C, 0x8B, 0xC8,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x13, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x0A,
    0xBB, 0x03, 0x40, 0x00, 0x80, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x6C, 0x24, 0x58, 0x8B,
    0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x40, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x5E, 0x5F, 0x5E, 0xC3,
};
static constexpr char MASK_BUTTON_WIN10_1703[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxx????xxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxx????xx"
    "xxxxxx????xxxxxxxxxxxxxxxxxxxxx";

// Windows 10 1709 / 16299 has its own ButtonBase private layout.
// These signatures are anchored by the PDB-resolved 16299.1685 targets
// (Query RVA 0x2C134C, Button RVA 0x761A9C) and matched uniquely across all
// ten harvested 16299 payloads. QueryScalePercentage is structurally shared
// by older builds too (including the sampled 15063 family), so classification
// still requires the matching 1709 ButtonBase signature and private layout.
static const BYTE PAT_QUERY_WIN10_1709[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x64, 0x24, 0x48, 0x00,
    0x48, 0x8B, 0xFA, 0x48, 0x8D, 0x54, 0x24, 0x48, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85,
    0xC0, 0x78, 0x32, 0x48, 0x8B, 0x4C, 0x24, 0x48, 0x48, 0x8D, 0x54, 0x24, 0x40, 0xFF, 0x15, 0x00,
    0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x13, 0x8B, 0x44, 0x24, 0x40, 0x89, 0x07, 0x8B,
    0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0x8B, 0xC8, 0xE8, 0x00,
    0x00, 0x00, 0x00, 0xEB, 0xEA, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0xE1, 0xCC,
};
static constexpr char MASK_QUERY_WIN10_1709[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxx????xxx";

static const BYTE PAT_BUTTON_WIN10_1709[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x00,
    0x00, 0x00, 0x00, 0x4C, 0x8D, 0xB1, 0x20, 0x01, 0x00, 0x00, 0x40, 0x88, 0x3A, 0x49, 0x8B, 0x06,
    0x48, 0x8D, 0x54, 0x24, 0x48, 0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x68, 0xFF, 0x15, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x6F, 0x49, 0x8B, 0x06, 0x48, 0x8D, 0x54, 0x24, 0x50,
    0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x70, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85,
    0xC0, 0x78, 0x4B, 0xF3, 0x0F, 0x10, 0x8D, 0xE0, 0x01, 0x00, 0x00, 0x0F, 0x57, 0xD2, 0x0F, 0x5A,
    0xC1, 0x66, 0x0F, 0x2F, 0xC2, 0x72, 0x32, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x48, 0x0F, 0x5A, 0xC9,
    0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x23, 0xF3, 0x0F, 0x10, 0x8D, 0xE4, 0x01, 0x00, 0x00, 0x0F, 0x5A,
    0xC1, 0x66, 0x0F, 0x2F, 0xC2, 0x72, 0x12, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x50, 0x0F, 0x5A, 0xC9,
    0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x03, 0x40, 0xB7, 0x01, 0x40, 0x88, 0x3E, 0xEB, 0x1C, 0x8B, 0xC8,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x13, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x0A,
    0xBB, 0x03, 0x40, 0x00, 0x80, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x6C, 0x24, 0x58, 0x8B,
    0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x40, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x5E, 0x5F, 0x5E, 0xC3,
};
static constexpr char MASK_BUTTON_WIN10_1709[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxx????xxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxx????xx"
    "xxxxxx????xxxxxxxxxxxxxxxxxxxxx";

// Windows 10 1803 / 17134 has its own ButtonBase private layout.
// The pattern below is PDB-resolved from 17134.1098 and was independently
// matched uniquely against all seven harvested x64 17134 builds (.1 through
// .1098). QueryScalePercentage shares the older Win10 implementation shape.
static const BYTE PAT_BUTTON_WIN10_1803[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x00,
    0x00, 0x00, 0x00, 0x4C, 0x8D, 0xB1, 0x38, 0x01, 0x00, 0x00, 0x40, 0x88, 0x3A, 0x49, 0x8B, 0x06,
    0x48, 0x8D, 0x54, 0x24, 0x48, 0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x68, 0xFF, 0x15, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x69, 0x49, 0x8B, 0x06, 0x48, 0x8D, 0x54, 0x24, 0x50,
    0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x70, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85,
    0xC0, 0x78, 0x45, 0xF3, 0x0F, 0x10, 0x8D, 0xF8, 0x01, 0x00, 0x00, 0x0F, 0x57, 0xD2, 0x0F, 0x5A,
    0xC9, 0x66, 0x0F, 0x2F, 0xCA, 0x72, 0x2C, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x48, 0x66, 0x0F, 0x2F,
    0xC1, 0x72, 0x20, 0xF3, 0x0F, 0x10, 0x8D, 0xFC, 0x01, 0x00, 0x00, 0x0F, 0x5A, 0xC9, 0x66, 0x0F,
    0x2F, 0xCA, 0x72, 0x0F, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x50, 0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x03,
    0x40, 0xB7, 0x01, 0x40, 0x88, 0x3E, 0xEB, 0x1C, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB,
    0x13, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x0A, 0xBB, 0x03, 0x40, 0x00, 0x80, 0xE8,
    0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x6C, 0x24, 0x58, 0x8B, 0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x40,
    0x48, 0x83, 0xC4, 0x20, 0x41, 0x5E, 0x5F, 0x5E, 0xC3,
};
static constexpr char MASK_BUTTON_WIN10_1803[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxx????xxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxx????xxxxxxxx"
    "????xxxxxxxxxxxxxxxxxxxxx";

static const BYTE PAT_QUERY_WIN10_1809[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0x64, 0x24, 0x48, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x48,
    0x48, 0x8B, 0xCB, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x0F, 0x88, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0x4C, 0x24, 0x48, 0x48, 0x8D, 0x54, 0x24, 0x40, 0x83, 0x64, 0x24, 0x40,
    0x00, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x0F, 0x88, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0x44, 0x24, 0x40, 0x89, 0x07, 0x33, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83,
    0xC4, 0x20, 0x5F, 0xC3,
};
static constexpr char MASK_QUERY_WIN10_1809[] =
    "xxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx????xxxxxx????xxxxxxxxxxxxxxxxx????xxxxxx????xxxxxxxxxxxxxxx"
    "xxxx";

static const BYTE PAT_BUTTON_WIN10_1809[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x00,
    0x00, 0x00, 0x00, 0x4C, 0x8D, 0xB1, 0x48, 0x01, 0x00, 0x00, 0x40, 0x88, 0x3A, 0x49, 0x8B, 0x06,
    0x48, 0x8D, 0x54, 0x24, 0x48, 0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x68, 0xFF, 0x15, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x69, 0x49, 0x8B, 0x06, 0x48, 0x8D, 0x54, 0x24, 0x50,
    0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x70, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85,
    0xC0, 0x78, 0x45, 0xF3, 0x0F, 0x10, 0x8D, 0x28, 0x02, 0x00, 0x00, 0x0F, 0x57, 0xD2, 0x0F, 0x5A,
    0xC9, 0x66, 0x0F, 0x2F, 0xCA, 0x72, 0x2C, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x48, 0x66, 0x0F, 0x2F,
    0xC1, 0x72, 0x20, 0xF3, 0x0F, 0x10, 0x8D, 0x2C, 0x02, 0x00, 0x00, 0x0F, 0x5A, 0xC9, 0x66, 0x0F,
    0x2F, 0xCA, 0x72, 0x0F, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x50, 0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x03,
    0x40, 0xB7, 0x01, 0x40, 0x88, 0x3E, 0xEB, 0x1C, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB,
    0x13, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x0A, 0xBB, 0x03, 0x40, 0x00, 0x80, 0xE8,
    0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x6C, 0x24, 0x58, 0x8B, 0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x40,
    0x48, 0x83, 0xC4, 0x20, 0x41, 0x5E, 0x5F, 0x5E, 0xC3,
};
static constexpr char MASK_BUTTON_WIN10_1809[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxx????xxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxx????xxxxxxxx"
    "????xxxxxxxxxxxxxxxxxxxxx";

// Windows 10 1903 / 18362 has the modern ButtonBase private layout and
// +/-0.05 edge semantics, but QueryScalePercentage still uses its own
// implementation shape. The query pattern was PDB-resolved independently at
// 18362.1 and 18362.1714 and matched uniquely across all 28 harvested payloads.
static const BYTE PAT_QUERY_WIN10_1903[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0x64, 0x24, 0x48, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x48,
    0x48, 0x8B, 0xCB, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x2E, 0x48, 0x8B,
    0x4C, 0x24, 0x48, 0x48, 0x8D, 0x54, 0x24, 0x40, 0x83, 0x64, 0x24, 0x40, 0x00, 0xFF, 0x15, 0x00,
    0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x1E, 0x8B, 0x44, 0x24, 0x40, 0x89, 0x07, 0x33,
    0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0x8B, 0xCB, 0xE8, 0x00,
    0x00, 0x00, 0x00, 0x8B, 0xC3, 0xEB, 0xEA, 0x8B, 0xCB, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0xF3,
};
static constexpr char MASK_QUERY_WIN10_1903[] =
    "xxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxx?"
    "???xxxxxxx????xx";

static const BYTE PAT_QUERY_WIN10[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0x64, 0x24, 0x48, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x48,
    0x48, 0x8B, 0xCB, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x35, 0x48, 0x8B,
    0x4C, 0x24, 0x48, 0x48, 0x8D, 0x54, 0x24, 0x40, 0x83, 0x64, 0x24, 0x40, 0x00, 0x48, 0xFF, 0x15,
    0x00, 0x00, 0x00, 0x00, 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x1F, 0x8B,
    0x44, 0x24, 0x40, 0x89, 0x07,
};
static constexpr char MASK_QUERY_WIN10[] =
    "xxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxx";

static const BYTE PAT_QUERY_WIN11[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0x64, 0x24, 0x48, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x48,
    0x48, 0x8B, 0xCB, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x78, 0x29, 0x48, 0x8B, 0x4C, 0x24,
    0x48, 0x48, 0x8D, 0x54, 0x24, 0x40, 0x83, 0x64, 0x24, 0x40, 0x00, 0x48, 0xFF, 0x15, 0x00, 0x00,
    0x00, 0x00, 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x14, 0x8B, 0x44, 0x24,
    0x40, 0x89, 0x07,
};
static constexpr char MASK_QUERY_WIN11[] =
    "xxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxx";


// Windows 11 26H1 / 28000 keeps the modern Win11 ButtonBase implementation
// and private layout, but QueryScalePercentage changed again. This signature
// is PDB-resolved from 28000.2605 (RVA 0x3917E8, function length 0x72) and
// independently matches 28000.2525 at RVA 0x397B28. Only build-dependent
// rel32 / RIP-relative displacement fields are wildcarded.
static const BYTE PAT_QUERY_WIN11_28000[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x48, 0x48, 0xC7, 0x44, 0x24, 0x48, 0x00,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x78, 0x2C, 0x48,
    0x8B, 0x4C, 0x24, 0x48, 0x48, 0x8D, 0x54, 0x24, 0x40, 0xC7, 0x44, 0x24, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x48, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x8B, 0xD8, 0x85,
    0xC0, 0x78, 0x14, 0x8B, 0x44, 0x24, 0x40, 0x89, 0x07, 0x33, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x30,
    0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0xCC, 0x8B, 0xCB, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xC3,
    0xEB, 0xE9,
};
static constexpr char MASK_QUERY_WIN11_28000[] =
    "xxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxx????xxxx";

static const BYTE PAT_BUTTON_WIN10[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x00,
    0x00, 0x00, 0x00, 0x4C, 0x8D, 0xB1, 0x60, 0x01, 0x00, 0x00, 0x40, 0x88, 0x3A, 0x49, 0x8B, 0x06,
    0x48, 0x8D, 0x54, 0x24, 0x48, 0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x68, 0xFF, 0x15, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x00, 0x49, 0x8B, 0x06, 0x48, 0x8D, 0x54, 0x24, 0x50,
    0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x70, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85,
    0xC0, 0x78, 0x00, 0xF3, 0x0F, 0x10, 0x8D, 0x40, 0x02, 0x00, 0x00, 0xF2, 0x0F, 0x10, 0x15, 0x00,
    0x00, 0x00, 0x00, 0x0F, 0x5A, 0xC9, 0x66, 0x0F, 0x2F, 0xCA, 0x72, 0x3C, 0xF2, 0x0F, 0x10, 0x44,
    0x24, 0x48, 0xF2, 0x0F, 0x10, 0x1D, 0x00, 0x00, 0x00, 0x00, 0xF2, 0x0F, 0x58, 0xC3, 0x66, 0x0F,
    0x2F, 0xC1, 0x72, 0x24, 0xF3, 0x0F, 0x10, 0x8D, 0x44, 0x02, 0x00, 0x00, 0x0F, 0x5A, 0xC9, 0x66,
    0x0F, 0x2F, 0xCA, 0x72, 0x13, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x50, 0xF2, 0x0F, 0x58, 0xC3, 0x66,
    0x0F, 0x2F, 0xC1, 0x72, 0x03, 0x40, 0xB7, 0x01, 0x40, 0x88, 0x3E,
};
static constexpr char MASK_BUTTON_WIN10[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxx?xxxxxxxxxxxxxxxxx????xxxxx?xxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

static const BYTE PAT_BUTTON_WIN11[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48,
    0x83, 0xEC, 0x20, 0x0F, 0x57, 0xC0, 0x33, 0xFF, 0xF2, 0x0F, 0x11, 0x40, 0x10, 0x48, 0x8B, 0xF2,
    0xF2, 0x0F, 0x11, 0x40, 0x18, 0x48, 0x8B, 0xE9, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x00, 0x00, 0x00,
    0x00, 0x4C, 0x8D, 0xB1, 0x60, 0x01, 0x00, 0x00, 0x40, 0x88, 0x3A, 0x49, 0x8B, 0x06, 0x48, 0x8D,
    0x54, 0x24, 0x48, 0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x68, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B,
    0xD8, 0x85, 0xC0, 0x0F, 0x88, 0x00, 0x00, 0x00, 0x00, 0x49, 0x8B, 0x06, 0x48, 0x8D, 0x54, 0x24,
    0x50, 0x49, 0x8B, 0xCE, 0x48, 0x8B, 0x40, 0x70, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85,
    0xC0, 0x0F, 0x88, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x8D, 0x40, 0x02, 0x00, 0x00, 0xF2,
    0x0F, 0x10, 0x15, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x5A, 0xC9, 0x66, 0x0F, 0x2F, 0xCA, 0x72, 0x3C,
    0xF2, 0x0F, 0x10, 0x44, 0x24, 0x48, 0xF2, 0x0F, 0x10, 0x1D, 0x00, 0x00, 0x00, 0x00, 0xF2, 0x0F,
    0x58, 0xC3, 0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x24, 0xF3, 0x0F, 0x10, 0x8D, 0x44, 0x02, 0x00, 0x00,
    0x0F, 0x5A, 0xC9, 0x66, 0x0F, 0x2F, 0xCA, 0x72, 0x13, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x50, 0xF2,
    0x0F, 0x58, 0xC3, 0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x03, 0x40, 0xB7, 0x01, 0x40, 0x88, 0x3E,
};
static constexpr char MASK_BUTTON_WIN11[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxx????xxxxxxxxxxxxxxxx????xxxxxx????xxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

// Windows 11 21H2 / 22000 uses the modern ButtonBase private layout and
// +/-0.05 edge semantics, but its code generation is distinct from both
// newer Win11 and modern Win10. In particular, ButtonBase carries early
// Win11/XFG call-site instrumentation, so keep it in a separate family.
static const BYTE PAT_QUERY_WIN11_22000[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0x64, 0x24, 0x48, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x48,
    0x48, 0x8B, 0xCB, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x78, 0x00, 0x48, 0x8B, 0x4C, 0x24,
    0x48, 0x48, 0x8D, 0x54, 0x24, 0x40, 0x83, 0x64, 0x24, 0x40, 0x00, 0x48, 0xFF, 0x15, 0x00, 0x00,
    0x00, 0x00, 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x0F, 0x88, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0x44, 0x24, 0x40, 0x89, 0x07, 0x33, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83,
    0xC4, 0x20, 0x5F, 0xC3,
};
static constexpr char MASK_QUERY_WIN11_22000[] =
    "xxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx????xxx?xxxxxxxxxxxxxxxxxx????xxxxxxxxxxx????xxxxxxxxxxxxxxx"
    "xxxx";

static const BYTE PAT_BUTTON_WIN11_22000[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x00,
    0x00, 0x00, 0x00, 0x4C, 0x8D, 0xB1, 0x60, 0x01, 0x00, 0x00, 0x40, 0x88, 0x3A, 0x49, 0x8B, 0x06,
    0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x40, 0x68, 0x48, 0x8D,
    0x54, 0x24, 0x48, 0x49, 0x8B, 0xCE, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0,
    0x0F, 0x88, 0x00, 0x00, 0x00, 0x00, 0x49, 0x8B, 0x06, 0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0x40, 0x70, 0x48, 0x8D, 0x54, 0x24, 0x50, 0x49, 0x8B, 0xCE, 0xFF,
    0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD8, 0x85, 0xC0, 0x78, 0x00, 0xF3, 0x0F, 0x10, 0x8D, 0x40,
    0x02, 0x00, 0x00, 0xF2, 0x0F, 0x10, 0x15, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x5A, 0xC9, 0x66, 0x0F,
    0x2F, 0xCA, 0x72, 0x3C, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x48, 0xF2, 0x0F, 0x10, 0x1D, 0x00, 0x00,
    0x00, 0x00, 0xF2, 0x0F, 0x58, 0xC3, 0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x24, 0xF3, 0x0F, 0x10, 0x8D,
    0x44, 0x02, 0x00, 0x00, 0x0F, 0x5A, 0xC9, 0x66, 0x0F, 0x2F, 0xCA, 0x72, 0x13, 0xF2, 0x0F, 0x10,
    0x44, 0x24, 0x50, 0xF2, 0x0F, 0x58, 0xC3, 0x66, 0x0F, 0x2F, 0xC1, 0x72, 0x03, 0x40, 0xB7, 0x01,
    0x40, 0x88, 0x3E,
};
static constexpr char MASK_BUTTON_WIN11_22000[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx????????xxxxxxxxxxxxxx????xxxxxx????xxxxx?????"
    "???xxxxxxxxxxxxxx????xxxxx?xxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxx";

static_assert(ARRAYSIZE(MASK_BUTTON_WIN10_1507) - 1 == ARRAYSIZE(PAT_BUTTON_WIN10_1507));
static_assert(ARRAYSIZE(MASK_BUTTON_WIN10_1511) - 1 == ARRAYSIZE(PAT_BUTTON_WIN10_1511));
static_assert(ARRAYSIZE(MASK_BUTTON_WIN10_1607) - 1 == ARRAYSIZE(PAT_BUTTON_WIN10_1607));
static_assert(ARRAYSIZE(MASK_BUTTON_WIN10_1703) - 1 == ARRAYSIZE(PAT_BUTTON_WIN10_1703));
static_assert(ARRAYSIZE(MASK_QUERY_WIN10_1709) - 1 == ARRAYSIZE(PAT_QUERY_WIN10_1709));
static_assert(ARRAYSIZE(MASK_BUTTON_WIN10_1709) - 1 == ARRAYSIZE(PAT_BUTTON_WIN10_1709));
static_assert(ARRAYSIZE(MASK_BUTTON_WIN10_1803) - 1 == ARRAYSIZE(PAT_BUTTON_WIN10_1803));
static_assert(ARRAYSIZE(MASK_QUERY_WIN10_1809) - 1 == ARRAYSIZE(PAT_QUERY_WIN10_1809));
static_assert(ARRAYSIZE(MASK_BUTTON_WIN10_1809) - 1 == ARRAYSIZE(PAT_BUTTON_WIN10_1809));
static_assert(ARRAYSIZE(MASK_QUERY_WIN10_1903) - 1 == ARRAYSIZE(PAT_QUERY_WIN10_1903));
static_assert(ARRAYSIZE(MASK_QUERY_WIN10) - 1 == ARRAYSIZE(PAT_QUERY_WIN10));
static_assert(ARRAYSIZE(MASK_QUERY_WIN11) - 1 == ARRAYSIZE(PAT_QUERY_WIN11));
static_assert(ARRAYSIZE(MASK_QUERY_WIN11_28000) - 1 == ARRAYSIZE(PAT_QUERY_WIN11_28000));
static_assert(ARRAYSIZE(MASK_BUTTON_WIN10) - 1 == ARRAYSIZE(PAT_BUTTON_WIN10));
static_assert(ARRAYSIZE(MASK_BUTTON_WIN11) - 1 == ARRAYSIZE(PAT_BUTTON_WIN11));
static_assert(ARRAYSIZE(MASK_QUERY_WIN11_22000) - 1 == ARRAYSIZE(PAT_QUERY_WIN11_22000));
static_assert(ARRAYSIZE(MASK_BUTTON_WIN11_22000) - 1 == ARRAYSIZE(PAT_BUTTON_WIN11_22000));

struct XamlFamilyDescriptor {
    int family;
    PCWSTR name;
    const BYTE* queryPattern;
    const char* queryMask;
    SIZE_T querySize;
    const BYTE* buttonPattern;
    const char* buttonMask;
    SIZE_T buttonSize;

    SIZE_T buttonPointerXOffset;
    SIZE_T buttonPointerYOffset;
    SIZE_T buttonSizeIfaceOffset;
    double buttonEdgeTolerance;

    // Newer families materialize +/-0.05 as RIP-relative doubles.
    // 17763 compares directly against xmm2=0 and therefore has no refs.
    bool hasExternalToleranceConstants;
    SIZE_T negativeToleranceDispOffset;
    SIZE_T positiveToleranceDispOffset;
};

static constexpr XamlFamilyDescriptor XAML_FAMILIES[] = {
    {
        XAML_FAMILY_WIN10_1507,
        L"Win10-1507/10240-compatible XAML family",
        PAT_QUERY_WIN10_1709,
        MASK_QUERY_WIN10_1709,
        ARRAYSIZE(PAT_QUERY_WIN10_1709),
        PAT_BUTTON_WIN10_1507,
        MASK_BUTTON_WIN10_1507,
        ARRAYSIZE(PAT_BUTTON_WIN10_1507),
        0x190,
        0x194,
        0x50,
        0.0,
        false,
        0,
        0,
    },
    {
        XAML_FAMILY_WIN10_1511,
        L"Win10-1511/10586-compatible XAML family",
        PAT_QUERY_WIN10_1709,
        MASK_QUERY_WIN10_1709,
        ARRAYSIZE(PAT_QUERY_WIN10_1709),
        PAT_BUTTON_WIN10_1511,
        MASK_BUTTON_WIN10_1511,
        ARRAYSIZE(PAT_BUTTON_WIN10_1511),
        0x1B0,
        0x1B4,
        0x110,
        0.0,
        false,
        0,
        0,
    },
    {
        XAML_FAMILY_WIN10_1607,
        L"Win10-1607/14393-compatible XAML family",
        PAT_QUERY_WIN10_1709,
        MASK_QUERY_WIN10_1709,
        ARRAYSIZE(PAT_QUERY_WIN10_1709),
        PAT_BUTTON_WIN10_1607,
        MASK_BUTTON_WIN10_1607,
        ARRAYSIZE(PAT_BUTTON_WIN10_1607),
        0x1C0,
        0x1C4,
        0x118,
        0.0,
        false,
        0,
        0,
    },
    {
        XAML_FAMILY_WIN10_1703,
        L"Win10-1703/15063-compatible XAML family",
        PAT_QUERY_WIN10_1709,
        MASK_QUERY_WIN10_1709,
        ARRAYSIZE(PAT_QUERY_WIN10_1709),
        PAT_BUTTON_WIN10_1703,
        MASK_BUTTON_WIN10_1703,
        ARRAYSIZE(PAT_BUTTON_WIN10_1703),
        0x1C0,
        0x1C4,
        0x110,
        0.0,
        false,
        0,
        0,
    },
    {
        XAML_FAMILY_WIN10_1709,
        L"Win10-1709/16299-compatible XAML family",
        PAT_QUERY_WIN10_1709,
        MASK_QUERY_WIN10_1709,
        ARRAYSIZE(PAT_QUERY_WIN10_1709),
        PAT_BUTTON_WIN10_1709,
        MASK_BUTTON_WIN10_1709,
        ARRAYSIZE(PAT_BUTTON_WIN10_1709),
        0x1E0,
        0x1E4,
        0x120,
        0.0,
        false,
        0,
        0,
    },
    {
        XAML_FAMILY_WIN10_1803,
        L"Win10-1803-compatible XAML family",
        PAT_QUERY_WIN10_1809,
        MASK_QUERY_WIN10_1809,
        ARRAYSIZE(PAT_QUERY_WIN10_1809),
        PAT_BUTTON_WIN10_1803,
        MASK_BUTTON_WIN10_1803,
        ARRAYSIZE(PAT_BUTTON_WIN10_1803),
        0x1F8,
        0x1FC,
        0x138,
        0.0,
        false,
        0,
        0,
    },
    {
        XAML_FAMILY_WIN10_1809,
        L"Win10-1809-compatible XAML family",
        PAT_QUERY_WIN10_1809,
        MASK_QUERY_WIN10_1809,
        ARRAYSIZE(PAT_QUERY_WIN10_1809),
        PAT_BUTTON_WIN10_1809,
        MASK_BUTTON_WIN10_1809,
        ARRAYSIZE(PAT_BUTTON_WIN10_1809),
        0x228,
        0x22C,
        0x148,
        0.0,
        false,
        0,
        0,
    },
    {
        XAML_FAMILY_WIN10_1903,
        L"Win10-1903/18362-compatible XAML family",
        PAT_QUERY_WIN10_1903,
        MASK_QUERY_WIN10_1903,
        ARRAYSIZE(PAT_QUERY_WIN10_1903),
        PAT_BUTTON_WIN10,
        MASK_BUTTON_WIN10,
        ARRAYSIZE(PAT_BUTTON_WIN10),
        0x240,
        0x244,
        0x160,
        0.05,
        true,
        111,
        134,
    },
    {
        XAML_FAMILY_WIN10_MODERN,
        L"Win10-compatible XAML family",
        PAT_QUERY_WIN10,
        MASK_QUERY_WIN10,
        ARRAYSIZE(PAT_QUERY_WIN10),
        PAT_BUTTON_WIN10,
        MASK_BUTTON_WIN10,
        ARRAYSIZE(PAT_BUTTON_WIN10),
        0x240,
        0x244,
        0x160,
        0.05,
        true,
        111,
        134,
    },
    {
        XAML_FAMILY_WIN11_22000,
        L"Win11-early/22000-22621-compatible XAML family",
        PAT_QUERY_WIN11_22000,
        MASK_QUERY_WIN11_22000,
        ARRAYSIZE(PAT_QUERY_WIN11_22000),
        PAT_BUTTON_WIN11_22000,
        MASK_BUTTON_WIN11_22000,
        ARRAYSIZE(PAT_BUTTON_WIN11_22000),
        0x240,
        0x244,
        0x160,
        0.05,
        true,
        135,
        158,
    },
    {
        XAML_FAMILY_WIN11_MODERN,
        L"Win11-compatible XAML family",
        PAT_QUERY_WIN11,
        MASK_QUERY_WIN11,
        ARRAYSIZE(PAT_QUERY_WIN11),
        PAT_BUTTON_WIN11,
        MASK_BUTTON_WIN11,
        ARRAYSIZE(PAT_BUTTON_WIN11),
        0x240,
        0x244,
        0x160,
        0.05,
        true,
        131,
        154,
    },
    {
        XAML_FAMILY_WIN11_28000,
        L"Win11-28000-compatible XAML family",
        PAT_QUERY_WIN11_28000,
        MASK_QUERY_WIN11_28000,
        ARRAYSIZE(PAT_QUERY_WIN11_28000),
        PAT_BUTTON_WIN11,
        MASK_BUTTON_WIN11,
        ARRAYSIZE(PAT_BUTTON_WIN11),
        0x240,
        0x244,
        0x160,
        0.05,
        true,
        131,
        154,
    },
};

static const XamlFamilyDescriptor* FindXamlFamilyDescriptor(int family) {
    for (const auto& candidate : XAML_FAMILIES) {
        if (candidate.family == family)
            return &candidate;
    }
    return nullptr;
}

static DWORD g_queryScalePercentageRva = 0;
static DWORD g_buttonIsValidPointerPositionRva = 0;
static int g_resolvedXamlFamily = 0;
static PCWSTR g_resolvedXamlName = L"unresolved";
static const XamlFamilyDescriptor* g_resolvedXamlDescriptor = nullptr;

static int g_rootScalePercent = 150;
static bool g_enableScaleOverride = true;
static bool g_enableInputCoordinateFix = true;
static bool g_strictCompatibility = true;
static bool g_allowCompatibleBuildDiscovery = true;
static bool g_diagnosticLogging = false;

static bool g_dispatchHookReady = false;
static bool g_xamlHooksRegistered = false;
static bool g_xamlHookAttempted = false;
static bool g_scaleOverrideActive = false;

static volatile LONG g_observedNativeScalePercent = 100;
static volatile LONG g_compensationCount = 0;

// -------------------------------------------------------------------------
// Small helpers.
// -------------------------------------------------------------------------

static int ValidPercentOrDefault(int value) {
    // Keep the textbox useful for custom scaling while bounding obviously
    // accidental values. 50% is intentionally below Windows' normal UI range
    // so experimental downscaling can be exercised without accepting
    // near-zero or nonsensical scale factors.
    if (value < 50 || value > 500)
        return 150;
    return value;
}

static void LoadSettings() {
    g_rootScalePercent =
        ValidPercentOrDefault(Wh_GetIntSetting(L"rootScalePercent"));
    g_enableScaleOverride =
        Wh_GetIntSetting(L"enableScaleOverride") != 0;
    g_enableInputCoordinateFix =
        Wh_GetIntSetting(L"enableInputCoordinateFix") != 0;
    g_strictCompatibility =
        Wh_GetIntSetting(L"strictCompatibility") != 0;
    g_allowCompatibleBuildDiscovery =
        Wh_GetIntSetting(L"allowCompatibleBuildDiscovery") != 0;
    g_diagnosticLogging =
        Wh_GetIntSetting(L"diagnosticLogging") != 0;

    Wh_Log(L"%s", BUILD);
    Wh_Log(
        L"target=%d%% scaleOverride=%d inputFix=%d strict=%d discover=%d diagnostics=%d",
        g_rootScalePercent,
        g_enableScaleOverride,
        g_enableInputCoordinateFix,
        g_strictCompatibility,
        g_allowCompatibleBuildDiscovery,
        g_diagnosticLogging);
}

static bool IsReadableRange(const void* address, SIZE_T size) {
    if (!address || size == 0)
        return false;

    const BYTE* start = static_cast<const BYTE*>(address);
    const BYTE* end = start + size;
    if (end < start)
        return false;

    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(start, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    if ((mbi.Protect & PAGE_GUARD) != 0)
        return false;

    DWORD baseProtect = mbi.Protect & 0xFF;
    if (baseProtect == PAGE_NOACCESS)
        return false;

    const BYTE* regionEnd =
        static_cast<const BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
    return end <= regionEnd;
}

static bool MatchMasked(
    const BYTE* address,
    const BYTE* pattern,
    const char* mask,
    SIZE_T length) {

    if (!address || !pattern || !mask || !length ||
        !IsReadableRange(address, length)) {
        return false;
    }

    for (SIZE_T i = 0; i < length; ++i) {
        if (mask[i] == 'x' && address[i] != pattern[i])
            return false;
    }

    return true;
}

struct PeImageInfo {
    const IMAGE_NT_HEADERS64* nt = nullptr;
    const IMAGE_SECTION_HEADER* text = nullptr;
};

static bool GetPeImageInfo(HMODULE module, PeImageInfo* info) {
    if (!module || !info)
        return false;

    const BYTE* base = reinterpret_cast<const BYTE*>(module);
    if (!IsReadableRange(base, sizeof(IMAGE_DOS_HEADER)))
        return false;

    const IMAGE_DOS_HEADER* dos =
        reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        return false;

    const IMAGE_NT_HEADERS64* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (!IsReadableRange(nt, sizeof(*nt)) ||
        nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    if (!IsReadableRange(
            section,
            sizeof(IMAGE_SECTION_HEADER) * nt->FileHeader.NumberOfSections)) {
        return false;
    }

    const IMAGE_SECTION_HEADER* text = nullptr;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        char name[9] = {};
        memcpy(name, section[i].Name, 8);
        if (strcmp(name, ".text") == 0) {
            text = &section[i];
            break;
        }
    }

    if (!text)
        return false;

    info->nt = nt;
    info->text = text;
    return true;
}

static bool VerifyPatternAtRva(
    HMODULE module,
    DWORD rva,
    const BYTE* pattern,
    const char* mask,
    SIZE_T length) {

    return MatchMasked(
        reinterpret_cast<const BYTE*>(module) + rva,
        pattern,
        mask,
        length);
}

static bool FindUniquePatternInText(
    HMODULE module,
    const PeImageInfo& image,
    const BYTE* pattern,
    const char* mask,
    SIZE_T length,
    DWORD* rvaOut) {

    if (!module || !image.text || !pattern || !mask || !length || !rvaOut)
        return false;

    const BYTE* base = reinterpret_cast<const BYTE*>(module);
    DWORD textRva = image.text->VirtualAddress;
    SIZE_T textSize = image.text->Misc.VirtualSize;
    const BYTE* text = base + textRva;

    if (!IsReadableRange(text, textSize) || textSize < length)
        return false;

    DWORD foundRva = 0;
    int matches = 0;

    for (SIZE_T i = 0; i + length <= textSize; ++i) {
        if (text[i] != pattern[0])
            continue;

        bool match = true;
        for (SIZE_T j = 1; j < length; ++j) {
            if (mask[j] == 'x' && text[i + j] != pattern[j]) {
                match = false;
                break;
            }
        }

        if (!match)
            continue;

        ++matches;
        foundRva = textRva + static_cast<DWORD>(i);
        if (matches > 1)
            return false;
    }

    if (matches != 1)
        return false;

    *rvaOut = foundRva;
    return true;
}

static bool VerifyButtonFamilySemantics(
    HMODULE module,
    DWORD buttonRva,
    const XamlFamilyDescriptor& family) {

    if (!family.hasExternalToleranceConstants) {
        // Complete family signatures pin the layout literals and exact
        // zero-edge comparison sequence. There are intentionally no +/-0.05
        // constants to chase on these older implementations.
        if (family.buttonEdgeTolerance != 0.0)
            return false;

        if (family.family == XAML_FAMILY_WIN10_1507) {
            return family.buttonPointerXOffset == 0x190 &&
                   family.buttonPointerYOffset == 0x194 &&
                   family.buttonSizeIfaceOffset == 0x50;
        }

        if (family.family == XAML_FAMILY_WIN10_1511) {
            return family.buttonPointerXOffset == 0x1B0 &&
                   family.buttonPointerYOffset == 0x1B4 &&
                   family.buttonSizeIfaceOffset == 0x110;
        }

        if (family.family == XAML_FAMILY_WIN10_1607) {
            return family.buttonPointerXOffset == 0x1C0 &&
                   family.buttonPointerYOffset == 0x1C4 &&
                   family.buttonSizeIfaceOffset == 0x118;
        }

        if (family.family == XAML_FAMILY_WIN10_1703) {
            return family.buttonPointerXOffset == 0x1C0 &&
                   family.buttonPointerYOffset == 0x1C4 &&
                   family.buttonSizeIfaceOffset == 0x110;
        }

        if (family.family == XAML_FAMILY_WIN10_1709) {
            return family.buttonPointerXOffset == 0x1E0 &&
                   family.buttonPointerYOffset == 0x1E4 &&
                   family.buttonSizeIfaceOffset == 0x120;
        }

        if (family.family == XAML_FAMILY_WIN10_1803) {
            return family.buttonPointerXOffset == 0x1F8 &&
                   family.buttonPointerYOffset == 0x1FC &&
                   family.buttonSizeIfaceOffset == 0x138;
        }

        if (family.family == XAML_FAMILY_WIN10_1809) {
            return family.buttonPointerXOffset == 0x228 &&
                   family.buttonPointerYOffset == 0x22C &&
                   family.buttonSizeIfaceOffset == 0x148;
        }

        return false;
    }

    if (family.negativeToleranceDispOffset == 0 ||
        family.positiveToleranceDispOffset == 0) {
        return false;
    }

    // Offsets of the disp32 fields in:
    //   movsd xmm2, qword ptr [rip + negativeTolerance]
    //   movsd xmm3, qword ptr [rip + positiveTolerance]
    const BYTE* function =
        reinterpret_cast<const BYTE*>(module) + buttonRva;

    SIZE_T maxDispOffset =
        family.negativeToleranceDispOffset >
                family.positiveToleranceDispOffset
            ? family.negativeToleranceDispOffset
            : family.positiveToleranceDispOffset;

    if (!IsReadableRange(function, maxDispOffset + sizeof(INT32)))
        return false;

    INT32 negDisp = 0;
    INT32 posDisp = 0;
    memcpy(
        &negDisp,
        function + family.negativeToleranceDispOffset,
        sizeof(negDisp));
    memcpy(
        &posDisp,
        function + family.positiveToleranceDispOffset,
        sizeof(posDisp));

    const BYTE* negAddress =
        function +
        family.negativeToleranceDispOffset +
        sizeof(INT32) +
        negDisp;
    const BYTE* posAddress =
        function +
        family.positiveToleranceDispOffset +
        sizeof(INT32) +
        posDisp;

    if (!IsReadableRange(negAddress, sizeof(uint64_t)) ||
        !IsReadableRange(posAddress, sizeof(uint64_t))) {
        return false;
    }

    uint64_t negBits = 0;
    uint64_t posBits = 0;
    memcpy(&negBits, negAddress, sizeof(negBits));
    memcpy(&posBits, posAddress, sizeof(posBits));

    return family.buttonEdgeTolerance == 0.05 &&
           negBits == 0xBFA999999999999AULL &&
           posBits == 0x3FA999999999999AULL;
}

static bool ValidateResolvedFamily(
    HMODULE module,
    int familyId,
    DWORD queryRva,
    DWORD buttonRva) {

    const XamlFamilyDescriptor* family =
        FindXamlFamilyDescriptor(familyId);
    if (!family)
        return false;

    return VerifyPatternAtRva(
               module,
               queryRva,
               family->queryPattern,
               family->queryMask,
               family->querySize) &&
           VerifyPatternAtRva(
               module,
               buttonRva,
               family->buttonPattern,
               family->buttonMask,
               family->buttonSize) &&
           VerifyButtonFamilySemantics(
               module,
               buttonRva,
               *family);
}

static bool CommitResolvedXamlTargets(
    DWORD queryRva,
    DWORD buttonRva,
    int familyId,
    PCWSTR name) {

    const XamlFamilyDescriptor* family =
        FindXamlFamilyDescriptor(familyId);
    if (!family || !name)
        return false;

    g_queryScalePercentageRva = queryRva;
    g_buttonIsValidPointerPositionRva = buttonRva;
    g_resolvedXamlFamily = familyId;
    g_resolvedXamlName = name;
    g_resolvedXamlDescriptor = family;
    return true;
}

static bool ResolveXamlTargets(HMODULE module) {
    if (!module)
        return false;

    PeImageInfo image = {};
    if (!GetPeImageInfo(module, &image)) {
        Wh_Log(L"Couldn't parse Windows.UI.Xaml PE image. Refusing internal hooks.");
        return false;
    }

    DWORD timestamp = image.nt->FileHeader.TimeDateStamp;
    DWORD sizeOfImage = image.nt->OptionalHeader.SizeOfImage;

    // First prefer exact known builds. Every exact entry is still revalidated
    // against its full implementation-family signatures before hooking.
    for (const auto& build : KNOWN_XAML_BUILDS) {
        if (build.timestamp != timestamp || build.sizeOfImage != sizeOfImage)
            continue;

        bool valid = ValidateResolvedFamily(
            module,
            build.family,
            build.queryScalePercentageRva,
            build.buttonIsValidPointerPositionRva);

        if (!valid) {
            Wh_Log(
                L"Known Windows.UI.Xaml identity matched '%s', but implementation validation failed. Refusing hooks.",
                build.name);
            return false;
        }

        if (!CommitResolvedXamlTargets(
                build.queryScalePercentageRva,
                build.buttonIsValidPointerPositionRva,
                build.family,
                build.name)) {
            Wh_Log(
                L"Known build '%s' resolved to an unknown family descriptor. Refusing hooks.",
                build.name);
            return false;
        }

        Wh_Log(
            L"Windows.UI.Xaml exact build matched: %s timestamp=0x%08X size=0x%X QueryScaleRva=0x%X ButtonHitTestRva=0x%X",
            build.name,
            timestamp,
            sizeOfImage,
            g_queryScalePercentageRva,
            g_buttonIsValidPointerPositionRva);
        return true;
    }

    if (!g_allowCompatibleBuildDiscovery) {
        Wh_Log(
            L"Unknown Windows.UI.Xaml build: timestamp=0x%08X size=0x%X and compatible-build discovery is disabled. Refusing hooks.",
            timestamp,
            sizeOfImage);
        return false;
    }

    // Conservative family discovery: both target functions must uniquely
    // match the SAME descriptor and its family-specific ButtonBase semantics.
    int validFamilies = 0;
    DWORD selectedQuery = 0;
    DWORD selectedButton = 0;
    const XamlFamilyDescriptor* selectedFamily = nullptr;

    for (const auto& family : XAML_FAMILIES) {
        DWORD queryRva = 0;
        DWORD buttonRva = 0;

        bool queryFound = FindUniquePatternInText(
            module,
            image,
            family.queryPattern,
            family.queryMask,
            family.querySize,
            &queryRva);

        bool buttonFound = FindUniquePatternInText(
            module,
            image,
            family.buttonPattern,
            family.buttonMask,
            family.buttonSize,
            &buttonRva);

        if (!queryFound || !buttonFound)
            continue;

        if (!VerifyButtonFamilySemantics(
                module,
                buttonRva,
                family)) {
            continue;
        }

        ++validFamilies;
        selectedQuery = queryRva;
        selectedButton = buttonRva;
        selectedFamily = &family;
    }

    if (validFamilies != 1 || !selectedFamily) {
        Wh_Log(
            L"Unknown Windows.UI.Xaml build timestamp=0x%08X size=0x%X did not resolve to exactly one validated implementation family (matches=%d). Refusing hooks.",
            timestamp,
            sizeOfImage,
            validFamilies);
        return false;
    }

    if (!CommitResolvedXamlTargets(
            selectedQuery,
            selectedButton,
            selectedFamily->family,
            selectedFamily->name)) {
        Wh_Log(
            L"Compatible Windows.UI.Xaml implementation matched, but family configuration failed. Refusing hooks.");
        return false;
    }

    Wh_Log(
        L"Windows.UI.Xaml compatible implementation discovered: %s timestamp=0x%08X size=0x%X QueryScaleRva=0x%X ButtonHitTestRva=0x%X",
        g_resolvedXamlName,
        timestamp,
        sizeOfImage,
        selectedQuery,
        selectedButton);

    return true;
}


static bool HookExport(
    HMODULE module,
    const char* name,
    void* hook,
    void** original) {

    if (!module)
        return false;

    void* target = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!target)
        return false;

    if (!Wh_SetFunctionHook(target, hook, original)) {
        Wh_Log(L"Failed to hook %S at %p", name, target);
        return false;
    }

    return true;
}

static bool HookRva(
    HMODULE module,
    PCWSTR name,
    DWORD rva,
    void* hook,
    void** original) {

    if (!module)
        return false;

    void* target = reinterpret_cast<BYTE*>(module) + rva;
    if (!Wh_SetFunctionHook(target, hook, original)) {
        Wh_Log(L"Failed to hook %s at RVA 0x%X", name, rva);
        return false;
    }

    if (g_diagnosticLogging)
        Wh_Log(L"Registered %s at RVA 0x%X", name, rva);

    return true;
}

static LONG ObservedNativePercent() {
    LONG value = InterlockedCompareExchange(
        const_cast<volatile LONG*>(&g_observedNativeScalePercent), 0, 0);
    if (value < 50 || value > 500)
        return 100;
    return value;
}

static double InputScaleRatio() {
    LONG observed = ObservedNativePercent();
    if (observed <= 0)
        return 1.0;
    return static_cast<double>(g_rootScalePercent) /
           static_cast<double>(observed);
}

static bool IsSupportedOverridePair(UINT observed) {
    if (observed < 50 || observed > 500)
        return false;

    // Arbitrary equal/upscale ratios are safe for the established
    // promotion-only ButtonBase correction model. True downscaling is
    // different: it creates false-positive hit tests and therefore requires
    // bidirectional correction (including valid -> invalid demotion).
    // Strict mode keeps the historical never-demote invariant by refusing
    // target scales below the native/source XAML percentage.
    if (g_strictCompatibility &&
        g_rootScalePercent < static_cast<int>(observed)) {
        return false;
    }

    return true;
}

// -------------------------------------------------------------------------
// XAML scale source.
// -------------------------------------------------------------------------

using QueryScalePercentage_t =
    HRESULT (__fastcall*)(void* self, UINT* scalePercentage);
static QueryScalePercentage_t QueryScalePercentage_Original = nullptr;

static HRESULT __fastcall QueryScalePercentage_Hook(
    void* self,
    UINT* scalePercentage) {

    HRESULT hr = QueryScalePercentage_Original(self, scalePercentage);

    if (FAILED(hr) || !scalePercentage)
        return hr;

    UINT observed = *scalePercentage;
    if (observed >= 50 && observed <= 500) {
        InterlockedExchange(
            const_cast<volatile LONG*>(&g_observedNativeScalePercent),
            static_cast<LONG>(observed));
    }

    g_scaleOverrideActive = false;

    if (!g_enableScaleOverride)
        return hr;

    if (!IsSupportedOverridePair(observed)) {
        if (g_strictCompatibility &&
            g_rootScalePercent < static_cast<int>(observed)) {
            Wh_Log(
                L"Scale override not applied: strict compatibility refuses downscale observed=%u%% -> target=%d%%. Disable strict compatibility to opt into experimental bidirectional hit-test correction.",
                observed,
                g_rootScalePercent);
        } else {
            Wh_Log(
                L"Scale override not applied: observed=%u%% target=%d%% is outside the supported 50-500%% scale range.",
                observed,
                g_rootScalePercent);
        }
        return hr;
    }

    // If input compensation is requested, never create the known broken
    // visual/input split unless the stable pointer-sample hook is ready.
    if (g_enableInputCoordinateFix && !g_dispatchHookReady) {
        Wh_Log(
            L"Scale override not applied because the input-coordinate hook is unavailable.");
        return hr;
    }

    if (observed != static_cast<UINT>(g_rootScalePercent)) {
        *scalePercentage = static_cast<UINT>(g_rootScalePercent);
        g_scaleOverrideActive = true;
    }

    if (g_diagnosticLogging) {
        Wh_Log(
            L"QueryScalePercentage observed=%u%% forwarded=%u%% active=%d",
            observed,
            *scalePercentage,
            g_scaleOverrideActive);
    }

    return hr;
}

// -------------------------------------------------------------------------
// Stable physical-pointer sample.
//
// Native scaling keeps pointer pixels and XAML logical geometry in one scaling
// model. With only XAML scaled, ButtonBase caches a point that behaves like:
//
//   wrongLocal = physicalClientPoint - logicalElementOrigin
//
// For a requested/native ratio R, the native-equivalent point is:
//
//   correctLocal = physicalClientPoint / R - logicalElementOrigin
//
// R > 1 (upscale) creates false negatives, so promotion-only correction is
// sufficient. R < 1 (downscale) creates false positives and needs
// bidirectional correction; that path is explicit/experimental via
// strictCompatibility=false.
//
// We keep the physical pointer from DispatchMessageW and infer the element
// origin once per pointer gesture. Critically, the origin is NOT recomputed
// from newer raw cursor samples; this avoids drift at button edges.
// -------------------------------------------------------------------------

struct PointerSample {
    bool valid = false;
    HWND hwnd = nullptr;
    UINT pointerId = 0;
    POINTER_INPUT_TYPE pointerType = PT_POINTER;
    POINT pixelClient = {};
};

struct ButtonOriginEntry {
    void* self = nullptr;
    bool valid = false;
    double x = 0.0;
    double y = 0.0;
};

static constexpr int ORIGIN_CACHE_SIZE = 32;

static thread_local PointerSample g_currentPointerSample;
static thread_local ButtonOriginEntry g_buttonOrigins[ORIGIN_CACHE_SIZE];
static thread_local UINT g_originReplaceIndex = 0;
static thread_local bool g_lastMouseSampleValid = false;
static thread_local POINT g_lastMouseClient = {};

static void ClearGestureOriginCache() {
    for (int i = 0; i < ORIGIN_CACHE_SIZE; ++i)
        g_buttonOrigins[i] = {};
    g_originReplaceIndex = 0;
}

static bool FindButtonOrigin(
    void* self,
    double* x,
    double* y) {

    if (!self || !x || !y)
        return false;

    for (int i = 0; i < ORIGIN_CACHE_SIZE; ++i) {
        if (g_buttonOrigins[i].valid &&
            g_buttonOrigins[i].self == self) {
            *x = g_buttonOrigins[i].x;
            *y = g_buttonOrigins[i].y;
            return true;
        }
    }

    return false;
}

static void StoreButtonOrigin(
    void* self,
    double x,
    double y) {

    if (!self)
        return;

    for (int i = 0; i < ORIGIN_CACHE_SIZE; ++i) {
        if (g_buttonOrigins[i].valid &&
            g_buttonOrigins[i].self == self) {
            // Freeze the first synchronized origin for this gesture.
            return;
        }
    }

    for (int i = 0; i < ORIGIN_CACHE_SIZE; ++i) {
        if (!g_buttonOrigins[i].valid) {
            g_buttonOrigins[i].self = self;
            g_buttonOrigins[i].valid = true;
            g_buttonOrigins[i].x = x;
            g_buttonOrigins[i].y = y;
            return;
        }
    }

    UINT slot = g_originReplaceIndex++ % ORIGIN_CACHE_SIZE;
    g_buttonOrigins[slot].self = self;
    g_buttonOrigins[slot].valid = true;
    g_buttonOrigins[slot].x = x;
    g_buttonOrigins[slot].y = y;
}

static bool IsPointerMessage(UINT message) {
    return message >= WM_POINTERUPDATE &&
           message <= WM_POINTERHWHEEL;
}

static void PreparePointerSample(const MSG* msg) {
    g_currentPointerSample = {};

    if (!msg || !IsPointerMessage(msg->message))
        return;

    UINT pointerId = LOWORD(msg->wParam);
    POINTER_INFO pi = {};
    if (!GetPointerInfo(pointerId, &pi))
        return;

    POINT client = pi.ptPixelLocation;
    if (!msg->hwnd || !ScreenToClient(msg->hwnd, &client))
        return;

    g_currentPointerSample.valid = true;
    g_currentPointerSample.hwnd = msg->hwnd;
    g_currentPointerSample.pointerId = pointerId;
    g_currentPointerSample.pointerType = pi.pointerType;
    g_currentPointerSample.pixelClient = client;

    if (msg->message == WM_POINTERDOWN)
        ClearGestureOriginCache();

    if (pi.pointerType == PT_MOUSE) {
        // Preserve the last mouse point that was actually dispatched through
        // USER32. XAML's cached ButtonBase local point corresponds to this
        // processed stream, which is safer than pairing it with a newer raw
        // cursor position during an asynchronous hold check.
        g_lastMouseSampleValid = true;
        g_lastMouseClient = client;
    }
}

using DispatchMessageW_t = LRESULT (WINAPI*)(const MSG*);
static DispatchMessageW_t DispatchMessageW_Original = nullptr;

static LRESULT WINAPI DispatchMessageW_Hook(const MSG* msg) {
    if (g_enableInputCoordinateFix)
        PreparePointerSample(msg);

    LRESULT result = DispatchMessageW_Original(msg);

    if (g_enableInputCoordinateFix) {
        g_currentPointerSample = {};
    }

    return result;
}

// -------------------------------------------------------------------------
// ButtonBase native-edge-equivalent validation.
// -------------------------------------------------------------------------

static bool TryReadButtonPointerPosition(
    void* self,
    float* x,
    float* y) {

    if (!self || !x || !y || !g_resolvedXamlDescriptor)
        return false;

    BYTE* base = static_cast<BYTE*>(self);
    float* px =
        reinterpret_cast<float*>(
            base + g_resolvedXamlDescriptor->buttonPointerXOffset);
    float* py =
        reinterpret_cast<float*>(
            base + g_resolvedXamlDescriptor->buttonPointerYOffset);

    if (!IsReadableRange(px, sizeof(float)) ||
        !IsReadableRange(py, sizeof(float))) {
        return false;
    }

    *x = *px;
    *y = *py;
    return true;
}

using ButtonSizeGetter_t =
    HRESULT (__fastcall*)(void* self, double* value);

static bool TryGetButtonActualSize(
    void* self,
    double* width,
    double* height) {

    if (!self || !width || !height)
        return false;

    if (!g_resolvedXamlDescriptor)
        return false;

    BYTE* iface =
        static_cast<BYTE*>(self) +
        g_resolvedXamlDescriptor->buttonSizeIfaceOffset;

    if (!IsReadableRange(iface, sizeof(void*)))
        return false;

    void** vtable = *reinterpret_cast<void***>(iface);
    if (!vtable ||
        !IsReadableRange(
            vtable + (0x68 / sizeof(void*)),
            sizeof(void*) * 2)) {
        return false;
    }

    auto getWidth =
        reinterpret_cast<ButtonSizeGetter_t>(
            vtable[0x68 / sizeof(void*)]);
    auto getHeight =
        reinterpret_cast<ButtonSizeGetter_t>(
            vtable[0x70 / sizeof(void*)]);

    if (!getWidth || !getHeight)
        return false;

    double w = 0.0;
    double h = 0.0;

    HRESULT hrW = getWidth(iface, &w);
    HRESULT hrH = getHeight(iface, &h);
    if (FAILED(hrW) || FAILED(hrH))
        return false;

    *width = w;
    *height = h;
    return true;
}

static bool NativeButtonInside(
    double x,
    double y,
    double width,
    double height) {

    if (!g_resolvedXamlDescriptor)
        return false;

    // Mirror the edge semantics of the resolved native implementation:
    //   older zero-tolerance families: x/y in [0, size]
    //   18362+/modern families: x/y in [-0.05, size + 0.05]
    double tolerance =
        g_resolvedXamlDescriptor->buttonEdgeTolerance;

    return
        x >= -tolerance &&
        x <= width + tolerance &&
        y >= -tolerance &&
        y <= height + tolerance;
}

using ButtonIsValidPointerPosition_t =
    HRESULT (__fastcall*)(void* self, BYTE* isValid);
static ButtonIsValidPointerPosition_t
    ButtonIsValidPointerPosition_Original = nullptr;

static HRESULT __fastcall ButtonIsValidPointerPosition_Hook(
    void* self,
    BYTE* isValid) {

    HRESULT hr =
        ButtonIsValidPointerPosition_Original(self, isValid);

    if (!g_enableInputCoordinateFix ||
        !g_scaleOverrideActive ||
        FAILED(hr) ||
        !isValid) {
        return hr;
    }

    BYTE originalValid = *isValid;
    double ratio = InputScaleRatio();

    bool isUpscale = ratio > 1.001;
    bool isDownscale = ratio < 0.999;

    if (!isUpscale && !isDownscale)
        return hr;

    // Strict mode preserves the historical safety invariant:
    // original-valid is never converted to invalid. QueryScalePercentage
    // already refuses strict downscales, so this is a second fail-closed guard.
    if (isDownscale && g_strictCompatibility)
        return hr;

    // Upscaling only creates the known false-negative shape. If the native
    // result is already valid, preserve it exactly and do no extra work.
    if (isUpscale && originalValid != 0)
        return hr;

    float wrongXf = 0.0f;
    float wrongYf = 0.0f;
    double width = 0.0;
    double height = 0.0;

    if (!TryReadButtonPointerPosition(
            self, &wrongXf, &wrongYf) ||
        !TryGetButtonActualSize(
            self, &width, &height)) {
        return hr;
    }

    double wrongX = static_cast<double>(wrongXf);
    double wrongY = static_cast<double>(wrongYf);

    bool sampleOk = false;
    bool synchronizedSample = false;
    POINT physicalClient = {};
    UINT pointerType = PT_POINTER;

    if (g_currentPointerSample.valid) {
        physicalClient = g_currentPointerSample.pixelClient;
        pointerType = static_cast<UINT>(
            g_currentPointerSample.pointerType);
        sampleOk = true;
        synchronizedSample = true;
    } else if (
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 &&
        GetCapture() != nullptr) {

        POINT screen = {};
        if (GetCursorPos(&screen)) {
            physicalClient = screen;
            if (ScreenToClient(GetCapture(), &physicalClient)) {
                pointerType = PT_MOUSE;
                sampleOk = true;
            }
        }
    }

    if (!sampleOk)
        return hr;

    double originX = 0.0;
    double originY = 0.0;
    bool haveOrigin =
        FindButtonOrigin(self, &originX, &originY);

    if (!haveOrigin) {
        // For a synchronized dispatch sample, wrongLocal and physicalClient
        // belong to the same pointer event and directly reveal the logical
        // element origin.
        if (synchronizedSample) {
            originX =
                static_cast<double>(physicalClient.x) - wrongX;
            originY =
                static_cast<double>(physicalClient.y) - wrongY;
        }
        // For an asynchronous mouse hold check, pair XAML's cached local
        // point with the last mouse position that actually went through
        // DispatchMessageW, not with a potentially newer GetCursorPos point.
        // This preserves a stable logical element origin even when the cursor
        // is moving quickly across a button edge.
        else if (pointerType == PT_MOUSE &&
                 g_lastMouseSampleValid) {
            originX =
                static_cast<double>(g_lastMouseClient.x) - wrongX;
            originY =
                static_cast<double>(g_lastMouseClient.y) - wrongY;
        } else {
            return hr;
        }

        StoreButtonOrigin(self, originX, originY);
    }

    double correctedX =
        static_cast<double>(physicalClient.x) / ratio - originX;
    double correctedY =
        static_cast<double>(physicalClient.y) / ratio - originY;

    bool correctedInside =
        NativeButtonInside(
            correctedX,
            correctedY,
            width,
            height);

    BYTE desiredValid = originalValid;
    PCWSTR action = L"none";

    if (isUpscale) {
        // Preserve the original production invariant: only promote a native
        // false negative when the ratio-corrected point is inside.
        if (originalValid == 0 && correctedInside) {
            desiredValid = 1;
            action = L"promote";
        }
    } else {
        // Experimental downscale path (strictCompatibility=false).
        // A smaller visual target creates false-positive hit tests, so exact
        // native-equivalent behavior must be allowed to demote as well as
        // promote according to the corrected point.
        desiredValid = correctedInside ? 1 : 0;
        if (desiredValid != originalValid)
            action = desiredValid ? L"promote" : L"demote";
    }

    if (desiredValid != originalValid) {
        *isValid = desiredValid;

        LONG count =
            InterlockedIncrement(
                const_cast<volatile LONG*>(&g_compensationCount));

        if (g_diagnosticLogging) {
            Wh_Log(
                L"Input compensation #%ld action=%s ratio=%.4f self=%p type=%u original=%u wrong=(%.3f,%.3f) physical=(%ld,%ld) origin=(%.3f,%.3f) corrected=(%.3f,%.3f) size=(%.3f,%.3f)",
                count,
                action,
                ratio,
                self,
                pointerType,
                static_cast<UINT>(originalValid),
                wrongX,
                wrongY,
                physicalClient.x,
                physicalClient.y,
                originX,
                originY,
                correctedX,
                correctedY,
                width,
                height);
        }
    }

    return hr;
}

// -------------------------------------------------------------------------
// XAML hook installation.
// -------------------------------------------------------------------------

static bool InstallXamlHooks() {
    if (g_xamlHooksRegistered)
        return true;
    if (g_xamlHookAttempted)
        return false;

    HMODULE xaml = GetModuleHandleW(L"Windows.UI.Xaml.dll");
    if (!xaml)
        return false;

    g_xamlHookAttempted = true;

    if (!ResolveXamlTargets(xaml))
        return false;

    bool ok = true;

    // Register the input hook first. If the scale hook registration were ever
    // to fail afterward, this hook remains pass-through because
    // g_scaleOverrideActive is still false.
    if (g_enableInputCoordinateFix) {
        ok &= HookRva(
            xaml,
            L"ButtonBase::IsValidPointerPosition",
            g_buttonIsValidPointerPositionRva,
            reinterpret_cast<void*>(
                ButtonIsValidPointerPosition_Hook),
            reinterpret_cast<void**>(
                &ButtonIsValidPointerPosition_Original));
    }

    if (g_enableScaleOverride) {
        ok &= HookRva(
            xaml,
            L"DXamlCore::QueryScalePercentage",
            g_queryScalePercentageRva,
            reinterpret_cast<void*>(
                QueryScalePercentage_Hook),
            reinterpret_cast<void**>(
                &QueryScalePercentage_Original));
    }

    g_xamlHooksRegistered = ok;

    if (ok) {
        Wh_Log(
            L"Windows.UI.Xaml release hooks registered successfully (%s, family=%d).",
            g_resolvedXamlName,
            g_resolvedXamlFamily);
    } else {
        Wh_Log(
            L"Windows.UI.Xaml release hook registration failed; scale override will not be considered safe.");
    }

    return ok;
}

// -------------------------------------------------------------------------
// Load timing.
// -------------------------------------------------------------------------

using LdrLoadDll_t =
    NTSTATUS (NTAPI*)(
        PWSTR,
        ULONG,
        PUNICODE_STRING,
        PHANDLE);
static LdrLoadDll_t LdrLoadDll_Original = nullptr;

static NTSTATUS NTAPI LdrLoadDll_Hook(
    PWSTR pathToFile,
    ULONG flags,
    PUNICODE_STRING moduleFileName,
    PHANDLE moduleHandle) {

    NTSTATUS status =
        LdrLoadDll_Original(
            pathToFile,
            flags,
            moduleFileName,
            moduleHandle);

    if (status >= 0 &&
        !g_xamlHooksRegistered &&
        GetModuleHandleW(L"Windows.UI.Xaml.dll")) {

        bool registered = InstallXamlHooks();
        if (registered) {
            BOOL applied = Wh_ApplyHookOperations();
            if (!applied) {
                Wh_Log(
                    L"Failed to apply late Windows.UI.Xaml hook operations.");
            }
        }
    }

    return status;
}

static bool InstallDispatchHook() {
    if (!g_enableInputCoordinateFix)
        return true;

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
        user32 = LoadLibraryW(L"user32.dll");

    bool ok = HookExport(
        user32,
        "DispatchMessageW",
        reinterpret_cast<void*>(DispatchMessageW_Hook),
        reinterpret_cast<void**>(&DispatchMessageW_Original));

    g_dispatchHookReady = ok;
    if (!ok) {
        Wh_Log(
            L"DispatchMessageW hook unavailable; safe scale override will be suppressed.");
    }

    return ok;
}

static bool InstallLdrHook() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
        ntdll = LoadLibraryW(L"ntdll.dll");

    return HookExport(
        ntdll,
        "LdrLoadDll",
        reinterpret_cast<void*>(LdrLoadDll_Hook),
        reinterpret_cast<void**>(&LdrLoadDll_Original));
}

// -------------------------------------------------------------------------
// Windhawk.
// -------------------------------------------------------------------------

BOOL Wh_ModInit() {
    LoadSettings();

    wchar_t imagePath[MAX_PATH] = {};
    GetModuleFileNameW(
        nullptr,
        imagePath,
        ARRAYSIZE(imagePath));

    Wh_Log(L"Injected into %s (PID %u)",
           imagePath,
           GetCurrentProcessId());

    InstallDispatchHook();
    InstallLdrHook();

    // If XAML was already loaded before the mod initialized, register now.
    InstallXamlHooks();

    return TRUE;
}

void Wh_ModAfterInit() {
    // No background probes, hook threads, timers, or polling.
}

void Wh_ModSettingsChanged() {
    LoadSettings();

    // Internal hooks, once registered, intentionally remain installed.
    // Their hooks read the live settings and pass through when disabled.
    // A new LogonUI process naturally re-runs QueryScalePercentage.
}

void Wh_ModUninit() {
    Wh_Log(
        L"Uninitializing %s; compensated hit-tests=%ld",
        BUILD,
        InterlockedCompareExchange(
            const_cast<volatile LONG*>(&g_compensationCount),
            0,
            0));
}

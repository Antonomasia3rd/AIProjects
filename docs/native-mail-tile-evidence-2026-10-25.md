# Installed Mail tile builder evidence, October 25

This bounded pass inspected the installed package manifest, public binary
strings and existing disassembly only. No Mail process, account, message store,
notification database or package permission was accessed or changed. No Mail
code or Microsoft bitmap was copied into DesktopStub.

Package: `microsoft.windowscommunicationsapps 16005.14326.22342.0`.
`HxOutlookBackground.dll` SHA-256 was rechecked as
`59A1E8FCC46108B37AA46FFE9DB2A56B0EB9C4390A191C58696AA52FDCEDB1A1`.
VAs below assume its preferred image base `0x180000000` and are not portable
across versions. The ignored local receipt is
`build/review-validation/mail-native-builder-witness-oct25.txt`, extracted from
the existing `calendar-HxOutlookBackground-nobytes-oct24.txt` witness.

## Actual message content rather than promotional templates

The complete XML strings at raw offsets `0x1EBD30`, `0x1ECA60`, `0x1ECF40` and
`0x1ED3E0` concern promotional Gmail/Yahoo/Outlook account setup. They are not
the ordinary message tile contract and must not be used as that reference.

The ordinary two-field block helper is `0x180073A9C`. It creates a group and
subgroup (`0x180073B18`, `0x180073BB9`), then a `caption` text node from the
record's first prepared string at `+0` (`0x180073C32`) and a `captionsubtle`
node from its second prepared string at `+0x20` (`0x180073C94`). The style
helpers are the same independently traced `0x1800730FC` / `0x180073258` used in
the Calendar evidence. These are regular caption styles, not a bold `base`
or large `title` style. Field semantics have not yet been traced back to the
producer; sender, subject and snippet must remain separate in a new backend
until that mapping or a deliberate presentation choice is established.

The per-message helper `0x180075708` creates a Wide binding at
`0x1800758BE` and a Medium binding at `0x180075956`. It adds the common pair
to Wide, then sets the first node to wrap with maxLines `1`
(`0x180075A28`) and the second to wrap with maxLines `3`
(`0x180075AA8`). Literal `1` was decoded directly at RVA `0x1EBC9C`;
literal `3` is RVA `0x1EE8D8`. Medium receives `caption` and `captionsubtle`
nodes at `0x180075B1B` and `0x180075B6F`; both call the explicit two-line
wrapping helper at `0x180075B2F` and `0x180075B86`.

Thus the recovered per-message text structure is:

| Binding | Primary prepared text | Secondary prepared text |
| --- | --- | --- |
| Medium | caption, wraps, max2 | captionSubtle, wraps, max2 |
| Wide | caption, wraps, max1 | captionSubtle, wraps, max3 |
| Large block | caption, no extra wrap setter | captionSubtle, wraps, max2 |

The Large block helper `0x180075660` appends the same grouped pair and applies
the two-line wrap helper to its secondary text at `0x1800756AF`. The aggregate
Large binding is created at `0x180074ECE`. This is not the Calendar's body-first
Large layout and is not evidence of a universal RSS template.

## Static Small, branding and separate badge

The inspected ordinary Mail builder path has no `TileSmall` binding. Its
manifest provides `HxMailSmallTile.png`, alongside explicit Medium, Wide and
Large image paths. The manifest shows the application name on Medium, Wide
and Large and specifies background `#0078D7`.

The inspected binding creator `0x180072EA4` creates the binding and template
attribute without an explicit branding/displayName override. Do not infer a
Calendar-like dynamic date label, count text node or custom logo placement
from this path. The precise shell footer depends on manifest/default branding
behavior and still needs visible acceptance.

Unread counts use a separate numeric badge document: the builder beginning at
`0x180075C60` creates a `badge` element at `0x180075DA4`, formats its supplied
count through `0x18000B69A4` (`0x180075E72`), assigns the `value` attribute at
`0x180075F1A` and queues it through `0x180076194` at `0x180075F89`. Static
ASCII trace labels independently identify `SendBadgeNotification` at RVA
`0x1EEAA0`, `UpdateBadge` at `0x1EEA90` and `UpdateTile` at `0x1EEAC8`.
No visible badge maximum or Small text layout was established by this pass.

## Multiple notifications and limits of the finding

The ordinary dispatch builder starts at `0x180074C30`. In its aggregate-enabled
branch it prepares a Large binding, then walks message records. The loop tests
index against `5` at `0x180074F32` and increments it at `0x18007521A`; it stops
on that bound or the available record count. Message documents are queued at
`0x180074FDE` / `0x18007519D` through `0x180076670`. The final aggregate
document can also receive the last message's Medium/Wide bindings; intermediate
documents receive independent Medium/Wide bindings. A feature-dependent branch
can skip aggregate preparation, so this is not a claim that every environment
receives an identical Large payload. `SendAllTileNotification` appears as an
ASCII trace label at RVA `0x1EEA40`.

This proves multiple queued XML documents and a five-iteration bound in this
builder, not a guaranteed display order, rotation cadence, unread selection
policy, sender/subject/preview mapping, account aggregation policy or complete
pixel reproduction. A Gmail implementation must separately define consent,
privacy, source filtering and error behavior; Calendar scopes do not grant
mail access.

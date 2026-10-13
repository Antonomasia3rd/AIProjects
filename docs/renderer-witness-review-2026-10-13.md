# Retained preset raster witness review

The one coordinated retained-witness batch passed the unchanged preset gate and
captured **DARK/DARK**, not a passing LIGHT/LIGHT pair. The original four-pixel
variation remains unresolved; this observation does not support a renderer fix
or an assertion/tolerance change.

Root compiled/scanned/ran the diagnostic once. This subtask only read its log,
inventories, saved profiles and RGBA bytes. No child, repeat probe, debugger,
native capture or real UI/API integration was executed for this review.

## Identity and scope

- Log: `build/review-validation/retained-renderer-preset-oct13c.log`.
- Retained fixture:
  `C:/Users/Amiya/AppData/Local/Temp/DesktopStubSmoke-74ac51d0d20943edae371bde16e6c76d`.
- Original/copied child SHA-256:
  `3F06C3F88E96FC586DF2756DA921F761F56C1F63D7AD9487D011BA4D00621C4C`.
- Seven inventories exist under `RendererDiagnostics/001` through `007`; there
  is no recorded variant 000 or 008. All seven saved Medium PNG/RGBA file hashes
  match their inventories, each RGBA file has 90,000 bytes (150x150x4), and every
  variant retains its INI, manifest and synthetic BMP.

The normal preset prehistory, manifest/default expansion, all-assets rendering
and cache-enabled path ran. Saved baseline/secondary profiles both specify
Windows81Preset, cache enabled, primary `AAAA` and badge `7`; their secondary
values are respectively `IIII` and `MMMM`. Children used the unchanged copied
machine code and offline render-only/configuration paths. The diagnostic stops
after the complete preset/field/NoWrap comparisons; the composition/adaptive/
integration tail was not run. Parent-side retention/hash/JSON I/O changes
inter-child timing and allocation, despite leaving child code unchanged.

## Medium PNG inventory

| Variant | Label | PNG SHA-256 |
| --- | --- | --- |
| 001 | TileText disabled | `9BC7D2149D6014B44DE187A94453E7A13BEB102D2096FB1064E34E65047E142E` |
| 002 | All fields: AAAA / IIII / 7 | `F5A76FCD16428B4AEC2819C07BF5A49FB221653EDE9C109062AA6F3C900DFBED` |
| 003 | Primary: WWWW / IIII / 7 | `CA93ABFDE2E3016298B06986AB9DD04727F486A583254D21F57B4CB1F157E7BE` |
| 004 | Secondary: AAAA / MMMM / 7 | `F5A76FCD16428B4AEC2819C07BF5A49FB221653EDE9C109062AA6F3C900DFBED` |
| 005 | Badge: AAAA / IIII / 8 | `DA46020932FAF38653D909AF0C6D403398B178700A98977DF6F49035DE54E5E4` |
| 006 | One line: DESKTOPSTUB TEST | `C518F7B4870A50F1B8B9394BF2FD7C5B3EAC8A3A1B426569FD04FC187BBF72AF` |
| 007 | One line: DESKTOPSTUB WIDE | `D7CA9F5D468EB39AFBA2FAB9CE6BD022C1AFDEDD9F75E44235EA7AB483A94013` |

Both 002 and 004 have the exact historical DARK PNG hash above. Their decoded
row-major RGBA SHA-256 is also identical:
`9C0C717F3C1CC1C25EA982FEC802BA6490090246982BAA73DD1F0791224E7070`.
Direct comparison of the two retained RGBA arrays finds **zero changed pixels**.

At `(20,93)`, `(30,93)`, `(40,93)` and `(50,93)`, both baseline and secondary
contain `(156,203,239,255)`. The historical LIGHT baseline instead had
`(174,212,242,255)` at all four positions, with PNG SHA-256
`EC8F0259326C166E7239FFE90C18985B6A7F93FDAE6D8D1F344992F2958526D8`.
That historical lighter artifact was not captured here. The badge variant also
retains the darker color at all four unchanged primary-caption positions.

Variant 006 happens to contain `(174,212,242,255)` at `(40,93)`, but draws
different primary text. This is ordinary antialias coverage in another glyph,
not a witness of the historical unchanged-AAAA variation; matching one color
alone does not establish matching glyphs, geometry or PNG data.

## Gate evidence and conclusion

The log reports the unchanged Medium unsupported-secondary equality passing,
while Wide/Large secondary variations produce 345/866 changed pixels inside
their intended regions. Badge variations change 442/431/442 pixels on
Medium/Wide/Large. The NoWrap primary comparison changes 604/373 pixels on
Medium/Large and passes its original region check.

Unlike earlier successful full probes, this passing batch keeps the actual
baseline/secondary witnesses, so DARK/DARK is established for this invocation.
The missing successful-fixture evidence gap is closed for this batch, but a
LIGHT glyph state and its causal native inputs remain uncaptured. No production
GDI+, cache, font, rendering-hint or tolerance change follows from these results.
The historical failed fixture and [exact renderer ledger](audit-render-determinism.md)
remain authoritative for the unresolved failure.

# Shared native and managed INI coordination

Native `IniWriteMutexGuard` and managed `ManagedIniFile` now acquire the same Global kernel mutex for one normalized file path. Previously the native name used a session-local, case-sensitive FNV identity while the managed name used a Global user-scoped SHA identity. Those names could not serialize mixed writers, including the password runtime's counter updates and the interactive editor's unrelated configuration edits.

The new common INI name is:

```text
Global\AIProjects.ConfigIni.<first-eight-SHA256-bytes-as-uppercase-hex>
```

The digest input is the normalized absolute file path encoded as UTF-8. Both implementations resolve the full Windows path, normalize slash direction, trim trailing directory separators, and apply invariant uppercase. Case, slash, relative/dot aliases and Unicode were compared directly between native and managed implementations. The name contains no user or session suffix, so cooperating accounts compute the same name for the same normalized path.

The native implementation is shared in `dependencies/named_objects.inc`; managed normalization is public as `ManagedNamedObjects.NormalizeFileIdentity`. Existing INI, transaction and Startup API call signatures remain unchanged. File-size, quote/comment dialect, atomic publication, original-byte rollback and original-absence restoration behavior are retained.

This changes functional coordination, not object permissions. Kernel-object security attributes remain standard. No permissive descriptor, privilege change, elevation request, or alternate-name fallback was added. The existing native access-denied retry opens the **same** object with its required synchronization/modification rights; if the caller lacks those rights, it still reports an unavailable lock. Managed acquisition likewise reports ordinary access/creation failures. Actual cross-account access therefore remains permission-limited even though the file identity is shared.

Native Startup locking now uses a Global user-scoped `StartupShortcut` name with the current SID identity and the existing normalized shortcut-path identity. It retains its existing bounded wait and ownership behavior. Shortcut filenames, executable checks, repair/removal logic and the managed `ManagedStartup` identity are unchanged. Native and managed Startup names are not presented as interchangeable when their component, shortcut files or identity inputs differ.

# Validation

All executed fixtures received fresh Defender scans and were run through `tools/ReviewValidation.ps1` with a maximum 60-second process stage. The mixed managed/native fixture and native worker enforce internal 45-second deadlines. No actual Startup-folder integration, elevation, service, hardware, package or network action was requested.

| Fixture | Result | Relevant coverage |
| --- | --- | --- |
| `ManagedIniInteropTests` + `IniInteropNativeWorker` | 21 checks passed | Exact name/hash parity; case/slash/relative/dot/Unicode aliases; independent file identities; Global per-user native Startup name; bidirectional blocking and configured waits; same-thread native recursion; 100 read-modify-writes in each language without losing either section; comments and temporary cleanup |
| `ManagedIniDocumentTests` | 50 checks passed | Atomic multi-section publication, immutable prospective snapshots, whole-document validation before writes, comments/unknown sections, encoding and exact original restoration, write failure/retry, bounded cooperating writer/read behavior |
| `ManagedIniTransactionTests` | 29 checks passed | Exact original bytes and absence, mutable-spec freezing, configured wait limits, thread ownership, failed restore retry and mutex release |
| Default `SharedBaselineTests` | 114 successful checks | Existing native INI/CLI/JSON/logging/DPAPI/application primitives, fake coupled Startup transactions and ShellLinks only in temporary directories; real Startup-folder integration explicitly skipped |

The interop build recipe is `tools/BuildManagedIniInteropTests.cmd`. `tools/TestManagedIniInteropTests.ps1` compiles only by default; `-Run` scans both workers before launching the fixture. The native compatibility recipe `tools/BuildNamedObjectBaselineTests.cmd` compiles only. Existing source-check cleanup remains owned by the main task.

The tests prove current same-user process cooperation and exact common naming. They do not switch Windows accounts or sessions, change permissions, execute the LocalSystem password runtime, or validate a visible Startup workflow. Older binaries that still use the previous lock protocol do not participate in the new coordination. Hard-link, mapped-drive or reparse aliases are not claimed to collapse into one identity; the contract is a normalized path and cooperating updated helpers, rather than a filesystem object identifier.

Usage was checked before the gates: ordinary usage available, 55% consumed at that check. Both interop workers and managed compatibility targets compiled with warnings treated as errors. The native baseline compiled successfully under its existing `/W4` policy. Scoped diff checks passed.

Verified scanned SHA-256 values:

```text
IniInteropNativeWorker.exe                 E481D755E6FFD07C1955A8663A7A08C7D93BCC9951D4F9CFBE1652336D87B54C
ManagedIniInteropTests.exe                 A968E458676ECA835CCE0C32CB92A3D258DEB1AE573A2194CAA251C72CBB9679
NamedObjectSharedBaselineTests.exe         8CEA5A2F5B201F071B65EAEC7D6B08AA6DEAF7C098407BD35099EDE79F98B647
ManagedIniDocumentTests.exe                791AB23635E5AF8827EB7827DCA8E65AE8F5B5E6F5A57E6F0303BC0CBD32049B
NamedObjectManagedTransactionTests.exe     4B98A1CAB6C74F0A8B34EF20D216A684304E5B63A4C102B85FD3F0454818F39D
```

Build, scan and run evidence is retained under `build/review-validation/managed-native-ini-*` and `build/review-validation/named-object-*`. The baseline was explicitly frozen after these gates so other agents could compile their final host/legacy fixtures against stable includes.

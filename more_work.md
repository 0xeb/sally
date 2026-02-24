# Handoff: Remaining Decoupling Work

## Snapshot
- Branch: `wip`
- HEAD: `34d3655` (`Use SalCreateFileH in codetbl for filesystem abstraction`)
- Working tree state at handoff: clean before this file was added
- Last validated state:
  - `cmake --build build --config Debug --target salamander`
  - `cmake --build build --config Release --target salamander`
  - Focused tests (all passing):
    - `build/tests/Debug/gtest_registry.exe --gtest_brief=1`
    - `build/tests/Debug/gtest_filesystem.exe --gtest_brief=1`
    - `build/tests/Debug/gtest_widepath_filesystem_bridge.exe --gtest_brief=1`
    - `build/tests/Debug/gtest_filesystem_ops.exe --gtest_brief=1`
    - `build/tests/Debug/gtest_cpathbuffer_winapi.exe --gtest_brief=1`
    - `build/tests/Debug/gtest_worker_wide.exe --gtest_brief=1`

## Exit-Crash Triage (Latest)
- Dump analyzed: `c:\temo\salamand.dmp` with `C:\Users\elias\Tools\dbgtools\cdb.exe`
- Key finding:
  - Fault bucket: single-step at function epilogue (`_RTC_CheckStackVars` path)
  - Fault location: `src/fileswn1.cpp:1281` (`IconThreadThreadFBody` return path)
  - Root cause: icon-reader path setup wrote wide data into `CWidePathBuffer` using ANSI-length math without ensuring wide capacity first.
- Fix applied in:
  - `bb8d91b` `Fix icon reader wide-path buffer overwrite on long paths`
  - `src/fileswn1.cpp` now computes required wide size first, calls `EnsureCapacity`, then converts safely.
- Post-fix validation:
  - Debug/Release `salamander` builds pass
  - focused gtests pass (same set listed above)

## Completed In This Cycle
- Registry decoupling extended through:
  - `src/mainwnd2.cpp`
  - `src/mainwnd3.cpp`
  - `src/plugins1.cpp`
  - `src/salamdr5.cpp` (`CSystemPolicies`)
  - `src/salamdr1.cpp`
  - `src/bugreprt.cpp`
  - `src/plugins2.cpp` (icon-list binary persistence)
- Filesystem decoupling extended through:
  - `src/worker.cpp` (`COperation` path-based open/create/delete/get/set attrs on wide path now via `IFileSystem`)
  - `src/codetbl.cpp` switched from direct `CreateFileW` to `SalCreateFileH` (abstraction path)
- Registry enumeration hardening in `src/common/Win32Registry.cpp` (dynamic buffer growth, long name support)
- Additional gtest coverage for registry helpers/integration.

## Current Design State (Important)
- `CPathBuffer` currently:
  - max: `SAL_MAX_LONG_PATH` (`32767`) in `src/common/widepath.h:28`
  - default initial capacity: `SAL_PATH_BUFFER_INITIAL_CAPACITY` (`4096`) in `src/common/widepath.h:32`
  - grows on demand up to max; not fixed 32K per instance.
- `IRegistry` is now usable in most main startup/config paths.
- `IFileSystem` is in place and already used in widepath bridge + parts of worker.

## What Is Left (Core-First Priority)

### 1) Registry decoupling still pending in core modules
Highest-value remaining files:
- `src/icncache.cpp` (heavy association/icon reads + key/value enumeration; many raw `RegOpenKey*`, `RegEnum*`, `RegCloseKey` usage)
- `src/shiconov.cpp` (shell overlay handler enumeration from registry)
- `src/snooper.cpp` (LanMan shares key open/close + notify loop)
- `src/mainwnd2.cpp:1131` and `src/mainwnd3.cpp:6460` still use `SHCopyKey` directly (intentional for now; not abstracted)

Special-case module:
- `src/salmoncl.cpp` uses direct `Reg*` in startup/entrypoint code and explicitly warns not to use runtime/global-object facilities (`src/salmoncl.cpp:15-16`).
  - This should be decoupled carefully with a low-level C-style shim (not normal `gRegistry` usage).

Notes:
- Legacy `OpenKeyAux/GetValueAux/...` calls are effectively removed from normal core callsites (remaining occurrences are wrapper implementation/comments/legacy areas).

### 2) Filesystem decoupling still pending (large)
Biggest remaining file:
- `src/worker.cpp` still has many direct Win32 file calls (scan count ~80).
  - Some are intentionally low-level (ADS streams, compression/encryption, reparse-point ops).
  - Others are straightforward get/set/delete/open replacements and can still move to `IFileSystem`.

Other core files with direct file API usage:
- `src/pack2.cpp` (~28)
- `src/pack1.cpp` (~10)
- `src/fileswn6.cpp` (~10)
- `src/safefile.cpp` (~7)
- `src/salamdr2.cpp`, `src/salamdr3.cpp`, `src/salamdr5.cpp`, etc.

### 3) Optional (lower priority / tooling / plugins)
- `src/shexreg.c` (large direct registry usage; shell extension registration code)
- plugin/tooling trees (`src/plugins/**`, `src/translator/**`, `src/tserver/**`, etc.) if scope expands.

## Recommended Next Slices (small, safe, high payoff)

1. `icncache.cpp` registry adapter slice
- Introduce local helpers (`GetIcnCacheRegistry()`, `OpenKeyReadA`, `GetStringA/GetValue`, `EnumSubKeys/EnumValues`) and replace raw opens/enums incrementally.
- Keep behavior/error handling unchanged.
- Add/extend tests in `gtest_registry` for edge cases needed by this migration.

2. `shiconov.cpp` + `snooper.cpp` registry slice
- Move key open/close and value reads to `IRegistry`.
- Keep `RegNotifyChangeKeyValue` direct in `snooper.cpp` (event notification is not currently represented in `IRegistry`).

3. worker “easy direct-call” slice
- Continue in `src/worker.cpp`, replacing straightforward `GetFileAttributesW/SetFileAttributesW/DeleteFileW/RemoveDirectoryW/CreateFileW` with `IFileSystem` calls where no special behavior is required.
- Leave specialty calls (FSCTL compression, encryption/decryption, ADS-specific behavior) for a dedicated pass.

## Suggested Medium-Term Interface Work
- Add a small platform adapter for worker specialty ops (example: `IWorkerPlatform`):
  - Compression (`FSCTL_SET_COMPRESSION`)
  - Encrypt/decrypt wrappers
  - Reparse/symlink-specific operations
- Keep `IFileSystem` focused on generic file system operations.

## Known Gotchas
- `HANDLES_Q(CreateFileA(...))` can collide with handle-tracker macros (`C__Handles::CreateFileA` member). Prefer:
  - `SalCreateFileH(...)` for ANSI-path calls in app code, or
  - direct `fileSystem->CreateFile(...)` with wide path.
- `salmoncl.cpp` startup constraints are strict; avoid introducing dependencies on globals requiring runtime init.
- `SHCopyKey` remains in `mainwnd2.cpp`/`mainwnd3.cpp`; treat as explicit exception until a safe abstraction is introduced.

## Quick Scan Commands
Use these to re-check remaining raw usage:

```powershell
# Remaining direct registry calls in core-first scope
$pattern = '\b(RegOpenKeyEx|RegCreateKeyEx|RegSetValueEx|RegQueryValueEx|RegDeleteKey|RegDeleteValue|RegCloseKey|SHDeleteKey|SHCopyKey)\b'
rg -n --glob '!src/common/dep/**' --glob '!src/reglib/**' --glob '!src/regwork.*' --glob '!src/tests/**' $pattern src |
  Where-Object { $_ -match '^src\\(?!plugins\\|setup\\|translator\\|salmon\\|tserver\\|common\\)' }

# Remaining direct file API calls in core-first scope
$pattern = '\b(CreateFileA|CreateFileW|DeleteFileA|DeleteFileW|GetFileAttributesA|GetFileAttributesW|SetFileAttributesA|SetFileAttributesW|RemoveDirectoryA|RemoveDirectoryW|MoveFileExA|MoveFileExW|FindFirstFileA|FindFirstFileW|FindNextFileA|FindNextFileW)\b'
rg -n --glob '!src/common/dep/**' --glob '!src/tests/**' $pattern src |
  Where-Object { $_ -match '^src\\(?!plugins\\|setup\\|translator\\|salmon\\|tserver\\|common\\)' }
```

## Commit Trail (recent)
- `34d3655` Use SalCreateFileH in codetbl for filesystem abstraction
- `db6e9bf` Use IRegistry binary helpers for plugin icon list persistence
- `71565aa` Route COperation wide-path file ops through IFileSystem
- `50cb27d` Use IRegistry helpers in bug report registry diagnostics
- `40177e3` Use IRegistry helpers for salamdr1 shell icon and metrics reads
- `7d2cb1a` Decouple CSystemPolicies registry reads through IRegistry
- `7960f15` Use IRegistry helpers in plugins1 save/load registry checks
- `ed18a06` Use IRegistry helpers for Shell Icon Size refresh in mainwnd3

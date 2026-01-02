# Cross-Build Review: xbuild2 Branch Judgement (Round 2)

This document provides an in-depth review of the cross-compilation patches in the `xbuild2`
branch, identifying issues that mask problems rather than properly addressing them.

## Summary

| Severity | Count | Description |
|----------|-------|-------------|
| **CRITICAL** | 1 | Must fix before ANY real-world use |
| **HIGH** | 3 | Should fix for production readiness |
| **MEDIUM** | 3 | Should fix for correctness |
| **LOW/OK** | 5 | Acceptable or minor issues |

---

## CRITICAL Issues

### 1. SEH (Structured Exception Handling) COMPLETELY DISABLED

**Files:** `src/common/compat.h`, `src/plugins/shared/spl_com.h`

```c
#define __try if (1)
#define __except(x) if (0)
#define __finally if (0); else
#define GetExceptionCode() 0
#define GetExceptionInformation() NULL
```

**IMPACT ANALYSIS:**

This is **catastrophically bad**. Let me explain exactly what happens:

**601 SEH usages across 40+ files** including:
- `src/find.cpp` - File search with memory-mapped files
- `src/bugreprt.cpp` - **Bug report generation** (the crash handler itself!)
- `src/callstk.cpp` - Call stack capture
- `src/cache.cpp` - File cache operations
- `src/icncache.cpp` - Icon cache (shell icon loading)
- `src/viewer2.cpp` - File viewer
- `src/plugins/winscp/` - WinSCP plugin (uses `__finally` extensively)
- 30+ more files

**What the code does:**
```cpp
// Original intent - protect against plugin crashes
__try {
    dangerousPluginCode();
}
__except (CCallStack::HandleException(GetExceptionInformation())) {
    // Log the crash, continue running
}
```

**What actually happens with fake SEH:**
```cpp
if (1) {
    dangerousPluginCode();  // If this crashes...
}
if (0) {
    // ...this NEVER runs. Program terminates immediately.
}
```

**Real-world consequences:**
1. **Memory-mapped file access** (`find.cpp:1191`) - If the file is truncated while mapped,
   access causes EXCEPTION_IN_PAGE_ERROR. With fake SEH → **instant crash**
2. **Icon overlay handlers** (`shiconov.cpp`) - Buggy shell extensions crash Salamander
   instead of being isolated
3. **Plugin crashes** - Any plugin exception kills the entire application
4. **Bug reporter cannot create reports** - The crash handler itself uses SEH!

**The `__finally` macro is particularly broken:**
```cpp
__try {
    AcquireLock();
    DoWork();
}
__finally {
    ReleaseLock();  // MUST run even if DoWork() throws!
}
```
With fake SEH, if `DoWork()` crashes:
- Real SEH: `ReleaseLock()` runs, then exception propagates
- Fake SEH: `ReleaseLock()` never runs, deadlock + crash

**Proper fix options (in order of preference):**

1. **Use LibSEH library** - [LibSEH](http://www.programmingunlimited.net/siteexec/content.cgi?page=libseh)
   provides `__seh_try`/`__seh_except`/`__seh_finally` macros that work with GCC/MinGW.
   Requires code changes but provides real SEH functionality.

2. **Use Clang targeting MSVC** instead of llvm-mingw - Clang has partial `__try/__except`
   support when targeting MSVC ABI (not MinGW). This requires MSVC runtime libraries.

3. **Use `AddVectoredExceptionHandler()`** as a global crash catcher - doesn't provide
   per-block exception handling but can at least catch crashes and generate bug reports.

4. **Use signal handlers + setjmp/longjmp** for critical paths - complex but portable.

5. **Refactor code** to use C++ exceptions (`try/catch`) where SEH is used for recoverable
   errors, and accept crashes for truly exceptional cases.

**Note:** The flag `-fseh-exceptions` only affects C++ exception handling mechanism,
NOT the `__try/__except` syntax. MinGW-w64 GCC/Clang does NOT support the Microsoft
`__try/__except/__finally` keywords natively.

---

## HIGH Severity Issues

### 2. Memory Allocation Failure Handler Disabled

**File:** `src/common/allochan.cpp:68-71`

```cpp
#ifdef _MSC_VER
    OldNewHandler = _set_new_handler(AltapNewHandler);
    OldNewMode = _set_new_mode(1);
#endif
```

**Impact:**
- Salamander has a sophisticated OOM handler showing "Retry/Ignore/Abort" dialog
- On MinGW builds: no dialog, just crash or `std::bad_alloc`
- Users lose ability to free memory and retry

**Why it matters:**
Salamander is a file manager that may handle huge directories (millions of files).
Running out of memory during enumeration should be recoverable.

**Proper fix:**
```cpp
#ifdef _MSC_VER
    _set_new_handler(AltapNewHandler);
#else
    std::set_new_handler([]() {
        if (!AltapNewHandler(0)) throw std::bad_alloc();
    });
#endif
```

---

### 3. ARM64 Platform Detection Incorrect

**File:** `cmake/sal_common.cmake:18-23`

```cmake
if(CMAKE_CROSSCOMPILING)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(SAL_PLATFORM "x64")
  else()
    set(SAL_PLATFORM "Win32")  # ARM64 falls here!
  endif()
```

**Impact:**
- ARM64 builds get `SAL_PLATFORM=Win32`
- Output directory is `Release_Win32` instead of `Release_ARM64`
- Confusion and potential file conflicts

**Fix:**
```cmake
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  set(SAL_PLATFORM "x64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
  set(SAL_PLATFORM "ARM64")
else()
  set(SAL_PLATFORM "Win32")
endif()
```

---

### 4. ODR Violation Hidden With Linker Flag

**Files:** `cmake/toolchain-llvm-mingw-x64.cmake:82-84`

```cmake
# Debug builds: allow duplicate symbols (ODR violations from CWindow class in diskmap)
set(CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT "-Wl,--allow-multiple-definition")
```

**The actual bug:**
Two DIFFERENT classes named `CWindow`:
1. `src/plugins/diskmap/DiskMap/GUI.CWindow.h` - Diskmap's own implementation
2. `src/common/winlib.h` - Salamander's core CWindow

These are completely different classes with different:
- Base classes (one inherits CWindowsObject, one doesn't)
- Constructors
- Method signatures
- Virtual tables

**Impact:**
With `--allow-multiple-definition`, linker picks ONE randomly. If wrong one is picked:
- Virtual method calls go to wrong implementation
- Undefined behavior at runtime
- This is a **pre-existing bug** that MSVC hides, cross-build exposes it

**Proper fix:**
Rename diskmap's CWindow to `CDiskMapWindow` or put in namespace.

---

## MEDIUM Severity Issues

### 5. ARM64 Crash Handler Patching Disabled

**File:** `src/callstk.cpp:79-82`

```cpp
#ifdef _M_ARM64
    // ARM64 instruction patching not implemented - skip this feature
    return FALSE;
#endif
```

**What this does:**
`PreventSetUnhandledExceptionFilter()` patches kernel32.dll to prevent CRT from
overriding the crash handler. On ARM64, this is skipped entirely.

**Impact:**
- CRT can intercept crashes before Salamander's handler
- Bug reports may not work correctly
- Different behavior between x64 and ARM64

**Proper fix:**
Use `AddVectoredExceptionHandler()` which works on all architectures,
or implement ARM64 instruction patching.

---

### 6. Module Initialization Order Partially Disabled

**File:** `src/common/allochan.cpp:51-53`

```cpp
#else
// Non-MSVC: no special section ordering needed
void Initialize__Allochan() {}
#endif
```

Salamander uses MSVC linker section tricks for init ordering:
- `.i_alc$m` - allochan
- `.i_hnd$m` - handles
- `.i_hea$m` - heap
- `.i_msg$m` - messages
- `.i_str$m` - str
- `.i_trc$m` - trace

**On MinGW:**
- `allochan.cpp` - stubbed out (empty function)
- `handles.cpp` - entire module disabled via `#ifdef HANDLES_ENABLE`
- `heap.cpp`, `trace.cpp`, `messages.cpp`, `str.cpp` - use MSVC pragmas, likely fail to compile
  OR are wrapped in `#ifdef` that I didn't see

**Potential issues:**
- Modules may initialize in wrong order
- Race conditions possible
- Works by accident if C++ static init happens to be ordered correctly

**Proper fix:**
Use GCC's `__attribute__((init_priority(N)))` for explicit ordering.

---

### 7. Debug Handle Tracking Disabled for MinGW

**File:** `cmake/sal_common.cmake:76-84`

```cmake
if(MSVC)
  list(APPEND SAL_DEBUG_DEFINES
    __DEBUG_WINLIB
    HANDLES_ENABLE
    MULTITHREADED_HANDLES_ENABLE
    _CRTDBG_MAP_ALLOC
    ...
  )
endif()
```

**Impact:**
- `HANDLES_ENABLE` controls handle leak detection
- Debug builds on MinGW won't detect:
  - GDI handle leaks (HBITMAP, HDC, HBRUSH)
  - Kernel handle leaks (HANDLE, HFILE)
  - Window handle issues

**Assessment:**
This is a **development-time** feature loss, not runtime. Users won't be affected.
However, developers using MinGW for debugging will miss handle leaks.

**Recommendation:**
The handle tracking code uses MSVC-specific CRT debug features. This is
acceptable as a MinGW limitation, but should be documented.

---

## LOW Severity / Acceptable Changes

### 8. `_AddressOfReturnAddress` Replacement

**File:** `src/callstk.cpp:1047-1051`

```cpp
#ifdef _MSC_VER
    PushCallerAddress = *(DWORD_PTR*)_AddressOfReturnAddress();
#else
    PushCallerAddress = (DWORD_PTR)__builtin_return_address(0);
#endif
```

**Analysis:**
- `_AddressOfReturnAddress()` returns address of return address on stack
- Dereferencing gives the actual return address
- `__builtin_return_address(0)` returns the return address directly

These are semantically equivalent. **Acceptable.**

---

### 9. Include Path Separators (Backslash → Forward Slash)

**Multiple files**

```cpp
// Before
#include "lang\lang.rh"
// After
#include "lang/lang.rh"
```

**Status:** Correct and necessary. Forward slashes work on all platforms.

---

### 10. Header Case Sensitivity

```cpp
// Before
#include <CommDlg.h>
// After
#include <commdlg.h>
```

**Status:** Correct for case-sensitive filesystems (Linux hosts).

---

### 11. Comment Translations (Czech → English)

Many files have comments translated. This is fine and improves maintainability.

---

### 12. SSE Intrinsics in Diskmap

**File:** `src/plugins/diskmap/DiskMap/TreeMap.Graphics.CCushionGraphics.h`

The CROSS-BUILD.md claims SSE was replaced with sse2neon.h, but actually:
- The SSE code is **commented out** (`//_mm_prefetch(...)`)
- No actual SSE intrinsics are used
- The header is included but provides nothing

**Status:** No actual issue, just misleading documentation.

---

## Files Requiring Immediate Attention

| Priority | File | Issue |
|----------|------|-------|
| P0 | `src/common/compat.h` | SEH disabled - critical |
| P0 | `src/plugins/shared/spl_com.h` | SEH disabled - critical |
| P1 | `src/common/allochan.cpp` | OOM handler disabled |
| P1 | `cmake/sal_common.cmake` | ARM64 detection wrong |
| P1 | `cmake/toolchain-*.cmake` | ODR workaround |
| P2 | `src/callstk.cpp` | ARM64 crash handler |
| P2 | `src/plugins/diskmap/` | ODR violation (pre-existing) |

---

## Recommended Actions

### Immediate (Before Any Testing)

1. **Address SEH problem** - Options:
   - Integrate [LibSEH](http://www.programmingunlimited.net/siteexec/content.cgi?page=libseh)
   - OR add `AddVectoredExceptionHandler()` for crash catching
   - OR accept that MinGW builds will crash on exceptions (document as limitation)

2. **Fix OOM handler** - Use `std::set_new_handler()` for non-MSVC.

### Before Release

3. **Fix ARM64 platform detection** in CMake

4. **Fix diskmap ODR violation** - Rename CWindow class

5. **Implement ARM64 crash handler** or use vectored exceptions

### Long Term

6. **Document MinGW limitations** - Handle tracking, CRT debug features

7. **Test extensively** - The SEH changes affect crash resilience throughout

---

## Testing Checklist

- [ ] Open corrupted/truncated file in viewer (tests SEH in file access)
- [ ] Load plugin that crashes on init (tests SEH isolation)
- [ ] Exhaust memory during large directory enumeration (tests OOM handler)
- [ ] Crash intentionally, verify bug report generated (tests crash handler)
- [ ] ARM64 build output goes to correct directory
- [ ] diskmap plugin works correctly (tests ODR fix)

---

*Review Date: 2026-01-02*
*Review Method: Full diff analysis main...xbuild2*
*Commits Reviewed: 9*
*Lines Changed: ~5000+*

---

## References

- [Clang MSVC Compatibility - SEH Support](https://clang.llvm.org/docs/MSVCCompatibility.html)
- [MinGW-w64 Exception Handling Wiki](https://sourceforge.net/p/mingw-w64/wiki2/Exception%20Handling/)
- [LibSEH - SEH Compatibility Library for MinGW](http://www.programmingunlimited.net/siteexec/content.cgi?page=libseh)
- [llvm-mingw GitHub Repository](https://github.com/mstorsjo/llvm-mingw)

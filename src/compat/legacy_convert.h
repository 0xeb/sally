// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_convert — the v107<->wide data conversions for the legacy plugin
// adapter.
//
// This is where the ABI break's data corruption risk lives, so it is a module of
// its own with its own tests rather than logic buried in 281 forwarders.
//
// THE TWO DIRECTIONS ARE NOT SYMMETRIC, deliberately:
//
//   A -> W preserves the plugin's active-code-page bytes exactly when they form
//   a valid sequence. Malformed byte sequences are refused explicitly; they
//   must never collapse into a legitimate empty path or label.
//
//   W -> A CANNOT be unconditional, and this is the whole point. A wide name may
//   have no representation in the active code page. Handing the plugin a
//   best-fit or '?'-substituted name would make it operate on a DIFFERENT FILE
//   than the one the user selected - silent, wrong, and destructive. So the
//   policy is exact-else-refuse: the conversion either round-trips byte-for-byte
//   or it fails, and the caller skips that item. Best-fit is banned outright.
//
// OWNERSHIP: both generations' CFileData::Name / DosName and v107's optional
// NameW must live on Salamander's heap, because the AddFile contract lets the
// core free them. The converters allocate with the same allocator the core uses
// and every function documents who owns what afterwards. The tests instrument
// allocation counts, because a leak or a double free here is a crash in someone
// else's plugin.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "compat/sdk107.h"

// The live SDK, for the wide side of every conversion.
#include "plugins/shared/spl_com.h"

namespace sally::compat
{

// Why a W->A conversion was refused. Callers surface this rather than guessing.
enum class NarrowRefusal
{
    None,
    // The name cannot be represented exactly in the active code page.
    NotRepresentable,
    // The input was empty or null where a name was required.
    Missing,
    // The frozen representation cannot carry the complete input.
    TooLong,
    // The adapter could not allocate transactional conversion storage.
    OutOfMemory,
};

struct NarrowResult
{
    bool ok = false;
    std::string value;
    NarrowRefusal refusal = NarrowRefusal::None;
};

// Exact-else-refuse narrowing. NEVER best-fits, never substitutes '?'.
// This is the single gate every W->A name conversion in the adapter goes through.
NarrowResult NarrowExact(std::wstring_view wide);

// Deterministic form used by the boundary tests and by NarrowExact. UTF-8 is
// special because Windows rejects WC_NO_BEST_FIT_CHARS for CP_UTF8; it still
// receives strict invalid-scalar checking and the same exact round trip.
NarrowResult NarrowExactForCodePage(std::wstring_view wide, UINT codePage);

// Translate a refusal into the Win32 error used at the frozen ABI boundary.
DWORD Win32ErrorForNarrowRefusal(NarrowRefusal refusal) noexcept;

// Decode one required NUL-terminated frozen string. Empty text is valid;
// nullptr and invalid byte sequences are refused with an explicit Win32 error.
bool WidenPluginText(const char* ansi, std::wstring& wide);

// Widen exactly byteLength frozen bytes. The legacy substring APIs count bytes,
// while their live counterparts count WCHARs; callers use the resulting string
// length after this boundary conversion. Invalid/null spans are refused and
// clear 'wide'.
bool WidenPluginSpan(const char* ansi, int byteLength, std::wstring& wide);
bool WidenPluginSpanForCodePage(const char* ansi, int byteLength,
                                UINT codePage, std::wstring& wide);

// Decode a counted frozen byte value that may intentionally contain NULs
// (notably REG_MULTI_SZ). This is distinct from text/span callers so embedded
// terminators never become accepted accidentally at path boundaries.
bool WidenPluginBytes(const char* ansi, int byteLength, std::wstring& wide);
bool WidenPluginBytesForCodePage(const char* ansi, int byteLength,
                                 UINT codePage, std::wstring& wide);

// Namespace-distinct scalar value objects must still cross field-by-field.
// These helpers are shared by General and the reverse plugin wrappers.
::CQuadWord QuadWordFromLegacy(const sdk107::CQuadWord& legacy);
sdk107::CQuadWord QuadWordToLegacy(const ::CQuadWord& wide);

// ---------------------------------------------------------------------------
// CFileData conversion
// ---------------------------------------------------------------------------

// Convert a row a v107 plugin produced into the live (wide) shape.
//
// 'out' is fully overwritten. Its wide Name/DosName are freshly allocated on
// Salamander's heap and become the CALLER's responsibility (i.e. the core's,
// once it is handed to AddFile). 'in' is untouched and still owned by whoever
// owned it before.
//
// The Ext interior pointer is RECOMPUTED, not copied: it points into Name, so a
// copied pointer would dangle into the plugin's buffer. Getting this wrong is
// invisible until something reads the extension.
// 'trustNameW' must be false for a plugin built before sdk107's NameW version:
// those binaries did not initialize that field, so even reading its value is
// invalid. The Directory facade derives this bit from BuiltForVersion.
bool FileDataFromLegacy(const sdk107::CFileData& in, ::CFileData& out,
                        bool trustNameW = true);

// Convert a live (wide) row into the shape a v107 plugin expects.
//
// Returns false and leaves 'out' zeroed when the name cannot be narrowed
// exactly - the caller must then SKIP the row rather than pass an approximation.
// 'refusal' says why, so the caller can log something true.
bool FileDataToLegacy(const ::CFileData& in, sdk107::CFileData& out,
                      NarrowRefusal* refusal = nullptr);

// The same conversion for callers whose own contract has NO failure value.
//
// CSalamanderDirectoryAbstract::GetFile/GetDir (sdk107/spl_com.h:385) return a row
// for a valid index and document no NULL case, so a plugin that trusts the frozen
// header dereferences whatever comes back. Refusing there turned a name the active
// code page cannot spell into a crash with Sally's name on it - reachable with a row
// the plugin added ITSELF, since the inbound direction has always trusted NameW.
//
// So when, and ONLY when, the exact narrowing fails because the code page cannot
// spell the name, this narrows it approximately into Name and puts the exact
// spelling in NameW - which is what v107 reserved that field for ("NULL if Name can
// represent the filename correctly ... use NameW when available for display and file
// operations", spl_com.h:259), and what UseWideName() is there to announce. A
// missing name, a name past INT_MAX and allocation failure still refuse: none of
// them is an encoding problem, and no side channel makes them safe.
//
// Every OTHER core-to-plugin path keeps FileDataToLegacy's exact-or-refuse rule.
// Those callers choose whether to invoke plugin code at all, and declining to ask a
// v107 plugin about a row it would misidentify is a decision Sally is entitled to
// make; a frozen accessor promising a row is not.
bool FileDataToLegacyWithWideNameFallback(const ::CFileData& in, sdk107::CFileData& out,
                                          NarrowRefusal* refusal = nullptr);

// Free the heap fields of a row produced by FileDataFromLegacy /
// FileDataToLegacy. Safe on a zeroed struct, and idempotent: it nulls what it
// frees so a double call cannot double-free.
void FreeLegacyFileData(sdk107::CFileData& row);
void FreeConvertedFileData(::CFileData& row);

#ifdef SALLY_LEGACY_CONVERT_STANDALONE
// Test-only allocator substitution. Directory-facade tests allocate the plugin
// input and both conversion generations through one tracker, proving success
// and failure ownership rather than merely asserting that calls do not crash.
using LegacyNameAllocForTests = void* (*)(std::size_t bytes);
using LegacyNameFreeForTests = void (*)(void* block);
void SetLegacyNameAllocationHooksForTests(LegacyNameAllocForTests alloc,
                                          LegacyNameFreeForTests free);
#endif

// Recompute the Ext interior pointer for a row whose Name was just set.
// Exposed because the enumerator thunks build rows in place.
//
// The v107 rule, preserved exactly: Ext points after the LAST dot in the name,
// or at the terminating NUL when there is no extension. A leading dot does not
// start an extension.
void RecomputeExtNarrow(sdk107::CFileData& row);
void RecomputeExtWide(::CFileData& row);

// ---------------------------------------------------------------------------
// Enumerator thunk support
// ---------------------------------------------------------------------------

// A v107 plugin enumerating the selection gets ANSI names. Names that cannot be
// narrowed exactly are SKIPPED, never approximated - so the plugin sees a
// shorter list rather than a wrong one, and the count of skips is reported so
// the caller can tell the user something happened.
struct EnumNarrowStats
{
    int delivered = 0;
    int skippedNotRepresentable = 0;

    bool anythingSkipped() const { return skippedNotRepresentable > 0; }
};

// Narrow one enumerated name for delivery to a legacy plugin.
// Returns false when the item must be skipped; 'stats' is updated either way.
bool NarrowEnumeratedName(const std::wstring& wide, std::string& out,
                          EnumNarrowStats& stats);

// Call-bound bridge for the selection2 callback shape. Returned string storage
// remains valid until the next call. Rows whose name or DOS name cannot be
// represented exactly are skipped; reset clears all retained state and output.
class CLegacySelection2Bridge final
{
public:
    CLegacySelection2Bridge(::SalEnumSelection2 next, void* nextParam);

    const char* Next(HWND parent, int enumFiles, const char** dosName,
                     BOOL* isDir, sdk107::CQuadWord* size, DWORD* attr,
                     FILETIME* lastWrite, int* errorOccurred);
    static const char* WINAPI Invoke(
        HWND parent, int enumFiles, const char** dosName, BOOL* isDir,
        sdk107::CQuadWord* size, DWORD* attr, FILETIME* lastWrite,
        void* parameter, int* errorOccurred);

private:
    ::SalEnumSelection2 NextWide;
    void* NextParam;
    std::string Name;
    std::string DosName;
    bool Skipped = false;
};

} // namespace sally::compat

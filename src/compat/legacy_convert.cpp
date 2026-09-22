// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_convert — see legacy_convert.h. UI-free by construction: this TU must
// keep compiling without precomp.h so tests can build it directly.

// THIS FILE MUST NEVER INCLUDE precomp.h — in any build.
//
// It includes the FROZEN v107 snapshot (via legacy_convert.h -> compat/sdk107.h),
// and the snapshot cannot share a translation unit with precomp.h's LIVE SDK:
// several SDK helpers are macro-generated into DWORD/WORD/BYTE overload sets, and
// expanding those a second time inside namespace sdk107 while the live macro
// definition is in scope makes every call to them ambiguous (C2668). Isolating
// this TU is what the namespace was for; pulling in the app's precompiled world
// would defeat it.
#define NOMINMAX
#include <windows.h>

#include "compat/legacy_convert.h"

#include <algorithm>
#include <cstring>

// The frozen sdk107 snapshot's spl_base.h #defines memcpy to _sal_safe_memcpy
// (a Debug+TRACE_ENABLE overlap check) and declares it inside `namespace
// sdk107`, per sdk107.h's own note that the namespace scopes functions, not
// just types. From here in sally::compat that declaration isn't visible, so
// the macro-expanded call fails to resolve. This TU's buffers are always
// freshly allocated and never overlap their source, so the check has nothing
// to catch here - undo the substitution and use the real memcpy.
#undef memcpy

namespace sally::compat
{
namespace
{

#ifdef SALLY_LEGACY_CONVERT_STANDALONE
LegacyNameAllocForTests TestAlloc = nullptr;
LegacyNameFreeForTests TestFree = nullptr;
#endif

// The adapter allocates names with the same allocator the core frees them with.
// In a standalone test build that is plain malloc, which is what the core's
// allocator is underneath; the point of routing through here is that there is
// ONE place to change if that ever stops being true.
void* AllocNameBytes(std::size_t bytes)
{
#ifdef SALLY_LEGACY_CONVERT_STANDALONE
    if (TestAlloc != nullptr)
        return TestAlloc(bytes);
#endif
    return std::malloc(bytes);
}

void FreeNameBytes(void* p)
{
#ifdef SALLY_LEGACY_CONVERT_STANDALONE
    if (TestFree != nullptr)
    {
        TestFree(p);
        return;
    }
#endif
    std::free(p);
}

char* DupNarrow(const char* text, std::size_t length)
{
    char* out = (char*)AllocNameBytes(length + 1);
    if (out == nullptr)
        return nullptr;
    if (length > 0)
        memcpy(out, text, length);
    out[length] = '\0';
    return out;
}

wchar_t* DupWide(const wchar_t* text, std::size_t length)
{
    wchar_t* out = (wchar_t*)AllocNameBytes((length + 1) * sizeof(wchar_t));
    if (out == nullptr)
        return nullptr;
    if (length > 0)
        memcpy(out, text, length * sizeof(wchar_t));
    out[length] = L'\0';
    return out;
}


// sdk107::CQuadWord and ::CQuadWord are DISTINCT types - that is the namespace
// isolation doing its job, and it means even a field with an identical layout
// needs an explicit conversion. Going through the Lo/Hi halves keeps this correct
// no matter what either side does with its own operators.
::CQuadWord SizeToLive(const sdk107::CQuadWord& in)
{
    ::CQuadWord out;
    out.LoDWord = in.LoDWord;
    out.HiDWord = in.HiDWord;
    return out;
}

sdk107::CQuadWord SizeToLegacy(const ::CQuadWord& in)
{
    sdk107::CQuadWord out;
    out.LoDWord = in.LoDWord;
    out.HiDWord = in.HiDWord;
    return out;
}

// The degrading counterpart of NarrowExactForCodePage, used in exactly one place:
// FileDataToLegacy, after the exact form has already refused with
// NotRepresentable, and only together with an exact NameW carrying the true
// spelling. No WC_NO_BEST_FIT_CHARS, no usedDefaultChar, no round trip - every
// character the code page cannot spell becomes the system default character, one
// for one, which is what a v107 plugin has always seen in Name.
//
// A lossy Name ALONE would be a lie. Paired with NameW it is what the frozen
// header asks for (spl_com.h:259 - "NULL if Name can represent the filename
// correctly ... use NameW when available for display and file operations"), and
// it is the difference between a plugin getting an approximate name it can still
// show and GetFile() handing it a NULL the frozen contract never mentions.
//
// defaultChar and usedDefaultChar are both null on purpose: that is the only form
// WideCharToMultiByte accepts for every code page including CP_UTF8, which GetACP
// can legitimately return.
bool NarrowLossyForCodePage(std::wstring_view wide, UINT codePage, std::string& narrow)
{
    if (wide.empty() || wide.size() > static_cast<std::size_t>(INT_MAX))
        return false;

    const int needed = WideCharToMultiByte(codePage, 0, wide.data(), (int)wide.size(),
                                          nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return false;

    std::string staged;
    try
    {
        staged.assign(static_cast<std::size_t>(needed), '\0');
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }

    const int written = WideCharToMultiByte(codePage, 0, wide.data(), (int)wide.size(),
                                           &staged[0], needed, nullptr, nullptr);
    if (written <= 0)
        return false;
    staged.resize(static_cast<std::size_t>(written));
    narrow.swap(staged);
    return true;
}

} // namespace

NarrowResult NarrowExactForCodePage(std::wstring_view wide, UINT codePage)
{
    NarrowResult result;
    if (wide.empty())
    {
        // An empty name is not "representable"; it is missing. Distinguishing
        // the two keeps callers from reporting a code-page problem that is
        // really a bug upstream.
        result.refusal = NarrowRefusal::Missing;
        SetLastError(ERROR_INVALID_PARAMETER);
        return result;
    }

    if (wide.size() > static_cast<std::size_t>(INT_MAX))
    {
        result.refusal = NarrowRefusal::TooLong;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return result;
    }

    const DWORD encodeFlags = codePage == CP_UTF8 ? WC_ERR_INVALID_CHARS
                                                   : WC_NO_BEST_FIT_CHARS;
    BOOL usedDefault = FALSE;
    const int needed = WideCharToMultiByte(codePage, encodeFlags,
                                          wide.data(), (int)wide.size(),
                                          nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
    {
        result.refusal = NarrowRefusal::NotRepresentable;
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return result;
    }

    std::string narrow;
    try
    {
        narrow.assign(static_cast<std::size_t>(needed), '\0');
    }
    catch (const std::bad_alloc&)
    {
        result.refusal = NarrowRefusal::OutOfMemory;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return result;
    }
    catch (const std::length_error&)
    {
        result.refusal = NarrowRefusal::TooLong;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return result;
    }
    const int written = WideCharToMultiByte(codePage, encodeFlags,
                                           wide.data(), (int)wide.size(),
                                           &narrow[0], needed,
                                           codePage == CP_UTF8 ? nullptr : "?",
                                           codePage == CP_UTF8 ? nullptr : &usedDefault);
    if (written <= 0 || usedDefault)
    {
        // usedDefault means at least one character became the substitution
        // character. That is precisely the case that would hand the plugin a
        // different file name than the user chose.
        result.refusal = NarrowRefusal::NotRepresentable;
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return result;
    }

    // Round-trip check. WC_NO_BEST_FIT_CHARS blocks the obvious substitutions,
    // but a code page can still map two distinct characters onto one byte, and
    // then two different files would narrow to the same name.
    const int back = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                                        narrow.c_str(), written, nullptr, 0);
    if (back <= 0)
    {
        result.refusal = NarrowRefusal::NotRepresentable;
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return result;
    }
    std::wstring round;
    try
    {
        round.assign(static_cast<std::size_t>(back), L'\0');
    }
    catch (const std::bad_alloc&)
    {
        result.refusal = NarrowRefusal::OutOfMemory;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return result;
    }
    catch (const std::length_error&)
    {
        result.refusal = NarrowRefusal::TooLong;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return result;
    }
    MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, narrow.c_str(), written,
                        &round[0], back);
    if (round.size() != wide.size() ||
        !std::equal(round.begin(), round.end(), wide.begin()))
    {
        result.refusal = NarrowRefusal::NotRepresentable;
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return result;
    }

    result.ok = true;
    result.value.swap(narrow);
    SetLastError(ERROR_SUCCESS);
    return result;
}

NarrowResult NarrowExact(std::wstring_view wide)
{
    return NarrowExactForCodePage(wide, GetACP());
}

DWORD Win32ErrorForNarrowRefusal(NarrowRefusal refusal) noexcept
{
    switch (refusal)
    {
    case NarrowRefusal::None:
        return ERROR_SUCCESS;
    case NarrowRefusal::Missing:
        return ERROR_INVALID_PARAMETER;
    case NarrowRefusal::TooLong:
        return ERROR_INSUFFICIENT_BUFFER;
    case NarrowRefusal::OutOfMemory:
        return ERROR_NOT_ENOUGH_MEMORY;
    case NarrowRefusal::NotRepresentable:
    default:
        return ERROR_NO_UNICODE_TRANSLATION;
    }
}

bool WidenPluginText(const char* ansi, std::wstring& wide)
{
    wide.clear();
    if (ansi == nullptr)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    const std::size_t byteLength = std::strlen(ansi);
    if (byteLength > static_cast<std::size_t>(INT_MAX))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    return WidenPluginSpan(ansi, static_cast<int>(byteLength), wide);
}

bool WidenPluginSpanForCodePage(const char* ansi, int byteLength,
                                UINT codePage, std::wstring& wide)
{
    wide.clear();
    if (ansi == nullptr || byteLength < 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (byteLength == 0)
    {
        SetLastError(ERROR_SUCCESS);
        return true;
    }
    if (std::memchr(ansi, '\0', static_cast<std::size_t>(byteLength)) !=
        nullptr)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    return WidenPluginBytesForCodePage(ansi, byteLength, codePage, wide);
}

bool WidenPluginBytesForCodePage(const char* ansi, int byteLength,
                                 UINT codePage, std::wstring& wide)
{
    wide.clear();
    if (ansi == nullptr || byteLength < 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (byteLength == 0)
    {
        SetLastError(ERROR_SUCCESS);
        return true;
    }

    const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    const int needed = MultiByteToWideChar(codePage, flags,
                                           ansi, byteLength,
                                           nullptr, 0);
    if (needed <= 0)
    {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return false;
    }
    try
    {
        wide.resize(static_cast<std::size_t>(needed));
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    const int written = MultiByteToWideChar(codePage, flags,
                                            ansi, byteLength,
                                            wide.data(), needed);
    if (written != needed)
    {
        wide.clear();
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    return true;
}

bool WidenPluginSpan(const char* ansi, int byteLength, std::wstring& wide)
{
    return WidenPluginSpanForCodePage(ansi, byteLength, GetACP(), wide);
}

bool WidenPluginBytes(const char* ansi, int byteLength, std::wstring& wide)
{
    return WidenPluginBytesForCodePage(ansi, byteLength, GetACP(), wide);
}

::CQuadWord QuadWordFromLegacy(const sdk107::CQuadWord& legacy)
{
    return SizeToLive(legacy);
}

sdk107::CQuadWord QuadWordToLegacy(const ::CQuadWord& wide)
{
    return SizeToLegacy(wide);
}

void RecomputeExtNarrow(sdk107::CFileData& row)
{
    if (row.Name == nullptr)
    {
        row.Ext = nullptr;
        return;
    }
    const std::size_t len = std::strlen(row.Name);
    // Search backwards for a dot, INCLUDING one at index 0. spl_com.h:204 and the frozen
    // sdk107/spl_com.h:249 both spell the rule out: Ext is a "pointer into Name after the
    // first dot from the right (including dot at the beginning of name, on Windows it is
    // considered as extension, unlike on UNIX)".
    //
    // This loop used to start at i > 1 and claim in a comment that excluding index 0 was
    // "the v107 rule verbatim". It is the inverse of it, and four other places in this tree
    // already say so: Sally's own disk enumeration (files_window_directory_read.cpp:552,
    // '".cvspass" in Windows is an extension'), pre-unicode's execute.cpp, the CFileData
    // invariant suite (gtest_filedata_invariants asserts ".gitignore" yields Ext
    // "gitignore"), and masks.cpp:666, which detects this adapter's disagreement at runtime
    // and TRACE_E's about it. Every dotfile a v107 plugin listed lost its extension: blank
    // Type column, wrong sort group, no match for a *.gitignore filter or association.
    for (std::size_t i = len; i > 0; i--)
    {
        if (row.Name[i - 1] == '.')
        {
            row.Ext = row.Name + i;
            return;
        }
    }
    row.Ext = row.Name + len; // no extension: point at the terminator
}

// A v107 plugin may deliberately declare "this row has NO extension" by pointing Ext at
// Name's terminator even when Name contains a dot. spl_com.h:249-253 sanctions exactly
// that: when SALCFG_SORTBYEXTDIRSASFILES is FALSE, "Ext for directories points to the end
// of Name (directories have no extensions)". Every in-tree plugin that lists directories
// queries that config and does it (folders/fs_operations.cpp:200, ftp/ftp.cpp:367,
// tar/untar.cpp:1085, 7zip/7zip.cpp:167, undelete, unfat, unarj, demoplug).
// Recomputing unconditionally clobbered the choice, so a directory named "src.old" got
// "old" in the Type column, sorted into the .old extension group, and matched an *.old
// filter it must not match. Only this documented case is honoured - for files the contract
// mandates the first dot from the right, so recomputing is correct there.
template <class TChar>
bool DeclaresNoExtension(const TChar* name, const TChar* ext, std::size_t nameLen)
{
    return name != nullptr && ext != nullptr && ext >= name && ext <= name + nameLen &&
           *ext == static_cast<TChar>(0);
}

void RecomputeExtWide(::CFileData& row)
{
    if (row.Name == nullptr)
    {
        row.Ext = nullptr;
        return;
    }
    const std::size_t len = std::wcslen(row.Name);
    // Same inclusive rule as RecomputeExtNarrow above; see the note there.
    for (std::size_t i = len; i > 0; i--)
    {
        if (row.Name[i - 1] == '.')
        {
            row.Ext = row.Name + i;
            return;
        }
    }
    row.Ext = row.Name + len;
}

bool FileDataFromLegacy(const sdk107::CFileData& in, ::CFileData& out,
                        bool trustNameW)
{
    out = ::CFileData();
    std::memset(&out, 0, sizeof(out));

    if (in.Name == nullptr)
        return false;

    // v107 may carry a lossless NameW beside its ANSI primary. That is the
    // authoritative spelling when present; otherwise decode the plugin bytes
    // transactionally so malformed input cannot become an empty live name.
    std::wstring wideName;
    try
    {
        if (trustNameW && in.NameW != nullptr && in.NameW[0] != L'\0')
            wideName.assign(in.NameW);
        else if (!WidenPluginText(in.Name, wideName))
            return false;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    if (wideName.empty())
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    const std::size_t nameLen = wideName.size();
    out.Name = DupWide(wideName.c_str(), nameLen);
    if (out.Name == nullptr)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }

    // Ext must be RECOMPUTED: the plugin's Ext points into the plugin's buffer,
    // so copying the pointer would leave it dangling into memory we do not own.
    // The one exception is the plugin declaring "no extension" outright, which is
    // offset-independent and therefore safe to carry across - see DeclaresNoExtension.
    if (in.Name != nullptr && DeclaresNoExtension(in.Name, in.Ext, std::strlen(in.Name)))
        out.Ext = out.Name + nameLen;
    else
        RecomputeExtWide(out);

    if (in.DosName != nullptr)
    {
        std::wstring dosName;
        if (!WidenPluginText(in.DosName, dosName))
        {
            FreeConvertedFileData(out);
            return false;
        }
        out.DosName = DupWide(dosName.c_str(), dosName.size());
        if (out.DosName == nullptr)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            FreeConvertedFileData(out);
            return false;
        }
    }

    out.Size = SizeToLive(in.Size);
    out.Attr = in.Attr;
    out.LastWrite = in.LastWrite;
    out.PluginData = in.PluginData;
    // INBOUND: `out` is the LIVE ::CFileData, whose NameLen is a full-width
    // DWORD count of WCHARs precisely so a plugin-supplied name is no longer
    // bounded by the frozen 9-bit field. Clamping here would re-impose that
    // ceiling on core state and desynchronise NameLen from Name — RecomputeExtWide
    // above places Ext using the full wcslen, so a clamp puts Ext past
    // Name + NameLen and breaks the row invariant.
    out.NameLen = (DWORD)nameLen;
    out.Hidden = in.Hidden;
    out.IsLink = in.IsLink;
    out.IsOffline = in.IsOffline;
    out.IconOverlayIndex = in.IconOverlayIndex;
    out.Association = in.Association;
    out.Selected = in.Selected;
    out.Shared = in.Shared;
    out.Archive = in.Archive;
    out.SizeValid = in.SizeValid;
    out.Dirty = in.Dirty;
    out.CutToClip = in.CutToClip;
    out.IconOverlayDone = in.IconOverlayDone;
    return true;
}

namespace
{

bool ConvertRowToLegacy(const ::CFileData& in, sdk107::CFileData& out,
                        NarrowRefusal* refusal, bool allowWideNameFallback)
{
    std::memset(&out, 0, sizeof(out));
    if (refusal != nullptr)
        *refusal = NarrowRefusal::None;

    // Name is the sole wide truth on the live side. Narrow it exactly when the code
    // page can spell it; handing a plugin an approximated name ALONE makes it act on
    // the wrong file.
    const NarrowResult narrowed = NarrowExact(
        in.Name != nullptr ? std::wstring_view(in.Name) : std::wstring_view());

    std::string nameBytes;
    bool approximate = false;
    if (narrowed.ok)
    {
        nameBytes = narrowed.value;
    }
    else if (allowWideNameFallback && narrowed.refusal == NarrowRefusal::NotRepresentable &&
             NarrowLossyForCodePage(std::wstring_view(in.Name), GetACP(), nameBytes))
    {
        // Only this one refusal has a side channel, and only for the callers that
        // cannot answer NULL (see FileDataToLegacyWithWideNameFallback). Missing,
        // TooLong and OutOfMemory are not encoding problems - there is nothing an
        // exact NameW could carry that would make them safe - so they still refuse.
        approximate = true;
        SetLastError(ERROR_SUCCESS);
    }
    else
    {
        if (refusal != nullptr)
            *refusal = narrowed.refusal;
        return false;
    }

    out.Name = DupNarrow(nameBytes.c_str(), nameBytes.size());
    if (out.Name == nullptr)
    {
        if (refusal != nullptr)
            *refusal = NarrowRefusal::OutOfMemory;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    // Same "no extension" carry-across as the inbound direction: the wide core marks a
    // directory extension-less the same way, and the v107 plugin must see that choice.
    if (in.Name != nullptr && DeclaresNoExtension(in.Name, in.Ext, std::wcslen(in.Name)))
        out.Ext = out.Name + nameBytes.size();
    else
        RecomputeExtNarrow(out);

    // NameW is v107's side channel for exactly this: "allocated wide (Unicode) file
    // name, NULL if Name can represent the filename correctly" (spl_com.h:259). So it
    // stays null whenever Name round-trips, and carries the true spelling whenever
    // Name is an approximation. UseWideName() then tells the plugin which it has.
    if (approximate)
    {
        const std::wstring_view exact(in.Name);
        out.NameW = DupWide(exact.data(), exact.size());
        if (out.NameW == nullptr)
        {
            if (refusal != nullptr)
                *refusal = NarrowRefusal::OutOfMemory;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            FreeLegacyFileData(out);
            return false;
        }
    }

    if (in.DosName != nullptr && in.DosName[0] != L'\0')
    {
        const NarrowResult dosName = NarrowExact(in.DosName);
        if (!dosName.ok)
        {
            if (refusal != nullptr)
                *refusal = dosName.refusal;
            FreeLegacyFileData(out);
            return false;
        }
        out.DosName = DupNarrow(dosName.value.c_str(), dosName.value.size());
        if (out.DosName == nullptr)
        {
            if (refusal != nullptr)
                *refusal = NarrowRefusal::OutOfMemory;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            FreeLegacyFileData(out);
            return false;
        }
    }

    out.Size = SizeToLegacy(in.Size);
    out.Attr = in.Attr;
    out.LastWrite = in.LastWrite;
    out.PluginData = in.PluginData;
    // OUTBOUND: the frozen sdk107 NameLen is a 9-bit field that physically cannot
    // carry more than 0x1FF, and v107 documents names as bounded by MAX_PATH - 5.
    // Refusing here is not an option: GetFile/GetDir have no documented NULL case
    // (see the note in legacy_directory_to_core.cpp), so answering NULL would crash
    // a conforming plugin. `Name` itself stays complete and NUL-terminated, so a
    // plugin using strlen(Name) is unaffected; only one reading NameLen sees the
    // clamp. Recorded by LegacyConvert.OutboundNameLenClampsAtFrozenFieldMax.
    out.NameLen = (unsigned)(nameBytes.size() > 0x1FF ? 0x1FF : nameBytes.size());
    out.Hidden = in.Hidden;
    out.IsLink = in.IsLink;
    out.IsOffline = in.IsOffline;
    out.IconOverlayIndex = in.IconOverlayIndex;
    out.Association = in.Association;
    out.Selected = in.Selected;
    out.Shared = in.Shared;
    out.Archive = in.Archive;
    out.SizeValid = in.SizeValid;
    out.Dirty = in.Dirty;
    out.CutToClip = in.CutToClip;
    out.IconOverlayDone = in.IconOverlayDone;
    return true;
}

} // namespace

bool FileDataToLegacy(const ::CFileData& in, sdk107::CFileData& out,
                      NarrowRefusal* refusal)
{
    return ConvertRowToLegacy(in, out, refusal, /*allowWideNameFallback*/ false);
}

bool FileDataToLegacyWithWideNameFallback(const ::CFileData& in, sdk107::CFileData& out,
                                          NarrowRefusal* refusal)
{
    return ConvertRowToLegacy(in, out, refusal, /*allowWideNameFallback*/ true);
}

#ifdef SALLY_LEGACY_CONVERT_STANDALONE
void SetLegacyNameAllocationHooksForTests(LegacyNameAllocForTests alloc,
                                          LegacyNameFreeForTests free)
{
    TestAlloc = alloc;
    TestFree = free;
}
#endif

void FreeLegacyFileData(sdk107::CFileData& row)
{
    // Null after freeing so a second call is safe: a double free inside someone
    // else's plugin is a crash with our name on it.
    if (row.Name != nullptr)
    {
        FreeNameBytes(row.Name);
        row.Name = nullptr;
    }
    if (row.NameW != nullptr)
    {
        FreeNameBytes(row.NameW);
        row.NameW = nullptr;
    }
    if (row.DosName != nullptr)
    {
        FreeNameBytes(row.DosName);
        row.DosName = nullptr;
    }
    row.Ext = nullptr; // interior pointer: never freed, always invalidated
}

void FreeConvertedFileData(::CFileData& row)
{
    if (row.Name != nullptr)
    {
        FreeNameBytes(row.Name);
        row.Name = nullptr;
    }
    if (row.DosName != nullptr)
    {
        FreeNameBytes(row.DosName);
        row.DosName = nullptr;
    }
    row.Ext = nullptr;
}

bool NarrowEnumeratedName(const std::wstring& wide, std::string& out,
                          EnumNarrowStats& stats)
{
    NarrowResult narrowed = NarrowExact(wide);
    if (!narrowed.ok)
    {
        stats.skippedNotRepresentable++;
        out.clear();
        return false;
    }
    out.swap(narrowed.value);
    stats.delivered++;
    return true;
}

CLegacySelection2Bridge::CLegacySelection2Bridge(::SalEnumSelection2 next,
                                                 void* nextParam)
    : NextWide(next), NextParam(nextParam)
{
}

const char* CLegacySelection2Bridge::Next(
    HWND parent, int enumFiles, const char** dosName, BOOL* isDir,
    sdk107::CQuadWord* size, DWORD* attr, FILETIME* lastWrite,
    int* errorOccurred)
{
    if (enumFiles == -1)
    {
        Name.clear();
        DosName.clear();
        Skipped = false;
        if (NextWide != nullptr)
            NextWide(parent, enumFiles, nullptr, nullptr, nullptr, nullptr,
                     nullptr, NextParam, nullptr);
        if (dosName != nullptr)
            *dosName = nullptr;
        if (isDir != nullptr)
            *isDir = FALSE;
        if (size != nullptr)
            *size = sdk107::CQuadWord(0, 0);
        if (attr != nullptr)
            *attr = 0;
        if (lastWrite != nullptr)
            *lastWrite = {};
        if (errorOccurred != nullptr)
            *errorOccurred = SALENUM_SUCCESS;
        return nullptr;
    }

    if (NextWide == nullptr)
    {
        if (dosName != nullptr)
            *dosName = nullptr;
        if (errorOccurred != nullptr)
            *errorOccurred = SALENUM_ERROR;
        return nullptr;
    }

    for (;;)
    {
        const wchar_t* wideDosName = nullptr;
        BOOL wideIsDir = FALSE;
        ::CQuadWord wideSize(0, 0);
        DWORD wideAttr = 0;
        FILETIME wideLastWrite = {};
        int wideError = SALENUM_SUCCESS;
        const wchar_t* wideName = NextWide(
            parent, enumFiles, dosName != nullptr ? &wideDosName : nullptr,
            isDir != nullptr ? &wideIsDir : nullptr,
            size != nullptr ? &wideSize : nullptr,
            attr != nullptr ? &wideAttr : nullptr,
            lastWrite != nullptr ? &wideLastWrite : nullptr, NextParam,
            &wideError);
        if (wideName == nullptr)
        {
            if (dosName != nullptr)
                *dosName = nullptr;
            if (errorOccurred != nullptr)
                *errorOccurred =
                    Skipped && wideError == SALENUM_SUCCESS ? SALENUM_ERROR
                                                            : wideError;
            return nullptr;
        }

        NarrowResult narrowed = NarrowExact(wideName);
        NarrowResult narrowedDos;
        if (wideDosName != nullptr)
            narrowedDos = NarrowExact(wideDosName);
        if (!narrowed.ok || (wideDosName != nullptr && !narrowedDos.ok))
        {
            Skipped = true;
            if (wideError == SALENUM_CANCEL)
            {
                if (dosName != nullptr)
                    *dosName = nullptr;
                if (errorOccurred != nullptr)
                    *errorOccurred = SALENUM_CANCEL;
                return nullptr;
            }
            continue;
        }

        Name.swap(narrowed.value);
        if (wideDosName != nullptr)
            DosName.swap(narrowedDos.value);
        else
            DosName.clear();
        if (dosName != nullptr)
            *dosName = wideDosName != nullptr ? DosName.c_str() : nullptr;
        if (isDir != nullptr)
            *isDir = wideIsDir;
        if (size != nullptr)
            *size = QuadWordToLegacy(wideSize);
        if (attr != nullptr)
            *attr = wideAttr;
        if (lastWrite != nullptr)
            *lastWrite = wideLastWrite;
        if (errorOccurred != nullptr)
            *errorOccurred =
                Skipped && wideError == SALENUM_SUCCESS ? SALENUM_ERROR
                                                        : wideError;
        return Name.c_str();
    }
}

const char* WINAPI CLegacySelection2Bridge::Invoke(
    HWND parent, int enumFiles, const char** dosName, BOOL* isDir,
    sdk107::CQuadWord* size, DWORD* attr, FILETIME* lastWrite,
    void* parameter, int* errorOccurred)
{
    if (parameter == nullptr)
        return nullptr;
    return static_cast<CLegacySelection2Bridge*>(parameter)->Next(
        parent, enumFiles, dosName, isDir, size, attr, lastWrite,
        errorOccurred);
}

} // namespace sally::compat

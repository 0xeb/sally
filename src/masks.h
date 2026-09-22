// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// functions used by the quick-search feature
BOOL IsQSWildChar(wchar_t ch);
std::wstring PrepareQSMask(const wchar_t* src);
BOOL AgreeQSMask(const wchar_t* filename, BOOL hasExtension, const wchar_t* mask, BOOL wholeString, int& offset);

// wildcards '*' (any string) + '?' (any character) <+ '#' (a digit) if extendedMode==TRUE>
void PrepareMask(wchar_t* mask, const wchar_t* src);                                                // converts the mask into the chosen format
BOOL AgreeMask(const wchar_t* filename, const wchar_t* mask, BOOL hasExtension, BOOL extendedMode); // does it match the mask ??
// Wide form. Folds with CharLowerW per character instead of the
// CP_ACP LowerCase[] table, so a mask containing non-ANSI characters matches,
// and characters differing only outside the code page stop colliding.

// adjusts the name according to the mask and stores the result in buffer 'buffer' of size 'bufSize'
// 'name' is the name to adjust; 'mask' is the mask (unmodified - do not call PrepareMask on it)
// returns 'buffer' on success (even if the name was truncated because the buffer was too small),
// otherwise NULL; NOTE: behaves like the "copy" command in Win2K
wchar_t* MaskName(wchar_t* buffer, int bufSize, const wchar_t* name, const wchar_t* mask);
std::wstring MaskNameOwnedW(const wchar_t* name, const wchar_t* mask);

//*****************************************************************************
//
// CMaskGroup
//
// Life cycle:
//   1) In the constructor or SetMasksString method, provide a group of masks.
//   2) Call PrepareMasks to build internal data; if it fails, display the error
//      location and after fixing the mask, return to step (2).
//   3) Call AgreeMasks at any time to check whether a name matches the mask group.
//   4) If SetMasksString is called again, continue from step (2).
//
// Mask:
//   '?' - any character
//   '*' - any string (including empty)
//   '#' - any digit (only if 'extendedMode'==TRUE)
//
//   Examples:
//     *     - all names
//     *.*   - all names
//     *.exe - names with the "exe" extension
//     *.t?? - names with an extension starting with 't' and having two additional characters
//     *.r## - names with an extension starting with 'r' and two additional digits
//
// Mask group:
//   Masks are separated by ';' character. The '|' character can also be used as a separator
//   that has a special meaning. All masks following '|' are treated inversely,
//   meaning AgreeMasks returns FALSE if a name matches them.
//   The '|' separator may appear only once in the mask group and must be followed by at least one mask.
//   If nothing precedes '|', a "*" mask is automatically inserted.
//
//   Examples:
//     *.txt;*.cpp - all names with the txt or cpp extension
//     *.h*|*.html - all names whose extension starts with 'h' but not names with the extension "html"
//     |*.txt      - all names with an extension other than "txt"
//

#define MASK_OPTIMIZE_NONE 0      // no optimization
#define MASK_OPTIMIZE_ALL 1       // mask satisfies all requests (*.* or *)
#define MASK_OPTIMIZE_EXTENSION 2 // mask is in form (*.xxxx) where xxxx is the extension

struct CMaskItemFlags
{
    unsigned Optimize : 7; // MASK_OPTIMIZE_xxx
    unsigned Exclude : 1;  // if 1, this is an exclude mask; otherwise, it is an include mask
                           // exclude masks are stored before include masks in PreparedMasks array
};

struct CMasksHashEntry
{
    wchar_t* Mask;         // packed wide mask; see the format note below
    CMasksHashEntry* Next; // next entry with the same hash
};

// A PREPARED MASK IS A PACKED BUFFER, not a string.
//
//   element 0   : flags (Optimize in the low 7 bits, Exclude in bit 7)
//   element 1.. : the mask text
//
// and the code reaches past it by raw offset - `mask + 3` is "flags, '*', '.'",
// i.e. the extension of a MASK_OPTIMIZE_EXTENSION mask. That arithmetic carries
// over unchanged now the element is wchar_t.
//
// WHAT DID NOT CARRY OVER is the old `(CMaskItemFlags*)buffer` cast. Over a
// wchar_t* it would alias only the low byte of the first code unit on
// little-endian, leaving the high byte uninitialised - something that works in
// testing and corrupts on a different build. So the flags are now a plain value
// behind these accessors and nothing aliases the buffer.
inline unsigned MaskItemOptimize(const wchar_t* mask) { return (unsigned)(mask[0] & 0x7F); }
inline unsigned MaskItemExclude(const wchar_t* mask) { return (unsigned)((mask[0] >> 7) & 1); }
inline void SetMaskItemFlags(wchar_t* mask, unsigned optimize, unsigned exclude)
{
    mask[0] = (wchar_t)((optimize & 0x7F) | ((exclude & 1) << 7));
}

class CMaskGroup
{
protected:
    // Dynamically owned UTF-16 is the sole storage. The unsuffixed methods below
    // remain source-compatible aliases; they neither convert nor own a mirror.
    std::wstring MasksString;              // mask group passed in the constructor or in PrepareMasks
    TDirectArray<wchar_t*> PreparedMasks;  // packed masks; see the note above for the format
    BOOL NeedPrepare;                      // is it necessary to call the PrepareMasks method before using 'PreparedMasks'?
    BOOL ExtendedMode;

    // MasksStringNarrow is gone. It was described as a "narrow rendering
    // so GetMasksString() can keep returning a stable const char*", but it had been
    // declared wchar_t[] and GetMasksString had been declared wide, so it rendered a lossy
    // CP_ACP copy that no declaration allowed anyone to receive.

    CMasksHashEntry* MasksHashArray; // if not NULL, it is a hash array containing all masks with MASK_OPTIMIZE_EXTENSION format (only those with Exclude==0)
    int MasksHashArraySize;          // size of MasksHashArray (twice the number of stored masks)

public:
    CMaskGroup();
    CMaskGroup(const wchar_t* masks, BOOL extendedMode = FALSE);
    ~CMaskGroup();
    void Release();

    CMaskGroup& operator=(const CMaskGroup& s);

    // WIDE PRIMARY.
    void SetMasksString(const wchar_t* masks, BOOL extendedMode = FALSE);
    const wchar_t* GetMasksString();

    BOOL GetExtendedMode();

    // Converts the mask group passed to the constructor or by the SetMasksString method
    // into an internal representation that allows calling AgreeMasks. If 'extendedMode' is TRUE, the
    // mask syntax is extended with '#' representing any digit.
    // Returns TRUE on success or FALSE on error. In case of an error,
    // 'errorPos' contains the index of the character (in the provided mask group) that caused the error.
    // If memory is low, 'errorPos' is set to 0.
    // If masksString == NULL, CMaskGroup::MasksString is used; otherwise the
    // provided 'masksString' is used (in that case, AgreeMasks can be called —
    // CMaskGroup::MasksString is ignored).
    BOOL PrepareMasks(int& errorPos, const wchar_t* masksString = NULL);
    // wide primary

    // Determines whether 'fileName' matches the mask group.
    // NOTE: 'fileName' must not be a full path, only in the form name.ext
    // fileExt must point either to the terminator of fileName or to the extension (if it exists)
    // if fileExt == NULL, the extension will be searched for - this is slower
    BOOL AgreeMasks(const wchar_t* fileName, const wchar_t* fileExt);
    // wide primary

protected:
    // releases the hash array MasksHashArray
    void ReleaseMasksHashArray();
};

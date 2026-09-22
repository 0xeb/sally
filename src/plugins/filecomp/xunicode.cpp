// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "common/unicode/LegacyByteCellMap.h"
#include "common/Win32TextCodec.h"

char* TCharSpecific<char>::LowerCase = ::LowerCase;
unsigned short* TCharSpecific<char>::CType = ::CType;
wchar_t TCharSpecific<char>::Display[256];

wchar_t TCharSpecific<wchar_t>::LowerCase[256 * 256];
unsigned short TCharSpecific<wchar_t>::CType[256 * 256];

bool DecodeFileCompLegacyText(const char* bytes, size_t byteCount,
                              std::wstring& text) noexcept
{
    // Permissive: this decodes the CONTENT of an 8-bit text file the user asked
    // to compare, and its caller turns a failure into an exception that refuses
    // the whole file. Strict decoding therefore meant a single byte the active
    // code page cannot map made File Comparator unable to open the file at all,
    // where legacy used MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, ...) and
    // simply showed the text. Rendering a file is the best-effort case
    // Win32TextCodec.h reserves the permissive variants for.
    return static_cast<bool>(Win32DecodeTextPermissive(CP_ACP, bytes, byteCount, text));
}

bool EncodeFileCompLegacyText(const wchar_t* text, size_t characterCount,
                              std::string& bytes) noexcept
{
    return static_cast<bool>(Win32EncodeText(CP_ACP, text, characterCount, bytes));
}

static wchar_t ProjectFileCompLegacyByteCell(char byte) noexcept
{
    std::wstring display;
    if (!DecodeFileCompLegacyText(&byte, 1, display) || display.size() != 1)
        return L'\uFFFD';
    return display.front();
}

wchar_t
TCharSpecific<wchar_t>::ConvertANSI8Char(char c)
{
    return ProjectFileCompLegacyByteCell(c);
}

void InitXUnicode()
{
    _ASSERT(TCharSpecific<char>::CType == ::CType);
    _ASSERT(TCharSpecific<char>::LowerCase == ::LowerCase);

    _ASSERT(sizeof(wchar_t) == 2);

    for (int byteValue = 0; byteValue < 256; ++byteValue)
    {
        const char byte = static_cast<char>(byteValue);
        TCharSpecific<char>::Display[byteValue] = ProjectFileCompLegacyByteCell(byte);
    }

    wchar_t charTable[256 * 256];
    uintptr_t i;
    for (i = 0; i < 256 * 256; i++)
    {
        // special handling for surrogates
        if (i >= 0xD800 && i <= 0xDFFF)
        {
            charTable[i] = 0;
            TCharSpecific<wchar_t>::LowerCase[i] = (wchar_t)i;
        }
        else
        {
            charTable[i] = wchar_t(i);
            TCharSpecific<wchar_t>::LowerCase[i] = (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)i);
        }
    }

    if (!GetStringTypeW(CT_CTYPE1, charTable, 256 * 256, TCharSpecific<wchar_t>::CType))
    {
        TRACE_E("GetStringTypeW failed. Last Error = " << GetLastError());
    }

    // sanitize surrogates in CType table
    for (i = 0xD800; i <= 0xDFFF; i++)
        TCharSpecific<wchar_t>::CType[i] = 0;
}

BOOL DrawFileCompText(HDC hdc, int X, int Y, UINT fuOptions, CONST RECT* lprc,
                      LPCSTR lpString, UINT cbCount, CONST INT* lpDx) noexcept
{
    std::wstring display;
    if (!sally::unicode::TryMapLegacyByteCells(
            lpString, cbCount, TCharSpecific<char>::Display, display))
        return FALSE;
    return ExtTextOutW(hdc, X, Y, fuOptions, lprc, display.c_str(), cbCount, lpDx);
}

int MeasureFileCompText(HDC hdc, LPCSTR text, int textLength, LPRECT bounds,
                        UINT format, LPDRAWTEXTPARAMS parameters) noexcept
{
    if (textLength < -1 || text == nullptr || (format & DT_MODIFYSTRING) != 0)
        return 0;

    size_t byteCount = textLength == -1 ? strlen(text) : static_cast<size_t>(textLength);
    if (byteCount > static_cast<size_t>(INT_MAX))
        return 0;

    std::wstring display;
    if (!sally::unicode::TryMapLegacyByteCells(
            text, byteCount, TCharSpecific<char>::Display, display))
        return 0;
    return DrawTextExW(hdc, display.data(), static_cast<int>(byteCount), bounds,
                       format, parameters);
}

BOOL DrawFileCompTextRuns(HDC hdc, const TFileCompTextRun<char>* runs,
                          int runCount) noexcept
{
    if (runCount < 0 || (runs == nullptr && runCount != 0))
        return FALSE;

    try
    {
        std::vector<std::wstring> displayText(static_cast<size_t>(runCount));
        std::vector<POLYTEXTW> nativeRuns(static_cast<size_t>(runCount));
        for (int i = 0; i < runCount; ++i)
        {
            if (!sally::unicode::TryMapLegacyByteCells(
                    runs[i].lpstr, runs[i].n, TCharSpecific<char>::Display,
                    displayText[i]))
                return FALSE;

            nativeRuns[i].x = runs[i].x;
            nativeRuns[i].y = runs[i].y;
            nativeRuns[i].n = runs[i].n;
            nativeRuns[i].lpstr = displayText[i].c_str();
            nativeRuns[i].uiFlags = runs[i].uiFlags;
            nativeRuns[i].rcl = runs[i].rcl;
            nativeRuns[i].pdx = runs[i].pdx;
        }
        return PolyTextOutW(hdc, nativeRuns.data(), runCount);
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL CopyFileCompSelectionToClipboard(const char* text, size_t byteCount,
                                      BOOL showEcho, HWND echoParent) noexcept
{
    std::wstring decoded;
    if (!DecodeFileCompLegacyText(text, byteCount, decoded) ||
        decoded.size() > static_cast<size_t>(INT_MAX))
        return FALSE;
    try
    {
        return SG->CopyTextToClipboard(decoded.data(), static_cast<int>(decoded.size()),
                                       showEcho, echoParent);
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL CopyFileCompSelectionToClipboard(const wchar_t* text, size_t characterCount,
                                      BOOL showEcho, HWND echoParent) noexcept
{
    if ((text == nullptr && characterCount != 0) ||
        characterCount > static_cast<size_t>(INT_MAX))
        return FALSE;
    try
    {
        return SG->CopyTextToClipboard(text, static_cast<int>(characterCount),
                                       showEcho, echoParent);
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL CopyFileCompByteCellsToClipboard(const char* bytes, size_t byteCount,
                                      BOOL showEcho, HWND echoParent) noexcept
{
    std::wstring cells;
    if (!sally::unicode::TryMapLegacyByteCells(
            bytes, byteCount, TCharSpecific<char>::Display, cells) ||
        cells.size() > static_cast<size_t>(INT_MAX))
        return FALSE;
    try
    {
        return SG->CopyTextToClipboard(cells.data(), static_cast<int>(cells.size()),
                                       showEcho, echoParent);
    }
    catch (...)
    {
        return FALSE;
    }
}

// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define IS_COMBINING_DIACRITIC(c) (((c) >= 0x300) && ((c) <= 0x36f))

template <class CChar>
class TCharSpecific
{
};

template <class CChar>
struct TFileCompTextRun
{
    int x;
    int y;
    UINT n;
    const CChar* lpstr;
    UINT uiFlags;
    RECT rcl;
    INT* pdx;
};

template <>
class TCharSpecific<char>
{
public:
    typedef TFileCompTextRun<char> POLYTEXT;
    typedef unsigned char Unsigned;

    static char* LowerCase;
    static unsigned short* CType;
    static wchar_t Display[256];

    static const bool IsUnicode() { return false; }
    static char ConvertANSI8Char(char c) { return c; }
    static bool IsBOM(char c) { return false; }
    static int CharCount() { return 256; }
    static bool IsValidChar(char c) { return true; }
};

template <>
class TCharSpecific<wchar_t>
{
public:
    typedef TFileCompTextRun<wchar_t> POLYTEXT;
    typedef unsigned short Unsigned;

    static wchar_t LowerCase[256 * 256];
    static unsigned short CType[256 * 256];

    static const bool IsUnicode() { return true; }
    static wchar_t ConvertANSI8Char(char c);
    static bool IsBOM(wchar_t c) { return c == 0xFEFF; }
    static int CharCount() { return 256 * 256; }
    static bool IsValidChar(wchar_t c) { return c != 0xFFFE && c != 0xFFFF && (c < 0xD800 || c > 0xDFFF); }
};

inline const char*
MemChr(const char* buf, char c, size_t count)
{
    return (const char*)memchr(buf, c, count);
}

inline const wchar_t*
MemChr(const wchar_t* buf, wchar_t c, size_t count)
{
    size_t i = 0;
    while (i < count && buf[i] != c)
        i++;

    return buf + i;
}

template <class CChar>
inline CChar ToLowerX(CChar c)
{
    return TCharSpecific<CChar>::LowerCase[TCharSpecific<CChar>::Unsigned(c)];
}

template <class CChar>
inline int IsSpaceX(CChar c)
{
    return TCharSpecific<CChar>::CType[TCharSpecific<CChar>::Unsigned(c)] & C1_SPACE;
}

template <class CChar>
inline int IsWordX(CChar c)
{
    return (TCharSpecific<CChar>::CType[TCharSpecific<CChar>::Unsigned(c)] & (C1_ALPHA | C1_DIGIT)) || c == '_';
}

// Byte-mode comparisons deliberately preserve one visual cell per source byte.
// Their GDI adapter maps those cells to UTF-16 dynamically; byte counts and
// narrow Win32 render-record ownership never reach the UI boundary.
BOOL DrawFileCompText(HDC hdc, int X, int Y, UINT fuOptions, CONST RECT* lprc,
                      LPCSTR lpString, UINT cbCount, CONST INT* lpDx) noexcept;

inline BOOL DrawFileCompText(HDC hdc, int X, int Y, UINT fuOptions, CONST RECT* lprc,
                             LPCWSTR lpString, UINT cbCount, CONST INT* lpDx) noexcept
{
    return ExtTextOutW(hdc, X, Y, fuOptions, lprc, lpString, cbCount, lpDx);
}

int MeasureFileCompText(HDC hdc, LPCSTR text, int textLength, LPRECT bounds,
                        UINT format, LPDRAWTEXTPARAMS parameters) noexcept;

inline int MeasureFileCompText(HDC hdc, LPWSTR text, int textLength, LPRECT bounds,
                               UINT format, LPDRAWTEXTPARAMS parameters) noexcept
{
    return DrawTextExW(hdc, text, textLength, bounds, format, parameters);
}

BOOL DrawFileCompTextRuns(HDC hdc, const TFileCompTextRun<char>* runs,
                          int runCount) noexcept;

inline BOOL DrawFileCompTextRuns(HDC hdc, const TFileCompTextRun<wchar_t>* runs,
                                 int runCount) noexcept
{
    if (runCount < 0 || (runs == nullptr && runCount != 0))
        return FALSE;

    try
    {
        std::vector<POLYTEXTW> nativeRuns(static_cast<size_t>(runCount));
        for (int i = 0; i < runCount; ++i)
        {
            nativeRuns[i].x = runs[i].x;
            nativeRuns[i].y = runs[i].y;
            nativeRuns[i].n = runs[i].n;
            nativeRuns[i].lpstr = runs[i].lpstr;
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

// Byte-mode selections are decoded as an explicit ACP text span. The resulting
// UTF-16 count, never the source byte count, reaches the wide SDK clipboard.
BOOL CopyFileCompSelectionToClipboard(const char* text, size_t byteCount,
                                      BOOL showEcho, HWND echoParent) noexcept;
BOOL CopyFileCompSelectionToClipboard(const wchar_t* text, size_t characterCount,
                                      BOOL showEcho, HWND echoParent) noexcept;
BOOL CopyFileCompByteCellsToClipboard(const char* bytes, size_t byteCount,
                                      BOOL showEcho, HWND echoParent) noexcept;

// FileComp's legacy configuration names and byte-mode text are encoded in the
// active Windows code page. Keep that byte boundary named and centralized.
bool DecodeFileCompLegacyText(const char* bytes, size_t byteCount,
                              std::wstring& text) noexcept;
bool EncodeFileCompLegacyText(const wchar_t* text, size_t characterCount,
                              std::string& bytes) noexcept;

void InitXUnicode();

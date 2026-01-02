// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

//*****************************************************************************
//
// 20.1.2003
// Note about optimizations by conversion to ASM: optimizations manifest mainly
// in comparison of identical strings, i.e., whether functions have the opportunity to search
// strings completely. Additionally, optimization is more noticeable on older processors, where
// ASM variants can run 8x faster (old Pentium).
//
// Modern processors (AMD Athlon, Pentium Pro) can execute optimized C++
// variant almost as fast as its ASM variant. However, because
// so far optimized C++ variants are not faster and ASM is much faster
// than debug C++ variant, we use ASM variants.
//
// Function StrNICmp in C++ on Pentium Pro runs faster than in ASM variant.
//

extern BYTE LowerCase[256]; // remapping of all characters to lowercase; generated using API CharLower
extern BYTE UpperCase[256]; // remapping of all characters to uppercase; generated using API CharUpper

//*****************************************************************************
//
// StrICpy
//
// Function copies characters from source to destination. Upper case letters are mapped to
// lower case using LowerCase array.
//
// Parameters
//   dest: pointer to the destination string
//   src: pointer to the null-terminated source string
//
// Return Values
//   The StrICpy returns the number of bytes stored in buffer, not counting
//   the terminating null character.
//
int StrICpy(char* dest, const char* src);

//*****************************************************************************
//
// StrICmp
//
// Function compares two strings. The comparsion is not case sensitive.
// For differences, upper case letters are mapped to lower case using LowerCase array.
// Thus, "abc_" < "ABCD" since '_' < 'd'.
//
// Parameters
//   s1, s2: null-terminated strings to compare
//
// Return Values
//   -1 if s1 < s2 (if string pointed to by s1 is less than the string pointed to by s2)
//    0 if s1 = s2 (if the strings are equal)
//   +1 if s1 > s2 (if string pointed to by s1 is greater than the string pointed to by s2)
//
int StrICmp(const char* s1, const char* s2);

//*****************************************************************************
//
// StrICmpEx
//
// Function compares two substrings. The comparsion is not case sensitive.
// For the purposes of the comparsion, all characters are converted to lower case
// using LowerCase array.
// If the two substrings are of different lengths, they are compared up to the
// length of the shortest one. If they are equal to that point, then the return
// value will indicate that the longer string is greater.
//
// Parameters
//   s1, s2: strings to compare
//   l1    : compared length of s1 (must be less or equal to strlen(s1))
//   l2    : compared length of s2 (must be less or equal to strlen(s2))
//
// Return Values
//   -1 if s1 < s2 (if substring pointed to by s1 is less than the substring pointed to by s2)
//    0 if s1 = s2 (if the substrings are equal)
//   +1 if s1 > s2 (if substring pointed to by s1 is greater than the substring pointed to by s2)
//
int StrICmpEx(const char* s1, int l1, const char* s2, int l2);

//*****************************************************************************
//
// StrCmpEx
//
// Function compares two substrings.
// If the two substrings are of different lengths, they are compared up to the
// length of the shortest one. If they are equal to that point, then the return
// value will indicate that the longer string is greater.
//
// Parameters
//   s1, s2: strings to compare
//   l1    : compared length of s1 (must be less or equal to strlen(s1))
//   l2    : compared length of s2 (must be less or equal to strlen(s1))
//
// Return Values
//   -1 if s1 < s2 (if substring pointed to by s1 is less than the substring pointed to by s2)
//    0 if s1 = s2 (if the substrings are equal)
//   +1 if s1 > s2 (if substring pointed to by s1 is greater than the substring pointed to by s2)
//
int StrCmpEx(const char* s1, int l1, const char* s2, int l2);

//*****************************************************************************
//
// StrNICmp
//
// Function compares two strings. The comparsion is not case sensitive.
// For the purposes of the comparsion, all characters are converted to lower case
// using LowerCase array. The comparsion stops after: (1) a difference between the
// strings is found, (2) the end of the strings is reached, or (3) n characters
// have been compared.
//
// Parameters
//   s1, s2: strings to compare
//   n:      maximum length to compare
//
// Return Values
//   -1 if s1 < s2 (if substring pointed to by s1 is less than the substring pointed to by s2)
//    0 if s1 = s2 (if the substrings are equal)
//   +1 if s1 > s2 (if substring pointed to by s1 is greater than the substring pointed to by s2)
//
int StrNICmp(const char* s1, const char* s2, int n);

//*****************************************************************************
//
// MemICmp
//
// Compares n bytes of the two blocks of memory stored at buf1
// and buf2. The characters are converted to lowercase before
// comparing (not permanently; using LowerCase array), so case
// is ignored in the search.
//
// Parameters
//   buf1, buf2: memory buffers to compare
//   n:          maximum length to compare
//
// Return Values
//   -1 if buf1 < buf2 (if buffer pointed to by buf1 is less than the buffer pointed to by buf2)
//    0 if buf1 = buf2 (if the buffers are equal)
//   +1 if buf1 > buf2 (if buffer pointed to by buf1 is greater than the buffer pointed to by buf2)
//
int MemICmp(const void* buf1, const void* buf2, int n);

// faster strlen, processes four characters at a time
// str is accessed four bytes at a time -> requires larger buffer
// int StrLen(const char *str);    // only 2x faster, unnecessary risk of accessing unaligned memory

// copies text into newly allocated space, NULL = insufficient memory
char* DupStr(const char* txt);

// copies text into newly allocated space, NULL = insufficient memory,
// additionally on insufficient memory sets 'err' to TRUE
char* DupStrEx(const char* str, BOOL& err);

// returns first occurrence of 'pattern' in 'txt' or NULL, is case-insensitive
const char* StrIStr(const char* txt, const char* pattern);

// returns first occurrence of 'pattern' in 'txt' or NULL, is case-insensitive
const char* StrIStr(const char* txtStart, const char* txtEnd,
                    const char* patternStart, const char* patternEnd);

// appends string 'src' after string 'dest', but does not exceed length 'dstSize'
// terminates string with zero, which is included in length 'dstSize'
// returns 'dst'
char* StrNCat(char* dst, const char* src, int dstSize);

// this historical code is not used by anyone
/*
#define CONVERT_TAB_CHARS     44
#define CONVERT_TAB_MAX_CHARS 256

class CConvertTab
{
  public:
    BYTE Data[CONVERT_TAB_MAX_CHARS];

  public:
    CConvertTab();
    void Convert(char *str);
};

extern CConvertTab ConvertTab;
*/

//*****************************************************************************
//
// SWPrintFToEnd_s
//
// the only difference from swprintf_s is that it writes after the text placed in the buffer

template <size_t _Size>
inline int SWPrintFToEnd_s(WCHAR (&_Dst)[_Size], const WCHAR* _Format, ...)
{
    va_list _ArgList;
    va_start(_ArgList, _Format);
    int len = wcslen(_Dst);
    return vswprintf_s(_Dst + len, _Size - len, _Format, _ArgList);
}

inline int SWPrintFToEnd_s(WCHAR* _Dst, size_t _SizeInWords, const WCHAR* _Format, ...)
{
    va_list _ArgList;
    va_start(_ArgList, _Format);
    int len = (int)wcslen(_Dst);
    return vswprintf_s(_Dst + len, _SizeInWords - len, _Format, _ArgList);
}

//*****************************************************************************
//
// SPrintFToEnd_s
//
// the only difference from swprintf_s is that it writes after the text placed in the buffer

template <size_t _Size>
inline int SPrintFToEnd_s(char (&_Dst)[_Size], const char* _Format, ...)
{
    va_list _ArgList;
    va_start(_ArgList, _Format);
    int len = strlen(_Dst);
    return vsprintf_s(_Dst + len, _Size - len, _Format, _ArgList);
}

inline int SPrintFToEnd_s(char* _Dst, size_t _Size, const char* _Format, ...)
{
    va_list _ArgList;
    va_start(_ArgList, _Format);
    int len = (int)strlen(_Dst);
    return vsprintf_s(_Dst + len, _Size - len, _Format, _ArgList);
}

#ifdef UNICODE
#define STPrintFToEnd_s SWPrintFToEnd_s
#else // UNICODE
#define STPrintFToEnd_s SPrintFToEnd_s
#endif // UNICODE



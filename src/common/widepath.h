// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Wide Path Support for Long Paths (>MAX_PATH)
//
// These utilities convert ANSI paths to wide strings and add the \\?\ prefix
// when needed to support paths longer than MAX_PATH (260 characters).
//
// NOTE: This is Phase 1 - long path support only. It does NOT fix Unicode
// filenames that are outside the current Windows codepage. That requires
// a larger architectural change (Phase 2).
//
//****************************************************************************

#pragma once

#include <windows.h>
#include <stdlib.h>
#include <string.h>

// Threshold for adding \\?\ prefix (leave some margin below MAX_PATH)
#define SAL_LONG_PATH_THRESHOLD 240

// Maximum path length with \\?\ prefix (Windows limit)
#define SAL_MAX_LONG_PATH 32767

// Default heap capacity for CPathBuffer instances.
// CPathBuffer starts at 4KB and can grow up to SAL_MAX_LONG_PATH on demand.
#define SAL_PATH_BUFFER_INITIAL_CAPACITY 4096

// Inline fallback capacity for CPathBuffer (used before heap allocation succeeds).
#define SAL_PATH_BUFFER_INLINE_CAPACITY MAX_PATH

// Initial capacity for CWidePathBuffer (kept at MAX_PATH).
#define SAL_WIDE_PATH_BUFFER_INITIAL_CAPACITY MAX_PATH

//
// CPathBuffer
//
// RAII path buffer.
// Starts with a 4KB heap buffer and grows on demand up to SAL_MAX_LONG_PATH.
// Keeps a MAX_PATH inline fallback only for low-memory allocation failure.
// Use this instead of char[MAX_PATH] for paths that may exceed 260 characters.
//
// Usage:
//   CPathBuffer path;
//   strcpy(path.Get(), "C:\\some\\path");
//   SalPathAppend(path.Get(), fileName, path.Size());
//
// Or with initial value:
//   CPathBuffer path(existingPath);
//   SalPathAppend(path.Get(), fileName, path.Size());
//
class CPathBuffer
{
public:
    // Constructs empty buffer (pre-allocates default 4KB heap capacity)
    CPathBuffer() : m_buffer(m_inline), m_capacity(SAL_PATH_BUFFER_INLINE_CAPACITY)
    {
        m_inline[0] = '\0';
        EnsureCapacity(SAL_PATH_BUFFER_INITIAL_CAPACITY);
    }

    // Constructs buffer initialized with a path
    explicit CPathBuffer(const char* initialPath) : m_buffer(m_inline), m_capacity(SAL_PATH_BUFFER_INLINE_CAPACITY)
    {
        m_inline[0] = '\0';
        EnsureCapacity(SAL_PATH_BUFFER_INITIAL_CAPACITY);
        Assign(initialPath);
    }

    // Destructor frees allocated memory
    ~CPathBuffer()
    {
        if (m_buffer != NULL && m_buffer != m_inline)
            free(m_buffer);
        m_buffer = NULL;
        m_capacity = 0;
    }

    // Move support (copy remains disabled)
    CPathBuffer(CPathBuffer&& other) : m_buffer(m_inline), m_capacity(SAL_PATH_BUFFER_INLINE_CAPACITY)
    {
        m_inline[0] = '\0';
        MoveFrom(other);
    }
    CPathBuffer& operator=(CPathBuffer&& other)
    {
        if (this != &other)
        {
            if (m_buffer != NULL && m_buffer != m_inline)
                free(m_buffer);
            m_buffer = m_inline;
            m_capacity = SAL_PATH_BUFFER_INLINE_CAPACITY;
            m_inline[0] = '\0';
            MoveFrom(other);
        }
        return *this;
    }

    // Returns pointer to the buffer
    char* Get() { return m_buffer; }
    const char* Get() const { return m_buffer; }
    char* Data() { return m_buffer; }
    const char* CStr() const { return m_buffer; }

    // Returns current buffer capacity in characters including null terminator.
    // NOTE: legacy call sites may expect this as "buffer size".
    int Size() const { return m_capacity; }
    int Capacity() const { return m_capacity; }
    int MaxCapacity() const { return SAL_MAX_LONG_PATH; }
    int Length() const { return (m_buffer != NULL) ? (int)strlen(m_buffer) : 0; }

    // Ensures the buffer can hold at least requiredChars characters (including '\0').
    BOOL EnsureCapacity(int requiredChars)
    {
        if (requiredChars <= 0)
            requiredChars = 1;
        if (requiredChars <= m_capacity)
            return TRUE;
        if (requiredChars > SAL_MAX_LONG_PATH)
            return FALSE;

        int newCapacity = m_capacity;
        if (newCapacity < SAL_PATH_BUFFER_INITIAL_CAPACITY)
            newCapacity = SAL_PATH_BUFFER_INITIAL_CAPACITY;
        while (newCapacity < requiredChars)
        {
            if (newCapacity >= SAL_MAX_LONG_PATH / 2)
            {
                newCapacity = SAL_MAX_LONG_PATH;
                break;
            }
            newCapacity *= 2;
        }
        if (newCapacity < requiredChars)
            newCapacity = requiredChars;
        if (newCapacity > SAL_MAX_LONG_PATH)
            newCapacity = SAL_MAX_LONG_PATH;
        if (newCapacity < requiredChars)
            return FALSE;

        char* newBuffer = (char*)malloc((size_t)newCapacity);
        if (newBuffer == NULL)
            return FALSE;

        lstrcpynA(newBuffer, m_buffer, newCapacity);
        if (m_buffer != m_inline)
            free(m_buffer);
        m_buffer = newBuffer;
        m_capacity = newCapacity;
        return TRUE;
    }

    void Clear()
    {
        if (m_buffer != NULL)
            m_buffer[0] = '\0';
    }

    BOOL Assign(const char* text)
    {
        if (text == NULL)
        {
            Clear();
            return TRUE;
        }
        size_t len = strlen(text);
        if (!EnsureCapacity((int)len + 1))
            return FALSE;
        memcpy(m_buffer, text, len + 1);
        return TRUE;
    }

    BOOL Append(const char* text)
    {
        if (text == NULL)
            return TRUE;
        size_t curLen = strlen(m_buffer);
        size_t addLen = strlen(text);
        if (!EnsureCapacity((int)(curLen + addLen + 1)))
            return FALSE;
        memcpy(m_buffer + curLen, text, addLen + 1);
        return TRUE;
    }

    // Implicit conversion for convenience
    // NOTE: kept for compatibility during migration. Prefer Data()/CStr().
    operator char*() { return m_buffer; }
    operator const char*() const { return m_buffer; }

    // Returns TRUE if allocation succeeded
    BOOL IsValid() const { return m_buffer != NULL; }

private:
    char* m_buffer;
    int m_capacity;
    char m_inline[SAL_PATH_BUFFER_INLINE_CAPACITY];

    // Disable copy
    CPathBuffer(const CPathBuffer&);
    CPathBuffer& operator=(const CPathBuffer&);

    void MoveFrom(CPathBuffer& other)
    {
        if (other.m_buffer == other.m_inline)
        {
            if (!EnsureCapacity(other.m_capacity))
                return;
            lstrcpynA(m_buffer, other.m_inline, m_capacity);
        }
        else
        {
            m_buffer = other.m_buffer;
            m_capacity = other.m_capacity;
        }

        other.m_buffer = other.m_inline;
        other.m_capacity = SAL_PATH_BUFFER_INLINE_CAPACITY;
        other.m_inline[0] = '\0';
    }
};

//
// CWidePathBuffer
//
// RAII wide path buffer with inline storage and grow-on-demand behavior.
// Starts at MAX_PATH capacity and grows up to SAL_MAX_LONG_PATH as needed.
// Use this instead of wchar_t[MAX_PATH] for paths that may exceed 260 characters.
//
// Usage:
//   CWidePathBuffer path;
//   wcscpy(path.Get(), L"C:\some\path");
//
class CWidePathBuffer
{
public:
    // Constructs empty buffer (inline storage, grows on demand)
    CWidePathBuffer();

    // Constructs buffer initialized with a path
    explicit CWidePathBuffer(const wchar_t* initialPath);

    // Destructor frees allocated memory
    ~CWidePathBuffer();

    // Returns pointer to the buffer
    wchar_t* Get() { return m_buffer; }
    const wchar_t* Get() const { return m_buffer; }
    wchar_t* Data() { return m_buffer; }
    const wchar_t* CStr() const { return m_buffer; }

    // Returns current buffer capacity in characters including null terminator.
    int Size() const { return m_capacity; }
    int Capacity() const { return m_capacity; }
    int MaxCapacity() const { return SAL_MAX_LONG_PATH; }
    int Length() const { return (m_buffer != NULL) ? (int)wcslen(m_buffer) : 0; }

    // Implicit conversion for convenience
    // NOTE: kept for compatibility during migration. Prefer Data()/CStr().
    operator wchar_t*() { return m_buffer; }
    operator const wchar_t*() const { return m_buffer; }

    // Returns TRUE if allocation succeeded
    BOOL IsValid() const { return m_buffer != NULL; }

    // Ensures the buffer can hold at least requiredChars characters (including '\0').
    BOOL EnsureCapacity(int requiredChars);
    void Clear();
    BOOL Assign(const wchar_t* text);

    // Appends a path component (adds backslash if needed)
    // Returns TRUE on success, FALSE if buffer full or invalid
    BOOL Append(const wchar_t* name);

    // Appends an ANSI path component (converts to wide, adds backslash if needed)
    // Returns TRUE on success, FALSE if buffer full or invalid
    BOOL Append(const char* name);

private:
    wchar_t* m_buffer;
    int m_capacity;
    wchar_t m_inline[SAL_WIDE_PATH_BUFFER_INITIAL_CAPACITY];

    // Disable copy
    CWidePathBuffer(const CWidePathBuffer&);
    CWidePathBuffer& operator=(const CWidePathBuffer&);
};

//
// SalWidePath
//
// RAII wrapper for wide path conversion. Converts ANSI path to wide string
// and adds \\?\ prefix if path exceeds SAL_LONG_PATH_THRESHOLD.
//
// Usage:
//   SalWidePath widePath(ansiPath);
//   if (widePath.IsValid())
//       CreateFileW(widePath.Get(), ...);
//
// The prefix is added as follows:
//   - Local paths: C:\foo\bar  ->  \\?\C:\foo\bar
//   - UNC paths:   \\server\share  ->  \\?\UNC\server\share
//
class SalWidePath
{
public:
    // Constructs wide path from ANSI path
    // If path is NULL, IsValid() returns FALSE
    explicit SalWidePath(const char* ansiPath);

    // Destructor frees allocated memory
    ~SalWidePath();

    // Returns TRUE if conversion succeeded
    BOOL IsValid() const { return m_widePath != NULL; }

    // Returns the wide path string (or NULL if invalid)
    const wchar_t* Get() const { return m_widePath; }

    // Implicit conversion for convenience
    operator const wchar_t*() const { return m_widePath; }

    // Returns TRUE if \\?\ prefix was added
    BOOL HasLongPathPrefix() const { return m_hasPrefix; }

private:
    wchar_t* m_widePath;
    BOOL m_hasPrefix;

    // Disable copy
    SalWidePath(const SalWidePath&);
    SalWidePath& operator=(const SalWidePath&);
};

//
// SalAnsiName
//
// RAII wrapper for wide-to-ANSI filename conversion with lossy detection.
// Used for converting WIN32_FIND_DATAW filenames to ANSI while tracking
// whether any Unicode characters were lost in the conversion.
//
// Usage:
//   SalAnsiName ansiName(findDataW.cFileName);
//   if (ansiName.IsLossy())
//       // Original wide name needed for proper display/operations
//       file.NameW = ansiName.AllocWideName();  // caller owns the memory
//   file.Name = ansiName.AllocAnsiName();  // caller owns the memory
//
class SalAnsiName
{
public:
    // Constructs from wide filename, converts to ANSI and detects lossy conversion
    explicit SalAnsiName(const wchar_t* wideName);

    // Destructor frees internal buffers
    ~SalAnsiName();

    // Returns TRUE if conversion lost characters (i.e., wide name is needed)
    BOOL IsLossy() const { return m_isLossy; }

    // Returns the ANSI name (internal buffer, valid until object destroyed)
    const char* GetAnsi() const { return m_ansiName; }

    // Returns the wide name (internal buffer, valid until object destroyed)
    const wchar_t* GetWide() const { return m_wideName; }

    // Returns length of ANSI name
    int GetAnsiLen() const { return m_ansiLen; }

    // Returns length of wide name
    int GetWideLen() const { return m_wideLen; }

    // Allocates and returns a copy of the ANSI name (caller must free)
    char* AllocAnsiName() const;

    // Allocates and returns a copy of the wide name (caller must free)
    // Only call if IsLossy() returns TRUE
    wchar_t* AllocWideName() const;

private:
    char* m_ansiName;
    wchar_t* m_wideName;
    int m_ansiLen;
    int m_wideLen;
    BOOL m_isLossy;

    // Disable copy
    SalAnsiName(const SalAnsiName&);
    SalAnsiName& operator=(const SalAnsiName&);
};

//
// Helper functions for manual memory management (if RAII not suitable)
//

// Converts ANSI path to wide path with optional \\?\ prefix
// Returns allocated wchar_t* that must be freed with SalFreeWidePath()
// Returns NULL on failure (sets LastError)
wchar_t* SalAllocWidePath(const char* ansiPath);

// Frees memory allocated by SalAllocWidePath
void SalFreeWidePath(wchar_t* widePath);

//
// Convenience wrappers for common file operations
// These handle the conversion internally
//

// CreateFile wrapper that supports long paths
HANDLE SalLPCreateFile(
    const char* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile);

// GetFileAttributes wrapper that supports long paths
DWORD SalLPGetFileAttributes(const char* fileName);

// SetFileAttributes wrapper that supports long paths
BOOL SalLPSetFileAttributes(const char* fileName, DWORD dwFileAttributes);

// DeleteFile wrapper that supports long paths
BOOL SalLPDeleteFile(const char* fileName);

// RemoveDirectory wrapper that supports long paths
BOOL SalLPRemoveDirectory(const char* dirName);

// CreateDirectory wrapper that supports long paths
BOOL SalLPCreateDirectory(const char* pathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);

// MoveFile wrapper that supports long paths
BOOL SalLPMoveFile(const char* existingFileName, const char* newFileName);

// CopyFile wrapper that supports long paths
BOOL SalLPCopyFile(const char* existingFileName, const char* newFileName, BOOL failIfExists);

// FindFirstFile wrapper that supports long paths
// Note: Returns wide find data; caller must convert if needed
HANDLE SalLPFindFirstFile(const char* fileName, WIN32_FIND_DATAW* findData);

// FindNextFile wrapper that supports long paths
// Note: Returns wide find data; caller must convert if needed
BOOL SalLPFindNextFile(HANDLE hFindFile, WIN32_FIND_DATAW* findData);

// FindFirstFile wrapper that supports long paths with ANSI find data
// Converts result back to ANSI WIN32_FIND_DATA for compatibility
HANDLE SalLPFindFirstFileA(const char* fileName, WIN32_FIND_DATAA* findData);

// FindNextFile wrapper for use with handles from SalLPFindFirstFileA
// Converts result back to ANSI WIN32_FIND_DATA for compatibility
BOOL SalLPFindNextFileA(HANDLE hFindFile, WIN32_FIND_DATAA* findData);

//
// Handle-tracking variants for integration with Salamander's HANDLES system
// These should be used when HANDLES_ENABLE is defined (debug builds)
//
// Usage:
//   HANDLE h = SalCreateFileH(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
//   // Handle is automatically tracked via HANDLES_ADD_EX
//

#ifdef HANDLES_ENABLE

// CreateFile with handle tracking - use instead of HANDLES_Q(CreateFile(...))
#define SalCreateFileH(fileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile) \
    SalLPCreateFileTracked(fileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile, __FILE__, __LINE__)

HANDLE SalLPCreateFileTracked(
    const char* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile,
    const char* srcFile,
    int srcLine);

// FindFirstFile with handle tracking - use instead of HANDLES_Q(FindFirstFile(...))
#define SalFindFirstFileH(fileName, findData) \
    SalLPFindFirstFileTracked(fileName, findData, __FILE__, __LINE__)

HANDLE SalLPFindFirstFileTracked(
    const char* fileName,
    WIN32_FIND_DATAA* findData,
    const char* srcFile,
    int srcLine);

// FindFirstFile (wide data) with handle tracking - use instead of HANDLES_Q(FindFirstFileW(...))
#define SalFindFirstFileHW(fileName, findData)     SalLPFindFirstFileTrackedW(fileName, findData, __FILE__, __LINE__)

HANDLE SalLPFindFirstFileTrackedW(
    const char* fileName,
    WIN32_FIND_DATAW* findData,
    const char* srcFile,
    int srcLine);

#else // !HANDLES_ENABLE

// In release builds, just use the regular wrapper
#define SalCreateFileH SalLPCreateFile
#define SalFindFirstFileH SalLPFindFirstFileA
#define SalFindFirstFileHW SalLPFindFirstFile

#endif // HANDLES_ENABLE

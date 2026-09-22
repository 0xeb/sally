// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

typedef struct chmFile* (*CHM_OPEN_PROC)(const wchar_t* filename);
typedef void (*CHM_CLOSE_PROC)(struct chmFile* h);
typedef int (*CHM_ITERATE_PROC)(struct chmFile* h, int pg, int what, struct chmUnitInfo* unitInfo);
typedef int (*CHM_ENUMERATE_PROC)(struct chmFile* h, int what, CHM_ENUMERATOR e, void* context);
typedef LONGINT64 (*CHM_RETRIEVE_OBJECT_PROC)(struct chmFile* h, struct chmUnitInfo* ui, unsigned char* buf, LONGUINT64 addr, LONGINT64 len);

//
#define UNPACK_ERROR 0
#define UNPACK_OK 1
#define UNPACK_CANCEL 2

//
//
//
class CCHMFile
{
public:
    CCHMFile();
    virtual ~CCHMFile();

    BOOL Open(const wchar_t* fileName, BOOL quiet = FALSE);
    BOOL Close();

    BOOL EnumObjects(CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData);

    BOOL ExtractObject(CSalamanderForOperationsAbstract* salamander, const wchar_t* srcPath, const wchar_t* path,
                       const CFileData* fileData, DWORD& silent, BOOL& toSkip);

    int ExtractAllObjects(CSalamanderForOperationsAbstract* salamander, std::wstring& srcPath,
                          CSalamanderDirectoryAbstract const* dir, const wchar_t* mask,
                          std::wstring& path, DWORD& silent, BOOL& toSkip);
    BOOL UnpackDir(const wchar_t* dirName, const CFileData* fileData);

public:
    DWORD ButtonFlags;

protected:
    chmFile* CHM;
    std::wstring FileName;
    FILETIME FileTime;

    CHM_OPEN_PROC ChmOpen;
    CHM_CLOSE_PROC ChmClose;
    CHM_ENUMERATE_PROC ChmEnumerate;
    CHM_RETRIEVE_OBJECT_PROC ChmRetrieveObject;

    BOOL AddFileDir(struct chmUnitInfo* ui, CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData);

    friend int ChmEnumObjectsCallBack(struct chmFile* CHM, struct chmUnitInfo* ui, void* context);
};

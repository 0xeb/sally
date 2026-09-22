// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <stdio.h>
#include "TreeMap.FileData.CZDirectory.h"
#include "TreeMap.FileData.CZRoot.h"

void CZDirectory::TEST_ENUM(FILE* fileHandle)
{
    wchar_t name[512];
    wchar_t str[512];
    size_t strSize;
    int cnt = this->_files->GetCount();
    for (int i = 0; i < cnt; i++)
    {

        CZFile* f = this->_files->At(i);

        wchar_t const* sn = f->GetName();
        wchar_t* dn = name;
        //wcscpy_s(name, sizeof(name)/sizeof(wchar_t), f->GetName());
        while (*sn != L'\0')
        {
            switch (*sn)
            {
            case '&':
                *dn++ = L'&';
                *dn++ = L'a';
                *dn++ = L'm';
                *dn++ = L'p';
                *dn++ = L';';
                break;
            case '"':
                *dn++ = L'&';
                *dn++ = L'q';
                *dn++ = L'u';
                *dn++ = L'o';
                *dn++ = L't';
                *dn++ = L';';
                break;
            default:
                *dn++ = *sn;
                break;
            }
            sn++;
        }
        *dn++ = L'\0';

        if (f->IsDirectory())
        {
            //strSize = _snwprintf_s(str, sizeof(str)/sizeof(wchar_t), L"<folder name=\"%s\" size=\"%I64d\">\n", name, f->GetSize());
            strSize = swprintf(str, 512, L"<folder name=\"%s\" datasize=\"%I64d\" realsize=\"%I64d\" disksize=\"%I64d\">\n", name, f->GetSizeEx(FILESIZE_DATA), f->GetSizeEx(FILESIZE_REAL), f->GetSizeEx(FILESIZE_DISK));
            if (fwrite(str, sizeof(wchar_t), strSize, fileHandle) != strSize)
            {
                wprintf(L"fwrite failed!\n");
            }

            ((CZDirectory*)f)->TEST_ENUM(fileHandle);
            //strSize = _snwprintf_s(str, sizeof(str)/sizeof(wchar_t), L"</folder>\n");
            strSize = swprintf(str, 512, L"</folder>\n");
            if (fwrite(str, sizeof(wchar_t), strSize, fileHandle) != strSize)
            {
                wprintf(L"fwrite failed!\n");
            }
        }
        else
        {
            //strSize = _snwprintf_s(str, sizeof(str)/sizeof(wchar_t), L"<file name=\"%s\" size=\"%I64d\" />\n", name, f->GetSize());
            strSize = swprintf(str, 512, L"<file name=\"%s\" datasize=\"%I64d\" realsize=\"%I64d\" disksize=\"%I64d\" />\n", name, f->GetSizeEx(FILESIZE_DATA), f->GetSizeEx(FILESIZE_REAL), f->GetSizeEx(FILESIZE_DISK));
            if (fwrite(str, sizeof(wchar_t), strSize, fileHandle) != strSize)
            {
                wprintf(L"fwrite failed!\n");
            }
        }
    }
}

void CZDirectory::TEST()
{
    wchar_t str[512];
    size_t strSize;
    FILE* fileHandle;

    // Create an the xml file in text and Unicode encoding mode.
    if ((fileHandle = _wfopen(L"_test.xml", L"wt+,ccs=UTF-8")) == NULL) // C4996
                                                                        // Note: _wfopen is deprecated; consider using _wfopen_s instead
    {
        wprintf(L"fopen failed!\n");
        return;
    }

    // Write a string into the file.
    //wcscpy_s(str, sizeof(str)/sizeof(wchar_t), L"<root>\n");
    //strSize = _snwprintf_s(str, sizeof(str)/sizeof(wchar_t), L"<root path=\"%s\">\n", this->_name);
    strSize = swprintf(str, 512, L"<root path=\"%s\">\n", this->_name);
    //strSize = wcslen(str);
    if (fwrite(str, sizeof(wchar_t), strSize, fileHandle) != strSize)
    {
        wprintf(L"fwrite failed!\n");
    }

    TEST_ENUM(fileHandle);

    // Write a string into the file.
    //wcscpy_s(str, sizeof(str)/sizeof(wchar_t), L"</root>");
    wcscpy(str, L"</root>");
    strSize = wcslen(str);
    if (fwrite(str, sizeof(wchar_t), strSize, fileHandle) != strSize)
    {
        wprintf(L"fwrite failed!\n");
    }

    // Close the file.
    if (fclose(fileHandle))
    {
        wprintf(L"fclose failed!\n");
    }
}

INT64 CZDirectory::PopulateDir(CWorkerThread* mythread, std::wstring path)
{
    if (this->_root == NULL)
        Beep(1000, 100);

    WIN32_FIND_DATAW FindFileData;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    DWORD dwError;

    int dircount = 0;
    int filecount = 0;
    INT64 tsize = 0;

    int sortorder = this->_root->GetSortOrder();

    if (!path.empty() && path.back() != L'\\')
        path.push_back(L'\\');
    path.append(this->_name, this->_namelen);
    if (path.empty() || path.back() != L'\\')
        path.push_back(L'\\');

    const std::wstring searchPath = path + L'*';
    /*
	int radixHistorgram[2048 * 6];
	radixHistorgram[0] = 0;
	for (int i = 1; i < 2048 * 6; i++)
	{
		radixHistorgram[i] = radixHistorgram[i - 1] + 1;
	}
	path[MAX_PATH] = (char)radixHistorgram[2048];
*/
    hFind = FindFirstFileW(searchPath.c_str(), &FindFileData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        //ERROR
        this->_root->LogLastError(this);
        return -1;
    }
    else
    {
        DWORD lastTime = GetTickCount();
        do
        {
            if (FindFileData.cFileName[0] == '.' && (FindFileData.cFileName[1] == '\0' || (FindFileData.cFileName[1] == '.' && FindFileData.cFileName[2] == '\0')))
                continue;
            INT64 datasize = 0;
            INT64 realsize = 0;
            INT64 disksize = 0;

            CZFile* f = NULL;
            if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                {
                    f = new CZDirectory(this, FindFileData.cFileName, &FindFileData.ftCreationTime, &FindFileData.ftLastWriteTime);
                    datasize = ((CZDirectory*)f)->PopulateDir(mythread, path);
                    if (datasize < 0) //error!
                    {
                        //this->_root->Log(LOG_ERROR, TEXT("Negative size of directory contents."), f);
                        delete f;
                        f = NULL;
                        datasize = 0;
                        realsize = 0;
                        disksize = 0;
                    }
                    else if (datasize == 0) //everything ok, but empty
                    {
                        this->_dircount++;
                        dircount++;
                        this->_dircount += ((CZDirectory*)f)->GetSubDirsCount(); //count empty ones so we match Explorer's results
                        this->_filecount += ((CZDirectory*)f)->GetSubFileCount();

                        delete f;
                        f = NULL;
                        datasize = 0;
                        realsize = 0;
                        disksize = 0;
                    }
                    else
                    {
                        this->_dircount++;
                        dircount++;
                        datasize = f->GetSizeEx(FILESIZE_DATA);
                        realsize = f->GetSizeEx(FILESIZE_REAL);
                        disksize = f->GetSizeEx(FILESIZE_DISK);
                        this->_dircount += ((CZDirectory*)f)->GetSubDirsCount();
                        this->_filecount += ((CZDirectory*)f)->GetSubFileCount();
                    }
                }
                else
                {
                    f = new CZDirectory(this, FindFileData.cFileName, &FindFileData.ftCreationTime, &FindFileData.ftLastWriteTime);
                    this->_root->Log(LOG_WARNING, L"Ignoring Reparse Point.", f);
                    delete f;
                    f = NULL;
                }
            }
            else
            {
                datasize = ((INT64)FindFileData.nFileSizeHigh * ((INT64)(MAXDWORD) + 1)) + FindFileData.nFileSizeLow;
                if ((FindFileData.dwFileAttributes & (FILE_ATTRIBUTE_SPARSE_FILE | FILE_ATTRIBUTE_COMPRESSED)) != 0)
                {
                    DWORD lo, hi;
                    const std::wstring filePath = path + FindFileData.cFileName;
                    lo = GetCompressedFileSizeW(filePath.c_str(), &hi);
                    if (lo == INVALID_FILE_SIZE)
                    {
                        //ErrorExit(FindFileData.cFileName);
                        realsize = datasize;
                    }
                    else
                    {
                        realsize = ((INT64)hi * ((INT64)(MAXDWORD) + 1)) + lo;
                    }
                }
                else
                {
                    realsize = datasize;
                }
                //always count them so we match Explorer's numbers
                this->_filecount++;
                filecount++;
                if (datasize > 0)
                {
                    disksize = this->_root->GetDiskSize(realsize);

                    f = new CZFile(this, FindFileData.cFileName, datasize, realsize, disksize, &FindFileData.ftCreationTime, &FindFileData.ftLastWriteTime);
                }
            }
            if (datasize > 0)
            {
                INT64 sortsize = f->GetSizeEx(sortorder);

                int fre = this->_files->Add(f);
                if (fre < 0)
                {
                    delete f;
                    f = NULL;
                }
                else
                {
                    this->_datasize += datasize;
                    this->_realsize += realsize;
                    this->_disksize += disksize;
                    if (!f->IsDirectory())
                        tsize += f->GetSizeEx(sortorder);

                    while (fre > 0)
                    {
                        int parent = (fre - 1) / 2;
                        if (this->_files->At(parent)->GetSizeEx(sortorder) > sortsize) //if the parent is larger, it violates the MIN-HEAP
                        {
                            this->_files->Copy(fre, parent);
                            fre = parent;
                        }
                        else
                        {
                            break;
                        }
                    }
                    this->_files->At(fre) = f;
                }
            }
            //if (((filecount + dircount) > MAXREPORTEDFILES) || (GetTickCount() - lastTime > 500)) //either many files or 0.5 sec elapsed
            if ((GetTickCount() - lastTime > 250) && (filecount + dircount) > 0) //if 0.25 sec elapsed and at least something new was found
            {
                this->_root->IncStats(filecount, dircount, tsize);
                lastTime = GetTickCount();
                dircount = 0;
                filecount = 0;
                tsize = 0;
            }
        } while ((FindNextFileW(hFind, &FindFileData) != 0) && (mythread == NULL || !mythread->Aborting()));

        this->_root->IncStats(filecount, dircount, tsize);

        dwError = GetLastError();
        FindClose(hFind);

        int cnt = this->_files->GetCount();
        for (int i = 1; i <= cnt; i++)
        {
            int end = cnt - i;
            CZFile* f = this->_files->At(end);
            this->_files->Copy(end, 0);
            int fre = 0;
            end--;
            while (fre * 2 + 1 <= end)
            {
                int child = fre * 2 + 1; //left
                if ((child < end) && (this->_files->At(child)->GetSizeEx(sortorder) > this->_files->At(child + 1)->GetSizeEx(sortorder)))
                    child++;
                if (this->_files->At(child)->GetSizeEx(sortorder) < f->GetSizeEx(sortorder)) //if the child is smaller, it violates the MIN-HEAP
                {
                    this->_files->Copy(fre, child);
                    fre = child;
                }
                else
                {
                    break;
                }
            }
            this->_files->At(fre) = f;
        }

        if (dwError != ERROR_NO_MORE_FILES)
        {
            //ERROR
            this->_root->LogError(this, dwError);
            return -1;
        }
    }

    return this->_realsize;
}

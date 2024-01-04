#include "stdafx.hxx"
//#include "vs_idl.hxx"
#include "vss.h"
#include "vswriter.h"
#include "vsbackup.h"
#include "compont.h"
#include <debug.h>
#include <cwriter.h>
#include <lmshare.h>
#include <lmaccess.h>
#include <time.h>
#include <vs_inc.hxx>

extern WCHAR g_wszSavedFilesDirectory[];
extern bool g_bAddFullUNCPath;
extern CComPtr<CWritersSelection>  g_pWriterSelection;
extern bool g_bAuthRestore;
extern bool g_bVerbose;

const WCHAR g_wszPartialFilePath[] = L"PartialFilesBackup";
const WCHAR g_wszDifferencedFilePath[] = L"DifferencedFilesBackup";

static const COPYBUFSIZE = 1024 * 1024;

static GUID ADAM_WRITER_GUID =
{   
    0xdd846aaa, 0xa1b6, 0x42a8, 0xaa, 0xf8, 0x03, 0xdc, 0xb6, 0x11, 0x4b, 0xfd
};

bool FindComponent
    (
    IVssExamineWriterMetadata *pMetadata,
    LPCWSTR wszLogicalPath,
    LPCWSTR wszComponentName,
    IVssWMComponent **ppComponent
    );

BOOL IsUNCPrefixLen(
    IN      LPCWSTR      wszUNCPath,
    OUT     DWORD       &dwPrefixLen
    );

bool FindComponentInDoc
    (
    IVssBackupComponents *pvbc,
    VSS_ID idWriter,
    LPCWSTR wszLogicalPath,
    LPCWSTR wszComponentName,
    IVssComponent **ppComponent,
    VSS_ID *pidInstance
    );

void SavePartialFile
    (
    LPCWSTR wszSourcePath,
    LPCWSTR wszSavePath,
    LPCWSTR wszFilename,
    LPCWSTR wszRanges
    );

bool needsBackingUp(DWORD dwMask);
bool needsSnapshot(DWORD dwMask);

typedef struct _FILE_DESCRIPTION
    {
    CComBSTR bstrPath;
    CComBSTR bstrFilespec;
    bool bRecursive;

    _FILE_DESCRIPTION(LPCWSTR wszPath, LPCWSTR wszFilespec, bool bRec) : 
                                                                                        bstrPath(wszPath),
                                                                                        bstrFilespec(wszFilespec),
                                                                                        bRecursive(bRec)
        {
        HRESULT hr = S_OK;
        
        assert(bstrPath.Length() > 0);
        assert(bstrFilespec.Length() > 0);
        CHECK_SUCCESS(bstrPath.ToUpper());
        CHECK_SUCCESS(bstrFilespec.ToUpper());

        if (bstrPath[bstrPath.Length() - 1] != '\\')
            bstrPath += L"\\";
        }

    bool operator==(const _FILE_DESCRIPTION& other) const
        { 
        // the filespec must match first of all
        if (!WildcardMatches(bstrFilespec, other.bstrFilespec))
            return false;

        // check the path
        if (bRecursive)   
            {
            if (!other.bRecursive)
                return wcsstr(other.bstrPath, bstrPath) == other.bstrPath;
            else 
                return (wcsstr(other.bstrPath, bstrPath) == (BSTR)other.bstrPath) || (wcsstr(bstrPath, other.bstrPath) == (BSTR)bstrPath);
            }   
        else        
            {
            if (!other.bRecursive)
                return bstrPath == other.bstrPath;
            else
                return wcsstr(bstrPath, other.bstrPath) == bstrPath;
            }   
        }

    bool WildcardMatches(LPCWSTR first, LPCWSTR second) const
        {
        assert (first && second);
        
        // if both string are empty, then they surely match
        if (wcslen(first) == 0 && wcslen(second) == 0)
            return true;

        // performance case:  the wildcards match exactly
        if (wcscmp(first, second) == 0)
            return true;
        
        // if we're done with the component, the wildcard better be terminated with '*' characters
        if (wcslen(first) == 0)
            return (second[0] == L'*') && WildcardMatches(first, second + 1);
        if (wcslen(second) == 0)
            return (first[0] == L'*') && WildcardMatches(first + 1, second);

        switch(first[0])   
            {
            case L'?':
                if (second[0] == L'*')  
                    {
                    return WildcardMatches(first + 1, second) ||   // '*' matches character
                               WildcardMatches(first, second + 1);      // '*' matches nothing
                    }

            // otherwise, the rest of the strings must match
            return WildcardMatches(first + 1, second + 1);
        case L'*':
            return WildcardMatches(first, second + 1)         || // '*' matches character
                       WildcardMatches(first + 1, second);            // '*' matches nothing
        default:
            switch(second[0])   
                {
                case L'?':
                    return WildcardMatches(first + 1, second + 1);
                case L'*':
                    return WildcardMatches(first + 1, second) || // '*' matches character
                               WildcardMatches(first, second + 1);    // '*' matches nothing
                default:
                    return (first[0] == second[0]) &&
                                WildcardMatches(first + 1, second + 1);
                }
            }
        }
    } FILE_DESCRIPTION;

typedef struct _FILE_RANGE
    {
    LARGE_INTEGER liStart;
    LARGE_INTEGER liExtent;

    _FILE_RANGE() 
        {
            liStart.QuadPart = 0;
            liExtent.QuadPart = 0;
        }  
    
    _FILE_RANGE(LARGE_INTEGER start, LARGE_INTEGER extent) 
        {
        liStart.QuadPart = start.QuadPart;
        liExtent.QuadPart = extent.QuadPart;
        }
    
    } FILE_RANGE;

typedef struct _SAVE_INFO
    {
    IVssBackupComponents *pvbc;
    IVssComponent *pComponent;
    IVssExamineWriterMetadata *pMetadata;
    CVssSimpleMap<VSS_PWSZ, VSS_PWSZ> mapSnapshots;
    CVssSimpleMap<FILE_DESCRIPTION, bool> excludeFiles;    
    } SAVE_INFO;

// stolen from mountmgr.h
bool isDASD(LPCWSTR path)
{
    return wcslen(path) == 28 &&
               path[0] == '\\' &&                                                
               (path[1] == '?' || path[1] == '\\') &&                     
               path[2] == '?' &&                                                 
               path[3] == '\\' &&                                                
               path[4] == 'V' &&                                                 
               path[5] == 'o' &&                                                 
               path[6] == 'l' &&                                                 
               path[7] == 'u' &&                                                 
               path[8] == 'm' &&                                                 
               path[9] == 'e' &&                                                 
               path[10] == '{' &&                                                
               path[19] == '-' &&                                                
               path[24] == '-' &&                                                
               path[29] == '-' &&                                                
               path[34] == '-' &&                                                
               path[47] == '}';
}


void StopService(LPCWSTR wszName)
{
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCM == NULL)
        Error(HRESULT_FROM_WIN32(GetLastError()), L"OpenSCManager");

    SC_HANDLE hService = OpenService(hSCM, wszName, SERVICE_ALL_ACCESS);
    if (hService == NULL)
        Error(HRESULT_FROM_WIN32(GetLastError()), L"OpenService");

    SERVICE_STATUS ss;
    if ( !QueryServiceStatus( hService, &ss ) )
       Error(HRESULT_FROM_WIN32(GetLastError()), L"QueryServiceStatus");

   if ( ss.dwCurrentState == SERVICE_STOPPED ) 
      return;

   while ( ss.dwCurrentState == SERVICE_STOP_PENDING ) 
   {
      Sleep( ss.dwWaitHint );
      if ( !QueryServiceStatus( hService, &ss ) )
         Error(HRESULT_FROM_WIN32(GetLastError()), L"QueryServiceStatus");

      if ( ss.dwCurrentState == SERVICE_STOPPED )
         return;
   }

  if ( !ControlService( hService, SERVICE_CONTROL_STOP, &ss ) )
      Error(HRESULT_FROM_WIN32(GetLastError()), L"ControlService");    

   while ( ss.dwCurrentState == SERVICE_STOP_PENDING ) 
   {
      Sleep( ss.dwWaitHint );
      if ( !QueryServiceStatus( hService, &ss ) )
         Error(HRESULT_FROM_WIN32(GetLastError()), L"QueryServiceStatus");
   }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
}


void StartService(LPCWSTR wszName)
{
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCM == NULL)
        Error(HRESULT_FROM_WIN32(GetLastError()), L"OpenSCManager");

    SC_HANDLE hService = OpenService(hSCM, wszName, SERVICE_ALL_ACCESS);
    if (hService == NULL)
        Error(HRESULT_FROM_WIN32(GetLastError()), L"OpenService");

    if (!StartService(hService, 0, NULL))
        Error(HRESULT_FROM_WIN32(GetLastError()), L"StartService");

    SERVICE_STATUS ss;
    if ( !QueryServiceStatus( hService, &ss ) )
       Error(HRESULT_FROM_WIN32(GetLastError()), L"QueryServiceStatus");

    while (ss.dwCurrentState == SERVICE_START_PENDING)
        {
        Sleep(ss.dwWaitHint);
        if ( !QueryServiceStatus( hService, &ss ) )
           Error(HRESULT_FROM_WIN32(GetLastError()), L"QueryServiceStatus");        
        }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);

}

void EnsurePath(LPCWSTR wszDest)
{
    CComBSTR bstr = wszDest;
    UINT cwc = (UINT) wcslen(wszDest);
    HANDLE hFile;
    LPWSTR wszBuf = new WCHAR[cwc + 1];
    if (wszBuf == NULL)
        Error(E_OUTOFMEMORY, L"Out of Memory");

    while(TRUE)
        {
        LPWSTR wsz = wcsrchr(bstr, L'\\');
        if (wsz == NULL)
            break;

        *wsz = L'\0';

        wcscpy(wszBuf, bstr);
        wcscat(wszBuf, L"\\");

        hFile = CreateFile
                    (
                    wszBuf,
                    GENERIC_READ,
                    FILE_SHARE_READ|FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS,
                    NULL
                    );

        if (hFile != INVALID_HANDLE_VALUE)
            {
            CloseHandle(hFile);
            break;
            }
        }

    delete wszBuf;

    // add backslash removed previously
    bstr[wcslen(bstr)] = L'\\';

    while(wcslen(bstr) < cwc)
        {
        if (!CreateDirectory(bstr, NULL))
            {
            DWORD dwErr = GetLastError();
            Error(HRESULT_FROM_WIN32(dwErr), L"CreateDirectory failed with error %d.\n", dwErr);
            }

        bstr[wcslen(bstr)] = L'\\';
        }
}

void DoCopyFile(LPCWSTR wszSource, LPCWSTR wszDest)
    {
    EnsurePath(wszDest);
    
    if (wszSource)
        {
        if (!CopyFile(wszSource, wszDest, FALSE))
            {
            DWORD dwErr = GetLastError();
            Error(HRESULT_FROM_WIN32(dwErr), L"CopyFile failed with error %d.\n", dwErr);
            }
        }
    }




void SaveFilesMatchingFilespec
    (
    LPCWSTR wszSnapshotPath,
    LPCWSTR wszSavedPath,
    LPCWSTR wszOriginalPath,
    LPCWSTR wszFilespec,
    FILETIME time,
    const CVssSimpleMap<FILE_DESCRIPTION, bool>* excludeMap,
    const CVssSimpleMap<FILE_DESCRIPTION, bool>* alreadyIncluded    
    )
    {
    bool bCheckingFiletime = time.dwLowDateTime > 0 || time.dwHighDateTime > 0;    

    CComBSTR bstrSP = wszSnapshotPath;
    
    if (bstrSP[wcslen(bstrSP) - 1] != L'\\')
       bstrSP.Append(L"\\");
    bstrSP.Append(wszFilespec);

    if (g_bVerbose) wprintf (L"saving files matching filespec %s\n", bstrSP);
    WIN32_FIND_DATA findData;
    HANDLE hFile = FindFirstFile(bstrSP, &findData);
    if (hFile == INVALID_HANDLE_VALUE)
        {
        wprintf (L"FindFirstFile failed with GetLastError=%d\n", GetLastError());
        return;
        }

    try
        {
        do
            {
            if (g_bVerbose) wprintf(L"saving file %s from path %s\n", findData.cFileName, wszSnapshotPath);            
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                // don't back up overridden files
                if ((alreadyIncluded && alreadyIncluded->Lookup(FILE_DESCRIPTION(wszOriginalPath, findData.cFileName, false))) ||
                     (excludeMap && excludeMap->Lookup(FILE_DESCRIPTION(wszOriginalPath, findData.cFileName, false))) )
                    {
                    wprintf(L"excluding file %s\n", findData.cFileName);
                    continue;
                    }
                
                CComBSTR bstrCP = wszSavedPath;

                bstrSP = wszSnapshotPath;
                if (bstrSP[wcslen(bstrSP) - 1] != L'\\')
                    bstrSP.Append(L"\\");
                bstrSP.Append(findData.cFileName);

                if (bstrCP[wcslen(bstrCP) - 1] != L'\\')
                    bstrCP.Append(L"\\");
                bstrCP.Append(findData.cFileName);

                if (bCheckingFiletime)
                    {
                    CVssAutoWin32Handle hFile = ::CreateFile
                                        (
                                        bstrSP,
                                        GENERIC_READ,
                                        FILE_SHARE_READ,
                                        NULL,
                                        OPEN_EXISTING,
                                        0,
                                        NULL
                                        );
                    if (!hFile.IsValid())
                        Error(HRESULT_FROM_WIN32(::GetLastError()), L"CreateFile failed while checking file time");

                    FILETIME lastWrite;
                    if (!GetFileTime(hFile, NULL, NULL, &lastWrite))
                        Error(HRESULT_FROM_WIN32(::GetLastError()), L"CreateFile failed while checking file time");

                    if (CompareFileTime(&lastWrite, &time) < 0)
                        continue;
                    }
                

                if (g_bVerbose) wprintf (L"copying file %s to %s\n", bstrSP, bstrCP);
                DoCopyFile(bstrSP, bstrCP);
                if (g_bVerbose) wprintf (L"copied file %s to %s\n", bstrSP, bstrCP);
                }
            } while(FindNextFile(hFile, &findData));

        FindClose(hFile);
        }
    catch(...)
        {
        FindClose(hFile);
        throw;
        }
    }

void RecurseSaveFiles
    (
    LPCWSTR wszSnapshotPath,
    LPCWSTR wszSavedPath,
    LPCWSTR wszOriginalPath,
    LPCWSTR wszFilespec,
    FILETIME time,
    const CVssSimpleMap<FILE_DESCRIPTION, bool>* excludeMap,
    const CVssSimpleMap<FILE_DESCRIPTION, bool>* alreadyIncluded
    )
    {
    CComBSTR bstrSP = wszSnapshotPath;
    bstrSP.Append(L"\\*.*");

    WIN32_FIND_DATA findData;
    HANDLE hFile = FindFirstFile(bstrSP, &findData);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    try
        {
        do
            {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                if (wcscmp(findData.cFileName, L".") == 0 ||
                    wcscmp(findData.cFileName, L"..") == 0)
                    continue;

                bstrSP = wszSnapshotPath;
                bstrSP.Append(L"\\");
                bstrSP.Append(findData.cFileName);
                CComBSTR bstrCP = wszSavedPath;
                bstrCP.Append(L"\\");
                bstrCP.Append(findData.cFileName);

                SaveFilesMatchingFilespec(bstrSP, bstrCP, wszOriginalPath, wszFilespec, time, excludeMap, alreadyIncluded);
                RecurseSaveFiles(bstrSP, bstrCP, wszOriginalPath, wszFilespec, time, excludeMap, alreadyIncluded);
                }
            } while(FindNextFile(hFile, &findData));

        FindClose(hFile);
        }
    catch(...)
        {
        FindClose(hFile);
        throw;
        }
    }

bool BuildSnapshotPath
    (
    bool bNeedsSnapshot,
    SAVE_INFO &info,
    LPCWSTR wszPath,
    CComBSTR &bstrSnapshotPath
    )
    {
    bool bTryShare = false;
    
    // drive letter
    CComBSTR bstrPath((UINT) wcslen(wszPath) + 1);
    CComBSTR bstrVolumePath((UINT) wcslen(wszPath) + 1);

    wcscpy(bstrPath, wszPath);
    if (wszPath[wcslen(wszPath) - 1] != L'\\')
        wcscat(bstrPath, L"\\");

    if (!GetVolumePathName(bstrPath, bstrVolumePath, (UINT) wcslen(wszPath) + 1))
        {
        DWORD dwErr = GetLastError();
        if (dwErr == ERROR_FILENAME_EXCED_RANGE)
            {
            if (wcslen(bstrPath) >= 3 && bstrPath[1] == L':' && bstrPath[2] == L'\\')
                {
                memcpy(bstrVolumePath, bstrPath, 6);
                bstrVolumePath[3] = L'\0';
                }
            else
                Error(HRESULT_FROM_WIN32(dwErr), L"GetVolumePathName failed with error %d\nPath=%s.", dwErr, wszPath);
            }
        else
            Error(HRESULT_FROM_WIN32(dwErr), L"GetVolumePathName failed with error %d\nPath=%s.", dwErr, wszPath);
        }

    WCHAR wszVolumeName[MAX_PATH];

    if (!GetVolumeNameForVolumeMountPoint(bstrVolumePath, wszVolumeName, MAX_PATH))
        bTryShare = true;

    if (! bTryShare)
        {
        LPCWSTR wszSnapshotDeviceName = info.mapSnapshots.Lookup(wszVolumeName);
        if (wszSnapshotDeviceName == NULL && !bNeedsSnapshot)
            bstrSnapshotPath.Append(wszVolumeName);
        else if (wszSnapshotDeviceName == NULL)
            Error(E_UNEXPECTED, L"Snapshot device does not exist for path %s", wszPath);
        else
            bstrSnapshotPath.Append(wszSnapshotDeviceName);
        
        bstrSnapshotPath.Append(wszPath + wcslen(bstrVolumePath) - 1);
        return true;
        }
    else
        {
        // share options
        // Path that we added to the set was either the full path from the writer or the 
        // share-volume root (see in DoAddToSnapshotSet)
        CComBSTR bstrLookupPath;
        LPWSTR wszSnapshotShareName = NULL;
        if (g_bAddFullUNCPath)
            bstrLookupPath.Append(wszPath);
        else
            bstrLookupPath.Append(bstrVolumePath);
        wszSnapshotShareName = info.mapSnapshots.Lookup((LPWSTR)bstrLookupPath);
        if (wszSnapshotShareName == NULL)
            {
            // Check for terminating backslash, if so try without it
            BSTR bstrTemp = (BSTR)bstrLookupPath;
            if (bstrTemp[wcslen(bstrTemp) - 1] == L'\\')
                bstrTemp[wcslen(bstrTemp) - 1] = L'\0';
            wszSnapshotShareName = info.mapSnapshots.Lookup((LPWSTR)bstrLookupPath);
            }
        if (wszSnapshotShareName == NULL)
            Error(E_UNEXPECTED, L"Snapshot share does not exist for path %s volumePath %s", 
                        wszPath, (LPWSTR)bstrVolumePath);

        // In any case, the path in addition ot the share should be the reminder after the share/volume root
        // BUGBUG: This doesn't work for the DFS case (need to resolve DFS to a share first)
        bstrSnapshotPath.Append(wszSnapshotShareName);
        bstrSnapshotPath.Append(wszPath + wcslen(bstrVolumePath) - 1);
        return true;
        }
    }


void BuildSavedPath
    (
    LPCWSTR wszPath,
    CComBSTR &bstrSavedPath
    )
{
    bstrSavedPath.Append(g_wszSavedFilesDirectory);
    bstrSavedPath.Append(L"VOLUME");

    BS_ASSERT(wcslen(wszPath) >= 2);
    if (iswalpha(wszPath[0]) && wszPath[1] == L':') {
        // drive letter
        WCHAR wszDrive[2];
        wszDrive[0] = wszPath[0];
        wszDrive[1] = L'\0';
        bstrSavedPath.Append(wszDrive);
        bstrSavedPath.Append(wszPath + 2);
    } else {
        // share options
        DWORD dwPrefixLen;
        if (IsUNCPrefixLen(wszPath, dwPrefixLen)) {
            bstrSavedPath.Append(L"\\");
            bstrSavedPath.Append(wszPath + dwPrefixLen);
        } else {
/*        BS_ASSERT(wcslen(wszPath) >= g_cVolumeLength);
        WCHAR wszVolumeName[g_cVolumeLength + 1];
        wcsncpy(wszVolumeName, wszPath, g_cVolumeLength + 1);
        BS_ASSERT(IsDASD(wszVolumeName));

        bstrSavedPath.Append(wszVolumeName);
        bstrSavedPath.Append(wszPath + g_cVolumeLength);
        */
        
            // check if volume-guid-name
            WCHAR *pwszPrefix = L"\\\\?\\Volume";
            if (_wcsnicmp(wszPath, pwszPrefix, wcslen(pwszPrefix)) == 0) {
                bstrSavedPath.Append(wszPath + wcslen(pwszPrefix));
            } else {
                // unexpected format - assert
                BS_ASSERT(FALSE);
            }
        }
    }
}

void DoExpandEnvironmentStrings(CComBSTR &bstrPath)
    {
    if (!bstrPath)
        return;

    if (wcschr(bstrPath, L'%') != NULL)
        {
        WCHAR wsz[MAX_PATH];

        UINT cwc = ExpandEnvironmentStrings(bstrPath, wsz, MAX_PATH);
        if (cwc == 0)
            {
            DWORD dwErr = GetLastError();
            Error(HRESULT_FROM_WIN32(dwErr), L"ExpandEnvironmentStrings failed due to error %d.\n", dwErr);
            }
        else if (cwc <= MAX_PATH)
            bstrPath = wsz;
        else
            {
            LPWSTR wszT = new WCHAR[cwc];
            if (!ExpandEnvironmentStrings(bstrPath, wszT, MAX_PATH))
                {
                DWORD dwErr = GetLastError();
                Error(HRESULT_FROM_WIN32(dwErr), L"ExpandEnvironmentStrings failed due to error %d.\n", dwErr);
                }

            bstrPath = wszT;
            }
        }
    }

void AddFileInfo
        (
        CVssSimpleMap<FILE_DESCRIPTION, CComPtr<IVssWMFiledesc> >& fileMap, 
        IVssWMFiledesc * pFiledesc
        )
{
    CComBSTR bstrPath;
    CComBSTR bstrFilespec;
    bool bRecursive;
    HRESULT hr;

    CHECK_SUCCESS(pFiledesc->GetPath(&bstrPath));
    CHECK_SUCCESS(pFiledesc->GetFilespec(&bstrFilespec));
    CHECK_NOFAIL(pFiledesc->GetRecursive(&bRecursive));

    if (!fileMap.Add(FILE_DESCRIPTION(bstrPath, bstrFilespec, bRecursive), pFiledesc))
        Error(E_OUTOFMEMORY, L"Out of memory");
}
void SaveDataFiles
    (
    SAVE_INFO &saveInfo,
    IVssWMFiledesc *pFiledesc,
    CVssSimpleMap<FILE_DESCRIPTION, bool>& alreadyIncluded
    )
    {
    CComBSTR bstrPath;
    CComBSTR bstrFilespec;
    bool bRecursive;
    CComBSTR bstrAlternatePath;
    HRESULT hr;

    CHECK_SUCCESS(pFiledesc->GetPath(&bstrPath));
    CHECK_SUCCESS(pFiledesc->GetFilespec(&bstrFilespec));
    CHECK_NOFAIL(pFiledesc->GetRecursive(&bRecursive));

    DWORD dwMask;
    CHECK_SUCCESS(pFiledesc->GetBackupTypeMask(&dwMask));
    
    if (!needsBackingUp(dwMask))
        return;
    
    CHECK_NOFAIL(pFiledesc->GetAlternateLocation(&bstrAlternatePath));

    DoExpandEnvironmentStrings(bstrPath);
    DoExpandEnvironmentStrings(bstrAlternatePath);

    CComBSTR bstrSnapshotPath;
    CComBSTR bstrSavedPath;
    if (!BuildSnapshotPath
            (
            needsSnapshot(dwMask),
            saveInfo,
            bstrAlternatePath ? bstrAlternatePath : bstrPath,
            bstrSnapshotPath
            ))
        return;

    BuildSavedPath(bstrPath, bstrSavedPath);
//    wprintf(L"\nSaveFiles params: snap-path <%s> save-path <%s> file-spec <%s>\n", 
//        (LPWSTR)bstrSnapshotPath, (LPWSTR)bstrSavedPath, (LPWSTR)bstrFilespec);

    if (isDASD(bstrPath) && bstrFilespec.Length() == 0)
        {
        if (alreadyIncluded.Lookup(FILE_DESCRIPTION(bstrPath, bstrFilespec, false)))
            return;

//        SaveVolume(bstrSnapshotPath, bstrSavedPath, 0, _UI64_MAX);
        }        
    else
        {
        FILETIME time;
        memset(&time, 0, sizeof(time));
        SaveFilesMatchingFilespec(bstrSnapshotPath, bstrSavedPath, bstrPath, bstrFilespec, time, &saveInfo.excludeFiles, &alreadyIncluded);
        if (bRecursive)
            RecurseSaveFiles(bstrSnapshotPath, bstrSavedPath, bstrPath, bstrFilespec, time, &saveInfo.excludeFiles, &alreadyIncluded);         
        }        
    }

void BuildPartialFileSavedPath(LPCWSTR wszPath, CComBSTR& bstrSavedPath)
    {
    BuildSavedPath(wszPath, bstrSavedPath);

    if (bstrSavedPath[bstrSavedPath.Length() - 1] != L'\\')
        bstrSavedPath.Append(L"\\");
    
    bstrSavedPath.Append(g_wszPartialFilePath);
    }

void BuildDifferencedFileSavedPath(LPCWSTR wszPath, CComBSTR& bstrSavedPath)
    {
    BuildSavedPath(wszPath, bstrSavedPath);

    if (bstrSavedPath[bstrSavedPath.Length() - 1] != L'\\')
        bstrSavedPath.Append(L"\\");
    
    bstrSavedPath.Append(g_wszDifferencedFilePath);
    }

void SavePartialFile
    (
    SAVE_INFO &saveInfo, 
    LPCWSTR wszPath, 
    LPCWSTR wszFilename, 
    LPCWSTR wszRanges,
    CVssSimpleMap<FILE_DESCRIPTION, CComPtr<IVssWMFiledesc> >& fileMap
    )
    {
    HRESULT hr = S_OK;
    
    CComBSTR bstrExpandedPath = wszPath;
    DoExpandEnvironmentStrings(bstrExpandedPath);

    DWORD dwMask = VSS_FSBT_ALL_BACKUP_REQUIRED | VSS_FSBT_ALL_SNAPSHOT_REQUIRED;
    CComBSTR bstrAlternateLocation;

    IVssWMFiledesc* pFiledesc = fileMap.Lookup(FILE_DESCRIPTION(wszPath, wszFilename, false));
    if (pFiledesc != NULL)
        {
        CHECK_SUCCESS(pFiledesc->GetBackupTypeMask(&dwMask));
        CHECK_NOFAIL(pFiledesc->GetAlternateLocation(&bstrAlternateLocation));
        }
    
    CComBSTR bstrSnapshotPath, bstrSavedPath;
    BuildPartialFileSavedPath(bstrExpandedPath, bstrSavedPath);

    if (!BuildSnapshotPath
        (
        needsSnapshot(dwMask),
        saveInfo,
        (bstrAlternateLocation.Length() > 0) ? bstrAlternateLocation : bstrExpandedPath,
        bstrSnapshotPath
        ))
        return;

/*
        if (isDASD(wszPath) && wcslen(wszFilename) == 0)
        {
        SaveVolume(bstrSnapshotPath, bstrSavedPath, wszRanges);
        }
*/

    SavePartialFile(bstrSnapshotPath, bstrSavedPath, wszFilename, wszRanges);
    }

void SaveDifferencedFile
    (
    SAVE_INFO& saveInfo, 
    LPCWSTR wszPath, 
    LPCWSTR wszFilename, 
    BOOL bRecursive, 
    FILETIME time, 
    CVssSimpleMap<FILE_DESCRIPTION, CComPtr<IVssWMFiledesc> >& fileMap
    )
    {
    HRESULT hr = S_OK;
    
    CComBSTR bstrExpandedPath = wszPath;
    DoExpandEnvironmentStrings(bstrExpandedPath);

    DWORD dwMask = VSS_FSBT_ALL_BACKUP_REQUIRED | VSS_FSBT_ALL_SNAPSHOT_REQUIRED;
    CComBSTR bstrAlternateLocation;

    IVssWMFiledesc* pFiledesc = fileMap.Lookup(FILE_DESCRIPTION(wszPath, wszFilename, false));
    if (pFiledesc != NULL)
        {
        CHECK_SUCCESS(pFiledesc->GetBackupTypeMask(&dwMask));
        CHECK_NOFAIL(pFiledesc->GetAlternateLocation(&bstrAlternateLocation));
        }

    CComBSTR bstrSnapshotPath, bstrSavedPath;
    BuildDifferencedFileSavedPath(bstrExpandedPath, bstrSavedPath);

    if (!BuildSnapshotPath
        (
        needsSnapshot(dwMask),
        saveInfo,
        (bstrAlternateLocation.Length() > 0) ? bstrAlternateLocation : bstrExpandedPath,
        bstrSnapshotPath
        ))
        return;

    SaveFilesMatchingFilespec(bstrSnapshotPath, bstrSavedPath, NULL, wszFilename, time, NULL, NULL);
    if (bRecursive)
        RecurseSaveFiles(bstrSnapshotPath, bstrSavedPath, NULL, wszFilename, time, NULL, NULL);
    }

void getRanges(CSimpleArray<FILE_RANGE>& ranges, LPCWSTR wszRanges)
{
    const WCHAR* wszFilePrefix = L"File=";

    const WCHAR* rangesString = wszRanges;
    if (wcsstr(wszRanges, wszFilePrefix) == wszRanges)
        {
        // TODO: handle this case
        // TODO: remember to backup ranges file as well
        return;
        }

    while (*rangesString != L'\0')
        {
        FILE_RANGE range;
        
        WCHAR* wszStop = NULL;
        range.liStart.LowPart = wcstoul(rangesString, &wszStop, 0);
        BS_ASSERT(wszStop != NULL);
        if (wszStop == rangesString)
            Error(E_UNEXPECTED, L"Error in partial-file ranges string");

        rangesString = wszStop;

        if (*rangesString != L':')
            Error(E_UNEXPECTED, L"Error in partial-file ranges string");

        ++rangesString;
        
        range.liExtent.LowPart = wcstoul(rangesString, &wszStop, 0);
        BS_ASSERT(wszStop != NULL);
        if (wszStop == rangesString)
            Error(E_UNEXPECTED, L"Error in partial-file ranges string");

        rangesString = wszStop;

        ranges.Add(range);

        if (*rangesString == L',')
            ++rangesString;
        }
}

void SavePartialFile
    (
    LPCWSTR wszSourcePath,
    LPCWSTR wszSavePath,
    LPCWSTR wszFilename,
    LPCWSTR wszRanges
    )
{
    CComBSTR bstrSource, bstrDest;
    bstrSource = wszSourcePath;
    if (bstrSource[bstrSource.Length() - 1] != L'\\')
        bstrSource += L"\\";
    bstrSource += wszFilename;

    bstrDest = wszSavePath;
    if (bstrDest[bstrDest.Length() - 1] != L'\\')
        bstrDest += L"\\";
    bstrDest += wszFilename;
    
    // if there's no range string, we backup the entire file
    if (wcslen(wszRanges) == 0)
        {
        DoCopyFile(bstrSource, bstrDest);
        return;
        }
    
    CSimpleArray<FILE_RANGE> ranges;
    getRanges(ranges, wszRanges);
    BS_ASSERT(ranges.GetSize() > 0);


    wprintf(L"backing up partial file %s\n", bstrSource);

    EnsurePath(bstrDest);
    CVssAutoWin32Handle hSource = ::CreateFile
                                                            (
                                                            bstrSource,
                                                            GENERIC_READ,
                                                            FILE_SHARE_READ,
                                                            NULL,
                                                            OPEN_EXISTING,
                                                            0,
                                                            NULL
                                                            );
    if (!hSource.IsValid())
        Error(HRESULT_FROM_WIN32(::GetLastError()), L"CreateFile failed on source of partial file backup with error code 0x%08lx", HRESULT_FROM_WIN32(::GetLastError()));

    CVssAutoWin32Handle hDest = ::CreateFile
                                                        (
                                                        bstrDest,
                                                        GENERIC_READ | GENERIC_WRITE,
                                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                        NULL,
                                                        CREATE_ALWAYS,
                                                        0,
                                                        NULL
                                                        );
    if (!hDest.IsValid())
        Error(HRESULT_FROM_WIN32(::GetLastError()), L"CreateFile failed on destination of partial file backup");

    BYTE* pBuffer = new BYTE[COPYBUFSIZE];
    if (pBuffer == NULL)
        Error(E_OUTOFMEMORY, L"out of memory");
    
    for (int iRange = 0; iRange < ranges.GetSize(); iRange++)
        {
         if (::SetFilePointer
              (
              hSource,
              ranges[iRange].liStart.LowPart,
              NULL,             // BETest does not support 64-bit ranges today
              FILE_BEGIN
              ) == INVALID_SET_FILE_POINTER)
            {
            Error(HRESULT_FROM_WIN32(::GetLastError()), L"SetFilePointer failed on source of partial file backup");            
            }

        DWORD dwBytesRead = 0;
        DWORD dwBytesWritten = 0;

        DWORD dwBytesRemaining = ranges[iRange].liExtent.LowPart ;
        while (dwBytesRemaining > 0)
            {
            DWORD dwBlockSize = min(dwBytesRemaining, COPYBUFSIZE);
            if (!::ReadFile
                    (
                    hSource, 
                    pBuffer,
                    dwBlockSize,
                    &dwBytesRead ,
                    NULL))
                {
                Error(HRESULT_FROM_WIN32(::GetLastError()), L"ReadFile failed on source of partial file backup");            
                }
            if (dwBytesRead < dwBlockSize)
                dwBytesRemaining = dwBytesRead;
            
            if (!::WriteFile
                    (
                    hDest,
                    pBuffer,
                    dwBytesRead,
                    &dwBytesWritten,
                    NULL))
                {
                Error(HRESULT_FROM_WIN32(::GetLastError()), L"WriteFile failed on destination of partial file backup");            
                }            
            if (dwBytesRead != dwBytesWritten)
                Error(E_UNEXPECTED, L"couldn't finish writing to destination file of partial file backup");

            dwBytesRemaining -= dwBytesRead;
            }        
        }    

    delete [] pBuffer;
    ::SetEndOfFile(hDest);
}

// save data files associated with a component
void SaveComponentFiles
    (
    SAVE_INFO &saveInfo
    )
    {
    HRESULT hr;

    PVSSCOMPONENTINFO pInfo = NULL;
    CComPtr<IVssWMComponent> pComponent;
    CComBSTR bstrComponentLogicalPath;
    CComBSTR bstrComponentName;

    CHECK_NOFAIL(saveInfo.pComponent->GetLogicalPath(&bstrComponentLogicalPath));
    CHECK_SUCCESS(saveInfo.pComponent->GetComponentName(&bstrComponentName));

    // calculate the component's full path
    CComBSTR bstrFullPath = bstrComponentLogicalPath;
    if (bstrFullPath.Length() >  0)
        bstrFullPath += L"\\";
    bstrFullPath += bstrComponentName;
    if (!bstrFullPath)
        Error(E_OUTOFMEMORY, L"Ran out of memory");
    
    try
        {
        unsigned cIncludeFiles, cExcludeFiles, cComponents;
        CHECK_SUCCESS(saveInfo.pMetadata->GetFileCounts
                                    (
                                    &cIncludeFiles,
                                    &cExcludeFiles,
                                    &cComponents
                                    ));

        for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
            {
            CHECK_SUCCESS(saveInfo.pMetadata->GetComponent(iComponent, &pComponent));
            CHECK_SUCCESS(pComponent->GetComponentInfo(&pInfo));

            // if the name and logical path match, we want to save the files
            bool bSaveComponent = false;
            bSaveComponent = (wcscmp(pInfo->bstrComponentName, bstrComponentName) == 0) &&
                                                ((!bstrComponentLogicalPath && !pInfo->bstrLogicalPath) ||
                                                (bstrComponentLogicalPath && pInfo->bstrLogicalPath &&
                                                wcscmp(bstrComponentLogicalPath, pInfo->bstrLogicalPath) == 0));

            // if this is a subcomponent, we want to save the files
            bSaveComponent = bSaveComponent ||
                                (pInfo->bstrLogicalPath && (wcsstr(pInfo->bstrLogicalPath, bstrFullPath) == pInfo->bstrLogicalPath));
                        
            if (bSaveComponent)
                {
                CVssSimpleMap<FILE_DESCRIPTION, CComPtr<IVssWMFiledesc> > fileMap;

               // gather info on all files
               for(UINT iFile = 0; iFile < pInfo->cFileCount; iFile++)
                   {
                   CComPtr<IVssWMFiledesc> pFiledesc;
                   CHECK_SUCCESS(pComponent->GetFile(iFile, &pFiledesc));
                    
                   AddFileInfo(fileMap, pFiledesc);
                   }

               for(iFile = 0; iFile < pInfo->cDatabases; iFile++)
                   {
                   CComPtr<IVssWMFiledesc> pFiledesc;
                   CHECK_SUCCESS(pComponent->GetDatabaseFile(iFile, &pFiledesc));
                   AddFileInfo(fileMap, pFiledesc);
                   }

               for(iFile = 0; iFile < pInfo->cLogFiles; iFile++)
                   {
                   CComPtr<IVssWMFiledesc> pFiledesc;
                   CHECK_SUCCESS(pComponent->GetDatabaseLogFile(iFile, &pFiledesc));
                   AddFileInfo(fileMap, pFiledesc);
                   }

                CVssSimpleMap<FILE_DESCRIPTION, bool > excludedMap;
                
                UINT cPartialFiles = 0;
                CHECK_SUCCESS(saveInfo.pComponent->GetPartialFileCount(&cPartialFiles));
                for (UINT iPartial = 0; iPartial < cPartialFiles; iPartial++)
                    {
                    CComBSTR bstrPath, bstrFilename, bstrRanges, bstrMetadata;
                    CHECK_SUCCESS(saveInfo.pComponent->GetPartialFile
                                                    (
                                                    iPartial, 
                                                    &bstrPath, 
                                                    &bstrFilename, 
                                                    &bstrRanges, 
                                                    &bstrMetadata
                                                    ));
                    if (excludedMap.Add(FILE_DESCRIPTION(bstrPath, bstrFilename, false), true) == FALSE)
                        Error(E_OUTOFMEMORY, L"Out of memory");
                    
                    SavePartialFile(saveInfo, bstrPath, bstrFilename, bstrRanges, fileMap);
                    }

                UINT cDifferencedFiles = 0;
                CHECK_SUCCESS(saveInfo.pComponent->GetDifferencedFilesCount(&cDifferencedFiles));
                for (UINT iDifferenced = 0; iDifferenced < cDifferencedFiles; iDifferenced++)
                    {
                    CComBSTR bstrPath, bstrFilename, bstrRanges, bstrLSN;
                    BOOL bRecursive;
                    FILETIME time;
                    CHECK_SUCCESS(saveInfo.pComponent->GetDifferencedFile
                                                    (
                                                    iDifferenced, 
                                                    &bstrPath, 
                                                    &bstrFilename, 
                                                    &bRecursive,
                                                    &bstrLSN, 
                                                    &time
                                                    ));
                    if (excludedMap.Add(FILE_DESCRIPTION(bstrPath, bstrFilename, !!bRecursive), true) == FALSE)
                        Error(E_OUTOFMEMORY, L"Out of memory");
                        
                    SaveDifferencedFile(saveInfo, bstrPath, bstrFilename, bRecursive, time, fileMap);
                    }

                for (int x = 0; x < fileMap.GetSize(); x++)
                    SaveDataFiles(saveInfo, fileMap.GetValueAt(x), excludedMap);
                
               }


           pComponent->FreeComponentInfo(pInfo);
           pInfo = NULL;
           pComponent = NULL;
           }
        }
    catch(...)
        {
        pComponent->FreeComponentInfo(pInfo);
        throw;
        }
    }

HANDLE OpenMetadataFile(VSS_ID idInstance, BOOL fWrite)
    {
    // create name of saved metadata file
    CComBSTR bstr;
    CComBSTR bstrId = idInstance;
    bstr.Append(g_wszSavedFilesDirectory);
    bstr.Append(L"WRITER");
    bstr.Append(bstrId);
    bstr.Append(L".xml");

    // create and write metadata file
    HANDLE hFile = CreateFile
                        (
                        bstr,
                        GENERIC_READ|GENERIC_WRITE,
                        0,
                        NULL,
                        fWrite ? CREATE_ALWAYS : OPEN_EXISTING,
                        0,
                        NULL
                        );

    if (hFile == INVALID_HANDLE_VALUE)
        {
        DWORD dwErr = GetLastError();
        Error(HRESULT_FROM_WIN32(dwErr), L"CreateFile failed due to error %d.\n", dwErr);
        }

    return hFile;
    }



void SaveFiles
    (
    IVssBackupComponents *pvbc,
    VSS_ID *rgSnapshotId,
    UINT cSnapshots
    )
    {
    SAVE_INFO saveInfo;
    HRESULT hr;

    unsigned cWriterComponents;
    unsigned cWriters;

    if (g_wszSavedFilesDirectory[0] != L'\0')
        {
        for(UINT iSnapshot = 0; iSnapshot < cSnapshots; iSnapshot++)
            {
            VSS_SNAPSHOT_PROP prop;
            CHECK_SUCCESS(pvbc->GetSnapshotProperties(rgSnapshotId[iSnapshot], &prop));

            if (prop.m_pwszSnapshotDeviceObject && prop.m_pwszSnapshotDeviceObject[0])
                {
                // Local snapshot
                CoTaskMemFree(prop.m_pwszOriginatingMachine);
                CoTaskMemFree(prop.m_pwszServiceMachine);
                CoTaskMemFree(prop.m_pwszExposedName);
                CoTaskMemFree(prop.m_pwszExposedPath);
                saveInfo.mapSnapshots.Add(prop.m_pwszOriginalVolumeName, prop.m_pwszSnapshotDeviceObject);

                // BUGBUG: Fix leak of m_pwszOriginalVolumeName and m_pwszSnapshotDeviceObject
                }
            else if (prop.m_pwszExposedName && prop.m_pwszExposedName[0])
                {
                // remote snapshot
                VSS_PWSZ pwszShare = (VSS_PWSZ)CoTaskMemAlloc
                            ((wcslen(prop.m_pwszOriginatingMachine) + wcslen(prop.m_pwszExposedName) + 10) * sizeof(WCHAR));
                if (! pwszShare)
                    {
                    wprintf(L"Failed to allocate memory for share name\n");
                    throw E_OUTOFMEMORY;
                    }
                
                wcscpy(pwszShare, L"\\\\");
                wcscat(pwszShare, prop.m_pwszOriginatingMachine);
                wcscat(pwszShare, L"\\");
                wcscat(pwszShare, prop.m_pwszExposedName);
                
                CoTaskMemFree(prop.m_pwszOriginatingMachine);
                CoTaskMemFree(prop.m_pwszServiceMachine);
                CoTaskMemFree(prop.m_pwszExposedName);
                CoTaskMemFree(prop.m_pwszExposedPath);
                CoTaskMemFree(prop.m_pwszSnapshotDeviceObject);
                saveInfo.mapSnapshots.Add(prop.m_pwszOriginalVolumeName, pwszShare);

                // BUGBUG: Fix leak of m_pwszOriginalVolumeName and pwszShare
                }
            else
                {
                // Unexpected
                BS_ASSERT(FALSE);
                }
            }
        }

    CHECK_SUCCESS(pvbc->GetWriterComponentsCount(&cWriterComponents));
    CHECK_SUCCESS(pvbc->GetWriterMetadataCount(&cWriters));

    saveInfo.pvbc = pvbc;
    for(unsigned iWriter = 0; iWriter < cWriterComponents; iWriter++)
        {
        CComPtr<IVssWriterComponentsExt> pWriter;
        CComPtr<IVssExamineWriterMetadata> pMetadata = NULL;

        CHECK_SUCCESS(pvbc->GetWriterComponents(iWriter, &pWriter));

        unsigned cComponents;
        CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));
        VSS_ID idWriter, idInstance;
        CHECK_SUCCESS(pWriter->GetWriterInfo(&idInstance, &idWriter));

        if (g_wszSavedFilesDirectory[0] != L'\0')
            {
            for(unsigned iWriter = 0; iWriter < cWriters; iWriter++)
                {
                VSS_ID idInstanceMetadata;
                CHECK_SUCCESS(pvbc->GetWriterMetadata(iWriter, &idInstanceMetadata, &pMetadata));
                if (idInstance == idInstanceMetadata)
                    break;

                pMetadata = NULL;
                }

            CComBSTR name;
            VSS_USAGE_TYPE usage;
            VSS_SOURCE_TYPE source;
            CHECK_SUCCESS(pMetadata->GetIdentity(&idInstance, &idWriter, &name, &usage, &source));
            
            wprintf(L"saving metadata for writer %s \n", name);

            
            // save metadata
            CComBSTR bstrMetadata;
            CHECK_SUCCESS(pMetadata->SaveAsXML(&bstrMetadata));

            CVssAutoWin32Handle hFile = OpenMetadataFile(idInstance, true);

            DWORD cbWritten;
            if (!WriteFile(hFile, bstrMetadata, (UINT) wcslen(bstrMetadata)*sizeof(WCHAR), &cbWritten, NULL))
                {
                CloseHandle(hFile);
                DWORD dwErr = GetLastError();
                Error(HRESULT_FROM_WIN32(dwErr), L"WriteFile failed due to error %d.\n", dwErr);
                }

            BS_ASSERT(pMetadata);
            saveInfo.pMetadata = pMetadata;
            UINT cInc, cExcludes, cComponents;
            CHECK_SUCCESS(pMetadata->GetFileCounts(&cInc, &cExcludes, &cComponents));
            for (unsigned x = 0; x < cExcludes; x++)
                {
                CComPtr<IVssWMFiledesc> pExclude;
                CHECK_SUCCESS(pMetadata->GetExcludeFile(x, &pExclude));

                CComBSTR bstrPath, bstrFilename;
                bool bRecursive;
                CHECK_SUCCESS(pExclude->GetPath(&bstrPath));
                CHECK_SUCCESS(pExclude->GetFilespec(&bstrFilename));
                CHECK_SUCCESS(pExclude->GetRecursive(&bRecursive));

                if (!saveInfo.excludeFiles.Add(FILE_DESCRIPTION(bstrPath, bstrFilename, bRecursive), true))
                    Error(E_OUTOFMEMORY, L"out of memory");                
                }            
            }

        for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
            {
            CComPtr<IVssComponent> pComponent;
            CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));

            VSS_COMPONENT_TYPE ct;
            CComBSTR bstrLogicalPath;
            CComBSTR bstrComponentName;

            CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
            CHECK_SUCCESS(pComponent->GetComponentType(&ct));
            CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));
            CComBSTR bstrStamp;

            CHECK_NOFAIL(pComponent->GetBackupStamp(&bstrStamp));
            if (bstrStamp)
                wprintf(L"Backup stamp for component %s = %s\n", bstrComponentName, bstrStamp);

            wprintf (L"\t\tsaving component %s\\%s\n", bstrLogicalPath, bstrComponentName);
            
            if (g_wszSavedFilesDirectory[0] != L'\0')
                {
                saveInfo.pComponent = pComponent;
                SaveComponentFiles(saveInfo);
                }

            CHECK_SUCCESS
                (
                pvbc->SetBackupSucceeded
                    (
                    idInstance,
                    idWriter,
                    ct,
                    bstrLogicalPath,
                    bstrComponentName,
                    true)
                );
            }
        }
    }

class CRestoreFile
    {
public:
    CRestoreFile(CRestoreFile *pFile)
        {
        m_hDestination = INVALID_HANDLE_VALUE;
        m_pNext = pFile;
        }

    virtual ~CRestoreFile()
        {
        if (m_hDestination != INVALID_HANDLE_VALUE)
            CloseHandle(m_hDestination);
        }

    void SetSourceFile(LPCWSTR wszPath) { m_bstrSourceFile = wszPath; }
    void SetDestinationHandle(HANDLE hFile) { m_hDestination = hFile; }
    void SetDestinationFile(LPCWSTR wszPath) { m_bstrDestinationPath = wszPath; }

    CRestoreFile *m_pNext;
    CComBSTR m_bstrSourceFile;
    CComBSTR m_bstrDestinationPath;
    HANDLE m_hDestination;
    };

class CRestorePartialFile : public CRestoreFile
    {
public:
    CRestorePartialFile(CRestoreFile* pFile) : CRestoreFile(pFile)
        {}
    
    void SetRanges(LPCWSTR wszRanges = L"") { m_bstrRanges = wszRanges; }
    
    CComBSTR m_bstrRanges;
    };

typedef struct _ALTERNATE_MAPPING
    {
    CComBSTR bstrPath;
    CComBSTR bstrAlternatePath;
    CComBSTR bstrFilespec;
    bool bRecursive;
    } ALTERNATE_MAPPING;


class RESTORE_INFO
    {
public:
    RESTORE_INFO()
        {
        rgMappings = NULL;
        pFile = NULL;
        pPartialFile = NULL;        
        pCopyBuf = NULL;
        bRebootRequired = false;
        restoreTarget = VSS_RT_ORIGINAL;        
        cMappings = 0;
        cTargets = 0;
        rgTargets = NULL;
        pComponentInfo = NULL;        
        }


    ~RESTORE_INFO()
        {
        delete [] rgMappings;
        delete [] rgTargets;

        CRestoreFile *pFileT = pFile;
        while(pFileT)
            {
            CRestoreFile *pFileNext = pFileT->m_pNext;
            delete pFileT;
            pFileT = pFileNext;
            }

        pFileT = pPartialFile;
        while(pFileT)
            {
            CRestoreFile *pFileNext = pFileT->m_pNext;
            delete pFileT;
            pFileT = pFileNext;
            }
        
        delete pCopyBuf;

        if (pWriterComponent && pComponentInfo)
            pWriterComponent->FreeComponentInfo(pComponentInfo);        
        }

    VSS_ID idWriter;
    VSS_ID idInstance;
    VSS_COMPONENT_TYPE ct;
    IVssExamineWriterMetadata *pMetadataSaved;
    IVssBackupComponents *pvbc;
    IVssComponent *pComponent;
    LPCWSTR wszLogicalPath;
    LPCWSTR wszComponentName;
    CComBSTR bstrServiceName;    
    VSS_RESTOREMETHOD_ENUM method;
    bool bRebootRequired;
    CRestoreFile *pFile;
    CRestorePartialFile* pPartialFile;    
    unsigned cMappings;
    ALTERNATE_MAPPING *rgMappings;
    unsigned cTargets;
    ALTERNATE_MAPPING* rgTargets;
    BYTE *pCopyBuf;
    VSS_RESTORE_TARGET restoreTarget;
    CComPtr<IVssWMComponent> pWriterComponent;
    PVSSCOMPONENTINFO pComponentInfo;
    };


void CompleteFiles(RESTORE_INFO &info, CRestoreFile* pFile)
    {
    while(pFile != NULL)
        {
        if (pFile->m_hDestination != INVALID_HANDLE_VALUE &&
            pFile->m_bstrSourceFile)
            {
            CVssAutoWin32Handle hSource = CreateFile
                                            (
                                            pFile->m_bstrSourceFile,
                                            GENERIC_READ,
                                            FILE_SHARE_READ,
                                            NULL,
                                            OPEN_EXISTING,
                                            0,
                                            NULL
                                            );

             if (hSource == INVALID_HANDLE_VALUE)
                 {
                 DWORD dwErr = GetLastError();
                 Error(HRESULT_FROM_WIN32(dwErr), L"CreateFile failed with error %d.\n", dwErr);
                 }

             DWORD dwSize = GetFileSize(hSource, NULL);
             if (dwSize == 0xffffffff)
                 {
                 DWORD dwErr = GetLastError();
                 Error(HRESULT_FROM_WIN32(dwErr), L"GetFileSize failed with error %d.\n", dwErr);
                 }

             while(dwSize > 0)
                 {
                 DWORD cb = min(COPYBUFSIZE, dwSize);
                 DWORD dwRead, dwWritten;
                 if (!ReadFile(hSource, info.pCopyBuf, cb, &dwRead, NULL))
                     {
                     DWORD dwErr = GetLastError();
                     Error(HRESULT_FROM_WIN32(dwErr), L"ReadFile failed dued to error %d.\n", dwErr);
                     }

                 if (!WriteFile(pFile->m_hDestination, info.pCopyBuf, cb, &dwWritten, NULL) ||
                     dwWritten < cb)
                     {
                     DWORD dwErr = GetLastError();
                     Error(HRESULT_FROM_WIN32(dwErr), L"Write file failed due to error %d.\n", dwErr);
                     }

                 dwSize -= cb;
                 }

             if (!SetEndOfFile(pFile->m_hDestination))
                 {
                 DWORD dwErr = GetLastError();
                 Error(HRESULT_FROM_WIN32(dwErr), L"SetEndOfFile failed due to error %d.\n", dwErr);
                 }
             }

         if (g_bVerbose) wprintf(L"completed file %s\n", pFile->m_bstrDestinationPath);
         info.pFile = pFile->m_pNext;
         delete pFile;
         pFile = info.pFile;
         }
    }

void CompletePartialFiles(RESTORE_INFO &info, CRestorePartialFile* pFile)
    {
    wprintf(L"\n\n");
    
    while (pFile != NULL)
        {
            CVssAutoWin32Handle hSource = CreateFile
                                            (
                                            pFile->m_bstrSourceFile,
                                            GENERIC_READ,
                                            FILE_SHARE_READ,
                                            NULL,
                                            OPEN_EXISTING,
                                            0,
                                            NULL
                                            );
            if (!hSource.IsValid())
                Error(HRESULT_FROM_WIN32(::GetLastError()), L"CreateFile error restoring partial file");

            CSimpleArray<FILE_RANGE> ranges;
            getRanges(ranges, pFile->m_bstrRanges);
            
            for (int iRange = 0; iRange < ranges.GetSize(); iRange++)
                {
                FILE_RANGE range = ranges[iRange];
                
                if (::SetFilePointer(pFile->m_hDestination, range.liStart.LowPart, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
                    Error(HRESULT_FROM_WIN32(::GetLastError()), L"SetFilePointer error restoring partial file");

                DWORD dwBytesToRead = range.liExtent.LowPart;
                while (dwBytesToRead != 0)
                    {
                    DWORD dwBlockSize = min(dwBytesToRead, COPYBUFSIZE);

                    DWORD dwRead = 0;
                    DWORD dwWritten = 0;
                    if (!::ReadFile(hSource, info.pCopyBuf, dwBlockSize, &dwRead, NULL))
                        Error(HRESULT_FROM_WIN32(::GetLastError()), L"ReadFile error restoring partial file");
                    if (dwBlockSize != dwRead)
                        dwBytesToRead = dwRead;

                    if (!::WriteFile(pFile->m_hDestination, info.pCopyBuf, dwRead, &dwWritten, NULL))
                        Error(HRESULT_FROM_WIN32(::GetLastError()), L"WriteFile error restoring partial file");
                    if (dwRead != dwWritten)
                        Error(E_UNEXPECTED, L"couldn't finish writing to destination file of partial file backup");
                                            
                    dwBytesToRead -= dwRead;
                    }
                }
         pFile = static_cast<CRestorePartialFile*>(pFile->m_pNext);        
        }
    }

void CompleteRestore(RESTORE_INFO &info)
    {
    CompleteFiles(info, info.pFile);
    CompletePartialFiles(info, info.pPartialFile);    
     }
void CleanupFailedRestore(RESTORE_INFO &info)
    {
    CRestoreFile *pFile = info.pFile;
    while (pFile != NULL)
        {
        info.pFile = pFile->m_pNext;
        delete pFile;
        pFile = info.pFile;
        }

    pFile = info.pPartialFile;
    while (pFile != NULL)
        {
        info.pFile = pFile->m_pNext;
        delete pFile;
        pFile = info.pFile;
        }
    }

bool PreAdamCustomRestore
        (
        RESTORE_INFO& info
        )
{
    StopService(info.bstrServiceName);
    return true;
}

void PostAdamCustomRestore
    (
    RESTORE_INFO& info
    )
{
    if (g_bAuthRestore)
        wprintf(L"----  Run adamutil.exe manually, and then restart the %s service", info.bstrServiceName);
    else
        StartService(info.bstrServiceName);
}


bool PreCustomRestoreStep
        (
        RESTORE_INFO& info
        )
    {
    BS_ASSERT(info.method == VSS_RME_CUSTOM);

    
    if (info.idWriter == ADAM_WRITER_GUID)
        return PreAdamCustomRestore(info);
    else
        {
        wprintf(L"BETest doesn't support CUSTOM restore for this writer\n:");
        return false;  // don't continue the restore
        }
    }

void PostCustomRestoreStep
    (
    RESTORE_INFO& info
    )
    {
    BS_ASSERT(info.method == VSS_RME_CUSTOM);

    if (info.idWriter == ADAM_WRITER_GUID)
        PostAdamCustomRestore(info);
    else
        BS_ASSERT(false);  // don't continue the restore
    }

bool SetupRestoreFile
    (
    RESTORE_INFO &info,
    LPCWSTR wszSavedFile,
    LPCWSTR wszRestoreFile
    )
    {
    CRestoreFile *pFile = new CRestoreFile(info.pFile);

    // ensure path up to destination file exists
    CComBSTR bstrDestinationPath = wszRestoreFile;
    LPWSTR wsz = wcsrchr(bstrDestinationPath, L'\\');
    *(wsz+1) = L'\0';
    EnsurePath(bstrDestinationPath);

    if (g_bVerbose) wprintf(L"setting up restore file %s\n", wszRestoreFile);
    
    if (info.method == VSS_RME_RESTORE_AT_REBOOT)
        {
        *wsz = L'\0';
        CComBSTR bstrTempFileName((UINT) wcslen(bstrDestinationPath) + MAX_PATH);
        if (!GetTempFileName(bstrDestinationPath, L"TBCK", 0, bstrTempFileName))
            {
            DWORD dwErr = GetLastError();
            Error(HRESULT_FROM_WIN32(dwErr), L"GetTempFileName failed due to error %d.\n", dwErr);
            }

        if (!CopyFile(wszSavedFile, bstrTempFileName, FALSE))
            {
            DWORD dwErr = GetLastError();
            Error(HRESULT_FROM_WIN32(dwErr), L"CopyFile failed due to error %d.\n", dwErr);
            }

        if (!MoveFileEx
            (
            bstrTempFileName,
            wszRestoreFile,
            MOVEFILE_DELAY_UNTIL_REBOOT|MOVEFILE_REPLACE_EXISTING
            ))
            {
            DWORD dwErr = GetLastError();
            Error(HRESULT_FROM_WIN32(dwErr), L"MoveFileEx failed due to error %d.\n", dwErr);
            }

        info.bRebootRequired = true;
        }
    else if (info.method == VSS_RME_RESTORE_IF_NOT_THERE)
        {
        HANDLE hFile = CreateFile
                            (
                            wszRestoreFile,
                            GENERIC_WRITE,
                            0,
                            NULL,
                            CREATE_NEW,
                            0,
                            NULL
                            );


        // assume if the create fails
        if (hFile == INVALID_HANDLE_VALUE)
            {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_FILE_EXISTS)
                return false;
            else
                Error(HRESULT_FROM_WIN32(dwErr), L"CreateFile failed due to error %d.\n", dwErr);
            }

        pFile->SetDestinationHandle(hFile);
        pFile->SetSourceFile(wszSavedFile);
        }
    else if (info.method == VSS_RME_RESTORE_IF_CAN_REPLACE || 
                info.method == VSS_RME_RESTORE_TO_ALTERNATE_LOCATION || 
                info.method == VSS_RME_CUSTOM ||
                info.method == VSS_RME_STOP_RESTORE_START ||
                info.method == VSS_RME_RESTORE_AT_REBOOT_IF_CANNOT_REPLACE ||
                info.restoreTarget == VSS_RT_ALTERNATE)
        {

        DWORD dwAttributes = GetFileAttributes(wszRestoreFile);
        dwAttributes = (dwAttributes != INVALID_FILE_ATTRIBUTES) ? dwAttributes : 0;
        
        HANDLE hFile = CreateFile
                            (
                            wszRestoreFile,
                            GENERIC_WRITE,
                            0,
                            NULL,
                            CREATE_ALWAYS,
                            dwAttributes,
                            NULL
                            );

        if (hFile == INVALID_HANDLE_VALUE)
            {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_SHARING_VIOLATION ||
                dwErr == ERROR_USER_MAPPED_FILE ||
                dwErr == ERROR_LOCK_VIOLATION)
                return false;
            else
                Error(HRESULT_FROM_WIN32(dwErr), L"CreateFile failed due to error %d.\n", dwErr);
            }

        pFile->SetDestinationHandle(hFile);
        pFile->SetSourceFile(wszSavedFile);
        }

    info.pFile = pFile;
    return true;
    }

bool SetupRestorePartialFile
    (
    RESTORE_INFO& info,
    LPCWSTR wszSource, 
    LPCWSTR wszDestination, 
    LPCWSTR wszRanges
    )
    {
    CRestorePartialFile *pFile = new CRestorePartialFile(info.pPartialFile);
    
    // ensure path up to destination file exists
    CComBSTR bstrDestinationPath = wszDestination;
    LPWSTR wsz = wcsrchr(bstrDestinationPath, L'\\');
    *(wsz+1) = L'\0';
    EnsurePath(bstrDestinationPath);

    CVssAutoWin32Handle hDestination = ::CreateFile
                                                                (
                                                                wszDestination,
                                                                GENERIC_READ | GENERIC_WRITE,
                                                                0,
                                                                NULL,
                                                                OPEN_ALWAYS,
                                                                0,
                                                                NULL
                                                                );
    if (!hDestination.IsValid())
       Error(HRESULT_FROM_WIN32(::GetLastError()), L"CreateFile error on destination of partial file restore");

    pFile->SetRanges(wszRanges);
    pFile->SetSourceFile(wszSource);
    pFile->SetDestinationFile(wszDestination);
    pFile->SetDestinationHandle(hDestination.Detach());

    info.pPartialFile = pFile;

    return true;
    }

bool TranslateRestorePath
    (
    CComBSTR &bstrRP,
    LPCWSTR wszFilename,
    unsigned int cMappings,
    ALTERNATE_MAPPING* pMappings
    )
    {
    BS_ASSERT(bstrRP.Length() > 0);
    if (bstrRP[bstrRP.Length() - 1] != L'\\')
        bstrRP += L"\\";
    
    for(unsigned iMapping = 0; iMapping < cMappings; iMapping++)
        {
        FILE_DESCRIPTION mapping(pMappings[iMapping].bstrPath, pMappings[iMapping].bstrFilespec, pMappings[iMapping].bRecursive);
        FILE_DESCRIPTION query(bstrRP, wszFilename, false);

        if (mapping == query)
            {
            BS_ASSERT(pMappings[iMapping].bRecursive || wcslen(bstrRP) == wcslen(pMappings[iMapping].bstrPath));
            BS_ASSERT(wcslen(pMappings[iMapping].bstrPath) <= wcslen(bstrRP));
            
            CComBSTR bstr = pMappings[iMapping].bstrAlternatePath;
            bstr += bstrRP + wcslen(pMappings[iMapping].bstrPath);
            bstrRP = bstr;

            return true;
            }
        }

        return false;
    }


bool SetupRestoreFilesMatchingFilespec
    (
    RESTORE_INFO &info,
    LPCWSTR wszSourcePath,
    LPCWSTR wszRestorePath,
    LPCWSTR wszFilespec
    )
    {
    CComBSTR bstrSP = wszSourcePath;
    bstrSP.Append(L"\\");
    bstrSP.Append(wszFilespec);
    WIN32_FIND_DATA findData;
    HANDLE hFile = FindFirstFile(bstrSP, &findData);
    if (hFile == INVALID_HANDLE_VALUE)
        return true;

    try
        {
        do
            {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                CComBSTR bstrRP = wszRestorePath;

                bstrSP = wszSourcePath;
                bstrSP.Append(L"\\");
                bstrSP.Append(findData.cFileName);

                bool bTranslated = TranslateRestorePath(bstrRP, findData.cFileName, info.cTargets, info.rgTargets);
                if (!bTranslated && 
                    (info.method == VSS_RME_RESTORE_TO_ALTERNATE_LOCATION || info.restoreTarget == VSS_RT_ALTERNATE))
                    bTranslated = TranslateRestorePath(bstrRP, findData.cFileName, info.cMappings, info.rgMappings);                

                bstrRP.Append(findData.cFileName);
                if (!SetupRestoreFile(info, bstrSP, bstrRP))
                    return false;
                }
            } while(FindNextFile(hFile, &findData));

        FindClose(hFile);
        }
    catch(...)
        {
        FindClose(hFile);
        throw;
        }

    return true;
    }

bool RecursiveRestoreFiles
    (
    RESTORE_INFO &info,
    LPCWSTR wszSavedPath,
    LPCWSTR wszPath,
    LPCWSTR wszFilespec
    )
    {
    CComBSTR bstrSP = wszSavedPath;
    bstrSP.Append(L"\\*.*");

    WIN32_FIND_DATA findData;
    HANDLE hFile = FindFirstFile(bstrSP, &findData);
    if (hFile == INVALID_HANDLE_VALUE)
        return true;
    try
        {
        do
            {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                if (wcscmp(findData.cFileName, L".") == 0 ||
                    wcscmp(findData.cFileName, L"..") == 0)
                    continue;

                // don't try and restore the partial-file information or the differenced-file information
                if (_wcsicmp(findData.cFileName, g_wszPartialFilePath) == 0)
                    continue;

                if (_wcsicmp(findData.cFileName, g_wszDifferencedFilePath) == 0)
                    continue;
                
                bstrSP = wszSavedPath;
                bstrSP.Append(L"\\");
                bstrSP.Append(findData.cFileName);
                CComBSTR bstrRP = wszPath;
                bstrRP.Append(L"\\");
                bstrRP.Append(findData.cFileName);

                if (!SetupRestoreFilesMatchingFilespec(info, bstrSP, bstrRP, wszFilespec))
                    return false;

                if (!RecursiveRestoreFiles(info, bstrSP, bstrRP, wszFilespec))
                    return false;
                }
            } while(FindNextFile(hFile, &findData));

        FindClose(hFile);
        }
    catch(...)
        {
        FindClose(hFile);
        throw;
        }

    return true;
    }



bool SetupRestoreDataFiles(RESTORE_INFO &info, IVssWMFiledesc *pFiledesc)
    {
    HRESULT hr;
    CComBSTR bstrPath;
    CComBSTR bstrFilespec;
    bool bRecursive;

    CHECK_SUCCESS(pFiledesc->GetPath(&bstrPath));
    CHECK_SUCCESS(pFiledesc->GetFilespec(&bstrFilespec));
    CHECK_NOFAIL(pFiledesc->GetRecursive(&bRecursive));

    CComBSTR bstrSavedPath;
    BuildSavedPath(bstrPath, bstrSavedPath);
//    wprintf(L"\nRestoreFiles (1) params: file-path <%s> save-path <%s> file-spec <%s>\n", 
//        (LPWSTR)bstrPath, (LPWSTR)bstrSavedPath, (LPWSTR)bstrFilespec);
    if (!SetupRestoreFilesMatchingFilespec(info, bstrSavedPath, bstrPath, bstrFilespec)) 
        {
        wprintf(L"SetupRestoreFilesMatchingFilespec Failed !!!\n");
        return false;
        }

    if (bRecursive)
        {
        bool bRet = RecursiveRestoreFiles(info, bstrSavedPath, bstrPath, bstrFilespec);
        if (! bRet)
            wprintf(L"RecursiveRestoreFiles Failed !!!\n");
        return bRet;
        }

    return true;
    }

bool SetupRestorePartialDataFiles
        (
        RESTORE_INFO& info, 
        LPCWSTR wszPath, 
        LPCWSTR wszFilename, 
        LPCWSTR wszRanges
        )
    {
    CComBSTR bstrSavedPath;
    BuildPartialFileSavedPath(wszPath, bstrSavedPath);

    CComBSTR bstrSource, bstrDestination;

    bstrSource = bstrSavedPath;
    if (bstrSource[bstrSource.Length() - 1] != L'\\')
        bstrSource += L"\\";

    bstrSource += wszFilename;

    bstrDestination = wszPath;
    if (bstrDestination[bstrDestination.Length() - 1] != L'\\')
        bstrDestination += L"\\";

    bstrDestination += wszFilename;

    return SetupRestorePartialFile(info, bstrSource, bstrDestination, wszRanges);
    }

bool SetupRestoreDifferencedDataFiles
    (
    RESTORE_INFO& info, 
    LPCWSTR wszPath, 
    LPCWSTR wszFilename, 
    BOOL bRecursive
    )
    {    
    CComBSTR bstrSavedPath;
    BuildDifferencedFileSavedPath(wszPath, bstrSavedPath);
    if (!SetupRestoreFilesMatchingFilespec(info, bstrSavedPath, wszPath, wszFilename))
        return false;

    if (bRecursive)
        return RecursiveRestoreFiles(info, bstrSavedPath, wszPath, wszFilename);

    return true;
   }

bool SetupRestoreDataFilesForComponent(RESTORE_INFO& info, bool bInDocument)
    {
    HRESULT hr = S_OK;
    
    if (bInDocument)
        {    
        UINT cPartialFiles = 0;
        CHECK_SUCCESS(info.pComponent->GetPartialFileCount(&cPartialFiles));
        for (UINT iFile = 0; iFile < cPartialFiles; iFile++)
            {
            CComBSTR bstrPath, bstrFilename, bstrRanges, bstrMetadata;
            CHECK_SUCCESS(info.pComponent->GetPartialFile
                (
                iFile,
                &bstrPath,
                &bstrFilename,
                &bstrRanges,
                &bstrMetadata
                ));

            if (!SetupRestorePartialDataFiles(info, bstrPath, bstrFilename, bstrRanges))
                return false;        
            }

        UINT cDifferencedFiles = 0;
        CHECK_SUCCESS(info.pComponent->GetDifferencedFilesCount(&cDifferencedFiles));

        for (iFile = 0; iFile < cDifferencedFiles; iFile++)
            {
            CComBSTR bstrPath, bstrFilename, bstrRanges, bstrLSN;
            BOOL bRecursive;
            FILETIME time;
            CHECK_SUCCESS(info.pComponent->GetDifferencedFile
                (
                iFile,
                &bstrPath,
                &bstrFilename,
                &bRecursive,
                &bstrLSN,
                &time
                ));

            if (bstrLSN.Length() > 0)
                continue;
            
            if (!SetupRestoreDifferencedDataFiles(info, bstrPath, bstrFilename, bRecursive))
                return false;   
            }
        }

    
    for(UINT iFile = 0; iFile < info.pComponentInfo->cFileCount; iFile++)
        {
        CComPtr<IVssWMFiledesc> pFiledesc;
        CHECK_SUCCESS(info.pWriterComponent->GetFile(iFile, &pFiledesc));

        DWORD dwMask;
        CHECK_SUCCESS(pFiledesc->GetBackupTypeMask(&dwMask));
        
        if (needsBackingUp(dwMask) && !SetupRestoreDataFiles(info, pFiledesc))
            return false;
        }

    for(iFile = 0; iFile < info.pComponentInfo->cDatabases; iFile++)
        {
        CComPtr<IVssWMFiledesc> pFiledesc;
        CHECK_SUCCESS(info.pWriterComponent->GetDatabaseFile(iFile, &pFiledesc));

        DWORD dwMask;
        CHECK_SUCCESS(pFiledesc->GetBackupTypeMask(&dwMask));
        
        if (needsBackingUp(dwMask) && !SetupRestoreDataFiles(info, pFiledesc))
            return false;
        }

    for(iFile = 0; iFile < info.pComponentInfo->cLogFiles; iFile++)
        {
        CComPtr<IVssWMFiledesc> pFiledesc;
        CHECK_SUCCESS(info.pWriterComponent->GetDatabaseLogFile(iFile, &pFiledesc));

        DWORD dwMask;
        CHECK_SUCCESS(pFiledesc->GetBackupTypeMask(&dwMask));
        
        if (needsBackingUp(dwMask) && !SetupRestoreDataFiles(info, pFiledesc))
            return false;
        }
    
    return true;
}

void LoadMetadataFile(VSS_ID idInstance, IVssExamineWriterMetadata **ppMetadataSaved)
    {
    HRESULT hr;

    // load saved metadata
    CVssAutoWin32Handle hFile = OpenMetadataFile(idInstance, false);
    DWORD dwSize = GetFileSize(hFile, NULL);
    if (dwSize == 0xffffffff)
        {
        DWORD dwErr = GetLastError();
        Error(HRESULT_FROM_WIN32(dwErr), L"GetFileSize failed with error %d.\n", dwErr);
        }

    CComBSTR bstrXML(dwSize/sizeof(WCHAR));

    DWORD dwRead;
    if(!ReadFile(hFile, bstrXML, dwSize, &dwRead, NULL))
        {
        DWORD dwErr = GetLastError();
        Error(HRESULT_FROM_WIN32(dwErr), L"ReadFile failed with error %d.\n", dwErr);
        }

    // null terminate XML string
    bstrXML[dwSize/sizeof(WCHAR)] = L'\0';

    CHECK_SUCCESS(CreateVssExamineWriterMetadata(bstrXML, ppMetadataSaved));
    }



bool RestoreComponentFiles(RESTORE_INFO &info)
    {
    HRESULT hr;
    PVSSCOMPONENTINFO pInfo = NULL;
    CComPtr<IVssWMComponent> pComponent;
    VSS_FILE_RESTORE_STATUS status = VSS_RS_NONE;

    CComBSTR bstrUserProcedure;
    CComBSTR bstrService;

    IVssExamineWriterMetadata *pMetadata;
    UINT cIncludeFiles, cExcludeFiles, cComponents;
    CHECK_SUCCESS(info.pMetadataSaved->GetFileCounts(&cIncludeFiles, &cExcludeFiles, &cComponents));
    for (UINT iComponent = 0; iComponent < cComponents; iComponent++)
        {
        CHECK_SUCCESS(info.pMetadataSaved->GetComponent(iComponent, &pComponent));
        CHECK_SUCCESS(pComponent->GetComponentInfo(&pInfo));

        if (wcscmp(pInfo->bstrComponentName, info.wszComponentName) == 0)
            {
            if ((!info.wszLogicalPath && !pInfo->bstrLogicalPath) ||
                (info.wszLogicalPath && pInfo->bstrLogicalPath &&
                 wcscmp(info.wszLogicalPath, pInfo->bstrLogicalPath) == 0))
                {
                info.pWriterComponent = pComponent;
                info.pComponentInfo = pInfo;
                pInfo = NULL;
                pComponent = NULL;
                break;
                }
            }

        pComponent->FreeComponentInfo(pInfo);
        pInfo = NULL;
        pComponent = NULL;
        }

    pMetadata = info.pMetadataSaved;

   CHECK_SUCCESS(info.pComponent->GetRestoreTarget(&info.restoreTarget));
    NewTarget* pCurrentTarget = g_pWriterSelection->GetNewTargets(info.idWriter, info.wszLogicalPath, info.wszComponentName);
    info.cTargets = (pCurrentTarget) ? pCurrentTarget->m_cTargets : 0;

    if (info.rgTargets == NULL && info.cTargets > 0)
        {
        info.rgTargets = new ALTERNATE_MAPPING[info.cTargets];
        if (info.rgTargets == NULL)
            Error(E_OUTOFMEMORY, L"OutOfMemory");

        unsigned iTarget = 0;
        while (pCurrentTarget != NULL)
            {
            BS_ASSERT (iTarget < info.cTargets);
            
            ALTERNATE_MAPPING& target = info.rgTargets[iTarget];
            target.bstrPath = pCurrentTarget->m_bstrSourcePath;
            DoExpandEnvironmentStrings(target.bstrPath);                
            target.bstrAlternatePath = pCurrentTarget->m_bstrTarget;
            DoExpandEnvironmentStrings(target.bstrAlternatePath);
            target.bstrFilespec = pCurrentTarget->m_bstrSourceFilespec;
            target.bRecursive = pCurrentTarget->m_bRecursive;

            if (target.bstrPath[target.bstrPath.Length() - 1] != L'\\')
                target.bstrPath += L"\\";
            if (target.bstrAlternatePath[target.bstrAlternatePath.Length() - 1] != L'\\')
                target.bstrAlternatePath += L"\\";

            pCurrentTarget = pCurrentTarget->m_pNext;
            ++iTarget;
            }
        BS_ASSERT(iTarget == info.cTargets);
        }       
    
    if (info.rgMappings == NULL)
        {
        if (info.cMappings > 0)
            {
            info.rgMappings = new ALTERNATE_MAPPING[info.cMappings];
            if (info.rgMappings == NULL)
                Error(E_OUTOFMEMORY, L"OutOfMemory");
            }

        for(unsigned iMapping = 0; iMapping < info.cMappings; iMapping++)
            {
            ALTERNATE_MAPPING& mapping= info.rgMappings[iMapping];
        
            CComPtr<IVssWMFiledesc> pFiledesc;
            CHECK_SUCCESS(pMetadata->GetAlternateLocationMapping(iMapping, &pFiledesc));
            CHECK_SUCCESS(pFiledesc->GetPath(&mapping.bstrPath));
            DoExpandEnvironmentStrings(mapping.bstrPath);
            CHECK_SUCCESS(pFiledesc->GetAlternateLocation(&mapping.bstrAlternatePath));
            DoExpandEnvironmentStrings(mapping.bstrAlternatePath);
            CHECK_SUCCESS(pFiledesc->GetFilespec(&mapping.bstrFilespec));
            CHECK_SUCCESS(pFiledesc->GetRecursive(&mapping.bRecursive));

        if (mapping.bstrPath[mapping.bstrPath.Length() - 1] != L'\\')
            mapping.bstrPath += L"\\";
        if (mapping.bstrAlternatePath[mapping.bstrAlternatePath.Length() - 1] != L'\\')
            mapping.bstrAlternatePath += L"\\";                                
            }
        }

//        if (info.method == VSS_RME_RESTORE_IF_NOT_THERE ||
//            info.method == VSS_RME_RESTORE_IF_CAN_REPLACE)
//            {
        if (info.pCopyBuf == NULL)
            {
            info.pCopyBuf = new BYTE[COPYBUFSIZE];
            if (info.pCopyBuf == NULL)
                Error(E_OUTOFMEMORY, L"Out of Memory");
            }
//            }


_retry:
    info.pFile = NULL;
    bool bFailRestore = false;

    // setup restore data files for all subcomponents
    UINT cSubcomponents = 0;
    CHECK_SUCCESS(info.pComponent->GetRestoreSubcomponentCount(&cSubcomponents));   

    // setup restore data files for the current component
    if (cSubcomponents == 0)
        bFailRestore = !SetupRestoreDataFilesForComponent(info, true);
    
    for (UINT iSubcomponent = 0; !bFailRestore && iSubcomponent < cSubcomponents; iSubcomponent++)
        {
        CComBSTR bstrSubLogicalPath, bstrSubName;
        bool foo;
        CHECK_SUCCESS(info.pComponent->GetRestoreSubcomponent(iSubcomponent, &bstrSubLogicalPath, 
                                                                                                        &bstrSubName, &foo));

        CComPtr<IVssWMComponent> pSubcomponent;
        if (!FindComponent(pMetadata, bstrSubLogicalPath, bstrSubName, &pSubcomponent))
            Error(E_UNEXPECTED, L"an invalid subcomponent was selected");

        IVssWMComponent* pOldComponent = info.pWriterComponent.Detach();
        info.pWriterComponent = pSubcomponent;

        PVSSCOMPONENTINFO pOldInfo = info.pComponentInfo;
        CHECK_SUCCESS(pSubcomponent->GetComponentInfo(&info.pComponentInfo));
        
        bFailRestore = !SetupRestoreDataFilesForComponent(info, false);

        pSubcomponent->FreeComponentInfo(info.pComponentInfo);
        info.pComponentInfo = pOldInfo;
    
        info.pWriterComponent.Attach(pOldComponent);                   
        }

        // calculate the full path to the current component
        CComBSTR fullPath = info.wszLogicalPath;
        if (fullPath.Length() > 0)
            fullPath += L"\\";
        fullPath += info.wszComponentName;
        if (fullPath.Length() == 0)
            Error(E_OUTOFMEMORY, L"Out of memory!");

        // setup restore data files for all subcomponents
        for (UINT iComponent = 0; !cSubcomponents && !bFailRestore && iComponent < cComponents; iComponent++)
            {
            CComPtr<IVssWMComponent> pCurrentComponent;
            PVSSCOMPONENTINFO pCurrentInfo;
            CHECK_SUCCESS(pMetadata->GetComponent(iComponent, &pCurrentComponent));
            CHECK_SUCCESS(pCurrentComponent->GetComponentInfo(&pCurrentInfo));

            if (pCurrentInfo->bstrLogicalPath &&
                 wcsstr(pCurrentInfo->bstrLogicalPath, fullPath) == pCurrentInfo->bstrLogicalPath)
                {
                IVssWMComponent* pOldComponent = info.pWriterComponent.Detach();
                info.pWriterComponent = pCurrentComponent;

                PVSSCOMPONENTINFO pOldInfo = info.pComponentInfo;
                info.pComponentInfo = pCurrentInfo;
                CHECK_SUCCESS(pCurrentComponent->GetComponentInfo(&info.pComponentInfo));
                
                bFailRestore = !SetupRestoreDataFilesForComponent(info,  false);

                info.pComponentInfo = pOldInfo;        
                info.pWriterComponent.Attach(pOldComponent);                        
                }

            pCurrentComponent->FreeComponentInfo(pCurrentInfo);
            }


    if (!bFailRestore)
        {
        status = VSS_RS_FAILED;
        CompleteRestore(info);
        status = VSS_RS_ALL;
        }
    else
        {
        CleanupFailedRestore(info);
        if ((info.method == VSS_RME_RESTORE_IF_NOT_THERE ||
             info.method == VSS_RME_RESTORE_IF_CAN_REPLACE) &&
            info.cMappings > 0)
            {
            info.method = VSS_RME_RESTORE_TO_ALTERNATE_LOCATION;
            goto _retry;
            }

        if (info.method == VSS_RME_RESTORE_AT_REBOOT_IF_CANNOT_REPLACE)
            {
            info.method = VSS_RME_RESTORE_AT_REBOOT;
            goto _retry;
            }
        }

    pInfo = NULL;
    pComponent = NULL;

    CHECK_SUCCESS(info.pvbc->SetFileRestoreStatus
                                (
                                info.idWriter,
                                info.ct,
                                info.wszLogicalPath,
                                info.wszComponentName,
                                status
                                ));

    return status == VSS_RS_ALL;
    }


void RestoreFiles(IVssBackupComponents *pvbc, const CSimpleMap<VSS_ID, HRESULT>& failedWriters)
    {
    RESTORE_INFO info;
    HRESULT hr;

    UINT cWriterComponents = 0, cWriters = 0;    
    info.pvbc = pvbc;
    CHECK_SUCCESS(pvbc->GetWriterComponentsCount(&cWriterComponents));
    CHECK_SUCCESS(pvbc->GetWriterMetadataCount(&cWriters));

    info.pvbc = pvbc;
    for(unsigned iWriter = 0; iWriter < cWriterComponents; iWriter++)
        {
        CComPtr<IVssWriterComponentsExt> pWriter;
        CComPtr<IVssExamineWriterMetadata> pMetadata = NULL;
        CComPtr<IVssExamineWriterMetadata> pMetadataSaved = NULL;

        CHECK_SUCCESS(pvbc->GetWriterComponents(iWriter, &pWriter));
            
        unsigned cComponents;
        CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));

        for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
            {
            CComPtr<IVssComponent> pComponent;
            bool bSelectedForRestore = false;
            CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));
            CHECK_NOFAIL(pComponent->IsSelectedForRestore(&bSelectedForRestore));
            if (bSelectedForRestore)
                break;

            UINT cSubcomponents = 0;
            CHECK_SUCCESS(pComponent->GetRestoreSubcomponentCount(&cSubcomponents));
            if (cSubcomponents > 0)
                break;
                
            CComBSTR bstrOptions;
            CHECK_NOFAIL(pComponent->GetRestoreOptions(&bstrOptions));
            if (bstrOptions.Length() != 0 && wcscmp(bstrOptions, L"RESTORE") == 0)
                break;
            }

        if (iComponent >= cComponents)
            continue;

        CHECK_SUCCESS(pWriter->GetWriterInfo(&info.idInstance, &info.idWriter));

        for(unsigned iWriter = 0; iWriter < cWriters; iWriter++)
            {
            VSS_ID idInstance, idWriter;
            CComBSTR bstrWriterName;
            VSS_USAGE_TYPE usage;
            VSS_SOURCE_TYPE source;

            CHECK_SUCCESS(pvbc->GetWriterMetadata(iWriter, &idInstance, &pMetadata));
            CHECK_SUCCESS
                (
                pMetadata->GetIdentity
                                (
                                &idInstance,
                                &idWriter,
                                &bstrWriterName,
                                &usage,
                                &source
                                )
                );

            if (idWriter == info.idWriter)
                break;

            pMetadata = NULL;
            }

        // load saved metadata
        LoadMetadataFile(info.idInstance, &pMetadataSaved);

        info.pMetadataSaved = pMetadataSaved;
        bool bWriterFailed = failedWriters.Lookup(info.idInstance) != NULL;

        CComBSTR bstrUserProcedure;
        bool bRebootRequired;
        VSS_WRITERRESTORE_ENUM writerRestore;

        CHECK_NOFAIL(pMetadataSaved->GetRestoreMethod
                                (
                                &info.method,
                                &info.bstrServiceName,
                                &bstrUserProcedure,
                                &writerRestore,
                                &bRebootRequired,
                                &info.cMappings
                                ));

        if (info.method == VSS_RME_CUSTOM)
            {        
            if (!PreCustomRestoreStep(info))        
                continue;
            }
        else if (info.method == VSS_RME_STOP_RESTORE_START)
            StopService(info.bstrServiceName);

        bool bOneSucceeded = false;
        for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
            {
            CComPtr<IVssComponent> pComponent;
            CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));

            bool bSelectedForRestore = false;
            CHECK_NOFAIL(pComponent->IsSelectedForRestore(&bSelectedForRestore));

            UINT cSubcomponents = 0;
            CHECK_SUCCESS(pComponent->GetRestoreSubcomponentCount(&cSubcomponents));

            
            if (!bSelectedForRestore && cSubcomponents == 0)
                {
                // BUGBUG: huge hack to fix the AD case.   We eventually need to 
                // BUGBUG: do something better here
                CComBSTR bstrOptions;
                CHECK_NOFAIL(pComponent->GetRestoreOptions(&bstrOptions));
                if (bstrOptions.Length() == 0 || wcscmp(bstrOptions, L"RESTORE") != 0)
                    continue;
                }

            CComBSTR bstrLogicalPath;
            CComBSTR bstrComponentName;

            CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
            CHECK_SUCCESS(pComponent->GetComponentType(&info.ct));
            CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));

            CComBSTR bstrPreRestoreFailure;
            CHECK_NOFAIL(pComponent->GetPreRestoreFailureMsg(&bstrPreRestoreFailure));
            if (bstrPreRestoreFailure)
                {
                wprintf
                    (
                    L"Not restoring Component %s\\%s because PreRestore failed:\n%s\n",
                    bstrLogicalPath,
                    bstrComponentName,
                    bstrPreRestoreFailure
                    );

                continue;
                }
            else if (bWriterFailed)
                {
                wprintf
                    (
                    L"Not restoring Component %s\\%s because PreRestore failed:\n\n",
                    bstrLogicalPath,
                    bstrComponentName
                    );

                continue;
                }

            info.pComponent = pComponent;
            info.wszLogicalPath = bstrLogicalPath;
            info.wszComponentName = bstrComponentName;
            bOneSucceeded = RestoreComponentFiles(info) || bOneSucceeded;
            }

            if (bOneSucceeded)
                {
                info.bRebootRequired = info.bRebootRequired || bRebootRequired;

                if (info.method == VSS_RME_CUSTOM)
                    PostCustomRestoreStep(info);
                else if (info.method == VSS_RME_STOP_RESTORE_START)
                    StartService(info.bstrServiceName);
                }
        

        // mappings are on a per writer basis and need to be cleared
        // when advancing to a new writer
        delete [] info.rgMappings;
        info.rgMappings = NULL;
        info.cMappings = 0;
        }


    if (info.bRebootRequired)
        wprintf(L"\n\n!!REBOOT is Required to complete the restore operation.\n\n");
    }


#define     UNC_PATH_PREFIX1        (L"\\\\?\\UNC\\")
#define     NONE_UNC_PATH_PREFIX1   (L"\\\\?\\")
#define     UNC_PATH_PREFIX2        (L"\\\\")

BOOL IsUNCPrefixLen(
    IN      LPCWSTR      wszUNCPath,
    OUT     DWORD       &dwPrefixLen
    ) 
{

    dwPrefixLen = 0;

    BOOL bRetval = TRUE;

    // Check UNC path prefix
    if (_wcsnicmp(wszUNCPath, UNC_PATH_PREFIX1, wcslen(UNC_PATH_PREFIX1)) == 0) 
        dwPrefixLen = (DWORD)wcslen(UNC_PATH_PREFIX1);
    else if (_wcsnicmp(wszUNCPath, NONE_UNC_PATH_PREFIX1, wcslen(NONE_UNC_PATH_PREFIX1)) == 0) 
        bRetval = FALSE;
    else if (_wcsnicmp(wszUNCPath, UNC_PATH_PREFIX2, wcslen(UNC_PATH_PREFIX2)) == 0) 
        dwPrefixLen = (DWORD)wcslen(UNC_PATH_PREFIX2);
    else
        bRetval = FALSE;

    return bRetval;
}


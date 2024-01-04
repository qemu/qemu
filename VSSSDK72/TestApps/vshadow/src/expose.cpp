/////////////////////////////////////////////////////////////////////////
// Copyright © 2004 Microsoft Corporation. All rights reserved.
// 
//  This file may contain preliminary information or inaccuracies, 
//  and may not correctly represent any associated Microsoft 
//  Product as commercially released. All Materials are provided entirely 
//  “AS IS.” To the extent permitted by law, MICROSOFT MAKES NO 
//  WARRANTY OF ANY KIND, DISCLAIMS ALL EXPRESS, IMPLIED AND STATUTORY 
//  WARRANTIES, AND ASSUMES NO LIABILITY TO YOU FOR ANY DAMAGES OF 
//  ANY TYPE IN CONNECTION WITH THESE MATERIALS OR ANY INTELLECTUAL PROPERTY IN THEM. 
// 


// Main header
#include "stdafx.h"


// Expose the given shadow copy set as a mount point or drive letter
void VssClient::ExposeSnapshotLocally(VSS_ID snapshotID, wstring path)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"- Exposing shadow copy " WSTR_GUID_FMT L" under the path '%s'", 
        GUID_PRINTF_ARG(snapshotID), path.c_str());

    // Make sure that the expose operation is valid for this snapshot.
    // Get the shadow copy properties
    VSS_SNAPSHOT_PROP Snap;
    HRESULT hr = m_pVssObject->GetSnapshotProperties(snapshotID, &Snap);
    if (hr == VSS_E_OBJECT_NOT_FOUND)
    {
        ft.WriteLine(L"\nERROR: there is no snapshot with the given ID");
        throw(E_INVALIDARG);
    }

    // Automatically call VssFreeSnapshotProperties on this structure at the end of scope
    CAutoSnapPointer snapAutoCleanup(&Snap);

    // Client Accessible snapshots are not exposable
    if (Snap.m_lSnapshotAttributes & VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE)
    {
        ft.WriteLine(L"\nERROR: the snapshot ID identifies a Client Accessible snapshot which cannot be exposed");
        throw(E_INVALIDARG);
    }

    // Client Accessible snapshots are not exposable
    if ((Snap.m_pwszExposedName != NULL) || (Snap.m_pwszExposedPath != NULL))
    {
        ft.WriteLine(L"\nERROR: Client-accessible (SFSF) snapshots cannot be exposed.");
        throw(E_INVALIDARG);
    }

    //
    // Check if the path parameter is valid
    //

    // If this looks like a drive letter then make sure that the drive letter is not in use
    if ((path.length() == 2) && path[1] == L':')
    {
        ft.WriteLine(L"- Checking if '%s' is a valid drive letter ...", path.c_str());

        wstring device(MAX_PATH, L'\0');
        if (0 != QueryDosDeviceW( path.c_str(), WString2Buffer(device), (DWORD)device.length()))
        {
            ft.WriteLine(L"\nERROR: the second parameter to -el [%s] is a drive letter already in use!", path.c_str());
            throw(E_INVALIDARG);
        }
    }
    else
    {
        // Append a backslash
        if (path[path.length() - 1] != L'\\') 
            path += L'\\';

        ft.WriteLine(L"- Checking if '%s' is a valid empty directory ...", path.c_str());

        // Make sure this is a directory
        DWORD dwAttributes = GetFileAttributes(path.c_str());
        if ((dwAttributes == INVALID_FILE_ATTRIBUTES) || ((dwAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0))
        {
            ft.WriteLine(L"\nERROR: the second parameter to -el [%s] is not a valid directory!", path.c_str());
            throw(E_INVALIDARG);
        }

        // Make sure that this directory is empty
        WIN32_FIND_DATA FindFileData;
        wstring pattern = path + L'*';
        HANDLE hFind = FindFirstFile( pattern.c_str(), &FindFileData);
        if (hFind == INVALID_HANDLE_VALUE) 
            CHECK_WIN32_ERROR(GetLastError(), L"FindFirstFile");

        // Automatically calls FindClose at the end of scope
        CAutoSearchHandle autoHandle(hFind);

        // Enumerate all the files/subdirectories
        while (true) 
        { 
            wstring fileName = FindFileData.cFileName; 
            if ((fileName != wstring(L".")) && (fileName != wstring(L"..")))
            {
                ft.WriteLine(L"\nERROR: the second parameter to -el [%s] is not an empty directory!", path.c_str());
                throw(E_INVALIDARG);
            }
         
            if (!FindNextFile(hFind, &FindFileData)) 
            {
                if (GetLastError() == ERROR_NO_MORE_FILES) 
                    break;

                CHECK_WIN32_ERROR(GetLastError(), L"FindNextFile");
            }
        }
    }

    // Expose locally the shadow copy set
    LPWSTR pwszExposed = NULL;
    CHECK_COM(m_pVssObject->ExposeSnapshot(snapshotID, NULL, 
        VSS_VOLSNAP_ATTR_EXPOSED_LOCALLY, (VSS_PWSZ)path.c_str(), &pwszExposed));

    // Automatically call CoTaskMemFree on this pointer at the end of scope
    CAutoComPointer ptrAutoCleanup(pwszExposed);

    ft.WriteLine(L"- Shadow copy exposed as '%s'", pwszExposed);
}



// Expose the given shadow copy set as a share
void VssClient::ExposeSnapshotRemotely(VSS_ID snapshotID, wstring shareName, wstring pathFromRoot)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"- Exposing shadow copy " WSTR_GUID_FMT L" under the share '%s' (path from root: '%s')", 
        GUID_PRINTF_ARG(snapshotID), shareName.c_str(), pathFromRoot.c_str());

    // Make sure that the expose operation is valid for this snapshot.
    // Get the shadow copy properties
    VSS_SNAPSHOT_PROP Snap;
    HRESULT hr = m_pVssObject->GetSnapshotProperties(snapshotID, &Snap);
    if (hr == VSS_E_OBJECT_NOT_FOUND)
    {
        ft.WriteLine(L"\nERROR: there is no snapshot with the given ID");
        throw(E_INVALIDARG);
    }

    // Automatically call VssFreeSnapshotProperties on this structure at the end of scope
    CAutoSnapPointer snapAutoCleanup(&Snap);

    // Client Accessible snapshots are not exposable
    if (Snap.m_lSnapshotAttributes & VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE)
    {
        ft.WriteLine(L"\nERROR: the snapshot ID identifies a Client Accessible snapshot which cannot be exposed");
        throw(E_INVALIDARG);
    }

    // Client Accessible snapshots are not exposable
    if ((Snap.m_pwszExposedName != NULL) || (Snap.m_pwszExposedPath != NULL))
    {
        ft.WriteLine(L"\nERROR: Client-accessible (SFSF) snapshots cannot be exposed.");
        throw(E_INVALIDARG);
    }

    // Note: a true reqestor should also check here if 
    // - the remote share name is valid (i.e. unused)
    // - the path from root is valid

    // The path from root represents where to put the share on the snapshot volume.
    // If the share is on root, then use a NULL argument
    LPWSTR pwszPathFromRoot = NULL;
    if (pathFromRoot.length() != 0)
        pwszPathFromRoot = (VSS_PWSZ)pathFromRoot.c_str();

    // Expose locally the shadow copy set
    LPWSTR pwszExposed = NULL;
    CHECK_COM(m_pVssObject->ExposeSnapshot(snapshotID, 
        pwszPathFromRoot, 
        VSS_VOLSNAP_ATTR_EXPOSED_REMOTELY, 
        (VSS_PWSZ)shareName.c_str(), 
        &pwszExposed));

    // Automatically call CoTaskMemFree on this pointer at the end of scope
    CAutoComPointer ptrAutoCleanup(pwszExposed);

    ft.WriteLine(L"- Shadow copy exposed as '%s'", pwszExposed);
}



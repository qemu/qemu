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


#pragma once



/////////////////////////////////////////////////////////////////////////
//  Utility classes 
//

// Used to automatically release a CoTaskMemAlloc allocated pointer when 
// when the instance of this class goes out of scope
// (even if an exception is thrown)
class CAutoComPointer
{
public:
    CAutoComPointer(LPVOID ptr): m_ptr(ptr) {};
    ~CAutoComPointer() { CoTaskMemFree(m_ptr); }
private:
    LPVOID m_ptr;
    
};


// Used to automatically release the contents of a VSS_SNAPSHOT_PROP structure 
// (but not the structure itself)
// when the instance of this class goes out of scope
// (even if an exception is thrown)
class CAutoSnapPointer
{
public:
    CAutoSnapPointer(VSS_SNAPSHOT_PROP * ptr): m_ptr(ptr) {};
    ~CAutoSnapPointer() { ::VssFreeSnapshotProperties(m_ptr); }
private:
    VSS_SNAPSHOT_PROP * m_ptr;
};


// Used to automatically release the given handle
// when the instance of this class goes out of scope
// (even if an exception is thrown)
class CAutoHandle
{
public:
    CAutoHandle(HANDLE h): m_h(h) {};
    ~CAutoHandle() { ::CloseHandle(m_h); }
private:
    HANDLE m_h;
};


// Used to automatically release the given handle
// when the instance of this class goes out of scope
// (even if an exception is thrown)
class CAutoSearchHandle
{
public:
    CAutoSearchHandle(HANDLE h): m_h(h) {};
    ~CAutoSearchHandle() { ::FindClose(m_h); }
private:
    HANDLE m_h;
};



//
//  Wrapper class to convert a wstring to/from a temporary WCHAR
//  buffer to be used as an in/out parameter in Win32 APIs
//
class WString2Buffer
{
public:

    WString2Buffer(wstring & s): 
        m_s(s), m_sv(s.length() + 1, L'\0')
    {
        // Move data from wstring to the temporary vector
        std::copy(m_s.begin(), m_s.end(), m_sv.begin());
    }

    ~WString2Buffer()
    {
        // Move data from the temporary vector to the string
        m_sv.resize(wcslen(&m_sv[0]));
        m_s.assign(m_sv.begin(), m_sv.end());
    }

    // Return the temporary WCHAR buffer 
    operator WCHAR* () { return &(m_sv[0]); }

    // Return the available size of the temporary WCHAR buffer 
    size_t length() { return m_s.length(); }

private: 
    wstring &       m_s;
    vector<WCHAR>   m_sv;
};





/////////////////////////////////////////////////////////////////////////
//  String-related utility functions
//


// Converts a wstring to a string class
inline string WString2String(wstring src)
{
    vector<CHAR> chBuffer;
    int iChars = WideCharToMultiByte(CP_ACP, 0, src.c_str(), -1, NULL, 0, NULL, NULL);
    if (iChars > 0)
    {
        chBuffer.resize(iChars);
        WideCharToMultiByte(CP_ACP, 0, src.c_str(), -1, &chBuffer.front(), (int)chBuffer.size(), NULL, NULL);
    }
    
    return std::string(&chBuffer.front());
}


// Converts a wstring into a GUID
inline GUID & WString2Guid(wstring src)
{
    FunctionTracer ft(DBG_INFO);

    // Check if this is a GUID
    static GUID result;
    HRESULT hr = ::CLSIDFromString(W2OLE(const_cast<WCHAR*>(src.c_str())), &result);
    if (FAILED(hr))
    {
        ft.WriteLine(L"ERROR: The string '%s' is not formatted as a GUID!", src.c_str());
        throw(E_INVALIDARG);
    }

    return result;
}


// Splits a string into a list of substrings separated by the given character
inline vector<wstring> SplitWString(wstring str, WCHAR separator)
{
    FunctionTracer ft(DBG_INFO);

    vector<wstring> strings;

    wstring remainder = str;
    while(true)
    {
        size_t position = remainder.find(separator);
        if (position == wstring::npos)
        {
            // Add the last string
            strings.push_back(remainder);
            break;
        }

        wstring token = remainder.substr(0, position);
        ft.Trace(DBG_INFO, L"Extracting token: '%s' from '%s' between 0..%d", 
            token.c_str(), remainder.c_str(), position);

        // Add this substring and continue with the rest
        strings.push_back(token);
        remainder = remainder.substr(position + 1);
    }

    return strings;
}


// Converts a GUID to a wstring
inline wstring Guid2WString(GUID guid)
{
    FunctionTracer ft(DBG_INFO);

    wstring guidString(100, L'\0');
    CHECK_COM(StringCchPrintfW(WString2Buffer(guidString), guidString.length(), WSTR_GUID_FMT, GUID_PRINTF_ARG(guid)));

    return guidString;
}


// Convert the given BSTR (potentially NULL) into a valid wstring
inline wstring BSTR2WString(BSTR bstr)
{
    return (bstr == NULL)? wstring(L""): wstring(bstr);
}



// Case insensitive comparison
inline bool IsEqual(wstring str1, wstring str2)
{
    return (_wcsicmp(str1.c_str(), str2.c_str()) == 0);
}


// Returns TRUE if the string is already present in the string list  
// (performs case insensitive comparison)
inline bool FindStringInList(wstring str, vector<wstring> stringList)
{
    // Check to see if the volume is already added 
    for (unsigned i = 0; i < stringList.size( ); i++)
        if (IsEqual(str, stringList[i]))
            return true;

    return false;
}


// Append a backslash to the current string 
inline wstring AppendBackslash(wstring str)
{
    if (str.length() == 0)
        return wstring(L"\\");
    if (str[str.length() - 1] == L'\\')
        return str;
    return str.append(L"\\");
}


/////////////////////////////////////////////////////////////////////////
//  Volume, File -related utility functions
//


// Returns TRUE if this is a real volume (for eample C:\ or C:)
// - The backslash termination is optional
inline bool IsVolume(wstring volumePath)
{
    FunctionTracer ft(DBG_INFO);

    ft.Trace(DBG_INFO, L"Checking if %s is a real volume path...", volumePath.c_str());
    _ASSERTE(volumePath.length() > 0);
    
    // If the last character is not '\\', append one
    volumePath = AppendBackslash(volumePath);

    // Get the volume name
    wstring volumeName(MAX_PATH, L'\0');
    if (!GetVolumeNameForVolumeMountPoint( volumePath.c_str(), WString2Buffer(volumeName), (DWORD)volumeName.length()))
    {
        ft.Trace(DBG_INFO, L"GetVolumeNameForVolumeMountPoint(%s) failed with %d", volumePath, GetLastError());
        return false;
    }

    return true;
}
    



// Get the unique volume name for the given mount point
inline wstring GetUniqueVolumeNameForMountPoint(wstring mountPoint)
{
    FunctionTracer ft(DBG_INFO);

    _ASSERTE(mountPoint.length() > 0);

    ft.Trace(DBG_INFO, L"- Get volume name for %s ...", mountPoint.c_str());

    // Add the backslash termination, if needed
    mountPoint = AppendBackslash(mountPoint);

    // Get the volume name alias (might be different from the unique volume name in rare cases)
    wstring volumeName(MAX_PATH, L'\0');
    CHECK_WIN32(GetVolumeNameForVolumeMountPointW((LPCWSTR)mountPoint.c_str(), WString2Buffer(volumeName), (DWORD)volumeName.length()));
    ft.Trace(DBG_INFO, L"- Volume name for mount point: %s ...", volumeName.c_str());

    // Get the unique volume name
    wstring volumeUniqueName(MAX_PATH, L'\0');
    CHECK_WIN32(GetVolumeNameForVolumeMountPointW((LPCWSTR)volumeName.c_str(), WString2Buffer(volumeUniqueName), (DWORD)volumeUniqueName.length()));
    ft.Trace(DBG_INFO, L"- Unique volume name: %s ...", volumeUniqueName.c_str());

    return volumeUniqueName;
}




// Get the unique volume name for the given path
inline wstring GetUniqueVolumeNameForPath(wstring path)
{
    FunctionTracer ft(DBG_INFO);

    _ASSERTE(path.length() > 0);

    ft.Trace(DBG_INFO, L"- Get volume path name for %s ...", path.c_str());

    // Add the backslash termination, if needed
    path = AppendBackslash(path);

    // Get the root path of the volume
    wstring volumeRootPath(MAX_PATH, L'\0');
    CHECK_WIN32(GetVolumePathNameW((LPCWSTR)path.c_str(), WString2Buffer(volumeRootPath), (DWORD)volumeRootPath.length()));
    ft.Trace(DBG_INFO, L"- Path name: %s ...", volumeRootPath.c_str());

    // Get the volume name alias (might be different from the unique volume name in rare cases)
    wstring volumeName(MAX_PATH, L'\0');
    CHECK_WIN32(GetVolumeNameForVolumeMountPointW((LPCWSTR)volumeRootPath.c_str(), WString2Buffer(volumeName), (DWORD)volumeName.length()));
    ft.Trace(DBG_INFO, L"- Volume name for path: %s ...", volumeName.c_str());

    // Get the unique volume name
    wstring volumeUniqueName(MAX_PATH, L'\0');
    CHECK_WIN32(GetVolumeNameForVolumeMountPointW((LPCWSTR)volumeName.c_str(), WString2Buffer(volumeUniqueName), (DWORD)volumeUniqueName.length()));
    ft.Trace(DBG_INFO, L"- Unique volume name: %s ...", volumeUniqueName.c_str());

    return volumeUniqueName;
}



// Get the Win32 device for the volume name
inline wstring GetDeviceForVolumeName(wstring volumeName)
{
    FunctionTracer ft(DBG_INFO);

    ft.Trace(DBG_INFO, L"- GetDeviceForVolumeName for '%s' ... ", volumeName.c_str());

    // The input parameter is a valid volume name
    _ASSERTE(wcslen(volumeName.c_str()) > 0);

    // Eliminate the last backslash, if present
    if (volumeName[wcslen(volumeName.c_str()) - 1] == L'\\')
        volumeName[wcslen(volumeName.c_str()) - 1] = L'\0';

    // Eliminate the GLOBALROOT prefix if present
    wstring globalRootPrefix = L"\\\\?\\GLOBALROOT";
    if (IsEqual(volumeName.substr(0,globalRootPrefix.size()), globalRootPrefix))
    {
        wstring kernelDevice = volumeName.substr(globalRootPrefix.size()); 
        ft.Trace(DBG_INFO, L"- GLOBALROOT prefix eliminated. Returning kernel device:  '%s' ", kernelDevice.c_str());

        return kernelDevice;
    }

    // If this is a volume name, get the device 
    wstring dosPrefix = L"\\\\?\\";
    wstring volumePrefix = L"\\\\?\\Volume";
    if (IsEqual(volumeName.substr(0,volumePrefix.size()), volumePrefix))
    {
        // Isolate the DOS device for the volume name (in the format Volume{GUID})
        wstring dosDevice = volumeName.substr(dosPrefix.size());
        ft.Trace(DBG_INFO, L"- DOS device for '%s' is '%s'", volumeName.c_str(), dosDevice.c_str() );

        // Get the real device underneath
        wstring kernelDevice(MAX_PATH, L'\0');
        CHECK_WIN32(QueryDosDevice((LPCWSTR)dosDevice.c_str(), WString2Buffer(kernelDevice), (DWORD)kernelDevice.size()));
        ft.Trace(DBG_INFO, L"- Kernel device for '%s' is '%s'", volumeName.c_str(), kernelDevice.c_str() );

        return kernelDevice;
    }

    return volumeName;
}



// Get the displayable root path for the given volume name
inline wstring GetDisplayNameForVolume(wstring volumeName)
{
    FunctionTracer ft(DBG_INFO);

    DWORD dwRequired = 0;
    wstring volumeMountPoints(MAX_PATH, L'\0');
    if (!GetVolumePathNamesForVolumeName((LPCWSTR)volumeName.c_str(), 
            WString2Buffer(volumeMountPoints), 
            (DWORD)volumeMountPoints.length(), 
            &dwRequired))
    {
            // If not enough, retry with a larger size
            volumeMountPoints.resize(dwRequired, L'\0');
            CHECK_WIN32(!GetVolumePathNamesForVolumeName((LPCWSTR)volumeName.c_str(), 
                WString2Buffer(volumeMountPoints), 
                (DWORD)volumeMountPoints.length(), 
                &dwRequired));
    }

    // compute the smallest mount point by enumerating the returned MULTI_SZ
    wstring mountPoint = volumeMountPoints;
    for(LPWSTR pwszString = (LPWSTR)volumeMountPoints.c_str(); pwszString[0]; pwszString += wcslen(pwszString) + 1)
        if (mountPoint.length() > wcslen(pwszString))
            mountPoint = pwszString;

    return mountPoint;
}


// Utility function to read the contents of a file
inline wstring ReadFileContents(wstring fileName)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Reading the file '%s' ...", fileName.c_str());

    HANDLE hFile = CreateFile((LPWSTR)fileName.c_str(),
                          GENERIC_READ,
                          FILE_SHARE_READ,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        CHECK_WIN32_ERROR(GetLastError(), L"CreateFile");

    // Will automatically call CloseHandle at the end of scope
    // (even if an exception is thrown)
    CAutoHandle autoCleanupHandle(hFile);

    // Allocate the read buffer
    DWORD dwFileSize = GetFileSize(hFile, 0);
    wstring contents(dwFileSize / sizeof(WCHAR), L'\0');

    // Read the file contents
    DWORD dwRead;
    CHECK_WIN32(ReadFile(hFile, WString2Buffer(contents), dwFileSize, &dwRead, NULL));

    return contents;
}


// Utility function to write a new file 
inline void WriteFile(wstring fileName, wstring contents)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Writing the file '%s' ...", fileName.c_str());

    HANDLE hFile = CreateFile((LPWSTR)fileName.c_str(),
                          GENERIC_WRITE,
                          FILE_SHARE_READ|FILE_SHARE_WRITE,
                          NULL,
                          CREATE_ALWAYS,
                          0,
                          NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        CHECK_WIN32_ERROR(GetLastError(), L"CreateFile");

    // Will automatically call CloseHandle at the end of scope
    // (even if an exception is thrown)
    CAutoHandle autoCleanupHandle(hFile);

    // Write the file contents
    DWORD dwWritten;
    DWORD cbWrite = (DWORD)((contents.length() + 1) * sizeof(WCHAR));
    CHECK_WIN32(WriteFile(hFile, (LPWSTR)contents.c_str(), cbWrite, &dwWritten, NULL));
}


// Execute a command
inline void ExecCommand(wstring execCommand)
{
    FunctionTracer ft(DBG_INFO);

    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &pi, sizeof(pi) );

    ft.WriteLine(L"- Executing command '%s' ...", execCommand.c_str());
    ft.WriteLine(L"-----------------------------------------------------");

    //
    // Security Remarks - CreateProcess
    // 
    // The first parameter, lpApplicationName, can be NULL, in which case the 
    // executable name must be in the white space-delimited string pointed to 
    // by lpCommandLine. If the executable or path name has a space in it, there 
    // is a risk that a different executable could be run because of the way the 
    // function parses spaces. The following example is dangerous because the 
    // function will attempt to run "Program.exe", if it exists, instead of "MyApp.exe".
    // 
    // CreateProcess(NULL, "C:\\Program Files\\MyApp", ...)
    // 
    // If a malicious user were to create an application called "Program.exe" 
    // on a system, any program that incorrectly calls CreateProcess 
    // using the Program Files directory will run this application 
    // instead of the intended application.
    // 
    // For this reason we blocked parameters in the executed command.
    //

    // Prepend/append the command with double-quotes. This will prevent adding parameters
    execCommand = wstring(L"\"") + execCommand + wstring(L"\"");

    // Start the child process. 
    CHECK_WIN32( CreateProcess( NULL, // No module name (use command line). 
        (LPWSTR)execCommand.c_str(), // Command line. 
        NULL,             // Process handle not inheritable. 
        NULL,             // Thread handle not inheritable. 
        FALSE,            // Set handle inheritance to FALSE. 
        0,                // No creation flags. 
        NULL,             // Use parent's environment block. 
        NULL,             // Use parent's starting directory. 
        &si,              // Pointer to STARTUPINFO structure.
        &pi ))             // Pointer to PROCESS_INFORMATION structure.

    // Close process and thread handles automatically when we wil leave this function
    CAutoHandle autoCleanupHandleProcess(pi.hProcess);
    CAutoHandle autoCleanupHandleThread(pi.hThread);

    // Wait until child process exits.
    CHECK_WIN32( WaitForSingleObject( pi.hProcess, INFINITE ) == WAIT_OBJECT_0);
    ft.WriteLine(L"-----------------------------------------------------");

    // Checking the exit code
    DWORD dwExitCode = 0;
    CHECK_WIN32( GetExitCodeProcess( pi.hProcess, &dwExitCode ) );
    if (dwExitCode != 0)
    {
        ft.WriteLine(L"ERROR: Command line '%s' failed!. Aborting the backup...", execCommand.c_str());
        ft.WriteLine(L"- Returned error code: %d", dwExitCode);
        throw(E_UNEXPECTED);
    }
}

inline wstring VssTimeToString(VSS_TIMESTAMP& vssTime)
{
    wstring stringDateTime;
    SYSTEMTIME stLocal = {0};
    FILETIME ftLocal = {0};

    //  Compensate for local TZ
    ::FileTimeToLocalFileTime( (FILETIME *)(&vssTime), &ftLocal );

    //  Finally convert it to system time
    ::FileTimeToSystemTime( &ftLocal, &stLocal );

    WCHAR pwszDate[64];
    WCHAR pwszTime[64];
    //  Convert timestamp to a date string
    ::GetDateFormatW( GetThreadLocale( ),
                      DATE_SHORTDATE,
                      &stLocal,
                      NULL,
                      pwszDate,
                      sizeof( pwszDate ) / sizeof( pwszDate[0] ));

    //  Convert timestamp to a time string
    ::GetTimeFormatW( GetThreadLocale( ),
                      0,
                      &stLocal,
                      NULL,
                      pwszTime,
                      sizeof( pwszTime ) / sizeof( pwszTime[0] ));

    stringDateTime = pwszDate;
    stringDateTime += L" ";
    stringDateTime += pwszTime;

    return stringDateTime;
}
/*--

Copyright (C) Microsoft Corporation, 2003

Module Name:

    Utility.h

Abstract:

    Declarations of various utility functions.

Notes:

Revision History:

--*/

#pragma once

#include "stdafx.h"

#define NELEMENTS(x) (sizeof (x) / sizeof (*(x)))

// Overload '<' operator on GUIDs. This is required for defining 
// map<GUID, IUnknown *> type.

inline bool operator<(const GUID &rguid1, const GUID &rguid2) {
    return (memcmp((void *)&rguid1, (void *)&rguid2, sizeof(GUID)) < 0);
}

template<class T> inline void
SAFE_DELETE(
    T& x
    )
{
    if (x != NULL) {
        delete x;
        x = NULL;
    }
}

template<class T> inline void
SAFE_RELEASE(
    T& x
    )
{
    if (x != NULL) {
        x->Release();
        x = NULL;
    }
}

template<class T> inline void
SAFE_COFREE(
    T& x
    )
{
    //
    // CoTaskMemFree allows NULL arguments, so
    // we don't need protection for this case.
    //
    CoTaskMemFree( x );
    x = NULL;
}

inline void
SAFE_FREESID(
    PSID& x
    )
{
    if (x != NULL) {
        FreeSid( x );
        x = NULL;
    }
}

inline void
SAFE_CLOSE(
    HANDLE& x
    )
{
    if (x != INVALID_HANDLE_VALUE) {
        CloseHandle( x );
        x = INVALID_HANDLE_VALUE;
    }
}

inline void
THROW_ON_FAILED(
    const char* strMessage,
    HRESULT hr
    )
{
    if (FAILED(hr)) { 
        throw hr; 
    }
}

class AutoLock {
 public:
    AutoLock( CRITICAL_SECTION& cs ) : m_pLock( &cs ) {
        EnterCriticalSection( m_pLock );
    };

    ~AutoLock() {
        Unlock();
    };

    void Unlock() {
        if (m_pLock != NULL) {
            LeaveCriticalSection( m_pLock );
            m_pLock = NULL;
        }
    };
    
 private:
    CRITICAL_SECTION* m_pLock;
};

//
// Utility functions
//

LPSTR
NewString(
    LPCSTR pszSource
    );

LPWSTR
NewString(
    LPCWSTR pwszSource
    );

LPWSTR
NewString(
    std::wstring& wsSrc
    );

std::string
GuidToString(
    GUID& guid
    );

std::wstring
GuidToWString(
    GUID& guid
    );

GUID
WStringToGuid(
    std::wstring& value
    );

_int64
WStringToInt64(
    std::wstring& value
    );

std::wstring
Int64ToWString(
    __int64 value
    );

HRESULT
UnicodeToAnsi(
    LPCWSTR pwszIn,
    LPSTR&  pszOut
    );

HRESULT
AnsiToUnicode(
    LPCSTR pszIn,
    LPWSTR& pwszOut
    );

HRESULT
AnsiToGuid(
    LPCSTR szString,
    GUID& guid
    );

LPSTR
GuidToAnsi(
    GUID& guid
    );

HRESULT
GetEnvVar(
    std::wstring& var,
    std::wstring& value
    );

//
// Tracing
//

void
TraceMsg(
    LPCWSTR msg,
    ...
    );

class FuncTrace {
 public:
    FuncTrace(LPCWSTR function)
     : m_function(function) {
        TraceMsg(L"Entering: %s\n", m_function);
    }

    ~FuncTrace() {
        TraceMsg(L"Exiting: %s\n", m_function);
    }

 private:
    LPCWSTR m_function;
};

#define TRACE_FUNCTION() FuncTrace FuncTraceLocal(TEXT(__FUNCTION__))

void
LogEvent(
    LPCWSTR pFormat,
    ...
    );

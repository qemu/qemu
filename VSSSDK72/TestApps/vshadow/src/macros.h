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
//  utility macros
//



#define GEN_EVAL(X) X
#define GEN_STRINGIZE_ARG(X) #X
#define GEN_STRINGIZE(X) GEN_EVAL(GEN_STRINGIZE_ARG(X))
#define GEN_MERGE(A, B) A##B
#define GEN_MAKE_W(A) GEN_MERGE(L, A)

#define GEN_WSTRINGIZE(X) GEN_MAKE_W(GEN_STRINGIZE_ARG(X))

#define __WFILE__ GEN_MAKE_W(GEN_EVAL(__FILE__))
#define __WFUNCTION__ GEN_MAKE_W(GEN_EVAL(__FUNCTION__))




// Helper macros to print a GUID using printf-style formatting
#define WSTR_GUID_FMT  L"{%.8x-%.4x-%.4x-%.2x%.2x-%.2x%.2x%.2x%.2x%.2x%.2x}"

#define GUID_PRINTF_ARG( X )                                \
    (X).Data1,                                              \
    (X).Data2,                                              \
    (X).Data3,                                              \
    (X).Data4[0], (X).Data4[1], (X).Data4[2], (X).Data4[3], \
    (X).Data4[4], (X).Data4[5], (X).Data4[6], (X).Data4[7]


// Helper macro for quick treatment of case statements for error codes
#define CHECK_CASE_FOR_CONSTANT(value)                      \
    case value: return wstring(GEN_MAKE_W(#value));


#define BOOL2TXT(b) ((b)? L"TRUE": L"FALSE")




// Max buffer size for logging/tracing 
const int MAX_VPRINTF_BUFFER_SIZE = 4096;


// Generic macro for variable parameter expansion
// Safely truncate the expansion if the buffer is not big enough
#define VPRINTF_VAR_PARAMS(buffer,param)            \
{                                                   \
    buffer.resize(MAX_VPRINTF_BUFFER_SIZE, L'\0');  \
    va_list marker;                                 \
    va_start( marker, param );                      \
    HRESULT hr = StringCchVPrintfW(                 \
        WString2Buffer(buffer),                     \
        buffer.length(),                            \
        param.c_str(),                              \
        marker );                                   \
    if (FAILED(hr)                                  \
        && (hr != STRSAFE_E_INSUFFICIENT_BUFFER))   \
        throw(hr);                                  \
    va_end( marker );                               \
}


// Macro that express the position in the source file (provided for better tracing)
#define DBG_INFO        __WFILE__ , __LINE__, __WFUNCTION__



//
//  Very simple ASSERT definition
//

#ifdef _DEBUG
    #define _ASSERTE(x) {                               \
        if (!(x))                                       \
        {                                           \
            wprintf(L"\nASSERTION FAILED: %S\n", #x);       \
            wprintf(L"- File: %s\n- Line: %d\n- Function: %s\n", DBG_INFO); \
            wprintf(L"\nPress <ENTER> to continue...");     \
            getchar();                              \
        }                                           \
    }
#else
    #define _ASSERTE(x)
#endif



/////////////////////////////////////////////////////////////////////////////
//  Error-treatment macros
//

//
//  Macro to execute a COM API and to test its result. 
//  - The macro will execute the call given in its parameter
//  - The macro will examine the returned HRESULT
//  - If an failed HRESULT is returned, an explanatory text is written to the console 
//  and an error is thrown. VSHADOW.EXE will then terminate
//
//  Example 
//  - API: HRESULT CoCreateInstance()
//  - Sample Usage: CHECK_COM( CoCreateInstance() )
//
#define CHECK_COM( Call ) CHECK_COM_ERROR( Call, #Call )


#define CHECK_COM_ERROR( ErrorCode, Text )                                                  \
{                                                                                           \
    ft.Trace(DBG_INFO, L"Executing COM call '%s'", GEN_WSTRINGIZE(Text));                   \
    HRESULT hrInternal = ErrorCode;                                                         \
    if (FAILED(hrInternal))                                                                 \
    {                                                                                       \
        ft.WriteLine(L"\nERROR: COM call %s failed.", GEN_WSTRINGIZE(Text));                \
        ft.WriteLine(L"- Returned HRESULT = 0x%08lx", hrInternal);                          \
        ft.WriteLine(L"- Error text: %s", FunctionTracer::HResult2String(hrInternal).c_str());      \
        ft.WriteLine(L"- Please re-run VSHADOW.EXE with the /tracing option to get more details");\
        throw(hrInternal);                                                                  \
    }                                                                                       \
}


//
//  Macro to execute a Win32 API and to test its result. 
//  - The macro will execute the call given in its parameter
//  - The macro will examine the returned BOOL (we assume that the API returns a BOOL. 
//  - If FALSE is returned, an explanatory text is written to the console 
//  and an error is thrown. VSHADOW.EXE will then terminate.
//
#define CHECK_WIN32( Call )                                                                 \
{                                                                                           \
    BOOL bRes = Call;                                                                       \
    if (!bRes)                                                                              \
        CHECK_WIN32_ERROR(GetLastError(), #Call);                                           \
}


#define CHECK_WIN32_ERROR( ErrorCode, Text )                                                \
{                                                                                           \
    ft.Trace(DBG_INFO, L"Executing Win32 call '%s'", GEN_WSTRINGIZE(Text));             \
    DWORD dwLastError = ErrorCode;                                                          \
    HRESULT hrInternal = HRESULT_FROM_WIN32(dwLastError);                                   \
    if (dwLastError != NOERROR)                                                             \
    {                                                                                       \
        ft.WriteLine(L"\nERROR: Win32 call %s failed.", GEN_WSTRINGIZE(Text));          \
        ft.WriteLine(L"- GetLastError() == %ld", dwLastError);                              \
        ft.WriteLine(L"- Error text: %s", FunctionTracer::HResult2String(hrInternal).c_str());      \
        ft.WriteLine(L"- Please re-run VSHADOW.EXE with the /tracing option to get more details");\
        throw(hrInternal);                                                                  \
    }                                                                                       \
}



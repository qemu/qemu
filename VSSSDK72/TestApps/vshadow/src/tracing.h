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
//  Generic tracing/logger class
//


// Very simple tracing/logging class 
class FunctionTracer
{
public:
    FunctionTracer(wstring fileName, INT lineNumber, wstring functionName);
    ~FunctionTracer();
    
    // tracing routine
    void Trace(wstring file, int line, wstring functionName, wstring format, ...);
    
    // console logging routine
    void WriteLine(wstring format, ...);
    
    // Converts a HRESULT into a printable message
    static wstring  HResult2String(HRESULT hrError);

    // Enables tracing
    static void EnableTracingMode();

private:

    //
    //  Data members
    //

    static bool m_traceEnabled;

    wstring     m_fileName;
    int         m_lineNumber;
    wstring     m_functionName;

};


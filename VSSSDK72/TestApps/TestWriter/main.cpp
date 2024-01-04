/*
**++
**
** Copyright (c) 2002  Microsoft Corporation
**
**
** Module Name:
**
**	main.cpp
**
**
** Abstract:
**
**	Test program to to register a Writer with various properties
**
** Author:
**
**	Reuven Lax      [reuvenl]       04-June-2002
**
**
** Revision History:
**
**--
*/


///////////////////////////////////////////////////////////////////////////////
// Includes

#include "stdafx.h"
#include "main.h"
#include "swriter.h"
#include "writerconfig.h"
#include <string>
#include <sstream>
#include <utility>
#include <memory>

///////////////////////////////////////////////////////////////////////////////
// Declarations

HANDLE g_quitEvent = NULL;
using Utility::checkReturn;


///////////////////////////////////////////////////////////////////////////////

extern "C" __cdecl wmain(int argc, wchar_t ** argv)
try
{
	if (argc != 2)
		throw Utility::TestWriterException(L"Invalid number of arguments\n Format: vswriter.exe <config-file>");

	HRESULT hr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED );
	checkReturn(hr, L"CoInitializeEx");
	
       hr = ::CoInitializeSecurity(
           NULL,                                 //  IN PSECURITY_DESCRIPTOR         pSecDesc,
           -1,                                  //  IN LONG                         cAuthSvc,
           NULL,                                //  IN SOLE_AUTHENTICATION_SERVICE *asAuthSvc,
           NULL,                                //  IN void                        *pReserved1,
           RPC_C_AUTHN_LEVEL_PKT_PRIVACY,       //  IN DWORD                        dwAuthnLevel,
           RPC_C_IMP_LEVEL_IDENTIFY,            //  IN DWORD                        dwImpLevel,
           NULL,                                //  IN void                        *pAuthList,
           EOAC_NONE,
                                                //  IN DWORD                        dwCapabilities,
           NULL                                 //  IN void                        *pReserved3
           );
       checkReturn(hr, L"CoInitializeSecurity");

	loadFile(argv[1]);

	g_quitEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	if (g_quitEvent == NULL)
		throw Utility::TestWriterException(L"Internal Error: could not create event\n");

	// set a control handler that allows the writer to be shut down
	if (!::SetConsoleCtrlHandler(handler, TRUE))
		checkReturn(HRESULT_FROM_WIN32(::GetLastError()), L"SetConsoleCtrlHandler");

        TestWriter::StaticInitialize();
        
	// We want the writer to go out of scope before the return statement
	{
		TestWriter writer;
		hr = writer.Initialize();
		checkReturn(hr, L"TestWriter::Initialize");

		if(::WaitForSingleObject(g_quitEvent, INFINITE) != WAIT_OBJECT_0)
			throw Utility::TestWriterException(L"internal Error: did not successfully wait on event\n");
	}
	
	return 0;	
}
catch(const std::exception& error)	
{
	Utility::printStatus(error.what(), Utility::low);
	exit(1);
}
catch(HRESULT error)	
{
	Utility::TestWriterException e(error);
	Utility::printStatus(e.what(), Utility::low);
	exit(1);
}

void loadFile(wchar_t* fileName)
{
	CXMLDocument document;
	if (!document.LoadFromFile(fileName))   {
	    if (::GetFileAttributes(fileName) == INVALID_FILE_ATTRIBUTES)
	        throw Utility::TestWriterException(L"file does not exist!");    
	    Utility::parseError(document);
	}

	CComBSTR xmlString = document.SaveAsXML();
	WriterConfiguration::instance()->loadFromXML((BSTR)xmlString);

	return;
}

BOOL WINAPI handler(DWORD dwCtrlType)
{
	// only print to console if it's safe
	if ((dwCtrlType == CTRL_C_EVENT) ||
	     (dwCtrlType == CTRL_BREAK_EVENT))
		Utility::printStatus(L"Terminating writer", Utility::low);
	
	// we want to quit independent of what the control event was
	::SetEvent(g_quitEvent);

	return TRUE;
}

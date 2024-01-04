/*
**++
**
** Copyright (c) 2002  Microsoft Corporation
**
**
** Module Name:
**
**	    utility.cpp
**
**
** Abstract:
**
**	defines functions and variable used by the Test writer
**
** Author:
**
**	Reuven Lax      [reuvenl]       04-June-2002
**
**
**
** Revision History:
**
**--
*/

////////////////////////////////////////////////////////////////////////
// Includes

#include "stdafx.h"
#include "utility.h"
#include "writerconfig.h"
#include "vs_xml.hxx"
#include "msxml2.h"
#include <string>
#include <sstream>
using std::wstring;

void Utility::missingAttribute(const wchar_t* name)
{
	wstring thrown(L"The attribute ");
	thrown += name;
	thrown += L" was omitted from the XML document";
	
	throw Utility::TestWriterException(thrown);
}

void Utility::missingElement(const wchar_t* name)
{
	wstring thrown(L"The  element ");
	thrown += name;
	thrown += L" was omitted from the XML document";
	throw Utility::TestWriterException(thrown);
}

void Utility::checkReturn(HRESULT returnCode, wstring function)
{
	if (FAILED(returnCode))	
		throw Utility::TestWriterException(returnCode, function);
}

void Utility::warnReturn(HRESULT returnCode, wstring function)
{
	if (FAILED(returnCode))	{
		Utility::TestWriterException ex(returnCode, function);
		printf("%s\n", ex.what());
	}
}

void Utility::parseError(const CXMLDocument& doc)
{
	CComBSTR reason, text;
	CComPtr<IXMLDOMParseError> parseError;
	HRESULT hr = doc.GetInterface()->get_parseError(&parseError);
	checkReturn(hr, L"IXMLDOMDocument::get_parseError");

	wstring thrown;
	if (parseError != NULL)	{
		hr = parseError->get_reason(&reason);
		checkReturn(hr, L"IXMLDOMParseError::get_reason");
		hr = parseError->get_srcText(&text);
		checkReturn(hr, L"IXMLDOMParseError::get_srcText");

		thrown = L"Failed to load configuration file:\n";
		thrown += L" Reason: " ;
		thrown += reason;
		thrown += L"\n Source Text:\n  " ;
		thrown += text;
	}	else		{
		thrown = L"Failed to load configuration file.";
	}

	throw Utility::TestWriterException(thrown);	
}

// this function better not throw exceptions
void Utility::printStatus(const wstring& status, Utility::Verbosity level)
try
{
	// if level == low, then we may be in exception-handling code.  Don't dare use
	// the configuration object in that case
	WriterConfiguration* config = WriterConfiguration::instance();
	if (level == Utility::low || level <= config->verbosity())
		wprintf(L"%s\n", status.c_str());
}
catch(const std::exception&)
{
	wprintf(L"Internal Error: an unexpected error happened in printStatus\n");
	wprintf(L"We were trying to print the following message: %s\n", status.c_str());
}

void Utility::printStatus(const std::string& status, Verbosity level)
try
{
	// if level == low, then we may be in exception-handling code.  Don't dare use
	// the configuration object in that case
	WriterConfiguration* config = WriterConfiguration::instance();
	if (level == Utility::low || level <= config->verbosity())
		printf("%s\n", status.c_str());
}
catch(const std::exception& exception)	
{
	printf("Internal Error: an unexpected error happened in printStatus\n");
	printf("the error is the following\n\t%s\n", exception.what());
	printf("We were trying to print the following message: %s\n", status.c_str());	
}

bool Utility::toBoolean(const wchar_t* name)
{
	assert(!wcscmp(name, L"yes") || !wcscmp(name, L"no"));
	return (wcscmp(name, L"yes") == 0) ? true : false;
}

VSS_USAGE_TYPE Utility::toUsage(const wchar_t* name)
{
	if (wcscmp(name, L"BOOTABLE_SYSTEM_STATE") == 0)
		return VSS_UT_BOOTABLESYSTEMSTATE;
	else if (wcscmp(name, L"SYSTEM_SERVICE") == 0)
		return VSS_UT_SYSTEMSERVICE;
	else if (wcscmp(name, L"USER_DATA") == 0)
		return VSS_UT_USERDATA;
	else if (wcscmp(name, L"OTHER") == 0)		
		return VSS_UT_OTHER;
	else
		assert(false);

	return VSS_UT_UNDEFINED;
}

VSS_RESTOREMETHOD_ENUM Utility::toMethod(const wchar_t* name)
{
	if (wcscmp(name, L"RESTORE_IF_NONE_THERE") == 0)
		return VSS_RME_RESTORE_IF_NOT_THERE;
	else if (wcscmp(name, L"RESTORE_IF_CAN_BE_REPLACED") == 0)
		return VSS_RME_RESTORE_IF_CAN_REPLACE;
	else if (wcscmp(name, L"STOP_RESTART_SERVICE") == 0)
		return VSS_RME_STOP_RESTORE_START;
	else if (wcscmp(name, L"REPLACE_AT_REBOOT") == 0)
		return VSS_RME_RESTORE_AT_REBOOT;
	else if (wcscmp(name, L"REPLACE_AT_REBOOT_IF_CANNOT_REPLACE") == 0)
	        return VSS_RME_RESTORE_AT_REBOOT_IF_CANNOT_REPLACE;
	else if (wcscmp(name, L"RESTORE_TO_ALTERNATE_LOCATION") == 0)
		return VSS_RME_RESTORE_TO_ALTERNATE_LOCATION;
	else if (wcscmp(name, L"CUSTOM") == 0)
		return VSS_RME_CUSTOM;
	else
		assert(false);

	return VSS_RME_RESTORE_AT_REBOOT;
}

VSS_WRITERRESTORE_ENUM Utility::toWriterRestore(const wchar_t* name)
{
	if (wcscmp(name, L"always") == 0)
		return VSS_WRE_ALWAYS;
	else if (wcscmp(name, L"never") == 0)
		return VSS_WRE_NEVER;
	else if (wcscmp(name, L"ifReplaceFails") == 0)
		return VSS_WRE_IF_REPLACE_FAILS;
	else
		assert(false);

	return VSS_WRE_UNDEFINED;
}

VSS_COMPONENT_TYPE Utility::toComponentType(const wchar_t* name)
{
	if (wcscmp(name, L"database") == 0)
		return VSS_CT_DATABASE;
	else if (wcscmp(name, L"filegroup") == 0)
		return VSS_CT_FILEGROUP;
	else
		assert(false);

	return VSS_CT_UNDEFINED;
}

VSS_RESTORE_TARGET Utility::toRestoreTarget(const wchar_t* name)
{
	if (wcscmp(name, L"VSS_RT_ORIGINAL") == 0)	
		return VSS_RT_ORIGINAL;
	else if (wcscmp(name, L"VSS_RT_ALTERNATE") == 0)
		return VSS_RT_ALTERNATE;
	else
		assert(false);

	return VSS_RT_UNDEFINED;
}

Utility::Events Utility::toWriterEvent(const wchar_t* name)
{
	if (wcscmp(name, L"Identify") == 0)
		return Identify;
	else if (wcscmp(name, L"PrepareForBackup") == 0)
		return PrepareForBackup;
	else if (wcscmp(name, L"PrepareForSnapshot") == 0)
		return PrepareForSnapshot;
	else if (wcscmp(name, L"Freeze") == 0)
		return Freeze;
	else if (wcscmp(name, L"Thaw") == 0)
		return Thaw;
	else if (wcscmp(name, L"PostSnapshot") == 0)
		return PostSnapshot;
	else if (wcscmp(name, L"Abort") == 0)
		return Abort;
	else if (wcscmp(name, L"BackupComplete") == 0)
		return BackupComplete;
	else if (wcscmp(name, L"BackupShutdown") == 0)
		return BackupShutdown;
	else if (wcscmp(name, L"PreRestore") == 0)
		return PreRestore;
	else if (wcscmp(name, L"PostRestore") == 0)
		return PostRestore;
	else 
		assert(false);

	return Identify;
}

Utility::Verbosity Utility::toVerbosity(const wchar_t* name)
{
	if (wcscmp(name, L"low") == 0)
		return low;
	else if (wcscmp(name, L"medium") == 0)
		return medium;
	else if (wcscmp(name, L"high") == 0)
		return high;
	else
		assert(false);

	return low;
}

long Utility::toLong(const wchar_t* name)
{
	wchar_t* stopPointer = NULL;
	long number = wcstol(name, &stopPointer, 10);

	assert(stopPointer > name);
	
	return number;	
}

wstring Utility::toString(VSS_USAGE_TYPE usage)
{
	switch(usage)	{
		case VSS_UT_BOOTABLESYSTEMSTATE:
			return L"BOOTABLE_SYSTEM_STATE";
		case VSS_UT_SYSTEMSERVICE:
			return L"SYSTEM_SERVICE";
		case VSS_UT_USERDATA:
			return L"USER_DATA";
		case VSS_UT_OTHER:
			return L"OTHER";
		default:
			assert(false);			
	}

	return L"";
}

wstring Utility::toString(VSS_RESTOREMETHOD_ENUM method)
{
	switch (method)	{
		case VSS_RME_RESTORE_IF_NOT_THERE:
			return L"RESTORE_IF_NONE_THERE";
		case VSS_RME_RESTORE_IF_CAN_REPLACE:
			return L"RESTORE_IF_CAN_BE_REPLACED";
		case VSS_RME_STOP_RESTORE_START:
			return L"STOP_RESTART_SERVICE";
		case VSS_RME_RESTORE_AT_REBOOT:
			return L"REPLACE_AT_REBOOT";
		case VSS_RME_RESTORE_AT_REBOOT_IF_CANNOT_REPLACE:
			return L"REPLACE_AT_REBOOT_IF_CANNOT_REPLACE";
		case VSS_RME_RESTORE_TO_ALTERNATE_LOCATION:
			return L"RESTORE_TO_ALTERNATE_LOCATION";
		case VSS_RME_CUSTOM:
			return L"CUSTOM";
		default:
			assert(false);
	}

	return L"";
}

wstring Utility::toString(VSS_WRITERRESTORE_ENUM writerRestore)
{
	switch (writerRestore)	{
		case VSS_WRE_ALWAYS:
			return L"always";
		case VSS_WRE_NEVER:
			return L"never";
		case VSS_WRE_IF_REPLACE_FAILS:
			return L"ifReplaceFails";
		default:
			assert(false);
	}

	return L"";
}

wstring Utility::toString(VSS_COMPONENT_TYPE type)
{
	switch (type)	{
		case VSS_CT_DATABASE:
			return L"database";
		case VSS_CT_FILEGROUP:
			return L"filegroup";
		default:
			assert(false);
	}

	return L"";
}

wstring Utility::toString(VSS_RESTORE_TARGET target)
{
	switch(target)	{
		case VSS_RT_ORIGINAL:
			return L"VSS_RT_ORIGINAL";
		case VSS_RT_ALTERNATE:
			return L"VSS_RT_ALTERNATE";
		case VSS_RT_DIRECTED:
			return L"VSS_RT_DIRECTED";
		default:
			assert(false);
	}

	return L"";
}

wstring Utility::toString(Events event)
{
	switch (event)	{
		case Identify:
			return L"Identify";
		case PrepareForBackup:
			return L"PrepareForBackup";
		case PrepareForSnapshot:
			return L"PrepareForSnapshot";
		case Freeze:
			return L"Freeze";
		case Thaw:
			return  L"Thaw";
		case PostSnapshot:
			return  L"PostSnapshot";
		case Abort:
			return L"Abort";
		case BackupComplete:
			return L"BackupComplete";
		case BackupShutdown:
			return L"BackupShutdown";
		case PreRestore:
			return L"PreRestore";
		case PostRestore:
			return L"PostRestore";
		default: 
			assert(false);		
	}

	return L"";
}

wstring Utility::toString(Verbosity verbosity)
{
	switch(verbosity)	{
		case low:
			return L"low";
		case medium:
			return L"medium";
		case high:
			return L"high";
		default:
			assert(false);
	}

	return L"";
}

wstring Utility::toString(bool value)
{
	return (value) ? L"yes" : L"no";
}
	

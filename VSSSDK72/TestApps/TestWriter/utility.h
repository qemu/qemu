/*
**++
**
** Copyright (c) 2002  Microsoft Corporation
**
**
** Module Name:
**
**	    utility.h
**
**
** Abstract:
**
**	declare functions and variable used by the Test writer
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

#ifndef _UTILITY_H_
#define _UTILITY_H_

////////////////////////////////////////////////////////////////////////
// Includes

#include <functional>
#include <string>
#include <sstream>
#include "vs_xml.hxx"


////////////////////////////////////////////////////////////////////////
// Declarations

using std::wstring;

namespace Utility	{
	enum Events	{
		Identify				  = 0,
		PrepareForBackup       = Identify+1,
		PrepareForSnapshot	  = PrepareForBackup+1,
		Freeze				  = PrepareForSnapshot+1,
		Thaw				  = Freeze+1,
		PostSnapshot		  = Thaw+1,
		Abort				  = PostSnapshot+1,
		BackupComplete	         = Abort+1,
		BackupShutdown          = BackupComplete+1,
		PreRestore			  = BackupShutdown+1,
		PostRestore		         = PreRestore+1,
		NumEvents			  = PostRestore+1
	};

	enum Verbosity	{
		low 			= 0,
		medium		= low+1,
		high		= medium+1
	};

	class TestWriterException : public std::exception	{
	private:
		std::string m_what;
	public:
		TestWriterException(const wstring& what) : m_what("unexpected exception")	{					
			char* buffer = new char[what.size() + 1];			
			if (buffer == NULL)
				return;
	 		if (::WideCharToMultiByte(CP_ACP, 0, what.c_str(), -1, buffer, (int)what.size() + 1, 
	 							NULL, NULL))
	 			m_what = buffer;

	 		delete [] buffer;
		}
		TestWriterException(HRESULT hr, wstring function = L"") : 
					m_what("Unexpected error") 	{
			std::wstringstream msg;
	 		msg << L"An error code of 0x" << std::hex << hr << L" was encountered";
			if (!function.empty())
				msg << L" by " << function;
			
			char* buffer = new char[msg.str().size() + 1];			
			if (buffer == NULL)
				return;
	 		if (::WideCharToMultiByte(CP_ACP, 0, msg.str().c_str(), -1, buffer, (int)msg.str().size() + 1, 
	 							NULL, NULL))
	 			m_what = buffer;

	 		delete [] buffer;
		}
		TestWriterException(const TestWriterException& other)	: m_what(other.m_what)
			{}
		virtual const char* what() const	{ return m_what.c_str(); }
	};

	// generic class that automatically releases in destructor.  Useful for pointers and handles.
	template <class ValueType, ValueType invalid, class CloseType, CloseType closeFunction>
	class AutoValue	{
	private:
		ValueType m_value;
	public:
		AutoValue(ValueType v) : m_value(v)	{}
		~AutoValue()	{ 
			if (m_value != invalid)
				closeFunction(m_value); 
		}
		operator ValueType()	{ return m_value; }
	};

	typedef BOOL(*CloseType)(HANDLE);
	typedef AutoValue<HANDLE, INVALID_HANDLE_VALUE, CloseType, ::FindClose> AutoFindFileHandle;
	typedef AutoValue<HANDLE, INVALID_HANDLE_VALUE, CloseType, ::FindVolumeMountPointClose>
						AutoFindMountHandle;

	// little class to automatically acquire and release a critical section
	struct AutoCS	{
		CComAutoCriticalSection& m_section;
		AutoCS(CComAutoCriticalSection& section) : m_section(section) 	{ m_section.Lock(); }
		~AutoCS()	{ m_section.Unlock(); }
	};

      // function object to compute the logical and of two function objects
      template<class Inner1, class Inner2>
      struct unary_and : public std::unary_function<typename Inner1::argument_type, bool>   {
      protected:
        Inner1 m_inner1;
        Inner2 m_inner2;
      public:
        unary_and();
        unary_and(const Inner1& inner1, const Inner2& inner2) : m_inner1(inner1), m_inner2(inner2)
            {}

        bool operator()(const typename Inner2::argument_type& argument) const {
            return m_inner1(argument) && m_inner2(argument);
        }
      };

      template<class Inner1, class Inner2>
      inline  unary_and<Inner1, Inner2> and1(const Inner1& inner1, const Inner2& inner2) {
        return unary_and<Inner1, Inner2>(inner1, inner2);
      }
      
	void missingAttribute(const wchar_t* name);
	void missingElement(const wchar_t* name);
	void checkReturn(HRESULT returnCode, wstring function);
	void warnReturn(HRESULT returnCode, wstring function);
	void parseError(const CXMLDocument& doc);
	void printStatus(const wstring& status, Verbosity level = medium);
	void printStatus(const std::string& status, Verbosity level = medium);
	
	bool toBoolean(const wchar_t* name);
	VSS_USAGE_TYPE toUsage(const wchar_t* name);
	VSS_RESTOREMETHOD_ENUM toMethod(const wchar_t* name);
	VSS_WRITERRESTORE_ENUM toWriterRestore(const wchar_t* name);
	VSS_COMPONENT_TYPE toComponentType(const wchar_t* name);
	VSS_RESTORE_TARGET toRestoreTarget(const wchar_t* name);
	Events toWriterEvent(const wchar_t* name);
	Verbosity toVerbosity(const wchar_t* name);
	long toLong(const wchar_t* name);
	
	wstring toString(VSS_USAGE_TYPE usage);
	wstring toString(VSS_RESTOREMETHOD_ENUM method);
	wstring toString(VSS_WRITERRESTORE_ENUM writerRestore);
	wstring toString(VSS_COMPONENT_TYPE type);
	wstring toString(VSS_RESTORE_TARGET target);
	wstring toString(Events event);
	wstring toString(Verbosity verbosity);
	wstring toString(bool value);	
}
#endif


/*
**++
**
** Copyright (c) 2002  Microsoft Corporation
**
**
** Module Name:
**
**	    writerconfig.h
**
**
** Abstract:
**
**	declare classes that encapsulate the Test writer's configuration
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

#ifndef _WRITERCONFIG_H_
#define _WRITERCONFIG_H_

///////////////////////////////////////////////////////////////////////////////
// Includes

#include <string>
#include <algorithm>
#include <vector>
#include "vs_xml.hxx"
#include "utility.h"

using std::wstring;
using Utility::missingAttribute;
using Utility::missingElement;
using Utility::AutoCS;

///////////////////////////////////////////////////////////////////////////////
// Declarations 

// this namespace contains all XML data
namespace XMLData	{
	// names of attributes and elements
	extern wchar_t AlternateLocationMapping[];
	extern wchar_t Component[];
	extern wchar_t ComponentFile[];
	extern wchar_t ExcludeFile[];
	extern wchar_t NewTarget[];
	extern wchar_t FailEvent[];
	extern wchar_t Dependency[];
}

///////////////////////////////////////////////////////////////////////////////
// Class Declarations

// this is the generic collection class for sequences in the XML document
template <class T, wchar_t ElementName[]>
class XMLCollection	{
public:
	// required typedefs for a collection class
	typedef const T value_type ;
	typedef const T& reference ;
	typedef const T& const_reference ;
	typedef T* pointer ;
	typedef const T* const_pointer ;
	typedef long size_type ;

	class Iterator;
	typedef  Iterator iterator;	
	typedef Iterator const_iterator ;

	// The iterator for objects in the collection.  The iterator is read only -- objects in the collection can't be modified
	class Iterator : public std::iterator<std::input_iterator_tag, T>	{		
		CXMLDocument m_doc;
		mutable long* m_identifier;		
		mutable T* m_currentElement;
		bool m_pastEnd;		
		unsigned long m_index;
	public:
		Iterator() :  m_identifier(NULL), m_currentElement(NULL), m_pastEnd(true), m_index(0) 	{}
		Iterator(const Iterator& other) : m_identifier(NULL)	{ *this = other; }
		Iterator(const XMLCollection& collection)  : m_doc(collection.m_doc), m_currentElement(NULL), m_pastEnd(false), m_index(0)
		{ 
			// the assumption is that m_doc is currently at a node with type ElementName
			// bad things will ensue if this is not true			
			m_doc.SetToplevel(); 

			// m_identifer is used to ensure the following statements			
			//		iterator  i1 = ...;
			//		iterator  i2 = i1;
			//		assert(i1 == i2);
			//		assert(++i1 == ++i2);
			m_identifier = new long(1);
			if (m_identifier == NULL)
				throw std::bad_alloc();
		}
		
		virtual ~Iterator()	{ 
			if (m_identifier && --*m_identifier == 0)
				delete m_identifier;
			
			delete m_currentElement; 
		}
		
		Iterator& operator=(const Iterator& other)	{
			if (&other == this)
				return *this;
			
			m_currentElement = NULL; 
			m_doc = other.m_doc; 
			m_pastEnd = other.m_pastEnd; 
			m_index = other.m_index; 

			// make the reference count right
			if (other.m_identifier)
				++*other.m_identifier;
			if(m_identifier && --*m_identifier == 0)
				delete m_identifier;
			
			m_identifier = other.m_identifier;

			return *this;
		}
		
		bool operator==(const Iterator& other) const	 { 
			return (m_pastEnd && other.m_pastEnd) ||
				      ((m_identifier == other.m_identifier) && (m_index == other.m_index) && !m_pastEnd && !other.m_pastEnd); 
		}
		
		bool operator!=(const Iterator& other) const  { return !(*this == other); }

		const_reference  operator*() const	{
			assert(m_identifier);
			assert(!m_pastEnd);
			
			if (!m_currentElement)	
				m_currentElement = new T(m_doc);
			if (m_currentElement == NULL)
				throw std::bad_alloc();				
			
			return *m_currentElement;	
		}
		
		const_pointer operator->() const	{ return &**this; }
		
		Iterator& operator++() 	{
			if (m_pastEnd)	{
				assert(false);
				return *this;
			}

			assert(m_identifier);
			
			delete m_currentElement;
			m_currentElement = NULL;

			if (!m_doc.FindElement(ElementName, false))	
				m_pastEnd = true;

			++m_index;
			
			return *this;
		}
		
		Iterator operator++(int) 	{
			Iterator temp = *this;
			++*this;
			return temp;
		}
	};
	
	XMLCollection() :  m_size(0) {}	// initialize an empty collection
	XMLCollection(const XMLCollection& other)	{ *this = other; }
	XMLCollection(CXMLDocument& document) : m_doc(document), m_size(-1)	{	m_doc.SetToplevel(); }
	virtual ~XMLCollection()	{}
	
	XMLCollection& operator= (const XMLCollection& other)	{ 
		m_doc = other.m_doc; 
		m_size = other.m_size; 

		return *this;
	}

	bool operator==(const XMLCollection& other) const	{ 
		return (size() == other.size()) && std::equal(begin(), end(), other.begin()); 
	}
	bool operator!=(const XMLCollection& other) const	{ return !(*this == other); }
	
	size_type size() const	{
		// if we've already calculated the size, return it
		if (m_size != -1)
			return m_size;

		// otherwise, calculate the size and return it
		assert(!m_doc.IsEmpty());				// if so, then m_size should==0, and we wouldn't be here
		size_type size = 0;
		iterator current(*this);		// can't use begin()/end() as that would recurse
		while (current != m_pastEndIterator)	{
			++size;
			++current;
		}
		assert(size > 0);

		return (m_size = size);
	}		
	
	size_type max_size() const	{ return LONG_MAX; }
	bool empty() const	{ return size() == 0; }
	iterator begin() const	{ return empty() ? m_pastEndIterator : Iterator(*this); 	}
	iterator end() const	{ return m_pastEndIterator; }
private:
	friend class Iterator;
	
	CXMLDocument m_doc;
	mutable long m_size;
	Iterator m_pastEndIterator;
};

// little class to ensure that the document is always reset at the end of each function
struct Resetter	{
	CXMLDocument& m_config;
	Resetter(CXMLDocument& config) : m_config(config)	{}
	~Resetter()	{ m_config.ResetToDocument(); }
};

// generic file specification. 
struct File	{
	File(CXMLDocument node);
	File(const wstring& path, const wstring& filespec, bool recursive) : 
							m_path(path), m_filespec(filespec), m_recursive(recursive)		{
		std::transform(m_path.begin(), m_path.end(), m_path.begin(), towupper);
		std::transform(m_filespec.begin(), m_filespec.end(), m_filespec.begin(), towupper);
	}
	bool operator==(const File& other) const	{
		return (m_path == other.m_path) && 
			    (m_filespec == other.m_filespec) && 
			    (m_recursive == other.m_recursive);
	}
	bool operator!=(const File& other) const	{ return !(*this == other); }

	wstring toString() const;
	
	wstring m_path;
	wstring m_filespec;
	bool m_recursive;
};

// file specification together with an alternate-path target.
struct TargetedFile : public File	{
	TargetedFile(CXMLDocument node);
	TargetedFile(const wstring &path, const wstring& filespec, 
			     bool recursive, const wstring& alternate) : File(path, filespec, recursive),
													m_alternatePath(alternate)	{
		std::transform(m_alternatePath.begin(), m_alternatePath.end(), m_alternatePath.begin(), towupper);
	}
	bool operator==(const TargetedFile& other) const	{
		return (m_alternatePath == other.m_alternatePath) && 
			    (File::operator==(other));
	}
	bool operator!=(const TargetedFile& other) const	{ return !(*this == other); }

	wstring toString() const;
	
	wstring m_alternatePath;
};

// Writer restore method
struct RestoreMethod		{
	RestoreMethod(CXMLDocument node);
	bool operator==(const RestoreMethod& other) const	{
		return (m_method == other.m_method) &&
			    (m_writerRestore == other.m_writerRestore) &&
			    (m_service ==other.m_service) &&
			    (m_rebootRequired == other.m_rebootRequired) &&
			    (m_alternateLocations == other.m_alternateLocations);
	}
	bool operator!=(const RestoreMethod& other) const	{ return !(*this == other); }

	wstring toString() const;
	
	VSS_RESTOREMETHOD_ENUM m_method;
	VSS_WRITERRESTORE_ENUM m_writerRestore;
	wstring m_service;
	bool m_rebootRequired;

	typedef XMLCollection<TargetedFile, XMLData::AlternateLocationMapping> AlternateList ;
	AlternateList m_alternateLocations;	
};


// component dependency
struct Dependency   {
    Dependency(CXMLDocument node);
    bool operator==(const Dependency& other) const  {
        return (m_writerId == other.m_writerId) &&
                    (m_logicalPath == other.m_logicalPath) &&
                    (m_componentName == other.m_componentName);
    }

    bool operator!=(const Dependency& other) const  { return !(*this == other); }

    wstring toString() const;
    
    VSS_ID m_writerId;
    wstring m_logicalPath;
    wstring m_componentName;
};

// Writer component
struct ComponentBase	{	
	ComponentBase(const wstring& path = L"", const wstring& name = L"") : m_logicalPath(path), m_name(name)
		{}
	wstring toString() const;
	
	wstring m_logicalPath;
	wstring m_name;
};

struct  Component : public ComponentBase    {
	Component(CXMLDocument node);

	VSS_COMPONENT_TYPE m_componentType;
	VSS_RESTORE_TARGET m_restoreTarget;
	bool m_selectable;
	bool m_selectableForRestore;

	typedef XMLCollection<TargetedFile, XMLData::ComponentFile> ComponentFileList;
	typedef std::vector<TargetedFile> TargetList;
	typedef XMLCollection<Dependency, XMLData::Dependency> DependencyList;
	
	ComponentFileList m_files;
	DependencyList m_dependencies;
	TargetList m_newTargets;
};

// comparison operators for Component and ComponentBase
bool operator==(const ComponentBase& left, const ComponentBase& right);
bool operator!=(const ComponentBase& left, const ComponentBase& right);
bool operator==(const Component& left, const Component& right);
bool operator!=(const Component& left, const Component& right);

// Writer event.
struct  WriterEvent	{
	WriterEvent(CXMLDocument node);
	WriterEvent(Utility::Events event, bool retryable = true, long failures = 1) : 
				m_writerEvent(event), m_retryable(retryable), 
				m_numFailures(failures)	{}
	bool operator==(const WriterEvent& other) const	{ return m_writerEvent == other.m_writerEvent; }
	bool operator!=(const WriterEvent& other) const	{ return !(*this == other); }
	
	Utility::Events m_writerEvent;
	bool m_retryable;
	long m_numFailures;
};


// Singleton class that encapsulates writer configuration
class WriterConfiguration	{
private:
	// disallow explicit creation of this class
	WriterConfiguration()	{}
	WriterConfiguration(WriterConfiguration&);
	operator= (WriterConfiguration&);

	mutable CComAutoCriticalSection m_section;
	mutable CXMLDocument m_doc;

	template <class T, wchar_t ElementName[]>
	const XMLCollection<T, ElementName> getCollection() const	{
		assert(m_doc.GetLevel() == 0);
		AutoCS critical(m_section);
		Resetter reset(m_doc);

		if (m_doc.FindElement(ElementName, true))
			return XMLCollection<T, ElementName>(m_doc);
		else
			return XMLCollection<T,ElementName>();
	}
public:
	typedef XMLCollection<File, XMLData::ExcludeFile> ExcludeFileList;
	typedef XMLCollection<Component, XMLData::Component>ComponentList;
	typedef XMLCollection<WriterEvent, XMLData::FailEvent> FailEventList;
	static WriterConfiguration* instance();

	void loadFromXML(const wstring& xml);
	VSS_USAGE_TYPE usage() const;
	Utility::Verbosity  verbosity() const;
	bool checkExcludes() const;
	bool checkIncludes() const;
	RestoreMethod  restoreMethod() const;
	const  ExcludeFileList excludeFiles() const
		{ return getCollection<File, XMLData::ExcludeFile>(); }
	const  ComponentList components() const
		{ return getCollection<Component, XMLData::Component>(); }
	const  FailEventList failEvents() const
		{ return getCollection<WriterEvent, XMLData::FailEvent>(); }
};

// return the singleton instance of the class
// This is always called for the first time at the beginning of main, so no critical section
// need be involved
inline WriterConfiguration* WriterConfiguration::instance()
{
	static WriterConfiguration configuration;

	return &configuration;
}

#endif


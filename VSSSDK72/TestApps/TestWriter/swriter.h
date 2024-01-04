/*
**++
**
** Copyright (c) 2002  Microsoft Corporation
**
**
** Module Name:
**
**	    swriter.h
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
**
** Revision History:
**
**--
*/

#ifndef _SWRITER_H_
#define _SWRITER_H_

///////////////////////////////////////////////////////////////////////////////
// Includes

#include <vector>
#include <stack>
#include <functional>
#include <string>
#include "writerconfig.h"
#include "utility.h"

///////////////////////////////////////////////////////////////////////////////
// Declarations and Definitions

// {5AFFB034-969F-4919-8875-88F830D0EF89}
static const VSS_ID TestWriterId  = 
	{ 0x5affb034, 0x969f, 0x4919, { 0x88, 0x75, 0x88, 0xf8, 0x30, 0xd0, 0xef, 0x89 } };

static const wchar_t* const  TestWriterName = L"TestVssWriter";

using std::vector;

///////////////////////////////////////////////////////////////////////////////
// TestWriter class

class TestWriter : public CVssWriter	{
private:
	// member variables
	vector<Component> m_selectedComponents;
	vector<Component> m_selectedRestoreComponents;
	vector<wstring> m_toDelete;
	std::stack<wstring> m_directoriesToRemove;
	long m_failures[Utility::NumEvents];
	
	// closure to encapsulate calls to verifyFileAtLocation and record error messages
	class VerifyFileAtLocation : public std::binary_function<const TargetedFile, const File, void>	{
	private:
		const vector<File>& m_excluded;
		bool m_verifyAlternateLocation;
		mutable IVssComponent* m_pComponent;	// necessary due to bug in STL

		wstring verifyFileAtLocation(const File& file, const TargetedFile& location) const;
		bool verifyAlternateLocation(const TargetedFile& writerAlt) const;
		void saveErrorMessage(const wstring& message) const;
	public:
		VerifyFileAtLocation(const vector<File>& excludeFiles, IVssComponent* pComponent, 
			                      bool verifyAlternateLocation) : 
								m_excluded(excludeFiles), m_pComponent(pComponent),
								m_verifyAlternateLocation(verifyAlternateLocation)
			{}

		// The function operator.  Verifies the file, and records any error message
		void operator()(const TargetedFile location, const File file)  const { 
			saveErrorMessage(verifyFileAtLocation(file, location)); 
		}
	};

	// static helper functions

	// filter out elements in a source container that match a specific condition.  Place these elements
	// into a target container.
	template <class SourceIterator, class TargetIterator, class Condition>
	static void buildContainer_if(SourceIterator begin, SourceIterator end, TargetIterator output, Condition cond)	{
		SourceIterator current = std::find_if(begin, end, cond);
		while (current != end)	{
			*output++ = *current++;
			current = std::find_if(current, end, cond);
		}
	}


    // build a list of all files in this component and in all non-selectable subcomponents
    template<class TargetIterator>
    static void __cdecl buildComponentFiles(Component component, TargetIterator output) {
        WriterConfiguration* config = WriterConfiguration::instance();

        buildComponentFilesHelper(component, output);
        
        // build a list of all subcomponents
        vector<Component> subcomponents;
        buildContainer_if(config->components().begin(), 
                                 config->components().end(), 
                                 std::back_inserter(subcomponents), 
                                 std::bind2nd(std::ptr_fun(isSubcomponent), component));

        // add all files in all non-selectable subcomponents to the output
        std::pointer_to_binary_function<Component, std::back_insert_iterator<vector<TargetedFile> >, void>
        ptrFun(buildComponentFilesHelper);
        std::for_each(subcomponents.begin(), 
                            subcomponents.end(), 
                            std::bind2nd(ptrFun, output));
    }

    template<class TargetIterator>
    static void __cdecl buildComponentFilesHelper(Component component, TargetIterator output)  {
        // add all the files in the current component
        Component::ComponentFileList::iterator currentCompFile = component.m_files.begin();
        while (currentCompFile != component.m_files.end())  
            *output++ = *currentCompFile++;
    }

    static bool __cdecl isSubcomponent(ComponentBase sub, ComponentBase super);
    static bool __cdecl isSupercomponent(ComponentBase super, ComponentBase sub)    {
        return isSubcomponent(sub, super);
    }
    
    // return whether a component is selectable for backup
    static bool __cdecl isComponentSelectable(Component component)  {
        return component.m_selectable;
    }

    static bool __cdecl addableComponent(Component toAdd);
    
	// Returns whether a filespec is a wildcard or an exact filespec.
	static bool isExact(const wstring& file)    { return file.find_first_of(L"*?") == wstring::npos; }
	
	static bool  __cdecl targetMatches(File target, File file);
	static bool wildcardMatches(const wstring& first, const wstring& second);
	
	// non-static helper functions
	void enterEvent(Utility::Events event);
	void addComponent(const Component& component, IVssCreateWriterMetadata* pMetadata);
	void spitFiles(const TargetedFile& file);
	wstring getName(IVssComponent* pComponent);
	wstring getPath(IVssComponent* pComponent);
	void writeBackupMetadata(IVssComponent* pComponent);
	bool verifyBackupMetadata(IVssComponent* pComponent);
	void writeRestoreMetadata(IVssComponent* pComponent);
	bool  verifyRestoreMetadata(IVssComponent* pComponent);
	bool checkPathAffected(const TargetedFile& file);
	void cleanupFiles();
	void updateNewTargets(IVssComponent* pComponent, Component& writerComponent);
	void verifyFilesRestored(IVssComponent* pComponent, const Component& writerComponent);	

	// returns the private metadata string that the writer stores in the document
	wstring metadata(IVssComponent* pComponent, const wstring& suffix)	{ 
		return getPath(pComponent) + L"\\" + getName(pComponent) + suffix;
	}

       bool inSequence(Utility::Events event)   { 
        return event != Utility::Identify &&  event != Utility::BackupComplete && 
                   event != Utility::BackupShutdown; 
        }
public:
	TestWriter()	{ memset(m_failures, 0, sizeof(m_failures)); }
	virtual ~TestWriter()	{ Uninitialize(); }

	HRESULT STDMETHODCALLTYPE Initialize();
	HRESULT STDMETHODCALLTYPE Uninitialize()	{ return Unsubscribe(); }
	bool STDMETHODCALLTYPE OnIdentify(IN IVssCreateWriterMetadata *pMetadata);
	bool STDMETHODCALLTYPE OnPrepareBackup(IN IVssWriterComponents *pComponents);
	bool STDMETHODCALLTYPE OnPrepareSnapshot();
	bool STDMETHODCALLTYPE OnFreeze();
	bool STDMETHODCALLTYPE OnThaw();
       bool STDMETHODCALLTYPE OnPostSnapshot(IN IVssWriterComponents *pComponents);
	bool STDMETHODCALLTYPE OnAbort();
	bool STDMETHODCALLTYPE OnBackupComplete(IN IVssWriterComponents *pComponents);
	bool STDMETHODCALLTYPE OnBackupShutdown(IN VSS_ID SnapshotSetId);
	bool STDMETHODCALLTYPE OnPreRestore(IN IVssWriterComponents *pComponents);
	bool STDMETHODCALLTYPE OnPostRestore(IN IVssWriterComponents *pComponents);
};


#endif
	

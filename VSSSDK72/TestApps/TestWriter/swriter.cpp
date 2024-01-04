/*
**++
**
** Copyright (c) 2002  Microsoft Corporation
**
**
** Module Name:
**
**	swriter.cpp
**
**
** Abstract:
**
**	Test  program to to register a Writer with various properties
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
#include "swriter.h"
#include "utility.h"
#include "writerconfig.h"
#include <string>
#include <sstream>
#include <functional>
#include <algorithm>
#include <queue>

///////////////////////////////////////////////////////////////////////////////
// Declarations and Definitions

using std::wstring;
using std::string;
using std::wstringstream;
using std::exception;
using std::vector;

using Utility::checkReturn;
using Utility::warnReturn;
using Utility::printStatus;

static const wchar_t* const BackupString = L"BACKUP";
static const wchar_t* const RestoreString = L"RESTORE";

///////////////////////////////////////////////////////////////////////////////

// Initialize the test writer
HRESULT STDMETHODCALLTYPE TestWriter::Initialize()
{
	WriterConfiguration* config = WriterConfiguration::instance();

	printStatus(L"Initializing Writer", Utility::high);
	
	HRESULT hr = CVssWriter::Initialize(TestWriterId, 		// WriterID
								    TestWriterName, 	// wszWriterName
								    config->usage(),		// ut
								    VSS_ST_OTHER); 		// st
	checkReturn(hr, L"CVssWriter::Initialize");
	
	hr = Subscribe();
	checkReturn(hr, L"CVssWriter::Subscribe");

	return S_OK;
}

// OnIdentify is called as a result of the requestor calling GatherWriterMetadata
// Here we report the writer metadata using the passed-in interface
bool STDMETHODCALLTYPE TestWriter::OnIdentify(IN IVssCreateWriterMetadata *pMetadata)
try	
{
	enterEvent(Utility::Identify);
	
	WriterConfiguration* config = WriterConfiguration::instance();

	// set the restore method properly
	RestoreMethod method = config->restoreMethod();
	HRESULT hr = pMetadata->SetRestoreMethod(method.m_method, 
	                                                                      method.m_service.c_str(),
								                     NULL, 
								                     method.m_writerRestore, 
								                     method.m_rebootRequired);
	checkReturn(hr, L"IVssCreateWriterMetadata::SetRestoreMethod");
	printStatus(L"\nSet restore method: ", Utility::high);
	printStatus(method.toString(), Utility::high);
	
	// set the alternate-location list
	RestoreMethod::AlternateList::iterator currentAlt = method.m_alternateLocations.begin();
	while (currentAlt != method.m_alternateLocations.end())	{
		hr = pMetadata->AddAlternateLocationMapping(currentAlt->m_path. c_str(), 
										currentAlt->m_filespec.c_str(),
										currentAlt->m_recursive, 
										currentAlt->m_alternatePath.substr(0, currentAlt->m_alternatePath.size()-1).c_str());
		checkReturn(hr, L"IVssCreateWriterMetadata::AddAlternateLocationMapping");

		printStatus(L"\nAdded Alternate Location Mapping");
		printStatus(currentAlt->toString());
		
		++currentAlt;
	}

	// set the exclude-file list
	WriterConfiguration::ExcludeFileList::iterator currentExclude = config->excludeFiles().begin();
	while (currentExclude != config->excludeFiles().end())	{
		hr = pMetadata->AddExcludeFiles(currentExclude->m_path.c_str(), 
									   currentExclude->m_filespec.c_str(), 
									   currentExclude->m_recursive);
		checkReturn(hr, L"IVssCreateWriterMetadata::AddExcludeFiles");

		printStatus(L"\nAdded exclude filespec");
		printStatus(currentExclude->toString());
		
		++currentExclude;
	}

	// add all necessary components
	WriterConfiguration::ComponentList::iterator currentComponent = config->components().begin();
	while (currentComponent != config->components().end())	{
		addComponent(*currentComponent, pMetadata);
		
		++currentComponent;
	}

	return true;
}
catch(const exception& thrown)	
{
	printStatus(string("Failure in Identify event: ") + thrown.what(), Utility::low);
	return false;
}
catch(HRESULT error)
{
	Utility::TestWriterException e(error);
	printStatus(e.what(), Utility::low);
	return false;
};

// This function is called as a result of the requestor calling PrepareForBackup
// Here we do some checking to ensure that the requestor selected components properly
bool STDMETHODCALLTYPE TestWriter::OnPrepareBackup(IN IVssWriterComponents *pComponents)
try
{
    enterEvent(Utility::PrepareForBackup);
    
    WriterConfiguration* config = WriterConfiguration::instance();

    // get the number of components
    UINT numComponents = 0;
    HRESULT hr = pComponents->GetComponentCount(&numComponents);
    checkReturn(hr, L"IVssWriterComponents::GetComponentCount");

    // we haven't defined CUSTOM restore method for this writer.  consequently, backup apps should
    // ignore it.
    if ((config->restoreMethod().m_method == VSS_RME_CUSTOM) &&
         numComponents > 0) {
         throw Utility::TestWriterException(L"Components were selected for backup when CUSTOM restore"
                                                           L" method was used.  This is incorrect");
    }

    m_selectedComponents.clear();

    // for each component that was added
    for (unsigned int x = 0; x < numComponents; x++)	{
    // --- get the relevant information
        CComPtr<IVssComponent> pComponent;
        hr = pComponents->GetComponent(x, &pComponent);
        checkReturn(hr, L"IVssWriterComponents::GetComponent");

        writeBackupMetadata(pComponent);    // --- write private metadata

        // --- find the component in the metadata document.
        // --- this component may actually be a supercomponent of something
        // --- listed in the metadata document, so we must handle that case.
        // --- this is no longer true with the new interface changes...  now, only components
        // --- in the metadata doc can be added
        ComponentBase identity(getPath(pComponent), getName(pComponent));
        WriterConfiguration::ComponentList::iterator found = 
                                            std::find(config->components().begin(), 
                                                         config->components().end(),
                                                         identity);
        if (found == config->components().end())    {
            wstringstream msg;
            msg << L"Component with logical path: " << identity.m_logicalPath <<
                        L"  and name: " << identity.m_name << L" was added to the document" << std::endl <<
                        L", but does not appear in the writer metadata";
            printStatus(msg.str());
        }   else if (!addableComponent(*found))  {   
            wstringstream msg;
            msg << L"Component with logical path: " << identity.m_logicalPath <<
                        L" and name: " << identity.m_name << L" was added to the document" << std::endl <<
                        L", but is not a selectable component";
            printStatus(msg.str());
        }   else    {
            m_selectedComponents.push_back(*found);
        }
    }

    // any non-selectable component with no selectable ancestor must be added.  Check this.
    vector<Component> mustAddComponents;
    buildContainer_if(config->components().begin(), 
                             config->components().end(), 
                             std::back_inserter(mustAddComponents), 
                             Utility::and1(std::not1(std::ptr_fun(isComponentSelectable)),
                                                std::ptr_fun(addableComponent)));

    vector<Component>::iterator currentMust = mustAddComponents.begin();
    while (currentMust != mustAddComponents.end())  {
        if (std::find(m_selectedComponents.begin(),
                          m_selectedComponents.end(),
                          *currentMust) == m_selectedComponents.end())  {
                          wstringstream msg;
                          msg << L"\nComponent with logical path: " << currentMust->m_logicalPath <<
                                      L" and name: " << currentMust->m_name <<
                                      L" is a non-selectable component with no selectable ancestor, and therefore " <<
                                      L" must be added to the document.  However, it was not added";
                          printStatus(msg.str());                          
        }
        ++currentMust;
    }
    
    return true;
}
catch(const exception& thrown)  
{
        printStatus(string("Failure in PrepareForBackup event: ") + thrown.what(), Utility::low);
    return false;
}
catch(HRESULT error)
{
    Utility::TestWriterException e(error);
    printStatus(e.what(), Utility::low);
    return false;
};

// This function is called after a requestor calls DoSnapshotSet
// Here we ensure that the requestor has added the appropriate volumes to the
// snapshot set.  If a spit directory is specified, the spit is done here as well.
bool STDMETHODCALLTYPE TestWriter::OnPrepareSnapshot()	
try
{
	enterEvent(Utility::PrepareForSnapshot);

	// build the list of all files being backed up
	vector<TargetedFile> componentFiles;
	std::pointer_to_binary_function<Component, std::back_insert_iterator<vector<TargetedFile> >, void>
	    ptrFun(buildComponentFiles);
	std::for_each(m_selectedComponents.begin(), 
		              m_selectedComponents.end(), 
                            std::bind2nd(ptrFun, std::back_inserter(componentFiles)));
	
	// for every file being backed up
	vector<TargetedFile>::iterator currentFile = componentFiles.begin();
	while (currentFile != componentFiles.end())	{
		// --- ensure the filespec has been snapshot, taking care of mount points
		if (!checkPathAffected(*currentFile))	{
			wstringstream msg;
			msg << L"Filespec " << currentFile->m_path << currentFile->m_filespec <<
				L"is selected for backup but contains files that have not been snapshot" << std::endl;
			printStatus(msg.str());
		}

		// --- if a spit is needed, spit the file to the proper directory
		if (!currentFile->m_alternatePath.empty())		
			spitFiles(*currentFile);
			
		++currentFile;
	}
		
	return true;
}	
catch(const exception& thrown)	
{
	printStatus(string("Failure in PrepareForSnapshot event: ") + thrown.what(), Utility::low);
	return false;
}
catch(HRESULT error)
{
	Utility::TestWriterException e(error);
	printStatus(e.what(), Utility::low);
	return false;
}

// This function is called after a requestor calls DoSnapshotSet
//  Currently, we don't do much here that is interesting.
bool STDMETHODCALLTYPE TestWriter::OnFreeze()
try	
{
	enterEvent(Utility::Freeze);

	return true;	
}	
catch(const exception& thrown)	
{
	printStatus(string("Failure in Freeze event: ") + thrown.what(), Utility::low);
	return false;
}
catch(HRESULT error)
{
	Utility::TestWriterException e(error);
	printStatus(e.what(), Utility::low);
	return false;
}

// This function is called after a requestor calls DoSnapshotSet
//  Currently, we don't do much here that is interesting.
bool STDMETHODCALLTYPE TestWriter::OnThaw()
try	
{
	enterEvent(Utility::Thaw);

	return true;	
}
catch(const exception& thrown)	
{
	printStatus(string("Failure in Thaw event: ") + thrown.what(), Utility::low);
	return false;
}
catch(HRESULT error)
{
	Utility::TestWriterException e(error);
	printStatus(e.what(), Utility::low);
	return false;
}

// This function is called after a requestor calls DoSnapshotSet
// Here we cleanup the files that were spit in OnPrepareSnapshot
// and do some basic sanity checking
bool STDMETHODCALLTYPE TestWriter::OnPostSnapshot(IN IVssWriterComponents *pComponents)
try	
{
	enterEvent(Utility::PostSnapshot);

	cleanupFiles();
	
	// get the number of components
	UINT numComponents = 0;
	HRESULT hr = pComponents->GetComponentCount(&numComponents);
	checkReturn(hr, L"IVssWriterComponents::GetComponentCount");

	// for each component that was added
	for (unsigned int x = 0; x < numComponents; x++)	{
		// --- get the relevant information		
		CComPtr<IVssComponent> pComponent;
		hr = pComponents->GetComponent(x, &pComponent);
		checkReturn(hr, L"IVssWriterComponents::GetComponent");
		
		// --- ensure that the component was backed up
		ComponentBase identity(getPath(pComponent), getName(pComponent));
		vector<Component>::iterator found = std::find(m_selectedComponents.begin(), 
												m_selectedComponents.end(),
												identity);
		if (found == m_selectedComponents.end())	{
			wstringstream msg;
			msg << L"Component with logical path: " << identity.m_logicalPath <<
				     L"and name: " << identity.m_name <<
				     L"was selected in PostSnapshot, but was not selected in PrepareForSnapshot";
			printStatus(msg.str(), Utility::low);
			
			continue;
		}

		if (!verifyBackupMetadata(pComponent))	{
			wstringstream msg;
			msg << L"Component with logical path: " << identity.m_logicalPath <<
				     L"and name: " << identity.m_name <<
				     L" has been corrupted in PostSnapshot";
			printStatus(msg.str(), Utility::low);			
		}
	}

	m_selectedComponents.clear();

	return true;	
}
catch(const exception& thrown)	
{
	printStatus(string("Failure in PostSnapshot event: ") + thrown.what(), Utility::low);
	return false;
}
catch(HRESULT error)
{
	Utility::TestWriterException e(error);
	printStatus(e.what(), Utility::low);
	return false;
}

// This function is called to abort the writer's backup sequence.
// If the writer has a spit component, spit files are cleaned up here.
bool STDMETHODCALLTYPE TestWriter::OnAbort()
try
{
	enterEvent(Utility::Abort);

	m_selectedComponents.clear();
	cleanupFiles();

	return true;
}
catch(const exception& thrown)	
{
	printStatus(string("Failure in Abort event: ") + thrown.what(), Utility::low);
	return false;
}
catch(HRESULT error)
{
	Utility::TestWriterException e(error);
	printStatus(e.what(), Utility::low);
	return false;
}

// This function is called as a result of the requestor calling BackupComplete
// Once again we do sanity checking, and we also verify that the metadata we 
// wrote in PrepareForBackup has remained the same
bool STDMETHODCALLTYPE TestWriter::OnBackupComplete(IN IVssWriterComponents *pComponents)
try	
{
	enterEvent(Utility::BackupComplete);
	
	WriterConfiguration* config = WriterConfiguration::instance();

	// get the number of components
	UINT numComponents = 0;
	HRESULT hr = pComponents->GetComponentCount(&numComponents);
	checkReturn(hr, L"IVssWriterComponents::GetComponentCount");

	// for each component that was added
	for (unsigned int x = 0; x < numComponents; x++)	{
		// --- get the relevant information		
		CComPtr<IVssComponent> pComponent;
		hr = pComponents->GetComponent(x, &pComponent);
		checkReturn(hr, L"IVssWriterComponents::GetComponent");
		
		// --- ensure that the component is valid
		ComponentBase identity(getPath(pComponent), getName(pComponent));
		WriterConfiguration::ComponentList::iterator found = 
								std::find(config->components().begin(), 
								   	      config->components().end(),
									      identity);
		if (found == config->components().end())	{
			wstringstream msg;
			msg << L"Component with logical path: " << identity.m_logicalPath <<
				     L"and name: " << identity.m_name <<
				     L" is selected in BackupComplete, but does not appear in the writer metadata";
			printStatus(msg.str(), Utility::low);
			
			continue;
		}

		if (!verifyBackupMetadata(pComponent))	{
			wstringstream msg;
			msg << L"Component with logical path: " << identity.m_logicalPath <<
				     L"and name: " << identity.m_name <<
				     L" has been corrupted in BackupComplete";
			printStatus(msg.str(), Utility::low);
			}	

		// check that the backup succeeded
		bool backupSucceeded = false;
		hr = pComponent->GetBackupSucceeded(&backupSucceeded);
		if (!backupSucceeded)	{
			wstringstream msg;
			msg << L"Component with logical path: " << identity.m_logicalPath <<
				     L"and name: " << identity.m_name <<
				     L" was not marked as successfully backed up.";
		}
	}

	return true;	
}	
catch(const exception& thrown)	
{
	printStatus(string("Failure in BackupComplete event: ") + thrown.what(), Utility::low);
	return false;
}
catch(HRESULT error)
{
	Utility::TestWriterException e(error);
	printStatus(e.what(), Utility::low);
	return false;
}

// This function is called at the end of the backup process.  This may happen as a result
// of the requestor shutting down, or it may happen as a result of abnormal termination 
// of the requestor.
bool STDMETHODCALLTYPE TestWriter::OnBackupShutdown(IN VSS_ID SnapshotSetId)
try
{
	UNREFERENCED_PARAMETER(SnapshotSetId);
	
	enterEvent(Utility::BackupShutdown);
	return true;
}
catch(const exception& thrown)
{
	printStatus(string("Failure in BackupShutdown event: ") + thrown.what(), Utility::low);
	return false;
}

// This function is called as a result of the requestor calling PreRestore
// We check that component selection has been done properly, verify the
// backup metadata, and set targets appropriately.
bool STDMETHODCALLTYPE TestWriter::OnPreRestore(IN IVssWriterComponents *pComponents)
try
{
    enterEvent(Utility::PreRestore);

    WriterConfiguration* config = WriterConfiguration::instance();

    // get the number of components
    UINT numComponents = 0;
    HRESULT hr = pComponents->GetComponentCount(&numComponents);
    checkReturn(hr, L"IVssWriterComponents::GetComponentCount");

    m_selectedRestoreComponents.clear();
    // for each component that was added
    for (unsigned int x = 0; x < numComponents; x++)    {
        // --- get the relevant information
        CComPtr<IVssComponent> pComponent;
        hr = pComponents->GetComponent(x, &pComponent);
        checkReturn(hr, L"IVssWriterComponents::GetComponent");
        
        // --- ensure that the component is valid
        ComponentBase identity(getPath(pComponent), getName(pComponent));
        WriterConfiguration::ComponentList::iterator found = 
                                                  std::find(config->components().begin(), 
                                                                config->components().end(), 
                                                                identity);
        if (found == config->components().end())    {
            wstringstream msg;
            msg << L"Component with logical path: " << identity.m_logicalPath <<
                         L"and name: " << identity.m_name <<
                         L" is selected in PreRestore, but does not appear in the writer metadata";

            pComponent->SetPreRestoreFailureMsg(msg.str().c_str());
            printStatus(msg.str(), Utility::low);
            continue;
        }

        // only process those component that are selected for restore
        bool selectedForRestore = false;
        hr = pComponent->IsSelectedForRestore(&selectedForRestore);
        checkReturn(hr, L"IVssComponent::IsSelectedForRestore");
        if (!selectedForRestore)
            continue;
        
        m_selectedRestoreComponents.push_back(*found);

        if (!verifyBackupMetadata(pComponent))  {       // --- verify the backup metadata
            wstringstream msg;
            msg << L"Component with logical path: " << identity.m_logicalPath <<
                         L"and name: " << identity.m_name <<
                         L" has been corrupted in PreRestore";
            pComponent->SetPreRestoreFailureMsg(msg.str().c_str());
            printStatus(msg.str(), Utility::low);
        }
        writeRestoreMetadata(pComponent);               // --- write restore metadata

        // --- set the target appropriately
        if (found->m_restoreTarget != VSS_RT_UNDEFINED) {
            HRESULT hr =pComponent->SetRestoreTarget(found->m_restoreTarget);
            checkReturn(hr, L"IVssComponent::SetRestoreTarget");

            printStatus(wstring(L"Set Restore Target: ") +
                      Utility::toString(found->m_restoreTarget), Utility::high);
        }
    }

    return true;
}
catch(const exception& thrown)
{
    printStatus(string("Failure in PreRestore event: ") + thrown.what(), Utility::low);
    return false;
}
catch(HRESULT error)
{
    Utility::TestWriterException e(error);
    printStatus(e.what(), Utility::low);
    return false;
}

// This function is called as a result of the requestor calling PreRestore
// We do some sanity checking, and then check to see if files have indeed
// been restored
bool STDMETHODCALLTYPE TestWriter::OnPostRestore(IN IVssWriterComponents *pComponents)
try
{
    enterEvent(Utility::PostRestore);    

    // get the number of components
    UINT numComponents = 0;
    HRESULT hr = pComponents->GetComponentCount(&numComponents);
    checkReturn(hr, L"IVssWriterComponents::GetComponentCount");

    // for each component 
    for (unsigned int x = 0; x < numComponents; x++)    {
        // --- get the relevant information
        CComPtr<IVssComponent> pComponent;
        hr = pComponents->GetComponent(x, &pComponent);
        checkReturn(hr, L"I VssWriterComponents::GetComponent");

        // --- ensure that the component is valid
        ComponentBase identity(getPath(pComponent), getName(pComponent));
        vector<Component>::iterator found = std::find(m_selectedRestoreComponents.begin(),
                                                                               m_selectedRestoreComponents.end(), 
                                                                               identity);
        if (found == m_selectedRestoreComponents.end()) {
            wstringstream msg;
            msg << L"Component with logical path: " << identity.m_logicalPath <<
                         L"and name: " << identity.m_name <<
                         L" is selected in PostRestore, but was not selected in PreRestore";
            pComponent->SetPostRestoreFailureMsg(msg.str().c_str());
            printStatus(msg.str(), Utility::low);
            continue;
        }

        // only process those component that are selected for restore
        bool selectedForRestore = false;
        hr = pComponent->IsSelectedForRestore(&selectedForRestore);
        checkReturn(hr, L"IVssComponent::IsSelectedForRestore");
        if (!selectedForRestore)
            continue;

        if (!verifyRestoreMetadata(pComponent)) {
            wstringstream msg;
            msg << L"Component with logical path: " << identity.m_logicalPath <<
                         L"and name: " << identity.m_name <<
                         L" has been corrupted in PostRestore";
            pComponent->SetPostRestoreFailureMsg(msg.str().c_str());
            printStatus(msg.str(), Utility::low);
            continue;
        }


        VSS_FILE_RESTORE_STATUS rStatus;
        hr = pComponent->GetFileRestoreStatus(&rStatus);
        checkReturn(hr, L"IVssComponent::GetFileRestoreStatus");

        if (rStatus != VSS_RS_ALL)  {
            wstringstream msg;
            msg << L"Component with logical path: " << identity.m_logicalPath <<
                         L"and name: " << identity.m_name <<
                         L" was not marked as having been successfully restored";
            printStatus(msg.str(), Utility::low);
            continue;
        }

        updateNewTargets(pComponent, *found);
        verifyFilesRestored(pComponent, *found);
    }

    return true;
}
catch(const exception& thrown)
{
    printStatus(string("Failure in PostRestore event: ") + thrown.what(), Utility::low);
    return false;
}
catch(HRESULT error)
{
    Utility::TestWriterException e(error);
    printStatus(e.what(), Utility::low);
    return false;
}

// This function is called at the entry to all writer events. 
// A status message is printed to the console, and the event is failed if necessary.
void TestWriter::enterEvent(Utility::Events event)
{
	static HRESULT errors[] = { VSS_E_WRITERERROR_INCONSISTENTSNAPSHOT,
							VSS_E_WRITERERROR_OUTOFRESOURCES,
							VSS_E_WRITERERROR_TIMEOUT,
							VSS_E_WRITERERROR_RETRYABLE							 
						      };
	
	printStatus(wstring(L"\nReceived event: ") + Utility::toString(event));

	WriterConfiguration* config = WriterConfiguration::instance();

	// figure out whether we should fail this event
	WriterEvent writerEvent(event);
	WriterConfiguration::FailEventList::iterator found = std::find(config->failEvents().begin(), 
													     config->failEvents().end(),
											 		     writerEvent);

	// if so, then fail it unless failures have run out
	if (found != config->failEvents().end())	{		
		bool failEvent = !found->m_retryable || (m_failures[event] < found->m_numFailures);
		bool setFailure = inSequence(event);
		if (!found->m_retryable && setFailure)
			SetWriterFailure(VSS_E_WRITERERROR_NONRETRYABLE);
		else if (failEvent && setFailure)
			SetWriterFailure(errors[rand() % (sizeof(errors) / sizeof(errors[0]))]);

		if (failEvent)	{
			++m_failures[event];
			wstringstream msg;
			msg << L"Failure Requested in Event: " << Utility::toString(event) <<
				L" Failing for the " << m_failures[event] << L" time";
			
			throw Utility::TestWriterException(msg.str());
		}
	}
}


// This helper function adds a component to the writer-metadata document
void TestWriter::addComponent(const Component& component, IVssCreateWriterMetadata* pMetadata)
{
	const wchar_t* logicalPath = component.m_logicalPath.empty() ? NULL : component.m_logicalPath.c_str();

	// add the component to the document
	HRESULT hr = pMetadata->AddComponent(component.m_componentType,		// ct
			 					                 logicalPath,							// logicalwszLogicalPath
					                			   component.m_name.c_str(),			// wszComponentName
					                			   NULL,								// wszCaption
					                			   NULL,								// pbIcon
 								                 0,									// cbIcon
								                 true,								// bRestoreMetadata
					       			          true,								// bNotifyOnBackupComplete
						                		   component.m_selectable,  			// bSelectable
						                		   component.m_selectableForRestore	// bSelectableForRestore
						                		   );
	checkReturn(hr, L"IVssCreateWriterMetadata::AddComponent");						                		   

	printStatus(L"\nAdded component: ", Utility::high);
	printStatus(component.toString(), Utility::high);

	// add all of the files to the component.  NOTE: we don't allow distinctions between database files
	// and database log files in the VSS_CT_DATABASE case.
	// we sometimes put a '\' on the end and sometimes not to keep requestors honest.
	Component::ComponentFileList::iterator current = component.m_files.begin();
	while (current != component.m_files.end())	{		
		if (component.m_componentType == VSS_CT_FILEGROUP)	{
			const wchar_t* alternate = current->m_alternatePath.empty() ? NULL : 
							              current->m_alternatePath.c_str();
			hr = pMetadata->AddFilesToFileGroup(logicalPath,
											  component.m_name.c_str(),
											  current->m_path.substr(0, current->m_path.size()-1).c_str(),
											  current->m_filespec.c_str(),
											  current->m_recursive,
											  alternate);
			checkReturn(hr, L"IVssCreateWriterMetadata::AddFilesToFileGroup");
		}    else if (component.m_componentType == VSS_CT_DATABASE)	{
			hr = pMetadata->AddDatabaseFiles(logicalPath,
										     component.m_name.c_str(),
										     current->m_path.c_str(),
										     current->m_filespec.c_str());
			checkReturn(hr, L"IVssCreateWriterMetadata::AddDatabaseFiles");
										     
		}	else		{
			assert(false);
		}
		
		printStatus(L"\nAdded Component Filespec: ");
		printStatus(current->toString());
		
		++current;
	}

    // add all dependencies to the dependency list for the writer
    Component::DependencyList::iterator currentDependency = component.m_dependencies.begin();
    while (currentDependency != component.m_dependencies.end())	{
        hr = pMetadata->AddComponentDependency(logicalPath, 
                                                                                  component.m_name.c_str(),                           // wszForLogicalPath
                                                                                  currentDependency->m_writerId,                  // wszForComponentName
                                                                                  currentDependency->m_logicalPath.c_str(),             // wszOnLogicalPath
                                                                                  currentDependency->m_componentName.c_str()    // wszOnComponentName
                                                                            );
		checkReturn(hr, L"IVssCreateWriterMetadata::AddComponentDependency");

		printStatus(L"\nAdded Component Dependency: ");
		printStatus(currentDependency->toString());
		
		++currentDependency;
	}
}

// This helper function spits all files in a file specification to an alternate location
void TestWriter::spitFiles(const TargetedFile& file)
{
	assert(!file.m_path.empty());
	assert(file.m_path[file.m_path.size() - 1] == L'\\');
	assert(!file.m_alternatePath.empty());
	assert(file.m_alternatePath[file.m_alternatePath.size() - 1] == L'\\');

	// ensure that both the source and target directories exist
	DWORD attributes = ::GetFileAttributes(file.m_path.c_str());
	if ((attributes == INVALID_FILE_ATTRIBUTES) ||
	     !(attributes & FILE_ATTRIBUTE_DIRECTORY))	{
		wstringstream msg;
		msg << L"The source path " << file.m_path << L" does not exist";
		throw Utility::TestWriterException(msg.str());
	}

	attributes = ::GetFileAttributes(file.m_alternatePath.c_str());
	if ((attributes == INVALID_FILE_ATTRIBUTES) ||
	    !(attributes & FILE_ATTRIBUTE_DIRECTORY))	{
		wstringstream msg;
		msg << L"The target path " << file.m_alternatePath << L" does not exist";
		throw Utility::TestWriterException(msg.str());
	}

	// start by copying files from the specified root directory
	std::queue<wstring> paths;
	paths.push(file.m_path);

	// walk through in breadth-first order.  It's less resource intensive than depth-first, and
	// potentially more performant
	while (!paths.empty())	{
		// --- grab the next path off the queue
		wstring currentPath = paths.front();
		paths.pop();

		// --- start walking all files in the directory
		WIN32_FIND_DATA findData;
		Utility::AutoFindFileHandle findHandle = ::FindFirstFile((currentPath + L'*').c_str(), &findData);
		if (findHandle == INVALID_HANDLE_VALUE)
			continue;

		do	{
			wstring currentName = findData.cFileName;
			if (currentName == L"." ||
			     currentName == L"..")
			     continue;
			
			std::transform(currentName.begin(), currentName.end(), currentName.begin(), towupper);

			// --- if we've hit a direcctory and we care to do a recursive spit
			if ((findData.dwFileAttributes  & FILE_ATTRIBUTE_DIRECTORY) &&
			     file.m_recursive)	{
				assert(!currentName.empty());
				if (currentName[currentName.size() - 1] != L'\\')
					currentName += L"\\";

				// figure out where the target for this new directory is
				assert(currentPath.find(file.m_path) == 0);
				wstring extraDirectory = currentPath.substr(file.m_path.size());
				wstring alternateLocation = file.m_alternatePath + extraDirectory + currentName;

			       // create a target directory to hold the copied files.  
				if (!::CreateDirectory(alternateLocation.c_str(), NULL) &&
					::GetLastError() != ERROR_ALREADY_EXISTS)
					checkReturn(HRESULT_FROM_WIN32(::GetLastError()), L"CreateDirectory");

                            m_directoriesToRemove.push(alternateLocation.c_str());
                            
			       // push the directory on the queue so it gets processed as well
				paths.push(currentPath + currentName);
				continue;			   
			}

			// --- if we've hit a regular file with a matching filespec
			if (!(findData.dwFileAttributes  & FILE_ATTRIBUTE_DIRECTORY) && 
				wildcardMatches(currentName, file.m_filespec))	{
				// figure out where the new target location is
				assert(currentPath.find(file.m_path) == 0);
				wstring extraDirectory = currentPath.substr(file.m_path.size());
				wstring alternateLocation = file.m_alternatePath + extraDirectory + currentName;

				wstringstream msg;
				msg << L"Spitting File: " << currentPath + currentName <<
					     L" To location: " <<  alternateLocation;
				printStatus(msg.str() , Utility::high);

				// copy the file over
				if (!::CopyFile((currentPath + currentName).c_str(), alternateLocation.c_str(), FALSE))
					checkReturn(HRESULT_FROM_WIN32(::GetLastError()), L"CopyFile");
				else
					m_toDelete.push_back(alternateLocation);
			}
		}	while (::FindNextFile(findHandle, &findData));
	}
}

// extract the component name from an interface pointer
wstring TestWriter::getName(IVssComponent* pComponent)
{
    CComBSTR name;
    HRESULT hr = pComponent->GetComponentName(&name);
    checkReturn(hr, L"IVssComponent::GetComponentName");

    assert(name != NULL);       // this should never happen

    return (BSTR)name;
}

// extract the component logical path from an interface pointer
wstring TestWriter::getPath(IVssComponent* pComponent)
{
    CComBSTR path;
    HRESULT hr = pComponent->GetLogicalPath(&path);
    checkReturn(hr, L"IVssComponent::GetLogicalPath");

    // GetLogicalPath can indeed return NULL so be careful
    return (path.Length() > 0) ? (BSTR)path : L"";
}

// write a backup metadata stamp to the component
void TestWriter::writeBackupMetadata(IVssComponent* pComponent)
{
    HRESULT hr = pComponent->SetBackupMetadata(metadata(pComponent, BackupString).c_str());
    checkReturn(hr, L"IVssComponent::SetBackupMetadata");	

    printStatus(wstring(L"Writing backup metadata: ") + metadata(pComponent, BackupString),
                   Utility::high);
}

// verify that a backup metadata stamp is intact
bool TestWriter::verifyBackupMetadata(IVssComponent* pComponent)
{
    CComBSTR data;
    HRESULT hr = pComponent->GetBackupMetadata(&data);
    checkReturn(hr, L"IVssComponent::GetBackupMetadata");

    printStatus(wstring(L"\nComparing metadata: ") + (data.Length() ? (BSTR)data : L"") +
                     wstring(L" Against expected string: ") + metadata(pComponent, BackupString),
                      Utility::high);

    if (data.Length() == 0 || metadata(pComponent, BackupString) != (BSTR)data)
        return false;

    return true;
}

// write a restore metadata stamp to the component
void TestWriter::writeRestoreMetadata(IVssComponent* pComponent)
{
    HRESULT hr = pComponent->SetRestoreMetadata(metadata(pComponent, RestoreString).c_str());
    checkReturn(hr, L"IVssComponent::SetRestoreMetadata");

    printStatus(wstring(L"Writing restore metadata: ") + metadata(pComponent, RestoreString),
                      Utility::high);
}

// verify that a restore metadata stamp is intact
bool TestWriter::verifyRestoreMetadata(IVssComponent* pComponent)
{
	CComBSTR data;
	HRESULT hr = pComponent->GetRestoreMetadata(&data);
	checkReturn(hr, L"IVssComponent::GetRestoreMetadata");

	printStatus(wstring(L"Comparing metadata: ") + (data.Length() ? (BSTR)data : L"") +
		          wstring(L" Against expected string: ") + metadata(pComponent, RestoreString),
		          Utility::high);

	if (data.Length() == 0 || metadata(pComponent, RestoreString) != (BSTR)data)
		return false;

	return true;
}

// check to see if the specified file (or files) are all in the current snapshot set
// doesn't check directory junctions... this will not be changed anytime soon.
// recursive mount points are also not handled very well
bool TestWriter::checkPathAffected(const TargetedFile& file)
{
	wstring backupPath = file.m_alternatePath.empty() ? file.m_path : file.m_alternatePath;
	
	// if the path in question isn't snapshot, then return false
	if (!IsPathAffected(backupPath.c_str()))
		return false;

	// if the filespec isn't recursive, then we're done
	if (!file.m_recursive)
		return true;

	// get the name of the volume we live on
	wchar_t volumeMount[MAX_PATH];
       if(!::GetVolumePathName(backupPath.c_str(), volumeMount, MAX_PATH))
       	checkReturn(HRESULT_FROM_WIN32(::GetLastError()), L"GetVolumePathName");
	assert(backupPath.find(volumeMount) == 0);
		
	wchar_t volumeName[MAX_PATH];
	if (!::GetVolumeNameForVolumeMountPoint(volumeMount, volumeName, MAX_PATH))
		checkReturn(HRESULT_FROM_WIN32(::GetLastError()), L"GetVolumeNameForVolumeMountPoint");

	// start off with the volume name and starting directory on a worklist.
	std::queue<std::pair<wstring, wstring> > worklist;
	worklist.push(std::make_pair(wstring(volumeName), backupPath.substr(wcslen(volumeMount))));

	while (!worklist.empty())	{
		// get the current volume and directory off the worklist
		wstring currentVolume = worklist.front().first;
		wstring currentPath = worklist.front().second;
		worklist.pop();
		
		// now, enumerate all mount points on the volume
	       Utility::AutoFindMountHandle findHandle = ::FindFirstVolumeMountPoint(currentVolume.c_str(), volumeMount, MAX_PATH);
       	if (findHandle == INVALID_HANDLE_VALUE)
       		continue;

	       do	{
			std::transform(volumeMount, volumeMount + wcslen(volumeMount), volumeMount, towupper);
		   	
       		wstring mountPoint = currentVolume + volumeMount;
			
	       	// if this mount point is included in the file specification, the volume better be included in the snapshot set
       		if ((mountPoint.find(currentVolume + currentPath) == 0) &&
       		    !IsPathAffected(mountPoint.c_str()))
	       	    return false;

			if (!::GetVolumeNameForVolumeMountPoint(volumeMount, volumeName, MAX_PATH))
				checkReturn(HRESULT_FROM_WIN32(::GetLastError()), L"GetVolumeNameForVolumeMountPoint");

			// put this volume on the worklist so it gets processed as well
			// Mount points always point to the root of a volume, so pass in 
			// an empty second argument.  When junctions are supported, we
			// will pass in the target directory as the second argument.
       		worklist.push(std::make_pair(wstring(volumeName), wstring()));	// this line will change when we support junctions	
	       }	while (::FindNextVolumeMountPoint(findHandle, volumeMount, MAX_PATH) == TRUE);
	}

	return true;
}

// delete all files and directories created in PrepareForSnapshot
void TestWriter::cleanupFiles()
{
       // delete all created files
    vector<wstring>::iterator currentFile = m_toDelete.begin();
    while (currentFile != m_toDelete.end()) {
        if (!::DeleteFile(currentFile->c_str()))
            warnReturn(HRESULT_FROM_WIN32(::GetLastError()), L"DeleteFile");

        ++currentFile;
    }
    m_toDelete.clear();

    // remove all created directories in the proper order
    while (!m_directoriesToRemove.empty())    {
        wstring dir = m_directoriesToRemove.top();
        if (!::RemoveDirectory(dir.c_str()))
            warnReturn(HRESULT_FROM_WIN32(::GetLastError()), L"RemoveDirectory");

        m_directoriesToRemove.pop();
    }
}

// check to see if the requestor has added any new targets, and add them to the
// Component structure
void TestWriter::updateNewTargets(IVssComponent* pComponent, Component& writerComponent)
{
    HRESULT hr = S_OK;

    UINT newTargetCount = 0;
    hr = pComponent->GetNewTargetCount(&newTargetCount);
    checkReturn(hr, L"IVssComponent::GetNewTargetCount");
    
    writerComponent.m_newTargets.clear();
    for (UINT x = 0; x < newTargetCount; x++)   {
        // get information about the new target
        CComPtr<IVssWMFiledesc> newTarget;

        hr = pComponent->GetNewTarget(x, &newTarget);
        checkReturn(hr, L"IVssComponent::GetNewTarget");

        CComBSTR path, filespec, alternateLocation;
        bool recursive = false;

        hr = newTarget->GetPath(&path);
        checkReturn(hr, L"IVssComponent:GetPath");
        
        hr = newTarget->GetFilespec(&filespec);
        checkReturn(hr, L"IVssComponent:GetFilespec");

        hr = newTarget->GetRecursive(&recursive);
        checkReturn(hr, L"IVssComponent:GetRecursive");

        hr = newTarget->GetAlternateLocation(&alternateLocation);
        checkReturn(hr, L"IVssComponent:GetAlternateLocation");

        // add it to the new-target list
        writerComponent.m_newTargets.push_back(TargetedFile(wstring(path), 
                                                                                              wstring(filespec), 
                                                                                              recursive, 
                                                                                              wstring(alternateLocation)));
    }
}

// verify that files in the component were restored properly.
// assumption is that the directory being restored to is empty if the checkExcluded parameter is true.
// currently, we have a very simple-minded approach to handle the wildcard case
// a more general solution will involve hashing files, and will be implemented if time is found
void TestWriter::verifyFilesRestored(IVssComponent* pComponent, const Component& writerComponent)
{
    WriterConfiguration* config = WriterConfiguration::instance();

    // no checking is being done.  Don't do anything
    if (!config->checkIncludes() && !config->checkExcludes())
        return;

    // for each file in the component
    VSS_RESTORE_TARGET target = writerComponent.m_restoreTarget;
    VSS_RESTOREMETHOD_ENUM method = config->restoreMethod().m_method;

    // build the list of all filespecs that need restoring
    vector<TargetedFile> componentFiles;
    buildComponentFiles(writerComponent, std::back_inserter(componentFiles));

    for (vector<TargetedFile>::iterator currentFile = componentFiles.begin();
            currentFile != componentFiles.end();
            ++currentFile)  {
            // --- figure out if there are any matching exclude files
            vector<File> excludeFiles;
           if (config->checkExcludes()) {
               buildContainer_if(config->excludeFiles().begin(),
                                         config->excludeFiles().end(),
                                         std::back_inserter(excludeFiles),
                                         std::bind2nd(std::ptr_fun(targetMatches), *currentFile));
            }
   
        // if there's no checking to be done for this filespec, continue
        if (excludeFiles.empty() && !config->checkIncludes())
            continue;

        // --- if there are new targets, look for one that references our file
        // --- if we find such a target, ensure that the file was restored there
        // --- NOTE: after the interface changes, there should be at most one matching
        // --- target. 
        
        vector<TargetedFile> targets;
        buildContainer_if(writerComponent.m_newTargets.begin(),
                                  writerComponent.m_newTargets.end(),
                                  std::back_inserter(targets),
                                  std::bind2nd(std::equal_to<File>(), *currentFile));

        if (targets.size() > 1) {
            wstringstream msg;
            msg << L"More than one new target matched filespec " <<
                        currentFile->toString() << std::endl << L"This is an illegal configuration";
            
            printStatus(msg.str());
        }

        if (!targets.empty()) {
            // create a function object to use for verification 
            VerifyFileAtLocation locationChecker(excludeFiles, pComponent, false);

            locationChecker(targets[0], *currentFile);      // TODO:  no longer need this fancy functor, since we're not doing a for_each
        }

        vector<TargetedFile> alternateLocations;
        buildContainer_if(config->restoreMethod().m_alternateLocations.begin(),
                                 config->restoreMethod().m_alternateLocations.end(),
                                 std::back_inserter(alternateLocations),
                                 std::bind2nd(std::equal_to<File>(), *currentFile));

        // --- NOTE: once again, interface changes mean we expect at most one
        assert(alternateLocations.size() <= 1);

        bool alternateRestore = !alternateLocations.empty() &&
            ((target == VSS_RT_ALTERNATE) || (method == VSS_RME_RESTORE_TO_ALTERNATE_LOCATION));
        
        if ((method == VSS_RME_RESTORE_IF_CAN_REPLACE) ||
            (method == VSS_RME_RESTORE_IF_NOT_THERE) ||
            alternateRestore)  {
            // --- in all of these cases, the backup application may restore to an alternate location.  

            // if we're not in either of the following two states, then the alternate location should only be used if there's
            // a matching element in the backup document.  Check to see if this is true
            // create a function object to use for verification 
            // TODO: we no longer need this fancy functor object since we're not doing a for_each
            if (!alternateLocations.empty())    {
                VerifyFileAtLocation locationChecker(excludeFiles, pComponent, 
                (target != VSS_RT_ALTERNATE) && (method != VSS_RME_RESTORE_TO_ALTERNATE_LOCATION));

                // check to ensure that the file has been restored to each matching alternate location
                // once again, this isn't quite correct, but good enough for now. More complicated
                // test scenarios will eventually break this.
                locationChecker(alternateLocations[0], *currentFile);
                }
        }

        // none of the above cases are true.  We need to check to see that the file is restored to its original location
        if ((method != VSS_RME_RESTORE_AT_REBOOT) && (method != VSS_RME_RESTORE_AT_REBOOT_IF_CANNOT_REPLACE) &&
             !alternateRestore)   {
        // create a function object to use for verification 
        VerifyFileAtLocation locationChecker(excludeFiles, pComponent, false);

        locationChecker(TargetedFile(currentFile->m_path, 
                                                   currentFile->m_filespec, 
                                                   currentFile->m_recursive, 
                                                   currentFile->m_path), 
                                                   *currentFile);
        }
    }
}


bool __cdecl TestWriter::isSubcomponent(ComponentBase sub, ComponentBase super)
{
    // if the components are the same, then return true
    if (super == sub)
        return true;

    wstring path = super.m_logicalPath;
    if (!path.empty() && path[path.size()  - 1] != L'\\')
        path+= L"\\";

    path += super.m_name;

    // if the supercomponent full path is the same as the subcomponent logical path, then true
    if (path == sub.m_logicalPath)
        return true;

    // otherwise, check for partial match
    return sub.m_logicalPath.find(path + L"\\") == 0;
}


bool  __cdecl TestWriter::targetMatches (File target, File file)
{
	assert(!file.m_filespec.empty());
	assert(!target.m_filespec.empty());
	
	// the filespec must match first of all
	if (!wildcardMatches(file.m_filespec, target.m_filespec))
		return false;

	// check the path
	if (file.m_recursive)	{
		if (!target.m_recursive)
			return target.m_path.find(file.m_path) == 0;
		else 
			return (target.m_path.find(file.m_path) == 0) ||(file.m_path.find(target.m_path) == 0);
	}	else	 	{
		if (!target.m_recursive)
			return file.m_path == target.m_path;
		else
			return file.m_path.find(target.m_path) == 0;
	}
}

// This helper function tests whether a component can be legally added to the backup document
bool __cdecl TestWriter::addableComponent(Component toAdd)
{
    WriterConfiguration* config = WriterConfiguration::instance();
    
    if (toAdd.m_selectable)
        return true;

    // see if there are any selectable ancestors
    vector<Component> ancestors;
    buildContainer_if(config->components().begin(),       
                              config->components().end(), 
                              std::back_inserter(ancestors), 
                              Utility::and1(std::bind2nd(std::ptr_fun(isSupercomponent), toAdd),
                                                 std::ptr_fun(isComponentSelectable)));

    return ancestors.empty();
}

// check to see if two wildcards match. 
// specifically, check to see whether the set of expansions of the first wildcard has a
// non-empty intersection with the set of expansions of the second wildcard.
// This function is not terribly efficient, but wildcards tend to be fairly short.
bool TestWriter::wildcardMatches(const wstring& first, const wstring& second)
{
	// if both string are empty, then they surely match
	if (first.empty() && second.empty())
		return true;

	// if we're done with the component, the wildcard better be terminated with '*' characters
	if (first.empty())	
		return (second[0] == L'*') && wildcardMatches(first, second.substr(1));
	if (second.empty())
		return (first[0] == L'*') && wildcardMatches(first.substr(1), second);	
	
	switch(first[0])	{
		case L'?':
			if (second[0] == L'*')	{
			      return wildcardMatches(first.substr(1), second) ||  // '*' matches character
				          wildcardMatches(first, second.substr(1));	// '*' matches nothing
			}

			// otherwise, the rest of the strings must match			
			return wildcardMatches(first.substr(1), second.substr(1));
		case L'*':
			return wildcardMatches(first, second.substr(1)) || // '*' matches character
				    wildcardMatches(first.substr(1), second);    // '*' matches nothing
		default:
			switch(second[0])	{
				case L'?':
					return wildcardMatches(first.substr(1), second.substr(1));
				case L'*':
					return wildcardMatches(first.substr(1), second) || // '*' matches character
						    wildcardMatches(first, second.substr(1));    // '*' matches nothing
				default:
					return (first[0] == second[0]) &&
						     wildcardMatches(first.substr(1), second.substr(1));
			}
	}
}

wstring TestWriter::VerifyFileAtLocation::verifyFileAtLocation(const File& file, const TargetedFile& location) const
{
    WriterConfiguration* config = WriterConfiguration::instance();

    // complicated set of assertions.  
    assert(!(file.m_recursive && !location.m_recursive) ||
               (location.m_path.find(file.m_path) == 0));
    assert(!(location.m_recursive && !file.m_recursive) ||
            (file.m_path.find(location.m_path) == 0));
    assert(!(file.m_recursive && location.m_recursive) ||
            ((file.m_path.find(location.m_path) == 0) || (location.m_path.find(file.m_path) == 0)));
    assert(!m_excluded.empty() || config->checkIncludes());
    assert(m_excluded.empty() || config->checkExcludes());
    
    // performant case where we don't have to walk any directory trees
    if (!file.m_recursive && !location.m_recursive && isExact(file.m_filespec)) {
        assert(m_excluded.size() <=  1);        // if not, the config file isn't set up right

        // --- if this is an alternate location mapping, only process it if there's a matching alternate location
        // --- in the backup document
        if (m_verifyAlternateLocation &&
             !verifyAlternateLocation(TargetedFile(file.m_path, file.m_filespec, false, location.m_alternatePath))) {
            return L"";
        }

        // --- ensure that the file has been restored, unless the file is excluded
        printStatus(wstring(L"\nChecking file ") +
                        location.m_alternatePath + file.m_filespec,
                        Utility::high);

        // check for error cases
        if (m_excluded.empty()) {
            if (::GetFileAttributes((location.m_alternatePath + file.m_filespec).c_str()) == INVALID_FILE_ATTRIBUTES)   {
                wstringstream msg;
                msg << L"\nThe file: " << std::endl << file.toString() << std::endl <<
                L"was not restored to location " << location.m_alternatePath;
                printStatus(msg.str(), Utility::low);

                return msg.str();
            }
        }   else if (::GetFileAttributes((location.m_alternatePath + file.m_filespec).c_str()) != INVALID_FILE_ATTRIBUTES)  {
                wstringstream msg;
                msg << L"\nThe file: " << file.m_path << file.m_filespec << 
                L" should have been excluded, but appears in location " << location.m_alternatePath;
                printStatus(msg.str(), Utility::low);

                return msg.str();
        }

        return L"";
    }

    std::queue<wstring> paths;

    // figure out what directory to start looking from
    wstring startPath = location.m_alternatePath;
    if (location.m_recursive && (file.m_path.find(location.m_path) == 0))
        startPath += file.m_path.substr(location.m_path.size());

    paths.push(startPath);

    // in the recursive case, files will hopefully be backed up high in the directory tree
    // consequently, we're going to walk the tree breadth-first
    printStatus(L"\nChecking that filespec was restored:", Utility::high);
    while (!paths.empty())  {
        wstring currentPath = paths.front();
        paths.pop();

        printStatus(wstring(L"      Checking directory: ") + currentPath, 
                        Utility::high);

        // for every file in the current directory (can't pass in filespec since we want to match all directories)
        WIN32_FIND_DATA findData;
        Utility::AutoFindFileHandle findHandle = ::FindFirstFile((currentPath + L"*").c_str(), &findData);
        if (findHandle == INVALID_HANDLE_VALUE)
            continue;

        do  {
            wstring currentName = findData.cFileName;
            std::transform(currentName.begin(), currentName.end(), currentName.begin(), towupper);

            if (currentName == L"." ||
            currentName == L"..")
                continue;

            // --- if the file is a directory
            if  (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)  {
                assert(!currentName.empty());
                if (currentName[currentName.size() - 1] != L'\\')
                    currentName += L"\\";

                // add it if necessary
                if (file.m_recursive)
                    paths.push(currentPath + currentName);

                    continue;       // skip to next file
            }

            printStatus(wstring(L"          Checking file: ") + currentName);

            // --- translate the path to what it would have been in the original tree
            assert(currentPath.find(location.m_alternatePath) == 0);
            wstring originalPath = file.m_path;
            if (file.m_recursive && (location.m_path.find(file.m_path) == 0))
                originalPath += location.m_path.substr(file.m_path.size());
            originalPath += currentPath.substr(location.m_alternatePath.size());

            // --- if this is an alternate location mapping, only process it if there's a matching 
            // --- alternate location mapping in the backup document
            if (m_verifyAlternateLocation &&
                !verifyAlternateLocation(TargetedFile(originalPath, currentName, false, currentPath)))	{	
                continue;
            }

            // --- find an exclude item that matches
            // --- if !config->checkExcluded(), m_excluded will be an empty container, and
            // --- std::find_if will return the end iterator
            vector<File>::const_iterator found = 
                    std::find_if(m_excluded.begin(), 
                                     m_excluded.end(),
                                     std::bind2nd(std::ptr_fun(targetMatches), File(originalPath, currentName, false)));

            // --- return if this is either an excluded file, or if we've found at least one matching include file
            if (found != m_excluded.end())  {
                wstringstream msg;
                msg << L"The file " << originalPath << currentName <<
                            L" should have been excluded, but appears in location " << currentPath;
                printStatus(msg.str(), Utility::low);

                return msg.str();
            }    else if (config->checkIncludes() &&
                              wildcardMatches(currentName, file.m_filespec))    {
                return L"";                             // declare success in cheesy case
            }

        }   while (::FindNextFile(findHandle, &findData));
    }

    if (config->checkIncludes())    {
        wstringstream msg;
        msg << L"None of the files specified by " << std::endl << file.toString() << std::endl <<
             L" were restored to location " << location.m_alternatePath;
        printStatus(msg.str(), Utility::low);
        return msg.str();
    }

    // we're only checking excludes, and we didn't find any violations
        return L"";
}


// verify that an alternate location mapping appears in the backup document
bool TestWriter::VerifyFileAtLocation::verifyAlternateLocation(const TargetedFile& writerAlt) const
{
	assert (isExact(writerAlt.m_filespec));
	assert(!writerAlt.m_recursive);
	
	unsigned int mappings = 0;
	HRESULT hr = m_pComponent->GetAlternateLocationMappingCount(&mappings);
	checkReturn(hr, L"IVssComponent::GetAlternateLocationMappingCount");

	for (unsigned int x = 0; x < mappings; x++)	{
		// get the current alternate location mapping
		CComPtr<IVssWMFiledesc> filedesc;
		hr = m_pComponent->GetAlternateLocationMapping(x, &filedesc);
		checkReturn(hr, L"IVssComponent::GetAlternateLocationMapping");

		// grab all relevant fields
		CComBSTR bstrPath, bstrFilespec, bstrAlternateLocation;

		hr  = filedesc->GetPath(&bstrPath);
		checkReturn(hr, L"IVssComponent::GetPath");
		if (bstrPath.Length() == 0)	{
			printStatus(L"An Alternate Location Mapping with an empty path was added to the backup document", 
			Utility::low);
			continue;
		}
				
		hr = filedesc->GetFilespec(&bstrFilespec);
		checkReturn(hr, L"IVssComponent::GetFilespec");
		if (bstrFilespec.Length() == 0)	{
			printStatus(L"An Alternate Location Mapping with an empty filespec was added to the backup document", 
			Utility::low);
			continue;
		}


		hr = filedesc->GetAlternateLocation(&bstrAlternateLocation);
		checkReturn(hr, L"IVssComponent::GetAlternateLocation");
		if (bstrAlternateLocation.Length() == 0)	{
			printStatus(L"An Alternate Location Mapping with an empty alternateLocation was added to the backup document", 
			Utility::low);
			continue;
		}

		bool recursive;
		hr = filedesc->GetRecursive(&recursive);
		checkReturn(hr, L"IVssComponent::GetRecursive");

		// convert the fields to uppercase and ensure paths are '\' terminated
		wstring path = bstrPath;
		std::transform(path.begin(), path.end(), path.begin(), towupper);
		if (path[path.size() - 1] != L'\\')
			path += L'\\';
		
		wstring filespec = bstrFilespec;
		std::transform(filespec.begin(), filespec.end(), filespec.begin(), towupper);

		wstring alternatePath = bstrAlternateLocation;
		std::transform(alternatePath.begin(), alternatePath.end(), alternatePath.begin(), towupper);
		
		if (alternatePath[alternatePath.size() - 1] != L'\\')
			alternatePath += L'\\';

		// check to see if that passed-in mapping is encompassed by the one in the backup document
		if (targetMatches(File(path, filespec, recursive), writerAlt))	{
			if (recursive)	{
				if (writerAlt.m_alternatePath.find(alternatePath) != 0)
					return false;

				assert(writerAlt.m_path.find(path) == 0);
				alternatePath += writerAlt.m_path.substr(path.size());
			}

			return alternatePath == writerAlt.m_alternatePath;
		}
	}

	return false;
}
// add the current error message to the PostRestoreFailureMsg
void TestWriter::VerifyFileAtLocation::saveErrorMessage(const wstring& message) const
{
	if (!message.empty())	{
		CComBSTR old;
		m_pComponent->GetPostRestoreFailureMsg(&old);
		wstring oldMessage = (old.Length() > 0) ? (BSTR)old : L"";
		m_pComponent->SetPostRestoreFailureMsg((oldMessage + wstring(L"\n") + message).c_str());					
	}
}

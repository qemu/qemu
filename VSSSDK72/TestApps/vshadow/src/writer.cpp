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


// Main header
#include "stdafx.h"



////////////////////////////////////////////////////////////////////////////////////
//  Main routines forwriter metadata/status gathering. 
//


// Gather writers metadata
void VssClient::GatherWriterMetadata()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"(Gathering writer metadata...)");

    // Gathers writer metadata
    // WARNING: this call can be performed only once per IVssBackupComponents instance!
    CComPtr<IVssAsync>  pAsync;
    CHECK_COM(m_pVssObject->GatherWriterMetadata(&pAsync));

    // Waits for the async operation to finish and checks the result
    WaitAndCheckForAsyncOperation(pAsync);

    ft.WriteLine(L"Initialize writer metadata ...");   

    // Initialize the internal metadata data structures
    InitializeWriterMetadata();
}


// Gather writers status
void VssClient::GatherWriterStatus()
{
    FunctionTracer ft(DBG_INFO);

    // Gathers writer status
    // WARNING: GatherWriterMetadata must be called before
    CComPtr<IVssAsync>  pAsync;
    CHECK_COM(m_pVssObject->GatherWriterStatus(&pAsync));

    // Waits for the async operation to finish and checks the result
    WaitAndCheckForAsyncOperation(pAsync);
}


void VssClient::InitializeWriterMetadata()
{
    FunctionTracer ft(DBG_INFO);

    // Get the list of writers in the metadata  
    unsigned cWriters = 0;
    CHECK_COM(m_pVssObject->GetWriterMetadataCount (&cWriters));

    // Enumerate writers
    for (unsigned iWriter = 0; iWriter < cWriters; iWriter++)
    {
        // Get the metadata for this particular writer
        VSS_ID idInstance = GUID_NULL;
        CComPtr<IVssExamineWriterMetadata> pMetadata;
        CHECK_COM(m_pVssObject->GetWriterMetadata(iWriter, &idInstance, &pMetadata));

        VssWriter   writer;
        writer.Initialize(pMetadata);

        // Add this writer to the list 
        m_writerList.push_back(writer);
    }
}


// Initialize the list of writers and components for restore
void VssClient::InitializeWriterComponentsForRestore()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Initializing writer components for restore ...");

    // Get the list of writers in the metadata  
    unsigned cWriters = 0;
    CHECK_COM(m_pVssObject->GetWriterComponentsCount(&cWriters));

    // Enumerate writers
    for (unsigned iWriter = 0; iWriter < cWriters; iWriter++)
    {
        // Get the selected components for this particular writer
        CComPtr<IVssWriterComponentsExt> pWriterComponents;
        CHECK_COM(m_pVssObject->GetWriterComponents(iWriter, &pWriterComponents));
        
        // Get writer identity. 
        // Ignore this writer if the real writer is not present in the system
        VSS_ID idInstance = GUID_NULL;
        VSS_ID idWriter = GUID_NULL;
        CHECK_COM(pWriterComponents->GetWriterInfo(
            &idInstance, 
            &idWriter
            ));

        wstring id = Guid2WString(idWriter);
        wstring instanceId = Guid2WString(idInstance);

        // Try to discover the name based on an existing writer (identical ID and instance ID).
        // Otherwise, ignore it!
        bool bFound = false;
        for (unsigned i = 0; i < m_writerList.size(); i++)
        {
            // Do Not check for instance ID ... 
            if (id != m_writerList[i].id)
                continue; 

            // Copy the information from the existing writer in the system
            VssWriter   writer = m_writerList[i];

            ft.WriteLine(L"- Writer %s is present in the Backup Components document and on the system. Considering for restore ...",
                writer.name.c_str());

            // Adding components
            writer.InitializeComponentsForRestore(pWriterComponents);

            // Add this writer object to the writer components list 
            m_writerComponentsForRestore.push_back(writer);

            // We found the writer!
            bFound = true;
        }

        if (!bFound)
        {
            ft.WriteLine(L"- Writer with ID %s is not present in the system! Ignoring ...", id.c_str());
        }
    }
}


// Lists the writer metadata
void VssClient::ListWriterMetadata(bool bListDetailedInfo)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Listing writer metadata ...");   
    
    // Enumerate writers
    for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
        m_writerList[iWriter].Print(bListDetailedInfo);
}



// Lists the status for all writers
void VssClient::ListWriterStatus()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Listing writer status ..."); 
    
    // Gets the number of writers in the gathered status info
    // (WARNING: GatherWriterStatus must be called before)
    unsigned cWriters = 0;
    CHECK_COM(m_pVssObject->GetWriterStatusCount(&cWriters));
    ft.WriteLine(L"- Number of writers that responded: %u", cWriters);  

    // Enumerate each writer
    for(unsigned iWriter = 0; iWriter < cWriters; iWriter++)
    {
        VSS_ID idInstance = GUID_NULL;
        VSS_ID idWriter= GUID_NULL;
        VSS_WRITER_STATE eWriterStatus = VSS_WS_UNKNOWN;
        CComBSTR bstrWriterName;
        HRESULT hrWriterFailure = S_OK;

        // Get writer status
        CHECK_COM(m_pVssObject->GetWriterStatus(iWriter,
                             &idInstance,
                             &idWriter,
                             &bstrWriterName,
                             &eWriterStatus,
                             &hrWriterFailure));

        // Print writer status
        ft.WriteLine(L"\n"
            L"* WRITER \"%s\"\n"
            L"   - Status: %d (%s)\n" 
            L"   - Writer Failure code: 0x%08lx (%s)\n" 
            L"   - Writer ID: " WSTR_GUID_FMT L"\n"
            L"   - Instance ID: " WSTR_GUID_FMT L"\n",
            (PWCHAR)bstrWriterName,
            eWriterStatus, GetStringFromWriterStatus(eWriterStatus).c_str(), 
            hrWriterFailure, FunctionTracer::HResult2String(hrWriterFailure).c_str(),
            GUID_PRINTF_ARG(idWriter),
            GUID_PRINTF_ARG(idInstance)
            );
    }
}
    

// Pre-restore 
void VssClient::PreRestore()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"\nSending the PreRestore event ... \n");

    // Gathers writer status
    // WARNING: GatherWriterMetadata must be called before
    CComPtr<IVssAsync>  pAsync;
    CHECK_COM(m_pVssObject->PreRestore(&pAsync));

    // Waits for the async operation to finish and checks the result
    WaitAndCheckForAsyncOperation(pAsync);
}



// Post-restore 
void VssClient::PostRestore()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"\nSending the PostRestore event ... \n");

    // Gathers writer status
    // WARNING: GatherWriterMetadata must be called before
    CComPtr<IVssAsync>  pAsync;
    CHECK_COM(m_pVssObject->PostRestore(&pAsync));

    // Waits for the async operation to finish and checks the result
    WaitAndCheckForAsyncOperation(pAsync);
}



// Convert a writer status into a string
wstring VssClient::GetStringFromWriterStatus(VSS_WRITER_STATE eWriterStatus)
{
    FunctionTracer ft(DBG_INFO);

    ft.Trace(DBG_INFO, L"Interpreting constant %d", (int)eWriterStatus);
    switch (eWriterStatus)
    {
    CHECK_CASE_FOR_CONSTANT(VSS_WS_STABLE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_FREEZE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_THAW);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_POST_SNAPSHOT);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_BACKUP_COMPLETE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_IDENTIFY);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_PREPARE_BACKUP);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_PREPARE_SNAPSHOT);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_FREEZE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_THAW);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_POST_SNAPSHOT);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_BACKUP_COMPLETE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_PRE_RESTORE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_POST_RESTORE);

    default:
        ft.WriteLine(L"Unknown constant: %d",eWriterStatus);
        _ASSERTE(false);
        return wstring(L"Undefined");
    }
}










////////////////////////////////////////////////////////////////////////////////////
//  VssWriter
//


// Initialize from a IVssWMFiledesc
void VssWriter::Initialize(IVssExamineWriterMetadata * pMetadata)
{
    FunctionTracer ft(DBG_INFO);

    // Get writer identity information
    VSS_ID idInstance = GUID_NULL;
    VSS_ID idWriter = GUID_NULL;
    CComBSTR bstrWriterName;
    VSS_USAGE_TYPE usage = VSS_UT_UNDEFINED;
    VSS_SOURCE_TYPE source= VSS_ST_UNDEFINED;
    CComBSTR bstrService;
    CComBSTR bstrUserProcedure;
    UINT iMappings;

    // Get writer identity
    CHECK_COM(pMetadata->GetIdentity (
        &idInstance, 
        &idWriter, 
        &bstrWriterName, 
        &usage, 
        &source
        ));

    // Get the restore method 
    CHECK_COM(pMetadata->GetRestoreMethod(
        &restoreMethod,
        &bstrService,
        &bstrUserProcedure,
        &writerRestoreConditions,
        &rebootRequiredAfterRestore,
        &iMappings
        ));

    // Initialize local members
    name = (LPWSTR)bstrWriterName;
    id = Guid2WString(idWriter);
    instanceId = Guid2WString(idInstance);
    supportsRestore = (writerRestoreConditions != VSS_WRE_NEVER);

    // Get file counts      
    unsigned cIncludeFiles = 0;
    unsigned cExcludeFiles = 0;
    unsigned cComponents = 0;
    CHECK_COM(pMetadata->GetFileCounts(&cIncludeFiles, &cExcludeFiles, &cComponents));
 
    // Get exclude files
    for(unsigned i = 0; i < cExcludeFiles; i++)
    {
        CComPtr<IVssWMFiledesc> pFileDesc;
        CHECK_COM(pMetadata->GetExcludeFile(i, &pFileDesc));

        // Add this descriptor to the list of excluded files
        VssFileDescriptor excludedFile;
        excludedFile.Initialize(pFileDesc, VSS_FDT_EXCLUDE_FILES);
        excludedFiles.push_back(excludedFile);
    }

    // Enumerate components
    for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
    {
        // Get component
        CComPtr<IVssWMComponent> pComponent;
        CHECK_COM(pMetadata->GetComponent(iComponent, &pComponent));

        // Add this component to the list of components
        VssComponent component;
        component.Initialize(name, pComponent);
        components.push_back(component);
    }

    // Discover toplevel components
    for(unsigned i = 0; i < cComponents; i++)
    {
        components[i].isTopLevel = true;
        for(unsigned j = 0; j < cComponents; j++)
            if (components[j].IsAncestorOf(components[i]))
                components[i].isTopLevel = false;
    }
}


// Initialize from a IVssWriterComponentsExt
void VssWriter::InitializeComponentsForRestore(IVssWriterComponentsExt * pWriterComponents)
{
    FunctionTracer ft(DBG_INFO);

    // Erase the current list of components for this writer
    components.clear();

    // Enumerate the components from the BC document
    unsigned cComponents = 0;
    CHECK_COM(pWriterComponents->GetComponentCount(&cComponents));

    // Enumerate components
    for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
    {
        // Get component
        CComPtr<IVssComponent> pComponent;
        CHECK_COM(pWriterComponents->GetComponent(iComponent, &pComponent));

        // Add this component to the list of components
        VssComponent component;
        component.Initialize(name, pComponent);

        ft.WriteLine(L"- Found component available for restore: \"%s\"", component.fullPath.c_str());

        components.push_back(component);
    }
}


// Prints the writer to the console
void VssWriter::Print(bool bListDetailedInfo)
{
    FunctionTracer ft(DBG_INFO);

    // Print writer identity information
    ft.WriteLine(L"\n"
        L"* WRITER \"%s\"\n"
        L"    - WriterId   = %s\n"
        L"    - InstanceId = %s\n"
        L"    - Supports restore events = %s\n"
        L"    - Writer restore conditions = %s\n"
        L"    - Restore method = %s\n"
        L"    - Requires reboot after restore = %s\n",
        name.c_str(),
        id.c_str(),
        instanceId.c_str(),
        BOOL2TXT(supportsRestore),
        GetStringFromRestoreConditions(writerRestoreConditions).c_str(),
        GetStringFromRestoreMethod(restoreMethod).c_str(),
        BOOL2TXT(rebootRequiredAfterRestore)
        );

    // Print exclude files
    ft.WriteLine(L"    - Excluded files:");
    for(unsigned i = 0; i < excludedFiles.size(); i++)
        excludedFiles[i].Print();

    // Enumerate components
    for(unsigned i = 0; i < components.size(); i++)
        components[i].Print(bListDetailedInfo);
}


// Convert a component type into a string
inline wstring VssWriter::GetStringFromRestoreMethod(VSS_RESTOREMETHOD_ENUM eRestoreMethod)
{
    FunctionTracer ft(DBG_INFO);

    ft.Trace(DBG_INFO, L"Interpreting constant %d", (int)eRestoreMethod);
    switch (eRestoreMethod)
    {
    CHECK_CASE_FOR_CONSTANT(VSS_RME_UNDEFINED);
    CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_IF_NOT_THERE);
    CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_IF_CAN_REPLACE);
    CHECK_CASE_FOR_CONSTANT(VSS_RME_STOP_RESTORE_START);
    CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_TO_ALTERNATE_LOCATION);
    CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_AT_REBOOT);
#ifdef VSS_SERVER
    CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_AT_REBOOT_IF_CANNOT_REPLACE);
#endif
    CHECK_CASE_FOR_CONSTANT(VSS_RME_CUSTOM);
                    
    default:
        ft.WriteLine(L"Unknown constant: %d",eRestoreMethod);
        _ASSERTE(false);
        return wstring(L"Undefined");
    }
}


// Convert a component type into a string
inline wstring VssWriter::GetStringFromRestoreConditions(VSS_WRITERRESTORE_ENUM eRestoreEnum)
{
    FunctionTracer ft(DBG_INFO);

    ft.Trace(DBG_INFO, L"Interpreting constant %d", (int)eRestoreEnum);
    switch (eRestoreEnum)
    {
    CHECK_CASE_FOR_CONSTANT(VSS_WRE_UNDEFINED);
    CHECK_CASE_FOR_CONSTANT(VSS_WRE_NEVER);
    CHECK_CASE_FOR_CONSTANT(VSS_WRE_IF_REPLACE_FAILS);
    CHECK_CASE_FOR_CONSTANT(VSS_WRE_ALWAYS);
                    
    default:
        ft.WriteLine(L"Unknown constant: %d",eRestoreEnum);
        _ASSERTE(false);
        return wstring(L"Undefined");
    }
}


////////////////////////////////////////////////////////////////////////////////////
//  VssComponent
//


// Initialize from a IVssWMComponent
void VssComponent::Initialize(wstring writerNameParam, IVssWMComponent * pComponent)
{
    FunctionTracer ft(DBG_INFO);

    writerName = writerNameParam;

    // Get the component info
    PVSSCOMPONENTINFO pInfo = NULL;
    CHECK_COM(pComponent->GetComponentInfo (&pInfo));

    // Initialize local members
    name = BSTR2WString(pInfo->bstrComponentName);
    logicalPath = BSTR2WString(pInfo->bstrLogicalPath);
    caption = BSTR2WString(pInfo->bstrCaption);
    type = pInfo->type;
    isSelectable = pInfo->bSelectable;
    notifyOnBackupComplete = pInfo->bNotifyOnBackupComplete;

    // Compute the full path
    fullPath = AppendBackslash(logicalPath) + name;
    if (fullPath[0] != L'\\')
        fullPath = wstring(L"\\") + fullPath;

    // Get file list descriptors
    for(unsigned i = 0; i < pInfo->cFileCount; i++)
    {
        CComPtr<IVssWMFiledesc> pFileDesc;
        CHECK_COM(pComponent->GetFile (i, &pFileDesc));

        VssFileDescriptor desc;
        desc.Initialize(pFileDesc, VSS_FDT_FILELIST);
        descriptors.push_back(desc);
    }
    
    // Get database descriptors
    for(unsigned i = 0; i < pInfo->cDatabases; i++)
    {
        CComPtr<IVssWMFiledesc> pFileDesc;
        CHECK_COM(pComponent->GetDatabaseFile (i, &pFileDesc));

        VssFileDescriptor desc;
        desc.Initialize(pFileDesc, VSS_FDT_DATABASE);
        descriptors.push_back(desc);
    }
    
    // Get log descriptors
    for(unsigned i = 0; i < pInfo->cLogFiles; i++)
    {
        CComPtr<IVssWMFiledesc> pFileDesc;
        CHECK_COM(pComponent->GetDatabaseLogFile (i, &pFileDesc));

        VssFileDescriptor desc;
        desc.Initialize(pFileDesc, VSS_FDT_DATABASE_LOG);
        descriptors.push_back(desc);
    }
    

#ifdef VSS_SERVER
    // Get dependencies
    for(unsigned i = 0; i < pInfo->cDependencies; i++)
    {
        CComPtr<IVssWMDependency> pDependency;
        CHECK_COM(pComponent->GetDependency(i, &pDependency));

        VssDependency dependency;
        dependency.Initialize(pDependency);
        dependencies.push_back(dependency);
    }
#endif


    pComponent->FreeComponentInfo (pInfo);

    // Compute the affected paths and volumes
    for(unsigned i = 0; i < descriptors.size(); i++)
    {
        if (!FindStringInList(descriptors[i].expandedPath, affectedPaths))
            affectedPaths.push_back(descriptors[i].expandedPath);

        if (!FindStringInList(descriptors[i].affectedVolume, affectedVolumes))
            affectedVolumes.push_back(descriptors[i].affectedVolume);
    }


    sort( affectedPaths.begin( ), affectedPaths.end( ) );
}


// Initialize from a IVssComponent
void VssComponent::Initialize(wstring writerNameParam, IVssComponent * pComponent)
{
    FunctionTracer ft(DBG_INFO);

    writerName = writerNameParam;

    // Get component type
    CHECK_COM(pComponent->GetComponentType(&type));

    // Get component name
    CComBSTR bstrComponentName; 
    CHECK_COM(pComponent->GetComponentName(&bstrComponentName));
    name = BSTR2WString(bstrComponentName);

    // Get component logical path
    CComBSTR bstrLogicalPath; 
    CHECK_COM(pComponent->GetLogicalPath(&bstrLogicalPath));
    logicalPath = BSTR2WString(bstrLogicalPath);

    // Compute the full path
    fullPath = AppendBackslash(logicalPath) + name;
    if (fullPath[0] != L'\\')
        fullPath = wstring(L"\\") + fullPath;
}


// Print summary/detalied information about this component
void VssComponent::Print(bool bListDetailedInfo)
{
    FunctionTracer ft(DBG_INFO);

    // Print writer identity information
    ft.WriteLine(L"    - Component \"%s\"\n"
        L"       - Name: '%s'\n"
        L"       - Logical Path: '%s'\n"
        L"       - Full Path: '%s'\n"
        L"       - Caption: '%s'\n"
        L"       - Type: %s [%d]\n"
        L"       - Is Selectable: '%s'\n"
        L"       - Is top level: '%s'\n"
        L"       - Notify on backup complete: '%s'",
        (writerName + L":" + fullPath).c_str(),
        name.c_str(),
        logicalPath.c_str(),
        fullPath.c_str(),
        caption.c_str(),
        GetStringFromComponentType(type).c_str(), type,
        BOOL2TXT(isSelectable),
        BOOL2TXT(isTopLevel),
        BOOL2TXT(notifyOnBackupComplete)
        );

    // Compute the affected paths and volumes
    if (bListDetailedInfo)
    {
        ft.WriteLine(L"       - Components:");
        for(unsigned i = 0; i < descriptors.size(); i++)
            descriptors[i].Print();
    }

    // Print the affected paths and volumes
    ft.WriteLine(L"       - Affected paths by this component:");
    for(unsigned i = 0; i < affectedPaths.size(); i++)
        ft.WriteLine(L"         - %s", affectedPaths[i].c_str());

    ft.WriteLine(L"       - Affected volumes by this component:");
    for(unsigned i = 0; i < affectedVolumes.size(); i++)
        ft.WriteLine(L"         - %s [%s]", 
            affectedVolumes[i].c_str(),
            GetDisplayNameForVolume(affectedVolumes[i]).c_str());

    // Print dependencies on Windows Server 2003
#ifdef VSS_SERVER

    ft.WriteLine(L"       - Component Dependencies:");
    for(unsigned i = 0; i < dependencies.size(); i++)
        dependencies[i].Print();

#endif 
}


// Convert a component type into a string
inline wstring VssComponent::GetStringFromComponentType(VSS_COMPONENT_TYPE eComponentType)
{
    FunctionTracer ft(DBG_INFO);

    ft.Trace(DBG_INFO, L"Interpreting constant %d", (int)eComponentType);
    switch (eComponentType)
    {
    CHECK_CASE_FOR_CONSTANT(VSS_CT_DATABASE);
    CHECK_CASE_FOR_CONSTANT(VSS_CT_FILEGROUP);
                    
    default:
        ft.WriteLine(L"Unknown constant: %d",eComponentType);
        _ASSERTE(false);
        return wstring(L"Undefined");
    }
}


// Return TRUE if the current component is parent of the given component
bool VssComponent::IsAncestorOf(VssComponent & descendent)
{
    // The child must have a longer full path
    if (descendent.fullPath.length() <= fullPath.length())
        return false;

    wstring fullPathAppendedWithBackslash = AppendBackslash(fullPath);
    wstring descendentPathAppendedWithBackslash = AppendBackslash(descendent.fullPath);

    // Return TRUE if the current full path is a prefix of the child full path
    return IsEqual(fullPathAppendedWithBackslash, 
        descendentPathAppendedWithBackslash.substr(0, 
            fullPathAppendedWithBackslash.length()));
}


// Return TRUE if the current component is parent of the given component
bool VssComponent::CanBeExplicitlyIncluded()
{
    if (isExcluded)
        return false;

    // selectable can be explictly included
    if (isSelectable) 
        return true;

    // Non-selectable top level can be explictly included
    if (isTopLevel)
        return true;

    return false;
}







////////////////////////////////////////////////////////////////////////////////////
//  VssFileDescriptor
//



// Initialize a file descriptor from a 
void VssFileDescriptor::Initialize(
        IVssWMFiledesc * pFileDesc, 
        VSS_DESCRIPTOR_TYPE typeParam
        )
{
    FunctionTracer ft(DBG_INFO);

    // Set the type
    type = typeParam;

    CComBSTR bstrPath;
    CHECK_COM(pFileDesc->GetPath(&bstrPath));

    CComBSTR bstrFilespec;
    CHECK_COM(pFileDesc->GetFilespec (&bstrFilespec));

    bool bRecursive = false;
    CHECK_COM(pFileDesc->GetRecursive(&bRecursive));

    CComBSTR bstrAlternate;
    CHECK_COM(pFileDesc->GetAlternateLocation(&bstrAlternate));

    // Initialize local data members
    path = BSTR2WString(bstrPath);
    filespec = BSTR2WString(bstrFilespec);
    expandedPath = bRecursive;
    path = BSTR2WString(bstrPath);

    // Get the expanded path
    expandedPath.resize(MAX_PATH, L'\0');
    _ASSERTE(bstrPath && bstrPath[0]);
    CHECK_WIN32(ExpandEnvironmentStringsW(bstrPath, (PWCHAR)expandedPath.c_str(), (DWORD)expandedPath.length()));
    expandedPath = AppendBackslash(expandedPath);

    // Get the affected volume 
    affectedVolume = GetUniqueVolumeNameForPath(expandedPath);
}


// Print a file description object
inline void VssFileDescriptor::Print()
{
    FunctionTracer ft(DBG_INFO);

    wstring alternateDisplayPath;
    if (alternatePath.length() > 0)
        alternateDisplayPath = wstring(L", Alternate Location = ") + alternatePath;
     
    ft.WriteLine(L"       - %s: Path = %s, Filespec = %s%s%s",
        GetStringFromFileDescriptorType(type).c_str(),
        path.c_str(),
        filespec.c_str(),
        isRecursive? L", Recursive": L"",
        alternateDisplayPath.c_str());
}


// Convert a component type into a string
wstring VssFileDescriptor::GetStringFromFileDescriptorType(VSS_DESCRIPTOR_TYPE eType)
{
    FunctionTracer ft(DBG_INFO);

    ft.Trace(DBG_INFO, L"Interpreting constant %d", (int)eType);
    switch (eType)
    {
    case VSS_FDT_UNDEFINED:     return L"Undefined";
    case VSS_FDT_EXCLUDE_FILES: return L"Exclude";
    case VSS_FDT_FILELIST:      return L"File List";
    case VSS_FDT_DATABASE:      return L"Database";
    case VSS_FDT_DATABASE_LOG:  return L"Database Log";
                    
    default:
        ft.WriteLine(L"Unknown constant: %d",eType);
        _ASSERTE(false);
        return wstring(L"Undefined");
    }
}


////////////////////////////////////////////////////////////////////////////////////
//  VssDependency
//

#ifdef VSS_SERVER


// Initialize a file descriptor from a 
void VssDependency::Initialize(
        IVssWMDependency * pDependency
        )
{
    FunctionTracer ft(DBG_INFO);

    VSS_ID guidWriterId;
    CHECK_COM(pDependency->GetWriterId(&guidWriterId));

    CComBSTR bstrLogicalPath;
    CHECK_COM(pDependency->GetLogicalPath(&bstrLogicalPath));

    CComBSTR bstrComponentName;
    CHECK_COM(pDependency->GetComponentName(&bstrComponentName));

    // Initialize local data members
    writerId = Guid2WString(guidWriterId);
    logicalPath = BSTR2WString(bstrLogicalPath);
    componentName = BSTR2WString(bstrComponentName);

    // Compute the full path
    fullPath = AppendBackslash(logicalPath) + componentName;
    if (fullPath[0] != L'\\')
        fullPath = wstring(L"\\") + fullPath;
}


// Print a file description object
inline void VssDependency::Print()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"       - Dependency to \"%s:%s%s\"",
        writerId.c_str(), 
        fullPath.c_str());
}

#endif

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
//  Main routines for writer component selection
//


// Select the maximum number of components such that their 
// file descriptors are pointing only to volumes to be shadow copied
void VssClient::SelectComponentsForBackup(
        vector<wstring> shadowSourceVolumes, 
        vector<wstring> excludedWriterAndComponentList,
        vector<wstring> includedWriterAndComponentList
        )
{
    FunctionTracer ft(DBG_INFO);

    // First, exclude all components that have data outside of the shadow set
    DiscoverDirectlyExcludedComponents(excludedWriterAndComponentList, m_writerList);

    // Then discover excluded components that have file groups outside the shadow set
    DiscoverNonShadowedExcludedComponents(shadowSourceVolumes);

    // Now, exclude all componenets that are have directly excluded descendents
    DiscoverAllExcludedComponents();

    // Next, exclude all writers that:
    // - either have a top-level nonselectable excluded component
    // - or do not have any included components (all its components are excluded)
    DiscoverExcludedWriters();

    // Now, discover the components that should be included (explicitly or implicitly)
    // These are the top components that do not have any excluded children
    DiscoverExplicitelyIncludedComponents();

    // Verify if the specified writers/components were included
    ft.WriteLine(L"Verifying explicitly specified writers/components ...");

    for(unsigned i = 0; i < includedWriterAndComponentList.size(); i++)
    {
        // Check whether a component or a writer is specified
        if (includedWriterAndComponentList[i].find(L':') != wstring::npos)
            VerifyExplicitelyIncludedComponent(includedWriterAndComponentList[i], m_writerList);
        else
            VerifyExplicitelyIncludedWriter(includedWriterAndComponentList[i], m_writerList);
    }

    // Finally, select the explicitly included components
    SelectExplicitelyIncludedComponents();
}


// Select the maximum number of components such that their 
// file descriptors are pointing only to volumes to be shadow copied
void VssClient::SelectComponentsForRestore(
        vector<wstring> excludedWriterAndComponentList,
        vector<wstring> includedWriterAndComponentList
        )
{
    FunctionTracer ft(DBG_INFO);

    // First, exclude all components that have data outside of the shadow set
    DiscoverDirectlyExcludedComponents(excludedWriterAndComponentList, m_writerComponentsForRestore);

    // Exclude all writers that do not support restore events
    ExcludeWritersWithNoRestoreEvents();

    // Verify if the specified writers/components were included
    ft.WriteLine(L"Verifying explicitly specified writers/components ...");

    for(unsigned i = 0; i < includedWriterAndComponentList.size(); i++)
    {
        // Check whether a component or a writer is specified
        if (includedWriterAndComponentList[i].find(L':') != wstring::npos)
            VerifyExplicitelyIncludedComponent(includedWriterAndComponentList[i], m_writerComponentsForRestore);
        else
            VerifyExplicitelyIncludedWriter(includedWriterAndComponentList[i], m_writerComponentsForRestore);
    }

    // Finally, select the explicitly included components
    SelectNonexcludedComponentsForRestore();
}


// Discover directly excluded components (that were excluded through the command-line)
void VssClient::DiscoverDirectlyExcludedComponents(
    vector<wstring> excludedWriterAndComponentList,
    vector<VssWriter> & writerList
)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Discover directly excluded components ...");

    // Discover components that should be excluded from the shadow set 
    // This means components that have at least one File Descriptor requiring 
    // volumes not in the shadow set. 
    for (unsigned iWriter = 0; iWriter < writerList.size(); iWriter++)
    {
        VssWriter & writer = writerList[iWriter];

        // Check if the writer is excluded
        if (FindStringInList(writer.name, excludedWriterAndComponentList) || 
            FindStringInList(writer.id, excludedWriterAndComponentList) ||
            FindStringInList(writer.instanceId, excludedWriterAndComponentList))
        {
            writer.isExcluded = true;
            continue;
        }

        // Check if the component is excluded
        for (unsigned iComponent = 0; iComponent < writer.components.size(); iComponent++)
        {
            VssComponent & component = writer.components[iComponent]; 

            // Check to see if this component is explicitly excluded

            // Compute various component paths
            // Format: Writer:logicaPath\componentName
            wstring componentPathWithWriterName = writer.name + L":" + component.fullPath;
            wstring componentPathWithWriterID = writer.id + L":" + component.fullPath;
            wstring componentPathWithWriterIID = writer.instanceId + L":" + component.fullPath;

            // Check to see if this component is explicitly excluded
            if (FindStringInList(componentPathWithWriterName, excludedWriterAndComponentList) || 
                FindStringInList(componentPathWithWriterID, excludedWriterAndComponentList) ||
                FindStringInList(componentPathWithWriterIID, excludedWriterAndComponentList))
            {
                ft.WriteLine(L"- Component '%s' from writer '%s' is explicitly excluded from backup ",
                    component.fullPath.c_str(), writer.name.c_str());

                component.isExcluded = true;
                continue;
            }
        }

        // Now, discover if we have any selected components. If none, exclude the whole writer
        bool nonExcludedComponents = false;
        for (unsigned i = 0; i < writer.components.size(); i++)
        {
            VssComponent & component = writer.components[i]; 

            if (!component.isExcluded)
                nonExcludedComponents = true;
        }

        // If all components are missing or excluded, then exclude the writer too
        if (!nonExcludedComponents)
        {
            ft.WriteLine(L"- Excluding writer '%s' since it has no selected components for restore.", writer.name.c_str());
            writer.isExcluded = true;
        }
    }
}


// Exclude writers that do not support restore events
void VssClient::ExcludeWritersWithNoRestoreEvents()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Exclude writers that do not support restore events ...");

    for (unsigned iWriter = 0; iWriter < m_writerComponentsForRestore.size(); iWriter++)
    {
        VssWriter & writer = m_writerComponentsForRestore[iWriter];

        // Check if the writer is excluded
        if (writer.isExcluded)
            continue;

        if (!writer.supportsRestore)
        {
            ft.WriteLine(L"- Excluding writer '%s' since it does not support restore events.", writer.name.c_str());
            writer.isExcluded = true;
        }
    }
}



// Discover excluded components that have file groups outside the shadow set
void VssClient::DiscoverNonShadowedExcludedComponents(
    vector<wstring> shadowSourceVolumes
)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Discover components that reside outside the shadow set ...");

    // Discover components that should be excluded from the shadow set 
    // This means components that have at least one File Descriptor requiring 
    // volumes not in the shadow set. 
    for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
    {
        VssWriter & writer = m_writerList[iWriter];

        // Check if the writer is excluded
        if (writer.isExcluded)
            continue;

        // Check if the component is excluded
        for (unsigned iComponent = 0; iComponent < writer.components.size(); iComponent++)
        {
            VssComponent & component = writer.components[iComponent]; 

            // Check to see if this component is explicitly excluded
            if (component.isExcluded)
                continue;

            // Try to find an affected volume outside the shadow set
            // If yes, exclude the component
            for (unsigned iVol = 0; iVol < component.affectedVolumes.size(); iVol++)
            {
                if (!FindStringInList(component.affectedVolumes[iVol], shadowSourceVolumes))
                {
                    ft.WriteLine(L"- Component '%s' from writer '%s' is excluded from backup "
                        L"(it requires %s in the shadow set)",
                        component.fullPath.c_str(), writer.name.c_str(), 
                        GetDisplayNameForVolume(component.affectedVolumes[iVol]).c_str());

                    component.isExcluded = true;
                    break;
                }
            }
        }
    }
}



// Discover the components that should not be included (explicitly or implicitly)
// These are componenets that are have directly excluded descendents
void VssClient::DiscoverAllExcludedComponents()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Discover all excluded components ...");

    // Discover components that should be excluded from the shadow set 
    // This means components that have at least one File Descriptor requiring 
    // volumes not in the shadow set. 
    for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
    {
        VssWriter & writer = m_writerList[iWriter];
        if (writer.isExcluded)
            continue;

        // Enumerate all components
        for (unsigned i = 0; i < writer.components.size(); i++)
        {
            VssComponent & component = writer.components[i]; 

            // Check if this component has any excluded children
            // If yes, deselect it
            for (unsigned j = 0; j < writer.components.size(); j++)
            {
                VssComponent & descendent = writer.components[j];
                if (component.IsAncestorOf(descendent) && descendent.isExcluded)
                {
                    ft.WriteLine(L"- Component '%s' from writer '%s' is excluded from backup "
                        L"(it has an excluded descendent: '%s')",
                        component.fullPath.c_str(), writer.name.c_str(), descendent.name.c_str());

                    component.isExcluded = true; 
                    break; 
                }
            }
        }
    }
}


// Discover excluded writers. These are writers that:
// - either have a top-level nonselectable excluded component
// - or do not have any included components (all its components are excluded)
void VssClient::DiscoverExcludedWriters()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Discover excluded writers ...");

    // Enumerate writers
    for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
    {
        VssWriter & writer = m_writerList[iWriter];
        if (writer.isExcluded)
            continue;

        // Discover if we have any:
        // - non-excluded selectable components 
        // - or non-excluded top-level non-selectable components
        // If we have none, then the whole writer must be excluded from the backup
        writer.isExcluded = true;
        for (unsigned i = 0; i < writer.components.size(); i++)
        {
            VssComponent & component = writer.components[i]; 
            if (component.CanBeExplicitlyIncluded())
            {
                writer.isExcluded = false;
                break;
            }
        }

        // No included components were found
        if (writer.isExcluded)
        {
            ft.WriteLine(L"- The writer '%s' is now entirely excluded from the backup:", writer.name.c_str());
            ft.WriteLine(L"  (it does not contain any components that can be potentially included in the backup)");
            continue;
        }

        // Now, discover if we have any top-level excluded non-selectable component 
        // If this is true, then the whole writer must be excluded from the backup
        for (unsigned i = 0; i < writer.components.size(); i++)
        {
            VssComponent & component = writer.components[i]; 

            if (component.isTopLevel && !component.isSelectable && component.isExcluded)
            {
                ft.WriteLine(L"- The writer '%s' is now entirely excluded from the backup:", writer.name.c_str());
                ft.WriteLine(L"  (the top-level non-selectable component '%s' is an excluded component)",
                    component.fullPath.c_str());
                writer.isExcluded = true;
                break;
            }
        }
    }
}




// Discover the components that should be explicitly included 
// These are any included top components 
void VssClient::DiscoverExplicitelyIncludedComponents()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Discover explicitly included components ...");

    // Enumerate all writers
    for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
    {
        VssWriter & writer = m_writerList[iWriter];
        if (writer.isExcluded)
            continue;

        // Compute the roots of included components
        for (unsigned i = 0; i < writer.components.size(); i++)
        {
            VssComponent & component = writer.components[i]; 

            if (!component.CanBeExplicitlyIncluded())
                continue;

            // Test if our component has a parent that is also included
            component.isExplicitlyIncluded = true;
            for (unsigned j = 0; j < writer.components.size(); j++)
            {
                VssComponent & ancestor = writer.components[j];
                if (ancestor.IsAncestorOf(component) && ancestor.CanBeExplicitlyIncluded())
                {
                    // This cannot be explicitely included since we have another 
                    // ancestor that that must be (implictely or explicitely) included
                    component.isExplicitlyIncluded = false; 
                    break; 
                }
            }
        }
    }
}


// Verify that the given components will be explicitly or implicitly selected
void VssClient::VerifyExplicitelyIncludedComponent(
    wstring includedComponent,
    vector<VssWriter> & writerList
    )
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"- Verifing component \"%s\" ...", includedComponent.c_str());

    // Enumerate all writers
    for (unsigned iWriter = 0; iWriter < writerList.size(); iWriter++)
    {
        VssWriter & writer = writerList[iWriter];

        // Ignore explicitly excluded writers
        if (writer.isExcluded)
            continue;

        // Find the associated component 
        for (unsigned j = 0; j < writer.components.size(); j++)
        {
            VssComponent & component = writer.components[j]; 

            // Ignore explicitly excluded components
            if (component.isExcluded)
                continue;

            // Compute various component paths
            // Format: Writer:logicaPath\componentName
            wstring componentPathWithWriterName = writer.name + L":" + component.fullPath;
            wstring componentPathWithWriterID = writer.id + L":" + component.fullPath;
            wstring componentPathWithWriterIID = writer.instanceId + L":" + component.fullPath;

            // Check to see if this component is (implicitly or explicitly) included
            if (IsEqual(componentPathWithWriterName, includedComponent) || 
                IsEqual(componentPathWithWriterID, includedComponent) ||
                IsEqual(componentPathWithWriterIID, includedComponent))
            {
                ft.Trace(DBG_INFO, L"- Found component '%s' from writer '%s'", 
                    component.fullPath.c_str(), writer.name.c_str());

                // If we are during restore, we just found our component
                if (m_bDuringRestore)
                {
                    ft.WriteLine(L"  - The component \"%s\" is selected", includedComponent.c_str());
                    return;
                }

                // If not explicitly included, check to see if there is an explicitly included ancestor
                bool isIncluded = component.isExplicitlyIncluded;
                if (!isIncluded)
                {
                    for (unsigned k = 0; k < writer.components.size(); k++)
                    {
                        VssComponent & ancestor = writer.components[k];
                        if (ancestor.IsAncestorOf(component) && ancestor.isExplicitlyIncluded)
                        {
                            isIncluded = true;
                            break;
                        }
                    }
                }

                if (isIncluded)
                {
                    ft.WriteLine(L"  - The component \"%s\" is selected", includedComponent.c_str());
                    return;
                }
                else
                {
                    ft.WriteLine(L"ERROR: The component \"%s\" was not included in the backup! Aborting backup ...", includedComponent.c_str());
                    ft.WriteLine(L"- Please reveiw the component/subcomponent definitions");
                    ft.WriteLine(L"- Also, please verify list of volumes to be shadow copied.");

                    throw(E_INVALIDARG);
                }
            }
        }
    }

    ft.WriteLine(L"ERROR: The component \"%s\" was not found in the writer components list! Aborting backup ...", includedComponent.c_str());
    ft.WriteLine(L"- Please check the syntax of the component name.");

    throw(E_INVALIDARG);
}



// Verify that all the components of this writer are selected
void VssClient::VerifyExplicitelyIncludedWriter(
    wstring writerName,
    vector<VssWriter> & writerList
    )
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"- Verifing that all components of writer \"%s\" are included in backup ...", writerName.c_str());

    // Enumerate all writers
    for (unsigned iWriter = 0; iWriter < writerList.size(); iWriter++)
    {
        VssWriter & writer = writerList[iWriter];

        // Ignore explicitly excluded writers
        if (writer.isExcluded)
            continue;

        // Check if we found the writer
        if (IsEqual(writerName, writer.name) 
            || IsEqual(writerName, writer.id) 
            || IsEqual(writerName, writer.instanceId))
        {
            if (writer.isExcluded)
            {
                ft.WriteLine(L"ERROR: The writer \"%s\" was not included in the backup! Aborting backup ...", writer.name.c_str());
                ft.WriteLine(L"- Please reveiw the component/subcomponent definitions");
                ft.WriteLine(L"- Also, please verify list of volumes to be shadow copied.");
                throw(E_INVALIDARG);
            }

            // Make sure all its associated components are selected
            for (unsigned j = 0; j < writer.components.size(); j++)
            {
                VssComponent & component = writer.components[j]; 

                if (component.isExcluded)
                {
                    ft.WriteLine(L"ERROR: The writer \"%s\" has components not included in the backup! Aborting backup ...", writer.name.c_str());
                    ft.WriteLine(L"- The component \"%s\" was not included in the backup.", component.fullPath.c_str());
                    ft.WriteLine(L"- Please reveiw the component/subcomponent definitions");
                    ft.WriteLine(L"- Also, please verify list of volumes to be shadow copied.");
                    throw(E_INVALIDARG);
                }
            }

            ft.WriteLine(L"   - All components from writer \"%s\" are selected", writerName.c_str());
            return; 
        }
    }

    ft.WriteLine(L"ERROR: The writer \"%s\" was not found! Aborting backup ...", writerName.c_str());
    ft.WriteLine(L"- Please check the syntax of the writer name/id.");

    throw(E_INVALIDARG);
}



// Discover the components that should be explicitly included 
// These are any included top components 
void VssClient::SelectExplicitelyIncludedComponents()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Select explicitly included components ...");

    // Enumerate all writers
    for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
    {
        VssWriter & writer = m_writerList[iWriter];
        if (writer.isExcluded)
            continue;

        ft.WriteLine(L" * Writer '%s':", writer.name.c_str());

        // Compute the roots of included components
        for (unsigned i = 0; i < writer.components.size(); i++)
        {
            VssComponent & component = writer.components[i]; 

            if (!component.isExplicitlyIncluded)
                continue;

            ft.WriteLine(L"   - Add component %s", component.fullPath.c_str());

            // Add the component
            CHECK_COM(m_pVssObject->AddComponent(
                WString2Guid(writer.instanceId),
                WString2Guid(writer.id),
                component.type,
                component.logicalPath.c_str(),
                component.name.c_str()));
        }
    }
}


// Select non excluded components for restore
void VssClient::SelectNonexcludedComponentsForRestore()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Select components for restore...");

    // Enumerate all writers
    for (unsigned iWriter = 0; iWriter < m_writerComponentsForRestore.size(); iWriter++)
    {
        VssWriter & writer = m_writerComponentsForRestore[iWriter];

        // Ignore explicitly excluded writers
        if (writer.isExcluded)
            continue;

        ft.WriteLine(L" * Writer '%s':", writer.name.c_str());

        // Compute the roots of included components
        for (unsigned i = 0; i < writer.components.size(); i++)
        {
            VssComponent & component = writer.components[i]; 

            // Do not select excluded components
            if (component.isExcluded)
                continue;

            ft.WriteLine(L"   - Select component %s", component.fullPath.c_str());

            // Select the component for restore
            CHECK_COM(m_pVssObject->SetSelectedForRestore(
                WString2Guid(writer.id),
                component.type,
                component.logicalPath.c_str(),
                component.name.c_str(),
                true));
        }
    }
}



// Notify the writer on the restore status
void VssClient::SetFileRestoreStatus(bool bSuccesfullyRestored)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Set restore status for all components components for restore...");

    //
    // All-or-nothing policy
    //
    // WARNING: this might be insufficient since we cannot distinguish
    // between a partial failed restore and a completely failed restore!
    // A true requestor should be able to make this difference (see the documentation for more details)
    VSS_FILE_RESTORE_STATUS restoreStatus = bSuccesfullyRestored? VSS_RS_ALL: VSS_RS_NONE;

    // Enumerate all writers
    for (unsigned iWriter = 0; iWriter < m_writerComponentsForRestore.size(); iWriter++)
    {
        VssWriter & writer = m_writerComponentsForRestore[iWriter];
        if (writer.isExcluded)
            continue;

        ft.WriteLine(L" * Writer '%s':", writer.name.c_str());

        // Compute the roots of included components
        for (unsigned i = 0; i < writer.components.size(); i++)
        {
            VssComponent & component = writer.components[i]; 

            // Do not select excluded components
            if (component.isExcluded)
                continue;

            ft.WriteLine(L"   - Select component %s", component.fullPath.c_str());

            // Select the component for restore
            CHECK_COM(m_pVssObject->SetFileRestoreStatus(
                WString2Guid(writer.id),
                component.type,
                component.logicalPath.c_str(),
                component.name.c_str(),
                restoreStatus)
                );
        }
    }
}



// Returns TRUE if the writer was previously selected
bool VssClient::IsWriterSelected(GUID guidInstanceId)
{
    // If this writer was not selected for backup, ignore it
    wstring instanceId = Guid2WString(guidInstanceId);
    for (unsigned i = 0; i < m_writerList.size(); i++)
        if ( (instanceId == m_writerList[i].instanceId) && !m_writerList[i].isExcluded)
            return true;

    return false;
}



// Check the status for all selected writers
void VssClient::CheckSelectedWriterStatus()
{
    FunctionTracer ft(DBG_INFO);

    // Gather writer status to detect potential errors
    GatherWriterStatus();
    
    // Gets the number of writers in the gathered status info
    // (WARNING: GatherWriterStatus must be called before)
    unsigned cWriters = 0;
    CHECK_COM(m_pVssObject->GetWriterStatusCount(&cWriters));

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

        // If the writer is not selected, just continue
        if (!IsWriterSelected(idInstance))
            continue;

        // If the writer is in non-stable state, break
        switch(eWriterStatus)
        {
            case VSS_WS_FAILED_AT_IDENTIFY:
            case VSS_WS_FAILED_AT_PREPARE_BACKUP:
            case VSS_WS_FAILED_AT_PREPARE_SNAPSHOT:
            case VSS_WS_FAILED_AT_FREEZE:
            case VSS_WS_FAILED_AT_THAW:
            case VSS_WS_FAILED_AT_POST_SNAPSHOT:
            case VSS_WS_FAILED_AT_BACKUP_COMPLETE:
            case VSS_WS_FAILED_AT_PRE_RESTORE:
            case VSS_WS_FAILED_AT_POST_RESTORE:
#ifdef VSS_SERVER
            case VSS_WS_FAILED_AT_BACKUPSHUTDOWN:
#endif
                break;

            default:
                continue;
        }

        // Print writer status
        ft.WriteLine(L"\n"
            L"ERROR: Selected writer '%s' is in failed state!\n"
            L"   - Status: %d (%s)\n" 
            L"   - Writer Failure code: 0x%08lx (%s)\n" 
            L"   - Writer ID: " WSTR_GUID_FMT L"\n"
            L"   - Instance ID: " WSTR_GUID_FMT L"\n",
            (PWCHAR)bstrWriterName,
            eWriterStatus, GetStringFromWriterStatus(eWriterStatus).c_str(), 
            hrWriterFailure,FunctionTracer::HResult2String(hrWriterFailure).c_str(),
            GUID_PRINTF_ARG(idWriter),
            GUID_PRINTF_ARG(idInstance)
            );

        // Stop here
        throw(E_UNEXPECTED);
    }
}
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



void VssClient::CreateSnapshotSet(
    vector<wstring> volumeList, 
    wstring outputXmlFile,     
    vector<wstring> excludedWriterList,
    vector<wstring> includedWriterList
    )
{
    FunctionTracer ft(DBG_INFO);

    bool bSnapshotWithWriters = ((m_dwContext & VSS_VOLSNAP_ATTR_NO_WRITERS) == 0);

    // Gather writer metadata
    if (bSnapshotWithWriters)
        GatherWriterMetadata();

    // Select writer components based on the given shadow volume list
    if (bSnapshotWithWriters)
        SelectComponentsForBackup(volumeList, excludedWriterList, includedWriterList);

    // Start the shadow set
    CHECK_COM(m_pVssObject->StartSnapshotSet(&m_latestSnapshotSetID))
    ft.WriteLine(L"Creating shadow set " WSTR_GUID_FMT L" ...", GUID_PRINTF_ARG(m_latestSnapshotSetID));

    // Add the specified volumes to the shadow set
    AddToSnapshotSet(volumeList);

    // Prepare for backup. 
    // This will internally create the backup components document with the selected components
    if (bSnapshotWithWriters)
        PrepareForBackup();

    // Creates the shadow set 
    DoSnapshotSet();

    // Saves the backup components document, if needed
    if (outputXmlFile.length() > 0)
        SaveBackupComponentsDocument(outputXmlFile);

    // List all the created shadow copies
    if ((m_dwContext & VSS_VOLSNAP_ATTR_TRANSPORTABLE) == 0)
    {
        ft.WriteLine(L"\nList of created shadow copies: \n");
        QuerySnapshotSet(m_latestSnapshotSetID);
    }
}


// Prepare the shadow for backup
void VssClient::PrepareForBackup()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Preparing for backup ... ");

    CComPtr<IVssAsync>  pAsync;
    CHECK_COM(m_pVssObject->PrepareForBackup(&pAsync));

    // Waits for the async operation to finish and checks the result
    WaitAndCheckForAsyncOperation(pAsync);

    // Check selected writer status
    CheckSelectedWriterStatus();
}


// Add volumes to the shadow set
void VssClient::AddToSnapshotSet(vector<wstring> volumeList)
{
    FunctionTracer ft(DBG_INFO);

    // Preserve the list of volumes for script generation 
    m_latestVolumeList = volumeList;

    _ASSERTE(m_latestSnapshotIdList.size() == 0);

    // Add volumes to the shadow set 
    for (unsigned i = 0; i < volumeList.size(); i++)
    {
        wstring volume = volumeList[i];
        ft.WriteLine(L"- Adding volume %s [%s] to the shadow set...", 
            volume.c_str(),
            GetDisplayNameForVolume(volume).c_str());

        VSS_ID SnapshotID;
        CHECK_COM(m_pVssObject->AddToSnapshotSet((LPWSTR)volume.c_str(), GUID_NULL, &SnapshotID));

        // Preserve this shadow ID for script generation 
        m_latestSnapshotIdList.push_back(SnapshotID);
    }
}



// Effectively creating the shadow (calling DoSnapshotSet)
void VssClient::DoSnapshotSet()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Creating the shadow (DoSnapshotSet) ... ");

    CComPtr<IVssAsync>  pAsync;
    CHECK_COM(m_pVssObject->DoSnapshotSet(&pAsync));

    // Waits for the async operation to finish and checks the result
    WaitAndCheckForAsyncOperation(pAsync);

    // Check selected writer status
    CheckSelectedWriterStatus();

    ft.WriteLine(L"Shadow copy set succesfully created.");
}


// Ending the backup (calling BackupComplete)
void VssClient::BackupComplete(bool succeeded)
{
    FunctionTracer ft(DBG_INFO);

    if (succeeded)
        ft.WriteLine(L"- Mark all writers as succesfully backed up... ");
    else
        ft.WriteLine(L"- Backup failed. Mark all writers as not succesfully backed up... ");

    SetBackupSucceeded(succeeded);

    ft.WriteLine(L"Completing the backup (BackupComplete) ... ");

    CComPtr<IVssAsync>  pAsync;
    CHECK_COM(m_pVssObject->BackupComplete(&pAsync));

    // Waits for the async operation to finish and checks the result
    WaitAndCheckForAsyncOperation(pAsync);

    // Check selected writer status
    CheckSelectedWriterStatus();

}


// Save the backup components document
void VssClient::SaveBackupComponentsDocument(wstring fileName)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Saving the backup components document ... ");

    // Get the Backup Components in XML format
    CComBSTR bstrXML;
    CHECK_COM(m_pVssObject->SaveAsXML(&bstrXML));

    // Save the XML string to the file
    WriteFile(fileName, BSTR2WString(bstrXML));
}


// Generate the SETVAR script
// This is useful for management operations
void VssClient::GenerateSetvarScript(wstring stringFileName)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Generating the SETVAR script (%s) ... ", stringFileName.c_str());

    wofstream ofile;
    ofile.open(WString2String(stringFileName).c_str());
    ofile << L"@echo.\n";
    ofile << L"@echo [This script is generated by VSHADOW.EXE for the shadow set " << Guid2WString(m_latestSnapshotSetID).c_str() << L"]\n";
    ofile << L"@echo.\n\n";

    wstring snapshotSetID = Guid2WString(m_latestSnapshotSetID);
    ofile << L"SET SHADOW_SET_ID=" << snapshotSetID.c_str() << L"\n";
    
    // For each added volume add the VSHADOW.EXE exposure command
    for (unsigned i = 0; i < m_latestSnapshotIdList.size(); i++)
    {
        wstring snapshotID = Guid2WString(m_latestSnapshotIdList[i]);
        ofile << L"SET SHADOW_ID_" << i+1 << L"=" << snapshotID.c_str() << L"\n";

        // Get shadow copy device (if the snapshot is there)
        if ((m_dwContext & VSS_VOLSNAP_ATTR_TRANSPORTABLE) == 0)
        {
            VSS_SNAPSHOT_PROP Snap;
            CHECK_COM(m_pVssObject->GetSnapshotProperties(WString2Guid(snapshotID), &Snap));

            // Automatically call VssFreeSnapshotProperties on this structure at the end of scope
            CAutoSnapPointer snapAutoCleanup(&Snap);

            ofile << L"SET SHADOW_DEVICE_" << i+1 << L"=" << Snap.m_pwszSnapshotDeviceObject << L"\n";
        }
    }

    ofile.close();
}


// Import the shadow set
void VssClient::ImportSnapshotSet()
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"Importing the transportable snapshot set ... ");

    CComPtr<IVssAsync>  pAsync;
    CHECK_COM(m_pVssObject->ImportSnapshots(&pAsync));

    // Waits for the async operation to finish and checks the result
    WaitAndCheckForAsyncOperation(pAsync);

    ft.WriteLine(L"Shadow copy set succesfully imported.");
}


// Marks all selected components as succeeded for backup
void VssClient::SetBackupSucceeded(bool succeeded)
{
    FunctionTracer ft(DBG_INFO);

    // Enumerate writers
    for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
    {
        VssWriter & writer = m_writerList[iWriter];

        // Enumerate components
        for(unsigned iComponent = 0; iComponent < writer.components.size(); iComponent++)
        {
            VssComponent & component = writer.components[iComponent]; 

            // Test that the component is explicitely selected and requires notification
            if (!component.isExplicitlyIncluded || !component.notifyOnBackupComplete)
                continue;

            // Call SetBackupSucceeded for this component
            CHECK_COM(m_pVssObject->SetBackupSucceeded(
                WString2Guid(writer.instanceId),
                WString2Guid(writer.id),
                component.type,
                component.logicalPath.c_str(),
                component.name.c_str(),
                succeeded));
        }
    }
}



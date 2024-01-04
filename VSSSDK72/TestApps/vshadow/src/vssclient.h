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
//  Regular VSS client class 
//
//  This class implements a high-level VSS API. It is not dependent on 
//  VSHADOW.EXE command-line interface - It can be called from an UI program as well.
//

class VssClient
{
public:

    // Constructor
    VssClient();

    // Destructor
    ~VssClient();

    // Initialize the internal pointers
    void Initialize(DWORD dwContext, wstring xmlDoc = L"", bool bDuringRestore = false);

    //
    //  Shadow copy creation related methods
    //

    // Method to create a shadow copy set with the given volumes
    void CreateSnapshotSet(
        vector<wstring> volumeList, 
        wstring outputXmlFile,     
        vector<wstring> excludedWriterList,
        vector<wstring> includedWriterList
        );

    // Prepare the shadow copy for backup
    void PrepareForBackup();

    // Add volumes to the shadow copy set
    void AddToSnapshotSet(vector<wstring> volumeList);
    
    // Effectively creating the shadow copy (calling DoSnapshotSet)
    void DoSnapshotSet();

    // Ending the backup (calling BackupComplete)
    void BackupComplete(bool succeeded);

    // Save the backup components document
    void SaveBackupComponentsDocument(wstring fileName);

    // Import the snapshot set
    void ImportSnapshotSet();

    // Generate the SETVAR script for this shadow copy set
    void GenerateSetvarScript(wstring stringFileName);

    // Marks all selected components as succeeded for backup
    void SetBackupSucceeded(bool succeeded);

    //
    //  Shadow copy query related methods
    //

    // Query all the shadow copies in the given set
    // If snapshotSetID is NULL, just query all shadow copies in the system
    void QuerySnapshotSet(VSS_ID snapshotSetID);

    // Query the properties of the given shadow copy
    void GetSnapshotProperties(VSS_ID snapshotID);

    // Print the properties for the given snasphot
    void PrintSnapshotProperties(VSS_SNAPSHOT_PROP & prop);

    //
    //  Shadow copy deletion related methods
    //
    
    // Delete all the shadow copies in the system
    void DeleteAllSnapshots();

    // Delete the given shadow copy set 
    void DeleteSnapshotSet(VSS_ID snapshotSetID);

    // Delete the given shadow copy
    void DeleteSnapshot(VSS_ID snapshotID);

    // Delete the oldest shadow copy on a volume
    void DeleteOldestSnapshot(const wstring& stringVolumeName);

    //
    //  Shadow copy break related methods
    //

    // Break the given shadow copy set 
    void BreakSnapshotSet(VSS_ID snapshotSetID, bool makeReadWrite, vector<wstring> *pVolumeNames=NULL);

    // Make the volumes in this list read-write using VDS API
    void MakeVolumesReadWrite(vector<wstring> volumeNames);
    
    void RevertToSnapshot(VSS_ID snapshotID);

    // Return the list of shadow copy volume devices in this shadow copy set
    vector<wstring> GetSnapshotDevices(VSS_ID SnapshotSetID);
    
    // Returns an array of enumerated VDS objects
    vector< CComPtr<IUnknown> > EnumerateVdsObjects(IEnumVdsObject * pEnumeration);

    //
    //  Expose related methods
    //

    // Expose a shadow copy locally
    void ExposeSnapshotLocally(VSS_ID snapshotID, wstring path);

    // Expose a shadow copy remotely
    void ExposeSnapshotRemotely(VSS_ID snapshotID, wstring shareName, wstring pathFromRoot);


    //
    //  Writer-related methods
    //

    // Gather writer metadata
    void GatherWriterMetadata();

    // Gather writer status
    void GatherWriterStatus();

    // Initialize writer metadata
    void InitializeWriterMetadata();

    // Initialize the list of writers and components for restore
    void InitializeWriterComponentsForRestore();

    // List gathered writer metadata
    void ListWriterMetadata(bool bListDetailedInfo);

    // List gathered writer status
    void ListWriterStatus();

    // Pre restore
    void PreRestore();

    // Post restore
    void PostRestore();

    // Get writer status as string
    wstring GetStringFromWriterStatus(VSS_WRITER_STATE eWriterStatus);

    //
    //  Writer/Component selection-related methods
    //


    // Select the maximum number of components such that their 
    // file descriptors are pointing only to volumes to be shadow copied
    void SelectComponentsForBackup(
        vector<wstring> shadowSourceVolumes, 
        vector<wstring> excludedWriterAndComponentList,
        vector<wstring> includedWriterAndComponentList
        );

    // Select the maximum number of components for restore
    void SelectComponentsForRestore(
        vector<wstring> excludedWriterAndComponentList,
        vector<wstring> includedWriterAndComponentList
        );

    // Discover directly excluded components (that were excluded through the command-line)
    void DiscoverDirectlyExcludedComponents(
        vector<wstring> excludedWriterAndComponentList,
        vector<VssWriter> & writerList
    );

    // Discover excluded components that have file groups outside the shadow set
    void DiscoverNonShadowedExcludedComponents(
        vector<wstring> shadowSourceVolumes
    );

    // Discover the components that should not be included (explicitly or implicitly)
    // These are componenets that are have directly excluded descendents
    void DiscoverAllExcludedComponents();

    // Discover excluded writers. These are writers that:
    // - either have a top-level nonselectable excluded component
    // - or do not have any included components (all its components are excluded)
    void DiscoverExcludedWriters();

    // Discover the components that should be explicitly included 
    // These are any included top components 
    void DiscoverExplicitelyIncludedComponents();

    // Exclude writers that do not support restore events
    void ExcludeWritersWithNoRestoreEvents();

    // Select explicitly included components
    void SelectExplicitelyIncludedComponents();

    // Select explicitly included components
    void SelectNonexcludedComponentsForRestore();

    // Set the restore status for all components
    void SetFileRestoreStatus(bool bSuccesfullyRestored);

    // Verify that the given components will be explicitly or implicitly selected
    void VerifyExplicitelyIncludedComponent(
        wstring includedComponent,
        vector<VssWriter> & writerList
        );

    // Verify that all the components of this writer are selected
    void VerifyExplicitelyIncludedWriter(
        wstring writerName,
        vector<VssWriter> & writerList
        );

    // Is this writer part of the backup?
    // (i.e. if it was not previously excluded)
    bool IsWriterSelected(GUID guidInstanceId);

    // Check the status for all selected writers
    void CheckSelectedWriterStatus();


private:

    // Waits for the async operation to finish
    void WaitAndCheckForAsyncOperation(IVssAsync*  pAsync);



private:
    
    //
    //  Data members
    //

    // TRUE if CoInitialize() was already called 
    // Needed to pair each succesfull call to CoInitialize with a corresponding CoUninitialize
    bool                            m_bCoInitializeCalled;

    // VSS context
    DWORD                           m_dwContext;

    // The IVssBackupComponents interface is automatically released when this object is destructed.
    // Needed to issue VSS calls 
    CComPtr<IVssBackupComponents>   m_pVssObject;

    // List of selected writers during the shadow copy creation process
    vector<wstring>                 m_latestVolumeList;

    // List of shadow copy IDs from the latest shadow copy creation process
    vector<VSS_ID>                  m_latestSnapshotIdList;

    // Latest shadow copy set ID
    VSS_ID                          m_latestSnapshotSetID;

    // List of writers
    vector<VssWriter>               m_writerList;

    // List of selected writers/componnets from the previous backup components document
    vector<VssWriter>               m_writerComponentsForRestore;

    // TRUE if we are during restore
    bool                            m_bDuringRestore;
};


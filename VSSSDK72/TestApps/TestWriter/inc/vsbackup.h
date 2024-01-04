/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    vsbackup.h

Abstract:

    Declaration of backup interfaces IVssExamineWriterMetadata and
	IVssBackupComponents, IVssWMComponent

	Brian Berkowitz  [brianb]  3/13/2000


TBD:
	
	Add comments.

Revision History:

    Name        Date        Comments
    brianb      03/30/2000  Created
    brianb		04/18/2000  Added IVssCancelCallback
    brianb		05/03/2000  Changed IVssWriter::Initialize method
    brianb		05/16/2000  Remove cancel stuff
    mikejohn		05/24/2000  Changed parameters on SimulateXxxx() calls
    mikejohn		09/18/2000  176860: Added calling convention methods where missing
    ssteiner    11/10/2000  143801 MOve SimulateSnashotXxxx() calls to be hosted by VssSvc

--*/

#ifndef _VSBACKUP_H_
#define _VSBACKUP_H_

// description of a component
typedef struct _VSS_COMPONENTINFO
	{
	VSS_COMPONENT_TYPE type;	// either VSS_CT_DATABASE or VSS_CT_FILEGROUP
	BSTR bstrLogicalPath;		// logical path to component
	BSTR bstrComponentName;		// component name
	BSTR bstrCaption;		// description of component
	BYTE *pbIcon;			// icon
	UINT cbIcon;			// icon
	bool bRestoreMetadata;		// whether component supplies restore metadata
	bool bNotifyOnBackupComplete;	// whether component needs to be informed if backup was successful
	bool bSelectable;		// is component selectable	
	bool bSelectableForRestore; // is component selectable for restore
	DWORD dwComponentFlags;	// extra attribute flags for the component
	UINT cFileCount;		// # of files in file group
	UINT cDatabases;		// # of database files
	UINT cLogFiles;			// # of log files
	UINT cDependencies;		// # of components that this component depends on
	} VSS_COMPONENTINFO;

typedef const VSS_COMPONENTINFO *PVSSCOMPONENTINFO;


// component information
class IVssWMComponent : public IUnknown
	{
public:
	// get component information
	STDMETHOD(GetComponentInfo)(PVSSCOMPONENTINFO *ppInfo) = 0;

	// free component information
	STDMETHOD(FreeComponentInfo)(PVSSCOMPONENTINFO pInfo) = 0;

	// obtain a specific file in a file group
	STDMETHOD(GetFile)
		(
		IN UINT iFile,
		OUT IVssWMFiledesc **ppFiledesc
		) = 0;

	// obtain a specific physical database file for a database
	STDMETHOD(GetDatabaseFile)
		(
		IN UINT iDBFile,
		OUT IVssWMFiledesc **ppFiledesc
		) = 0;

	// obtain a specific physical log file for a database
	STDMETHOD(GetDatabaseLogFile)
		(
		IN UINT iDbLogFile,
		OUT IVssWMFiledesc **ppFiledesc
		) = 0;

	STDMETHOD(GetDependency)
		(
		IN UINT iDependency,
		OUT IVssWMDependency **ppDependency
		) = 0;
	};


// interface to examine writer metadata
class IVssExamineWriterMetadata : public IUnknown
	{
public:
	// obtain identity of the writer
	STDMETHOD(GetIdentity)
		(
		OUT VSS_ID *pidInstance,
		OUT VSS_ID *pidWriter,
		OUT BSTR *pbstrWriterName,
		OUT VSS_USAGE_TYPE *pUsage,
		OUT VSS_SOURCE_TYPE *pSource
		) = 0;

	// obtain number of include files, exclude files, and components
	STDMETHOD(GetFileCounts)
		(
		OUT UINT *pcIncludeFiles,
		OUT UINT *pcExcludeFiles,
		OUT UINT *pcComponents
		) = 0;

	// obtain specific include files
	STDMETHOD(GetIncludeFile)
		(
		IN UINT iFile,
		OUT IVssWMFiledesc **ppFiledesc
		) = 0;

	// obtain specific exclude files
	STDMETHOD(GetExcludeFile)
		(
		IN UINT iFile,
		OUT IVssWMFiledesc **ppFiledesc
		) = 0;

	// obtain specific component
	STDMETHOD(GetComponent)
		(
		IN UINT iComponent,
		OUT IVssWMComponent **ppComponent
		) = 0;

	// obtain restoration method
	STDMETHOD(GetRestoreMethod)
		(
		OUT VSS_RESTOREMETHOD_ENUM *pMethod,
		OUT BSTR *pbstrService,
		OUT BSTR *pbstrUserProcedure,
		OUT VSS_WRITERRESTORE_ENUM *pwriterRestore,
		OUT bool *pbRebootRequired,
		UINT *pcMappings
		) = 0;

	// obtain a specific alternative location mapping
	STDMETHOD(GetAlternateLocationMapping)
		(
		IN UINT iMapping,
		OUT IVssWMFiledesc **ppFiledesc
		) = 0;

	// get the backup schema
	STDMETHOD(GetBackupSchema)
		(
		OUT DWORD *pdwSchemaMask
		) = 0;

	// obtain reference to actual XML document
	STDMETHOD(GetDocument)(IXMLDOMDocument **pDoc) = 0;

	// convert document to a XML string
	STDMETHOD(SaveAsXML)(BSTR *pbstrXML) = 0;

	// load document from an XML string
	STDMETHOD(LoadFromXML)(BSTR bstrXML) = 0;
	};


class IVssWriterComponentsExt :
	public IVssWriterComponents,
	public IUnknown
	{
	};


// backup components interface
class IVssBackupComponents : public IUnknown
	{
public:
	// get count of writer components
	STDMETHOD(GetWriterComponentsCount)(OUT UINT *pcComponents) = 0;

	// obtain a specific writer component
	STDMETHOD(GetWriterComponents)
		(
		IN UINT iWriter,
		OUT IVssWriterComponentsExt **ppWriter
		) = 0;

	// initialize and create BACKUP_COMPONENTS document
	STDMETHOD(InitializeForBackup)(IN BSTR bstrXML = NULL) = 0;

	// set state describing backup
	STDMETHOD(SetBackupState)
		(
		IN bool bSelectComponents,
		IN bool bBackupBootableSystemState,
		IN VSS_BACKUP_TYPE backupType,
		IN bool bPartialFileSupport = false
		) = 0;

	STDMETHOD(InitializeForRestore)(IN BSTR bstrXML) = 0;

       // set state describing restore
       STDMETHOD(SetRestoreState)
       	(
       	VSS_RESTORE_TYPE restoreType
       	) = 0;
       	
	// gather writer metadata
	STDMETHOD(GatherWriterMetadata)
		(
		OUT IVssAsync **pAsync
		) = 0;

	// get count of writers with metadata
	STDMETHOD(GetWriterMetadataCount)
		(
		OUT UINT *pcWriters
		) = 0;

	// get writer metadata for a specific writer
	STDMETHOD(GetWriterMetadata)
		(
		IN UINT iWriter,
		OUT VSS_ID *pidInstance,
		OUT IVssExamineWriterMetadata **ppMetadata
		) = 0;

	// free writer metadata
	STDMETHOD(FreeWriterMetadata)() = 0;

	// add a component to the BACKUP_COMPONENTS document
	STDMETHOD(AddComponent)
		(
		IN VSS_ID instanceId,
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName
		) = 0;

	// dispatch PrepareForBackup event to writers
	STDMETHOD(PrepareForBackup)
		(
		OUT IVssAsync **ppAsync
		) = 0;

	// abort the backup
	STDMETHOD(AbortBackup)() = 0;

	// dispatch the Identify event so writers can expose their metadata
	STDMETHOD(GatherWriterStatus)
		(
		OUT IVssAsync **pAsync
		) = 0;


	// get count of writers with status
	STDMETHOD(GetWriterStatusCount)
		(
		OUT UINT *pcWriters
		) = 0;

	STDMETHOD(FreeWriterStatus)() = 0;

	STDMETHOD(GetWriterStatus)
		(
		IN UINT iWriter,
		OUT VSS_ID *pidInstance,
		OUT VSS_ID *pidWriter,
		OUT BSTR *pbstrWriter,
		OUT VSS_WRITER_STATE *pnStatus,
		OUT HRESULT *phResultFailure
		) = 0;

	// indicate whether backup succeeded on a component
	STDMETHOD(SetBackupSucceeded)
		(
		IN VSS_ID instanceId,
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN bool bSucceded
		) = 0;

    // set backup options for the writer
	STDMETHOD(SetBackupOptions)
		(
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN LPCWSTR wszBackupOptions
		) = 0;

    // indicate that a given component is selected to be restored
    STDMETHOD(SetSelectedForRestore)
		(
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN bool bSelectedForRestore
		) = 0;


    // set restore options for the writer
	STDMETHOD(SetRestoreOptions)
		(
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN LPCWSTR wszRestoreOptions
		) = 0;

	// indicate that additional restores will follow
	STDMETHOD(SetAdditionalRestores)
		(
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN bool bAdditionalRestores
		) = 0;


    // set the backup stamp that the differential or incremental
	// backup is based on
    STDMETHOD(SetPreviousBackupStamp)
		(
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN LPCWSTR wszPreviousBackupStamp
		) = 0;



	// save BACKUP_COMPONENTS document as XML string
	STDMETHOD(SaveAsXML)(BSTR *pbstrXML) = 0;

	// signal BackupComplete event to the writers
	STDMETHOD(BackupComplete)(OUT IVssAsync **ppAsync) = 0;

	// add an alternate mapping on restore
	STDMETHOD(AddAlternativeLocationMapping)
		(
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE componentType,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN LPCWSTR wszPath,
		IN LPCWSTR wszFilespec,
		IN bool bRecursive,
		IN LPCWSTR wszDestination
		) = 0;

    // add a subcomponent to be restored
	STDMETHOD(AddRestoreSubcomponent)
		(
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE componentType,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN LPCWSTR wszSubComponentLogicalPath,
		IN LPCWSTR wszSubComponentName,
		IN bool bRepair
		) = 0;

	// requestor indicates whether files were successfully restored
	STDMETHOD(SetFileRestoreStatus)
		(
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN VSS_FILE_RESTORE_STATUS status
		) = 0;

    // add a new location target for a file to be restored
    STDMETHOD(AddNewTarget)
		(
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN LPCWSTR wszPath,
		IN LPCWSTR wszFileName,	
		IN bool bRecursive,
		IN LPCWSTR wszAlternatePath
		) = 0;

     // add a new location for the ranges file in case it was restored to
    // a different location
    STDMETHOD(SetRangesFilePath)
    		(
		IN VSS_ID writerId,
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,    		
    		IN UINT iPartialFile,
    		IN LPCWSTR wszRangesFile
    		) = 0;

	// signal PreRestore event to the writers
	STDMETHOD(PreRestore)(OUT IVssAsync **ppAsync) = 0;

	// signal PostRestore event to the writers
	STDMETHOD(PostRestore)(OUT IVssAsync **ppAsync) = 0;

    // Called to set the context for subsequent snapshot-related operations
    STDMETHOD(SetContext)
		(
        IN LONG lContext
        ) = 0;
    
	// start a snapshot set
	STDMETHOD(StartSnapshotSet)
	    (
	    OUT VSS_ID *pSnapshotSetId
	    ) = 0;

	// add a volume to a snapshot set
	STDMETHOD(AddToSnapshotSet)
		(							
		IN VSS_PWSZ		pwszVolumeName, 			
		IN VSS_ID		ProviderId,
		OUT VSS_ID		*pidSnapshot
		) = 0;												

	// create the snapshot set
	STDMETHOD(DoSnapshotSet)
		(								
		OUT IVssAsync** 	ppAsync 					
		) = 0;

   	STDMETHOD(DeleteSnapshots)
		(							
		IN VSS_ID		SourceObjectId, 		
		IN VSS_OBJECT_TYPE 	eSourceObjectType,		
		IN BOOL			bForceDelete,			
		IN LONG*		plDeletedSnapshots,		
		IN VSS_ID*		pNondeletedSnapshotID	
		) = 0;

    STDMETHOD(ImportSnapshots)
		(
		OUT IVssAsync**		ppAsync
		) = 0;

	STDMETHOD(BreakSnapshotSet)
		(
		IN VSS_ID			SnapshotSetId
		) = 0;

	STDMETHOD(GetSnapshotProperties)
		(								
		IN VSS_ID		SnapshotId, 			
		OUT VSS_SNAPSHOT_PROP	*pProp
		) = 0;												
		
	STDMETHOD(Query)
		(										
		IN VSS_ID		QueriedObjectId,		
		IN VSS_OBJECT_TYPE	eQueriedObjectType, 	
		IN VSS_OBJECT_TYPE	eReturnedObjectsType,	
		IN IVssEnumObject 	**ppEnum 				
		) = 0;												
	
	STDMETHOD(IsVolumeSupported)
		(										
		IN VSS_ID ProviderId,		
        IN VSS_PWSZ pwszVolumeName,
        IN BOOL * pbSupportedByThisProvider
		) = 0;

    STDMETHOD(DisableWriterClasses)
		(
		IN const VSS_ID *rgWriterClassId,
		IN UINT cClassId
		) = 0;

    STDMETHOD(EnableWriterClasses)
		(
		IN const VSS_ID *rgWriterClassId,
		IN UINT cClassId
		) = 0;

    STDMETHOD(DisableWriterInstances)
		(
		IN const VSS_ID *rgWriterInstanceId,
		IN UINT cInstanceId
		) = 0;

    // called to expose a snapshot 
    STDMETHOD(ExposeSnapshot)
		(
        IN VSS_ID SnapshotId,
        IN VSS_PWSZ wszPathFromRoot,
        IN LONG lAttributes,
        IN VSS_PWSZ wszExpose,
        OUT VSS_PWSZ *pwszExposed
        ) = 0;

    STDMETHOD(RevertToSnapshot)
    	(
    	IN VSS_ID SnapshotId,
    	IN BOOL bForceDismount
    	) = 0;

    STDMETHOD(QueryRevertStatus)
    	(
    	IN VSS_PWSZ pwszVolume,
    	OUT IVssAsync **ppAsync
    	) = 0;
    
	};


__declspec(dllexport) HRESULT STDAPICALLTYPE CreateVssBackupComponents(
    OUT IVssBackupComponents **ppBackup
    );

__declspec(dllexport) HRESULT STDAPICALLTYPE CreateVssExamineWriterMetadata (
    IN BSTR bstrXML,
    OUT IVssExamineWriterMetadata **ppMetadata
    );


#define VSS_SW_BOOTABLE_STATE	(1 << 0)

__declspec(dllexport) HRESULT APIENTRY SimulateSnapshotFreeze (
    IN GUID         guidSnapshotSetId,
    IN ULONG        ulOptionFlags,	
    IN ULONG        ulVolumeCount,	
    IN LPWSTR      *ppwszVolumeNamesArray,
    OUT IVssAsync **ppAsync
    );

__declspec(dllexport) HRESULT APIENTRY SimulateSnapshotThaw(
    IN GUID guidSnapshotSetId
    );

__declspec(dllexport) HRESULT APIENTRY IsVolumeSnapshotted(
    IN VSS_PWSZ  pwszVolumeName,
    OUT BOOL    *pbSnapshotsPresent,
    OUT LONG	*plSnapshotCapability
    );

/////////////////////////////////////////////////////////////////////
// Life-management methods for structure members 

__declspec(dllexport) void APIENTRY VssFreeSnapshotProperties(
    IN VSS_SNAPSHOT_PROP*  pProp
    );


///


#endif // _VSBACKUP_H_

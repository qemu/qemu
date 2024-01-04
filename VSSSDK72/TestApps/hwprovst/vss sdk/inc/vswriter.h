/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Abstract:

    @doc
    @module Writer.h | Declaration of Writer
    @end

Author:

    Adi Oltean  [aoltean]  08/18/1999

TBD:
	
	Add comments.

Revision History:

    Name        Date        Comments
    aoltean     08/18/1999  Created
    brianb	03/28/2000  hid implementation details
    mikejohn	09/18/2000  176860: Added calling convention methods where missing

--*/

#ifndef __CVSS_WRITER_H_
#define __CVSS_WRITER_H_

// declaration of how application data is used
typedef enum VSS_USAGE_TYPE
	{
	VSS_UT_UNDEFINED = 0,
	VSS_UT_BOOTABLESYSTEMSTATE,	// formerly "system state"
	VSS_UT_SYSTEMSERVICE,		// system service
	VSS_UT_USERDATA,			// user data
	VSS_UT_OTHER				// unclassified
	};

typedef enum VSS_SOURCE_TYPE
	{
	VSS_ST_UNDEFINED = 0,
	VSS_ST_TRANSACTEDDB,			// transacted db (e.g., SQL Server, JET Blue)
	VSS_ST_NONTRANSACTEDDB,			// not transacted(e.g., Jet Red)
	VSS_ST_OTHER					// unclassified
	};

typedef enum VSS_RESTOREMETHOD_ENUM
	{
	VSS_RME_UNDEFINED = 0,
	VSS_RME_RESTORE_IF_NOT_THERE,
	VSS_RME_RESTORE_IF_CAN_REPLACE,
	VSS_RME_STOP_RESTORE_START,
	VSS_RME_RESTORE_TO_ALTERNATE_LOCATION,
	VSS_RME_RESTORE_AT_REBOOT,
	VSS_RME_RESTORE_AT_REBOOT_IF_CANNOT_REPLACE,
	VSS_RME_CUSTOM
	};

typedef enum VSS_WRITERRESTORE_ENUM
	{
	VSS_WRE_UNDEFINED = 0,
	VSS_WRE_NEVER,
	VSS_WRE_IF_REPLACE_FAILS,
	VSS_WRE_ALWAYS
	};


typedef enum VSS_COMPONENT_TYPE
	{
	VSS_CT_UNDEFINED = 0,
	VSS_CT_DATABASE,
	VSS_CT_FILEGROUP
	};

typedef enum VSS_ALTERNATE_WRITER_STATE
    {
    VSS_AWS_UNDEFINED = 0,
    VSS_AWS_NO_ALTERNATE_WRITER,
    VSS_AWS_ALTERNATE_WRITER_EXISTS,
    VSS_AWS_THIS_IS_ALTERNATE_WRITER
    };

// Flags to specify which types of events to receive.  Used in Subscribe.
typedef enum VSS_SUBSCRIBE_MASK
    {
	VSS_SM_POST_SNAPSHOT_FLAG	 = 0x00000001,	
	VSS_SM_BACKUP_EVENTS_FLAG 	= 0x00000002,	
	VSS_SM_RESTORE_EVENTS_FLAG	= 0x00000004,	
	VSS_SM_IO_THROTTLING_FLAG	 = 0x00000008,
	VSS_SM_ALL_FLAGS             = 0xffffffff		
    };

// enumeration of restore targets
typedef enum VSS_RESTORE_TARGET
	{
	VSS_RT_UNDEFINED = 0,
	VSS_RT_ORIGINAL,        
	VSS_RT_ALTERNATE,
	VSS_RT_DIRECTED,
        VSS_RT_ORIGINAL_LOCATION
	};

// enumeration of file restore status codes
typedef enum VSS_FILE_RESTORE_STATUS
	{
	VSS_RS_UNDEFINED = 0,
	VSS_RS_NONE,
	VSS_RS_ALL,
	VSS_RS_FAILED
	};


typedef enum VSS_COMPONENT_FLAGS
	{
	VSS_CF_BACKUP_RECOVERY                  = 0x00000001,
	VSS_CF_APP_ROLLBACK_RECOVERY       = 0x00000002
	};

// file description
class IVssWMFiledesc : public IUnknown
	{
public:
	// get path to toplevel directory
	STDMETHOD(GetPath)(OUT BSTR *pbstrPath) = 0;

	// get filespec (may include wildcards)
	STDMETHOD(GetFilespec)(OUT BSTR *pbstrFilespec) = 0;

	// is path a directory or root of a tree
	STDMETHOD(GetRecursive)(OUT bool *pbRecursive) = 0;

	// alternate location for files
	STDMETHOD(GetAlternateLocation)(OUT BSTR *pbstrAlternateLocation) = 0;

	// backup type
	STDMETHOD(GetBackupTypeMask)(OUT DWORD *pdwTypeMask) = 0;
	};

// dependency description
class IVssWMDependency : public IUnknown
	{
public:
	STDMETHOD(GetWriterId)(OUT VSS_ID *pWriterId) = 0;
	STDMETHOD(GetLogicalPath)(OUT BSTR *pbstrLogicalPath) = 0;
	STDMETHOD(GetComponentName)(OUT BSTR *pbstrComponentName) = 0;
	};

// backup components interface
class IVssComponent : public IUnknown
	{
public:
	// obtain logical path of component
	STDMETHOD(GetLogicalPath)(OUT BSTR *pbstrPath) = 0;

	// obtain component type(VSS_CT_DATABASE or VSS_CT_FILEGROUP)
	STDMETHOD(GetComponentType)(VSS_COMPONENT_TYPE *pct) = 0;

	// get component name
	STDMETHOD(GetComponentName)(OUT BSTR *pbstrName) = 0;

	// determine whether the component was successfully backed up.
	STDMETHOD(GetBackupSucceeded)(OUT bool *pbSucceeded) = 0;

	// get altermative location mapping count
	STDMETHOD(GetAlternateLocationMappingCount)
		(
		OUT UINT *pcMappings
		) = 0;

	// get a paraticular alternative location mapping
	STDMETHOD(GetAlternateLocationMapping)
		(
		IN UINT iMapping,
		OUT IVssWMFiledesc **ppFiledesc
		) = 0;

    // set the backup metadata for a component
	STDMETHOD(SetBackupMetadata)
		(
		IN LPCWSTR wszData
		) = 0;

	// get the backup metadata for a component
	STDMETHOD(GetBackupMetadata)
		(
		OUT BSTR *pbstrData
		) = 0;

    // indicate that only ranges in the file are to be backed up
	STDMETHOD(AddPartialFile)
		(
		IN LPCWSTR wszPath,
		IN LPCWSTR wszFilename,
		IN LPCWSTR wszRanges,
		IN LPCWSTR wszMetadata
		) = 0;

    // get count of partial file declarations
    STDMETHOD(GetPartialFileCount)
		(
		OUT UINT *pcPartialFiles
		) = 0;

    // get a partial file declaration
    STDMETHOD(GetPartialFile)
		(
		IN UINT iPartialFile,
		OUT BSTR *pbstrPath,
		OUT BSTR *pbstrFilename,
		OUT BSTR *pbstrRange,
		OUT BSTR *pbstrMetadata
		) = 0;
    		
    // determine if the component is selected to be restored
	STDMETHOD(IsSelectedForRestore)
		(
		OUT bool *pbSelectedForRestore
		) = 0;

	STDMETHOD(GetAdditionalRestores)
		(
		OUT bool *pbAdditionalRestores
		) = 0;

    // get count of new target specifications
    STDMETHOD(GetNewTargetCount)
		(
		OUT UINT *pcNewTarget
		) = 0;

	STDMETHOD(GetNewTarget)
		(
		IN UINT iNewTarget,
		OUT IVssWMFiledesc **ppFiledesc
		) = 0;

    // add a directed target specification
    STDMETHOD(AddDirectedTarget)
		(
		IN LPCWSTR wszSourcePath,
		IN LPCWSTR wszSourceFilename,
		IN LPCWSTR wszSourceRangeList,
		IN LPCWSTR wszDestinationPath,
		IN LPCWSTR wszDestinationFilename,
		IN LPCWSTR wszDestinationRangeList
		) = 0;

    // get count of directed target specifications
	STDMETHOD(GetDirectedTargetCount)
		(
		OUT UINT *pcDirectedTarget
		) = 0;

    // obtain a particular directed target specification
    STDMETHOD(GetDirectedTarget)
		(
		IN UINT iDirectedTarget,
		OUT BSTR *pbstrSourcePath,
		OUT BSTR *pbstrSourceFileName,
		OUT BSTR *pbstrSourceRangeList,
		OUT BSTR *pbstrDestinationPath,
		OUT BSTR *pbstrDestinationFilename,
		OUT BSTR *pbstrDestinationRangeList
		) = 0;

    // set restore metadata associated with the component
    STDMETHOD(SetRestoreMetadata)
		(
		IN LPCWSTR wszRestoreMetadata
		) = 0;

    // obtain restore metadata associated with the component
    STDMETHOD(GetRestoreMetadata)
		(
		OUT BSTR *pbstrRestoreMetadata
		) = 0;

     // set the restore target
	 STDMETHOD(SetRestoreTarget)
   		(
		IN VSS_RESTORE_TARGET target
		) = 0;

    // obtain the restore target
	STDMETHOD(GetRestoreTarget)
		(
		OUT VSS_RESTORE_TARGET *pTarget
		) = 0;

    // set failure message during pre restore event
	STDMETHOD(SetPreRestoreFailureMsg)
		(
		IN LPCWSTR wszPreRestoreFailureMsg
		) = 0;

    // obtain failure message during pre restore event
	STDMETHOD(GetPreRestoreFailureMsg)
		(
		OUT BSTR *pbstrPreRestoreFailureMsg
		) = 0;

    // set the failure message during the post restore event
    STDMETHOD(SetPostRestoreFailureMsg)
		(
		IN LPCWSTR wszPostRestoreFailureMsg
		) = 0;

    // obtain the failure message set during the post restore event
    STDMETHOD(GetPostRestoreFailureMsg)
		(
		OUT BSTR *pbstrPostRestoreFailureMsg
		) = 0;

    // set the backup stamp of the backup
    STDMETHOD(SetBackupStamp)
		(
		IN LPCWSTR wszBackupStamp
		) = 0;

    // obtain the stamp of the backup
    STDMETHOD(GetBackupStamp)
		(
		OUT BSTR *pbstrBackupStamp
		) = 0;


    // obtain the backup stamp that the differential or incremental
	// backup is baed on
	STDMETHOD(GetPreviousBackupStamp)
		(
		OUT BSTR *pbstrBackupStamp
		) = 0;

    // obtain backup options for the writer
	STDMETHOD(GetBackupOptions)
		(
		OUT BSTR *pbstrBackupOptions
		) = 0;

    // obtain the restore options
	STDMETHOD(GetRestoreOptions)
		(
		OUT BSTR *pbstrRestoreOptions
		) = 0;

    // obtain count of subcomponents to be restored
	STDMETHOD(GetRestoreSubcomponentCount)
		(
		OUT UINT *pcRestoreSubcomponent
		) = 0;

    // obtain a particular subcomponent to be restored
    STDMETHOD(GetRestoreSubcomponent)
		(
		UINT iComponent,
		OUT BSTR *pbstrLogicalPath,
		OUT BSTR *pbstrComponentName,
		OUT bool *pbRepair
		) = 0;


	// obtain whether files were successfully restored
	STDMETHOD(GetFileRestoreStatus)
		(
		OUT VSS_FILE_RESTORE_STATUS *pStatus
		) = 0;

	// add differenced files by last modify time
	STDMETHOD(AddDifferencedFilesByLastModifyTime)
		(
		IN LPCWSTR wszPath,
		IN LPCWSTR wszFilespec,
		IN BOOL bRecursive,
		IN FILETIME ftLastModifyTime
		) = 0;

	STDMETHOD(AddDifferencedFilesByLastModifyLSN)
		(
		IN LPCWSTR wszPath,
		IN LPCWSTR wszFilespec,
		IN BOOL bRecursive,
		IN BSTR bstrLsnString
		) = 0;

	STDMETHOD(GetDifferencedFilesCount)
		(
		OUT UINT *pcDifferencedFiles
		) = 0;

	STDMETHOD(GetDifferencedFile)
		(
		IN UINT iDifferencedFile,
		OUT BSTR *pbstrPath,
		OUT BSTR *pbstrFilespec,
		OUT BOOL *pbRecursive,
		OUT BSTR *pbstrLsnString,
		OUT FILETIME *pftLastModifyTime
		) = 0;
	};

// backup writer components interface (i.e., all components for an
// individual writer
class IVssWriterComponents
	{
public:
	// get count of components	
	STDMETHOD(GetComponentCount)(OUT UINT *pcComponents) = 0;

	// get information about the writer
	STDMETHOD(GetWriterInfo)
		(
		OUT VSS_ID *pidInstance,
		OUT VSS_ID *pidWriter
		) = 0;

    // obtain a specific component
	STDMETHOD(GetComponent)
		(
		IN UINT iComponent,
		OUT IVssComponent **ppComponent
		) = 0;
    };

// create backup metadata interface
class IVssCreateWriterMetadata
	{
public:
    // add files to include to metadata document
	STDMETHOD(AddIncludeFiles)
		(
		IN LPCWSTR wszPath,
		IN LPCWSTR wszFilespec,
		IN bool bRecursive,
		IN LPCWSTR wszAlternateLocation
		) = 0;

	// add files to exclude to metadata document
    STDMETHOD(AddExcludeFiles)
		(
		IN LPCWSTR wszPath,
		IN LPCWSTR wszFilespec,
		IN bool bRecursive
		) = 0;

    // add component to metadata document
    STDMETHOD(AddComponent)
		(
		IN VSS_COMPONENT_TYPE ct,
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszComponentName,
		IN LPCWSTR wszCaption,
		IN const BYTE *pbIcon,
		IN UINT cbIcon,
		IN bool bRestoreMetadata,
		IN bool bNotifyOnBackupComplete,
		IN bool bSelectable,
		IN bool bSelectableForRestore = false,
		IN DWORD dwComponentFlags = 0
		) = 0;

    // add physical database files to a database component
    STDMETHOD(AddDatabaseFiles)
		(
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszDatabaseName,
		IN LPCWSTR wszPath,
		IN LPCWSTR wszFilespec,
		IN DWORD dwBackupTypeMask = (VSS_FSBT_ALL_BACKUP_REQUIRED |
									 VSS_FSBT_ALL_SNAPSHOT_REQUIRED)
		) = 0;

    // add log files to a database component
    STDMETHOD(AddDatabaseLogFiles)
		(
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszDatabaseName,
		IN LPCWSTR wszPath,
		IN LPCWSTR wszFilespec,
		IN DWORD dwBackupTypeMask = (VSS_FSBT_ALL_BACKUP_REQUIRED |
									 VSS_FSBT_ALL_SNAPSHOT_REQUIRED)
		) = 0;


    // add files to a FILE_GROUP component
    STDMETHOD(AddFilesToFileGroup)
		(
		IN LPCWSTR wszLogicalPath,
		IN LPCWSTR wszGroupName,
		IN LPCWSTR wszPath,
		IN LPCWSTR wszFilespec,
		IN bool bRecursive,
		IN LPCWSTR wszAlternateLocation,
		IN DWORD dwBackupTypeMask = (VSS_FSBT_ALL_BACKUP_REQUIRED |
									 VSS_FSBT_ALL_SNAPSHOT_REQUIRED)
		) = 0;

    // create a restore method
	STDMETHOD(SetRestoreMethod)
		(
		IN VSS_RESTOREMETHOD_ENUM method,
		IN LPCWSTR wszService,
		IN LPCWSTR wszUserProcedure,
		IN VSS_WRITERRESTORE_ENUM writerRestore,
		IN bool bRebootRequired
		) = 0;

    // add alternative location mappings to the restore method
    STDMETHOD(AddAlternateLocationMapping)
		(
		IN LPCWSTR wszSourcePath,
		IN LPCWSTR wszSourceFilespec,
		IN bool bRecursive,
		IN LPCWSTR wszDestination
		) = 0;

	// add a dependency to another writer's component
	STDMETHOD(AddComponentDependency)
				(
				IN LPCWSTR wszForLogicalPath,
				IN LPCWSTR wszForComponentName,
				IN VSS_ID onWriterId,
				IN LPCWSTR wszOnLogicalPath,
				IN LPCWSTR wszOnComponentName
				) = 0;

	// Set the schema used during backup
	STDMETHOD(SetBackupSchema)
				(
				IN DWORD dwSchemaMask
				) = 0;

    // obtain reference to actual XML document
	STDMETHOD(GetDocument)(IXMLDOMDocument **pDoc) = 0;

    // save document as an XML string
    STDMETHOD(SaveAsXML)(BSTR *pbstrXML) = 0;
	};


class IVssWriterImpl;


/////////////////////////////////////////////////////////////////////////////
// CVssWriter


class CVssWriter
	{
// Constants
public:

// Constructors & Destructors
public:
	__declspec(dllexport)
	STDMETHODCALLTYPE CVssWriter();

	__declspec(dllexport)
	virtual STDMETHODCALLTYPE ~CVssWriter();

// Exposed operations
public:
	// initialize the writer object
	__declspec(dllexport)
	HRESULT STDMETHODCALLTYPE Initialize
		(
		IN VSS_ID WriterID,
		IN LPCWSTR wszWriterName,
		IN VSS_USAGE_TYPE ut,
		IN VSS_SOURCE_TYPE st,
		IN VSS_APPLICATION_LEVEL nLevel = VSS_APP_FRONT_END,
		IN DWORD dwTimeoutFreeze = 60000,			// Maximum milliseconds between Freeze/Thaw
		IN VSS_ALTERNATE_WRITER_STATE aws = VSS_AWS_NO_ALTERNATE_WRITER,
		IN bool bIOThrottlingOnly = false,
		IN LPCWSTR wszWriterInstanceName = NULL
		);

   	// cause the writer to subscribe to events.
	__declspec(dllexport)
	HRESULT STDMETHODCALLTYPE Subscribe
	    (
	    IN DWORD dwEventFlags = VSS_SM_BACKUP_EVENTS_FLAG | VSS_SM_RESTORE_EVENTS_FLAG
	    );

   	// cause the writer to unsubscribe from events
	__declspec(dllexport)
	HRESULT STDMETHODCALLTYPE Unsubscribe();

    // installs an alternative writer
    __declspec(dllexport)
    HRESULT STDMETHODCALLTYPE InstallAlternateWriter
        (
        IN VSS_ID writerId,
        IN CLSID persistentWriterClassId
        );

	// Internal properties - accessible from OnXXX methods
protected:

	// get array of volume names
	__declspec(dllexport)
	LPCWSTR* STDMETHODCALLTYPE GetCurrentVolumeArray() const;

	// get count of volume names in array
	__declspec(dllexport)
	UINT STDMETHODCALLTYPE GetCurrentVolumeCount() const;

	// get the name of the snapshot device corresponding to a given volume.
	__declspec(dllexport)
	HRESULT STDMETHODCALLTYPE GetSnapshotDeviceName
	  (
	  IN LPCWSTR wszOriginalVolume,
	  OUT LPCWSTR* ppwszSnapshotDevice
	  ) const;
	
	// current snapshot set GUID
	__declspec(dllexport)
	VSS_ID STDMETHODCALLTYPE GetCurrentSnapshotSetId() const;

	// Current backup context.  
	__declspec(dllexport)
	LONG STDMETHODCALLTYPE GetContext() const;
	
	// current app level (either 1,2,3)
	__declspec(dllexport)
	VSS_APPLICATION_LEVEL STDMETHODCALLTYPE GetCurrentLevel() const;

	// determine if path is in set of volumes being snapshotted
	__declspec(dllexport)
	bool STDMETHODCALLTYPE IsPathAffected
	    (
	    IN LPCWSTR wszPath
	    ) const;

	// does the backup include bootable state (formerly system state backup)
	__declspec(dllexport)
	bool STDMETHODCALLTYPE IsBootableSystemStateBackedUp() const;

	// is the backup application smart (i.e., selecting components) or
	// dump (i.e., just selecting volumes)
	__declspec(dllexport)
	bool STDMETHODCALLTYPE AreComponentsSelected() const;

	__declspec(dllexport)
	VSS_BACKUP_TYPE STDMETHODCALLTYPE GetBackupType() const;

	__declspec(dllexport)
	VSS_RESTORE_TYPE STDMETHODCALLTYPE GetRestoreType() const;
	
	__declspec(dllexport)
	bool STDMETHODCALLTYPE IsPartialFileSupportEnabled() const;

	_declspec(dllexport)
       	HRESULT STDMETHODCALLTYPE SetWriterFailure(HRESULT hr);

// Ovverides
public:
	// callback when request for metadata comes in
	__declspec(dllexport)
	virtual bool STDMETHODCALLTYPE OnIdentify(IN IVssCreateWriterMetadata *pMetadata);

	// callback for prepare backup event
	__declspec(dllexport)
	virtual bool STDMETHODCALLTYPE OnPrepareBackup(
	    IN IVssWriterComponents *pComponent
	    );

	// callback for prepare snapsot event
	virtual bool STDMETHODCALLTYPE OnPrepareSnapshot() = 0;

	// callback for freeze event
	virtual bool STDMETHODCALLTYPE OnFreeze() = 0;

	// callback for thaw event
	virtual bool STDMETHODCALLTYPE OnThaw() = 0;

	// callback if current sequence is aborted
	virtual bool STDMETHODCALLTYPE OnAbort() = 0;

	// callback on backup complete event
	__declspec(dllexport)
	virtual bool STDMETHODCALLTYPE OnBackupComplete
	    (
	    IN IVssWriterComponents *pComponent
	    );

    // callback indicating that the backup process has either completed or has shut down
    __declspec(dllexport)
    virtual bool STDMETHODCALLTYPE OnBackupShutdown
        (
        IN VSS_ID SnapshotSetId
        );
        
    // callback on pre-restore event
    __declspec(dllexport)
    virtual bool STDMETHODCALLTYPE OnPreRestore
        (
        IN IVssWriterComponents *pComponent
        );

    // callback on post-restore event
    __declspec(dllexport)
    virtual bool STDMETHODCALLTYPE OnPostRestore
        (
        IN IVssWriterComponents *pComponent
        );

    // callback on post snapshot event
    __declspec(dllexport)
    virtual bool STDMETHODCALLTYPE OnPostSnapshot
        (
        IN IVssWriterComponents *pComponent
        );

    // callback on back off I/O volume event
    __declspec(dllexport)
    virtual bool STDMETHODCALLTYPE OnBackOffIOOnVolume
        (
        IN VSS_PWSZ wszVolumeName,
        IN VSS_ID snapshotId,
        IN VSS_ID providerId
        );

    // callback on Continue I/O on volume event
    __declspec(dllexport)
    virtual bool STDMETHODCALLTYPE OnContinueIOOnVolume
        (
        IN VSS_PWSZ wszVolumeName,
        IN VSS_ID snapshotId,
        IN VSS_ID providerId
        );

    // callback to specify that the volume snaphost service is shutting down.  Used
    // by alternative writers to signal when to shutdown.
    __declspec(dllexport)
    virtual bool STDMETHODCALLTYPE OnVSSShutdown();

    // callback to an alternative writer when the application writer subscribes.  Used to
    // signal the alternative writer to shutdown.
    __declspec(dllexport)
    virtual bool STDMETHODCALLTYPE OnVSSApplicationStartup();

private:

	IVssWriterImpl *m_pWrapper;
	};

//
// MessageId: VSS_E_WRITERERROR_INCONSISTENTSNAPSHOT
//
// MessageText:
//
//  indicates that the snapshot contains only a subset of the
//  volumes needed to correctly backup an application component
//
const HRESULT VSS_E_WRITERERROR_INCONSISTENTSNAPSHOT	= (0x800423f0L);

//
// MessageId: VSS_E_WRITERERROR_OUTOFRESOURCES
//
// MessageText:
//
//  indicates that the writer failed due to an out of memory,
//  out of handles, or other resource allocation failure
//
const HRESULT VSS_E_WRITERERROR_OUTOFRESOURCES		= (0x800423f1L);


//
// MessageId: VSS_E_WRITERERROR_TIMEOUT
//
// MessageText:
//
//  indicates that the writer failed due to a timeout between
//  freeze and thaw.
//
const HRESULT VSS_E_WRITERERROR_TIMEOUT		= (0x800423f2L);

//
// MessageId: VSS_E_WRITERERROR_RETRYABLE
//
// MessageText:
//
//  indicates that the writer failed due to an error
//  that might not occur if another snapshot is created
//

const HRESULT VSS_E_WRITERERROR_RETRYABLE	= (0x800423f3L);

//
// MessageId: VSS_E_WRITERERROR_NONRETRYABLE
//
// MessageText:
//
//  indicates that the writer failed due to an error
//  that most likely would occur if another snapshot is created
//
const HRESULT VSS_E_WRITERERROR_NONRETRYABLE	= (0x800423f4L);

//
// MessageId: VSS_E_WRITERERROR_RECOVERY_FAILED
//
// MessageText:
//
// indicates that auto recovery of the snapshot volume failed
const HRESULT VSS_E_WRITERERROR_RECOVERY_FAILED = (0x800423f5L);




#endif //__CVSS_WRITER_H_

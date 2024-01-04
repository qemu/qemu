

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 6.00.0361 */
/* Compiler settings for vds.idl:
    Oicf, W1, Zp8, env=Win32 (32b run)
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
//@@MIDL_FILE_HEADING(  )

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

#ifndef __vds_h__
#define __vds_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IEnumVdsObject_FWD_DEFINED__
#define __IEnumVdsObject_FWD_DEFINED__
typedef interface IEnumVdsObject IEnumVdsObject;
#endif 	/* __IEnumVdsObject_FWD_DEFINED__ */


#ifndef __IVdsAdviseSink_FWD_DEFINED__
#define __IVdsAdviseSink_FWD_DEFINED__
typedef interface IVdsAdviseSink IVdsAdviseSink;
#endif 	/* __IVdsAdviseSink_FWD_DEFINED__ */


#ifndef __IVdsProvider_FWD_DEFINED__
#define __IVdsProvider_FWD_DEFINED__
typedef interface IVdsProvider IVdsProvider;
#endif 	/* __IVdsProvider_FWD_DEFINED__ */


#ifndef __IVdsAsync_FWD_DEFINED__
#define __IVdsAsync_FWD_DEFINED__
typedef interface IVdsAsync IVdsAsync;
#endif 	/* __IVdsAsync_FWD_DEFINED__ */


#ifndef __IVdsSwProvider_FWD_DEFINED__
#define __IVdsSwProvider_FWD_DEFINED__
typedef interface IVdsSwProvider IVdsSwProvider;
#endif 	/* __IVdsSwProvider_FWD_DEFINED__ */


#ifndef __IVdsPack_FWD_DEFINED__
#define __IVdsPack_FWD_DEFINED__
typedef interface IVdsPack IVdsPack;
#endif 	/* __IVdsPack_FWD_DEFINED__ */


#ifndef __IVdsDisk_FWD_DEFINED__
#define __IVdsDisk_FWD_DEFINED__
typedef interface IVdsDisk IVdsDisk;
#endif 	/* __IVdsDisk_FWD_DEFINED__ */


#ifndef __IVdsAdvancedDisk_FWD_DEFINED__
#define __IVdsAdvancedDisk_FWD_DEFINED__
typedef interface IVdsAdvancedDisk IVdsAdvancedDisk;
#endif 	/* __IVdsAdvancedDisk_FWD_DEFINED__ */


#ifndef __IVdsRemovable_FWD_DEFINED__
#define __IVdsRemovable_FWD_DEFINED__
typedef interface IVdsRemovable IVdsRemovable;
#endif 	/* __IVdsRemovable_FWD_DEFINED__ */


#ifndef __IVdsVolume_FWD_DEFINED__
#define __IVdsVolume_FWD_DEFINED__
typedef interface IVdsVolume IVdsVolume;
#endif 	/* __IVdsVolume_FWD_DEFINED__ */


#ifndef __IVdsVolumePlex_FWD_DEFINED__
#define __IVdsVolumePlex_FWD_DEFINED__
typedef interface IVdsVolumePlex IVdsVolumePlex;
#endif 	/* __IVdsVolumePlex_FWD_DEFINED__ */


#ifndef __IVdsHwProvider_FWD_DEFINED__
#define __IVdsHwProvider_FWD_DEFINED__
typedef interface IVdsHwProvider IVdsHwProvider;
#endif 	/* __IVdsHwProvider_FWD_DEFINED__ */


#ifndef __IVdsSubSystem_FWD_DEFINED__
#define __IVdsSubSystem_FWD_DEFINED__
typedef interface IVdsSubSystem IVdsSubSystem;
#endif 	/* __IVdsSubSystem_FWD_DEFINED__ */


#ifndef __IVdsController_FWD_DEFINED__
#define __IVdsController_FWD_DEFINED__
typedef interface IVdsController IVdsController;
#endif 	/* __IVdsController_FWD_DEFINED__ */


#ifndef __IVdsDrive_FWD_DEFINED__
#define __IVdsDrive_FWD_DEFINED__
typedef interface IVdsDrive IVdsDrive;
#endif 	/* __IVdsDrive_FWD_DEFINED__ */


#ifndef __IVdsLun_FWD_DEFINED__
#define __IVdsLun_FWD_DEFINED__
typedef interface IVdsLun IVdsLun;
#endif 	/* __IVdsLun_FWD_DEFINED__ */


#ifndef __IVdsLunPlex_FWD_DEFINED__
#define __IVdsLunPlex_FWD_DEFINED__
typedef interface IVdsLunPlex IVdsLunPlex;
#endif 	/* __IVdsLunPlex_FWD_DEFINED__ */


#ifndef __IVdsMaintenance_FWD_DEFINED__
#define __IVdsMaintenance_FWD_DEFINED__
typedef interface IVdsMaintenance IVdsMaintenance;
#endif 	/* __IVdsMaintenance_FWD_DEFINED__ */


#ifndef __IVdsServiceLoader_FWD_DEFINED__
#define __IVdsServiceLoader_FWD_DEFINED__
typedef interface IVdsServiceLoader IVdsServiceLoader;
#endif 	/* __IVdsServiceLoader_FWD_DEFINED__ */


#ifndef __IVdsService_FWD_DEFINED__
#define __IVdsService_FWD_DEFINED__
typedef interface IVdsService IVdsService;
#endif 	/* __IVdsService_FWD_DEFINED__ */


#ifndef __IVdsServiceInitialization_FWD_DEFINED__
#define __IVdsServiceInitialization_FWD_DEFINED__
typedef interface IVdsServiceInitialization IVdsServiceInitialization;
#endif 	/* __IVdsServiceInitialization_FWD_DEFINED__ */


#ifndef __IVdsVolumeMF_FWD_DEFINED__
#define __IVdsVolumeMF_FWD_DEFINED__
typedef interface IVdsVolumeMF IVdsVolumeMF;
#endif 	/* __IVdsVolumeMF_FWD_DEFINED__ */


#ifndef __IVdsDiskPath_FWD_DEFINED__
#define __IVdsDiskPath_FWD_DEFINED__
typedef interface IVdsDiskPath IVdsDiskPath;
#endif 	/* __IVdsDiskPath_FWD_DEFINED__ */


#ifndef __IVdsDiskSan_FWD_DEFINED__
#define __IVdsDiskSan_FWD_DEFINED__
typedef interface IVdsDiskSan IVdsDiskSan;
#endif 	/* __IVdsDiskSan_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

#ifdef __cplusplus
extern "C"{
#endif 

void * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void * ); 

/* interface __MIDL_itf_vds_0000 */
/* [local] */ 

//+--------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (c) 2000 Microsoft Corporation.
//
//---------------------------------------------------------------




// {9C38ED61-D565-4728-AEEE-C80952F0ECDE}
DEFINE_GUID(CLSID_VdsLoader, 
 0X9C38ED61,0xD565,0x4728,0xAE,0xEE,0xC8,0x09,0x52,0xF0,0xEC,0xDE);

// {7D1933CB-86F6-4A98-8628-01BE94C9A575}
DEFINE_GUID(CLSID_VdsService, 
 0x7D1933CB,0x86F6,0x4A98,0x86,0x28,0x01,0xBE,0x94,0xC9,0xA5,0x75);

#define	MAX_FS_NAME_SIZE	( 8 )

typedef GUID VDS_OBJECT_ID;

typedef 
enum _VDS_OBJECT_TYPE
    {	VDS_OT_UNKNOWN	= 0,
	VDS_OT_PROVIDER	= 1,
	VDS_OT_PACK	= 10,
	VDS_OT_VOLUME	= 11,
	VDS_OT_VOLUME_PLEX	= 12,
	VDS_OT_DISK	= 13,
	VDS_OT_SUB_SYSTEM	= 30,
	VDS_OT_CONTROLLER	= 31,
	VDS_OT_DRIVE	= 32,
	VDS_OT_LUN	= 33,
	VDS_OT_LUN_PLEX	= 34,
	VDS_OT_ASYNC	= 100,
	VDS_OT_ENUM	= 101
    } 	VDS_OBJECT_TYPE;

typedef 
enum _VDS_PROVIDER_TYPE
    {	VDS_PT_UNKNOWN	= 0,
	VDS_PT_SOFTWARE	= 1,
	VDS_PT_HARDWARE	= 2
    } 	VDS_PROVIDER_TYPE;

typedef 
enum _VDS_PROVIDER_FLAG
    {	VDS_PF_DYNAMIC	= 0x1,
	VDS_PF_INTERNAL_HARDWARE_PROVIDER	= 0x2,
	VDS_PF_ONE_DISK_ONLY_PER_PACK	= 0x4,
	VDS_PF_ONE_PACK_ONLINE_ONLY	= 0x8,
	VDS_PF_VOLUME_SPACE_MUST_BE_CONTIGUOUS	= 0x10,
	VDS_PF_SUPPORT_DYNAMIC	= 0x80000000,
	VDS_PF_SUPPORT_FAULT_TOLERANT	= 0x40000000,
	VDS_PF_SUPPORT_DYNAMIC_1394	= 0x20000000
    } 	VDS_PROVIDER_FLAG;

typedef 
enum _VDS_RECOVER_ACTION
    {	VDS_RA_UNKNOWN	= 0,
	VDS_RA_REFRESH	= 1,
	VDS_RA_RESTART	= 2
    } 	VDS_RECOVER_ACTION;

typedef 
enum _VDS_NOTIFICATION_TARGET_TYPE
    {	VDS_NTT_UNKNOWN	= 0,
	VDS_NTT_PACK	= VDS_OT_PACK,
	VDS_NTT_VOLUME	= VDS_OT_VOLUME,
	VDS_NTT_DISK	= VDS_OT_DISK,
	VDS_NTT_PARTITION	= 60,
	VDS_NTT_DRIVE_LETTER	= 61,
	VDS_NTT_FILE_SYSTEM	= 62,
	VDS_NTT_MOUNT_POINT	= 63,
	VDS_NTT_SUB_SYSTEM	= VDS_OT_SUB_SYSTEM,
	VDS_NTT_CONTROLLER	= VDS_OT_CONTROLLER,
	VDS_NTT_DRIVE	= VDS_OT_DRIVE,
	VDS_NTT_LUN	= VDS_OT_LUN,
	VDS_NTT_SERVICE	= 200
    } 	VDS_NOTIFICATION_TARGET_TYPE;

#define	VDS_NF_PACK_ARRIVE	( 1 )

#define	VDS_NF_PACK_DEPART	( 2 )

#define	VDS_NF_PACK_MODIFY	( 3 )

#define	VDS_NF_VOLUME_ARRIVE	( 4 )

#define	VDS_NF_VOLUME_DEPART	( 5 )

#define	VDS_NF_VOLUME_MODIFY	( 6 )

#define	VDS_NF_VOLUME_REBUILDING_PROGRESS	( 7 )

#define	VDS_NF_DISK_ARRIVE	( 8 )

#define	VDS_NF_DISK_DEPART	( 9 )

#define	VDS_NF_DISK_MODIFY	( 10 )

#define	VDS_NF_PARTITION_ARRIVE	( 11 )

#define	VDS_NF_PARTITION_DEPART	( 12 )

#define	VDS_NF_PARTITION_MODIFY	( 13 )

#define	VDS_NF_SUB_SYSTEM_ARRIVE	( 101 )

#define	VDS_NF_SUB_SYSTEM_DEPART	( 102 )

#define	VDS_NF_CONTROLLER_ARRIVE	( 103 )

#define	VDS_NF_CONTROLLER_DEPART	( 104 )

#define	VDS_NF_DRIVE_ARRIVE	( 105 )

#define	VDS_NF_DRIVE_DEPART	( 106 )

#define	VDS_NF_DRIVE_MODIFY	( 107 )

#define	VDS_NF_LUN_ARRIVE	( 108 )

#define	VDS_NF_LUN_DEPART	( 109 )

#define	VDS_NF_LUN_MODIFY	( 110 )

#define	VDS_NF_DRIVE_LETTER_FREE	( 201 )

#define	VDS_NF_DRIVE_LETTER_ASSIGN	( 202 )

#define	VDS_NF_FILE_SYSTEM_MODIFY	( 203 )

#define	VDS_NF_FILE_SYSTEM_FORMAT_PROGRESS	( 204 )

#define	VDS_NF_MOUNT_POINTS_CHANGE	( 205 )

#define	VDS_NF_SERVICE_OUT_OF_SYNC	( 301 )

typedef struct _VDS_PACK_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_OBJECT_ID packId;
    } 	VDS_PACK_NOTIFICATION;

typedef struct _VDS_DISK_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_OBJECT_ID diskId;
    } 	VDS_DISK_NOTIFICATION;

typedef struct _VDS_VOLUME_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_OBJECT_ID volumeId;
    VDS_OBJECT_ID plexId;
    ULONG ulPercentCompleted;
    } 	VDS_VOLUME_NOTIFICATION;

typedef struct _VDS_PARTITION_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_OBJECT_ID diskId;
    ULONGLONG ullOffset;
    } 	VDS_PARTITION_NOTIFICATION;

typedef struct _VDS_DRIVE_LETTER_NOTIFICATION
    {
    ULONG ulEvent;
    WCHAR wcLetter;
    VDS_OBJECT_ID volumeId;
    } 	VDS_DRIVE_LETTER_NOTIFICATION;

typedef struct _VDS_FILE_SYSTEM_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_OBJECT_ID volumeId;
    DWORD dwPercentCompleted;
    } 	VDS_FILE_SYSTEM_NOTIFICATION;

typedef struct _VDS_MOUNT_POINT_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_OBJECT_ID volumeId;
    } 	VDS_MOUNT_POINT_NOTIFICATION;

typedef struct _VDS_SERVICE_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_RECOVER_ACTION action;
    } 	VDS_SERVICE_NOTIFICATION;

typedef struct _VDS_SUB_SYSTEM_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_OBJECT_ID subSystemId;
    } 	VDS_SUB_SYSTEM_NOTIFICATION;

typedef struct _VDS_CONTROLLER_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_OBJECT_ID controllerId;
    } 	VDS_CONTROLLER_NOTIFICATION;

typedef struct _VDS_DRIVE_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_OBJECT_ID driveId;
    } 	VDS_DRIVE_NOTIFICATION;

typedef struct _VDS_LUN_NOTIFICATION
    {
    ULONG ulEvent;
    VDS_OBJECT_ID LunId;
    } 	VDS_LUN_NOTIFICATION;

typedef struct _VDS_NOTIFICATION
    {
    VDS_NOTIFICATION_TARGET_TYPE objectType;
    /* [switch_is] */ /* [switch_type] */ union 
        {
        /* [case()] */ VDS_PACK_NOTIFICATION Pack;
        /* [case()] */ VDS_DISK_NOTIFICATION Disk;
        /* [case()] */ VDS_VOLUME_NOTIFICATION Volume;
        /* [case()] */ VDS_PARTITION_NOTIFICATION Partition;
        /* [case()] */ VDS_DRIVE_LETTER_NOTIFICATION Letter;
        /* [case()] */ VDS_FILE_SYSTEM_NOTIFICATION FileSystem;
        /* [case()] */ VDS_MOUNT_POINT_NOTIFICATION MountPoint;
        /* [case()] */ VDS_SUB_SYSTEM_NOTIFICATION SubSystem;
        /* [case()] */ VDS_CONTROLLER_NOTIFICATION Controller;
        /* [case()] */ VDS_DRIVE_NOTIFICATION Drive;
        /* [case()] */ VDS_LUN_NOTIFICATION Lun;
        /* [case()] */ VDS_SERVICE_NOTIFICATION Service;
        /* [default] */  /* Empty union arm */ 
        } 	;
    } 	VDS_NOTIFICATION;

typedef 
enum _VDS_SERVICE_FLAG
    {	VDS_SVF_SUPPORT_DYNAMIC	= 0x1,
	VDS_SVF_SUPPORT_FAULT_TOLERANT	= 0x2,
	VDS_SVF_SUPPORT_GPT	= 0x4,
	VDS_SVF_SUPPORT_DYNAMIC_1394	= 0x8,
	VDS_SVF_CLUSTER_SERVICE_CONFIGURED	= 0x10,
	VDS_SVF_AUTO_MOUNT_OFF	= 0x20,
	VDS_SVF_OS_UNINSTALL_VALID	= 0x40
    } 	VDS_SERVICE_FLAG;

typedef struct _VDS_SERVICE_PROP
    {
    /* [string] */ LPWSTR pwszVersion;
    ULONG ulFlags;
    } 	VDS_SERVICE_PROP;

typedef struct VDS_REPARSE_POINT_PROP
    {
    VDS_OBJECT_ID SourceVolumeId;
    /* [string] */ LPWSTR pwszPath;
    } 	VDS_REPARSE_POINT_PROP;

typedef struct VDS_REPARSE_POINT_PROP *PVDS_REPARSE_POINT_PROP;

typedef 
enum _VDS_DRIVE_LETTER_FLAG
    {	VDS_DLF_NON_PERSISTENT	= 0x1
    } 	VDS_DRIVE_LETTER_FLAG;

typedef struct _VDS_DRIVE_LETTER_PROP
    {
    WCHAR wcLetter;
    VDS_OBJECT_ID volumeId;
    ULONG ulFlags;
    BOOL bUsed;
    } 	VDS_DRIVE_LETTER_PROP;

typedef struct _VDS_DRIVE_LETTER_PROP *PVDS_DRIVE_LETTER_PROP;

typedef /* [public][public][public] */ 
enum __MIDL___MIDL_itf_vds_0000_0002
    {	VDS_ASYNCOUT_UNKNOWN	= 0,
	VDS_ASYNCOUT_CREATEVOLUME	= 1,
	VDS_ASYNCOUT_EXTENDVOLUME	= 2,
	VDS_ASYNCOUT_SHRINKVOLUME	= 3,
	VDS_ASYNCOUT_ADDVOLUMEPLEX	= 4,
	VDS_ASYNCOUT_BREAKVOLUMEPLEX	= 5,
	VDS_ASYNCOUT_REMOVEVOLUMEPLEX	= 6,
	VDS_ASYNCOUT_REPAIRVOLUMEPLEX	= 7,
	VDS_ASYNCOUT_RECOVERPACK	= 8,
	VDS_ASYNCOUT_REPLACEDISK	= 9,
	VDS_ASYNCOUT_CREATEPARTITION	= 10,
	VDS_ASYNCOUT_CLEAN	= 11,
	VDS_ASYNCOUT_CREATELUN	= 50,
	VDS_ASYNCOUT_BREAKLUNPLEX	= 51,
	VDS_ASYNCOUT_ADDLUNPLEX	= 52,
	VDS_ASYNCOUT_REMOVELUNPLEX	= 53,
	VDS_ASYNCOUT_EXTENDLUN	= 54,
	VDS_ASYNCOUT_SHRINKLUN	= 55,
	VDS_ASYNCOUT_RECOVERLUN	= 56,
	VDS_ASYNCOUT_FORMAT	= 101
    } 	VDS_ASYNC_OUTPUT_TYPE;

typedef struct _VDS_ASYNC_OUTPUT
    {
    VDS_ASYNC_OUTPUT_TYPE type;
    /* [switch_is] */ /* [switch_type] */ union 
        {
        /* [case()] */ struct _cp
            {
            ULONGLONG ullOffset;
            VDS_OBJECT_ID volumeId;
            } 	cp;
        /* [case()] */ struct _cv
            {
            IUnknown *pVolumeUnk;
            } 	cv;
        /* [case()] */ struct _bvp
            {
            IUnknown *pVolumeUnk;
            } 	bvp;
        /* [case()] */ struct _cl
            {
            IUnknown *pLunUnk;
            } 	cl;
        /* [case()] */ struct _blp
            {
            IUnknown *pLunUnk;
            } 	blp;
        /* [default] */  /* Empty union arm */ 
        } 	;
    } 	VDS_ASYNC_OUTPUT;

#define	VDS_E_NOT_SUPPORTED	( 0x80042400L )

#define	VDS_E_INITIALIZED_FAILED	( 0x80042401L )

#define	VDS_E_INITIALIZE_NOT_CALLED	( 0x80042402L )

#define	VDS_E_ALREADY_REGISTERED	( 0x80042403L )

#define	VDS_E_ANOTHER_CALL_IN_PROGRESS	( 0x80042404L )

#define	VDS_E_OBJECT_NOT_FOUND	( 0x80042405L )

#define	VDS_E_INVALID_SPACE	( 0x80042406L )

#define	VDS_E_PARTITION_LIMIT_REACHED	( 0x80042407L )

#define	VDS_E_PARTITION_NOT_EMPTY	( 0x80042408L )

#define	VDS_E_OPERATION_PENDING	( 0x80042409L )

#define	VDS_E_OPERATION_DENIED	( 0x8004240aL )

#define	VDS_E_OBJECT_DELETED	( 0x8004240bL )

#define	VDS_E_CANCEL_TOO_LATE	( 0x8004240cL )

#define	VDS_E_OPERATION_CANCELED	( 0x8004240dL )

#define	VDS_E_CANNOT_EXTEND	( 0x8004240eL )

#define	VDS_E_NOT_ENOUGH_SPACE	( 0x8004240fL )

#define	VDS_E_NOT_ENOUGH_DRIVE	( 0x80042410L )

#define	VDS_E_BAD_COOKIE	( 0x80042411L )

#define	VDS_E_NO_MEDIA	( 0x80042412L )

#define	VDS_E_DEVICE_IN_USE	( 0x80042413L )

#define	VDS_E_DISK_NOT_EMPTY	( 0x80042414L )

#define	VDS_E_INVALID_OPERATION	( 0x80042415L )

#define	VDS_E_PATH_NOT_FOUND	( 0x80042416L )

#define	VDS_E_DISK_NOT_INITIALIZED	( 0x80042417L )

#define	VDS_E_NOT_AN_UNALLOCATED_DISK	( 0x80042418L )

#define	VDS_E_UNRECOVERABLE_ERROR	( 0x80042419L )

#define	VDS_S_DISK_PARTIALLY_CLEANED	( 0x4241aL )

#define	VDS_E_DMADMIN_SERVICE_CONNECTION_FAILED	( 0x8004241bL )

#define	VDS_E_PROVIDER_INITIALIZATION_FAILED	( 0x8004241cL )

#define	VDS_E_OBJECT_EXISTS	( 0x8004241dL )

#define	VDS_E_NO_DISKS_FOUND	( 0x8004241eL )

#define	VDS_E_PROVIDER_CACHE_CORRUPT	( 0x8004241fL )

#define	VDS_E_DMADMIN_METHOD_CALL_FAILED	( 0x80042420L )

#define	VDS_S_PROVIDER_ERROR_LOADING_CACHE	( 0x42421L )

#define	VDS_E_PROVIDER_VOL_DEVICE_NAME_NOT_FOUND	( 0x80042422L )

#define	VDS_E_PROVIDER_VOL_OPEN	( 0x80042423L )

#define	VDS_E_DMADMIN_CORRUPT_NOTIFICATION	( 0x80042424L )

#define	VDS_E_INCOMPATIBLE_FILE_SYSTEM	( 0x80042425L )

#define	VDS_E_INCOMPATIBLE_MEDIA	( 0x80042426L )

#define	VDS_E_ACCESS_DENIED	( 0x80042427L )

#define	VDS_E_MEDIA_WRITE_PROTECTED	( 0x80042428L )

#define	VDS_E_BAD_LABEL	( 0x80042429L )

#define	VDS_E_CANT_QUICK_FORMAT	( 0x8004242aL )

#define	VDS_E_IO_ERROR	( 0x8004242bL )

#define	VDS_E_VOLUME_TOO_SMALL	( 0x8004242cL )

#define	VDS_E_VOLUME_TOO_BIG	( 0x8004242dL )

#define	VDS_E_CLUSTER_SIZE_TOO_SMALL	( 0x8004242eL )

#define	VDS_E_CLUSTER_SIZE_TOO_BIG	( 0x8004242fL )

#define	VDS_E_CLUSTER_COUNT_BEYOND_32BITS	( 0x80042430L )

#define	VDS_E_OBJECT_STATUS_FAILED	( 0x80042431L )

#define	VDS_E_VOLUME_INCOMPLETE	( 0x80042432L )

#define	VDS_E_EXTENT_SIZE_LESS_THAN_MIN	( 0x80042433L )

#define	VDS_S_UPDATE_BOOTFILE_FAILED	( 0x42434L )

#define	VDS_S_BOOT_PARTITION_NUMBER_CHANGE	( 0x42436L )

#define	VDS_E_BOOT_PARTITION_NUMBER_CHANGE	( 0x80042436L )

#define	VDS_E_NO_FREE_SPACE	( 0x80042437L )

#define	VDS_E_ACTIVE_PARTITION	( 0x80042438L )

#define	VDS_E_PARTITION_OF_UNKNOWN_TYPE	( 0x80042439L )

#define	VDS_E_LEGACY_VOLUME_FORMAT	( 0x8004243aL )

#define	VDS_E_NON_CONTIGUOUS_DATA_PARTITIONS	( 0x8004243bL )

#define	VDS_E_MIGRATE_OPEN_VOLUME	( 0x8004243cL )

#define	VDS_E_ONLINE_PACK_EXISTS	( 0x8004243cL )

#define	VDS_E_VOLUME_NOT_ONLINE	( 0x8004243dL )

#define	VDS_E_VOLUME_NOT_HEALTHY	( 0x8004243eL )

#define	VDS_E_VOLUME_SPANS_DISKS	( 0x8004243fL )

#define	VDS_E_REQUIRES_CONTIGUOUS_DISK_SPACE	( 0x80042440L )

#define	VDS_E_BAD_PROVIDER_DATA	( 0x80042441L )

#define	VDS_E_PROVIDER_FAILURE	( 0x80042442L )

#define	VDS_S_VOLUME_COMPRESS_FAILED	( 0x42443L )

#define	VDS_E_PACK_OFFLINE	( 0x80042444L )

#define	VDS_E_VOLUME_NOT_A_MIRROR	( 0x80042445L )

#define	VDS_E_NO_EXTENTS_FOR_VOLUME	( 0x80042446L )

#define	VDS_E_DISK_NOT_LOADED_TO_CACHE	( 0x80042447L )

#define	VDS_E_INTERNAL_ERROR	( 0x80042448L )

#define	VDS_S_ACCESS_PATH_NOT_DELETED	( 0x42449L )

#define	VDS_E_PROVIDER_TYPE_NOT_SUPPORTED	( 0x8004244aL )

#define	VDS_E_DISK_NOT_ONLINE	( 0x8004244bL )

#define	VDS_E_DISK_IN_USE_BY_VOLUME	( 0x8004244cL )

#define	VDS_S_IN_PROGRESS	( 0x4244dL )

#define	VDS_E_ASYNC_OBJECT_FAILURE	( 0x8004244eL )

#define	VDS_E_VOLUME_NOT_MOUNTED	( 0x8004244fL )

#define	VDS_E_PACK_NOT_FOUND	( 0x80042450L )

#define	VDS_E_IMPORT_SET_INCOMPLETE	( 0x80042451L )

#define	VDS_E_DISK_NOT_IMPORTED	( 0x80042452L )

#define	VDS_E_OBJECT_OUT_OF_SYNC	( 0x80042453L )

#define	VDS_E_MISSING_DISK	( 0x80042454L )

#define	VDS_E_DISK_PNP_REG_CORRUPT	( 0x80042455L )

#define	VDS_E_LBN_REMAP_ENABLED_FLAG	( 0x80042456L )

#define	VDS_E_NO_DRIVELETTER_FLAG	( 0x80042457L )

#define	VDS_E_REVERT_ON_CLOSE	( 0x80042458L )

#define	VDS_E_REVERT_ON_CLOSE_SET	( 0x80042459L )

#define	VDS_E_REVERT_ON_CLOSE_MISMATCH	( 0x80042459L )

#define	VDS_E_IA64_BOOT_MIRRORED_TO_MBR	( 0x8004245aL )

#define	VDS_S_IA64_BOOT_MIRRORED_TO_MBR	( 0x4245aL )

#define	VDS_S_UNABLE_TO_GET_GPT_ATTRIBUTES	( 0x4245bL )

#define	VDS_E_VOLUME_TEMPORARILY_DISMOUNTED	( 0x8004245cL )

#define	VDS_E_VOLUME_PERMANENTLY_DISMOUNTED	( 0x8004245dL )

#define	VDS_E_VOLUME_HAS_PATH	( 0x8004245eL )

#define	VDS_E_TIMEOUT	( 0x8004245fL )

#define	VDS_E_REPAIR_VOLUMESTATE	( 0x80042460L )

#define	VDS_E_LDM_TIMEOUT	( 0x80042461L )

#define	VDS_E_RETRY	( 0x80042463L )





typedef 
enum _VDS_HEALTH
    {	VDS_H_UNKNOWN	= 0,
	VDS_H_HEALTHY	= 1,
	VDS_H_REBUILDING	= 2,
	VDS_H_STALE	= 3,
	VDS_H_FAILING	= 4,
	VDS_H_FAILING_REDUNDANCY	= 5,
	VDS_H_FAILED_REDUNDANCY	= 6,
	VDS_H_FAILED_REDUNDANCY_FAILING	= 7,
	VDS_H_FAILED	= 8
    } 	VDS_HEALTH;

typedef 
enum _VDS_TRANSITION_STATE
    {	VDS_TS_UNKNOWN	= 0,
	VDS_TS_STABLE	= 1,
	VDS_TS_EXTENDING	= 2,
	VDS_TS_SHRINKING	= 3,
	VDS_TS_RECONFIGING	= 4
    } 	VDS_TRANSITION_STATE;

typedef 
enum _VDS_FILE_SYSTEM_TYPE
    {	VDS_FST_UNKNOWN	= 0,
	VDS_FST_RAW	= VDS_FST_UNKNOWN + 1,
	VDS_FST_FAT	= VDS_FST_RAW + 1,
	VDS_FST_FAT32	= VDS_FST_FAT + 1,
	VDS_FST_NTFS	= VDS_FST_FAT32 + 1,
	VDS_FST_CDFS	= VDS_FST_NTFS + 1,
	VDS_FST_UDF	= VDS_FST_CDFS + 1
    } 	VDS_FILE_SYSTEM_TYPE;

typedef struct _VDS_PROVIDER_PROP
    {
    VDS_OBJECT_ID id;
    /* [string] */ LPWSTR pwszName;
    GUID guidVersionId;
    /* [string] */ LPWSTR pwszVersion;
    VDS_PROVIDER_TYPE type;
    ULONG ulFlags;
    ULONG ulStripeSizeFlags;
    SHORT sRebuildPriority;
    } 	VDS_PROVIDER_PROP;

typedef 
enum _VDS_STORAGE_IDENTIFIER_CODE_SET
    {	VDSStorageIdCodeSetReserved	= 0,
	VDSStorageIdCodeSetBinary	= 1,
	VDSStorageIdCodeSetAscii	= 2
    } 	VDS_STORAGE_IDENTIFIER_CODE_SET;

typedef 
enum _VDS_STORAGE_IDENTIFIER_TYPE
    {	VDSStorageIdTypeVendorSpecific	= 0,
	VDSStorageIdTypeVendorId	= 1,
	VDSStorageIdTypeEUI64	= 2,
	VDSStorageIdTypeFCPHName	= 3
    } 	VDS_STORAGE_IDENTIFIER_TYPE;

typedef 
enum _VDS_STORAGE_BUS_TYPE
    {	VDSBusTypeUnknown	= 0,
	VDSBusTypeScsi	= VDSBusTypeUnknown + 1,
	VDSBusTypeAtapi	= VDSBusTypeScsi + 1,
	VDSBusTypeAta	= VDSBusTypeAtapi + 1,
	VDSBusType1394	= VDSBusTypeAta + 1,
	VDSBusTypeSsa	= VDSBusType1394 + 1,
	VDSBusTypeFibre	= VDSBusTypeSsa + 1,
	VDSBusTypeUsb	= VDSBusTypeFibre + 1,
	VDSBusTypeRAID	= VDSBusTypeUsb + 1,
	VDSBusTypeMaxReserved	= 0x7f
    } 	VDS_STORAGE_BUS_TYPE;

typedef struct _VDS_STORAGE_IDENTIFIER
    {
    VDS_STORAGE_IDENTIFIER_CODE_SET m_CodeSet;
    VDS_STORAGE_IDENTIFIER_TYPE m_Type;
    ULONG m_cbIdentifier;
    /* [size_is] */ BYTE *m_rgbIdentifier;
    } 	VDS_STORAGE_IDENTIFIER;

typedef struct _VDS_STORAGE_DEVICE_ID_DESCRIPTOR
    {
    ULONG m_version;
    ULONG m_cIdentifiers;
    /* [size_is] */ VDS_STORAGE_IDENTIFIER *m_rgIdentifiers;
    } 	VDS_STORAGE_DEVICE_ID_DESCRIPTOR;

typedef 
enum _VDS_INTERCONNECT_ADDRESS_TYPE
    {	VDS_IA_UNKNOWN	= 0,
	VDS_IA_FCFS	= VDS_IA_UNKNOWN + 1,
	VDS_IA_FCPH	= VDS_IA_FCFS + 1,
	VDS_IA_FCPH3	= VDS_IA_FCPH + 1,
	VDS_IA_MAC	= VDS_IA_FCPH3 + 1,
	VDS_IA_SCSI	= VDS_IA_MAC + 1
    } 	VDS_INTERCONNECT_ADDRESS_TYPE;

typedef struct _VDS_INTERCONNECT
    {
    VDS_INTERCONNECT_ADDRESS_TYPE m_addressType;
    ULONG m_cbPort;
    /* [size_is] */ BYTE *m_pbPort;
    ULONG m_cbAddress;
    /* [size_is] */ BYTE *m_pbAddress;
    } 	VDS_INTERCONNECT;

typedef struct _VDS_LUN_INFORMATION
    {
    ULONG m_version;
    BYTE m_DeviceType;
    BYTE m_DeviceTypeModifier;
    BOOL m_bCommandQueueing;
    VDS_STORAGE_BUS_TYPE m_BusType;
    /* [string] */ char *m_szVendorId;
    /* [string] */ char *m_szProductId;
    /* [string] */ char *m_szProductRevision;
    /* [string] */ char *m_szSerialNumber;
    GUID m_diskSignature;
    VDS_STORAGE_DEVICE_ID_DESCRIPTOR m_deviceIdDescriptor;
    ULONG m_cInterconnects;
    /* [size_is] */ VDS_INTERCONNECT *m_rgInterconnects;
    } 	VDS_LUN_INFORMATION;

#define	VER_VDS_LUN_INFORMATION	( 1 )



extern RPC_IF_HANDLE __MIDL_itf_vds_0000_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_vds_0000_v0_0_s_ifspec;

#ifndef __IEnumVdsObject_INTERFACE_DEFINED__
#define __IEnumVdsObject_INTERFACE_DEFINED__

/* interface IEnumVdsObject */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IEnumVdsObject;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("118610b7-8d94-4030-b5b8-500889788e4e")
    IEnumVdsObject : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Next( 
            /* [in] */ ULONG celt,
            /* [length_is][size_is][out] */ IUnknown **ppObjectArray,
            /* [out] */ ULONG *pcFetched) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Skip( 
            /* [in] */ ULONG celt) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Reset( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Clone( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IEnumVdsObjectVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IEnumVdsObject * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IEnumVdsObject * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IEnumVdsObject * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Next )( 
            IEnumVdsObject * This,
            /* [in] */ ULONG celt,
            /* [length_is][size_is][out] */ IUnknown **ppObjectArray,
            /* [out] */ ULONG *pcFetched);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Skip )( 
            IEnumVdsObject * This,
            /* [in] */ ULONG celt);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Reset )( 
            IEnumVdsObject * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Clone )( 
            IEnumVdsObject * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        END_INTERFACE
    } IEnumVdsObjectVtbl;

    interface IEnumVdsObject
    {
        CONST_VTBL struct IEnumVdsObjectVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IEnumVdsObject_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IEnumVdsObject_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IEnumVdsObject_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IEnumVdsObject_Next(This,celt,ppObjectArray,pcFetched)	\
    (This)->lpVtbl -> Next(This,celt,ppObjectArray,pcFetched)

#define IEnumVdsObject_Skip(This,celt)	\
    (This)->lpVtbl -> Skip(This,celt)

#define IEnumVdsObject_Reset(This)	\
    (This)->lpVtbl -> Reset(This)

#define IEnumVdsObject_Clone(This,ppEnum)	\
    (This)->lpVtbl -> Clone(This,ppEnum)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IEnumVdsObject_Next_Proxy( 
    IEnumVdsObject * This,
    /* [in] */ ULONG celt,
    /* [length_is][size_is][out] */ IUnknown **ppObjectArray,
    /* [out] */ ULONG *pcFetched);


void __RPC_STUB IEnumVdsObject_Next_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IEnumVdsObject_Skip_Proxy( 
    IEnumVdsObject * This,
    /* [in] */ ULONG celt);


void __RPC_STUB IEnumVdsObject_Skip_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IEnumVdsObject_Reset_Proxy( 
    IEnumVdsObject * This);


void __RPC_STUB IEnumVdsObject_Reset_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IEnumVdsObject_Clone_Proxy( 
    IEnumVdsObject * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IEnumVdsObject_Clone_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IEnumVdsObject_INTERFACE_DEFINED__ */


#ifndef __IVdsAdviseSink_INTERFACE_DEFINED__
#define __IVdsAdviseSink_INTERFACE_DEFINED__

/* interface IVdsAdviseSink */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsAdviseSink;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("8326cd1d-cf59-4936-b786-5efc08798e25")
    IVdsAdviseSink : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE OnNotify( 
            /* [range][in] */ LONG lNumberOfNotifications,
            /* [size_is][in] */ VDS_NOTIFICATION *pNotificationArray) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsAdviseSinkVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsAdviseSink * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsAdviseSink * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsAdviseSink * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *OnNotify )( 
            IVdsAdviseSink * This,
            /* [range][in] */ LONG lNumberOfNotifications,
            /* [size_is][in] */ VDS_NOTIFICATION *pNotificationArray);
        
        END_INTERFACE
    } IVdsAdviseSinkVtbl;

    interface IVdsAdviseSink
    {
        CONST_VTBL struct IVdsAdviseSinkVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsAdviseSink_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsAdviseSink_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsAdviseSink_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsAdviseSink_OnNotify(This,lNumberOfNotifications,pNotificationArray)	\
    (This)->lpVtbl -> OnNotify(This,lNumberOfNotifications,pNotificationArray)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAdviseSink_OnNotify_Proxy( 
    IVdsAdviseSink * This,
    /* [range][in] */ LONG lNumberOfNotifications,
    /* [size_is][in] */ VDS_NOTIFICATION *pNotificationArray);


void __RPC_STUB IVdsAdviseSink_OnNotify_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsAdviseSink_INTERFACE_DEFINED__ */


#ifndef __IVdsProvider_INTERFACE_DEFINED__
#define __IVdsProvider_INTERFACE_DEFINED__

/* interface IVdsProvider */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsProvider;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("10c5e575-7984-4e81-a56b-431f5f92ae42")
    IVdsProvider : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_PROVIDER_PROP *pProviderProp) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsProviderVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsProvider * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsProvider * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsProvider * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsProvider * This,
            /* [out] */ VDS_PROVIDER_PROP *pProviderProp);
        
        END_INTERFACE
    } IVdsProviderVtbl;

    interface IVdsProvider
    {
        CONST_VTBL struct IVdsProviderVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsProvider_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsProvider_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsProvider_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsProvider_GetProperties(This,pProviderProp)	\
    (This)->lpVtbl -> GetProperties(This,pProviderProp)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsProvider_GetProperties_Proxy( 
    IVdsProvider * This,
    /* [out] */ VDS_PROVIDER_PROP *pProviderProp);


void __RPC_STUB IVdsProvider_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsProvider_INTERFACE_DEFINED__ */


#ifndef __IVdsAsync_INTERFACE_DEFINED__
#define __IVdsAsync_INTERFACE_DEFINED__

/* interface IVdsAsync */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsAsync;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("d5d23b6d-5a55-4492-9889-397a3c2d2dbc")
    IVdsAsync : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Cancel( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Wait( 
            /* [out] */ HRESULT *pHrResult,
            /* [out] */ VDS_ASYNC_OUTPUT *pAsyncOut) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryStatus( 
            /* [out] */ HRESULT *pHrResult,
            /* [out] */ ULONG *pulPercentCompleted) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsAsyncVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsAsync * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsAsync * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsAsync * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Cancel )( 
            IVdsAsync * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Wait )( 
            IVdsAsync * This,
            /* [out] */ HRESULT *pHrResult,
            /* [out] */ VDS_ASYNC_OUTPUT *pAsyncOut);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryStatus )( 
            IVdsAsync * This,
            /* [out] */ HRESULT *pHrResult,
            /* [out] */ ULONG *pulPercentCompleted);
        
        END_INTERFACE
    } IVdsAsyncVtbl;

    interface IVdsAsync
    {
        CONST_VTBL struct IVdsAsyncVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsAsync_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsAsync_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsAsync_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsAsync_Cancel(This)	\
    (This)->lpVtbl -> Cancel(This)

#define IVdsAsync_Wait(This,pHrResult,pAsyncOut)	\
    (This)->lpVtbl -> Wait(This,pHrResult,pAsyncOut)

#define IVdsAsync_QueryStatus(This,pHrResult,pulPercentCompleted)	\
    (This)->lpVtbl -> QueryStatus(This,pHrResult,pulPercentCompleted)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAsync_Cancel_Proxy( 
    IVdsAsync * This);


void __RPC_STUB IVdsAsync_Cancel_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAsync_Wait_Proxy( 
    IVdsAsync * This,
    /* [out] */ HRESULT *pHrResult,
    /* [out] */ VDS_ASYNC_OUTPUT *pAsyncOut);


void __RPC_STUB IVdsAsync_Wait_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAsync_QueryStatus_Proxy( 
    IVdsAsync * This,
    /* [out] */ HRESULT *pHrResult,
    /* [out] */ ULONG *pulPercentCompleted);


void __RPC_STUB IVdsAsync_QueryStatus_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsAsync_INTERFACE_DEFINED__ */


/* interface __MIDL_itf_vds_0262 */
/* [local] */ 









typedef 
enum _VDS_PACK_STATUS
    {	VDS_PS_UNKNOWN	= 0,
	VDS_PS_ONLINE	= 1,
	VDS_PS_OFFLINE	= 4
    } 	VDS_PACK_STATUS;

typedef 
enum _VDS_PACK_FLAG
    {	VDS_PKF_FOREIGN	= 0x1,
	VDS_PKF_NOQUORUM	= 0x2,
	VDS_PKF_POLICY	= 0x4,
	VDS_PKF_CORRUPTED	= 0x8,
	VDS_PKF_ONLINE_ERROR	= 0x10
    } 	VDS_PACK_FLAG;

typedef 
enum _VDS_DISK_STATUS
    {	VDS_DS_UNKNOWN	= 0,
	VDS_DS_ONLINE	= 1,
	VDS_DS_NOT_READY	= 2,
	VDS_DS_NO_MEDIA	= 3,
	VDS_DS_FAILED	= 5,
	VDS_DS_MISSING	= 6
    } 	VDS_DISK_STATUS;

typedef 
enum _VDS_PARTITION_STYLE
    {	VDS_PST_UNKNOWN	= 0,
	VDS_PST_MBR	= 1,
	VDS_PST_GPT	= 2
    } 	VDS_PARTITION_STYLE;

typedef 
enum _VDS_DISK_FLAG
    {	VDS_DF_AUDIO_CD	= 0x1,
	VDS_DF_HOTSPARE	= 0x2,
	VDS_DF_RESERVE_CAPABLE	= 0x4,
	VDS_DF_MASKED	= 0x8,
	VDS_DF_STYLE_CONVERTIBLE	= 0x10,
	VDS_DF_CLUSTERED	= 0x20
    } 	VDS_DISK_FLAG;

typedef 
enum _VDS_PARTITION_FLAG
    {	VDS_PTF_SYSTEM	= 0x1
    } 	VDS_PARTITION_FLAG;

typedef 
enum _VDS_LUN_RESERVE_MODE
    {	VDS_LRM_NONE	= 0,
	VDS_LRM_EXCLUSIVE_RW	= 1,
	VDS_LRM_EXCLUSIVE_RO	= 2,
	VDS_LRM_SHARED_RO	= 3,
	VDS_LRM_SHARED_RW	= 4
    } 	VDS_LUN_RESERVE_MODE;

typedef 
enum _VDS_VOLUME_STATUS
    {	VDS_VS_UNKNOWN	= 0,
	VDS_VS_ONLINE	= 1,
	VDS_VS_NO_MEDIA	= 3,
	VDS_VS_FAILED	= 5
    } 	VDS_VOLUME_STATUS;

typedef 
enum _VDS_VOLUME_TYPE
    {	VDS_VT_UNKNOWN	= 0,
	VDS_VT_SIMPLE	= 10,
	VDS_VT_SPAN	= 11,
	VDS_VT_STRIPE	= 12,
	VDS_VT_MIRROR	= 13,
	VDS_VT_PARITY	= 14
    } 	VDS_VOLUME_TYPE;

typedef 
enum _VDS_VOLUME_FLAG
    {	VDS_VF_SYSTEM_VOLUME	= 0x1,
	VDS_VF_BOOT_VOLUME	= 0x2,
	VDS_VF_ACTIVE	= 0x4,
	VDS_VF_READONLY	= 0x8,
	VDS_VF_HIDDEN	= 0x10,
	VDS_VF_CAN_EXTEND	= 0x20,
	VDS_VF_CAN_SHRINK	= 0x40,
	VDS_VF_PAGEFILE	= 0x80,
	VDS_VF_HIBERNATION	= 0x100,
	VDS_VF_CRASHDUMP	= 0x200,
	VDS_VF_INSTALLABLE	= 0x400,
	VDS_VF_LBN_REMAP_ENABLED	= 0x800,
	VDS_VF_FORMATTING	= 0x1000,
	VDS_VF_NOT_FORMATTABLE	= 0x2000,
	VDS_VF_NTFS_NOT_SUPPORTED	= 0x4000,
	VDS_VF_FAT32_NOT_SUPPORTED	= 0x8000,
	VDS_VF_FAT_NOT_SUPPORTED	= 0x10000,
	VDS_VF_NO_DEFAULT_DRIVE_LETTER	= 0x20000,
	VDS_VF_PERMANENTLY_DISMOUNTED	= 0x40000,
	VDS_VF_PERMANENT_DISMOUNT_SUPPORTED	= 0x80000
    } 	VDS_VOLUME_FLAG;

typedef 
enum _VDS_VOLUME_PLEX_TYPE
    {	VDS_VPT_UNKNOWN	= 0,
	VDS_VPT_SIMPLE	= VDS_VT_SIMPLE,
	VDS_VPT_SPAN	= VDS_VT_SPAN,
	VDS_VPT_STRIPE	= VDS_VT_STRIPE,
	VDS_VPT_PARITY	= VDS_VT_PARITY
    } 	VDS_VOLUME_PLEX_TYPE;

typedef 
enum _VDS_VOLUME_PLEX_STATUS
    {	VDS_VPS_UNKNOWN	= 0,
	VDS_VPS_ONLINE	= 1,
	VDS_VPS_NO_MEDIA	= 3,
	VDS_VPS_FAILED	= 5
    } 	VDS_VOLUME_PLEX_STATUS;

typedef 
enum _VDS_DISK_EXTENT_TYPE
    {	VDS_DET_UNKNOWN	= 0,
	VDS_DET_FREE	= 1,
	VDS_DET_DATA	= 2,
	VDS_DET_OEM	= 3,
	VDS_DET_ESP	= 4,
	VDS_DET_MSR	= 5,
	VDS_DET_LDM	= 6,
	VDS_DET_UNUSABLE	= 0x7fff
    } 	VDS_DISK_EXTENT_TYPE;

typedef struct _VDS_PACK_PROP
    {
    VDS_OBJECT_ID id;
    /* [string] */ LPWSTR pwszName;
    VDS_PACK_STATUS status;
    ULONG ulFlags;
    } 	VDS_PACK_PROP;

typedef struct _VDS_PACK_PROP *PVDS_PACK_PROP;

typedef struct _VDS_DISK_PROP
    {
    VDS_OBJECT_ID id;
    VDS_DISK_STATUS status;
    VDS_LUN_RESERVE_MODE ReserveMode;
    VDS_HEALTH health;
    DWORD dwDeviceType;
    DWORD dwMediaType;
    ULONGLONG ullSize;
    ULONG ulBytesPerSector;
    ULONG ulSectorsPerTrack;
    ULONG ulTracksPerCylinder;
    ULONG ulFlags;
    VDS_STORAGE_BUS_TYPE BusType;
    VDS_PARTITION_STYLE PartitionStyle;
    /* [switch_is] */ /* [switch_type] */ union 
        {
        /* [case()] */ DWORD dwSignature;
        /* [case()] */ GUID DiskGuid;
        /* [default] */  /* Empty union arm */ 
        } 	;
    /* [string] */ LPWSTR pwszDiskAddress;
    /* [string] */ LPWSTR pwszName;
    /* [string] */ LPWSTR pwszFriendlyName;
    /* [string] */ LPWSTR pwszAdaptorName;
    /* [string] */ LPWSTR pwszDevicePath;
    } 	VDS_DISK_PROP;

typedef struct _VDS_DISK_PROP *PVDS_DISK_PROP;

typedef struct _VDS_VOLUME_PROP
    {
    VDS_OBJECT_ID id;
    VDS_VOLUME_TYPE type;
    VDS_VOLUME_STATUS status;
    VDS_HEALTH health;
    VDS_TRANSITION_STATE TransitionState;
    ULONGLONG ullSize;
    ULONG ulFlags;
    VDS_FILE_SYSTEM_TYPE RecommendedFileSystemType;
    /* [string] */ LPWSTR pwszName;
    } 	VDS_VOLUME_PROP;

typedef struct _VDS_VOLUME_PROP *PVDS_VOLUME_PROP;

typedef struct _VDS_VOLUME_PLEX_PROP
    {
    VDS_OBJECT_ID id;
    VDS_VOLUME_PLEX_TYPE type;
    VDS_VOLUME_PLEX_STATUS status;
    VDS_HEALTH health;
    VDS_TRANSITION_STATE TransitionState;
    ULONGLONG ullSize;
    ULONG ulStripeSize;
    ULONG ulNumberOfMembers;
    } 	VDS_VOLUME_PLEX_PROP;

typedef struct _VDS_VOLUME_PLEX_PROP *PVDS_VOLUME_PLEX_PROP;

typedef struct _VDS_DISK_EXTENT
    {
    VDS_OBJECT_ID diskId;
    VDS_DISK_EXTENT_TYPE type;
    ULONGLONG ullOffset;
    ULONGLONG ullSize;
    VDS_OBJECT_ID volumeId;
    VDS_OBJECT_ID plexId;
    ULONG memberIdx;
    } 	VDS_DISK_EXTENT;

typedef struct _VDS_DISK_EXTENT *PVDS_DISK_EXTENT;

typedef struct _VDS_INPUT_DISK
    {
    VDS_OBJECT_ID diskId;
    ULONGLONG ullSize;
    VDS_OBJECT_ID plexId;
    ULONG memberIdx;
    } 	VDS_INPUT_DISK;

#define GPT_PARTITION_NAME_LENGTH	36
typedef struct _VDS_PARTITION_INFO_GPT
    {
    GUID partitionType;
    GUID partitionId;
    ULONGLONG attributes;
    WCHAR name[ 36 ];
    } 	VDS_PARTITION_INFO_GPT;

typedef struct _VDS_PARTITION_INFO_MBR
    {
    BYTE partitionType;
    BOOLEAN bootIndicator;
    BOOLEAN recognizedPartition;
    DWORD hiddenSectors;
    } 	VDS_PARTITION_INFO_MBR;

typedef struct _VDS_PARTITION_PROP
    {
    VDS_PARTITION_STYLE PartitionStyle;
    ULONG ulFlags;
    ULONG ulPartitionNumber;
    ULONGLONG ullOffset;
    ULONGLONG ullSize;
    /* [switch_is] */ /* [switch_type] */ union 
        {
        /* [case()] */ VDS_PARTITION_INFO_MBR Mbr;
        /* [case()] */ VDS_PARTITION_INFO_GPT Gpt;
        /* [default] */  /* Empty union arm */ 
        } 	;
    } 	VDS_PARTITION_PROP;

typedef 
enum tag_VDS_PARTITION_STYLE
    {	VDS_PARTITION_STYLE_MBR	= 0,
	VDS_PARTITION_STYLE_GPT	= VDS_PARTITION_STYLE_MBR + 1,
	VDS_PARTITION_STYLE_RAW	= VDS_PARTITION_STYLE_GPT + 1
    } 	__VDS_PARTITION_STYLE;

typedef struct _VDS_PARTITION_INFORMATION_EX
    {
    __VDS_PARTITION_STYLE dwPartitionStyle;
    ULONGLONG ullStartingOffset;
    ULONGLONG ullPartitionLength;
    DWORD dwPartitionNumber;
    BOOLEAN bRewritePartition;
    /* [switch_is] */ /* [switch_type] */ union 
        {
        /* [case()] */ VDS_PARTITION_INFO_MBR Mbr;
        /* [case()] */ VDS_PARTITION_INFO_GPT Gpt;
        } 	;
    } 	VDS_PARTITION_INFORMATION_EX;

typedef struct _VDS_DRIVE_LAYOUT_INFORMATION_MBR
    {
    DWORD dwSignature;
    } 	VDS_DRIVE_LAYOUT_INFORMATION_MBR;

typedef struct _VDS_DRIVE_LAYOUT_INFORMATION_GPT
    {
    GUID DiskGuid;
    ULONGLONG ullStartingUsableOffset;
    ULONGLONG ullUsableLength;
    DWORD dwMaxPartitionCount;
    } 	VDS_DRIVE_LAYOUT_INFORMATION_GPT;

typedef struct _VDS_DRIVE_LAYOUT_INFORMATION_EX
    {
    DWORD dwPartitionStyle;
    DWORD dwPartitionCount;
    /* [switch_is] */ /* [switch_type] */ union 
        {
        /* [case()] */ VDS_DRIVE_LAYOUT_INFORMATION_MBR Mbr;
        /* [case()] */ VDS_DRIVE_LAYOUT_INFORMATION_GPT Gpt;
        } 	;
    /* [size_is] */ VDS_PARTITION_INFORMATION_EX PartitionEntry[ 1 ];
    } 	VDS_DRIVE_LAYOUT_INFORMATION_EX;



extern RPC_IF_HANDLE __MIDL_itf_vds_0262_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_vds_0262_v0_0_s_ifspec;

#ifndef __IVdsSwProvider_INTERFACE_DEFINED__
#define __IVdsSwProvider_INTERFACE_DEFINED__

/* interface IVdsSwProvider */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsSwProvider;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("9aa58360-ce33-4f92-b658-ed24b14425b8")
    IVdsSwProvider : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryPacks( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE CreatePack( 
            /* [out] */ IVdsPack **ppPack) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsSwProviderVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsSwProvider * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsSwProvider * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsSwProvider * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryPacks )( 
            IVdsSwProvider * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *CreatePack )( 
            IVdsSwProvider * This,
            /* [out] */ IVdsPack **ppPack);
        
        END_INTERFACE
    } IVdsSwProviderVtbl;

    interface IVdsSwProvider
    {
        CONST_VTBL struct IVdsSwProviderVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsSwProvider_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsSwProvider_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsSwProvider_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsSwProvider_QueryPacks(This,ppEnum)	\
    (This)->lpVtbl -> QueryPacks(This,ppEnum)

#define IVdsSwProvider_CreatePack(This,ppPack)	\
    (This)->lpVtbl -> CreatePack(This,ppPack)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSwProvider_QueryPacks_Proxy( 
    IVdsSwProvider * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsSwProvider_QueryPacks_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSwProvider_CreatePack_Proxy( 
    IVdsSwProvider * This,
    /* [out] */ IVdsPack **ppPack);


void __RPC_STUB IVdsSwProvider_CreatePack_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsSwProvider_INTERFACE_DEFINED__ */


#ifndef __IVdsPack_INTERFACE_DEFINED__
#define __IVdsPack_INTERFACE_DEFINED__

/* interface IVdsPack */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsPack;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("3b69d7f5-9d94-4648-91ca-79939ba263bf")
    IVdsPack : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_PACK_PROP *pPackProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProvider( 
            /* [out] */ IVdsProvider **ppProvider) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryVolumes( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryDisks( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE CreateVolume( 
            /* [in] */ VDS_VOLUME_TYPE type,
            /* [size_is][in] */ VDS_INPUT_DISK *pInputDiskArray,
            /* [in] */ LONG lNumberOfDisks,
            /* [in] */ ULONG ulStripeSize,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AddDisk( 
            /* [in] */ VDS_OBJECT_ID DiskId,
            /* [in] */ VDS_PARTITION_STYLE PartitionStyle,
            /* [in] */ BOOL bAsHotSpare) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE MigrateDisks( 
            /* [size_is][in] */ VDS_OBJECT_ID *pDiskArray,
            /* [in] */ LONG lNumberOfDisks,
            /* [in] */ VDS_OBJECT_ID TargetPack,
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bQueryOnly,
            /* [size_is][out] */ HRESULT *pResults,
            /* [out] */ BOOL *pbRebootNeeded) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ReplaceDisk( 
            /* [in] */ VDS_OBJECT_ID OldDiskId,
            /* [in] */ VDS_OBJECT_ID NewDiskId,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE RemoveMissingDisk( 
            /* [in] */ VDS_OBJECT_ID DiskId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Recover( 
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsPackVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsPack * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsPack * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsPack * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsPack * This,
            /* [out] */ VDS_PACK_PROP *pPackProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProvider )( 
            IVdsPack * This,
            /* [out] */ IVdsProvider **ppProvider);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryVolumes )( 
            IVdsPack * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryDisks )( 
            IVdsPack * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *CreateVolume )( 
            IVdsPack * This,
            /* [in] */ VDS_VOLUME_TYPE type,
            /* [size_is][in] */ VDS_INPUT_DISK *pInputDiskArray,
            /* [in] */ LONG lNumberOfDisks,
            /* [in] */ ULONG ulStripeSize,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AddDisk )( 
            IVdsPack * This,
            /* [in] */ VDS_OBJECT_ID DiskId,
            /* [in] */ VDS_PARTITION_STYLE PartitionStyle,
            /* [in] */ BOOL bAsHotSpare);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *MigrateDisks )( 
            IVdsPack * This,
            /* [size_is][in] */ VDS_OBJECT_ID *pDiskArray,
            /* [in] */ LONG lNumberOfDisks,
            /* [in] */ VDS_OBJECT_ID TargetPack,
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bQueryOnly,
            /* [size_is][out] */ HRESULT *pResults,
            /* [out] */ BOOL *pbRebootNeeded);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ReplaceDisk )( 
            IVdsPack * This,
            /* [in] */ VDS_OBJECT_ID OldDiskId,
            /* [in] */ VDS_OBJECT_ID NewDiskId,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *RemoveMissingDisk )( 
            IVdsPack * This,
            /* [in] */ VDS_OBJECT_ID DiskId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Recover )( 
            IVdsPack * This,
            /* [out] */ IVdsAsync **ppAsync);
        
        END_INTERFACE
    } IVdsPackVtbl;

    interface IVdsPack
    {
        CONST_VTBL struct IVdsPackVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsPack_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsPack_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsPack_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsPack_GetProperties(This,pPackProp)	\
    (This)->lpVtbl -> GetProperties(This,pPackProp)

#define IVdsPack_GetProvider(This,ppProvider)	\
    (This)->lpVtbl -> GetProvider(This,ppProvider)

#define IVdsPack_QueryVolumes(This,ppEnum)	\
    (This)->lpVtbl -> QueryVolumes(This,ppEnum)

#define IVdsPack_QueryDisks(This,ppEnum)	\
    (This)->lpVtbl -> QueryDisks(This,ppEnum)

#define IVdsPack_CreateVolume(This,type,pInputDiskArray,lNumberOfDisks,ulStripeSize,ppAsync)	\
    (This)->lpVtbl -> CreateVolume(This,type,pInputDiskArray,lNumberOfDisks,ulStripeSize,ppAsync)

#define IVdsPack_AddDisk(This,DiskId,PartitionStyle,bAsHotSpare)	\
    (This)->lpVtbl -> AddDisk(This,DiskId,PartitionStyle,bAsHotSpare)

#define IVdsPack_MigrateDisks(This,pDiskArray,lNumberOfDisks,TargetPack,bForce,bQueryOnly,pResults,pbRebootNeeded)	\
    (This)->lpVtbl -> MigrateDisks(This,pDiskArray,lNumberOfDisks,TargetPack,bForce,bQueryOnly,pResults,pbRebootNeeded)

#define IVdsPack_ReplaceDisk(This,OldDiskId,NewDiskId,ppAsync)	\
    (This)->lpVtbl -> ReplaceDisk(This,OldDiskId,NewDiskId,ppAsync)

#define IVdsPack_RemoveMissingDisk(This,DiskId)	\
    (This)->lpVtbl -> RemoveMissingDisk(This,DiskId)

#define IVdsPack_Recover(This,ppAsync)	\
    (This)->lpVtbl -> Recover(This,ppAsync)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsPack_GetProperties_Proxy( 
    IVdsPack * This,
    /* [out] */ VDS_PACK_PROP *pPackProp);


void __RPC_STUB IVdsPack_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsPack_GetProvider_Proxy( 
    IVdsPack * This,
    /* [out] */ IVdsProvider **ppProvider);


void __RPC_STUB IVdsPack_GetProvider_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsPack_QueryVolumes_Proxy( 
    IVdsPack * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsPack_QueryVolumes_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsPack_QueryDisks_Proxy( 
    IVdsPack * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsPack_QueryDisks_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsPack_CreateVolume_Proxy( 
    IVdsPack * This,
    /* [in] */ VDS_VOLUME_TYPE type,
    /* [size_is][in] */ VDS_INPUT_DISK *pInputDiskArray,
    /* [in] */ LONG lNumberOfDisks,
    /* [in] */ ULONG ulStripeSize,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsPack_CreateVolume_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsPack_AddDisk_Proxy( 
    IVdsPack * This,
    /* [in] */ VDS_OBJECT_ID DiskId,
    /* [in] */ VDS_PARTITION_STYLE PartitionStyle,
    /* [in] */ BOOL bAsHotSpare);


void __RPC_STUB IVdsPack_AddDisk_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsPack_MigrateDisks_Proxy( 
    IVdsPack * This,
    /* [size_is][in] */ VDS_OBJECT_ID *pDiskArray,
    /* [in] */ LONG lNumberOfDisks,
    /* [in] */ VDS_OBJECT_ID TargetPack,
    /* [in] */ BOOL bForce,
    /* [in] */ BOOL bQueryOnly,
    /* [size_is][out] */ HRESULT *pResults,
    /* [out] */ BOOL *pbRebootNeeded);


void __RPC_STUB IVdsPack_MigrateDisks_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsPack_ReplaceDisk_Proxy( 
    IVdsPack * This,
    /* [in] */ VDS_OBJECT_ID OldDiskId,
    /* [in] */ VDS_OBJECT_ID NewDiskId,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsPack_ReplaceDisk_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsPack_RemoveMissingDisk_Proxy( 
    IVdsPack * This,
    /* [in] */ VDS_OBJECT_ID DiskId);


void __RPC_STUB IVdsPack_RemoveMissingDisk_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsPack_Recover_Proxy( 
    IVdsPack * This,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsPack_Recover_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsPack_INTERFACE_DEFINED__ */


#ifndef __IVdsDisk_INTERFACE_DEFINED__
#define __IVdsDisk_INTERFACE_DEFINED__

/* interface IVdsDisk */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsDisk;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("07e5c822-f00c-47a1-8fce-b244da56fd06")
    IVdsDisk : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_DISK_PROP *pDiskProperties) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetPack( 
            /* [out] */ IVdsPack **ppPack) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetIdentificationData( 
            /* [out] */ VDS_LUN_INFORMATION *pLunInfo) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryExtents( 
            /* [size_is][size_is][out] */ VDS_DISK_EXTENT **ppExtentArray,
            /* [out] */ LONG *plNumberOfExtents) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ConvertStyle( 
            /* [in] */ VDS_PARTITION_STYLE NewStyle) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetFlags( 
            /* [in] */ ULONG ulFlags) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ClearFlags( 
            /* [in] */ ULONG ulFlags) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsDiskVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsDisk * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsDisk * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsDisk * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsDisk * This,
            /* [out] */ VDS_DISK_PROP *pDiskProperties);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetPack )( 
            IVdsDisk * This,
            /* [out] */ IVdsPack **ppPack);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetIdentificationData )( 
            IVdsDisk * This,
            /* [out] */ VDS_LUN_INFORMATION *pLunInfo);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryExtents )( 
            IVdsDisk * This,
            /* [size_is][size_is][out] */ VDS_DISK_EXTENT **ppExtentArray,
            /* [out] */ LONG *plNumberOfExtents);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ConvertStyle )( 
            IVdsDisk * This,
            /* [in] */ VDS_PARTITION_STYLE NewStyle);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetFlags )( 
            IVdsDisk * This,
            /* [in] */ ULONG ulFlags);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ClearFlags )( 
            IVdsDisk * This,
            /* [in] */ ULONG ulFlags);
        
        END_INTERFACE
    } IVdsDiskVtbl;

    interface IVdsDisk
    {
        CONST_VTBL struct IVdsDiskVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsDisk_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsDisk_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsDisk_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsDisk_GetProperties(This,pDiskProperties)	\
    (This)->lpVtbl -> GetProperties(This,pDiskProperties)

#define IVdsDisk_GetPack(This,ppPack)	\
    (This)->lpVtbl -> GetPack(This,ppPack)

#define IVdsDisk_GetIdentificationData(This,pLunInfo)	\
    (This)->lpVtbl -> GetIdentificationData(This,pLunInfo)

#define IVdsDisk_QueryExtents(This,ppExtentArray,plNumberOfExtents)	\
    (This)->lpVtbl -> QueryExtents(This,ppExtentArray,plNumberOfExtents)

#define IVdsDisk_ConvertStyle(This,NewStyle)	\
    (This)->lpVtbl -> ConvertStyle(This,NewStyle)

#define IVdsDisk_SetFlags(This,ulFlags)	\
    (This)->lpVtbl -> SetFlags(This,ulFlags)

#define IVdsDisk_ClearFlags(This,ulFlags)	\
    (This)->lpVtbl -> ClearFlags(This,ulFlags)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDisk_GetProperties_Proxy( 
    IVdsDisk * This,
    /* [out] */ VDS_DISK_PROP *pDiskProperties);


void __RPC_STUB IVdsDisk_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDisk_GetPack_Proxy( 
    IVdsDisk * This,
    /* [out] */ IVdsPack **ppPack);


void __RPC_STUB IVdsDisk_GetPack_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDisk_GetIdentificationData_Proxy( 
    IVdsDisk * This,
    /* [out] */ VDS_LUN_INFORMATION *pLunInfo);


void __RPC_STUB IVdsDisk_GetIdentificationData_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDisk_QueryExtents_Proxy( 
    IVdsDisk * This,
    /* [size_is][size_is][out] */ VDS_DISK_EXTENT **ppExtentArray,
    /* [out] */ LONG *plNumberOfExtents);


void __RPC_STUB IVdsDisk_QueryExtents_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDisk_ConvertStyle_Proxy( 
    IVdsDisk * This,
    /* [in] */ VDS_PARTITION_STYLE NewStyle);


void __RPC_STUB IVdsDisk_ConvertStyle_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDisk_SetFlags_Proxy( 
    IVdsDisk * This,
    /* [in] */ ULONG ulFlags);


void __RPC_STUB IVdsDisk_SetFlags_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDisk_ClearFlags_Proxy( 
    IVdsDisk * This,
    /* [in] */ ULONG ulFlags);


void __RPC_STUB IVdsDisk_ClearFlags_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsDisk_INTERFACE_DEFINED__ */


#ifndef __IVdsAdvancedDisk_INTERFACE_DEFINED__
#define __IVdsAdvancedDisk_INTERFACE_DEFINED__

/* interface IVdsAdvancedDisk */
/* [unique][uuid][object] */ 

typedef struct _CREATE_PARTITION_PARAMETERS
    {
    VDS_PARTITION_STYLE style;
    /* [switch_is] */ /* [switch_type] */ union 
        {
        /* [case()] */ struct 
            {
            BYTE partitionType;
            BOOLEAN bootIndicator;
            } 	MbrPartInfo;
        /* [case()] */ struct 
            {
            GUID partitionType;
            GUID partitionId;
            ULONGLONG attributes;
            WCHAR name[ 36 ];
            } 	GptPartInfo;
        /* [default] */  /* Empty union arm */ 
        } 	;
    } 	CREATE_PARTITION_PARAMETERS;

typedef struct _CHANGE_ATTRIBUTES_PARAMETERS
    {
    VDS_PARTITION_STYLE style;
    /* [switch_is] */ /* [switch_type] */ union 
        {
        /* [case()] */ struct 
            {
            BOOLEAN bootIndicator;
            } 	MbrPartInfo;
        /* [case()] */ struct 
            {
            ULONGLONG attributes;
            } 	GptPartInfo;
        /* [default] */  /* Empty union arm */ 
        } 	;
    } 	CHANGE_ATTRIBUTES_PARAMETERS;


EXTERN_C const IID IID_IVdsAdvancedDisk;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("6e6f6b40-977c-4069-bddd-ac710059f8c0")
    IVdsAdvancedDisk : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetPartitionProperties( 
            /* [in] */ ULONGLONG ullOffset,
            /* [out] */ VDS_PARTITION_PROP *pPartitionProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryPartitions( 
            /* [size_is][size_is][out] */ VDS_PARTITION_PROP **ppPartitionPropArray,
            /* [out] */ LONG *plNumberOfPartitions) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE CreatePartition( 
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ ULONGLONG ullSize,
            /* [in] */ CREATE_PARTITION_PARAMETERS *para,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE DeletePartition( 
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bForceProtected) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ChangeAttributes( 
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ CHANGE_ATTRIBUTES_PARAMETERS *para) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AssignDriveLetter( 
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ WCHAR wcLetter) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE DeleteDriveLetter( 
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ WCHAR wcLetter) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetDriveLetter( 
            /* [in] */ ULONGLONG ullOffset,
            /* [out] */ WCHAR *pwcLetter) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE FormatPartition( 
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ VDS_FILE_SYSTEM_TYPE type,
            /* [string][in] */ LPWSTR pwszLabel,
            /* [in] */ DWORD dwUnitAllocationSize,
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bQuickFormat,
            /* [in] */ BOOL bEnableCompression,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Clean( 
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bForceOEM,
            /* [in] */ BOOL bFullClean,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsAdvancedDiskVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsAdvancedDisk * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsAdvancedDisk * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsAdvancedDisk * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetPartitionProperties )( 
            IVdsAdvancedDisk * This,
            /* [in] */ ULONGLONG ullOffset,
            /* [out] */ VDS_PARTITION_PROP *pPartitionProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryPartitions )( 
            IVdsAdvancedDisk * This,
            /* [size_is][size_is][out] */ VDS_PARTITION_PROP **ppPartitionPropArray,
            /* [out] */ LONG *plNumberOfPartitions);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *CreatePartition )( 
            IVdsAdvancedDisk * This,
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ ULONGLONG ullSize,
            /* [in] */ CREATE_PARTITION_PARAMETERS *para,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *DeletePartition )( 
            IVdsAdvancedDisk * This,
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bForceProtected);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ChangeAttributes )( 
            IVdsAdvancedDisk * This,
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ CHANGE_ATTRIBUTES_PARAMETERS *para);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AssignDriveLetter )( 
            IVdsAdvancedDisk * This,
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ WCHAR wcLetter);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *DeleteDriveLetter )( 
            IVdsAdvancedDisk * This,
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ WCHAR wcLetter);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetDriveLetter )( 
            IVdsAdvancedDisk * This,
            /* [in] */ ULONGLONG ullOffset,
            /* [out] */ WCHAR *pwcLetter);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *FormatPartition )( 
            IVdsAdvancedDisk * This,
            /* [in] */ ULONGLONG ullOffset,
            /* [in] */ VDS_FILE_SYSTEM_TYPE type,
            /* [string][in] */ LPWSTR pwszLabel,
            /* [in] */ DWORD dwUnitAllocationSize,
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bQuickFormat,
            /* [in] */ BOOL bEnableCompression,
            /* [out] */ IVdsAsync **ppAsync);
        
        HRESULT ( STDMETHODCALLTYPE *Clean )( 
            IVdsAdvancedDisk * This,
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bForceOEM,
            /* [in] */ BOOL bFullClean,
            /* [out] */ IVdsAsync **ppAsync);
        
        END_INTERFACE
    } IVdsAdvancedDiskVtbl;

    interface IVdsAdvancedDisk
    {
        CONST_VTBL struct IVdsAdvancedDiskVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsAdvancedDisk_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsAdvancedDisk_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsAdvancedDisk_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsAdvancedDisk_GetPartitionProperties(This,ullOffset,pPartitionProp)	\
    (This)->lpVtbl -> GetPartitionProperties(This,ullOffset,pPartitionProp)

#define IVdsAdvancedDisk_QueryPartitions(This,ppPartitionPropArray,plNumberOfPartitions)	\
    (This)->lpVtbl -> QueryPartitions(This,ppPartitionPropArray,plNumberOfPartitions)

#define IVdsAdvancedDisk_CreatePartition(This,ullOffset,ullSize,para,ppAsync)	\
    (This)->lpVtbl -> CreatePartition(This,ullOffset,ullSize,para,ppAsync)

#define IVdsAdvancedDisk_DeletePartition(This,ullOffset,bForce,bForceProtected)	\
    (This)->lpVtbl -> DeletePartition(This,ullOffset,bForce,bForceProtected)

#define IVdsAdvancedDisk_ChangeAttributes(This,ullOffset,para)	\
    (This)->lpVtbl -> ChangeAttributes(This,ullOffset,para)

#define IVdsAdvancedDisk_AssignDriveLetter(This,ullOffset,wcLetter)	\
    (This)->lpVtbl -> AssignDriveLetter(This,ullOffset,wcLetter)

#define IVdsAdvancedDisk_DeleteDriveLetter(This,ullOffset,wcLetter)	\
    (This)->lpVtbl -> DeleteDriveLetter(This,ullOffset,wcLetter)

#define IVdsAdvancedDisk_GetDriveLetter(This,ullOffset,pwcLetter)	\
    (This)->lpVtbl -> GetDriveLetter(This,ullOffset,pwcLetter)

#define IVdsAdvancedDisk_FormatPartition(This,ullOffset,type,pwszLabel,dwUnitAllocationSize,bForce,bQuickFormat,bEnableCompression,ppAsync)	\
    (This)->lpVtbl -> FormatPartition(This,ullOffset,type,pwszLabel,dwUnitAllocationSize,bForce,bQuickFormat,bEnableCompression,ppAsync)

#define IVdsAdvancedDisk_Clean(This,bForce,bForceOEM,bFullClean,ppAsync)	\
    (This)->lpVtbl -> Clean(This,bForce,bForceOEM,bFullClean,ppAsync)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAdvancedDisk_GetPartitionProperties_Proxy( 
    IVdsAdvancedDisk * This,
    /* [in] */ ULONGLONG ullOffset,
    /* [out] */ VDS_PARTITION_PROP *pPartitionProp);


void __RPC_STUB IVdsAdvancedDisk_GetPartitionProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAdvancedDisk_QueryPartitions_Proxy( 
    IVdsAdvancedDisk * This,
    /* [size_is][size_is][out] */ VDS_PARTITION_PROP **ppPartitionPropArray,
    /* [out] */ LONG *plNumberOfPartitions);


void __RPC_STUB IVdsAdvancedDisk_QueryPartitions_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAdvancedDisk_CreatePartition_Proxy( 
    IVdsAdvancedDisk * This,
    /* [in] */ ULONGLONG ullOffset,
    /* [in] */ ULONGLONG ullSize,
    /* [in] */ CREATE_PARTITION_PARAMETERS *para,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsAdvancedDisk_CreatePartition_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAdvancedDisk_DeletePartition_Proxy( 
    IVdsAdvancedDisk * This,
    /* [in] */ ULONGLONG ullOffset,
    /* [in] */ BOOL bForce,
    /* [in] */ BOOL bForceProtected);


void __RPC_STUB IVdsAdvancedDisk_DeletePartition_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAdvancedDisk_ChangeAttributes_Proxy( 
    IVdsAdvancedDisk * This,
    /* [in] */ ULONGLONG ullOffset,
    /* [in] */ CHANGE_ATTRIBUTES_PARAMETERS *para);


void __RPC_STUB IVdsAdvancedDisk_ChangeAttributes_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAdvancedDisk_AssignDriveLetter_Proxy( 
    IVdsAdvancedDisk * This,
    /* [in] */ ULONGLONG ullOffset,
    /* [in] */ WCHAR wcLetter);


void __RPC_STUB IVdsAdvancedDisk_AssignDriveLetter_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAdvancedDisk_DeleteDriveLetter_Proxy( 
    IVdsAdvancedDisk * This,
    /* [in] */ ULONGLONG ullOffset,
    /* [in] */ WCHAR wcLetter);


void __RPC_STUB IVdsAdvancedDisk_DeleteDriveLetter_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAdvancedDisk_GetDriveLetter_Proxy( 
    IVdsAdvancedDisk * This,
    /* [in] */ ULONGLONG ullOffset,
    /* [out] */ WCHAR *pwcLetter);


void __RPC_STUB IVdsAdvancedDisk_GetDriveLetter_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsAdvancedDisk_FormatPartition_Proxy( 
    IVdsAdvancedDisk * This,
    /* [in] */ ULONGLONG ullOffset,
    /* [in] */ VDS_FILE_SYSTEM_TYPE type,
    /* [string][in] */ LPWSTR pwszLabel,
    /* [in] */ DWORD dwUnitAllocationSize,
    /* [in] */ BOOL bForce,
    /* [in] */ BOOL bQuickFormat,
    /* [in] */ BOOL bEnableCompression,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsAdvancedDisk_FormatPartition_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVdsAdvancedDisk_Clean_Proxy( 
    IVdsAdvancedDisk * This,
    /* [in] */ BOOL bForce,
    /* [in] */ BOOL bForceOEM,
    /* [in] */ BOOL bFullClean,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsAdvancedDisk_Clean_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsAdvancedDisk_INTERFACE_DEFINED__ */


#ifndef __IVdsRemovable_INTERFACE_DEFINED__
#define __IVdsRemovable_INTERFACE_DEFINED__

/* interface IVdsRemovable */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsRemovable;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("0316560b-5db4-4ed9-bbb5-213436ddc0d9")
    IVdsRemovable : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryMedia( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Eject( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsRemovableVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsRemovable * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsRemovable * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsRemovable * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryMedia )( 
            IVdsRemovable * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Eject )( 
            IVdsRemovable * This);
        
        END_INTERFACE
    } IVdsRemovableVtbl;

    interface IVdsRemovable
    {
        CONST_VTBL struct IVdsRemovableVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsRemovable_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsRemovable_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsRemovable_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsRemovable_QueryMedia(This)	\
    (This)->lpVtbl -> QueryMedia(This)

#define IVdsRemovable_Eject(This)	\
    (This)->lpVtbl -> Eject(This)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsRemovable_QueryMedia_Proxy( 
    IVdsRemovable * This);


void __RPC_STUB IVdsRemovable_QueryMedia_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsRemovable_Eject_Proxy( 
    IVdsRemovable * This);


void __RPC_STUB IVdsRemovable_Eject_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsRemovable_INTERFACE_DEFINED__ */


#ifndef __IVdsVolume_INTERFACE_DEFINED__
#define __IVdsVolume_INTERFACE_DEFINED__

/* interface IVdsVolume */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsVolume;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("88306bb2-e71f-478c-86a2-79da200a0f11")
    IVdsVolume : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_VOLUME_PROP *pVolumeProperties) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetPack( 
            /* [out] */ IVdsPack **ppPack) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryPlexes( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Extend( 
            /* [size_is][unique][in] */ VDS_INPUT_DISK *pInputDiskArray,
            /* [in] */ LONG lNumberOfDisks,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Shrink( 
            /* [in] */ ULONGLONG uNumberOfBytesToRemove,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AddPlex( 
            /* [in] */ VDS_OBJECT_ID VolumeId,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE BreakPlex( 
            /* [in] */ VDS_OBJECT_ID plexId,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE RemovePlex( 
            /* [in] */ VDS_OBJECT_ID plexId,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Delete( 
            /* [in] */ BOOL bForce) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetFlags( 
            /* [in] */ ULONG ulFlags,
            /* [in] */ BOOL bRevertOnClose) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ClearFlags( 
            /* [in] */ ULONG ulFlags) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsVolumeVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsVolume * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsVolume * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsVolume * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsVolume * This,
            /* [out] */ VDS_VOLUME_PROP *pVolumeProperties);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetPack )( 
            IVdsVolume * This,
            /* [out] */ IVdsPack **ppPack);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryPlexes )( 
            IVdsVolume * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Extend )( 
            IVdsVolume * This,
            /* [size_is][unique][in] */ VDS_INPUT_DISK *pInputDiskArray,
            /* [in] */ LONG lNumberOfDisks,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Shrink )( 
            IVdsVolume * This,
            /* [in] */ ULONGLONG uNumberOfBytesToRemove,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AddPlex )( 
            IVdsVolume * This,
            /* [in] */ VDS_OBJECT_ID VolumeId,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *BreakPlex )( 
            IVdsVolume * This,
            /* [in] */ VDS_OBJECT_ID plexId,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *RemovePlex )( 
            IVdsVolume * This,
            /* [in] */ VDS_OBJECT_ID plexId,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Delete )( 
            IVdsVolume * This,
            /* [in] */ BOOL bForce);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetFlags )( 
            IVdsVolume * This,
            /* [in] */ ULONG ulFlags,
            /* [in] */ BOOL bRevertOnClose);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ClearFlags )( 
            IVdsVolume * This,
            /* [in] */ ULONG ulFlags);
        
        END_INTERFACE
    } IVdsVolumeVtbl;

    interface IVdsVolume
    {
        CONST_VTBL struct IVdsVolumeVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsVolume_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsVolume_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsVolume_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsVolume_GetProperties(This,pVolumeProperties)	\
    (This)->lpVtbl -> GetProperties(This,pVolumeProperties)

#define IVdsVolume_GetPack(This,ppPack)	\
    (This)->lpVtbl -> GetPack(This,ppPack)

#define IVdsVolume_QueryPlexes(This,ppEnum)	\
    (This)->lpVtbl -> QueryPlexes(This,ppEnum)

#define IVdsVolume_Extend(This,pInputDiskArray,lNumberOfDisks,ppAsync)	\
    (This)->lpVtbl -> Extend(This,pInputDiskArray,lNumberOfDisks,ppAsync)

#define IVdsVolume_Shrink(This,uNumberOfBytesToRemove,ppAsync)	\
    (This)->lpVtbl -> Shrink(This,uNumberOfBytesToRemove,ppAsync)

#define IVdsVolume_AddPlex(This,VolumeId,ppAsync)	\
    (This)->lpVtbl -> AddPlex(This,VolumeId,ppAsync)

#define IVdsVolume_BreakPlex(This,plexId,ppAsync)	\
    (This)->lpVtbl -> BreakPlex(This,plexId,ppAsync)

#define IVdsVolume_RemovePlex(This,plexId,ppAsync)	\
    (This)->lpVtbl -> RemovePlex(This,plexId,ppAsync)

#define IVdsVolume_Delete(This,bForce)	\
    (This)->lpVtbl -> Delete(This,bForce)

#define IVdsVolume_SetFlags(This,ulFlags,bRevertOnClose)	\
    (This)->lpVtbl -> SetFlags(This,ulFlags,bRevertOnClose)

#define IVdsVolume_ClearFlags(This,ulFlags)	\
    (This)->lpVtbl -> ClearFlags(This,ulFlags)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_GetProperties_Proxy( 
    IVdsVolume * This,
    /* [out] */ VDS_VOLUME_PROP *pVolumeProperties);


void __RPC_STUB IVdsVolume_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_GetPack_Proxy( 
    IVdsVolume * This,
    /* [out] */ IVdsPack **ppPack);


void __RPC_STUB IVdsVolume_GetPack_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_QueryPlexes_Proxy( 
    IVdsVolume * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsVolume_QueryPlexes_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_Extend_Proxy( 
    IVdsVolume * This,
    /* [size_is][unique][in] */ VDS_INPUT_DISK *pInputDiskArray,
    /* [in] */ LONG lNumberOfDisks,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsVolume_Extend_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_Shrink_Proxy( 
    IVdsVolume * This,
    /* [in] */ ULONGLONG uNumberOfBytesToRemove,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsVolume_Shrink_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_AddPlex_Proxy( 
    IVdsVolume * This,
    /* [in] */ VDS_OBJECT_ID VolumeId,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsVolume_AddPlex_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_BreakPlex_Proxy( 
    IVdsVolume * This,
    /* [in] */ VDS_OBJECT_ID plexId,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsVolume_BreakPlex_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_RemovePlex_Proxy( 
    IVdsVolume * This,
    /* [in] */ VDS_OBJECT_ID plexId,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsVolume_RemovePlex_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_Delete_Proxy( 
    IVdsVolume * This,
    /* [in] */ BOOL bForce);


void __RPC_STUB IVdsVolume_Delete_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_SetFlags_Proxy( 
    IVdsVolume * This,
    /* [in] */ ULONG ulFlags,
    /* [in] */ BOOL bRevertOnClose);


void __RPC_STUB IVdsVolume_SetFlags_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolume_ClearFlags_Proxy( 
    IVdsVolume * This,
    /* [in] */ ULONG ulFlags);


void __RPC_STUB IVdsVolume_ClearFlags_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsVolume_INTERFACE_DEFINED__ */


#ifndef __IVdsVolumePlex_INTERFACE_DEFINED__
#define __IVdsVolumePlex_INTERFACE_DEFINED__

/* interface IVdsVolumePlex */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsVolumePlex;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("4daa0135-e1d1-40f1-aaa5-3cc1e53221c3")
    IVdsVolumePlex : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_VOLUME_PLEX_PROP *pPlexProperties) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetVolume( 
            /* [out] */ IVdsVolume **ppVolume) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryExtents( 
            /* [size_is][size_is][out] */ VDS_DISK_EXTENT **ppExtentArray,
            /* [out] */ LONG *plNumberOfExtents) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Repair( 
            /* [size_is][in] */ VDS_INPUT_DISK *pInputDiskArray,
            /* [in] */ LONG lNumberOfDisks,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsVolumePlexVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsVolumePlex * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsVolumePlex * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsVolumePlex * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsVolumePlex * This,
            /* [out] */ VDS_VOLUME_PLEX_PROP *pPlexProperties);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetVolume )( 
            IVdsVolumePlex * This,
            /* [out] */ IVdsVolume **ppVolume);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryExtents )( 
            IVdsVolumePlex * This,
            /* [size_is][size_is][out] */ VDS_DISK_EXTENT **ppExtentArray,
            /* [out] */ LONG *plNumberOfExtents);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Repair )( 
            IVdsVolumePlex * This,
            /* [size_is][in] */ VDS_INPUT_DISK *pInputDiskArray,
            /* [in] */ LONG lNumberOfDisks,
            /* [out] */ IVdsAsync **ppAsync);
        
        END_INTERFACE
    } IVdsVolumePlexVtbl;

    interface IVdsVolumePlex
    {
        CONST_VTBL struct IVdsVolumePlexVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsVolumePlex_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsVolumePlex_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsVolumePlex_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsVolumePlex_GetProperties(This,pPlexProperties)	\
    (This)->lpVtbl -> GetProperties(This,pPlexProperties)

#define IVdsVolumePlex_GetVolume(This,ppVolume)	\
    (This)->lpVtbl -> GetVolume(This,ppVolume)

#define IVdsVolumePlex_QueryExtents(This,ppExtentArray,plNumberOfExtents)	\
    (This)->lpVtbl -> QueryExtents(This,ppExtentArray,plNumberOfExtents)

#define IVdsVolumePlex_Repair(This,pInputDiskArray,lNumberOfDisks,ppAsync)	\
    (This)->lpVtbl -> Repair(This,pInputDiskArray,lNumberOfDisks,ppAsync)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumePlex_GetProperties_Proxy( 
    IVdsVolumePlex * This,
    /* [out] */ VDS_VOLUME_PLEX_PROP *pPlexProperties);


void __RPC_STUB IVdsVolumePlex_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumePlex_GetVolume_Proxy( 
    IVdsVolumePlex * This,
    /* [out] */ IVdsVolume **ppVolume);


void __RPC_STUB IVdsVolumePlex_GetVolume_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumePlex_QueryExtents_Proxy( 
    IVdsVolumePlex * This,
    /* [size_is][size_is][out] */ VDS_DISK_EXTENT **ppExtentArray,
    /* [out] */ LONG *plNumberOfExtents);


void __RPC_STUB IVdsVolumePlex_QueryExtents_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumePlex_Repair_Proxy( 
    IVdsVolumePlex * This,
    /* [size_is][in] */ VDS_INPUT_DISK *pInputDiskArray,
    /* [in] */ LONG lNumberOfDisks,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsVolumePlex_Repair_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsVolumePlex_INTERFACE_DEFINED__ */


/* interface __MIDL_itf_vds_0269 */
/* [local] */ 







typedef 
enum _VDS_SUB_SYSTEM_STATUS
    {	VDS_SSS_UNKNOWN	= 0,
	VDS_SSS_ONLINE	= 1,
	VDS_SSS_NOT_READY	= 2,
	VDS_SSS_OFFLINE	= 4,
	VDS_SSS_FAILED	= 5
    } 	VDS_SUB_SYSTEM_STATUS;

typedef 
enum _VDS_SUB_SYSTEM_FLAG
    {	VDS_SF_LUN_MASKING_CAPABLE	= 0x1,
	VDS_SF_LUN_PLEXING_CAPABLE	= 0x2,
	VDS_SF_LUN_REMAPPING_CAPABLE	= 0x4,
	VDS_SF_DRIVE_EXTENT_CAPABLE	= 0x8,
	VDS_SF_HARDWARE_CHECKSUM_CAPABLE	= 0x10
    } 	VDS_SUB_SYSTEM_FLAG;

typedef 
enum _VDS_CONTROLLER_STATUS
    {	VDS_CS_UNKNOWN	= 0,
	VDS_CS_ONLINE	= 1,
	VDS_CS_NOT_READY	= 2,
	VDS_CS_OFFLINE	= 4,
	VDS_CS_FAILED	= 5
    } 	VDS_CONTROLLER_STATUS;

typedef 
enum _VDS_HBA_STATUS
    {	VDS_HBS_UNKNOWN	= 0,
	VDS_HBS_ONLINE	= 1,
	VDS_HBS_NOT_READY	= 2,
	VDS_HBS_OFFLINE	= 4,
	VDS_HBS_FAILED	= 5
    } 	VDS_HBA_STATUS;

typedef 
enum _VDS_DRIVE_STATUS
    {	VDS_DRS_UNKNOWN	= 0,
	VDS_DRS_ONLINE	= 1,
	VDS_DRS_NOT_READY	= 2,
	VDS_DRS_OFFLINE	= 4,
	VDS_DRS_FAILED	= 5
    } 	VDS_DRIVE_STATUS;

typedef 
enum _VDS_DRIVE_FLAG
    {	VDS_DRF_HOTSPARE	= 0x1
    } 	VDS_DRIVE_FLAG;

typedef 
enum _VDS_LUN_TYPE
    {	VDS_LT_UNKNOWN	= 0,
	VDS_LT_DEFAULT	= 1,
	VDS_LT_FAULT_TOLERANT	= 2,
	VDS_LT_NON_FAULT_TOLERANT	= 3,
	VDS_LT_SIMPLE	= 10,
	VDS_LT_SPAN	= 11,
	VDS_LT_STRIPE	= 12,
	VDS_LT_MIRROR	= 13,
	VDS_LT_PARITY	= 14
    } 	VDS_LUN_TYPE;

typedef 
enum _VDS_LUN_STATUS
    {	VDS_LS_UNKNOWN	= 0,
	VDS_LS_ONLINE	= 1,
	VDS_LS_NOT_READY	= 2,
	VDS_LS_OFFLINE	= 4,
	VDS_LS_FAILED	= 5
    } 	VDS_LUN_STATUS;

typedef 
enum _VDS_LUN_FLAG
    {	VDS_LF_LBN_REMAP_ENABLED	= 0x1,
	VDS_LF_READ_BACK_VERIFY_ENABLED	= 0x2,
	VDS_LF_WRITE_THROUGH_CACHING_ENABLED	= 0x4,
	VDS_LF_HARDWARE_CHECKSUM_ENABLED	= 0x8
    } 	VDS_LUN_FLAG;

typedef 
enum _VDS_LUN_PLEX_TYPE
    {	VDS_LPT_UNKNOWN	= 0,
	VDS_LPT_SIMPLE	= VDS_LT_SIMPLE,
	VDS_LPT_SPAN	= VDS_LT_SPAN,
	VDS_LPT_STRIPE	= VDS_LT_STRIPE,
	VDS_LPT_PARITY	= VDS_LT_PARITY
    } 	VDS_LUN_PLEX_TYPE;

typedef 
enum _VDS_LUN_PLEX_STATUS
    {	VDS_LPS_UNKNOWN	= 0,
	VDS_LPS_ONLINE	= 1,
	VDS_LPS_NOT_READY	= 2,
	VDS_LPS_OFFLINE	= 4,
	VDS_LPS_FAILED	= 5
    } 	VDS_LUN_PLEX_STATUS;

typedef 
enum _VDS_LUN_PLEX_FLAG
    {	VDS_LPF_LBN_REMAP_ENABLED	= VDS_LF_LBN_REMAP_ENABLED
    } 	VDS_LUN_PLEX_FLAG;

typedef 
enum _VDS_MAINTENANCE_OPERATION
    {	BlinkLight	= 1,
	BeepAlarm	= 2,
	SpinDown	= 3,
	SpinUp	= 4,
	Ping	= 5
    } 	VDS_MAINTENANCE_OPERATION;

typedef 
enum _VDS_PORT_STATUS
    {	VDS_PRS_UNKNOWN	= 0,
	VDS_PRS_ONLINE	= 1,
	VDS_PRS_NOT_READY	= 2,
	VDS_PRS_OFFLINE	= 4,
	VDS_PRS_FAILED	= 5
    } 	VDS_PORT_STATUS;

typedef struct _VDS_HINTS
    {
    ULONGLONG ullHintMask;
    ULONGLONG ullExpectedMaximumSize;
    ULONG ulOptimalReadSize;
    ULONG ulOptimalReadAlignment;
    ULONG ulOptimalWriteSize;
    ULONG ulOptimalWriteAlignment;
    ULONG ulMaximumDriveCount;
    ULONG ulStripeSize;
    BOOL bFastCrashRecoveryRequired;
    BOOL bMostlyReads;
    BOOL bOptimizeForSequentialReads;
    BOOL bOptimizeForSequentialWrites;
    BOOL bRemapEnabled;
    BOOL bReadBackVerifyEnabled;
    BOOL bWriteThroughCachingEnabled;
    BOOL bHardwareChecksumEnabled;
    BOOL bIsYankable;
    SHORT sRebuildPriority;
    } 	VDS_HINTS;

typedef struct _VDS_HINTS *PVDS_HINTS;

#define	VDS_HINT_FASTCRASHRECOVERYREQUIRED	( 0x1L )

#define	VDS_HINT_MOSTLYREADS	( 0x2L )

#define	VDS_HINT_OPTIMIZEFORSEQUENTIALREADS	( 0x4L )

#define	VDS_HINT_OPTIMIZEFORSEQUENTIALWRITES	( 0x8L )

#define	VDS_HINT_READBACKVERIFYENABLED	( 0x10L )

#define	VDS_HINT_REMAPENABLED	( 0x20L )

#define	VDS_HINT_WRITETHROUGHCACHINGENABLED	( 0x40L )

#define	VDS_HINT_HARDWARECHECKSUMENABLED	( 0x80L )

#define	VDS_HINT_ISYANKABLE	( 0x100L )

typedef struct _VDS_SUB_SYSTEM_PROP
    {
    VDS_OBJECT_ID id;
    /* [string] */ LPWSTR pwszFriendlyName;
    /* [string] */ LPWSTR pwszIdentification;
    ULONG ulFlags;
    ULONG ulStripeSizeFlags;
    VDS_SUB_SYSTEM_STATUS status;
    VDS_HEALTH health;
    SHORT sNumberOfInternalBuses;
    SHORT sMaxNumberOfSlotsEachBus;
    SHORT sMaxNumberOfControllers;
    SHORT sRebuildPriority;
    } 	VDS_SUB_SYSTEM_PROP;

typedef struct _VDS_CONTROLLER_PROP
    {
    VDS_OBJECT_ID id;
    /* [string] */ LPWSTR pwszFriendlyName;
    /* [string] */ LPWSTR pwszIdentification;
    VDS_CONTROLLER_STATUS status;
    VDS_HEALTH health;
    SHORT sNumberOfPorts;
    } 	VDS_CONTROLLER_PROP;

typedef struct _VDS_HBA_PROP
    {
    VDS_OBJECT_ID id;
    /* [string] */ LPWSTR pwszFriendlyName;
    /* [string] */ LPWSTR pwszIdentification;
    VDS_HBA_STATUS status;
    VDS_HEALTH health;
    SHORT sNumberOfPorts;
    } 	VDS_HBA_PROP;

typedef struct _VDS_DRIVE_PROP
    {
    VDS_OBJECT_ID id;
    ULONGLONG ullSize;
    /* [string] */ LPWSTR pwszFriendlyName;
    /* [string] */ LPWSTR pwszIdentification;
    ULONG ulFlags;
    VDS_DRIVE_STATUS status;
    VDS_HEALTH health;
    SHORT sInternalBusNumber;
    SHORT sSlotNumber;
    } 	VDS_DRIVE_PROP;

typedef struct _VDS_DRIVE_EXTENT
    {
    VDS_OBJECT_ID id;
    VDS_OBJECT_ID LunId;
    ULONGLONG ullSize;
    BOOL bUsed;
    } 	VDS_DRIVE_EXTENT;

#define VDS_REBUILD_PRIORITY_MIN        0
#define VDS_REBUILD_PRIORITY_MAX        16
typedef struct _VDS_LUN_PROP
    {
    VDS_OBJECT_ID id;
    ULONGLONG ullSize;
    /* [string] */ LPWSTR pwszFriendlyName;
    /* [string] */ LPWSTR pwszIdentification;
    /* [string] */ LPWSTR pwszUnmaskingList;
    ULONG ulFlags;
    VDS_LUN_TYPE type;
    VDS_LUN_STATUS status;
    VDS_HEALTH health;
    VDS_TRANSITION_STATE TransitionState;
    SHORT sRebuildPriority;
    } 	VDS_LUN_PROP;

typedef struct _VDS_LUN_PROP *PVDS_LUN_PROP;

typedef struct _VDS_LUN_PLEX_PROP
    {
    VDS_OBJECT_ID id;
    ULONGLONG ullSize;
    VDS_LUN_PLEX_TYPE type;
    VDS_LUN_PLEX_STATUS status;
    VDS_HEALTH health;
    VDS_TRANSITION_STATE TransitionState;
    ULONG ulFlags;
    ULONG ulStripeSize;
    SHORT sRebuildPriority;
    } 	VDS_LUN_PLEX_PROP;

typedef struct _VDS_PORT_PROP
    {
    VDS_OBJECT_ID id;
    /* [string] */ LPWSTR pwszFriendlyName;
    /* [string] */ LPWSTR pwszIdentification;
    VDS_PORT_STATUS status;
    } 	VDS_PORT_PROP;



extern RPC_IF_HANDLE __MIDL_itf_vds_0269_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_vds_0269_v0_0_s_ifspec;

#ifndef __IVdsHwProvider_INTERFACE_DEFINED__
#define __IVdsHwProvider_INTERFACE_DEFINED__

/* interface IVdsHwProvider */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsHwProvider;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("d99bdaae-b13a-4178-9fdb-e27f16b4603e")
    IVdsHwProvider : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QuerySubSystems( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Reenumerate( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Refresh( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsHwProviderVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsHwProvider * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsHwProvider * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsHwProvider * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QuerySubSystems )( 
            IVdsHwProvider * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Reenumerate )( 
            IVdsHwProvider * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Refresh )( 
            IVdsHwProvider * This);
        
        END_INTERFACE
    } IVdsHwProviderVtbl;

    interface IVdsHwProvider
    {
        CONST_VTBL struct IVdsHwProviderVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsHwProvider_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsHwProvider_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsHwProvider_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsHwProvider_QuerySubSystems(This,ppEnum)	\
    (This)->lpVtbl -> QuerySubSystems(This,ppEnum)

#define IVdsHwProvider_Reenumerate(This)	\
    (This)->lpVtbl -> Reenumerate(This)

#define IVdsHwProvider_Refresh(This)	\
    (This)->lpVtbl -> Refresh(This)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsHwProvider_QuerySubSystems_Proxy( 
    IVdsHwProvider * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsHwProvider_QuerySubSystems_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsHwProvider_Reenumerate_Proxy( 
    IVdsHwProvider * This);


void __RPC_STUB IVdsHwProvider_Reenumerate_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsHwProvider_Refresh_Proxy( 
    IVdsHwProvider * This);


void __RPC_STUB IVdsHwProvider_Refresh_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsHwProvider_INTERFACE_DEFINED__ */


#ifndef __IVdsSubSystem_INTERFACE_DEFINED__
#define __IVdsSubSystem_INTERFACE_DEFINED__

/* interface IVdsSubSystem */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsSubSystem;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("6fcee2d3-6d90-4f91-80e2-a5c7caaca9d8")
    IVdsSubSystem : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_SUB_SYSTEM_PROP *pSubSystemProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProvider( 
            /* [out] */ IVdsProvider **ppProvider) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryControllers( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryLuns( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryDrives( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetDrive( 
            /* [in] */ SHORT sBusNumber,
            /* [in] */ SHORT sSlotNumber,
            /* [out] */ IVdsDrive **ppDrive) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Reenumerate( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetControllerStatus( 
            /* [size_is][in] */ VDS_OBJECT_ID *pOnlineControllerIdArray,
            /* [in] */ LONG lNumberOfOnlineControllers,
            /* [size_is][in] */ VDS_OBJECT_ID *pOfflineControllerIdArray,
            /* [in] */ LONG lNumberOfOfflineControllers) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE CreateLun( 
            /* [in] */ VDS_LUN_TYPE type,
            /* [in] */ ULONGLONG ullSizeInBytes,
            /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
            /* [in] */ LONG lNumberOfDrives,
            /* [string][in] */ LPWSTR pwszUnmaskingList,
            /* [unique][in] */ VDS_HINTS *pHints,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ReplaceDrive( 
            /* [in] */ VDS_OBJECT_ID DriveToBeReplaced,
            /* [in] */ VDS_OBJECT_ID ReplacementDrive) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetStatus( 
            /* [in] */ VDS_SUB_SYSTEM_STATUS status) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryMaxLunCreateSize( 
            /* [in] */ VDS_LUN_TYPE type,
            /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
            /* [in] */ LONG lNumberOfDrives,
            /* [unique][in] */ VDS_HINTS *pHints,
            /* [out] */ ULONGLONG *pullMaxLunSize) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsSubSystemVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsSubSystem * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsSubSystem * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsSubSystem * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsSubSystem * This,
            /* [out] */ VDS_SUB_SYSTEM_PROP *pSubSystemProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProvider )( 
            IVdsSubSystem * This,
            /* [out] */ IVdsProvider **ppProvider);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryControllers )( 
            IVdsSubSystem * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryLuns )( 
            IVdsSubSystem * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryDrives )( 
            IVdsSubSystem * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetDrive )( 
            IVdsSubSystem * This,
            /* [in] */ SHORT sBusNumber,
            /* [in] */ SHORT sSlotNumber,
            /* [out] */ IVdsDrive **ppDrive);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Reenumerate )( 
            IVdsSubSystem * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetControllerStatus )( 
            IVdsSubSystem * This,
            /* [size_is][in] */ VDS_OBJECT_ID *pOnlineControllerIdArray,
            /* [in] */ LONG lNumberOfOnlineControllers,
            /* [size_is][in] */ VDS_OBJECT_ID *pOfflineControllerIdArray,
            /* [in] */ LONG lNumberOfOfflineControllers);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *CreateLun )( 
            IVdsSubSystem * This,
            /* [in] */ VDS_LUN_TYPE type,
            /* [in] */ ULONGLONG ullSizeInBytes,
            /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
            /* [in] */ LONG lNumberOfDrives,
            /* [string][in] */ LPWSTR pwszUnmaskingList,
            /* [unique][in] */ VDS_HINTS *pHints,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ReplaceDrive )( 
            IVdsSubSystem * This,
            /* [in] */ VDS_OBJECT_ID DriveToBeReplaced,
            /* [in] */ VDS_OBJECT_ID ReplacementDrive);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetStatus )( 
            IVdsSubSystem * This,
            /* [in] */ VDS_SUB_SYSTEM_STATUS status);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryMaxLunCreateSize )( 
            IVdsSubSystem * This,
            /* [in] */ VDS_LUN_TYPE type,
            /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
            /* [in] */ LONG lNumberOfDrives,
            /* [unique][in] */ VDS_HINTS *pHints,
            /* [out] */ ULONGLONG *pullMaxLunSize);
        
        END_INTERFACE
    } IVdsSubSystemVtbl;

    interface IVdsSubSystem
    {
        CONST_VTBL struct IVdsSubSystemVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsSubSystem_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsSubSystem_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsSubSystem_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsSubSystem_GetProperties(This,pSubSystemProp)	\
    (This)->lpVtbl -> GetProperties(This,pSubSystemProp)

#define IVdsSubSystem_GetProvider(This,ppProvider)	\
    (This)->lpVtbl -> GetProvider(This,ppProvider)

#define IVdsSubSystem_QueryControllers(This,ppEnum)	\
    (This)->lpVtbl -> QueryControllers(This,ppEnum)

#define IVdsSubSystem_QueryLuns(This,ppEnum)	\
    (This)->lpVtbl -> QueryLuns(This,ppEnum)

#define IVdsSubSystem_QueryDrives(This,ppEnum)	\
    (This)->lpVtbl -> QueryDrives(This,ppEnum)

#define IVdsSubSystem_GetDrive(This,sBusNumber,sSlotNumber,ppDrive)	\
    (This)->lpVtbl -> GetDrive(This,sBusNumber,sSlotNumber,ppDrive)

#define IVdsSubSystem_Reenumerate(This)	\
    (This)->lpVtbl -> Reenumerate(This)

#define IVdsSubSystem_SetControllerStatus(This,pOnlineControllerIdArray,lNumberOfOnlineControllers,pOfflineControllerIdArray,lNumberOfOfflineControllers)	\
    (This)->lpVtbl -> SetControllerStatus(This,pOnlineControllerIdArray,lNumberOfOnlineControllers,pOfflineControllerIdArray,lNumberOfOfflineControllers)

#define IVdsSubSystem_CreateLun(This,type,ullSizeInBytes,pDriveIdArray,lNumberOfDrives,pwszUnmaskingList,pHints,ppAsync)	\
    (This)->lpVtbl -> CreateLun(This,type,ullSizeInBytes,pDriveIdArray,lNumberOfDrives,pwszUnmaskingList,pHints,ppAsync)

#define IVdsSubSystem_ReplaceDrive(This,DriveToBeReplaced,ReplacementDrive)	\
    (This)->lpVtbl -> ReplaceDrive(This,DriveToBeReplaced,ReplacementDrive)

#define IVdsSubSystem_SetStatus(This,status)	\
    (This)->lpVtbl -> SetStatus(This,status)

#define IVdsSubSystem_QueryMaxLunCreateSize(This,type,pDriveIdArray,lNumberOfDrives,pHints,pullMaxLunSize)	\
    (This)->lpVtbl -> QueryMaxLunCreateSize(This,type,pDriveIdArray,lNumberOfDrives,pHints,pullMaxLunSize)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_GetProperties_Proxy( 
    IVdsSubSystem * This,
    /* [out] */ VDS_SUB_SYSTEM_PROP *pSubSystemProp);


void __RPC_STUB IVdsSubSystem_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_GetProvider_Proxy( 
    IVdsSubSystem * This,
    /* [out] */ IVdsProvider **ppProvider);


void __RPC_STUB IVdsSubSystem_GetProvider_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_QueryControllers_Proxy( 
    IVdsSubSystem * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsSubSystem_QueryControllers_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_QueryLuns_Proxy( 
    IVdsSubSystem * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsSubSystem_QueryLuns_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_QueryDrives_Proxy( 
    IVdsSubSystem * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsSubSystem_QueryDrives_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_GetDrive_Proxy( 
    IVdsSubSystem * This,
    /* [in] */ SHORT sBusNumber,
    /* [in] */ SHORT sSlotNumber,
    /* [out] */ IVdsDrive **ppDrive);


void __RPC_STUB IVdsSubSystem_GetDrive_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_Reenumerate_Proxy( 
    IVdsSubSystem * This);


void __RPC_STUB IVdsSubSystem_Reenumerate_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_SetControllerStatus_Proxy( 
    IVdsSubSystem * This,
    /* [size_is][in] */ VDS_OBJECT_ID *pOnlineControllerIdArray,
    /* [in] */ LONG lNumberOfOnlineControllers,
    /* [size_is][in] */ VDS_OBJECT_ID *pOfflineControllerIdArray,
    /* [in] */ LONG lNumberOfOfflineControllers);


void __RPC_STUB IVdsSubSystem_SetControllerStatus_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_CreateLun_Proxy( 
    IVdsSubSystem * This,
    /* [in] */ VDS_LUN_TYPE type,
    /* [in] */ ULONGLONG ullSizeInBytes,
    /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
    /* [in] */ LONG lNumberOfDrives,
    /* [string][in] */ LPWSTR pwszUnmaskingList,
    /* [unique][in] */ VDS_HINTS *pHints,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsSubSystem_CreateLun_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_ReplaceDrive_Proxy( 
    IVdsSubSystem * This,
    /* [in] */ VDS_OBJECT_ID DriveToBeReplaced,
    /* [in] */ VDS_OBJECT_ID ReplacementDrive);


void __RPC_STUB IVdsSubSystem_ReplaceDrive_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_SetStatus_Proxy( 
    IVdsSubSystem * This,
    /* [in] */ VDS_SUB_SYSTEM_STATUS status);


void __RPC_STUB IVdsSubSystem_SetStatus_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsSubSystem_QueryMaxLunCreateSize_Proxy( 
    IVdsSubSystem * This,
    /* [in] */ VDS_LUN_TYPE type,
    /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
    /* [in] */ LONG lNumberOfDrives,
    /* [unique][in] */ VDS_HINTS *pHints,
    /* [out] */ ULONGLONG *pullMaxLunSize);


void __RPC_STUB IVdsSubSystem_QueryMaxLunCreateSize_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsSubSystem_INTERFACE_DEFINED__ */


#ifndef __IVdsController_INTERFACE_DEFINED__
#define __IVdsController_INTERFACE_DEFINED__

/* interface IVdsController */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsController;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("cb53d96e-dffb-474a-a078-790d1e2bc082")
    IVdsController : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_CONTROLLER_PROP *pControllerProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetSubSystem( 
            /* [out] */ IVdsSubSystem **ppSubSystem) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetPortProperties( 
            /* [in] */ SHORT sPortNumber,
            /* [out] */ VDS_PORT_PROP *pPortProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE FlushCache( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE InvalidateCache( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Reset( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryAssociatedLuns( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetStatus( 
            /* [in] */ VDS_CONTROLLER_STATUS status) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsControllerVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsController * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsController * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsController * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsController * This,
            /* [out] */ VDS_CONTROLLER_PROP *pControllerProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetSubSystem )( 
            IVdsController * This,
            /* [out] */ IVdsSubSystem **ppSubSystem);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetPortProperties )( 
            IVdsController * This,
            /* [in] */ SHORT sPortNumber,
            /* [out] */ VDS_PORT_PROP *pPortProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *FlushCache )( 
            IVdsController * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *InvalidateCache )( 
            IVdsController * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Reset )( 
            IVdsController * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryAssociatedLuns )( 
            IVdsController * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetStatus )( 
            IVdsController * This,
            /* [in] */ VDS_CONTROLLER_STATUS status);
        
        END_INTERFACE
    } IVdsControllerVtbl;

    interface IVdsController
    {
        CONST_VTBL struct IVdsControllerVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsController_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsController_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsController_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsController_GetProperties(This,pControllerProp)	\
    (This)->lpVtbl -> GetProperties(This,pControllerProp)

#define IVdsController_GetSubSystem(This,ppSubSystem)	\
    (This)->lpVtbl -> GetSubSystem(This,ppSubSystem)

#define IVdsController_GetPortProperties(This,sPortNumber,pPortProp)	\
    (This)->lpVtbl -> GetPortProperties(This,sPortNumber,pPortProp)

#define IVdsController_FlushCache(This)	\
    (This)->lpVtbl -> FlushCache(This)

#define IVdsController_InvalidateCache(This)	\
    (This)->lpVtbl -> InvalidateCache(This)

#define IVdsController_Reset(This)	\
    (This)->lpVtbl -> Reset(This)

#define IVdsController_QueryAssociatedLuns(This,ppEnum)	\
    (This)->lpVtbl -> QueryAssociatedLuns(This,ppEnum)

#define IVdsController_SetStatus(This,status)	\
    (This)->lpVtbl -> SetStatus(This,status)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsController_GetProperties_Proxy( 
    IVdsController * This,
    /* [out] */ VDS_CONTROLLER_PROP *pControllerProp);


void __RPC_STUB IVdsController_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsController_GetSubSystem_Proxy( 
    IVdsController * This,
    /* [out] */ IVdsSubSystem **ppSubSystem);


void __RPC_STUB IVdsController_GetSubSystem_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsController_GetPortProperties_Proxy( 
    IVdsController * This,
    /* [in] */ SHORT sPortNumber,
    /* [out] */ VDS_PORT_PROP *pPortProp);


void __RPC_STUB IVdsController_GetPortProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsController_FlushCache_Proxy( 
    IVdsController * This);


void __RPC_STUB IVdsController_FlushCache_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsController_InvalidateCache_Proxy( 
    IVdsController * This);


void __RPC_STUB IVdsController_InvalidateCache_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsController_Reset_Proxy( 
    IVdsController * This);


void __RPC_STUB IVdsController_Reset_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsController_QueryAssociatedLuns_Proxy( 
    IVdsController * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsController_QueryAssociatedLuns_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsController_SetStatus_Proxy( 
    IVdsController * This,
    /* [in] */ VDS_CONTROLLER_STATUS status);


void __RPC_STUB IVdsController_SetStatus_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsController_INTERFACE_DEFINED__ */


#ifndef __IVdsDrive_INTERFACE_DEFINED__
#define __IVdsDrive_INTERFACE_DEFINED__

/* interface IVdsDrive */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsDrive;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("ff24efa4-aade-4b6b-898b-eaa6a20887c7")
    IVdsDrive : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_DRIVE_PROP *pDriveProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetSubSystem( 
            /* [out] */ IVdsSubSystem **ppSubSystem) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryExtents( 
            /* [size_is][size_is][out] */ VDS_DRIVE_EXTENT **ppExtentArray,
            /* [out] */ LONG *plNumberOfExtents) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetFlags( 
            /* [in] */ ULONG ulFlags) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ClearFlags( 
            /* [in] */ ULONG ulFlags) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetStatus( 
            /* [in] */ VDS_DRIVE_STATUS status) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsDriveVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsDrive * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsDrive * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsDrive * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsDrive * This,
            /* [out] */ VDS_DRIVE_PROP *pDriveProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetSubSystem )( 
            IVdsDrive * This,
            /* [out] */ IVdsSubSystem **ppSubSystem);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryExtents )( 
            IVdsDrive * This,
            /* [size_is][size_is][out] */ VDS_DRIVE_EXTENT **ppExtentArray,
            /* [out] */ LONG *plNumberOfExtents);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetFlags )( 
            IVdsDrive * This,
            /* [in] */ ULONG ulFlags);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ClearFlags )( 
            IVdsDrive * This,
            /* [in] */ ULONG ulFlags);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetStatus )( 
            IVdsDrive * This,
            /* [in] */ VDS_DRIVE_STATUS status);
        
        END_INTERFACE
    } IVdsDriveVtbl;

    interface IVdsDrive
    {
        CONST_VTBL struct IVdsDriveVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsDrive_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsDrive_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsDrive_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsDrive_GetProperties(This,pDriveProp)	\
    (This)->lpVtbl -> GetProperties(This,pDriveProp)

#define IVdsDrive_GetSubSystem(This,ppSubSystem)	\
    (This)->lpVtbl -> GetSubSystem(This,ppSubSystem)

#define IVdsDrive_QueryExtents(This,ppExtentArray,plNumberOfExtents)	\
    (This)->lpVtbl -> QueryExtents(This,ppExtentArray,plNumberOfExtents)

#define IVdsDrive_SetFlags(This,ulFlags)	\
    (This)->lpVtbl -> SetFlags(This,ulFlags)

#define IVdsDrive_ClearFlags(This,ulFlags)	\
    (This)->lpVtbl -> ClearFlags(This,ulFlags)

#define IVdsDrive_SetStatus(This,status)	\
    (This)->lpVtbl -> SetStatus(This,status)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDrive_GetProperties_Proxy( 
    IVdsDrive * This,
    /* [out] */ VDS_DRIVE_PROP *pDriveProp);


void __RPC_STUB IVdsDrive_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDrive_GetSubSystem_Proxy( 
    IVdsDrive * This,
    /* [out] */ IVdsSubSystem **ppSubSystem);


void __RPC_STUB IVdsDrive_GetSubSystem_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDrive_QueryExtents_Proxy( 
    IVdsDrive * This,
    /* [size_is][size_is][out] */ VDS_DRIVE_EXTENT **ppExtentArray,
    /* [out] */ LONG *plNumberOfExtents);


void __RPC_STUB IVdsDrive_QueryExtents_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDrive_SetFlags_Proxy( 
    IVdsDrive * This,
    /* [in] */ ULONG ulFlags);


void __RPC_STUB IVdsDrive_SetFlags_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDrive_ClearFlags_Proxy( 
    IVdsDrive * This,
    /* [in] */ ULONG ulFlags);


void __RPC_STUB IVdsDrive_ClearFlags_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDrive_SetStatus_Proxy( 
    IVdsDrive * This,
    /* [in] */ VDS_DRIVE_STATUS status);


void __RPC_STUB IVdsDrive_SetStatus_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsDrive_INTERFACE_DEFINED__ */


#ifndef __IVdsLun_INTERFACE_DEFINED__
#define __IVdsLun_INTERFACE_DEFINED__

/* interface IVdsLun */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsLun;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("3540a9c7-e60f-4111-a840-8bba6c2c83d8")
    IVdsLun : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_LUN_PROP *pLunProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetSubSystem( 
            /* [out] */ IVdsSubSystem **ppSubSystem) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetIdentificationData( 
            /* [out] */ VDS_LUN_INFORMATION *pLunInfo) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryActiveControllers( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Extend( 
            /* [in] */ ULONGLONG ullNumberOfBytesToAdd,
            /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
            /* [in] */ LONG lNumberOfDrives,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Shrink( 
            /* [in] */ ULONGLONG uNumberOfBytesToRemove,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryPlexes( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AddPlex( 
            /* [in] */ VDS_OBJECT_ID lunId,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE RemovePlex( 
            /* [in] */ VDS_OBJECT_ID plexId,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Recover( 
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetMask( 
            /* [string][in] */ LPWSTR pwszUnmaskingList) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Delete( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AssociateControllers( 
            /* [size_is][unique][in] */ VDS_OBJECT_ID *pActiveControllerIdArray,
            /* [in] */ LONG lNumberOfActiveControllers,
            /* [size_is][unique][in] */ VDS_OBJECT_ID *pInactiveControllerIdArray,
            /* [in] */ LONG lNumberOfInactiveControllers) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryHints( 
            /* [out] */ VDS_HINTS *pHints) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ApplyHints( 
            /* [in] */ VDS_HINTS *pHints) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetStatus( 
            /* [in] */ VDS_LUN_STATUS status) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryMaxLunExtendSize( 
            /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
            /* [in] */ LONG lNumberOfDrives,
            /* [out] */ ULONGLONG *pullMaxBytesToBeAdded) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsLunVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsLun * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsLun * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsLun * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsLun * This,
            /* [out] */ VDS_LUN_PROP *pLunProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetSubSystem )( 
            IVdsLun * This,
            /* [out] */ IVdsSubSystem **ppSubSystem);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetIdentificationData )( 
            IVdsLun * This,
            /* [out] */ VDS_LUN_INFORMATION *pLunInfo);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryActiveControllers )( 
            IVdsLun * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Extend )( 
            IVdsLun * This,
            /* [in] */ ULONGLONG ullNumberOfBytesToAdd,
            /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
            /* [in] */ LONG lNumberOfDrives,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Shrink )( 
            IVdsLun * This,
            /* [in] */ ULONGLONG uNumberOfBytesToRemove,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryPlexes )( 
            IVdsLun * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AddPlex )( 
            IVdsLun * This,
            /* [in] */ VDS_OBJECT_ID lunId,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *RemovePlex )( 
            IVdsLun * This,
            /* [in] */ VDS_OBJECT_ID plexId,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Recover )( 
            IVdsLun * This,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetMask )( 
            IVdsLun * This,
            /* [string][in] */ LPWSTR pwszUnmaskingList);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Delete )( 
            IVdsLun * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AssociateControllers )( 
            IVdsLun * This,
            /* [size_is][unique][in] */ VDS_OBJECT_ID *pActiveControllerIdArray,
            /* [in] */ LONG lNumberOfActiveControllers,
            /* [size_is][unique][in] */ VDS_OBJECT_ID *pInactiveControllerIdArray,
            /* [in] */ LONG lNumberOfInactiveControllers);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryHints )( 
            IVdsLun * This,
            /* [out] */ VDS_HINTS *pHints);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ApplyHints )( 
            IVdsLun * This,
            /* [in] */ VDS_HINTS *pHints);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetStatus )( 
            IVdsLun * This,
            /* [in] */ VDS_LUN_STATUS status);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryMaxLunExtendSize )( 
            IVdsLun * This,
            /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
            /* [in] */ LONG lNumberOfDrives,
            /* [out] */ ULONGLONG *pullMaxBytesToBeAdded);
        
        END_INTERFACE
    } IVdsLunVtbl;

    interface IVdsLun
    {
        CONST_VTBL struct IVdsLunVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsLun_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsLun_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsLun_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsLun_GetProperties(This,pLunProp)	\
    (This)->lpVtbl -> GetProperties(This,pLunProp)

#define IVdsLun_GetSubSystem(This,ppSubSystem)	\
    (This)->lpVtbl -> GetSubSystem(This,ppSubSystem)

#define IVdsLun_GetIdentificationData(This,pLunInfo)	\
    (This)->lpVtbl -> GetIdentificationData(This,pLunInfo)

#define IVdsLun_QueryActiveControllers(This,ppEnum)	\
    (This)->lpVtbl -> QueryActiveControllers(This,ppEnum)

#define IVdsLun_Extend(This,ullNumberOfBytesToAdd,pDriveIdArray,lNumberOfDrives,ppAsync)	\
    (This)->lpVtbl -> Extend(This,ullNumberOfBytesToAdd,pDriveIdArray,lNumberOfDrives,ppAsync)

#define IVdsLun_Shrink(This,uNumberOfBytesToRemove,ppAsync)	\
    (This)->lpVtbl -> Shrink(This,uNumberOfBytesToRemove,ppAsync)

#define IVdsLun_QueryPlexes(This,ppEnum)	\
    (This)->lpVtbl -> QueryPlexes(This,ppEnum)

#define IVdsLun_AddPlex(This,lunId,ppAsync)	\
    (This)->lpVtbl -> AddPlex(This,lunId,ppAsync)

#define IVdsLun_RemovePlex(This,plexId,ppAsync)	\
    (This)->lpVtbl -> RemovePlex(This,plexId,ppAsync)

#define IVdsLun_Recover(This,ppAsync)	\
    (This)->lpVtbl -> Recover(This,ppAsync)

#define IVdsLun_SetMask(This,pwszUnmaskingList)	\
    (This)->lpVtbl -> SetMask(This,pwszUnmaskingList)

#define IVdsLun_Delete(This)	\
    (This)->lpVtbl -> Delete(This)

#define IVdsLun_AssociateControllers(This,pActiveControllerIdArray,lNumberOfActiveControllers,pInactiveControllerIdArray,lNumberOfInactiveControllers)	\
    (This)->lpVtbl -> AssociateControllers(This,pActiveControllerIdArray,lNumberOfActiveControllers,pInactiveControllerIdArray,lNumberOfInactiveControllers)

#define IVdsLun_QueryHints(This,pHints)	\
    (This)->lpVtbl -> QueryHints(This,pHints)

#define IVdsLun_ApplyHints(This,pHints)	\
    (This)->lpVtbl -> ApplyHints(This,pHints)

#define IVdsLun_SetStatus(This,status)	\
    (This)->lpVtbl -> SetStatus(This,status)

#define IVdsLun_QueryMaxLunExtendSize(This,pDriveIdArray,lNumberOfDrives,pullMaxBytesToBeAdded)	\
    (This)->lpVtbl -> QueryMaxLunExtendSize(This,pDriveIdArray,lNumberOfDrives,pullMaxBytesToBeAdded)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_GetProperties_Proxy( 
    IVdsLun * This,
    /* [out] */ VDS_LUN_PROP *pLunProp);


void __RPC_STUB IVdsLun_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_GetSubSystem_Proxy( 
    IVdsLun * This,
    /* [out] */ IVdsSubSystem **ppSubSystem);


void __RPC_STUB IVdsLun_GetSubSystem_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_GetIdentificationData_Proxy( 
    IVdsLun * This,
    /* [out] */ VDS_LUN_INFORMATION *pLunInfo);


void __RPC_STUB IVdsLun_GetIdentificationData_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_QueryActiveControllers_Proxy( 
    IVdsLun * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsLun_QueryActiveControllers_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_Extend_Proxy( 
    IVdsLun * This,
    /* [in] */ ULONGLONG ullNumberOfBytesToAdd,
    /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
    /* [in] */ LONG lNumberOfDrives,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsLun_Extend_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_Shrink_Proxy( 
    IVdsLun * This,
    /* [in] */ ULONGLONG uNumberOfBytesToRemove,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsLun_Shrink_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_QueryPlexes_Proxy( 
    IVdsLun * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsLun_QueryPlexes_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_AddPlex_Proxy( 
    IVdsLun * This,
    /* [in] */ VDS_OBJECT_ID lunId,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsLun_AddPlex_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_RemovePlex_Proxy( 
    IVdsLun * This,
    /* [in] */ VDS_OBJECT_ID plexId,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsLun_RemovePlex_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_Recover_Proxy( 
    IVdsLun * This,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsLun_Recover_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_SetMask_Proxy( 
    IVdsLun * This,
    /* [string][in] */ LPWSTR pwszUnmaskingList);


void __RPC_STUB IVdsLun_SetMask_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_Delete_Proxy( 
    IVdsLun * This);


void __RPC_STUB IVdsLun_Delete_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_AssociateControllers_Proxy( 
    IVdsLun * This,
    /* [size_is][unique][in] */ VDS_OBJECT_ID *pActiveControllerIdArray,
    /* [in] */ LONG lNumberOfActiveControllers,
    /* [size_is][unique][in] */ VDS_OBJECT_ID *pInactiveControllerIdArray,
    /* [in] */ LONG lNumberOfInactiveControllers);


void __RPC_STUB IVdsLun_AssociateControllers_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_QueryHints_Proxy( 
    IVdsLun * This,
    /* [out] */ VDS_HINTS *pHints);


void __RPC_STUB IVdsLun_QueryHints_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_ApplyHints_Proxy( 
    IVdsLun * This,
    /* [in] */ VDS_HINTS *pHints);


void __RPC_STUB IVdsLun_ApplyHints_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_SetStatus_Proxy( 
    IVdsLun * This,
    /* [in] */ VDS_LUN_STATUS status);


void __RPC_STUB IVdsLun_SetStatus_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLun_QueryMaxLunExtendSize_Proxy( 
    IVdsLun * This,
    /* [unique][size_is][in] */ VDS_OBJECT_ID *pDriveIdArray,
    /* [in] */ LONG lNumberOfDrives,
    /* [out] */ ULONGLONG *pullMaxBytesToBeAdded);


void __RPC_STUB IVdsLun_QueryMaxLunExtendSize_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsLun_INTERFACE_DEFINED__ */


#ifndef __IVdsLunPlex_INTERFACE_DEFINED__
#define __IVdsLunPlex_INTERFACE_DEFINED__

/* interface IVdsLunPlex */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsLunPlex;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("0ee1a790-5d2e-4abb-8c99-c481e8be2138")
    IVdsLunPlex : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_LUN_PLEX_PROP *pPlexProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetLun( 
            /* [out] */ IVdsLun **ppLun) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryExtents( 
            /* [size_is][size_is][out] */ VDS_DRIVE_EXTENT **ppExtentArray,
            /* [out] */ LONG *plNumberOfExtents) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryHints( 
            /* [out] */ VDS_HINTS *pHints) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ApplyHints( 
            /* [in] */ VDS_HINTS *pHints) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsLunPlexVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsLunPlex * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsLunPlex * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsLunPlex * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsLunPlex * This,
            /* [out] */ VDS_LUN_PLEX_PROP *pPlexProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetLun )( 
            IVdsLunPlex * This,
            /* [out] */ IVdsLun **ppLun);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryExtents )( 
            IVdsLunPlex * This,
            /* [size_is][size_is][out] */ VDS_DRIVE_EXTENT **ppExtentArray,
            /* [out] */ LONG *plNumberOfExtents);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryHints )( 
            IVdsLunPlex * This,
            /* [out] */ VDS_HINTS *pHints);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ApplyHints )( 
            IVdsLunPlex * This,
            /* [in] */ VDS_HINTS *pHints);
        
        END_INTERFACE
    } IVdsLunPlexVtbl;

    interface IVdsLunPlex
    {
        CONST_VTBL struct IVdsLunPlexVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsLunPlex_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsLunPlex_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsLunPlex_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsLunPlex_GetProperties(This,pPlexProp)	\
    (This)->lpVtbl -> GetProperties(This,pPlexProp)

#define IVdsLunPlex_GetLun(This,ppLun)	\
    (This)->lpVtbl -> GetLun(This,ppLun)

#define IVdsLunPlex_QueryExtents(This,ppExtentArray,plNumberOfExtents)	\
    (This)->lpVtbl -> QueryExtents(This,ppExtentArray,plNumberOfExtents)

#define IVdsLunPlex_QueryHints(This,pHints)	\
    (This)->lpVtbl -> QueryHints(This,pHints)

#define IVdsLunPlex_ApplyHints(This,pHints)	\
    (This)->lpVtbl -> ApplyHints(This,pHints)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLunPlex_GetProperties_Proxy( 
    IVdsLunPlex * This,
    /* [out] */ VDS_LUN_PLEX_PROP *pPlexProp);


void __RPC_STUB IVdsLunPlex_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLunPlex_GetLun_Proxy( 
    IVdsLunPlex * This,
    /* [out] */ IVdsLun **ppLun);


void __RPC_STUB IVdsLunPlex_GetLun_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLunPlex_QueryExtents_Proxy( 
    IVdsLunPlex * This,
    /* [size_is][size_is][out] */ VDS_DRIVE_EXTENT **ppExtentArray,
    /* [out] */ LONG *plNumberOfExtents);


void __RPC_STUB IVdsLunPlex_QueryExtents_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLunPlex_QueryHints_Proxy( 
    IVdsLunPlex * This,
    /* [out] */ VDS_HINTS *pHints);


void __RPC_STUB IVdsLunPlex_QueryHints_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsLunPlex_ApplyHints_Proxy( 
    IVdsLunPlex * This,
    /* [in] */ VDS_HINTS *pHints);


void __RPC_STUB IVdsLunPlex_ApplyHints_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsLunPlex_INTERFACE_DEFINED__ */


#ifndef __IVdsMaintenance_INTERFACE_DEFINED__
#define __IVdsMaintenance_INTERFACE_DEFINED__

/* interface IVdsMaintenance */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsMaintenance;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("daebeef3-8523-47ed-a2b9-05cecce2a1ae")
    IVdsMaintenance : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE StartMaintenance( 
            /* [in] */ VDS_MAINTENANCE_OPERATION operation) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE StopMaintenance( 
            /* [in] */ VDS_MAINTENANCE_OPERATION operation) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE PulseMaintenance( 
            /* [in] */ VDS_MAINTENANCE_OPERATION operation,
            /* [in] */ ULONG ulCount) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsMaintenanceVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsMaintenance * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsMaintenance * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsMaintenance * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *StartMaintenance )( 
            IVdsMaintenance * This,
            /* [in] */ VDS_MAINTENANCE_OPERATION operation);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *StopMaintenance )( 
            IVdsMaintenance * This,
            /* [in] */ VDS_MAINTENANCE_OPERATION operation);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *PulseMaintenance )( 
            IVdsMaintenance * This,
            /* [in] */ VDS_MAINTENANCE_OPERATION operation,
            /* [in] */ ULONG ulCount);
        
        END_INTERFACE
    } IVdsMaintenanceVtbl;

    interface IVdsMaintenance
    {
        CONST_VTBL struct IVdsMaintenanceVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsMaintenance_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsMaintenance_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsMaintenance_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsMaintenance_StartMaintenance(This,operation)	\
    (This)->lpVtbl -> StartMaintenance(This,operation)

#define IVdsMaintenance_StopMaintenance(This,operation)	\
    (This)->lpVtbl -> StopMaintenance(This,operation)

#define IVdsMaintenance_PulseMaintenance(This,operation,ulCount)	\
    (This)->lpVtbl -> PulseMaintenance(This,operation,ulCount)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsMaintenance_StartMaintenance_Proxy( 
    IVdsMaintenance * This,
    /* [in] */ VDS_MAINTENANCE_OPERATION operation);


void __RPC_STUB IVdsMaintenance_StartMaintenance_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsMaintenance_StopMaintenance_Proxy( 
    IVdsMaintenance * This,
    /* [in] */ VDS_MAINTENANCE_OPERATION operation);


void __RPC_STUB IVdsMaintenance_StopMaintenance_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsMaintenance_PulseMaintenance_Proxy( 
    IVdsMaintenance * This,
    /* [in] */ VDS_MAINTENANCE_OPERATION operation,
    /* [in] */ ULONG ulCount);


void __RPC_STUB IVdsMaintenance_PulseMaintenance_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsMaintenance_INTERFACE_DEFINED__ */


/* interface __MIDL_itf_vds_0276 */
/* [local] */ 

typedef 
enum _VDS_FILE_SYSTEM_FLAG
    {	VDS_FSF_SUPPORT_FORMAT	= 0x1,
	VDS_FSF_SUPPORT_QUICK_FORMAT	= 0x2,
	VDS_FSF_SUPPORT_COMPRESS	= 0x4,
	VDS_FSF_SUPPORT_SPECIFY_LABEL	= 0x8,
	VDS_FSF_SUPPORT_MOUNT_POINT	= 0x10,
	VDS_FSF_SUPPORT_REMOVABLE_MEDIA	= 0x20,
	VDS_FSF_SUPPORT_EXTEND	= 0x40,
	VDS_FSF_ALLOCATION_UNIT_512	= 0x10000,
	VDS_FSF_ALLOCATION_UNIT_1K	= 0x20000,
	VDS_FSF_ALLOCATION_UNIT_2K	= 0x40000,
	VDS_FSF_ALLOCATION_UNIT_4K	= 0x80000,
	VDS_FSF_ALLOCATION_UNIT_8K	= 0x100000,
	VDS_FSF_ALLOCATION_UNIT_16K	= 0x200000,
	VDS_FSF_ALLOCATION_UNIT_32K	= 0x400000,
	VDS_FSF_ALLOCATION_UNIT_64K	= 0x800000,
	VDS_FSF_ALLOCATION_UNIT_128K	= 0x1000000,
	VDS_FSF_ALLOCATION_UNIT_256K	= 0x2000000
    } 	VDS_FILE_SYSTEM_FLAG;

typedef struct _VDS_FILE_SYSTEM_TYPE_PROP
    {
    VDS_FILE_SYSTEM_TYPE type;
    WCHAR wszName[ 8 ];
    ULONG ulFlags;
    ULONG ulCompressionFlags;
    ULONG ulMaxLableLength;
    /* [string] */ LPWSTR pwszIllegalLabelCharSet;
    } 	VDS_FILE_SYSTEM_TYPE_PROP;

typedef struct _VDS_FILE_SYSTEM_TYPE_PROP *PVDS_FILE_SYSTEM_TYPE_PROP;

typedef 
enum _VDS_FILE_SYSTEM_PROP_FLAG
    {	VDS_FPF_COMPRESSED	= 0x1
    } 	VDS_FILE_SYSTEM_PROP_FLAG;

typedef struct _VDS_FILE_SYSTEM_PROP
    {
    VDS_FILE_SYSTEM_TYPE type;
    VDS_OBJECT_ID volumeId;
    ULONG ulFlags;
    ULONGLONG ullTotalAllocationUnits;
    ULONGLONG ullAvailableAllocationUnits;
    ULONG ulAllocationUnitSize;
    /* [string] */ LPWSTR pwszLabel;
    } 	VDS_FILE_SYSTEM_PROP;

typedef struct _VDS_FILE_SYSTEM_PROP *PVDS_FILE_SYSTEM_PROP;

typedef 
enum _VDS_PATH_STATUS
    {	VDS_PHS_UNKNOWN	= 0,
	VDS_PHS_ENABLED	= VDS_PHS_UNKNOWN + 1,
	VDS_PHS_CAPABLE	= VDS_PHS_ENABLED + 1,
	VDS_PHS_BLOCKED	= VDS_PHS_CAPABLE + 1
    } 	VDS_PATH_STATUS;

typedef 
enum _VDS_PATH_FLAG
    {	VDS_PHF_ACTIVE	= 0,
	VDS_PHF_READ	= VDS_PHF_ACTIVE + 1,
	VDS_PHF_WRITE	= VDS_PHF_READ + 1
    } 	VDS_PATH_FLAG;

typedef struct _VDS_DISK_PATH
    {
    /* [string] */ LPWSTR pwszPath;
    VDS_PATH_STATUS status;
    VDS_HEALTH health;
    ULONG ulFlags;
    } 	VDS_DISK_PATH;



extern RPC_IF_HANDLE __MIDL_itf_vds_0276_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_vds_0276_v0_0_s_ifspec;

#ifndef __IVdsServiceLoader_INTERFACE_DEFINED__
#define __IVdsServiceLoader_INTERFACE_DEFINED__

/* interface IVdsServiceLoader */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsServiceLoader;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("e0393303-90d4-4a97-ab71-e9b671ee2729")
    IVdsServiceLoader : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE LoadService( 
            /* [string][unique][in] */ LPWSTR pwszMachineName,
            /* [out] */ IVdsService **ppService) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsServiceLoaderVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsServiceLoader * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsServiceLoader * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsServiceLoader * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *LoadService )( 
            IVdsServiceLoader * This,
            /* [string][unique][in] */ LPWSTR pwszMachineName,
            /* [out] */ IVdsService **ppService);
        
        END_INTERFACE
    } IVdsServiceLoaderVtbl;

    interface IVdsServiceLoader
    {
        CONST_VTBL struct IVdsServiceLoaderVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsServiceLoader_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsServiceLoader_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsServiceLoader_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsServiceLoader_LoadService(This,pwszMachineName,ppService)	\
    (This)->lpVtbl -> LoadService(This,pwszMachineName,ppService)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsServiceLoader_LoadService_Proxy( 
    IVdsServiceLoader * This,
    /* [string][unique][in] */ LPWSTR pwszMachineName,
    /* [out] */ IVdsService **ppService);


void __RPC_STUB IVdsServiceLoader_LoadService_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsServiceLoader_INTERFACE_DEFINED__ */


#ifndef __IVdsService_INTERFACE_DEFINED__
#define __IVdsService_INTERFACE_DEFINED__

/* interface IVdsService */
/* [unique][uuid][object] */ 

typedef 
enum _VDS_QUERY_PROVIDER_FLAG
    {	VDS_QUERY_SOFTWARE_PROVIDERS	= 0x1,
	VDS_QUERY_HARDWARE_PROVIDERS	= 0x2
    } 	VDS_QUERY_PROVIDER_FLAG;


EXTERN_C const IID IID_IVdsService;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("0818a8ef-9ba9-40d8-a6f9-e22833cc771e")
    IVdsService : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE IsServiceReady( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE WaitForServiceReady( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetProperties( 
            /* [out] */ VDS_SERVICE_PROP *pServiceProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryProviders( 
            /* [in] */ DWORD masks,
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryMaskedDisks( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryUnallocatedDisks( 
            /* [out] */ IEnumVdsObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetObject( 
            /* [in] */ VDS_OBJECT_ID ObjectId,
            /* [in] */ VDS_OBJECT_TYPE type,
            /* [out] */ IUnknown **ppObjectUnk) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryDriveLetters( 
            /* [in] */ WCHAR wcFirstLetter,
            /* [in] */ DWORD count,
            /* [size_is][out] */ VDS_DRIVE_LETTER_PROP *pDriveLetterPropArray) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryFileSystemTypes( 
            /* [size_is][size_is][out] */ VDS_FILE_SYSTEM_TYPE_PROP **ppFileSystemTypeProps,
            /* [out] */ LONG *plNumberOfFileSystems) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Reenumerate( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Refresh( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE CleanupObsoleteMountPoints( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Advise( 
            /* [in] */ IVdsAdviseSink *pSink,
            /* [out] */ DWORD *pdwCookie) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Unadvise( 
            /* [in] */ DWORD dwCookie) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Reboot( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetFlags( 
            /* [in] */ ULONG ulFlags) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ClearFlags( 
            /* [in] */ ULONG ulFlags) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsServiceVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsService * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsService * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsService * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *IsServiceReady )( 
            IVdsService * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *WaitForServiceReady )( 
            IVdsService * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetProperties )( 
            IVdsService * This,
            /* [out] */ VDS_SERVICE_PROP *pServiceProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryProviders )( 
            IVdsService * This,
            /* [in] */ DWORD masks,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryMaskedDisks )( 
            IVdsService * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryUnallocatedDisks )( 
            IVdsService * This,
            /* [out] */ IEnumVdsObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetObject )( 
            IVdsService * This,
            /* [in] */ VDS_OBJECT_ID ObjectId,
            /* [in] */ VDS_OBJECT_TYPE type,
            /* [out] */ IUnknown **ppObjectUnk);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryDriveLetters )( 
            IVdsService * This,
            /* [in] */ WCHAR wcFirstLetter,
            /* [in] */ DWORD count,
            /* [size_is][out] */ VDS_DRIVE_LETTER_PROP *pDriveLetterPropArray);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryFileSystemTypes )( 
            IVdsService * This,
            /* [size_is][size_is][out] */ VDS_FILE_SYSTEM_TYPE_PROP **ppFileSystemTypeProps,
            /* [out] */ LONG *plNumberOfFileSystems);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Reenumerate )( 
            IVdsService * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Refresh )( 
            IVdsService * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *CleanupObsoleteMountPoints )( 
            IVdsService * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Advise )( 
            IVdsService * This,
            /* [in] */ IVdsAdviseSink *pSink,
            /* [out] */ DWORD *pdwCookie);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Unadvise )( 
            IVdsService * This,
            /* [in] */ DWORD dwCookie);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Reboot )( 
            IVdsService * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetFlags )( 
            IVdsService * This,
            /* [in] */ ULONG ulFlags);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ClearFlags )( 
            IVdsService * This,
            /* [in] */ ULONG ulFlags);
        
        END_INTERFACE
    } IVdsServiceVtbl;

    interface IVdsService
    {
        CONST_VTBL struct IVdsServiceVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsService_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsService_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsService_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsService_IsServiceReady(This)	\
    (This)->lpVtbl -> IsServiceReady(This)

#define IVdsService_WaitForServiceReady(This)	\
    (This)->lpVtbl -> WaitForServiceReady(This)

#define IVdsService_GetProperties(This,pServiceProp)	\
    (This)->lpVtbl -> GetProperties(This,pServiceProp)

#define IVdsService_QueryProviders(This,masks,ppEnum)	\
    (This)->lpVtbl -> QueryProviders(This,masks,ppEnum)

#define IVdsService_QueryMaskedDisks(This,ppEnum)	\
    (This)->lpVtbl -> QueryMaskedDisks(This,ppEnum)

#define IVdsService_QueryUnallocatedDisks(This,ppEnum)	\
    (This)->lpVtbl -> QueryUnallocatedDisks(This,ppEnum)

#define IVdsService_GetObject(This,ObjectId,type,ppObjectUnk)	\
    (This)->lpVtbl -> GetObject(This,ObjectId,type,ppObjectUnk)

#define IVdsService_QueryDriveLetters(This,wcFirstLetter,count,pDriveLetterPropArray)	\
    (This)->lpVtbl -> QueryDriveLetters(This,wcFirstLetter,count,pDriveLetterPropArray)

#define IVdsService_QueryFileSystemTypes(This,ppFileSystemTypeProps,plNumberOfFileSystems)	\
    (This)->lpVtbl -> QueryFileSystemTypes(This,ppFileSystemTypeProps,plNumberOfFileSystems)

#define IVdsService_Reenumerate(This)	\
    (This)->lpVtbl -> Reenumerate(This)

#define IVdsService_Refresh(This)	\
    (This)->lpVtbl -> Refresh(This)

#define IVdsService_CleanupObsoleteMountPoints(This)	\
    (This)->lpVtbl -> CleanupObsoleteMountPoints(This)

#define IVdsService_Advise(This,pSink,pdwCookie)	\
    (This)->lpVtbl -> Advise(This,pSink,pdwCookie)

#define IVdsService_Unadvise(This,dwCookie)	\
    (This)->lpVtbl -> Unadvise(This,dwCookie)

#define IVdsService_Reboot(This)	\
    (This)->lpVtbl -> Reboot(This)

#define IVdsService_SetFlags(This,ulFlags)	\
    (This)->lpVtbl -> SetFlags(This,ulFlags)

#define IVdsService_ClearFlags(This,ulFlags)	\
    (This)->lpVtbl -> ClearFlags(This,ulFlags)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_IsServiceReady_Proxy( 
    IVdsService * This);


void __RPC_STUB IVdsService_IsServiceReady_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_WaitForServiceReady_Proxy( 
    IVdsService * This);


void __RPC_STUB IVdsService_WaitForServiceReady_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_GetProperties_Proxy( 
    IVdsService * This,
    /* [out] */ VDS_SERVICE_PROP *pServiceProp);


void __RPC_STUB IVdsService_GetProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_QueryProviders_Proxy( 
    IVdsService * This,
    /* [in] */ DWORD masks,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsService_QueryProviders_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_QueryMaskedDisks_Proxy( 
    IVdsService * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsService_QueryMaskedDisks_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_QueryUnallocatedDisks_Proxy( 
    IVdsService * This,
    /* [out] */ IEnumVdsObject **ppEnum);


void __RPC_STUB IVdsService_QueryUnallocatedDisks_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_GetObject_Proxy( 
    IVdsService * This,
    /* [in] */ VDS_OBJECT_ID ObjectId,
    /* [in] */ VDS_OBJECT_TYPE type,
    /* [out] */ IUnknown **ppObjectUnk);


void __RPC_STUB IVdsService_GetObject_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_QueryDriveLetters_Proxy( 
    IVdsService * This,
    /* [in] */ WCHAR wcFirstLetter,
    /* [in] */ DWORD count,
    /* [size_is][out] */ VDS_DRIVE_LETTER_PROP *pDriveLetterPropArray);


void __RPC_STUB IVdsService_QueryDriveLetters_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_QueryFileSystemTypes_Proxy( 
    IVdsService * This,
    /* [size_is][size_is][out] */ VDS_FILE_SYSTEM_TYPE_PROP **ppFileSystemTypeProps,
    /* [out] */ LONG *plNumberOfFileSystems);


void __RPC_STUB IVdsService_QueryFileSystemTypes_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_Reenumerate_Proxy( 
    IVdsService * This);


void __RPC_STUB IVdsService_Reenumerate_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_Refresh_Proxy( 
    IVdsService * This);


void __RPC_STUB IVdsService_Refresh_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_CleanupObsoleteMountPoints_Proxy( 
    IVdsService * This);


void __RPC_STUB IVdsService_CleanupObsoleteMountPoints_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_Advise_Proxy( 
    IVdsService * This,
    /* [in] */ IVdsAdviseSink *pSink,
    /* [out] */ DWORD *pdwCookie);


void __RPC_STUB IVdsService_Advise_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_Unadvise_Proxy( 
    IVdsService * This,
    /* [in] */ DWORD dwCookie);


void __RPC_STUB IVdsService_Unadvise_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_Reboot_Proxy( 
    IVdsService * This);


void __RPC_STUB IVdsService_Reboot_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_SetFlags_Proxy( 
    IVdsService * This,
    /* [in] */ ULONG ulFlags);


void __RPC_STUB IVdsService_SetFlags_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsService_ClearFlags_Proxy( 
    IVdsService * This,
    /* [in] */ ULONG ulFlags);


void __RPC_STUB IVdsService_ClearFlags_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsService_INTERFACE_DEFINED__ */


#ifndef __IVdsServiceInitialization_INTERFACE_DEFINED__
#define __IVdsServiceInitialization_INTERFACE_DEFINED__

/* interface IVdsServiceInitialization */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsServiceInitialization;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("4afc3636-db01-4052-80c3-03bbcb8d3c69")
    IVdsServiceInitialization : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Initialize( 
            /* [string][unique][in] */ LPWSTR pwszMachineName) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsServiceInitializationVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsServiceInitialization * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsServiceInitialization * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsServiceInitialization * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Initialize )( 
            IVdsServiceInitialization * This,
            /* [string][unique][in] */ LPWSTR pwszMachineName);
        
        END_INTERFACE
    } IVdsServiceInitializationVtbl;

    interface IVdsServiceInitialization
    {
        CONST_VTBL struct IVdsServiceInitializationVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsServiceInitialization_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsServiceInitialization_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsServiceInitialization_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsServiceInitialization_Initialize(This,pwszMachineName)	\
    (This)->lpVtbl -> Initialize(This,pwszMachineName)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsServiceInitialization_Initialize_Proxy( 
    IVdsServiceInitialization * This,
    /* [string][unique][in] */ LPWSTR pwszMachineName);


void __RPC_STUB IVdsServiceInitialization_Initialize_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsServiceInitialization_INTERFACE_DEFINED__ */


#ifndef __IVdsVolumeMF_INTERFACE_DEFINED__
#define __IVdsVolumeMF_INTERFACE_DEFINED__

/* interface IVdsVolumeMF */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsVolumeMF;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("ee2d5ded-6236-4169-931d-b9778ce03dc6")
    IVdsVolumeMF : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetFileSystemProperties( 
            /* [out] */ VDS_FILE_SYSTEM_PROP *pFileSystemProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Format( 
            /* [in] */ VDS_FILE_SYSTEM_TYPE type,
            /* [string][in] */ LPWSTR pwszLabel,
            /* [in] */ DWORD dwUnitAllocationSize,
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bQuickFormat,
            /* [in] */ BOOL bEnableCompression,
            /* [out] */ IVdsAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AddAccessPath( 
            /* [string][max_is][in] */ LPWSTR pwszPath) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryAccessPaths( 
            /* [size_is][size_is][string][out] */ LPWSTR **pwszPathArray,
            /* [out] */ LONG *plNumberOfAccessPaths) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryReparsePoints( 
            /* [size_is][size_is][out] */ VDS_REPARSE_POINT_PROP **ppReparsePointProps,
            /* [out] */ LONG *plNumberOfReparsePointProps) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE DeleteAccessPath( 
            /* [string][max_is][in] */ LPWSTR pwszPath,
            /* [in] */ BOOL bForce) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Mount( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Dismount( 
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bPermanent) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetFileSystemFlags( 
            /* [in] */ ULONG ulFlags) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ClearFileSystemFlags( 
            /* [in] */ ULONG ulFlags) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsVolumeMFVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsVolumeMF * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsVolumeMF * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsVolumeMF * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetFileSystemProperties )( 
            IVdsVolumeMF * This,
            /* [out] */ VDS_FILE_SYSTEM_PROP *pFileSystemProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Format )( 
            IVdsVolumeMF * This,
            /* [in] */ VDS_FILE_SYSTEM_TYPE type,
            /* [string][in] */ LPWSTR pwszLabel,
            /* [in] */ DWORD dwUnitAllocationSize,
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bQuickFormat,
            /* [in] */ BOOL bEnableCompression,
            /* [out] */ IVdsAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AddAccessPath )( 
            IVdsVolumeMF * This,
            /* [string][max_is][in] */ LPWSTR pwszPath);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryAccessPaths )( 
            IVdsVolumeMF * This,
            /* [size_is][size_is][string][out] */ LPWSTR **pwszPathArray,
            /* [out] */ LONG *plNumberOfAccessPaths);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryReparsePoints )( 
            IVdsVolumeMF * This,
            /* [size_is][size_is][out] */ VDS_REPARSE_POINT_PROP **ppReparsePointProps,
            /* [out] */ LONG *plNumberOfReparsePointProps);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *DeleteAccessPath )( 
            IVdsVolumeMF * This,
            /* [string][max_is][in] */ LPWSTR pwszPath,
            /* [in] */ BOOL bForce);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Mount )( 
            IVdsVolumeMF * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Dismount )( 
            IVdsVolumeMF * This,
            /* [in] */ BOOL bForce,
            /* [in] */ BOOL bPermanent);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetFileSystemFlags )( 
            IVdsVolumeMF * This,
            /* [in] */ ULONG ulFlags);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ClearFileSystemFlags )( 
            IVdsVolumeMF * This,
            /* [in] */ ULONG ulFlags);
        
        END_INTERFACE
    } IVdsVolumeMFVtbl;

    interface IVdsVolumeMF
    {
        CONST_VTBL struct IVdsVolumeMFVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsVolumeMF_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsVolumeMF_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsVolumeMF_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsVolumeMF_GetFileSystemProperties(This,pFileSystemProp)	\
    (This)->lpVtbl -> GetFileSystemProperties(This,pFileSystemProp)

#define IVdsVolumeMF_Format(This,type,pwszLabel,dwUnitAllocationSize,bForce,bQuickFormat,bEnableCompression,ppAsync)	\
    (This)->lpVtbl -> Format(This,type,pwszLabel,dwUnitAllocationSize,bForce,bQuickFormat,bEnableCompression,ppAsync)

#define IVdsVolumeMF_AddAccessPath(This,pwszPath)	\
    (This)->lpVtbl -> AddAccessPath(This,pwszPath)

#define IVdsVolumeMF_QueryAccessPaths(This,pwszPathArray,plNumberOfAccessPaths)	\
    (This)->lpVtbl -> QueryAccessPaths(This,pwszPathArray,plNumberOfAccessPaths)

#define IVdsVolumeMF_QueryReparsePoints(This,ppReparsePointProps,plNumberOfReparsePointProps)	\
    (This)->lpVtbl -> QueryReparsePoints(This,ppReparsePointProps,plNumberOfReparsePointProps)

#define IVdsVolumeMF_DeleteAccessPath(This,pwszPath,bForce)	\
    (This)->lpVtbl -> DeleteAccessPath(This,pwszPath,bForce)

#define IVdsVolumeMF_Mount(This)	\
    (This)->lpVtbl -> Mount(This)

#define IVdsVolumeMF_Dismount(This,bForce,bPermanent)	\
    (This)->lpVtbl -> Dismount(This,bForce,bPermanent)

#define IVdsVolumeMF_SetFileSystemFlags(This,ulFlags)	\
    (This)->lpVtbl -> SetFileSystemFlags(This,ulFlags)

#define IVdsVolumeMF_ClearFileSystemFlags(This,ulFlags)	\
    (This)->lpVtbl -> ClearFileSystemFlags(This,ulFlags)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumeMF_GetFileSystemProperties_Proxy( 
    IVdsVolumeMF * This,
    /* [out] */ VDS_FILE_SYSTEM_PROP *pFileSystemProp);


void __RPC_STUB IVdsVolumeMF_GetFileSystemProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumeMF_Format_Proxy( 
    IVdsVolumeMF * This,
    /* [in] */ VDS_FILE_SYSTEM_TYPE type,
    /* [string][in] */ LPWSTR pwszLabel,
    /* [in] */ DWORD dwUnitAllocationSize,
    /* [in] */ BOOL bForce,
    /* [in] */ BOOL bQuickFormat,
    /* [in] */ BOOL bEnableCompression,
    /* [out] */ IVdsAsync **ppAsync);


void __RPC_STUB IVdsVolumeMF_Format_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumeMF_AddAccessPath_Proxy( 
    IVdsVolumeMF * This,
    /* [string][max_is][in] */ LPWSTR pwszPath);


void __RPC_STUB IVdsVolumeMF_AddAccessPath_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumeMF_QueryAccessPaths_Proxy( 
    IVdsVolumeMF * This,
    /* [size_is][size_is][string][out] */ LPWSTR **pwszPathArray,
    /* [out] */ LONG *plNumberOfAccessPaths);


void __RPC_STUB IVdsVolumeMF_QueryAccessPaths_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumeMF_QueryReparsePoints_Proxy( 
    IVdsVolumeMF * This,
    /* [size_is][size_is][out] */ VDS_REPARSE_POINT_PROP **ppReparsePointProps,
    /* [out] */ LONG *plNumberOfReparsePointProps);


void __RPC_STUB IVdsVolumeMF_QueryReparsePoints_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumeMF_DeleteAccessPath_Proxy( 
    IVdsVolumeMF * This,
    /* [string][max_is][in] */ LPWSTR pwszPath,
    /* [in] */ BOOL bForce);


void __RPC_STUB IVdsVolumeMF_DeleteAccessPath_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumeMF_Mount_Proxy( 
    IVdsVolumeMF * This);


void __RPC_STUB IVdsVolumeMF_Mount_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumeMF_Dismount_Proxy( 
    IVdsVolumeMF * This,
    /* [in] */ BOOL bForce,
    /* [in] */ BOOL bPermanent);


void __RPC_STUB IVdsVolumeMF_Dismount_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumeMF_SetFileSystemFlags_Proxy( 
    IVdsVolumeMF * This,
    /* [in] */ ULONG ulFlags);


void __RPC_STUB IVdsVolumeMF_SetFileSystemFlags_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsVolumeMF_ClearFileSystemFlags_Proxy( 
    IVdsVolumeMF * This,
    /* [in] */ ULONG ulFlags);


void __RPC_STUB IVdsVolumeMF_ClearFileSystemFlags_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsVolumeMF_INTERFACE_DEFINED__ */


#ifndef __IVdsDiskPath_INTERFACE_DEFINED__
#define __IVdsDiskPath_INTERFACE_DEFINED__

/* interface IVdsDiskPath */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsDiskPath;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("574a73af-baa8-448a-a764-f457d146d32f")
    IVdsDiskPath : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryPaths( 
            /* [out] */ DWORD *pdwNumberOfPaths,
            /* [size_is][size_is][out] */ VDS_DISK_PATH **ppPathArray) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetPathConfig( 
            /* [string][max_is][in] */ LPWSTR pwszPath,
            /* [string][max_is][in] */ LPWSTR pwszConfig) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsDiskPathVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsDiskPath * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsDiskPath * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsDiskPath * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryPaths )( 
            IVdsDiskPath * This,
            /* [out] */ DWORD *pdwNumberOfPaths,
            /* [size_is][size_is][out] */ VDS_DISK_PATH **ppPathArray);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetPathConfig )( 
            IVdsDiskPath * This,
            /* [string][max_is][in] */ LPWSTR pwszPath,
            /* [string][max_is][in] */ LPWSTR pwszConfig);
        
        END_INTERFACE
    } IVdsDiskPathVtbl;

    interface IVdsDiskPath
    {
        CONST_VTBL struct IVdsDiskPathVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsDiskPath_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsDiskPath_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsDiskPath_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsDiskPath_QueryPaths(This,pdwNumberOfPaths,ppPathArray)	\
    (This)->lpVtbl -> QueryPaths(This,pdwNumberOfPaths,ppPathArray)

#define IVdsDiskPath_SetPathConfig(This,pwszPath,pwszConfig)	\
    (This)->lpVtbl -> SetPathConfig(This,pwszPath,pwszConfig)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDiskPath_QueryPaths_Proxy( 
    IVdsDiskPath * This,
    /* [out] */ DWORD *pdwNumberOfPaths,
    /* [size_is][size_is][out] */ VDS_DISK_PATH **ppPathArray);


void __RPC_STUB IVdsDiskPath_QueryPaths_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDiskPath_SetPathConfig_Proxy( 
    IVdsDiskPath * This,
    /* [string][max_is][in] */ LPWSTR pwszPath,
    /* [string][max_is][in] */ LPWSTR pwszConfig);


void __RPC_STUB IVdsDiskPath_SetPathConfig_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsDiskPath_INTERFACE_DEFINED__ */


#ifndef __IVdsDiskSan_INTERFACE_DEFINED__
#define __IVdsDiskSan_INTERFACE_DEFINED__

/* interface IVdsDiskSan */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IVdsDiskSan;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("2772adb2-4a0a-41db-a213-c4686615206e")
    IVdsDiskSan : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE TakeOwnership( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE FreeOwnership( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVdsDiskSanVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVdsDiskSan * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVdsDiskSan * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVdsDiskSan * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *TakeOwnership )( 
            IVdsDiskSan * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *FreeOwnership )( 
            IVdsDiskSan * This);
        
        END_INTERFACE
    } IVdsDiskSanVtbl;

    interface IVdsDiskSan
    {
        CONST_VTBL struct IVdsDiskSanVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVdsDiskSan_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVdsDiskSan_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVdsDiskSan_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVdsDiskSan_TakeOwnership(This)	\
    (This)->lpVtbl -> TakeOwnership(This)

#define IVdsDiskSan_FreeOwnership(This)	\
    (This)->lpVtbl -> FreeOwnership(This)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDiskSan_TakeOwnership_Proxy( 
    IVdsDiskSan * This);


void __RPC_STUB IVdsDiskSan_TakeOwnership_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVdsDiskSan_FreeOwnership_Proxy( 
    IVdsDiskSan * This);


void __RPC_STUB IVdsDiskSan_FreeOwnership_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVdsDiskSan_INTERFACE_DEFINED__ */


/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif



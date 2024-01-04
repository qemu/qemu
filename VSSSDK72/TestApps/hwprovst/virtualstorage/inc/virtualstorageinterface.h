
/*++

Copyright (c) 2000 Microsoft Corporation

Module Name:

    VirtualStorageInterface.h

Abstract:

    User/Kernel mode interface of the Virtual Bus.

Author:

    Arunvijay Kumar (arunku)

Environment:

    

Notes:

    

Revision History:

    10/23/2001  arunku 

        Created.

--*/
#pragma once

// Allow Zero length arrays
#pragma warning(push)
#pragma warning(disable:4200)


// {97B2CAC0-9E83-45ac-9C87-FBB27E75B7E1}
static const GUID GUID_VIRTUAL_BUS_INTERFACE =
    { 0x97b2cac0, 0x9e83, 0x45ac, {0x9c, 0x87, 0xfb, 0xb2, 0x7e, 0x75, 0xb7, 0xe1} };

// {D322F7C6-584C-4816-BC8A-23C87C1E61EF}
static const GUID GUID_VIRTUAL_DRIVE_INTERFACE = 
{ 0xd322f7c6, 0x584c, 0x4816, { 0xbc, 0x8a, 0x23, 0xc8, 0x7c, 0x1e, 0x61, 0xef } };

static const char VIRTUAL_STORAGE_PRODUCT_ID[] = "VIRTUALSTORAGE";

const ULONG VIRTUAL_BUS_BASE = 0xCC0;
const ULONG VIRTUAL_DRIVE_BASE = 0xDD0;

const ULONG VOLUME_NAME_SIZE = sizeof(L"\\\\?\\Volume{374279c3-1a69-11d6-adb5-806d6172696f}\\");
const ULONG VOLUME_NAME_CHARS = (VOLUME_NAME_SIZE / sizeof(WCHAR));

#define VIRTUAL_BUS_CODE(x)   (VIRTUAL_BUS_BASE + (x))
#define VIRTUAL_DRIVE_CODE(x) (VIRTUAL_DRIVE_BASE + (x))

#define VIRTUAL_BUS_IOCTL(x,access)  (CTL_CODE(FILE_DEVICE_BUS_EXTENDER,(VIRTUAL_BUS_BASE + (x)),METHOD_BUFFERED,(access)))
#define VIRTUAL_DRIVE_IOCTL(x,access)  (CTL_CODE(FILE_DEVICE_DISK,(VIRTUAL_DRIVE_BASE + (x)),METHOD_BUFFERED,(access)))



const ULONG IOCTL_VIRTUAL_BUS_CREATE_DRIVE      = VIRTUAL_BUS_IOCTL(1,FILE_READ_ACCESS|FILE_WRITE_ACCESS);
const ULONG IOCTL_VIRTUAL_BUS_REMOVE_DRIVE      = VIRTUAL_BUS_IOCTL(2,FILE_READ_ACCESS|FILE_WRITE_ACCESS);
const ULONG IOCTL_VIRTUAL_BUS_LIST_DRIVES       = VIRTUAL_BUS_IOCTL(3,FILE_READ_ACCESS);
const ULONG IOCTL_VIRTUAL_BUS_MOUNT             = VIRTUAL_BUS_IOCTL(4,FILE_READ_ACCESS|FILE_WRITE_ACCESS);
const ULONG IOCTL_VIRTUAL_BUS_UNMOUNT           = VIRTUAL_BUS_IOCTL(5,FILE_READ_ACCESS|FILE_WRITE_ACCESS);
const ULONG IOCTL_VIRTUAL_BUS_QUERY_INFORMATION = VIRTUAL_BUS_IOCTL(6,FILE_READ_ACCESS);
const ULONG IOCTL_VIRTUAL_BUS_QUERY_IMAGE       = VIRTUAL_BUS_IOCTL(7,FILE_READ_ACCESS);
const ULONG IOCTL_VIRTUAL_BUS_QUERY_DRIVE_INTERFACE   = VIRTUAL_BUS_IOCTL(8,FILE_READ_ACCESS);
const ULONG IOCTL_VIRTUAL_BUS_SET_IMAGE_SIZE    = VIRTUAL_BUS_IOCTL(9,FILE_READ_ACCESS|FILE_WRITE_ACCESS);

const ULONG IOCTL_VIRTUAL_STORAGE_QUERY_VERSION     = VIRTUAL_BUS_IOCTL(9,FILE_ANY_ACCESS);


const ULONG IOCTL_VIRTUAL_DRIVE_QUERY_ID = VIRTUAL_DRIVE_IOCTL(1,FILE_READ_ACCESS);

const ULONG VIRTUAL_DRIVE_REMOVABLE = 0x01;

//
// Flag values for NEW_VIRTUAL_BUS_CREATE_DRIVE
//
const ULONG VIRTUAL_DRIVE_FLAG_NOWAIT = 0x00001;

typedef enum 
{
    VIRTUAL_NONE = 0,
    VIRTUAL_CDROM,
    VIRTUAL_CDR,
    VIRTUAL_CDRW,
    VIRTUAL_DVDROM,
    VIRTUAL_DVDRAM,
    VIRTUAL_REMOVABLE_DISK,
    VIRTUAL_FIXED_DISK,
    VIRTUAL_INVALID_DEVICE_TYPE = 0xFFFFFFFF
}VIRTUAL_DEVICE_TYPE;
/******************************************************************************/
// Output for IOCTL_VIRTUAL_STORAGE_QUERY_VERSION
typedef struct tagVirtualStorageVersion
{
    ULONG MajorVersion;
    ULONG MinorVersion;
    ULONG Build;
	ULONG QFE;
}
VIRTUAL_STORAGE_VERSION_INFORMATION,*PVIRTUAL_STORAGE_VERSION_INFORMATION;
/******************************************************************************/
// Input for IOCTL_VIRTUAL_BUS_CREATE_DRIVE

// This flag makes virtual drive use
// main memory as the backing store
const ULONG VIRTUAL_DRIVE_USE_MEMORY_STORE = 0x1;
typedef struct tagNewVirtualDriveDescription
{
    ULONG  Length;
    ULONG  Flags;
    VIRTUAL_DEVICE_TYPE  DeviceType;
    GUID    DriveID;
    ULONG   BlockSize;
	ULONG	NumberOfBlocks;
	USHORT  FileNameOffset;  // offset in the buffer
    USHORT  FileNameLength;
	USHORT  StorageDeviceIdDescOffset;
	USHORT  StorageDeviceIdDescLength;
    UCHAR   Buffer[1];
}
NEW_VIRTUAL_DRIVE_DESCRIPTION,
       *PNEW_VIRTUAL_DRIVE_DESCRIPTION;
// Output from IOCTL_VIRTUAL_BUS_CREATE_DRIVE
typedef struct tagVirtualDriveInformation
{
    GUID    DriveID;
    ULONG   Flags;
    VIRTUAL_DEVICE_TYPE DeviceType;
    ULONG   BlockSize;
	ULONG	NumberOfBlocks;
	BOOLEAN MediaInserted;
}
VIRTUAL_DRIVE_INFORMATION,  *PVIRTUAL_DRIVE_INFORMATION;
/******************************************************************************/
const ULONG VIRTUAL_DRIVE_SURPRISE_REMOVE = 0x1;
// Input for IOCTL_VIRTUAL_BUS_REMOVE_DRIVE
typedef struct tagVirtualDriveRemoveParams
{
    ULONG  Length;
    GUID   DriveID;
    ULONG  Flags;
}VIRTUAL_DRIVE_REMOVE_PARAMETERS,*PVIRTUAL_DRIVE_REMOVE_PARAMETERS;
/******************************************************************************/
// Output from IOCTL_VIRTUAL_BUS_LIST_DRIVES
typedef struct tagVirtualBusDrivesList
{
    ULONG Length;
    ULONG NumberOfDrives;
    VIRTUAL_DRIVE_INFORMATION Drives[0];
}VIRTUAL_DRIVES_LIST,*PVIRTUAL_DRIVES_LIST;
/******************************************************************************/
// Input for IOCTL_VIRTUAL_BUS_MOUNT
typedef struct tagVirtualDriveMountParams
{
    ULONG   Length; // Length of this structure
    GUID    DriveID; // ID of the drive to mount this image to
    ULONG   Flags;
    USHORT  FileNameLength;
    WCHAR   FileName[1];
}
VIRTUAL_DRIVE_MOUNT_PARAMETERS,*PVIRTUAL_DRIVE_MOUNT_PARAMETERS;
/******************************************************************************/
// Input for IOCTL_VIRTUAL_BUS_UNMOUNT
typedef struct tagVirtualDriveUnmountParams
{
    ULONG  Length;
    GUID   DriveID;
    ULONG  Flags;
}VIRTUAL_DRIVE_UNMOUNT_PARAMETERS,*PVIRTUAL_DRIVE_UNMOUNT_PARAMETERS;
/******************************************************************************/
// Input for IOCTL_VIRTUAL_BUS_QUERY_INFORMATION
typedef struct tagVirtualDriveQueryInfoParams
{
    ULONG Length;
    GUID  DriveID;
    ULONG Flags;
}VIRTUAL_DRIVE_QUERY_INFORMATION_PARAMETERS,*PVIRTUAL_DRIVE_QUERY_INFORMATION_PARAMETERS;

// Output from IOCTL_VIRTUAL_BUS_QUERY_INFORMATION
typedef struct tagVirtualDriveQueryInfoOut
{
    ULONG Length;
    VIRTUAL_DRIVE_INFORMATION Info;
}VIRTUAL_DRIVE_QUERY_INFORMATION_OUT,*PVIRTUAL_DRIVE_QUERY_INFORMATION_OUT;
/******************************************************************************/
// Input for IOCTL_VIRTUAL_BUS_QUERY_IMAGE
typedef struct tagVirtuaDriveQueryImageParams
{
    ULONG Length;
    GUID  DriveID;
    ULONG Flags;
}VIRTUAL_DRIVE_QUERY_IMAGE_PARAMETERS,*PVIRTUAL_DRIVE_QUERY_IMAGE_PARAMETERS;

// Output from IOCTL_VIRTUAL_BUS_QUERY_IMAGE
typedef struct tagVirtualDriveQueryImageOut
{
    ULONG Length;
    ULONG Flags;
    USHORT FileNameLength;
    WCHAR  FileName[1];
}VIRTUAL_DRIVE_QUERY_IMAGE_OUT,*PVIRTUAL_DRIVE_QUERY_IMAGE_OUT;
/******************************************************************************/
// Input for IOCTL_VIRTUAL_BUS_SET_IMAGE_SIZE
typedef struct tagVirtuaDriveSetImageSizeParams
{
    ULONG Length;
    GUID  DriveID;
    LARGE_INTEGER ImageSize;
}VIRTUAL_DRIVE_SET_IMAGE_SIZE_PARAMETERS,*PVIRTUAL_DRIVE_SET_IMAGE_SIZE_PARAMETERS;
/******************************************************************************/
// Input for IOCTL_VIRTUAL_BUS_QUERY_DRIVE_INTERFACE
typedef struct tagVirtualDriveQueryInterfaceParameters
{
    ULONG Length;
    GUID  DriveID;
    ULONG Flags;
}VIRTUAL_DRIVE_QUERY_INTERFACE_PARAMETERS,*PVIRTUAL_DRIVE_QUERY_INTERFACE_PARAMETERS;
// Output from IOCTL_VIRTUAL_BUS_QUERY_DRIVE_INTERFACE
typedef struct tagVirtualQueryDriveInterfaceOut
{
    ULONG  Length;
    ULONG  Flags;
    USHORT InterfaceNameLength;
    WCHAR  InterfaceName[1];
}
VIRTUAL_DRIVE_QUERY_INTERFACE_OUT,*PVIRTUAL_DRIVE_QUERY_INTERFACE_OUT;
/******************************************************************************/
// Output from IOCTL_VIRTUAL_DRIVE_QUERY_ID
typedef struct tagVirtualDriveQueryIdOut
{
    GUID DriveID;
}VIRTUAL_DRIVE_QUERY_ID_OUT,*PVIRTUAL_DRIVE_QUERY_ID_OUT;
/******************************************************************************/
#pragma warning(pop)





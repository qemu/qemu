
/*++

Copyright (c) 2000 Microsoft Corporation

Module Name:

    VirtualStorageInterface.hpp

Abstract:

    Encapsulates the IOCTLs used to control the Virtual Bus and Drives on the bus

Author:

    Arunvijay Kumar (arunku)

Environment:

    

Notes:

    

Revision History:

    04/23/2002  arunku 

        Created.

--*/
#pragma once
#include "VirtualStorageInterface.h"

namespace VirtualStorage
{

typedef std::vector<std::wstring>  VOLUMES;

/******************************************************************************/
class   VirtualBus
{

  public:
    typedef  std::vector<VIRTUAL_DRIVE_INFORMATION>  DRIVES;
    typedef  std::vector<GUID>   DRIVEGUIDS;
    typedef struct
    {
        ULONG  DeviceType;
        ULONG  DeviceNumber;
    }STORAGE_INFORMATION;

    typedef std::vector<STORAGE_INFORMATION>  STORAGE_INFORMATION_LIST;
    
    VirtualBus();
    ~VirtualBus();

    bool  IsValid();

    HRESULT 
	QueryVersion(
		OUT VIRTUAL_STORAGE_VERSION_INFORMATION&);
        
   
    HRESULT 
	CreateDrive(
    	IN VIRTUAL_DEVICE_TYPE DeviceType,
        IN USHORT usBlockSize,
        OUT VIRTUAL_DRIVE_INFORMATION & infoDrive);
	
    HRESULT 
	CreateDrive(
		IN VIRTUAL_DEVICE_TYPE DeviceType,
        IN USHORT usBlockSize,
        IN const std::wstring& strImage,
        OUT VIRTUAL_DRIVE_INFORMATION & infoDrive);

	HRESULT 
	CreateRamDrive(
    	IN VIRTUAL_DEVICE_TYPE DeviceType,
        IN USHORT usBlockSize,
        IN ULONG  ulMaxBlocks,
        OUT VIRTUAL_DRIVE_INFORMATION & infoDrive);
	
    HRESULT 
	CreateDriveEx(
    	IN PNEW_VIRTUAL_DRIVE_DESCRIPTION  pDriveDesc,
    	OUT VIRTUAL_DRIVE_INFORMATION&  infoDrive);
    

    HRESULT 
	OpenDrive(
		IN const GUID& guidDrive,
		OUT HANDLE& hDrive);
    
    HRESULT 
	RemoveDrive(
		IN const GUID& guidDrive,
		IN bool bSurprise = false);
	
    HRESULT 
	Mount(
		IN const GUID& guidDrive,
		IN const std::wstring & strImage);
	
    HRESULT	
	Remount(
		IN const GUID& guidDrive);
	
    HRESULT 
	Eject(
		IN const GUID& guidDrive);
	
    HRESULT 
	SetSize(
		IN const GUID& guidDrive,
		IN ULONGLONG newSize);

	HRESULT 
	ListDrives(
		IN DRIVES& vecDrives);

    static HRESULT   
	QueryVirtualDriveID(
		IN HANDLE hDrive,
		OUT GUID& idDrive);

    HRESULT 
	QueryInformation(
		IN const GUID& guidDrive,
		OUT VIRTUAL_DRIVE_INFORMATION& infoDrive);
	
    HRESULT 
	QueryMountedImage(
		IN const GUID& guidDrive,
		OUT std::wstring& strImage);
	
	
    HRESULT 
	QueryDriveInterface(
		IN const GUID& guidDrive,
		OUT std::wstring& strInterface);
    
    HRESULT 
	QueryStorageInformation(
		IN const GUID& guidDrive,
		OUT STORAGE_INFORMATION& infoStorage);
	
    HRESULT 
	QueryStorageInformation(
		IN std::wstring strInterface,
		OUT STORAGE_INFORMATION& infoStorage);
	
    HRESULT 
	QueryStorageInformation(
		IN HANDLE hDrive,
		OUT STORAGE_INFORMATION& infoStorage);

	HRESULT 
	QueryVolumesOnDrive(
		IN const GUID& guidDrive,
		OUT VOLUMES& volumes);
	
    HRESULT 
	QueryVolumesOnDrive(
		IN const STORAGE_INFORMATION& infoStorage,
		OUT VOLUMES& volumes);
	
    HRESULT 
	QueryDrivesUsedByVolume(
		IN HANDLE hVolume,
		OUT STORAGE_INFORMATION_LIST& drives);
    
    static HRESULT    
	InstallDriver(
		IN const std::wstring & strInfFilePath,
		OUT bool& bReboot);
	
    static HRESULT    UnInstallDriver();
    static bool    Installed() 
	{
		return (FindExistingDevice( HARDWARE_ID )); 
	}
  private:
    static const wchar_t* HARDWARE_ID;
    
    static bool    
	FindExistingDevice(
		IN const std::wstring & strHardwareID);
    static HRESULT 
	RemoveExistingDevice(
		IN const std::wstring & strHardwareID);

	static HRESULT 
	InstallRootEnumeratedDriver(
		IN const std::wstring & strHardwareId,
        IN const std::wstring & strINFFile,
        OUT PBOOL pbRebootRequired = NULL);

	HRESULT Open();
    void    Close();
    HANDLE  m_hBus;
};
}


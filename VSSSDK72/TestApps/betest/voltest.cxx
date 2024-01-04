#include "stdafx.hxx"
#include "vss.h"
#include "vswriter.h"
#include "vsbackup.h"
#include <debug.h>
#include <time.h>

#include <vs_inc.hxx>
#include <vsevent.h>
#include <vscoordint.h>
#include <vdslun.h>
#include <vs_wmxml.hxx>
#include <vs_cmxml.hxx>
#include <vs_trace.hxx>

void PrintVolumeInfo(LPWSTR wszVolume)
	{
	wprintf(L"\n\nInformation for volume %s\n\n", wszVolume);
	// get rid of last backslash
	wszVolume[wcslen(wszVolume)-1] = L'\0';

	HANDLE hVol = CreateFile
					(
					wszVolume,
					GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					FILE_ATTRIBUTE_NORMAL,
					NULL
					);

    if (hVol == INVALID_HANDLE_VALUE)
		{
		DWORD dwErr = GetLastError();
		wprintf(L"CreateFile of volume failed with error %d.\n", dwErr);
		return;
		}

	WCHAR bufExtents[1024];
	DWORD size;
	BOOL b = DeviceIoControl
			(
			hVol,
			IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
			NULL,
			0,
			(PVOID) bufExtents,
			sizeof(bufExtents),
			&size,
			NULL
			);

    if (!b)
		{
		DWORD dwErr = GetLastError();
		wprintf(L"IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed with error %d.\n", dwErr);
		CloseHandle(hVol);
		return;
		}

	VOLUME_DISK_EXTENTS *pDiskExtents = (VOLUME_DISK_EXTENTS *) bufExtents;

	ULONG PrevDiskNo = 0xffffffff;
	printf("# of extents = %d\n\n", pDiskExtents->NumberOfDiskExtents);

	for(UINT iExtent = 0; iExtent < pDiskExtents->NumberOfDiskExtents; iExtent++)
		{
		ULONG DiskNo = pDiskExtents->Extents[iExtent].DiskNumber;
		printf("Extent %d:\nDisk %d, Low=0x%I64lx, Length=0x%I64lx\n\n",
			   iExtent,
		       DiskNo,
               pDiskExtents->Extents[iExtent].StartingOffset,
               pDiskExtents->Extents[iExtent].ExtentLength
			   );

		if (DiskNo != PrevDiskNo)
			{
			PrevDiskNo = DiskNo;
			WCHAR wszbuf[32];
			swprintf(wszbuf, L"\\\\.\\PHYSICALDRIVE%u", DiskNo);
			HANDLE hDisk = CreateFile
								(
								wszbuf,
								GENERIC_READ|GENERIC_WRITE,
								FILE_SHARE_READ,
								NULL,
								OPEN_EXISTING,
								0,
								NULL
								);

            if (hDisk == INVALID_HANDLE_VALUE)
				{
				DWORD dwErr = GetLastError();
				wprintf(L"Cannot open disk %d due to error %d.  Skipping\n", DiskNo, dwErr);
				continue;
				}

			STORAGE_PROPERTY_QUERY query;
			query.PropertyId = StorageDeviceProperty;
			query.QueryType = PropertyStandardQuery;
			BYTE buf[1024];
			DWORD dwSize;

			if (!DeviceIoControl
						(
						hDisk,
						IOCTL_STORAGE_QUERY_PROPERTY,
						&query,
						sizeof(query),
						buf,
						1024,
						&dwSize,
						NULL
						))
                {
				DWORD dwErr = GetLastError();
				if (dwErr != ERROR_NOT_SUPPORTED)
					wprintf(L"IOCTL_STORAGE_QUERY_PROPERTY failed due to error %d.\n", dwErr);

				CloseHandle(hDisk);
				continue;
				}

			STORAGE_DEVICE_DESCRIPTOR *pDesc = (STORAGE_DEVICE_DESCRIPTOR *) buf;
			printf("Information for disk %d.\n\nbus=", DiskNo);

			switch (pDesc->BusType)
                {
				default:
					printf("(other)\n");
					break;
					

				case BusTypeScsi:
					printf("(SCSI)\n");
					break;

				case BusTypeAtapi:
					printf("(ATAPI)\n");
					break;

                case BusTypeAta:
					printf("(ATA)\n");
					break;

				case BusType1394:
					printf("(1394)\n");
					break;

				case BusTypeSsa:
					printf("(SSA)\n");
					break;

				case BusTypeFibre:
					printf("(Fibre)\n");
					break;

				case BusTypeUsb:
					printf("(Usb)\n");
					break;

				case BusTypeRAID:
					printf("(RAID)\n");
					break;
                }

            if (pDesc->VendorIdOffset)
				{
                LPSTR szVendor = (LPSTR)((BYTE *) pDesc + pDesc->VendorIdOffset);
				printf("VendorId: %s\n", szVendor);
                }

			if (pDesc->ProductIdOffset)
                {
				LPSTR szProduct = (LPSTR)((BYTE *) pDesc + pDesc->ProductIdOffset);
				printf("ProductId: %s\n", szProduct);
                }
			if (pDesc->ProductRevisionOffset)
                {
				LPSTR szRevision = (LPSTR)((BYTE *) pDesc + pDesc->ProductRevisionOffset);
				printf("RevisionId: %s\n", szRevision);
                }

			if (pDesc->SerialNumberOffset)
				{
				LPSTR szSerialNo = (LPSTR)((BYTE *) pDesc + pDesc->SerialNumberOffset);
				printf("Serial#: %s\n", szSerialNo);
				}

			query.PropertyId = StorageDeviceIdProperty;
			query.QueryType = PropertyStandardQuery;

			if (!DeviceIoControl
						(
						hDisk,
						IOCTL_STORAGE_QUERY_PROPERTY,
						&query,
						sizeof(query),
						buf,
						1024,
						&dwSize,
						NULL
						))
                {
				DWORD dwErr = GetLastError();
			    if (dwErr != ERROR_NOT_SUPPORTED)
					wprintf(L"IOCTL_STORAGE_QUERY_PROPERTY failed due to error %d.\n", dwErr);

				CloseHandle(hDisk);
				continue;
				}

			STORAGE_DEVICE_ID_DESCRIPTOR *pDevId = (STORAGE_DEVICE_ID_DESCRIPTOR *) buf;
			printf("# of identifiers = %d\n", pDevId->NumberOfIdentifiers);

			STORAGE_IDENTIFIER *pId = (STORAGE_IDENTIFIER *) pDevId->Identifiers;
			for(UINT i = 0; i < pDevId->NumberOfIdentifiers; i++)
				{
				switch(pId->Type)
					{
					default:
						printf("(other) ");
						break;

                    case StorageIdTypeVendorSpecific:
						printf("(vendor specific) ");
						break;

                    case StorageIdTypeVendorId:
						printf("(vendor id) ");
						break;

                    case StorageIdTypeEUI64:
						printf("(EUI64) ");
						break;

                    case StorageIdTypeFCPHName:
						printf("(FCPHName) ");
						break;
                    }

				if (pId->CodeSet == StorageIdCodeSetAscii)
					printf("%s\n", pId->Identifier);
				else
					{
					for(UINT i = 0; i < pId->IdentifierSize; i++)
						{
						printf("%2x ", pId->Identifier[i]);
						if ((i % 16) == 0 && i > 0)
							printf("\n");
						}
					}

				pId = (STORAGE_IDENTIFIER *) ((BYTE *) pId + pId->NextOffset);
				}
				

			CloseHandle(hDisk);
			}
		}
	}



				



void EnumVolumes()
	{
	CVssFunctionTracer ft(VSSDBG_XML, L"EnumVolumes");

	HANDLE h = INVALID_HANDLE_VALUE;
	try
		{
		WCHAR volName[1024];

		h = FindFirstVolume(volName, sizeof(volName)/sizeof(WCHAR));
		if (h == INVALID_HANDLE_VALUE)
			{
			DWORD dwErr = GetLastError();
			Error(E_UNEXPECTED, L"FindFirstVolume failed due to error %d.\n", dwErr);
			}

		while(TRUE)
			{
			PrintVolumeInfo(volName);
			if (!FindNextVolume(h, volName, sizeof(volName)/ sizeof(WCHAR)))
				{
				DWORD dwErr = GetLastError();
				if (dwErr == ERROR_NO_MORE_FILES)
					break;
				else
					Error(E_UNEXPECTED, L"Unexpected error %d from FindNextVolume.\n", dwErr);
				}
			}

		if (!FindVolumeClose(h))
			{
			DWORD dwErr = GetLastError();
			Error(E_UNEXPECTED, L"Cannot close volume handle due to error %d.\n", dwErr);
			}

		h = INVALID_HANDLE_VALUE;
		}
	VSS_STANDARD_CATCH(ft)

	if (h != INVALID_HANDLE_VALUE)
		FindClose(h);
	}




#include "stdafx.hxx"
#include "vss.h"
#include "vswriter.h"
#include "vsbackup.h"
#include <debug.h>
#include <time.h>
#include <ntddstor.h>

#include <vs_inc.hxx>
#include <vsevent.h>
#include <vdslun.h>
#include <vscoordint.h>
#include <vs_wmxml.hxx>
#include <vs_cmxml.hxx>
#include <vs_trace.hxx>




static VSS_ID x_idSnapSet =
	{
	0xD79FE5AD, 0x767F, 0x4251,
	0xA9, 0x7A, 0x37, 0x37, 0xd0, 0xf9, 0xf7, 0x4f
	};



static VSS_ID x_idSnap1 =
	{
    0x78B049FB, 0x9D12, 0x40A6,
	0x82, 0x6C, 0xED, 0x8A, 0x80, 0x4E, 0xB4, 0xAA
	};

static VSS_ID x_idSnap2 =
	{
	0xE700B0EC, 0xA993, 0x4B1B,
	0xAD, 0xDA, 0xC2, 0xAA, 0x08, 0x53, 0x6F, 0x27
	};

static VSS_ID x_idProv =
	{
	0x587E6660, 0x3FEF, 0x45D6,
	0x8D, 0x91, 0xB1, 0x2E, 0x16, 0xAC, 0x5C, 0x18
	};

void GetAndValidateSnapshots
	(
	IN IVssSnapshotSetDescription *pSnapshotSet,
	OUT IVssSnapshotDescription **ppSnapshot1,
	OUT IVssSnapshotDescription **ppSnapshot2,
	OUT VSS_ID &idSnap1,
	OUT VSS_ID &idSnap2
	)
	{
	HRESULT hr;

	UINT cSnapshots;
	CHECK_SUCCESS(pSnapshotSet->GetSnapshotCount(&cSnapshots));
	if (cSnapshots != 2)
		{
		wprintf(L"Number of snapshots %d is not correct.\n");
		BS_ASSERT(FALSE);
		throw(E_UNEXPECTED);
		}

	VSS_ID idProv;
	CHECK_SUCCESS(pSnapshotSet->GetSnapshotDescription(0, ppSnapshot1));
	CHECK_SUCCESS(pSnapshotSet->GetSnapshotDescription(1, ppSnapshot2));
	CHECK_SUCCESS((*ppSnapshot1)->GetSnapshotId(&idSnap1));
	CHECK_SUCCESS((*ppSnapshot2)->GetSnapshotId(&idSnap2));
	if ((idSnap1 != x_idSnap1 || idSnap2 != x_idSnap2) &&
		(idSnap1 != x_idSnap2 || idSnap2 != x_idSnap1))
		Error(E_UNEXPECTED,
			  L"Snapshots were not added properly.  Added snapshots are:\n"
			  WSTR_GUID_FMT L" and " WSTR_GUID_FMT
			  L"\nFound snapshots are" WSTR_GUID_FMT L" and "
			  WSTR_GUID_FMT L"\n",
			  GUID_PRINTF_ARG(x_idSnap1),
			  GUID_PRINTF_ARG(x_idSnap2),
			  GUID_PRINTF_ARG(idSnap1),
			  GUID_PRINTF_ARG(idSnap2));


	CHECK_SUCCESS((*ppSnapshot1)->GetProviderId(&idProv));
	if (idProv != x_idProv)
		Error(E_UNEXPECTED,
			  L"Provider id was not correct.\n"
			  WSTR_GUID_FMT L" != " WSTR_GUID_FMT L".\n",
			  GUID_PRINTF_ARG(idProv),
			  GUID_PRINTF_ARG(x_idProv));


	CHECK_SUCCESS((*ppSnapshot2)->GetProviderId(&idProv));
	if (idProv != x_idProv)
		Error(E_UNEXPECTED,
			  L"Provider id was not correct.\n"
			  WSTR_GUID_FMT L" != " WSTR_GUID_FMT L".\n",
			  GUID_PRINTF_ARG(idProv),
			  GUID_PRINTF_ARG(x_idProv));

	}


void ValidateSnapshotSet(IVssSnapshotSetDescription *pSnapshotSet)
	{
	HRESULT hr;
	VSS_ID idSnapSet;

	CHECK_SUCCESS(pSnapshotSet->GetSnapshotSetId(&idSnapSet));
	if (idSnapSet != x_idSnapSet)
		Error(E_UNEXPECTED, L"snapshot set id does not match: "
			  WSTR_GUID_FMT L" != " WSTR_GUID_FMT,
			  GUID_PRINTF_ARG(idSnapSet),
			  GUID_PRINTF_ARG(x_idSnapSet));


	CComBSTR bstrDescription;
	CComBSTR bstrMetadata;
	CHECK_SUCCESS(pSnapshotSet->GetDescription(&bstrDescription));
	if (wcscmp(bstrDescription, L"This is a test snapshot set") != 0)
		Error(E_UNEXPECTED, L"Snapshot description is invalid:\n%s", bstrDescription);

	CHECK_SUCCESS(pSnapshotSet->GetMetadata(&bstrMetadata));
	if (wcscmp(bstrMetadata, L"This is some test metadata for the snapshot set.") != 0)
		Error(E_UNEXPECTED, L"Snapshot metadata is invalid:\n%s", bstrMetadata);

	LONG lContext;
	CHECK_SUCCESS(pSnapshotSet->GetContext(&lContext));
	if (lContext != VSS_CTX_BACKUP)
		Error(E_UNEXPECTED, L"Context is invalid. lContext=%d\n", lContext);
	}

VSS_SNAPSHOT_PROP rgSnapshotProp[2];

static UCHAR x_DeviceType1 = 1;
static UCHAR x_DeviceType2 = 2;

static UCHAR x_DeviceTypeModifier1 = 100;
static UCHAR x_DeviceTypeModifier2 = 200;

static ULONGLONG x_rgDiskExtents1[] = {10L, 2000L, 4000L, 1000L};
static UINT x_cDiskExtents1 = 2;

static ULONGLONG x_rgDiskExtents2[] = {100L, 1000L, 2000L, 10000L, 100000L, 4000L};
static UINT x_cDiskExtents2 = 3;

static LPCSTR x_szVendorId1 = "MICROSOFT";
static LPCSTR x_szVendorId2 = "PLATFORMS";

static LPCSTR x_szProductId1 = "LVM";
static LPCSTR x_szProductId2 = "VDS";

static LPCSTR x_szProductRevision1 = "1.0";
static LPCSTR x_szProductRevision2 = "2.1";

static LPCSTR x_szSerialNumber1S = "123987";
static LPCSTR x_szSerialNumber1D = "343434";

static LPCSTR x_szSerialNumber2S = "999999-1111";
static LPCSTR x_szSerialNumber2D = "888888-2222";


static VDS_STORAGE_BUS_TYPE x_busType1 = VDSBusTypeScsi;
static VDS_STORAGE_BUS_TYPE x_busType2 = VDSBusTypeFibre;



static VSS_ID x_idDiskSignature1 =
	{
    0xF1CFF9EC, 0xB0A3, 0x408C,
	0xB5, 0xC9, 0x0C, 0x98, 0xDF, 0xFD, 0xDA, 0xED
	};

static VSS_ID x_idDiskSignature2 =
	{
    0xB33FF922, 0xB0A3, 0x408C,
	0xB5, 0xC9, 0x0C, 0x98, 0xDF, 0xFD, 0xDA, 0xED
	};

static VDS_INTERCONNECT_ADDRESS_TYPE x_rgIAType1S[] = {VDS_IA_FCFS, VDS_IA_FCFS, VDS_IA_FCFS};
static LPCSTR x_rgszAddresses1S[] = {"CAB1.BUS10.SLOT10", "CAB1.BUS20.SLOT30", "CAB20.BUS3.SLOT100"};
static UINT x_cInterconnectAddresses1S = 3;

static VDS_INTERCONNECT_ADDRESS_TYPE x_rgIAType1D[] = {VDS_IA_FCFS, VDS_IA_FCFS, VDS_IA_FCFS};
static LPCSTR x_rgszAddresses1D[] = {"CAB1.BUS1.SLOT10", "CAB1.BUS2.SLOT30", "CAB2.BUS3.SLOT10"};
static UINT x_cInterconnectAddresses1D = 3;


static VDS_INTERCONNECT_ADDRESS_TYPE rgIAType2S[] = {VDS_IA_FCPH};
static LPCSTR x_rgszAddresses2S[] = {"10.1.1.4.BUS1.SLOT10"};
static UINT x_cInterconnectAddresses2S = 1;

static VDS_INTERCONNECT_ADDRESS_TYPE rgIAType2D[] = {VDS_IA_FCPH};
static LPCSTR x_rgszAddresses2D[] = {"100.2.3.8.BUS11.SLOT10"};
static UINT x_cInterconnectAddresses2D = 1;

static STORAGE_IDENTIFIER x_storeid1 = {StorageIdCodeSetBinary, StorageIdTypeVendorId, 8, 0, StorageIdAssocDevice, 0};
static STORAGE_IDENTIFIER x_storeid2 = {StorageIdCodeSetBinary, StorageIdTypeVendorSpecific, 20, 0, StorageIdAssocDevice, 0};
static STORAGE_IDENTIFIER x_storeid3 = {StorageIdCodeSetAscii, StorageIdTypeFCPHName, 32, 0, StorageIdAssocDevice, 0};
static STORAGE_IDENTIFIER x_storeid4 = {StorageIdCodeSetBinary, StorageIdTypeEUI64, 8, 0, StorageIdAssocDevice, 0};

void AddIdentifier(BYTE **ppb, STORAGE_IDENTIFIER *pid, UINT &ib)
	{
	if (pid)
		{
		UINT cb = pid->IdentifierSize + FIELD_OFFSET(STORAGE_IDENTIFIER, Identifier);

		memcpy(*ppb, pid, FIELD_OFFSET(STORAGE_IDENTIFIER, Identifier));
		ib += cb;
		((STORAGE_IDENTIFIER *) (*ppb))->NextOffset = (USHORT) ib;
		memset(*ppb + FIELD_OFFSET(STORAGE_IDENTIFIER, Identifier), 0x10, pid->IdentifierSize);
		*ppb += cb;
		}
	}

void BuildStorageIdDescriptor
	(
	STORAGE_DEVICE_ID_DESCRIPTOR **ppstore,
	STORAGE_IDENTIFIER *pid1,
	STORAGE_IDENTIFIER *pid2,
	STORAGE_IDENTIFIER *pid3,
	STORAGE_IDENTIFIER *pid4
	)
	{
	UINT cid = 0;
	UINT cb = FIELD_OFFSET(STORAGE_DEVICE_ID_DESCRIPTOR, Identifiers);
	if (pid1)
		{
		cid++;
		cb += pid1->IdentifierSize + FIELD_OFFSET(STORAGE_IDENTIFIER, Identifier);
		}

	if (pid2)
		{
		cid++;
		cb += pid2->IdentifierSize + FIELD_OFFSET(STORAGE_IDENTIFIER, Identifier);
		}

	if (pid3)
		{
		cid++;
		cb += pid3->IdentifierSize + FIELD_OFFSET(STORAGE_IDENTIFIER, Identifier);
		}

	if (pid4)
		{
		cid++;
		cb += pid4->IdentifierSize + FIELD_OFFSET(STORAGE_IDENTIFIER, Identifier);
		}

	BYTE *pb = new BYTE[cb];
	STORAGE_DEVICE_ID_DESCRIPTOR *pstore = (STORAGE_DEVICE_ID_DESCRIPTOR *) pb;
	pstore->Version = 10;
	pstore->Size = cb;
	pstore->NumberOfIdentifiers = cid;
	pb = pstore->Identifiers;
	UINT ib = FIELD_OFFSET(STORAGE_DEVICE_ID_DESCRIPTOR, Identifiers);

	AddIdentifier(&pb, pid1, ib);
	AddIdentifier(&pb, pid2, ib);
	AddIdentifier(&pb, pid3, ib);
	AddIdentifier(&pb, pid4, ib);

	BS_ASSERT(ib == cb);
	*ppstore = pstore;
	}

typedef struct _BETEST_LUN_INFO
	{
	UCHAR DeviceType;
	UCHAR DeviceTypeModifier;
	ULONGLONG *rgDiskExtents;
	UINT cDiskExtents;
	LPCSTR szVendorId;
	LPCSTR szProductId;
	LPCSTR szProductRevision;
	LPCSTR szSerialNumberSource;
	LPCSTR szSerialNumberDest;
	VDS_STORAGE_BUS_TYPE busType;
	VSS_ID idDiskSignature;
	VDS_INTERCONNECT_ADDRESS_TYPE *rgiatypeS;
	VDS_INTERCONNECT_ADDRESS_TYPE *rgiatypeD;
	LPCSTR	*rgszIAS;
	LPCSTR *rgszIAD;
	UINT cIAS;
	UINT cIAD;
	STORAGE_DEVICE_ID_DESCRIPTOR *pstoreS;
	STORAGE_DEVICE_ID_DESCRIPTOR *pstoreD;
	} BETEST_LUN_INFO;


void BuildLunInfo (UINT i, BETEST_LUN_INFO &info)
	{
	if (i == 1)
		{
		info.DeviceType = x_DeviceType1;
		info.DeviceTypeModifier = x_DeviceTypeModifier1;
		info.rgDiskExtents = x_rgDiskExtents1;
		info.cDiskExtents = x_cDiskExtents1;
		info.szVendorId = x_szVendorId1;
		info.szProductId = x_szProductId1;
		info.szProductRevision = x_szProductRevision1;
		info.szSerialNumberSource = x_szSerialNumber1S;
		info.szSerialNumberDest = x_szSerialNumber1D;
		info.busType = x_busType1;
		info.idDiskSignature = x_idDiskSignature1;
		info.rgiatypeS = x_rgIAType1S;
		info.rgiatypeD = x_rgIAType1D;
		info.rgszIAS = x_rgszAddresses1S;
		info.rgszIAD = x_rgszAddresses1D;
		info.cIAS = x_cInterconnectAddresses1S;
		info.cIAD = x_cInterconnectAddresses1D;
		BuildStorageIdDescriptor(&info.pstoreS, &x_storeid1, &x_storeid3, NULL, NULL);
		BuildStorageIdDescriptor(&info.pstoreD, &x_storeid1, &x_storeid3, &x_storeid4, NULL);
		}
	else
		{
		info.DeviceType = x_DeviceType2;
		info.DeviceTypeModifier = x_DeviceTypeModifier2;
		info.rgDiskExtents = x_rgDiskExtents2;
		info.cDiskExtents = x_cDiskExtents2;
		info.szVendorId = x_szVendorId2;
		info.szProductId = x_szProductId2;
		info.szProductRevision = x_szProductRevision2;
		info.szSerialNumberSource = x_szSerialNumber2S;
		info.szSerialNumberDest = x_szSerialNumber2D;
		info.busType = x_busType2;
		info.idDiskSignature = x_idDiskSignature2;
		info.rgiatypeS = rgIAType2S;
		info.rgiatypeD = rgIAType2D;
		info.rgszIAS = x_rgszAddresses2S;
		info.rgszIAD = x_rgszAddresses2D;
		info.cIAS = x_cInterconnectAddresses2S;
		info.cIAD = x_cInterconnectAddresses2D;
		BuildStorageIdDescriptor(&info.pstoreS, &x_storeid1, &x_storeid2, NULL, NULL);
		BuildStorageIdDescriptor(&info.pstoreD, &x_storeid1, &x_storeid2, &x_storeid3, &x_storeid4);
		}
	}


void AddLunInfo(IVssLunMapping *pLunMapping, UINT i)
	{
	HRESULT hr;

	CComPtr<IVssLunInformation> pSourceLun;
	CComPtr<IVssLunInformation> pDestLun;
	BETEST_LUN_INFO info;

	BuildLunInfo(i, info);

	CHECK_SUCCESS(pLunMapping->GetSourceLun(&pSourceLun));
	CHECK_SUCCESS(pLunMapping->GetDestinationLun(&pDestLun));

	CHECK_SUCCESS(pSourceLun->SetLunBasicType
					  (
					  info.DeviceType,
					  info.DeviceTypeModifier,
					  true,
					  info.szVendorId,
					  info.szProductId,
					  info.szProductRevision,
					  info.szSerialNumberSource,
					  info.busType
					  ));

	CHECK_SUCCESS(pDestLun->SetLunBasicType
					  (
					  info.DeviceType,
					  info.DeviceTypeModifier,
					  true,
					  info.szVendorId,
					  info.szProductId,
					  info.szProductRevision,
					  info.szSerialNumberDest,
					  info.busType
					  ));

	CHECK_SUCCESS(pSourceLun->SetDiskSignature(info.idDiskSignature));
	CHECK_SUCCESS(pDestLun->SetDiskSignature(info.idDiskSignature));
	for(UINT iExtent = 0; iExtent < info.cDiskExtents; iExtent++)
		CHECK_SUCCESS(pLunMapping->AddDiskExtent
						(
						info.rgDiskExtents[iExtent * 2],
						info.rgDiskExtents[iExtent * 2 + 1]
						));

	for(UINT iIAS = 0; iIAS < info.cIAS; iIAS++)
		CHECK_SUCCESS(pSourceLun->AddInterconnectAddress
							(
							info.rgiatypeS[iIAS],
							0,
							NULL,
							(UINT) (strlen(info.rgszIAS[iIAS]) + 1),
							(const BYTE *) info.rgszIAS[iIAS]
							));

	for(UINT iIAD = 0; iIAD < info.cIAD; iIAD++)
		CHECK_SUCCESS(pDestLun->AddInterconnectAddress
							(
							info.rgiatypeD[iIAD],
							3,
							(BYTE *) "foo",
							(UINT) (strlen(info.rgszIAD[iIAD]) + 1),
							(const BYTE *) info.rgszIAD[iIAD]
							));

	CHECK_SUCCESS(pSourceLun->SetStorageDeviceIdDescriptor(info.pstoreS));
	CHECK_SUCCESS(pDestLun->SetStorageDeviceIdDescriptor(info.pstoreD));
    }

bool cmp_str_eq(LPCSTR sz1, LPCSTR sz2)
	{
	return (sz1 == NULL && sz2 == NULL) ||
	       (sz1 != NULL && sz2 != NULL && strcmp(sz1, sz2) == 0);
	}

void DoCoTaskFree
	(
	LPSTR &str1,
	LPSTR &str2,
	LPSTR &str3,
	LPSTR &str4
	)
	{
	if (str1)
		{
		CoTaskMemFree(str1);
		str1 = NULL;
		}

	if (str2)
		{
		CoTaskMemFree(str2);
		str2 = NULL;
		}

	if (str3)
		{
		CoTaskMemFree(str3);
		str3 = NULL;
		}

	if (str4)
		{
		CoTaskMemFree(str4);
		str4 = NULL;
		}
	}

void ValidateLunInfo(IVssLunMapping *pLunMapping, UINT i)
	{
	HRESULT hr;

	CComPtr<IVssLunInformation> pSourceLun;
	CComPtr<IVssLunInformation> pDestLun;
	BETEST_LUN_INFO info;

	BuildLunInfo(i, info);

	CHECK_SUCCESS(pLunMapping->GetSourceLun(&pSourceLun));
	CHECK_SUCCESS(pLunMapping->GetDestinationLun(&pDestLun));

	UINT cExtents;
	CHECK_SUCCESS(pLunMapping->GetDiskExtentCount(&cExtents));
	if (cExtents != info.cDiskExtents)
		Error(E_UNEXPECTED, L"Invalid number of extents for lun %d", i);

	for(UINT iExtent = 0; iExtent < cExtents; iExtent++)
		{
		ULONGLONG start, length;
		CHECK_SUCCESS(pLunMapping->GetDiskExtent(iExtent, &start, &length));
		if (start != info.rgDiskExtents[iExtent * 2] ||
			length != info.rgDiskExtents[iExtent * 2 + 1])
			Error(E_UNEXPECTED, L"Invalid extent %d for lun %d", iExtent, i);
		}

	LPSTR strVendorId;
	LPSTR strProductId;
	LPSTR strProductRevision;
	LPSTR strSerialNumber;
	VDS_STORAGE_BUS_TYPE busTypeFound;
	UCHAR DeviceTypeFound;
	UCHAR DeviceTypeModifierFound;
	BOOL bCommandQueueing;

	CHECK_SUCCESS(pSourceLun->GetLunBasicType
					  (
					  &DeviceTypeFound,
					  &DeviceTypeModifierFound,
					  &bCommandQueueing,
					  &strVendorId,
					  &strProductId,
					  &strProductRevision,
					  &strSerialNumber,
					  &busTypeFound
					  ));

    if (DeviceTypeFound != info.DeviceType ||
		DeviceTypeModifierFound != info.DeviceTypeModifier ||
		busTypeFound != info.busType ||
		!bCommandQueueing ||
		!cmp_str_eq(strVendorId, info.szVendorId) ||
		!cmp_str_eq(strProductId, info.szProductId) ||
		!cmp_str_eq(strProductRevision, info.szProductRevision) ||
		!cmp_str_eq(strSerialNumber, info.szSerialNumberSource))
		{
		DoCoTaskFree(strVendorId, strProductId, strProductRevision, strSerialNumber);
		Error(E_UNEXPECTED, L"Problem in basic LUN information for source %d.\n", i);
		}

	DoCoTaskFree(strVendorId, strProductId, strProductRevision, strSerialNumber);

	CHECK_SUCCESS(pDestLun->GetLunBasicType
					  (
					  &DeviceTypeFound,
					  &DeviceTypeModifierFound,
					  &bCommandQueueing,
					  &strVendorId,
					  &strProductId,
					  &strProductRevision,
					  &strSerialNumber,
					  &busTypeFound
					  ));

    if (DeviceTypeFound != info.DeviceType ||
		DeviceTypeModifierFound != info.DeviceTypeModifier ||
		busTypeFound != info.busType ||
		!cmp_str_eq(strVendorId, info.szVendorId) ||
		!cmp_str_eq(strProductId, info.szProductId)  ||
		!cmp_str_eq(strProductRevision, info.szProductRevision) ||
		!cmp_str_eq(strSerialNumber, info.szSerialNumberDest))
		{
		DoCoTaskFree(strVendorId, strProductId, strProductRevision, strSerialNumber);
		Error(E_UNEXPECTED, L"Problem in basic LUN information four destination %d.\n", i);
		}

	DoCoTaskFree(strVendorId, strProductId, strProductRevision, strSerialNumber);

	VSS_ID idDiskSignatureFound;

	CHECK_SUCCESS(pSourceLun->GetDiskSignature(&idDiskSignatureFound));
	if (info.idDiskSignature != idDiskSignatureFound)
		Error(E_UNEXPECTED, L"Disk signatures do not match for source %d.\n", i);

	CHECK_SUCCESS(pDestLun->GetDiskSignature(&idDiskSignatureFound));
	if (info.idDiskSignature != idDiskSignatureFound)
		Error(E_UNEXPECTED, L"Disk signatures do not match for destination %d.\n", i);

	UINT cia;

	CHECK_SUCCESS(pSourceLun->GetInterconnectAddressCount(&cia));
	if (cia != info.cIAS)
		Error(E_UNEXPECTED, L"Interconnect address count does not match for source %d", i);

	CHECK_SUCCESS(pDestLun->GetInterconnectAddressCount(&cia));
	if (cia != info.cIAD)
		Error(E_UNEXPECTED, L"Interconnect address count does not match for source %d", i);

	for(UINT iIAS = 0; iIAS < info.cIAS; iIAS++)
		{
		VDS_INTERCONNECT_ADDRESS_TYPE iat;
		LPBYTE pbAddress, pbPort;
		ULONG cbAddress, cbPort;

		CHECK_SUCCESS(pSourceLun->GetInterconnectAddress
						(
						iIAS,
						&iat,
						&cbPort,
						&pbPort,
						&cbAddress,
						&pbAddress
						));
		if (iat != info.rgiatypeS[iIAS] ||
			cbPort != 0 ||
			pbPort != NULL ||
			cbAddress != strlen(info.rgszIAS[iIAS]) + 1 ||
			strcmp((char *) pbAddress, info.rgszIAS[iIAS]) != 0)
			{
			CoTaskMemFree(pbAddress);
			Error(E_UNEXPECTED, L"Interconnect address %d does not match for source %d", iIAS, i);
			}

		if (pbAddress)
			CoTaskMemFree(pbAddress);

		if (pbPort)
			CoTaskMemFree(pbPort);
		}

	for(UINT iIAD = 0; iIAD < info.cIAD; iIAD++)
		{
		VDS_INTERCONNECT_ADDRESS_TYPE iat;
		LPBYTE pbAddress, pbPort;
		ULONG cbAddress, cbPort;

		CHECK_SUCCESS(pDestLun->GetInterconnectAddress
						(
						iIAD,
						&iat,
						&cbPort,
						&pbPort,
						&cbAddress,
						&pbAddress
						));
		if (iat != info.rgiatypeD[iIAD] ||
			cbPort != 3 ||
			memcmp(pbPort, "foo", 3) != 0  ||
			cbAddress != strlen(info.rgszIAD[iIAD]) + 1 ||
			strcmp((char *) pbAddress, info.rgszIAD[iIAD]) != 0)
			{
			CoTaskMemFree(pbAddress);
			CoTaskMemFree(pbPort);
			Error(E_UNEXPECTED, L"Interconnect address %d does not match for destination %d", iIAS, i);
			}

		if (pbPort)
			CoTaskMemFree(pbPort);

		if (pbAddress)
			CoTaskMemFree(pbAddress);
		}


	STORAGE_DEVICE_ID_DESCRIPTOR *pstoreFound;

	CHECK_SUCCESS(pSourceLun->GetStorageDeviceIdDescriptor(&pstoreFound));
	if (memcmp(pstoreFound, info.pstoreS, info.pstoreS->Size) != 0)
		{
		CoTaskMemFree(pstoreFound);
		Error(E_UNEXPECTED, L"Storage device descriptor does not match for source %d", i);
		}

	CoTaskMemFree(pstoreFound);

	CHECK_SUCCESS(pDestLun->GetStorageDeviceIdDescriptor(&pstoreFound));
	if (memcmp(pstoreFound, info.pstoreD, info.pstoreD->Size) != 0)
		{
		CoTaskMemFree(pstoreFound);
		Error(E_UNEXPECTED, L"Storage device descriptor does not match for destination %d", i);
		}

	CoTaskMemFree(pstoreFound);
	}		



void AddLunMappings(IVssSnapshotDescription *pSnapshot)
	{
	HRESULT hr;

	CComPtr<IVssLunMapping> pLunMapping1;
	CComPtr<IVssLunMapping> pLunMapping2;


	CHECK_SUCCESS(pSnapshot->AddLunMapping());
	CHECK_SUCCESS(pSnapshot->AddLunMapping());
	CHECK_SUCCESS(pSnapshot->GetLunMapping(0, &pLunMapping1));
	CHECK_SUCCESS(pSnapshot->GetLunMapping(1, &pLunMapping2));
	AddLunInfo(pLunMapping1, 1);
	AddLunInfo(pLunMapping2, 2);
	}

void ValidateLunMappings(IVssSnapshotDescription *pSnapshot)
	{
	HRESULT hr;

	CComPtr<IVssLunMapping> pLunMapping1;
	CComPtr<IVssLunMapping> pLunMapping2;
	UINT cMappings;

	CHECK_SUCCESS(pSnapshot->GetLunCount(&cMappings));
	if (cMappings != 2)
		Error(E_UNEXPECTED, L"Lun mapping count is incorrect");

	CHECK_SUCCESS(pSnapshot->GetLunMapping(0, &pLunMapping1));
	CHECK_SUCCESS(pSnapshot->GetLunMapping(1, &pLunMapping2));
	ValidateLunInfo(pLunMapping1, 1);
	ValidateLunInfo(pLunMapping2, 2);
	}


void ValidateSnapshot(IVssSnapshotDescription *pSnapshot, UINT iSnapshot)
	{
	HRESULT hr;

	VSS_SNAPSHOT_PROP *pProp = &rgSnapshotProp[iSnapshot];

	VSS_TIMESTAMP timestamp;
	LONG lAttributes;
	CComBSTR bstrOriginatingMachine;
	CComBSTR bstrServiceMachine;
	CComBSTR bstrOriginalVolumeName;
	CComBSTR bstrSnapshotDevice;

	CHECK_SUCCESS(pSnapshot->GetTimestamp(&timestamp));
	if (timestamp != pProp->m_tsCreationTimestamp)
		Error(E_UNEXPECTED, L"Timestamp mismatch on snapshot %d", iSnapshot);

	CHECK_SUCCESS(pSnapshot->GetAttributes(&lAttributes))
	if (lAttributes != pProp->m_lSnapshotAttributes)
		Error(E_UNEXPECTED, L"Attributes mismatch on snapshot %d", iSnapshot);

	CHECK_SUCCESS(pSnapshot->GetOrigin
						(
						&bstrOriginatingMachine,
						&bstrOriginalVolumeName
						));

	CHECK_SUCCESS(pSnapshot->GetServiceMachine
						(
						&bstrServiceMachine
						));

    if (wcscmp(bstrOriginatingMachine, pProp->m_pwszOriginatingMachine) != 0)
		Error(E_UNEXPECTED, L"Originating machine mismatch on snapshot %d.", iSnapshot);

	if (wcscmp(bstrOriginalVolumeName, pProp->m_pwszOriginalVolumeName) != 0)
		Error(E_UNEXPECTED, L"Original volume name mismatch on snapshot %d.", iSnapshot);

    if (wcscmp(bstrServiceMachine, pProp->m_pwszServiceMachine) != 0)
		Error(E_UNEXPECTED, L"Service machine mismatch on snapshot %d.", iSnapshot);

	CHECK_SUCCESS(pSnapshot->GetDeviceName(&bstrSnapshotDevice));
	if (wcscmp(bstrSnapshotDevice, pProp->m_pwszSnapshotDeviceObject) != 0)
		Error(E_UNEXPECTED, L"Snapshot device name mismatch on snapshot %d.", iSnapshot);

	if (iSnapshot == 2)
		{
		CComBSTR bstrExposedShare;
		CComBSTR bstrExposedPath;
		CHECK_SUCCESS(pSnapshot->GetExposure(&bstrExposedShare, &bstrExposedPath));

		if (wcscmp(bstrExposedShare, L"exposed1") != 0)
			Error(E_UNEXPECTED, L"Exposed share mismatch on snapshot %d.", iSnapshot);

		if (wcscmp(bstrExposedPath, L"thePath") != 0)
			Error(E_UNEXPECTED, L"Exposed path mismatch on snapshot %d.", iSnapshot);
		}

	ValidateLunMappings(pSnapshot);
	}

void AddSnapshotData(IVssSnapshotDescription *pSnapshot, UINT iSnapshot)
	{
	HRESULT hr;

	SYSTEMTIME time;

	VSS_SNAPSHOT_PROP *pProp = &rgSnapshotProp[iSnapshot];
	GetSystemTime(&time);
	LONGLONG timestamp;

	timestamp = time.wYear * 400 + time.wMonth * 31 + time.wDay;
	timestamp *= 3600000 * 24;
	timestamp += time.wMilliseconds + time.wSecond * 1000 + time.wMinute*60000+ time.wHour * 3600000;
	pProp->m_tsCreationTimestamp = timestamp;
	pProp->m_pwszSnapshotDeviceObject = iSnapshot == 1 ? L"Snapshot1" : L"Snapshot2";
	pProp->m_pwszOriginalVolumeName = iSnapshot == 1 ? L"Disk1" : L"Disk2";
	pProp->m_lSnapshotAttributes = VSS_CTX_BACKUP;
	WCHAR buf[1024];
	DWORD cb = 1024;
	GetComputerNameEx(ComputerNameDnsFullyQualified, buf, &cb);
	pProp->m_pwszOriginatingMachine = _wcsdup(buf);
    pProp->m_pwszServiceMachine = _wcsdup(buf);
	CHECK_SUCCESS(pSnapshot->SetTimestamp(pProp->m_tsCreationTimestamp));
	CHECK_SUCCESS(pSnapshot->SetAttributes(pProp->m_lSnapshotAttributes));
	CHECK_SUCCESS(pSnapshot->SetOrigin
						(
						pProp->m_pwszOriginatingMachine,
						pProp->m_pwszOriginalVolumeName
						));
	CHECK_SUCCESS(pSnapshot->SetServiceMachine
						(
						pProp->m_pwszServiceMachine
						));

	CHECK_SUCCESS(pSnapshot->SetDeviceName(pProp->m_pwszSnapshotDeviceObject));
	if (iSnapshot == 2)
		CHECK_SUCCESS(pSnapshot->SetExposure(L"exposed1", L"thePath"));

	AddLunMappings(pSnapshot);
	ValidateSnapshot(pSnapshot, iSnapshot);
	}




void TestSnapshotXML()
	{
	CVssFunctionTracer ft(VSSDBG_XML, L"TestSnapshotXML");

	HRESULT hr;

	try
		{
		CComPtr<IVssSnapshotSetDescription> pSnapshotSet;

		CHECK_SUCCESS(CreateVssSnapshotSetDescription
						(
						x_idSnapSet,
						VSS_CTX_BACKUP,
						&pSnapshotSet
						));



		CHECK_SUCCESS(pSnapshotSet->SetDescription(L"This is a test snapshot set"));
		CHECK_SUCCESS(pSnapshotSet->SetMetadata(L"This is some test metadata for the snapshot set."));

		ValidateSnapshotSet(pSnapshotSet);

		CHECK_SUCCESS(pSnapshotSet->AddSnapshotDescription(x_idSnap1, x_idProv));
		CHECK_SUCCESS(pSnapshotSet->AddSnapshotDescription(x_idSnap2, x_idProv));

		VSS_ID idSnap1, idSnap2;
		CComPtr<IVssSnapshotDescription> pSnapshot1;
		CComPtr<IVssSnapshotDescription> pSnapshot2;

		GetAndValidateSnapshots(pSnapshotSet, &pSnapshot1, &pSnapshot2, idSnap1, idSnap2);

		AddSnapshotData(pSnapshot1, idSnap1 == x_idSnap1 ? 1 : 2);
		AddSnapshotData(pSnapshot2, idSnap2 == x_idSnap1 ? 1 : 2);
		pSnapshot1 = NULL;
		pSnapshot2 = NULL;

		CComBSTR bstrXML;
		CHECK_SUCCESS(pSnapshotSet->SaveAsXML(&bstrXML));
		pSnapshotSet = NULL;

		CHECK_SUCCESS(LoadVssSnapshotSetDescription(bstrXML, &pSnapshotSet));

		ValidateSnapshotSet(pSnapshotSet);
		GetAndValidateSnapshots(pSnapshotSet, &pSnapshot1, &pSnapshot2, idSnap1, idSnap2);
        ValidateSnapshot(pSnapshot1, idSnap1 == x_idSnap1 ? 1 : 2);
		ValidateSnapshot(pSnapshot2, idSnap2 == x_idSnap1 ? 1 : 2);
		}
	VSS_STANDARD_CATCH(ft)

	if (ft.HrFailed())
		wprintf(L"Snapshot XML test failed with hr = 0x%08lx\n", ft.hr);
	else
		wprintf(L"Snapshot XML test succeeded\n");
    }











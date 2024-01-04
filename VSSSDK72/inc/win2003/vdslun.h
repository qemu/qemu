

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 6.00.0366 */
/* Compiler settings for vdslun.idl:
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


#ifndef __vdslun_h__
#define __vdslun_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

#ifdef __cplusplus
extern "C"{
#endif 

void * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void * ); 

/* interface __MIDL_itf_vdslun_0000 */
/* [local] */ 

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
	VDSBusTypeIScsi	= 9,
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



extern RPC_IF_HANDLE __MIDL_itf_vdslun_0000_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_vdslun_0000_v0_0_s_ifspec;

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif



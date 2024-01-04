

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 6.00.0366 */
/* Compiler settings for vsswprv.idl:
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


#ifndef __vsswprv_h__
#define __vsswprv_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __VSSoftwareProvider_FWD_DEFINED__
#define __VSSoftwareProvider_FWD_DEFINED__

#ifdef __cplusplus
typedef class VSSoftwareProvider VSSoftwareProvider;
#else
typedef struct VSSoftwareProvider VSSoftwareProvider;
#endif /* __cplusplus */

#endif 	/* __VSSoftwareProvider_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"
#include "vss.h"

#ifdef __cplusplus
extern "C"{
#endif 

void * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void * ); 

/* interface __MIDL_itf_vsswprv_0000 */
/* [local] */ 

const GUID VSS_SWPRV_ProviderId = { 0xb5946137, 0x7b9f, 0x4925, { 0xaf, 0x80, 0x51, 0xab, 0xd6, 0xb, 0x20, 0xd5 } };


extern RPC_IF_HANDLE __MIDL_itf_vsswprv_0000_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_vsswprv_0000_v0_0_s_ifspec;


#ifndef __VSSW_LIBRARY_DEFINED__
#define __VSSW_LIBRARY_DEFINED__

/* library VSSW */
/* [helpstring][version][uuid] */ 


EXTERN_C const IID LIBID_VSSW;

EXTERN_C const CLSID CLSID_VSSoftwareProvider;

#ifdef __cplusplus

class DECLSPEC_UUID("65EE1DBA-8FF4-4a58-AC1C-3470EE2F376A")
VSSoftwareProvider;
#endif
#endif /* __VSSW_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif



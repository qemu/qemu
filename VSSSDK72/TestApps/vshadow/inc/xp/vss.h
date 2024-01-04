
#pragma warning( disable: 4049 )  /* more than 64k source lines */

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 6.00.0347 */
/* Compiler settings for vss.idl:
    Oicf, W1, Zp8, env=Win32 (32b run)
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
//@@MIDL_FILE_HEADING(  )


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

#ifndef __vss_h__
#define __vss_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IVssEnumObject_FWD_DEFINED__
#define __IVssEnumObject_FWD_DEFINED__
typedef interface IVssEnumObject IVssEnumObject;
#endif 	/* __IVssEnumObject_FWD_DEFINED__ */


#ifndef __IVssAsync_FWD_DEFINED__
#define __IVssAsync_FWD_DEFINED__
typedef interface IVssAsync IVssAsync;
#endif 	/* __IVssAsync_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

#ifdef __cplusplus
extern "C"{
#endif 

void * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void * ); 

/* interface __MIDL_itf_vss_0000 */
/* [local] */ 



typedef 
enum _VSS_OBJECT_TYPE
    {	VSS_OBJECT_UNKNOWN	= 0,
	VSS_OBJECT_NONE	= VSS_OBJECT_UNKNOWN + 1,
	VSS_OBJECT_SNAPSHOT_SET	= VSS_OBJECT_NONE + 1,
	VSS_OBJECT_SNAPSHOT	= VSS_OBJECT_SNAPSHOT_SET + 1,
	VSS_OBJECT_PROVIDER	= VSS_OBJECT_SNAPSHOT + 1,
	VSS_OBJECT_TYPE_COUNT	= VSS_OBJECT_PROVIDER + 1
    } 	VSS_OBJECT_TYPE;

typedef enum _VSS_OBJECT_TYPE *PVSS_OBJECT_TYPE;

typedef 
enum _VSS_SNAPSHOT_STATE
    {	VSS_SS_UNKNOWN	= 0,
	VSS_SS_PREPARING	= VSS_SS_UNKNOWN + 1,
	VSS_SS_PROCESSING_PREPARE	= VSS_SS_PREPARING + 1,
	VSS_SS_PREPARED	= VSS_SS_PROCESSING_PREPARE + 1,
	VSS_SS_PROCESSING_PRECOMMIT	= VSS_SS_PREPARED + 1,
	VSS_SS_PRECOMMITTED	= VSS_SS_PROCESSING_PRECOMMIT + 1,
	VSS_SS_PROCESSING_COMMIT	= VSS_SS_PRECOMMITTED + 1,
	VSS_SS_COMMITTED	= VSS_SS_PROCESSING_COMMIT + 1,
	VSS_SS_PROCESSING_POSTCOMMIT	= VSS_SS_COMMITTED + 1,
	VSS_SS_CREATED	= VSS_SS_PROCESSING_POSTCOMMIT + 1,
	VSS_SS_ABORTED	= VSS_SS_CREATED + 1,
	VSS_SS_DELETED	= VSS_SS_ABORTED + 1,
	VSS_SS_COUNT	= VSS_SS_DELETED + 1
    } 	VSS_SNAPSHOT_STATE;

typedef enum _VSS_SNAPSHOT_STATE *PVSS_SNAPSHOT_STATE;


enum _VSS_VOLUME_SNAPSHOT_ATTRIBUTES
    {	VSS_VOLSNAP_ATTR_PERSISTENT	= 0x1,
	VSS_VOLSNAP_ATTR_READ_WRITE	= 0x2,
	VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE	= 0x4,
	VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE	= 0x8,
	VSS_VOLSNAP_ATTR_NO_WRITERS	= 0x10,
	VSS_VOLSNAP_ATTR_TRANSPORTABLE	= 0x20,
	VSS_VOLSNAP_ATTR_NOT_SURFACED	= 0x40,
	VSS_VOLSNAP_ATTR_HARDWARE_ASSISTED	= 0x10000,
	VSS_VOLSNAP_ATTR_DIFFERENTIAL	= 0x20000,
	VSS_VOLSNAP_ATTR_PLEX	= 0x40000,
	VSS_VOLSNAP_ATTR_IMPORTED	= 0x80000,
	VSS_VOLSNAP_ATTR_EXPOSED_LOCALLY	= 0x100000,
	VSS_VOLSNAP_ATTR_EXPOSED_REMOTELY	= 0x200000
    } ;

enum _VSS_SNAPSHOT_CONTEXT
    {	VSS_CTX_BACKUP	= 0,
	VSS_CTX_FILE_SHARE_BACKUP	= VSS_VOLSNAP_ATTR_NO_WRITERS,
	VSS_CTX_NAS_ROLLBACK	= VSS_VOLSNAP_ATTR_PERSISTENT | VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE | VSS_VOLSNAP_ATTR_NO_WRITERS,
	VSS_CTX_APP_ROLLBACK	= VSS_VOLSNAP_ATTR_PERSISTENT | VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE,
	VSS_CTX_CLIENT_ACCESSIBLE	= VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE | VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE | VSS_VOLSNAP_ATTR_NO_WRITERS,
	VSS_CTX_PERSISTENT_CLIENT_ACCESSIBLE	= VSS_VOLSNAP_ATTR_PERSISTENT | VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE | VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE | VSS_VOLSNAP_ATTR_NO_WRITERS,
	VSS_CTX_ALL	= 0xffffffff
    } ;
typedef 
enum _VSS_WRITER_STATE
    {	VSS_WS_UNKNOWN	= 0,
	VSS_WS_STABLE	= VSS_WS_UNKNOWN + 1,
	VSS_WS_WAITING_FOR_FREEZE	= VSS_WS_STABLE + 1,
	VSS_WS_WAITING_FOR_THAW	= VSS_WS_WAITING_FOR_FREEZE + 1,
	VSS_WS_WAITING_FOR_POST_SNAPSHOT	= VSS_WS_WAITING_FOR_THAW + 1,
	VSS_WS_WAITING_FOR_BACKUP_COMPLETE	= VSS_WS_WAITING_FOR_POST_SNAPSHOT + 1,
	VSS_WS_FAILED_AT_IDENTIFY	= VSS_WS_WAITING_FOR_BACKUP_COMPLETE + 1,
	VSS_WS_FAILED_AT_PREPARE_BACKUP	= VSS_WS_FAILED_AT_IDENTIFY + 1,
	VSS_WS_FAILED_AT_PREPARE_SNAPSHOT	= VSS_WS_FAILED_AT_PREPARE_BACKUP + 1,
	VSS_WS_FAILED_AT_FREEZE	= VSS_WS_FAILED_AT_PREPARE_SNAPSHOT + 1,
	VSS_WS_FAILED_AT_THAW	= VSS_WS_FAILED_AT_FREEZE + 1,
	VSS_WS_FAILED_AT_POST_SNAPSHOT	= VSS_WS_FAILED_AT_THAW + 1,
	VSS_WS_FAILED_AT_BACKUP_COMPLETE	= VSS_WS_FAILED_AT_POST_SNAPSHOT + 1,
	VSS_WS_FAILED_AT_PRE_RESTORE	= VSS_WS_FAILED_AT_BACKUP_COMPLETE + 1,
	VSS_WS_FAILED_AT_POST_RESTORE	= VSS_WS_FAILED_AT_PRE_RESTORE + 1,
	VSS_WS_COUNT	= VSS_WS_FAILED_AT_POST_RESTORE + 1
    } 	VSS_WRITER_STATE;

typedef enum _VSS_WRITER_STATE *PVSS_WRITER_STATE;

typedef 
enum _VSS_BACKUP_TYPE
    {	VSS_BT_UNDEFINED	= 0,
	VSS_BT_FULL	= VSS_BT_UNDEFINED + 1,
	VSS_BT_INCREMENTAL	= VSS_BT_FULL + 1,
	VSS_BT_DIFFERENTIAL	= VSS_BT_INCREMENTAL + 1,
	VSS_BT_LOG	= VSS_BT_DIFFERENTIAL + 1,
	VSS_BT_OTHER	= VSS_BT_LOG + 1
    } 	VSS_BACKUP_TYPE;

typedef 
enum _VSS_PROVIDER_TYPE
    {	VSS_PROV_UNKNOWN	= 0,
	VSS_PROV_SYSTEM	= 1,
	VSS_PROV_SOFTWARE	= 2,
	VSS_PROV_HARDWARE	= 3
    } 	VSS_PROVIDER_TYPE;

typedef enum _VSS_PROVIDER_TYPE *PVSS_PROVIDER_TYPE;

typedef 
enum _VSS_APPLICATION_LEVEL
    {	VSS_APP_UNKNOWN	= 0,
	VSS_APP_SYSTEM	= 1,
	VSS_APP_BACK_END	= 2,
	VSS_APP_FRONT_END	= 3,
	VSS_APP_AUTO	= -1
    } 	VSS_APPLICATION_LEVEL;

typedef enum _VSS_APPLICATION_LEVEL *PVSS_APPLICATION_LEVEL;

typedef 
enum _VSS_SNAPSHOT_COMPATIBILITY
    {	VSS_SC_DISABLE_DEFRAG	= 0x1,
	VSS_SC_DISABLE_CONTENTINDEX	= 0x2
    } 	VSS_SNAPSHOT_COMPATIBILITY;

typedef 
enum _VSS_SNAPSHOT_PROPERTY_ID
    {	VSS_SPROPID_UNKNOWN	= 0,
	VSS_SPROPID_SNAPSHOT_ID	= 0x1,
	VSS_SPROPID_SNAPSHOT_SET_ID	= 0x2,
	VSS_SPROPID_SNAPSHOTS_COUNT	= 0x3,
	VSS_SPROPID_SNAPSHOT_DEVICE	= 0x4,
	VSS_SPROPID_ORIGINAL_VOLUME	= 0x5,
	VSS_SPROPID_ORIGINATING_MACHINE	= 0x6,
	VSS_SPROPID_SERVICE_MACHINE	= 0x7,
	VSS_SPROPID_EXPOSED_NAME	= 0x8,
	VSS_SPROPID_EXPOSED_PATH	= 0x9,
	VSS_SPROPID_PROVIDER_ID	= 0xa,
	VSS_SPROPID_SNAPSHOT_ATTRIBUTES	= 0xb,
	VSS_SPROPID_CREATION_TIMESTAMP	= 0xc,
	VSS_SPROPID_STATUS	= 0xd
    } 	VSS_SNAPSHOT_PROPERTY_ID;

typedef enum _VSS_SNAPSHOT_PROPERTY_ID *PVSS_SNAPSHOT_PROPERTY_ID;

typedef GUID VSS_ID;

typedef /* [string][unique] */ WCHAR *VSS_PWSZ;

typedef LONGLONG VSS_TIMESTAMP;

typedef struct _VSS_SNAPSHOT_PROP
    {
    VSS_ID m_SnapshotId;
    VSS_ID m_SnapshotSetId;
    LONG m_lSnapshotsCount;
    VSS_PWSZ m_pwszSnapshotDeviceObject;
    VSS_PWSZ m_pwszOriginalVolumeName;
    VSS_PWSZ m_pwszOriginatingMachine;
    VSS_PWSZ m_pwszServiceMachine;
    VSS_PWSZ m_pwszExposedName;
    VSS_PWSZ m_pwszExposedPath;
    VSS_ID m_ProviderId;
    LONG m_lSnapshotAttributes;
    VSS_TIMESTAMP m_tsCreationTimestamp;
    VSS_SNAPSHOT_STATE m_eStatus;
    } 	VSS_SNAPSHOT_PROP;

typedef struct _VSS_SNAPSHOT_PROP *PVSS_SNAPSHOT_PROP;

typedef struct _VSS_PROVIDER_PROP
    {
    VSS_ID m_ProviderId;
    VSS_PWSZ m_pwszProviderName;
    VSS_PROVIDER_TYPE m_eProviderType;
    VSS_PWSZ m_pwszProviderVersion;
    VSS_ID m_ProviderVersionId;
    CLSID m_ClassId;
    } 	VSS_PROVIDER_PROP;

typedef struct _VSS_PROVIDER_PROP *PVSS_PROVIDER_PROP;

typedef /* [public][public][public][public][switch_type] */ union __MIDL___MIDL_itf_vss_0000_0001
    {
    /* [case()] */ VSS_SNAPSHOT_PROP Snap;
    /* [case()] */ VSS_PROVIDER_PROP Prov;
    /* [default] */  /* Empty union arm */ 
    } 	VSS_OBJECT_UNION;

typedef struct _VSS_OBJECT_PROP
    {
    VSS_OBJECT_TYPE Type;
    /* [switch_is] */ VSS_OBJECT_UNION Obj;
    } 	VSS_OBJECT_PROP;

typedef struct _VSS_OBJECT_PROP *PVSS_OBJECT_PROP;



extern RPC_IF_HANDLE __MIDL_itf_vss_0000_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_vss_0000_v0_0_s_ifspec;

#ifndef __IVssEnumObject_INTERFACE_DEFINED__
#define __IVssEnumObject_INTERFACE_DEFINED__

/* interface IVssEnumObject */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssEnumObject;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("AE1C7110-2F60-11d3-8A39-00C04F72D8E3")
    IVssEnumObject : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE Next( 
            /* [in] */ ULONG celt,
            /* [length_is][size_is][out] */ VSS_OBJECT_PROP *rgelt,
            /* [out] */ ULONG *pceltFetched) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Skip( 
            /* [in] */ ULONG celt) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Reset( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Clone( 
            /* [out][in] */ IVssEnumObject **ppenum) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssEnumObjectVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssEnumObject * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssEnumObject * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssEnumObject * This);
        
        HRESULT ( STDMETHODCALLTYPE *Next )( 
            IVssEnumObject * This,
            /* [in] */ ULONG celt,
            /* [length_is][size_is][out] */ VSS_OBJECT_PROP *rgelt,
            /* [out] */ ULONG *pceltFetched);
        
        HRESULT ( STDMETHODCALLTYPE *Skip )( 
            IVssEnumObject * This,
            /* [in] */ ULONG celt);
        
        HRESULT ( STDMETHODCALLTYPE *Reset )( 
            IVssEnumObject * This);
        
        HRESULT ( STDMETHODCALLTYPE *Clone )( 
            IVssEnumObject * This,
            /* [out][in] */ IVssEnumObject **ppenum);
        
        END_INTERFACE
    } IVssEnumObjectVtbl;

    interface IVssEnumObject
    {
        CONST_VTBL struct IVssEnumObjectVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssEnumObject_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssEnumObject_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssEnumObject_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssEnumObject_Next(This,celt,rgelt,pceltFetched)	\
    (This)->lpVtbl -> Next(This,celt,rgelt,pceltFetched)

#define IVssEnumObject_Skip(This,celt)	\
    (This)->lpVtbl -> Skip(This,celt)

#define IVssEnumObject_Reset(This)	\
    (This)->lpVtbl -> Reset(This)

#define IVssEnumObject_Clone(This,ppenum)	\
    (This)->lpVtbl -> Clone(This,ppenum)

#endif /* COBJMACROS */


#endif 	/* C style interface */



HRESULT STDMETHODCALLTYPE IVssEnumObject_Next_Proxy( 
    IVssEnumObject * This,
    /* [in] */ ULONG celt,
    /* [length_is][size_is][out] */ VSS_OBJECT_PROP *rgelt,
    /* [out] */ ULONG *pceltFetched);


void __RPC_STUB IVssEnumObject_Next_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssEnumObject_Skip_Proxy( 
    IVssEnumObject * This,
    /* [in] */ ULONG celt);


void __RPC_STUB IVssEnumObject_Skip_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssEnumObject_Reset_Proxy( 
    IVssEnumObject * This);


void __RPC_STUB IVssEnumObject_Reset_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssEnumObject_Clone_Proxy( 
    IVssEnumObject * This,
    /* [out][in] */ IVssEnumObject **ppenum);


void __RPC_STUB IVssEnumObject_Clone_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssEnumObject_INTERFACE_DEFINED__ */


#ifndef __IVssAsync_INTERFACE_DEFINED__
#define __IVssAsync_INTERFACE_DEFINED__

/* interface IVssAsync */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssAsync;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("C7B98A22-222D-4e62-B875-1A44980634AF")
    IVssAsync : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Cancel( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Wait( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryStatus( 
            /* [out] */ HRESULT *pHrResult,
            /* [unique][out][in] */ INT *pReserved) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssAsyncVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssAsync * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssAsync * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssAsync * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Cancel )( 
            IVssAsync * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Wait )( 
            IVssAsync * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryStatus )( 
            IVssAsync * This,
            /* [out] */ HRESULT *pHrResult,
            /* [unique][out][in] */ INT *pReserved);
        
        END_INTERFACE
    } IVssAsyncVtbl;

    interface IVssAsync
    {
        CONST_VTBL struct IVssAsyncVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssAsync_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssAsync_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssAsync_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssAsync_Cancel(This)	\
    (This)->lpVtbl -> Cancel(This)

#define IVssAsync_Wait(This)	\
    (This)->lpVtbl -> Wait(This)

#define IVssAsync_QueryStatus(This,pHrResult,pReserved)	\
    (This)->lpVtbl -> QueryStatus(This,pHrResult,pReserved)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssAsync_Cancel_Proxy( 
    IVssAsync * This);


void __RPC_STUB IVssAsync_Cancel_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssAsync_Wait_Proxy( 
    IVssAsync * This);


void __RPC_STUB IVssAsync_Wait_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssAsync_QueryStatus_Proxy( 
    IVssAsync * This,
    /* [out] */ HRESULT *pHrResult,
    /* [unique][out][in] */ INT *pReserved);


void __RPC_STUB IVssAsync_QueryStatus_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssAsync_INTERFACE_DEFINED__ */


/* interface __MIDL_itf_vss_0257 */
/* [local] */ 

#define	VSS_E_BAD_STATE	( 0x80042301L )

#define	VSS_E_PROVIDER_ALREADY_REGISTERED	( 0x80042303L )

#define	VSS_E_PROVIDER_NOT_REGISTERED	( 0x80042304L )

#define	VSS_E_PROVIDER_VETO	( 0x80042306L )

#define	VSS_E_PROVIDER_IN_USE	( 0x80042307L )

#define	VSS_E_OBJECT_NOT_FOUND	( 0x80042308L )

#define	VSS_S_ASYNC_PENDING	( 0x42309L )

#define	VSS_S_ASYNC_FINISHED	( 0x4230aL )

#define	VSS_S_ASYNC_CANCELLED	( 0x4230bL )

#define	VSS_E_VOLUME_NOT_SUPPORTED	( 0x8004230cL )

#define	VSS_E_VOLUME_NOT_SUPPORTED_BY_PROVIDER	( 0x8004230eL )

#define	VSS_E_OBJECT_ALREADY_EXISTS	( 0x8004230dL )

#define	VSS_E_UNEXPECTED_PROVIDER_ERROR	( 0x8004230fL )

#define	VSS_E_CORRUPT_XML_DOCUMENT	( 0x80042310L )

#define	VSS_E_INVALID_XML_DOCUMENT	( 0x80042311L )

#define	VSS_E_MAXIMUM_NUMBER_OF_VOLUMES_REACHED	( 0x80042312L )

#define	VSS_E_FLUSH_WRITES_TIMEOUT	( 0x80042313L )

#define	VSS_E_HOLD_WRITES_TIMEOUT	( 0x80042314L )

#define	VSS_E_UNEXPECTED_WRITER_ERROR	( 0x80042315L )

#define	VSS_E_SNAPSHOT_SET_IN_PROGRESS	( 0x80042316L )

#define	VSS_E_MAXIMUM_NUMBER_OF_SNAPSHOTS_REACHED	( 0x80042317L )

#define	VSS_E_WRITER_INFRASTRUCTURE	( 0x80042318L )

#define	VSS_E_WRITER_NOT_RESPONDING	( 0x80042319L )

#define	VSS_E_WRITER_ALREADY_SUBSCRIBED	( 0x8004231aL )

#define	VSS_E_UNSUPPORTED_CONTEXT	( 0x8004231bL )

#define	VSS_E_VOLUME_IN_USE	( 0x8004231dL )

#define	VSS_E_MAXIMUM_DIFFAREA_ASSOCIATIONS_REACHED	( 0x8004231eL )

#define	VSS_E_INSUFFICIENT_STORAGE	( 0x8004231fL )



extern RPC_IF_HANDLE __MIDL_itf_vss_0257_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_vss_0257_v0_0_s_ifspec;

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif



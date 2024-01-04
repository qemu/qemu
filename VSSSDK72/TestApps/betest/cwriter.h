
//
//  ATL debugging support turned on at debug version
//  BUGBUG: the ATL thunking support is not enable yet in IA64
//  When this will be enabled then enable it here also!
//
#ifdef _DEBUG
#ifdef _M_IX86
#define _ATL_DEBUG_INTERFACES
#define _ATL_DEBUG_QI
#define _ATL_DEBUG_REFCOUNT
#endif
#endif // _DEBUG

class CTestVssWriter : public CVssWriter
    {
public:
    enum
        {
        x_bitWaitIdentify = 1,
        x_bitWaitPrepareForBackup = 2,
        x_bitWaitPostSnapshot = 4,
        x_bitWaitBackupComplete = 8,
        x_bitWaitPreRestore = 16,
        x_bitWaitPostRestore = 32,
        x_bitWaitPrepareSnapshot = 64,
        x_bitWaitFreeze = 128,
        x_bitWaitThaw = 256,
        x_bitWaitAbort = 512,
        x_RestoreTestOptions_RestoreIfNotThere = 1
        };

    CTestVssWriter(bool bRestoreTest, bool bTestNewInterfaces, LONG lWait, LONG lRestoreTestOptions) :
        m_lWait(lWait),
        m_bRestoreTest(bRestoreTest),
        m_bTestNewInterfaces(bTestNewInterfaces),
        m_lRestoreTestOptions(lRestoreTestOptions),
        m_rghOpen(NULL),
        m_chOpen(0),
        m_chOpenMax(0)
        {
        }

    ~CTestVssWriter()
        {
        for(UINT ih = 0; ih < m_chOpen; ih++)
            CloseHandle(m_rghOpen[ih]);

        delete m_rghOpen;
        }


    void Initialize();

    virtual bool STDMETHODCALLTYPE OnIdentify(IN IVssCreateWriterMetadata *pMetadata);

    virtual bool STDMETHODCALLTYPE OnPrepareBackup(IN IVssWriterComponents *pComponent);

    virtual bool STDMETHODCALLTYPE OnPrepareSnapshot();

    virtual bool STDMETHODCALLTYPE OnFreeze();

    virtual bool STDMETHODCALLTYPE OnThaw();

    virtual bool STDMETHODCALLTYPE OnAbort();
    virtual bool STDMETHODCALLTYPE OnPostSnapshot(IN IVssWriterComponents *pComponent);


    virtual bool STDMETHODCALLTYPE OnBackupComplete(IN IVssWriterComponents *pComponent);

    virtual bool STDMETHODCALLTYPE OnBackupShutdown(IN VSS_ID SnapshotSetId);

    virtual bool STDMETHODCALLTYPE OnPreRestore(IN IVssWriterComponents *pComponent);

    virtual bool STDMETHODCALLTYPE OnPostRestore(IN IVssWriterComponents *pComponent);
private:
    bool DoNewInterfacesTestIdentify(IVssCreateWriterMetadata* pMetadata);

    bool DoRestoreTestIdentify(IN IVssCreateWriterMetadata *pMetadata);
    bool DoRestoreTestPrepareBackup(IN IVssWriterComponents *pComponents);
    bool DoRestoreTestPreRestore(IN IVssWriterComponents *pComponents);
    bool DoRestoreTestPostRestore(IN IVssWriterComponents *pComponents);
    void DoAddComponent
        (
        IVssCreateWriterMetadata *pMetadata,
        LPCWSTR wszComponentName,
        LPCWSTR wszRootDirectory,
        LPCWSTR wszSubdirectory,
        LPCWSTR wszFilespec,
        LPCWSTR wszAlternateDirectory,
        bool selectable,
        bool selectableForRestore,
        LONG attributes
        );

    void CreateDirectoryName(LPWSTR buf);
    void CreateComponentFilesA(LPCWSTR buf, bool bKeepOpen);
    void CreateComponentFilesB(LPCWSTR buf, bool bKeepOpen);
    void CreateComponentFilesC(LPCWSTR buf, bool bKeepOpen);
    void VerifyComponentFilesA(LPCWSTR buf);
    void VerifyComponentFilesB(LPCWSTR buf);
    void VerifyComponentFilesC(LPCWSTR buf);

    void DoCreateFile
            (
            LPCWSTR wszPath,
            LPCWSTR wszFilename,
            DWORD length,
            bool bKeepOpen
            );

    void DoVerifyFile(LPCWSTR wszPath, LPCWSTR wszFilename, DWORD length);

    LONG m_lWait;
    LONG m_lRestoreTestOptions;
    bool m_bRestoreTest;
    bool m_bTestNewInterfaces;
    HANDLE *m_rghOpen;
    UINT m_chOpen;
    UINT m_chOpenMax;
    };



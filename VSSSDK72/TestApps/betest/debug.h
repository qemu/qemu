// Execute the given call and check that the return code must be S_OK
#define CHECK_SUCCESS( Call )                                                                           \
    {                                                                                                   \
        hr = Call;                                                                                    \
        if (hr != S_OK)                                                                               \
            Error(1, L"\nError in %S(%d): \n\t- Call %S not succeeded. \n"                              \
                L"\t  Error code = 0x%08lx. Error description = %s\n",                                  \
                __FILE__, __LINE__, #Call, hr, GetStringFromFailureType(hr));                       \
    }

#define CHECK_NOFAIL( Call )                                                                            \
    {                                                                                                   \
        hr = Call;                                                                                    \
        if (FAILED(hr))                                                                               \
            Error(1, L"\nError in %S(%d): \n\t- Call %S not succeeded. \n"                              \
                L"\t  Error code = 0x%08lx. Error description = %s\n",                                  \
                __FILE__, __LINE__, #Call, hr, GetStringFromFailureType(hr));                       \
    }



void Error(INT nReturnCode, const WCHAR* pwszMsgFormat, ...);
LPCWSTR GetStringFromFailureType(HRESULT hrStatus);
LPCWSTR WszFromRestoreTarget(VSS_RESTORE_TARGET rt);
LPCWSTR WszFromFileRestoreStatus(VSS_FILE_RESTORE_STATUS rs);
void PrintPartialFiles(IVssComponent *pComponent);
void PrintDifferencedFiles(IVssComponent* pComponent);
void PrintNewTargets(IVssComponent *pComponent);
void PrintDirectedTargets(IVssComponent *pComponent);
void PrintRestoreSubcomponents(IVssComponent *pComponent);


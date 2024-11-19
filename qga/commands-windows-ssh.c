/*
 * QEMU Guest Agent win32-specific command implementations for SSH keys.
 * The implementation is opinionated and expects the SSH implementation to
 * be OpenSSH.
 *
 * Copyright Schweitzer Engineering Laboratories. 2024
 *
 * Authors:
 *  Aidan Leuck <aidan_leuck@selinc.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <aclapi.h>
#include <qga-qapi-types.h>

#include "commands-common-ssh.h"
#include "commands-windows-ssh.h"
#include "guest-agent-core.h"
#include "limits.h"
#include "lmaccess.h"
#include "lmapibuf.h"
#include "lmerr.h"
#include "qapi/error.h"

#include "qga-qapi-commands.h"
#include "sddl.h"
#include "shlobj.h"
#include "userenv.h"

#define AUTHORIZED_KEY_FILE "authorized_keys"
#define AUTHORIZED_KEY_FILE_ADMIN "administrators_authorized_keys"
#define LOCAL_SYSTEM_SID "S-1-5-18"
#define ADMIN_SID "S-1-5-32-544"

/*
 * Frees userInfo structure. This implements the g_auto cleanup
 * for the structure.
 */
void free_userInfo(PWindowsUserInfo info)
{
    g_free(info->sshDirectory);
    g_free(info->authorizedKeyFile);
    LocalFree(info->SSID);
    g_free(info->username);
    g_free(info);
}

/*
 * Gets the admin SSH folder for OpenSSH. OpenSSH does not store
 * the authorized_key file in the users home directory for security reasons and
 * instead stores it at %PROGRAMDATA%/ssh. This function returns the path to
 * that directory on the users machine
 *
 * parameters:
 * errp -> error structure to set when an error occurs
 * returns: The path to the ssh folder in %PROGRAMDATA% or NULL if an error
 * occurred.
 */
static char *get_admin_ssh_folder(Error **errp)
{
    /* Allocate memory for the program data path */
    g_autofree char *programDataPath = NULL;
    char *authkeys_path = NULL;
    PWSTR pgDataW = NULL;
    g_autoptr(GError) gerr = NULL;

    /* Get the KnownFolderPath on the machine. */
    HRESULT folderResult =
        SHGetKnownFolderPath(&FOLDERID_ProgramData, 0, NULL, &pgDataW);
    if (folderResult != S_OK) {
        error_setg(errp, "Failed to retrieve ProgramData folder");
        return NULL;
    }

    /* Convert from a wide string back to a standard character string. */
    programDataPath = g_utf16_to_utf8(pgDataW, -1, NULL, NULL, &gerr);
    CoTaskMemFree(pgDataW);
    if (!programDataPath) {
        error_setg(errp,
                   "Failed converting ProgramData folder path to UTF-16 %s",
                   gerr->message);
        return NULL;
    }

    /* Build the path to the file. */
    authkeys_path = g_build_filename(programDataPath, "ssh", NULL);
    return authkeys_path;
}

/*
 * Gets the path to the SSH folder for the specified user. If the user is an
 * admin it returns the ssh folder located at %PROGRAMDATA%/ssh. If the user is
 * not an admin it returns %USERPROFILE%/.ssh
 *
 * parameters:
 * username -> Username to get the SSH folder for
 * isAdmin -> Whether the user is an admin or not
 * errp -> Error structure to set any errors that occur.
 * returns: path to the ssh folder as a string.
 */
static char *get_ssh_folder(const char *username, const bool isAdmin,
                            Error **errp)
{
    DWORD maxSize = MAX_PATH;
    g_autofree char *profilesDir = g_new0(char, maxSize);

    if (isAdmin) {
        return get_admin_ssh_folder(errp);
    }

    /* If not an Admin the SSH key is in the user directory. */
    /* Get the user profile directory on the machine. */
    BOOL ret = GetProfilesDirectory(profilesDir, &maxSize);
    if (!ret) {
        error_setg_win32(errp, GetLastError(),
                         "failed to retrieve profiles directory");
        return NULL;
    }

    /* Builds the filename */
    return g_build_filename(profilesDir, username, ".ssh", NULL);
}

/*
 * Creates an entry for the user so they can access the ssh folder in their
 * userprofile.
 *
 * parameters:
 * userInfo -> Information about the current user
 * pACL -> Pointer to an ACL structure
 * errp -> Error structure to set any errors that occur
 * returns -> 1 on success, 0 otherwise
 */
static bool create_acl_user(PWindowsUserInfo userInfo, PACL *pACL, Error **errp)
{
    const int aclSize = 1;
    PACL newACL = NULL;
    EXPLICIT_ACCESS eAccess[1];
    PSID userPSID = NULL;

    /* Get a pointer to the internal SID object in Windows */
    bool converted = ConvertStringSidToSid(userInfo->SSID, &userPSID);
    if (!converted) {
        error_setg_win32(errp, GetLastError(), "failed to retrieve user %s SID",
                         userInfo->username);
        goto error;
    }

    /* Set the permissions for the user. */
    eAccess[0].grfAccessPermissions = GENERIC_ALL;
    eAccess[0].grfAccessMode = SET_ACCESS;
    eAccess[0].grfInheritance = NO_INHERITANCE;
    eAccess[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    eAccess[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    eAccess[0].Trustee.ptstrName = (LPTSTR)userPSID;

    /* Set the ACL entries */
    DWORD setResult;

    /*
     * If we are given a pointer that is already initialized, then we can merge
     * the existing entries instead of overwriting them.
     */
    if (*pACL) {
        setResult = SetEntriesInAcl(aclSize, eAccess, *pACL, &newACL);
    } else {
        setResult = SetEntriesInAcl(aclSize, eAccess, NULL, &newACL);
    }

    if (setResult != ERROR_SUCCESS) {
        error_setg_win32(errp, GetLastError(),
                         "failed to set ACL entries for user %s %lu",
                         userInfo->username, setResult);
        goto error;
    }

    /* Free any old memory since we are going to overwrite the users pointer. */
    LocalFree(*pACL);
    *pACL = newACL;

    LocalFree(userPSID);
    return true;
error:
    LocalFree(userPSID);
    return false;
}

/*
 * Creates a base ACL for both normal users and admins to share
 * pACL -> Pointer to an ACL structure
 * errp -> Error structure to set any errors that occur
 * returns: 1 on success, 0 otherwise
 */
static bool create_acl_base(PACL *pACL, Error **errp)
{
    PSID adminGroupPSID = NULL;
    PSID systemPSID = NULL;

    const int aclSize = 2;
    EXPLICIT_ACCESS eAccess[2];

    /* Create an entry for the system user. */
    const char *systemSID = LOCAL_SYSTEM_SID;
    bool converted = ConvertStringSidToSid(systemSID, &systemPSID);
    if (!converted) {
        error_setg_win32(errp, GetLastError(), "failed to retrieve system SID");
        goto error;
    }

    /* set permissions for system user */
    eAccess[0].grfAccessPermissions = GENERIC_ALL;
    eAccess[0].grfAccessMode = SET_ACCESS;
    eAccess[0].grfInheritance = NO_INHERITANCE;
    eAccess[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    eAccess[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    eAccess[0].Trustee.ptstrName = (LPTSTR)systemPSID;

    /* Create an entry for the admin user. */
    const char *adminSID = ADMIN_SID;
    converted = ConvertStringSidToSid(adminSID, &adminGroupPSID);
    if (!converted) {
        error_setg_win32(errp, GetLastError(), "failed to retrieve Admin SID");
        goto error;
    }

    /* Set permissions for admin group. */
    eAccess[1].grfAccessPermissions = GENERIC_ALL;
    eAccess[1].grfAccessMode = SET_ACCESS;
    eAccess[1].grfInheritance = NO_INHERITANCE;
    eAccess[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    eAccess[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    eAccess[1].Trustee.ptstrName = (LPTSTR)adminGroupPSID;

    /* Put the entries in an ACL object. */
    PACL pNewACL = NULL;
    DWORD setResult;

    /*
     *If we are given a pointer that is already initialized, then we can merge
     *the existing entries instead of overwriting them.
     */
    if (*pACL) {
        setResult = SetEntriesInAcl(aclSize, eAccess, *pACL, &pNewACL);
    } else {
        setResult = SetEntriesInAcl(aclSize, eAccess, NULL, &pNewACL);
    }

    if (setResult != ERROR_SUCCESS) {
        error_setg_win32(errp, GetLastError(),
                         "failed to set base ACL entries for system user and "
                         "admin group %lu",
                         setResult);
        goto error;
    }

    LocalFree(adminGroupPSID);
    LocalFree(systemPSID);

    /* Free any old memory since we are going to overwrite the users pointer. */
    LocalFree(*pACL);

    *pACL = pNewACL;

    return true;

error:
    LocalFree(adminGroupPSID);
    LocalFree(systemPSID);
    return false;
}

/*
 * Sets the access control on the authorized_keys file and any ssh folders that
 * need to be created. For administrators the required permissions on the
 * file/folders are that only administrators and the LocalSystem account can
 * access the folders. For normal user accounts only the specified user,
 * LocalSystem and Administrators can have access to the key.
 *
 * parameters:
 * userInfo -> pointer to structure that contains information about the user
 * PACL -> pointer to an access control structure that will be set upon
 * successful completion of the function.
 * errp -> error structure that will be set upon error.
 * returns: 1 upon success 0 upon failure.
 */
static bool create_acl(PWindowsUserInfo userInfo, PACL *pACL, Error **errp)
{
    /*
     * Creates a base ACL that both admins and users will share
     * This adds the Administrators group and the SYSTEM group
     */
    if (!create_acl_base(pACL, errp)) {
        return false;
    }

    /*
     * If the user is not an admin give the user creating the key permission to
     * access the file.
     */
    if (!userInfo->isAdmin) {
        if (!create_acl_user(userInfo, pACL, errp)) {
            return false;
        }

        return true;
    }

    return true;
}
/*
 * Create the SSH directory for the user and d sets appropriate permissions.
 * In general the directory will be %PROGRAMDATA%/ssh if the user is an admin.
 * %USERPOFILE%/.ssh if not an admin
 *
 * parameters:
 * userInfo -> Contains information about the user
 * errp -> Structure that will contain errors if the function fails.
 * returns: zero upon failure, 1 upon success
 */
static bool create_ssh_directory(WindowsUserInfo *userInfo, Error **errp)
{
    PACL pNewACL = NULL;
    g_autofree PSECURITY_DESCRIPTOR pSD = NULL;

    /* Gets the appropriate ACL for the user */
    if (!create_acl(userInfo, &pNewACL, errp)) {
        goto error;
    }

    /* Allocate memory for a security descriptor */
    pSD = g_malloc(SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION)) {
        error_setg_win32(errp, GetLastError(),
                         "Failed to initialize security descriptor");
        goto error;
    }

    /* Associate the security descriptor with the ACL permissions. */
    if (!SetSecurityDescriptorDacl(pSD, TRUE, pNewACL, FALSE)) {
        error_setg_win32(errp, GetLastError(),
                         "Failed to set security descriptor ACL");
        goto error;
    }

    /* Set the security attributes on the folder */
    SECURITY_ATTRIBUTES sAttr;
    sAttr.bInheritHandle = FALSE;
    sAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sAttr.lpSecurityDescriptor = pSD;

    /* Create the directory with the created permissions */
    BOOL created = CreateDirectory(userInfo->sshDirectory, &sAttr);
    if (!created) {
        error_setg_win32(errp, GetLastError(), "failed to create directory %s",
                         userInfo->sshDirectory);
        goto error;
    }

    /* Free memory */
    LocalFree(pNewACL);
    return true;
error:
    LocalFree(pNewACL);
    return false;
}

/*
 * Sets permissions on the authorized_key_file that is created.
 *
 * parameters: userInfo -> Information about the user
 * errp -> error structure that will contain errors upon failure
 * returns: 1 upon success, zero upon failure.
 */
static bool set_file_permissions(PWindowsUserInfo userInfo, Error **errp)
{
    PACL pACL = NULL;
    PSID userPSID = NULL;

    /* Creates the access control structure */
    if (!create_acl(userInfo, &pACL, errp)) {
        goto error;
    }

    /* Get the PSID structure for the user based off the string SID. */
    bool converted = ConvertStringSidToSid(userInfo->SSID, &userPSID);
    if (!converted) {
        error_setg_win32(errp, GetLastError(), "failed to retrieve user %s SID",
                         userInfo->username);
        goto error;
    }

    /* Prevents permissions from being inherited and use the DACL provided. */
    const SE_OBJECT_TYPE securityBitFlags =
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION;

    /* Set the ACL on the file. */
    if (SetNamedSecurityInfo(userInfo->authorizedKeyFile, SE_FILE_OBJECT,
                             securityBitFlags, userPSID, NULL, pACL,
                             NULL) != ERROR_SUCCESS) {
        error_setg_win32(errp, GetLastError(),
                         "failed to set file security for file %s",
                         userInfo->authorizedKeyFile);
        goto error;
    }

    LocalFree(pACL);
    LocalFree(userPSID);
    return true;

error:
    LocalFree(pACL);
    LocalFree(userPSID);

    return false;
}

/*
 * Writes the specified keys to the authenticated keys file.
 * parameters:
 * userInfo: Information about the user we are writing the authkeys file to.
 * authkeys: Array of keys to write to disk
 * errp: Error structure that will contain any errors if they occur.
 * returns: 1 if successful, 0 otherwise.
 */
static bool write_authkeys(WindowsUserInfo *userInfo, GStrv authkeys,
                           Error **errp)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) err = NULL;

    contents = g_strjoinv("\n", authkeys);

    if (!g_file_set_contents(userInfo->authorizedKeyFile, contents, -1, &err)) {
        error_setg(errp, "failed to write to '%s': %s",
                   userInfo->authorizedKeyFile, err->message);
        return false;
    }

    if (!set_file_permissions(userInfo, errp)) {
        return false;
    }

    return true;
}

/*
 * Retrieves information about a Windows user by their username
 *
 * parameters:
 * userInfo -> Double pointer to a WindowsUserInfo structure. Upon success, it
 * will be allocated with information about the user and need to be freed.
 * username -> Name of the user to lookup.
 * errp -> Contains any errors that occur.
 * returns: 1 upon success, 0 upon failure.
 */
static bool get_user_info(PWindowsUserInfo *userInfo, const char *username,
                          Error **errp)
{
    DWORD infoLevel = 4;
    LPUSER_INFO_4 uBuf = NULL;
    g_autofree wchar_t *wideUserName = NULL;
    g_autoptr(GError) gerr = NULL;
    PSID psid = NULL;

    /*
     * Converts a string to a Windows wide string since the GetNetUserInfo
     * function requires it.
     */
    wideUserName = g_utf8_to_utf16(username, -1, NULL, NULL, &gerr);
    if (!wideUserName) {
        goto error;
    }

    /* allocate data */
    PWindowsUserInfo uData = g_new0(WindowsUserInfo, 1);

    /* Set pointer so it can be cleaned up by the callee, even upon error. */
    *userInfo = uData;

    /* Find the information */
    NET_API_STATUS result =
        NetUserGetInfo(NULL, wideUserName, infoLevel, (LPBYTE *)&uBuf);
    if (result != NERR_Success) {
        /* Give a friendlier error message if the user was not found. */
        if (result == NERR_UserNotFound) {
            error_setg(errp, "User %s was not found", username);
            goto error;
        }

        error_setg(errp,
                   "Received unexpected error when asking for user info: Error "
                   "Code %lu",
                   result);
        goto error;
    }

    /* Get information from the buffer returned by NetUserGetInfo. */
    uData->username = g_strdup(username);
    uData->isAdmin = uBuf->usri4_priv == USER_PRIV_ADMIN;
    psid = uBuf->usri4_user_sid;

    char *sidStr = NULL;

    /*
     * We store the string representation of the SID not SID structure in
     * memory. Callees wanting to use the SID structure should call
     * ConvertStringSidToSID.
     */
    if (!ConvertSidToStringSid(psid, &sidStr)) {
        error_setg_win32(errp, GetLastError(),
                         "failed to get SID string for user %s", username);
        goto error;
    }

    /* Store the SSID */
    uData->SSID = sidStr;

    /* Get the SSH folder for the user. */
    char *sshFolder = get_ssh_folder(username, uData->isAdmin, errp);
    if (sshFolder == NULL) {
        goto error;
    }

    /* Get the authorized key file path */
    const char *authorizedKeyFile =
        uData->isAdmin ? AUTHORIZED_KEY_FILE_ADMIN : AUTHORIZED_KEY_FILE;
    char *authorizedKeyPath =
        g_build_filename(sshFolder, authorizedKeyFile, NULL);
    uData->sshDirectory = sshFolder;
    uData->authorizedKeyFile = authorizedKeyPath;

    /* Free */
    NetApiBufferFree(uBuf);
    return true;
error:
    if (uBuf) {
        NetApiBufferFree(uBuf);
    }

    return false;
}

/*
 * Gets the list of authorized keys for a user.
 *
 * parameters:
 * username -> Username to retrieve the keys for.
 * errp -> Error structure that will display any errors through QMP.
 * returns: List of keys associated with the user.
 */
GuestAuthorizedKeys *qmp_guest_ssh_get_authorized_keys(const char *username,
                                                       Error **errp)
{
    GuestAuthorizedKeys *keys = NULL;
    g_auto(GStrv) authKeys = NULL;
    g_autoptr(GuestAuthorizedKeys) ret = NULL;
    g_auto(PWindowsUserInfo) userInfo = NULL;

    /* Gets user information */
    if (!get_user_info(&userInfo, username, errp)) {
        return NULL;
    }

    /* Reads authkeys for the user */
    authKeys = read_authkeys(userInfo->authorizedKeyFile, errp);
    if (authKeys == NULL) {
        return NULL;
    }

    /* Set the GuestAuthorizedKey struct with keys from the file */
    ret = g_new0(GuestAuthorizedKeys, 1);
    for (int i = 0; authKeys[i] != NULL; i++) {
        g_strstrip(authKeys[i]);
        if (!authKeys[i][0] || authKeys[i][0] == '#') {
            continue;
        }

        QAPI_LIST_PREPEND(ret->keys, g_strdup(authKeys[i]));
    }

    /*
     * Steal the pointer because it is up for the callee to deallocate the
     * memory.
     */
    keys = g_steal_pointer(&ret);
    return keys;
}

/*
 * Adds an ssh key for a user.
 *
 * parameters:
 * username -> User to add the SSH key to
 * strList -> Array of keys to add to the list
 * has_reset -> Whether the keys have been reset
 * reset -> Boolean to reset the keys (If this is set the existing list will be
 * cleared) and the other key reset. errp -> Pointer to an error structure that
 * will get returned over QMP if anything goes wrong.
 */
void qmp_guest_ssh_add_authorized_keys(const char *username, strList *keys,
                                       bool has_reset, bool reset, Error **errp)
{
    g_auto(PWindowsUserInfo) userInfo = NULL;
    g_auto(GStrv) authkeys = NULL;
    strList *k;
    size_t nkeys, nauthkeys;

    /* Make sure the keys given are valid */
    if (!check_openssh_pub_keys(keys, &nkeys, errp)) {
        return;
    }

    /* Gets user information */
    if (!get_user_info(&userInfo, username, errp)) {
        return;
    }

    /* Determine whether we should reset the keys */
    reset = has_reset && reset;
    if (!reset) {
        /* Read existing keys into memory */
        authkeys = read_authkeys(userInfo->authorizedKeyFile, NULL);
    }

    /* Check that the SSH key directory exists for the user. */
    if (!g_file_test(userInfo->sshDirectory, G_FILE_TEST_IS_DIR)) {
        BOOL success = create_ssh_directory(userInfo, errp);
        if (!success) {
            return;
        }
    }

    /* Reallocates the buffer to fit the new keys. */
    nauthkeys = authkeys ? g_strv_length(authkeys) : 0;
    authkeys = g_realloc_n(authkeys, nauthkeys + nkeys + 1, sizeof(char *));

    /* zero out the memory for the reallocated buffer */
    memset(authkeys + nauthkeys, 0, (nkeys + 1) * sizeof(char *));

    /* Adds the keys */
    for (k = keys; k != NULL; k = k->next) {
        /* Check that the key doesn't already exist */
        if (g_strv_contains((const gchar *const *)authkeys, k->value)) {
            continue;
        }

        authkeys[nauthkeys++] = g_strdup(k->value);
    }

    /* Write the authkeys to the file. */
    write_authkeys(userInfo, authkeys, errp);
}

/*
 * Removes an SSH key for a user
 *
 * parameters:
 * username -> Username to remove the key from
 * strList -> List of strings to remove
 * errp -> Contains any errors that occur.
 */
void qmp_guest_ssh_remove_authorized_keys(const char *username, strList *keys,
                                          Error **errp)
{
    g_auto(PWindowsUserInfo) userInfo = NULL;
    g_autofree struct passwd *p = NULL;
    g_autofree GStrv new_keys = NULL; /* do not own the strings */
    g_auto(GStrv) authkeys = NULL;
    GStrv a;
    size_t nkeys = 0;

    /* Validates the keys passed in by the user */
    if (!check_openssh_pub_keys(keys, NULL, errp)) {
        return;
    }

    /* Gets user information */
    if (!get_user_info(&userInfo, username, errp)) {
        return;
    }

    /* Reads the authkeys for the user */
    authkeys = read_authkeys(userInfo->authorizedKeyFile, errp);
    if (authkeys == NULL) {
        return;
    }

    /* Create a new buffer to hold the keys */
    new_keys = g_new0(char *, g_strv_length(authkeys) + 1);
    for (a = authkeys; *a != NULL; a++) {
        strList *k;

        /* Filters out keys that are equal to ones the user specified. */
        for (k = keys; k != NULL; k = k->next) {
            if (g_str_equal(k->value, *a)) {
                break;
            }
        }

        if (k != NULL) {
            continue;
        }

        new_keys[nkeys++] = *a;
    }

    /* Write the new authkeys to the file. */
    write_authkeys(userInfo, new_keys, errp);
}

/*
 * QEMU Guest Agent win32-specific command implementations
 *
 * Copyright IBM Corp. 2012
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *  Gal Hammer        <ghammer@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include <wtypes.h>
#include <powrprof.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iptypes.h>
#include <iphlpapi.h>
#ifdef CONFIG_QGA_NTDDSCSI
#include <winioctl.h>
#include <ntddscsi.h>
#include <setupapi.h>
#include <initguid.h>
#endif
#include <lm.h>

#include "qga/guest-agent-core.h"
#include "qga/vss-win32.h"
#include "qga-qmp-commands.h"
#include "qapi/qmp/qerror.h"
#include "qemu/queue.h"
#include "qemu/host-utils.h"
#include "qemu/base64.h"

#ifndef SHTDN_REASON_FLAG_PLANNED
#define SHTDN_REASON_FLAG_PLANNED 0x80000000
#endif

/* multiple of 100 nanoseconds elapsed between windows baseline
 *    (1/1/1601) and Unix Epoch (1/1/1970), accounting for leap years */
#define W32_FT_OFFSET (10000000ULL * 60 * 60 * 24 * \
                       (365 * (1970 - 1601) +       \
                        (1970 - 1601) / 4 - 3))

#define INVALID_SET_FILE_POINTER ((DWORD)-1)

typedef struct GuestFileHandle {
    int64_t id;
    HANDLE fh;
    QTAILQ_ENTRY(GuestFileHandle) next;
} GuestFileHandle;

static struct {
    QTAILQ_HEAD(, GuestFileHandle) filehandles;
} guest_file_state = {
    .filehandles = QTAILQ_HEAD_INITIALIZER(guest_file_state.filehandles),
};

#define FILE_GENERIC_APPEND (FILE_GENERIC_WRITE & ~FILE_WRITE_DATA)

typedef struct OpenFlags {
    const char *forms;
    DWORD desired_access;
    DWORD creation_disposition;
} OpenFlags;
static OpenFlags guest_file_open_modes[] = {
    {"r",   GENERIC_READ,                     OPEN_EXISTING},
    {"rb",  GENERIC_READ,                     OPEN_EXISTING},
    {"w",   GENERIC_WRITE,                    CREATE_ALWAYS},
    {"wb",  GENERIC_WRITE,                    CREATE_ALWAYS},
    {"a",   FILE_GENERIC_APPEND,              OPEN_ALWAYS  },
    {"r+",  GENERIC_WRITE|GENERIC_READ,       OPEN_EXISTING},
    {"rb+", GENERIC_WRITE|GENERIC_READ,       OPEN_EXISTING},
    {"r+b", GENERIC_WRITE|GENERIC_READ,       OPEN_EXISTING},
    {"w+",  GENERIC_WRITE|GENERIC_READ,       CREATE_ALWAYS},
    {"wb+", GENERIC_WRITE|GENERIC_READ,       CREATE_ALWAYS},
    {"w+b", GENERIC_WRITE|GENERIC_READ,       CREATE_ALWAYS},
    {"a+",  FILE_GENERIC_APPEND|GENERIC_READ, OPEN_ALWAYS  },
    {"ab+", FILE_GENERIC_APPEND|GENERIC_READ, OPEN_ALWAYS  },
    {"a+b", FILE_GENERIC_APPEND|GENERIC_READ, OPEN_ALWAYS  }
};

static OpenFlags *find_open_flag(const char *mode_str)
{
    int mode;
    Error **errp = NULL;

    for (mode = 0; mode < ARRAY_SIZE(guest_file_open_modes); ++mode) {
        OpenFlags *flags = guest_file_open_modes + mode;

        if (strcmp(flags->forms, mode_str) == 0) {
            return flags;
        }
    }

    error_setg(errp, "invalid file open mode '%s'", mode_str);
    return NULL;
}

static int64_t guest_file_handle_add(HANDLE fh, Error **errp)
{
    GuestFileHandle *gfh;
    int64_t handle;

    handle = ga_get_fd_handle(ga_state, errp);
    if (handle < 0) {
        return -1;
    }
    gfh = g_new0(GuestFileHandle, 1);
    gfh->id = handle;
    gfh->fh = fh;
    QTAILQ_INSERT_TAIL(&guest_file_state.filehandles, gfh, next);

    return handle;
}

static GuestFileHandle *guest_file_handle_find(int64_t id, Error **errp)
{
    GuestFileHandle *gfh;
    QTAILQ_FOREACH(gfh, &guest_file_state.filehandles, next) {
        if (gfh->id == id) {
            return gfh;
        }
    }
    error_setg(errp, "handle '%" PRId64 "' has not been found", id);
    return NULL;
}

static void handle_set_nonblocking(HANDLE fh)
{
    DWORD file_type, pipe_state;
    file_type = GetFileType(fh);
    if (file_type != FILE_TYPE_PIPE) {
        return;
    }
    /* If file_type == FILE_TYPE_PIPE, according to MSDN
     * the specified file is socket or named pipe */
    if (!GetNamedPipeHandleState(fh, &pipe_state, NULL,
                                 NULL, NULL, NULL, 0)) {
        return;
    }
    /* The fd is named pipe fd */
    if (pipe_state & PIPE_NOWAIT) {
        return;
    }

    pipe_state |= PIPE_NOWAIT;
    SetNamedPipeHandleState(fh, &pipe_state, NULL, NULL);
}

int64_t qmp_guest_file_open(const char *path, bool has_mode,
                            const char *mode, Error **errp)
{
    int64_t fd;
    HANDLE fh;
    HANDLE templ_file = NULL;
    DWORD share_mode = FILE_SHARE_READ;
    DWORD flags_and_attr = FILE_ATTRIBUTE_NORMAL;
    LPSECURITY_ATTRIBUTES sa_attr = NULL;
    OpenFlags *guest_flags;

    if (!has_mode) {
        mode = "r";
    }
    slog("guest-file-open called, filepath: %s, mode: %s", path, mode);
    guest_flags = find_open_flag(mode);
    if (guest_flags == NULL) {
        error_setg(errp, "invalid file open mode");
        return -1;
    }

    fh = CreateFile(path, guest_flags->desired_access, share_mode, sa_attr,
                    guest_flags->creation_disposition, flags_and_attr,
                    templ_file);
    if (fh == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to open file '%s'",
                         path);
        return -1;
    }

    /* set fd non-blocking to avoid common use cases (like reading from a
     * named pipe) from hanging the agent
     */
    handle_set_nonblocking(fh);

    fd = guest_file_handle_add(fh, errp);
    if (fd < 0) {
        CloseHandle(fh);
        error_setg(errp, "failed to add handle to qmp handle table");
        return -1;
    }

    slog("guest-file-open, handle: % " PRId64, fd);
    return fd;
}

void qmp_guest_file_close(int64_t handle, Error **errp)
{
    bool ret;
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);
    slog("guest-file-close called, handle: %" PRId64, handle);
    if (gfh == NULL) {
        return;
    }
    ret = CloseHandle(gfh->fh);
    if (!ret) {
        error_setg_win32(errp, GetLastError(), "failed close handle");
        return;
    }

    QTAILQ_REMOVE(&guest_file_state.filehandles, gfh, next);
    g_free(gfh);
}

static void acquire_privilege(const char *name, Error **errp)
{
    HANDLE token = NULL;
    TOKEN_PRIVILEGES priv;
    Error *local_err = NULL;

    if (OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &token))
    {
        if (!LookupPrivilegeValue(NULL, name, &priv.Privileges[0].Luid)) {
            error_setg(&local_err, QERR_QGA_COMMAND_FAILED,
                       "no luid for requested privilege");
            goto out;
        }

        priv.PrivilegeCount = 1;
        priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (!AdjustTokenPrivileges(token, FALSE, &priv, 0, NULL, 0)) {
            error_setg(&local_err, QERR_QGA_COMMAND_FAILED,
                       "unable to acquire requested privilege");
            goto out;
        }

    } else {
        error_setg(&local_err, QERR_QGA_COMMAND_FAILED,
                   "failed to open privilege token");
    }

out:
    if (token) {
        CloseHandle(token);
    }
    if (local_err) {
        error_propagate(errp, local_err);
    }
}

static void execute_async(DWORD WINAPI (*func)(LPVOID), LPVOID opaque,
                          Error **errp)
{
    Error *local_err = NULL;

    HANDLE thread = CreateThread(NULL, 0, func, opaque, 0, NULL);
    if (!thread) {
        error_setg(&local_err, QERR_QGA_COMMAND_FAILED,
                   "failed to dispatch asynchronous command");
        error_propagate(errp, local_err);
    }
}

void qmp_guest_shutdown(bool has_mode, const char *mode, Error **errp)
{
    Error *local_err = NULL;
    UINT shutdown_flag = EWX_FORCE;

    slog("guest-shutdown called, mode: %s", mode);

    if (!has_mode || strcmp(mode, "powerdown") == 0) {
        shutdown_flag |= EWX_POWEROFF;
    } else if (strcmp(mode, "halt") == 0) {
        shutdown_flag |= EWX_SHUTDOWN;
    } else if (strcmp(mode, "reboot") == 0) {
        shutdown_flag |= EWX_REBOOT;
    } else {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "mode",
                   "halt|powerdown|reboot");
        return;
    }

    /* Request a shutdown privilege, but try to shut down the system
       anyway. */
    acquire_privilege(SE_SHUTDOWN_NAME, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (!ExitWindowsEx(shutdown_flag, SHTDN_REASON_FLAG_PLANNED)) {
        slog("guest-shutdown failed: %lu", GetLastError());
        error_setg(errp, QERR_UNDEFINED_ERROR);
    }
}

GuestFileRead *qmp_guest_file_read(int64_t handle, bool has_count,
                                   int64_t count, Error **errp)
{
    GuestFileRead *read_data = NULL;
    guchar *buf;
    HANDLE fh;
    bool is_ok;
    DWORD read_count;
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);

    if (!gfh) {
        return NULL;
    }
    if (!has_count) {
        count = QGA_READ_COUNT_DEFAULT;
    } else if (count < 0) {
        error_setg(errp, "value '%" PRId64
                   "' is invalid for argument count", count);
        return NULL;
    }

    fh = gfh->fh;
    buf = g_malloc0(count+1);
    is_ok = ReadFile(fh, buf, count, &read_count, NULL);
    if (!is_ok) {
        error_setg_win32(errp, GetLastError(), "failed to read file");
        slog("guest-file-read failed, handle %" PRId64, handle);
    } else {
        buf[read_count] = 0;
        read_data = g_new0(GuestFileRead, 1);
        read_data->count = (size_t)read_count;
        read_data->eof = read_count == 0;

        if (read_count != 0) {
            read_data->buf_b64 = g_base64_encode(buf, read_count);
        }
    }
    g_free(buf);

    return read_data;
}

GuestFileWrite *qmp_guest_file_write(int64_t handle, const char *buf_b64,
                                     bool has_count, int64_t count,
                                     Error **errp)
{
    GuestFileWrite *write_data = NULL;
    guchar *buf;
    gsize buf_len;
    bool is_ok;
    DWORD write_count;
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);
    HANDLE fh;

    if (!gfh) {
        return NULL;
    }
    fh = gfh->fh;
    buf = qbase64_decode(buf_b64, -1, &buf_len, errp);
    if (!buf) {
        return NULL;
    }

    if (!has_count) {
        count = buf_len;
    } else if (count < 0 || count > buf_len) {
        error_setg(errp, "value '%" PRId64
                   "' is invalid for argument count", count);
        goto done;
    }

    is_ok = WriteFile(fh, buf, count, &write_count, NULL);
    if (!is_ok) {
        error_setg_win32(errp, GetLastError(), "failed to write to file");
        slog("guest-file-write-failed, handle: %" PRId64, handle);
    } else {
        write_data = g_new0(GuestFileWrite, 1);
        write_data->count = (size_t) write_count;
    }

done:
    g_free(buf);
    return write_data;
}

GuestFileSeek *qmp_guest_file_seek(int64_t handle, int64_t offset,
                                   GuestFileWhence *whence_code,
                                   Error **errp)
{
    GuestFileHandle *gfh;
    GuestFileSeek *seek_data;
    HANDLE fh;
    LARGE_INTEGER new_pos, off_pos;
    off_pos.QuadPart = offset;
    BOOL res;
    int whence;
    Error *err = NULL;

    gfh = guest_file_handle_find(handle, errp);
    if (!gfh) {
        return NULL;
    }

    /* We stupidly exposed 'whence':'int' in our qapi */
    whence = ga_parse_whence(whence_code, &err);
    if (err) {
        error_propagate(errp, err);
        return NULL;
    }

    fh = gfh->fh;
    res = SetFilePointerEx(fh, off_pos, &new_pos, whence);
    if (!res) {
        error_setg_win32(errp, GetLastError(), "failed to seek file");
        return NULL;
    }
    seek_data = g_new0(GuestFileSeek, 1);
    seek_data->position = new_pos.QuadPart;
    return seek_data;
}

void qmp_guest_file_flush(int64_t handle, Error **errp)
{
    HANDLE fh;
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);
    if (!gfh) {
        return;
    }

    fh = gfh->fh;
    if (!FlushFileBuffers(fh)) {
        error_setg_win32(errp, GetLastError(), "failed to flush file");
    }
}

#ifdef CONFIG_QGA_NTDDSCSI

static STORAGE_BUS_TYPE win2qemu[] = {
    [BusTypeUnknown] = GUEST_DISK_BUS_TYPE_UNKNOWN,
    [BusTypeScsi] = GUEST_DISK_BUS_TYPE_SCSI,
    [BusTypeAtapi] = GUEST_DISK_BUS_TYPE_IDE,
    [BusTypeAta] = GUEST_DISK_BUS_TYPE_IDE,
    [BusType1394] = GUEST_DISK_BUS_TYPE_IEEE1394,
    [BusTypeSsa] = GUEST_DISK_BUS_TYPE_SSA,
    [BusTypeFibre] = GUEST_DISK_BUS_TYPE_SSA,
    [BusTypeUsb] = GUEST_DISK_BUS_TYPE_USB,
    [BusTypeRAID] = GUEST_DISK_BUS_TYPE_RAID,
#if (_WIN32_WINNT >= 0x0600)
    [BusTypeiScsi] = GUEST_DISK_BUS_TYPE_ISCSI,
    [BusTypeSas] = GUEST_DISK_BUS_TYPE_SAS,
    [BusTypeSata] = GUEST_DISK_BUS_TYPE_SATA,
    [BusTypeSd] =  GUEST_DISK_BUS_TYPE_SD,
    [BusTypeMmc] = GUEST_DISK_BUS_TYPE_MMC,
#endif
#if (_WIN32_WINNT >= 0x0601)
    [BusTypeVirtual] = GUEST_DISK_BUS_TYPE_VIRTUAL,
    [BusTypeFileBackedVirtual] = GUEST_DISK_BUS_TYPE_FILE_BACKED_VIRTUAL,
#endif
};

static GuestDiskBusType find_bus_type(STORAGE_BUS_TYPE bus)
{
    if (bus > ARRAY_SIZE(win2qemu) || (int)bus < 0) {
        return GUEST_DISK_BUS_TYPE_UNKNOWN;
    }
    return win2qemu[(int)bus];
}

DEFINE_GUID(GUID_DEVINTERFACE_VOLUME,
        0x53f5630dL, 0xb6bf, 0x11d0, 0x94, 0xf2,
        0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

static GuestPCIAddress *get_pci_info(char *guid, Error **errp)
{
    HDEVINFO dev_info;
    SP_DEVINFO_DATA dev_info_data;
    DWORD size = 0;
    int i;
    char dev_name[MAX_PATH];
    char *buffer = NULL;
    GuestPCIAddress *pci = NULL;
    char *name = g_strdup(&guid[4]);

    if (!QueryDosDevice(name, dev_name, ARRAY_SIZE(dev_name))) {
        error_setg_win32(errp, GetLastError(), "failed to get dos device name");
        goto out;
    }

    dev_info = SetupDiGetClassDevs(&GUID_DEVINTERFACE_VOLUME, 0, 0,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (dev_info == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to get devices tree");
        goto out;
    }

    dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
    for (i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
        DWORD addr, bus, slot, func, dev, data, size2;
        while (!SetupDiGetDeviceRegistryProperty(dev_info, &dev_info_data,
                                            SPDRP_PHYSICAL_DEVICE_OBJECT_NAME,
                                            &data, (PBYTE)buffer, size,
                                            &size2)) {
            size = MAX(size, size2);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                g_free(buffer);
                /* Double the size to avoid problems on
                 * W2k MBCS systems per KB 888609.
                 * https://support.microsoft.com/en-us/kb/259695 */
                buffer = g_malloc(size * 2);
            } else {
                error_setg_win32(errp, GetLastError(),
                        "failed to get device name");
                goto out;
            }
        }

        if (g_strcmp0(buffer, dev_name)) {
            continue;
        }

        /* There is no need to allocate buffer in the next functions. The size
         * is known and ULONG according to
         * https://support.microsoft.com/en-us/kb/253232
         * https://msdn.microsoft.com/en-us/library/windows/hardware/ff543095(v=vs.85).aspx
         */
        if (!SetupDiGetDeviceRegistryProperty(dev_info, &dev_info_data,
                   SPDRP_BUSNUMBER, &data, (PBYTE)&bus, size, NULL)) {
            break;
        }

        /* The function retrieves the device's address. This value will be
         * transformed into device function and number */
        if (!SetupDiGetDeviceRegistryProperty(dev_info, &dev_info_data,
                   SPDRP_ADDRESS, &data, (PBYTE)&addr, size, NULL)) {
            break;
        }

        /* This call returns UINumber of DEVICE_CAPABILITIES structure.
         * This number is typically a user-perceived slot number. */
        if (!SetupDiGetDeviceRegistryProperty(dev_info, &dev_info_data,
                   SPDRP_UI_NUMBER, &data, (PBYTE)&slot, size, NULL)) {
            break;
        }

        /* SetupApi gives us the same information as driver with
         * IoGetDeviceProperty. According to Microsoft
         * https://support.microsoft.com/en-us/kb/253232
         * FunctionNumber = (USHORT)((propertyAddress) & 0x0000FFFF);
         * DeviceNumber = (USHORT)(((propertyAddress) >> 16) & 0x0000FFFF);
         * SPDRP_ADDRESS is propertyAddress, so we do the same.*/

        func = addr & 0x0000FFFF;
        dev = (addr >> 16) & 0x0000FFFF;
        pci = g_malloc0(sizeof(*pci));
        pci->domain = dev;
        pci->slot = slot;
        pci->function = func;
        pci->bus = bus;
        break;
    }
out:
    g_free(buffer);
    g_free(name);
    return pci;
}

static int get_disk_bus_type(HANDLE vol_h, Error **errp)
{
    STORAGE_PROPERTY_QUERY query;
    STORAGE_DEVICE_DESCRIPTOR *dev_desc, buf;
    DWORD received;

    dev_desc = &buf;
    dev_desc->Size = sizeof(buf);
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    if (!DeviceIoControl(vol_h, IOCTL_STORAGE_QUERY_PROPERTY, &query,
                         sizeof(STORAGE_PROPERTY_QUERY), dev_desc,
                         dev_desc->Size, &received, NULL)) {
        error_setg_win32(errp, GetLastError(), "failed to get bus type");
        return -1;
    }

    return dev_desc->BusType;
}

/* VSS provider works with volumes, thus there is no difference if
 * the volume consist of spanned disks. Info about the first disk in the
 * volume is returned for the spanned disk group (LVM) */
static GuestDiskAddressList *build_guest_disk_info(char *guid, Error **errp)
{
    GuestDiskAddressList *list = NULL;
    GuestDiskAddress *disk;
    SCSI_ADDRESS addr, *scsi_ad;
    DWORD len;
    int bus;
    HANDLE vol_h;

    scsi_ad = &addr;
    char *name = g_strndup(guid, strlen(guid)-1);

    vol_h = CreateFile(name, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       0, NULL);
    if (vol_h == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to open volume");
        goto out_free;
    }

    bus = get_disk_bus_type(vol_h, errp);
    if (bus < 0) {
        goto out_close;
    }

    disk = g_malloc0(sizeof(*disk));
    disk->bus_type = find_bus_type(bus);
    if (bus == BusTypeScsi || bus == BusTypeAta || bus == BusTypeRAID
#if (_WIN32_WINNT >= 0x0600)
            /* This bus type is not supported before Windows Server 2003 SP1 */
            || bus == BusTypeSas
#endif
        ) {
        /* We are able to use the same ioctls for different bus types
         * according to Microsoft docs
         * https://technet.microsoft.com/en-us/library/ee851589(v=ws.10).aspx */
        if (DeviceIoControl(vol_h, IOCTL_SCSI_GET_ADDRESS, NULL, 0, scsi_ad,
                            sizeof(SCSI_ADDRESS), &len, NULL)) {
            disk->unit = addr.Lun;
            disk->target = addr.TargetId;
            disk->bus = addr.PathId;
            disk->pci_controller = get_pci_info(name, errp);
        }
        /* We do not set error in this case, because we still have enough
         * information about volume. */
    } else {
         disk->pci_controller = NULL;
    }

    list = g_malloc0(sizeof(*list));
    list->value = disk;
    list->next = NULL;
out_close:
    CloseHandle(vol_h);
out_free:
    g_free(name);
    return list;
}

#else

static GuestDiskAddressList *build_guest_disk_info(char *guid, Error **errp)
{
    return NULL;
}

#endif /* CONFIG_QGA_NTDDSCSI */

static GuestFilesystemInfo *build_guest_fsinfo(char *guid, Error **errp)
{
    DWORD info_size;
    char mnt, *mnt_point;
    char fs_name[32];
    char vol_info[MAX_PATH+1];
    size_t len;
    GuestFilesystemInfo *fs = NULL;

    GetVolumePathNamesForVolumeName(guid, (LPCH)&mnt, 0, &info_size);
    if (GetLastError() != ERROR_MORE_DATA) {
        error_setg_win32(errp, GetLastError(), "failed to get volume name");
        return NULL;
    }

    mnt_point = g_malloc(info_size + 1);
    if (!GetVolumePathNamesForVolumeName(guid, mnt_point, info_size,
                                         &info_size)) {
        error_setg_win32(errp, GetLastError(), "failed to get volume name");
        goto free;
    }

    len = strlen(mnt_point);
    mnt_point[len] = '\\';
    mnt_point[len+1] = 0;
    if (!GetVolumeInformation(mnt_point, vol_info, sizeof(vol_info), NULL, NULL,
                              NULL, (LPSTR)&fs_name, sizeof(fs_name))) {
        if (GetLastError() != ERROR_NOT_READY) {
            error_setg_win32(errp, GetLastError(), "failed to get volume info");
        }
        goto free;
    }

    fs_name[sizeof(fs_name) - 1] = 0;
    fs = g_malloc(sizeof(*fs));
    fs->name = g_strdup(guid);
    if (len == 0) {
        fs->mountpoint = g_strdup("System Reserved");
    } else {
        fs->mountpoint = g_strndup(mnt_point, len);
    }
    fs->type = g_strdup(fs_name);
    fs->disk = build_guest_disk_info(guid, errp);
free:
    g_free(mnt_point);
    return fs;
}

GuestFilesystemInfoList *qmp_guest_get_fsinfo(Error **errp)
{
    HANDLE vol_h;
    GuestFilesystemInfoList *new, *ret = NULL;
    char guid[256];

    vol_h = FindFirstVolume(guid, sizeof(guid));
    if (vol_h == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to find any volume");
        return NULL;
    }

    do {
        GuestFilesystemInfo *info = build_guest_fsinfo(guid, errp);
        if (info == NULL) {
            continue;
        }
        new = g_malloc(sizeof(*ret));
        new->value = info;
        new->next = ret;
        ret = new;
    } while (FindNextVolume(vol_h, guid, sizeof(guid)));

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        error_setg_win32(errp, GetLastError(), "failed to find next volume");
    }

    FindVolumeClose(vol_h);
    return ret;
}

/*
 * Return status of freeze/thaw
 */
GuestFsfreezeStatus qmp_guest_fsfreeze_status(Error **errp)
{
    if (!vss_initialized()) {
        error_setg(errp, QERR_UNSUPPORTED);
        return 0;
    }

    if (ga_is_frozen(ga_state)) {
        return GUEST_FSFREEZE_STATUS_FROZEN;
    }

    return GUEST_FSFREEZE_STATUS_THAWED;
}

/*
 * Freeze local file systems using Volume Shadow-copy Service.
 * The frozen state is limited for up to 10 seconds by VSS.
 */
int64_t qmp_guest_fsfreeze_freeze(Error **errp)
{
    int i;
    Error *local_err = NULL;

    if (!vss_initialized()) {
        error_setg(errp, QERR_UNSUPPORTED);
        return 0;
    }

    slog("guest-fsfreeze called");

    /* cannot risk guest agent blocking itself on a write in this state */
    ga_set_frozen(ga_state);

    qga_vss_fsfreeze(&i, &local_err, true);
    if (local_err) {
        error_propagate(errp, local_err);
        goto error;
    }

    return i;

error:
    local_err = NULL;
    qmp_guest_fsfreeze_thaw(&local_err);
    if (local_err) {
        g_debug("cleanup thaw: %s", error_get_pretty(local_err));
        error_free(local_err);
    }
    return 0;
}

int64_t qmp_guest_fsfreeze_freeze_list(bool has_mountpoints,
                                       strList *mountpoints,
                                       Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);

    return 0;
}

/*
 * Thaw local file systems using Volume Shadow-copy Service.
 */
int64_t qmp_guest_fsfreeze_thaw(Error **errp)
{
    int i;

    if (!vss_initialized()) {
        error_setg(errp, QERR_UNSUPPORTED);
        return 0;
    }

    qga_vss_fsfreeze(&i, errp, false);

    ga_unset_frozen(ga_state);
    return i;
}

static void guest_fsfreeze_cleanup(void)
{
    Error *err = NULL;

    if (!vss_initialized()) {
        return;
    }

    if (ga_is_frozen(ga_state) == GUEST_FSFREEZE_STATUS_FROZEN) {
        qmp_guest_fsfreeze_thaw(&err);
        if (err) {
            slog("failed to clean up frozen filesystems: %s",
                 error_get_pretty(err));
            error_free(err);
        }
    }

    vss_deinit(true);
}

/*
 * Walk list of mounted file systems in the guest, and discard unused
 * areas.
 */
GuestFilesystemTrimResponse *
qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

typedef enum {
    GUEST_SUSPEND_MODE_DISK,
    GUEST_SUSPEND_MODE_RAM
} GuestSuspendMode;

static void check_suspend_mode(GuestSuspendMode mode, Error **errp)
{
    SYSTEM_POWER_CAPABILITIES sys_pwr_caps;
    Error *local_err = NULL;

    ZeroMemory(&sys_pwr_caps, sizeof(sys_pwr_caps));
    if (!GetPwrCapabilities(&sys_pwr_caps)) {
        error_setg(&local_err, QERR_QGA_COMMAND_FAILED,
                   "failed to determine guest suspend capabilities");
        goto out;
    }

    switch (mode) {
    case GUEST_SUSPEND_MODE_DISK:
        if (!sys_pwr_caps.SystemS4) {
            error_setg(&local_err, QERR_QGA_COMMAND_FAILED,
                       "suspend-to-disk not supported by OS");
        }
        break;
    case GUEST_SUSPEND_MODE_RAM:
        if (!sys_pwr_caps.SystemS3) {
            error_setg(&local_err, QERR_QGA_COMMAND_FAILED,
                       "suspend-to-ram not supported by OS");
        }
        break;
    default:
        error_setg(&local_err, QERR_INVALID_PARAMETER_VALUE, "mode",
                   "GuestSuspendMode");
    }

out:
    if (local_err) {
        error_propagate(errp, local_err);
    }
}

static DWORD WINAPI do_suspend(LPVOID opaque)
{
    GuestSuspendMode *mode = opaque;
    DWORD ret = 0;

    if (!SetSuspendState(*mode == GUEST_SUSPEND_MODE_DISK, TRUE, TRUE)) {
        slog("failed to suspend guest, %lu", GetLastError());
        ret = -1;
    }
    g_free(mode);
    return ret;
}

void qmp_guest_suspend_disk(Error **errp)
{
    Error *local_err = NULL;
    GuestSuspendMode *mode = g_new(GuestSuspendMode, 1);

    *mode = GUEST_SUSPEND_MODE_DISK;
    check_suspend_mode(*mode, &local_err);
    acquire_privilege(SE_SHUTDOWN_NAME, &local_err);
    execute_async(do_suspend, mode, &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        g_free(mode);
    }
}

void qmp_guest_suspend_ram(Error **errp)
{
    Error *local_err = NULL;
    GuestSuspendMode *mode = g_new(GuestSuspendMode, 1);

    *mode = GUEST_SUSPEND_MODE_RAM;
    check_suspend_mode(*mode, &local_err);
    acquire_privilege(SE_SHUTDOWN_NAME, &local_err);
    execute_async(do_suspend, mode, &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        g_free(mode);
    }
}

void qmp_guest_suspend_hybrid(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}

static IP_ADAPTER_ADDRESSES *guest_get_adapters_addresses(Error **errp)
{
    IP_ADAPTER_ADDRESSES *adptr_addrs = NULL;
    ULONG adptr_addrs_len = 0;
    DWORD ret;

    /* Call the first time to get the adptr_addrs_len. */
    GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX,
                         NULL, adptr_addrs, &adptr_addrs_len);

    adptr_addrs = g_malloc(adptr_addrs_len);
    ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX,
                               NULL, adptr_addrs, &adptr_addrs_len);
    if (ret != ERROR_SUCCESS) {
        error_setg_win32(errp, ret, "failed to get adapters addresses");
        g_free(adptr_addrs);
        adptr_addrs = NULL;
    }
    return adptr_addrs;
}

static char *guest_wctomb_dup(WCHAR *wstr)
{
    char *str;
    size_t i;

    i = wcslen(wstr) + 1;
    str = g_malloc(i);
    WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK,
                        wstr, -1, str, i, NULL, NULL);
    return str;
}

static char *guest_addr_to_str(IP_ADAPTER_UNICAST_ADDRESS *ip_addr,
                               Error **errp)
{
    char addr_str[INET6_ADDRSTRLEN + INET_ADDRSTRLEN];
    DWORD len;
    int ret;

    if (ip_addr->Address.lpSockaddr->sa_family == AF_INET ||
            ip_addr->Address.lpSockaddr->sa_family == AF_INET6) {
        len = sizeof(addr_str);
        ret = WSAAddressToString(ip_addr->Address.lpSockaddr,
                                 ip_addr->Address.iSockaddrLength,
                                 NULL,
                                 addr_str,
                                 &len);
        if (ret != 0) {
            error_setg_win32(errp, WSAGetLastError(),
                "failed address presentation form conversion");
            return NULL;
        }
        return g_strdup(addr_str);
    }
    return NULL;
}

#if (_WIN32_WINNT >= 0x0600)
static int64_t guest_ip_prefix(IP_ADAPTER_UNICAST_ADDRESS *ip_addr)
{
    /* For Windows Vista/2008 and newer, use the OnLinkPrefixLength
     * field to obtain the prefix.
     */
    return ip_addr->OnLinkPrefixLength;
}
#else
/* When using the Windows XP and 2003 build environment, do the best we can to
 * figure out the prefix.
 */
static IP_ADAPTER_INFO *guest_get_adapters_info(void)
{
    IP_ADAPTER_INFO *adptr_info = NULL;
    ULONG adptr_info_len = 0;
    DWORD ret;

    /* Call the first time to get the adptr_info_len. */
    GetAdaptersInfo(adptr_info, &adptr_info_len);

    adptr_info = g_malloc(adptr_info_len);
    ret = GetAdaptersInfo(adptr_info, &adptr_info_len);
    if (ret != ERROR_SUCCESS) {
        g_free(adptr_info);
        adptr_info = NULL;
    }
    return adptr_info;
}

static int64_t guest_ip_prefix(IP_ADAPTER_UNICAST_ADDRESS *ip_addr)
{
    int64_t prefix = -1; /* Use for AF_INET6 and unknown/undetermined values. */
    IP_ADAPTER_INFO *adptr_info, *info;
    IP_ADDR_STRING *ip;
    struct in_addr *p;

    if (ip_addr->Address.lpSockaddr->sa_family != AF_INET) {
        return prefix;
    }
    adptr_info = guest_get_adapters_info();
    if (adptr_info == NULL) {
        return prefix;
    }

    /* Match up the passed in ip_addr with one found in adaptr_info.
     * The matching one in adptr_info will have the netmask.
     */
    p = &((struct sockaddr_in *)ip_addr->Address.lpSockaddr)->sin_addr;
    for (info = adptr_info; info; info = info->Next) {
        for (ip = &info->IpAddressList; ip; ip = ip->Next) {
            if (p->S_un.S_addr == inet_addr(ip->IpAddress.String)) {
                prefix = ctpop32(inet_addr(ip->IpMask.String));
                goto out;
            }
        }
    }
out:
    g_free(adptr_info);
    return prefix;
}
#endif

GuestNetworkInterfaceList *qmp_guest_network_get_interfaces(Error **errp)
{
    IP_ADAPTER_ADDRESSES *adptr_addrs, *addr;
    IP_ADAPTER_UNICAST_ADDRESS *ip_addr = NULL;
    GuestNetworkInterfaceList *head = NULL, *cur_item = NULL;
    GuestIpAddressList *head_addr, *cur_addr;
    GuestNetworkInterfaceList *info;
    GuestIpAddressList *address_item = NULL;
    unsigned char *mac_addr;
    char *addr_str;
    WORD wsa_version;
    WSADATA wsa_data;
    int ret;

    adptr_addrs = guest_get_adapters_addresses(errp);
    if (adptr_addrs == NULL) {
        return NULL;
    }

    /* Make WSA APIs available. */
    wsa_version = MAKEWORD(2, 2);
    ret = WSAStartup(wsa_version, &wsa_data);
    if (ret != 0) {
        error_setg_win32(errp, ret, "failed socket startup");
        goto out;
    }

    for (addr = adptr_addrs; addr; addr = addr->Next) {
        info = g_malloc0(sizeof(*info));

        if (cur_item == NULL) {
            head = cur_item = info;
        } else {
            cur_item->next = info;
            cur_item = info;
        }

        info->value = g_malloc0(sizeof(*info->value));
        info->value->name = guest_wctomb_dup(addr->FriendlyName);

        if (addr->PhysicalAddressLength != 0) {
            mac_addr = addr->PhysicalAddress;

            info->value->hardware_address =
                g_strdup_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                                (int) mac_addr[0], (int) mac_addr[1],
                                (int) mac_addr[2], (int) mac_addr[3],
                                (int) mac_addr[4], (int) mac_addr[5]);

            info->value->has_hardware_address = true;
        }

        head_addr = NULL;
        cur_addr = NULL;
        for (ip_addr = addr->FirstUnicastAddress;
                ip_addr;
                ip_addr = ip_addr->Next) {
            addr_str = guest_addr_to_str(ip_addr, errp);
            if (addr_str == NULL) {
                continue;
            }

            address_item = g_malloc0(sizeof(*address_item));

            if (!cur_addr) {
                head_addr = cur_addr = address_item;
            } else {
                cur_addr->next = address_item;
                cur_addr = address_item;
            }

            address_item->value = g_malloc0(sizeof(*address_item->value));
            address_item->value->ip_address = addr_str;
            address_item->value->prefix = guest_ip_prefix(ip_addr);
            if (ip_addr->Address.lpSockaddr->sa_family == AF_INET) {
                address_item->value->ip_address_type =
                    GUEST_IP_ADDRESS_TYPE_IPV4;
            } else if (ip_addr->Address.lpSockaddr->sa_family == AF_INET6) {
                address_item->value->ip_address_type =
                    GUEST_IP_ADDRESS_TYPE_IPV6;
            }
        }
        if (head_addr) {
            info->value->has_ip_addresses = true;
            info->value->ip_addresses = head_addr;
        }
    }
    WSACleanup();
out:
    g_free(adptr_addrs);
    return head;
}

int64_t qmp_guest_get_time(Error **errp)
{
    SYSTEMTIME ts = {0};
    int64_t time_ns;
    FILETIME tf;

    GetSystemTime(&ts);
    if (ts.wYear < 1601 || ts.wYear > 30827) {
        error_setg(errp, "Failed to get time");
        return -1;
    }

    if (!SystemTimeToFileTime(&ts, &tf)) {
        error_setg(errp, "Failed to convert system time: %d", (int)GetLastError());
        return -1;
    }

    time_ns = ((((int64_t)tf.dwHighDateTime << 32) | tf.dwLowDateTime)
                - W32_FT_OFFSET) * 100;

    return time_ns;
}

void qmp_guest_set_time(bool has_time, int64_t time_ns, Error **errp)
{
    Error *local_err = NULL;
    SYSTEMTIME ts;
    FILETIME tf;
    LONGLONG time;

    if (!has_time) {
        /* Unfortunately, Windows libraries don't provide an easy way to access
         * RTC yet:
         *
         * https://msdn.microsoft.com/en-us/library/aa908981.aspx
         */
        error_setg(errp, "Time argument is required on this platform");
        return;
    }

    /* Validate time passed by user. */
    if (time_ns < 0 || time_ns / 100 > INT64_MAX - W32_FT_OFFSET) {
        error_setg(errp, "Time %" PRId64 "is invalid", time_ns);
        return;
    }

    time = time_ns / 100 + W32_FT_OFFSET;

    tf.dwLowDateTime = (DWORD) time;
    tf.dwHighDateTime = (DWORD) (time >> 32);

    if (!FileTimeToSystemTime(&tf, &ts)) {
        error_setg(errp, "Failed to convert system time %d",
                   (int)GetLastError());
        return;
    }

    acquire_privilege(SE_SYSTEMTIME_NAME, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (!SetSystemTime(&ts)) {
        error_setg(errp, "Failed to set time to guest: %d", (int)GetLastError());
        return;
    }
}

GuestLogicalProcessorList *qmp_guest_get_vcpus(Error **errp)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION pslpi, ptr;
    DWORD length;
    GuestLogicalProcessorList *head, **link;
    Error *local_err = NULL;
    int64_t current;

    ptr = pslpi = NULL;
    length = 0;
    current = 0;
    head = NULL;
    link = &head;

    if ((GetLogicalProcessorInformation(pslpi, &length) == FALSE) &&
        (GetLastError() == ERROR_INSUFFICIENT_BUFFER) &&
        (length > sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION))) {
        ptr = pslpi = g_malloc0(length);
        if (GetLogicalProcessorInformation(pslpi, &length) == FALSE) {
            error_setg(&local_err, "Failed to get processor information: %d",
                       (int)GetLastError());
        }
    } else {
        error_setg(&local_err,
                   "Failed to get processor information buffer length: %d",
                   (int)GetLastError());
    }

    while ((local_err == NULL) && (length > 0)) {
        if (pslpi->Relationship == RelationProcessorCore) {
            ULONG_PTR cpu_bits = pslpi->ProcessorMask;

            while (cpu_bits > 0) {
                if (!!(cpu_bits & 1)) {
                    GuestLogicalProcessor *vcpu;
                    GuestLogicalProcessorList *entry;

                    vcpu = g_malloc0(sizeof *vcpu);
                    vcpu->logical_id = current++;
                    vcpu->online = true;
                    vcpu->has_can_offline = false;

                    entry = g_malloc0(sizeof *entry);
                    entry->value = vcpu;

                    *link = entry;
                    link = &entry->next;
                }
                cpu_bits >>= 1;
            }
        }
        length -= sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        pslpi++; /* next entry */
    }

    g_free(ptr);

    if (local_err == NULL) {
        if (head != NULL) {
            return head;
        }
        /* there's no guest with zero VCPUs */
        error_setg(&local_err, "Guest reported zero VCPUs");
    }

    qapi_free_GuestLogicalProcessorList(head);
    error_propagate(errp, local_err);
    return NULL;
}

int64_t qmp_guest_set_vcpus(GuestLogicalProcessorList *vcpus, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return -1;
}

static gchar *
get_net_error_message(gint error)
{
    HMODULE module = NULL;
    gchar *retval = NULL;
    wchar_t *msg = NULL;
    int flags;
    size_t nchars;

    flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_IGNORE_INSERTS |
        FORMAT_MESSAGE_FROM_SYSTEM;

    if (error >= NERR_BASE && error <= MAX_NERR) {
        module = LoadLibraryExW(L"netmsg.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);

        if (module != NULL) {
            flags |= FORMAT_MESSAGE_FROM_HMODULE;
        }
    }

    FormatMessageW(flags, module, error, 0, (LPWSTR)&msg, 0, NULL);

    if (msg != NULL) {
        nchars = wcslen(msg);

        if (nchars >= 2 &&
            msg[nchars - 1] == L'\n' &&
            msg[nchars - 2] == L'\r') {
            msg[nchars - 2] = L'\0';
        }

        retval = g_utf16_to_utf8(msg, -1, NULL, NULL, NULL);

        LocalFree(msg);
    }

    if (module != NULL) {
        FreeLibrary(module);
    }

    return retval;
}

void qmp_guest_set_user_password(const char *username,
                                 const char *password,
                                 bool crypted,
                                 Error **errp)
{
    NET_API_STATUS nas;
    char *rawpasswddata = NULL;
    size_t rawpasswdlen;
    wchar_t *user = NULL, *wpass = NULL;
    USER_INFO_1003 pi1003 = { 0, };
    GError *gerr = NULL;

    if (crypted) {
        error_setg(errp, QERR_UNSUPPORTED);
        return;
    }

    rawpasswddata = (char *)qbase64_decode(password, -1, &rawpasswdlen, errp);
    if (!rawpasswddata) {
        return;
    }
    rawpasswddata = g_renew(char, rawpasswddata, rawpasswdlen + 1);
    rawpasswddata[rawpasswdlen] = '\0';

    user = g_utf8_to_utf16(username, -1, NULL, NULL, &gerr);
    if (!user) {
        goto done;
    }

    wpass = g_utf8_to_utf16(rawpasswddata, -1, NULL, NULL, &gerr);
    if (!wpass) {
        goto done;
    }

    pi1003.usri1003_password = wpass;
    nas = NetUserSetInfo(NULL, user,
                         1003, (LPBYTE)&pi1003,
                         NULL);

    if (nas != NERR_Success) {
        gchar *msg = get_net_error_message(nas);
        error_setg(errp, "failed to set password: %s", msg);
        g_free(msg);
    }

done:
    if (gerr) {
        error_setg(errp, QERR_QGA_COMMAND_FAILED, gerr->message);
        g_error_free(gerr);
    }
    g_free(user);
    g_free(wpass);
    g_free(rawpasswddata);
}

GuestMemoryBlockList *qmp_guest_get_memory_blocks(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestMemoryBlockResponseList *
qmp_guest_set_memory_blocks(GuestMemoryBlockList *mem_blks, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestMemoryBlockInfo *qmp_guest_get_memory_block_info(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

/* add unsupported commands to the blacklist */
GList *ga_command_blacklist_init(GList *blacklist)
{
    const char *list_unsupported[] = {
        "guest-suspend-hybrid",
        "guest-set-vcpus",
        "guest-get-memory-blocks", "guest-set-memory-blocks",
        "guest-get-memory-block-size",
        "guest-fsfreeze-freeze-list",
        "guest-fstrim", NULL};
    char **p = (char **)list_unsupported;

    while (*p) {
        blacklist = g_list_append(blacklist, g_strdup(*p++));
    }

    if (!vss_init(true)) {
        g_debug("vss_init failed, vss commands are going to be disabled");
        const char *list[] = {
            "guest-get-fsinfo", "guest-fsfreeze-status",
            "guest-fsfreeze-freeze", "guest-fsfreeze-thaw", NULL};
        p = (char **)list;

        while (*p) {
            blacklist = g_list_append(blacklist, g_strdup(*p++));
        }
    }

    return blacklist;
}

/* register init/cleanup routines for stateful command groups */
void ga_command_state_init(GAState *s, GACommandState *cs)
{
    if (!vss_initialized()) {
        ga_command_state_add(cs, NULL, guest_fsfreeze_cleanup);
    }
}

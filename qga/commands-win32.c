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

#include <wtypes.h>
#include <powrprof.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iptypes.h>
#include <iphlpapi.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpropdef.h>
#include <lm.h>
#include <wtsapi32.h>
#include <wininet.h>

#include "guest-agent-core.h"
#include "vss-win32.h"
#include "qga-qapi-commands.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/queue.h"
#include "qemu/host-utils.h"
#include "qemu/base64.h"
#include "commands-common.h"

/*
 * The following should be in devpkey.h, but it isn't. The key names were
 * prefixed to avoid (future) name clashes. Once the definitions get into
 * mingw the following lines can be removed.
 */
DEFINE_DEVPROPKEY(qga_DEVPKEY_NAME, 0xb725f130, 0x47ef, 0x101a, 0xa5,
    0xf1, 0x02, 0x60, 0x8c, 0x9e, 0xeb, 0xac, 10);
    /* DEVPROP_TYPE_STRING */
DEFINE_DEVPROPKEY(qga_DEVPKEY_Device_HardwareIds, 0xa45c254e, 0xdf1c,
    0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 3);
    /* DEVPROP_TYPE_STRING_LIST */
DEFINE_DEVPROPKEY(qga_DEVPKEY_Device_DriverDate, 0xa8b865dd, 0x2e3d,
    0x4094, 0xad, 0x97, 0xe5, 0x93, 0xa7, 0xc, 0x75, 0xd6, 2);
    /* DEVPROP_TYPE_FILETIME */
DEFINE_DEVPROPKEY(qga_DEVPKEY_Device_DriverVersion, 0xa8b865dd, 0x2e3d,
    0x4094, 0xad, 0x97, 0xe5, 0x93, 0xa7, 0xc, 0x75, 0xd6, 3);
    /* DEVPROP_TYPE_STRING */
/* The CM_Get_DevNode_PropertyW prototype is only sometimes in cfgmgr32.h */
#ifndef CM_Get_DevNode_Property
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
CMAPI CONFIGRET WINAPI CM_Get_DevNode_PropertyW(
    DEVINST          dnDevInst,
    CONST DEVPROPKEY * PropertyKey,
    DEVPROPTYPE      * PropertyType,
    PBYTE            PropertyBuffer,
    PULONG           PropertyBufferSize,
    ULONG            ulFlags
);
#define CM_Get_DevNode_Property CM_Get_DevNode_PropertyW
#pragma GCC diagnostic pop
#endif

#ifndef SHTDN_REASON_FLAG_PLANNED
#define SHTDN_REASON_FLAG_PLANNED 0x80000000
#endif

/* multiple of 100 nanoseconds elapsed between windows baseline
 *    (1/1/1601) and Unix Epoch (1/1/1970), accounting for leap years */
#define W32_FT_OFFSET (10000000ULL * 60 * 60 * 24 * \
                       (365 * (1970 - 1601) +       \
                        (1970 - 1601) / 4 - 3))

#define INVALID_SET_FILE_POINTER ((DWORD)-1)

struct GuestFileHandle {
    int64_t id;
    HANDLE fh;
    QTAILQ_ENTRY(GuestFileHandle) next;
};

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
    {"r+",  GENERIC_WRITE | GENERIC_READ,       OPEN_EXISTING},
    {"rb+", GENERIC_WRITE | GENERIC_READ,       OPEN_EXISTING},
    {"r+b", GENERIC_WRITE | GENERIC_READ,       OPEN_EXISTING},
    {"w+",  GENERIC_WRITE | GENERIC_READ,       CREATE_ALWAYS},
    {"wb+", GENERIC_WRITE | GENERIC_READ,       CREATE_ALWAYS},
    {"w+b", GENERIC_WRITE | GENERIC_READ,       CREATE_ALWAYS},
    {"a+",  FILE_GENERIC_APPEND | GENERIC_READ, OPEN_ALWAYS  },
    {"ab+", FILE_GENERIC_APPEND | GENERIC_READ, OPEN_ALWAYS  },
    {"a+b", FILE_GENERIC_APPEND | GENERIC_READ, OPEN_ALWAYS  }
};

#define debug_error(msg) do { \
    char *suffix = g_win32_error_message(GetLastError()); \
    g_debug("%s: %s", (msg), suffix); \
    g_free(suffix); \
} while (0)

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

GuestFileHandle *guest_file_handle_find(int64_t id, Error **errp)
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
    int64_t fd = -1;
    HANDLE fh;
    HANDLE templ_file = NULL;
    DWORD share_mode = FILE_SHARE_READ;
    DWORD flags_and_attr = FILE_ATTRIBUTE_NORMAL;
    LPSECURITY_ATTRIBUTES sa_attr = NULL;
    OpenFlags *guest_flags;
    GError *gerr = NULL;
    wchar_t *w_path = NULL;

    if (!has_mode) {
        mode = "r";
    }
    slog("guest-file-open called, filepath: %s, mode: %s", path, mode);
    guest_flags = find_open_flag(mode);
    if (guest_flags == NULL) {
        error_setg(errp, "invalid file open mode");
        goto done;
    }

    w_path = g_utf8_to_utf16(path, -1, NULL, NULL, &gerr);
    if (!w_path) {
        goto done;
    }

    fh = CreateFileW(w_path, guest_flags->desired_access, share_mode, sa_attr,
                    guest_flags->creation_disposition, flags_and_attr,
                    templ_file);
    if (fh == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to open file '%s'",
                         path);
        goto done;
    }

    /* set fd non-blocking to avoid common use cases (like reading from a
     * named pipe) from hanging the agent
     */
    handle_set_nonblocking(fh);

    fd = guest_file_handle_add(fh, errp);
    if (fd < 0) {
        CloseHandle(fh);
        error_setg(errp, "failed to add handle to qmp handle table");
        goto done;
    }

    slog("guest-file-open, handle: % " PRId64, fd);

done:
    if (gerr) {
        error_setg(errp, QERR_QGA_COMMAND_FAILED, gerr->message);
        g_error_free(gerr);
    }
    g_free(w_path);
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
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
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
    error_propagate(errp, local_err);
}

static void execute_async(DWORD WINAPI (*func)(LPVOID), LPVOID opaque,
                          Error **errp)
{
    HANDLE thread = CreateThread(NULL, 0, func, opaque, 0, NULL);
    if (!thread) {
        error_setg(errp, QERR_QGA_COMMAND_FAILED,
                   "failed to dispatch asynchronous command");
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
                   "'halt', 'powerdown', or 'reboot'");
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
        g_autofree gchar *emsg = g_win32_error_message(GetLastError());
        slog("guest-shutdown failed: %s", emsg);
        error_setg_win32(errp, GetLastError(), "guest-shutdown failed");
    }
}

GuestFileRead *guest_file_read_unsafe(GuestFileHandle *gfh,
                                      int64_t count, Error **errp)
{
    GuestFileRead *read_data = NULL;
    guchar *buf;
    HANDLE fh = gfh->fh;
    bool is_ok;
    DWORD read_count;

    buf = g_malloc0(count + 1);
    is_ok = ReadFile(fh, buf, count, &read_count, NULL);
    if (!is_ok) {
        error_setg_win32(errp, GetLastError(), "failed to read file");
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

static GuestDiskBusType win2qemu[] = {
    [BusTypeUnknown] = GUEST_DISK_BUS_TYPE_UNKNOWN,
    [BusTypeScsi] = GUEST_DISK_BUS_TYPE_SCSI,
    [BusTypeAtapi] = GUEST_DISK_BUS_TYPE_IDE,
    [BusTypeAta] = GUEST_DISK_BUS_TYPE_IDE,
    [BusType1394] = GUEST_DISK_BUS_TYPE_IEEE1394,
    [BusTypeSsa] = GUEST_DISK_BUS_TYPE_SSA,
    [BusTypeFibre] = GUEST_DISK_BUS_TYPE_SSA,
    [BusTypeUsb] = GUEST_DISK_BUS_TYPE_USB,
    [BusTypeRAID] = GUEST_DISK_BUS_TYPE_RAID,
    [BusTypeiScsi] = GUEST_DISK_BUS_TYPE_ISCSI,
    [BusTypeSas] = GUEST_DISK_BUS_TYPE_SAS,
    [BusTypeSata] = GUEST_DISK_BUS_TYPE_SATA,
    [BusTypeSd] =  GUEST_DISK_BUS_TYPE_SD,
    [BusTypeMmc] = GUEST_DISK_BUS_TYPE_MMC,
#if (_WIN32_WINNT >= 0x0601)
    [BusTypeVirtual] = GUEST_DISK_BUS_TYPE_VIRTUAL,
    [BusTypeFileBackedVirtual] = GUEST_DISK_BUS_TYPE_FILE_BACKED_VIRTUAL,
    /*
     * BusTypeSpaces currently is not suported
     */
    [BusTypeSpaces] = GUEST_DISK_BUS_TYPE_UNKNOWN,
    [BusTypeNvme] = GUEST_DISK_BUS_TYPE_NVME,
#endif
};

static GuestDiskBusType find_bus_type(STORAGE_BUS_TYPE bus)
{
    if (bus >= ARRAY_SIZE(win2qemu) || (int)bus < 0) {
        return GUEST_DISK_BUS_TYPE_UNKNOWN;
    }
    return win2qemu[(int)bus];
}

DEFINE_GUID(GUID_DEVINTERFACE_DISK,
        0x53f56307L, 0xb6bf, 0x11d0, 0x94, 0xf2,
        0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);
DEFINE_GUID(GUID_DEVINTERFACE_STORAGEPORT,
        0x2accfe60L, 0xc130, 0x11d2, 0xb0, 0x82,
        0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

static void get_pci_address_for_device(GuestPCIAddress *pci,
                                       HDEVINFO dev_info)
{
    SP_DEVINFO_DATA dev_info_data;
    DWORD j;
    DWORD size;
    bool partial_pci = false;

    dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

    for (j = 0;
         SetupDiEnumDeviceInfo(dev_info, j, &dev_info_data);
         j++) {
        DWORD addr, bus, ui_slot, type;
        int func, slot;
        size = sizeof(DWORD);

        /*
        * There is no need to allocate buffer in the next functions. The
        * size is known and ULONG according to
        * https://msdn.microsoft.com/en-us/library/windows/hardware/ff543095(v=vs.85).aspx
        */
        if (!SetupDiGetDeviceRegistryProperty(
                dev_info, &dev_info_data, SPDRP_BUSNUMBER,
                &type, (PBYTE)&bus, size, NULL)) {
            debug_error("failed to get PCI bus");
            bus = -1;
            partial_pci = true;
        }

        /*
        * The function retrieves the device's address. This value will be
        * transformed into device function and number
        */
        if (!SetupDiGetDeviceRegistryProperty(
                dev_info, &dev_info_data, SPDRP_ADDRESS,
                &type, (PBYTE)&addr, size, NULL)) {
            debug_error("failed to get PCI address");
            addr = -1;
            partial_pci = true;
        }

        /*
        * This call returns UINumber of DEVICE_CAPABILITIES structure.
        * This number is typically a user-perceived slot number.
        */
        if (!SetupDiGetDeviceRegistryProperty(
                dev_info, &dev_info_data, SPDRP_UI_NUMBER,
                &type, (PBYTE)&ui_slot, size, NULL)) {
            debug_error("failed to get PCI slot");
            ui_slot = -1;
            partial_pci = true;
        }

        /*
        * SetupApi gives us the same information as driver with
        * IoGetDeviceProperty. According to Microsoft:
        *
        *   FunctionNumber = (USHORT)((propertyAddress) & 0x0000FFFF)
        *   DeviceNumber = (USHORT)(((propertyAddress) >> 16) & 0x0000FFFF)
        *   SPDRP_ADDRESS is propertyAddress, so we do the same.
        *
        * https://docs.microsoft.com/en-us/windows/desktop/api/setupapi/nf-setupapi-setupdigetdeviceregistrypropertya
        */
        if (partial_pci) {
            pci->domain = -1;
            pci->slot = -1;
            pci->function = -1;
            pci->bus = -1;
            continue;
        } else {
            func = ((int)addr == -1) ? -1 : addr & 0x0000FFFF;
            slot = ((int)addr == -1) ? -1 : (addr >> 16) & 0x0000FFFF;
            if ((int)ui_slot != slot) {
                g_debug("mismatch with reported slot values: %d vs %d",
                        (int)ui_slot, slot);
            }
            pci->domain = 0;
            pci->slot = (int)ui_slot;
            pci->function = func;
            pci->bus = (int)bus;
            return;
        }
    }
}

static GuestPCIAddress *get_pci_info(int number, Error **errp)
{
    HDEVINFO dev_info = INVALID_HANDLE_VALUE;
    HDEVINFO parent_dev_info = INVALID_HANDLE_VALUE;

    SP_DEVINFO_DATA dev_info_data;
    SP_DEVICE_INTERFACE_DATA dev_iface_data;
    HANDLE dev_file;
    int i;
    GuestPCIAddress *pci = NULL;

    pci = g_malloc0(sizeof(*pci));
    pci->domain = -1;
    pci->slot = -1;
    pci->function = -1;
    pci->bus = -1;

    dev_info = SetupDiGetClassDevs(&GUID_DEVINTERFACE_DISK, 0, 0,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (dev_info == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to get devices tree");
        goto end;
    }

    g_debug("enumerating devices");
    dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
    dev_iface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    for (i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
        g_autofree PSP_DEVICE_INTERFACE_DETAIL_DATA pdev_iface_detail_data = NULL;
        STORAGE_DEVICE_NUMBER sdn;
        g_autofree char *parent_dev_id = NULL;
        SP_DEVINFO_DATA parent_dev_info_data;
        DWORD size = 0;

        g_debug("getting device path");
        if (SetupDiEnumDeviceInterfaces(dev_info, &dev_info_data,
                                        &GUID_DEVINTERFACE_DISK, 0,
                                        &dev_iface_data)) {
            if (!SetupDiGetDeviceInterfaceDetail(dev_info, &dev_iface_data,
                                                 pdev_iface_detail_data,
                                                 size, &size,
                                                 &dev_info_data)) {
                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    pdev_iface_detail_data = g_malloc(size);
                    pdev_iface_detail_data->cbSize =
                        sizeof(*pdev_iface_detail_data);
                } else {
                    error_setg_win32(errp, GetLastError(),
                                     "failed to get device interfaces");
                    goto end;
                }
            }

            if (!SetupDiGetDeviceInterfaceDetail(dev_info, &dev_iface_data,
                                                 pdev_iface_detail_data,
                                                 size, &size,
                                                 &dev_info_data)) {
                // pdev_iface_detail_data already is allocated
                error_setg_win32(errp, GetLastError(),
                                    "failed to get device interfaces");
                goto end;
            }

            dev_file = CreateFile(pdev_iface_detail_data->DevicePath, 0,
                                  FILE_SHARE_READ, NULL, OPEN_EXISTING, 0,
                                  NULL);

            if (!DeviceIoControl(dev_file, IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                 NULL, 0, &sdn, sizeof(sdn), &size, NULL)) {
                CloseHandle(dev_file);
                error_setg_win32(errp, GetLastError(),
                                 "failed to get device slot number");
                goto end;
            }

            CloseHandle(dev_file);
            if (sdn.DeviceNumber != number) {
                continue;
            }
        } else {
            error_setg_win32(errp, GetLastError(),
                             "failed to get device interfaces");
            goto end;
        }

        g_debug("found device slot %d. Getting storage controller", number);
        {
            CONFIGRET cr;
            DEVINST dev_inst, parent_dev_inst;
            ULONG dev_id_size = 0;

            size = 0;
            if (!SetupDiGetDeviceInstanceId(dev_info, &dev_info_data,
                                            parent_dev_id, size, &size)) {
                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    parent_dev_id = g_malloc(size);
                } else {
                    error_setg_win32(errp, GetLastError(),
                                     "failed to get device instance ID");
                    goto end;
                }
            }

            if (!SetupDiGetDeviceInstanceId(dev_info, &dev_info_data,
                                            parent_dev_id, size, &size)) {
                // parent_dev_id already is allocated
                error_setg_win32(errp, GetLastError(),
                                    "failed to get device instance ID");
                goto end;
            }

            /*
             * CM API used here as opposed to
             * SetupDiGetDeviceProperty(..., DEVPKEY_Device_Parent, ...)
             * which exports are only available in mingw-w64 6+
             */
            cr = CM_Locate_DevInst(&dev_inst, parent_dev_id, 0);
            if (cr != CR_SUCCESS) {
                g_error("CM_Locate_DevInst failed with code %lx", cr);
                error_setg_win32(errp, GetLastError(),
                                 "failed to get device instance");
                goto end;
            }
            cr = CM_Get_Parent(&parent_dev_inst, dev_inst, 0);
            if (cr != CR_SUCCESS) {
                g_error("CM_Get_Parent failed with code %lx", cr);
                error_setg_win32(errp, GetLastError(),
                                 "failed to get parent device instance");
                goto end;
            }

            cr = CM_Get_Device_ID_Size(&dev_id_size, parent_dev_inst, 0);
            if (cr != CR_SUCCESS) {
                g_error("CM_Get_Device_ID_Size failed with code %lx", cr);
                error_setg_win32(errp, GetLastError(),
                                 "failed to get parent device ID length");
                goto end;
            }

            ++dev_id_size;
            if (dev_id_size > size) {
                g_free(parent_dev_id);
                parent_dev_id = g_malloc(dev_id_size);
            }

            cr = CM_Get_Device_ID(parent_dev_inst, parent_dev_id, dev_id_size,
                                  0);
            if (cr != CR_SUCCESS) {
                g_error("CM_Get_Device_ID failed with code %lx", cr);
                error_setg_win32(errp, GetLastError(),
                                 "failed to get parent device ID");
                goto end;
            }
        }

        g_debug("querying storage controller %s for PCI information",
                parent_dev_id);
        parent_dev_info =
            SetupDiGetClassDevs(&GUID_DEVINTERFACE_STORAGEPORT, parent_dev_id,
                                NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

        if (parent_dev_info == INVALID_HANDLE_VALUE) {
            error_setg_win32(errp, GetLastError(),
                             "failed to get parent device");
            goto end;
        }

        parent_dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
        if (!SetupDiEnumDeviceInfo(parent_dev_info, 0, &parent_dev_info_data)) {
            error_setg_win32(errp, GetLastError(),
                           "failed to get parent device data");
            goto end;
        }

        get_pci_address_for_device(pci, parent_dev_info);

        break;
    }

end:
    if (parent_dev_info != INVALID_HANDLE_VALUE) {
        SetupDiDestroyDeviceInfoList(parent_dev_info);
    }
    if (dev_info != INVALID_HANDLE_VALUE) {
        SetupDiDestroyDeviceInfoList(dev_info);
    }
    return pci;
}

static void get_disk_properties(HANDLE vol_h, GuestDiskAddress *disk,
    Error **errp)
{
    STORAGE_PROPERTY_QUERY query;
    STORAGE_DEVICE_DESCRIPTOR *dev_desc, buf;
    DWORD received;
    ULONG size = sizeof(buf);

    dev_desc = &buf;
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    if (!DeviceIoControl(vol_h, IOCTL_STORAGE_QUERY_PROPERTY, &query,
                         sizeof(STORAGE_PROPERTY_QUERY), dev_desc,
                         size, &received, NULL)) {
        error_setg_win32(errp, GetLastError(), "failed to get bus type");
        return;
    }
    disk->bus_type = find_bus_type(dev_desc->BusType);
    g_debug("bus type %d", disk->bus_type);

    /* Query once more. Now with long enough buffer. */
    size = dev_desc->Size;
    dev_desc = g_malloc0(size);
    if (!DeviceIoControl(vol_h, IOCTL_STORAGE_QUERY_PROPERTY, &query,
                         sizeof(STORAGE_PROPERTY_QUERY), dev_desc,
                         size, &received, NULL)) {
        error_setg_win32(errp, GetLastError(), "failed to get serial number");
        g_debug("failed to get serial number");
        goto out_free;
    }
    if (dev_desc->SerialNumberOffset > 0) {
        const char *serial;
        size_t len;

        if (dev_desc->SerialNumberOffset >= received) {
            error_setg(errp, "failed to get serial number: offset outside the buffer");
            g_debug("serial number offset outside the buffer");
            goto out_free;
        }
        serial = (char *)dev_desc + dev_desc->SerialNumberOffset;
        len = received - dev_desc->SerialNumberOffset;
        g_debug("serial number \"%s\"", serial);
        if (*serial != 0) {
            disk->serial = g_strndup(serial, len);
            disk->has_serial = true;
        }
    }
out_free:
    g_free(dev_desc);

    return;
}

static void get_single_disk_info(int disk_number,
                                 GuestDiskAddress *disk, Error **errp)
{
    SCSI_ADDRESS addr, *scsi_ad;
    DWORD len;
    HANDLE disk_h;
    Error *local_err = NULL;

    scsi_ad = &addr;

    g_debug("getting disk info for: %s", disk->dev);
    disk_h = CreateFile(disk->dev, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       0, NULL);
    if (disk_h == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to open disk");
        return;
    }

    get_disk_properties(disk_h, disk, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto err_close;
    }

    g_debug("bus type %d", disk->bus_type);
    /* always set pci_controller as required by schema. get_pci_info() should
     * report -1 values for non-PCI buses rather than fail. fail the command
     * if that doesn't hold since that suggests some other unexpected
     * breakage
     */
    disk->pci_controller = get_pci_info(disk_number, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto err_close;
    }
    if (disk->bus_type == GUEST_DISK_BUS_TYPE_SCSI
            || disk->bus_type == GUEST_DISK_BUS_TYPE_IDE
            || disk->bus_type == GUEST_DISK_BUS_TYPE_RAID
            /* This bus type is not supported before Windows Server 2003 SP1 */
            || disk->bus_type == GUEST_DISK_BUS_TYPE_SAS
        ) {
        /* We are able to use the same ioctls for different bus types
         * according to Microsoft docs
         * https://technet.microsoft.com/en-us/library/ee851589(v=ws.10).aspx */
        g_debug("getting SCSI info");
        if (DeviceIoControl(disk_h, IOCTL_SCSI_GET_ADDRESS, NULL, 0, scsi_ad,
                            sizeof(SCSI_ADDRESS), &len, NULL)) {
            disk->unit = addr.Lun;
            disk->target = addr.TargetId;
            disk->bus = addr.PathId;
        }
        /* We do not set error in this case, because we still have enough
         * information about volume. */
    }

err_close:
    CloseHandle(disk_h);
    return;
}

/* VSS provider works with volumes, thus there is no difference if
 * the volume consist of spanned disks. Info about the first disk in the
 * volume is returned for the spanned disk group (LVM) */
static GuestDiskAddressList *build_guest_disk_info(char *guid, Error **errp)
{
    Error *local_err = NULL;
    GuestDiskAddressList *list = NULL;
    GuestDiskAddress *disk = NULL;
    int i;
    HANDLE vol_h;
    DWORD size;
    PVOLUME_DISK_EXTENTS extents = NULL;

    /* strip final backslash */
    char *name = g_strdup(guid);
    if (g_str_has_suffix(name, "\\")) {
        name[strlen(name) - 1] = 0;
    }

    g_debug("opening %s", name);
    vol_h = CreateFile(name, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       0, NULL);
    if (vol_h == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to open volume");
        goto out;
    }

    /* Get list of extents */
    g_debug("getting disk extents");
    size = sizeof(VOLUME_DISK_EXTENTS);
    extents = g_malloc0(size);
    if (!DeviceIoControl(vol_h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL,
                         0, extents, size, &size, NULL)) {
        DWORD last_err = GetLastError();
        if (last_err == ERROR_MORE_DATA) {
            /* Try once more with big enough buffer */
            g_free(extents);
            extents = g_malloc0(size);
            if (!DeviceIoControl(
                    vol_h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL,
                    0, extents, size, NULL, NULL)) {
                error_setg_win32(errp, GetLastError(),
                    "failed to get disk extents");
                goto out;
            }
        } else if (last_err == ERROR_INVALID_FUNCTION) {
            /* Possibly CD-ROM or a shared drive. Try to pass the volume */
            g_debug("volume not on disk");
            disk = g_new0(GuestDiskAddress, 1);
            disk->has_dev = true;
            disk->dev = g_strdup(name);
            get_single_disk_info(0xffffffff, disk, &local_err);
            if (local_err) {
                g_debug("failed to get disk info, ignoring error: %s",
                    error_get_pretty(local_err));
                error_free(local_err);
                goto out;
            }
            QAPI_LIST_PREPEND(list, disk);
            disk = NULL;
            goto out;
        } else {
            error_setg_win32(errp, GetLastError(),
                "failed to get disk extents");
            goto out;
        }
    }
    g_debug("Number of extents: %lu", extents->NumberOfDiskExtents);

    /* Go through each extent */
    for (i = 0; i < extents->NumberOfDiskExtents; i++) {
        disk = g_new0(GuestDiskAddress, 1);

        /* Disk numbers directly correspond to numbers used in UNCs
         *
         * See documentation for DISK_EXTENT:
         * https://docs.microsoft.com/en-us/windows/desktop/api/winioctl/ns-winioctl-_disk_extent
         *
         * See also Naming Files, Paths and Namespaces:
         * https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file#win32-device-namespaces
         */
        disk->has_dev = true;
        disk->dev = g_strdup_printf("\\\\.\\PhysicalDrive%lu",
                                    extents->Extents[i].DiskNumber);

        get_single_disk_info(extents->Extents[i].DiskNumber, disk, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto out;
        }
        QAPI_LIST_PREPEND(list, disk);
        disk = NULL;
    }


out:
    if (vol_h != INVALID_HANDLE_VALUE) {
        CloseHandle(vol_h);
    }
    qapi_free_GuestDiskAddress(disk);
    g_free(extents);
    g_free(name);

    return list;
}

GuestDiskInfoList *qmp_guest_get_disks(Error **errp)
{
    GuestDiskInfoList *ret = NULL;
    HDEVINFO dev_info;
    SP_DEVICE_INTERFACE_DATA dev_iface_data;
    int i;

    dev_info = SetupDiGetClassDevs(&GUID_DEVINTERFACE_DISK, 0, 0,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (dev_info == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to get device tree");
        return NULL;
    }

    g_debug("enumerating devices");
    dev_iface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    for (i = 0;
        SetupDiEnumDeviceInterfaces(dev_info, NULL, &GUID_DEVINTERFACE_DISK,
            i, &dev_iface_data);
        i++) {
        GuestDiskAddress *address = NULL;
        GuestDiskInfo *disk = NULL;
        Error *local_err = NULL;
        g_autofree PSP_DEVICE_INTERFACE_DETAIL_DATA
            pdev_iface_detail_data = NULL;
        STORAGE_DEVICE_NUMBER sdn;
        HANDLE dev_file;
        DWORD size = 0;
        BOOL result;
        int attempt;

        g_debug("  getting device path");
        for (attempt = 0, result = FALSE; attempt < 2 && !result; attempt++) {
            result = SetupDiGetDeviceInterfaceDetail(dev_info,
                &dev_iface_data, pdev_iface_detail_data, size, &size, NULL);
            if (result) {
                break;
            }
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                pdev_iface_detail_data = g_realloc(pdev_iface_detail_data,
                    size);
                pdev_iface_detail_data->cbSize =
                    sizeof(*pdev_iface_detail_data);
            } else {
                g_debug("failed to get device interface details");
                break;
            }
        }
        if (!result) {
            g_debug("skipping device");
            continue;
        }

        g_debug("  device: %s", pdev_iface_detail_data->DevicePath);
        dev_file = CreateFile(pdev_iface_detail_data->DevicePath, 0,
            FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (!DeviceIoControl(dev_file, IOCTL_STORAGE_GET_DEVICE_NUMBER,
                NULL, 0, &sdn, sizeof(sdn), &size, NULL)) {
            CloseHandle(dev_file);
            debug_error("failed to get storage device number");
            continue;
        }
        CloseHandle(dev_file);

        disk = g_new0(GuestDiskInfo, 1);
        disk->name = g_strdup_printf("\\\\.\\PhysicalDrive%lu",
            sdn.DeviceNumber);

        g_debug("  number: %lu", sdn.DeviceNumber);
        address = g_new0(GuestDiskAddress, 1);
        address->has_dev = true;
        address->dev = g_strdup(disk->name);
        get_single_disk_info(sdn.DeviceNumber, address, &local_err);
        if (local_err) {
            g_debug("failed to get disk info: %s",
                error_get_pretty(local_err));
            error_free(local_err);
            qapi_free_GuestDiskAddress(address);
            address = NULL;
        } else {
            disk->address = address;
            disk->has_address = true;
        }

        QAPI_LIST_PREPEND(ret, disk);
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return ret;
}

static GuestFilesystemInfo *build_guest_fsinfo(char *guid, Error **errp)
{
    DWORD info_size;
    char mnt, *mnt_point;
    wchar_t wfs_name[32];
    char fs_name[32];
    wchar_t vol_info[MAX_PATH + 1];
    size_t len;
    uint64_t i64FreeBytesToCaller, i64TotalBytes, i64FreeBytes;
    GuestFilesystemInfo *fs = NULL;
    HANDLE hLocalDiskHandle = INVALID_HANDLE_VALUE;

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

    hLocalDiskHandle = CreateFile(guid, 0 , 0, NULL, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL |
                                  FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (INVALID_HANDLE_VALUE == hLocalDiskHandle) {
        error_setg_win32(errp, GetLastError(), "failed to get handle for volume");
        goto free;
    }

    len = strlen(mnt_point);
    mnt_point[len] = '\\';
    mnt_point[len + 1] = 0;

    if (!GetVolumeInformationByHandleW(hLocalDiskHandle, vol_info,
                                       sizeof(vol_info), NULL, NULL, NULL,
                                       (LPWSTR) & wfs_name, sizeof(wfs_name))) {
        if (GetLastError() != ERROR_NOT_READY) {
            error_setg_win32(errp, GetLastError(), "failed to get volume info");
        }
        goto free;
    }

    fs = g_malloc(sizeof(*fs));
    fs->name = g_strdup(guid);
    fs->has_total_bytes = false;
    fs->has_used_bytes = false;
    if (len == 0) {
        fs->mountpoint = g_strdup("System Reserved");
    } else {
        fs->mountpoint = g_strndup(mnt_point, len);
        if (GetDiskFreeSpaceEx(fs->mountpoint,
                               (PULARGE_INTEGER) & i64FreeBytesToCaller,
                               (PULARGE_INTEGER) & i64TotalBytes,
                               (PULARGE_INTEGER) & i64FreeBytes)) {
            fs->used_bytes = i64TotalBytes - i64FreeBytes;
            fs->total_bytes = i64TotalBytes;
            fs->has_total_bytes = true;
            fs->has_used_bytes = true;
        }
    }
    wcstombs(fs_name, wfs_name, sizeof(wfs_name));
    fs->type = g_strdup(fs_name);
    fs->disk = build_guest_disk_info(guid, errp);
free:
    if (hLocalDiskHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(hLocalDiskHandle);
    }
    g_free(mnt_point);
    return fs;
}

GuestFilesystemInfoList *qmp_guest_get_fsinfo(Error **errp)
{
    HANDLE vol_h;
    GuestFilesystemInfoList *ret = NULL;
    char guid[256];

    vol_h = FindFirstVolume(guid, sizeof(guid));
    if (vol_h == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to find any volume");
        return NULL;
    }

    do {
        Error *local_err = NULL;
        GuestFilesystemInfo *info = build_guest_fsinfo(guid, &local_err);
        if (local_err) {
            g_debug("failed to get filesystem info, ignoring error: %s",
                    error_get_pretty(local_err));
            error_free(local_err);
            continue;
        }
        QAPI_LIST_PREPEND(ret, info);
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
    return qmp_guest_fsfreeze_freeze_list(false, NULL, errp);
}

int64_t qmp_guest_fsfreeze_freeze_list(bool has_mountpoints,
                                       strList *mountpoints,
                                       Error **errp)
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

    qga_vss_fsfreeze(&i, true, mountpoints, &local_err);
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

    qga_vss_fsfreeze(&i, false, NULL, errp);

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
    GuestFilesystemTrimResponse *resp;
    HANDLE handle;
    WCHAR guid[MAX_PATH] = L"";
    OSVERSIONINFO osvi;
    BOOL win8_or_later;

    ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&osvi);
    win8_or_later = (osvi.dwMajorVersion > 6 ||
                          ((osvi.dwMajorVersion == 6) &&
                           (osvi.dwMinorVersion >= 2)));
    if (!win8_or_later) {
        error_setg(errp, "fstrim is only supported for Win8+");
        return NULL;
    }

    handle = FindFirstVolumeW(guid, ARRAYSIZE(guid));
    if (handle == INVALID_HANDLE_VALUE) {
        error_setg_win32(errp, GetLastError(), "failed to find any volume");
        return NULL;
    }

    resp = g_new0(GuestFilesystemTrimResponse, 1);

    do {
        GuestFilesystemTrimResult *res;
        PWCHAR uc_path;
        DWORD char_count = 0;
        char *path, *out;
        GError *gerr = NULL;
        gchar *argv[4];

        GetVolumePathNamesForVolumeNameW(guid, NULL, 0, &char_count);

        if (GetLastError() != ERROR_MORE_DATA) {
            continue;
        }
        if (GetDriveTypeW(guid) != DRIVE_FIXED) {
            continue;
        }

        uc_path = g_new(WCHAR, char_count);
        if (!GetVolumePathNamesForVolumeNameW(guid, uc_path, char_count,
                                              &char_count) || !*uc_path) {
            /* strange, but this condition could be faced even with size == 2 */
            g_free(uc_path);
            continue;
        }

        res = g_new0(GuestFilesystemTrimResult, 1);

        path = g_utf16_to_utf8(uc_path, char_count, NULL, NULL, &gerr);

        g_free(uc_path);

        if (!path) {
            res->has_error = true;
            res->error = g_strdup(gerr->message);
            g_error_free(gerr);
            break;
        }

        res->path = path;

        QAPI_LIST_PREPEND(resp->paths, res);

        memset(argv, 0, sizeof(argv));
        argv[0] = (gchar *)"defrag.exe";
        argv[1] = (gchar *)"/L";
        argv[2] = path;

        if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                          &out /* stdout */, NULL /* stdin */,
                          NULL, &gerr)) {
            res->has_error = true;
            res->error = g_strdup(gerr->message);
            g_error_free(gerr);
        } else {
            /* defrag.exe is UGLY. Exit code is ALWAYS zero.
               Error is reported in the output with something like
               (x89000020) etc code in the stdout */

            int i;
            gchar **lines = g_strsplit(out, "\r\n", 0);
            g_free(out);

            for (i = 0; lines[i] != NULL; i++) {
                if (g_strstr_len(lines[i], -1, "(0x") == NULL) {
                    continue;
                }
                res->has_error = true;
                res->error = g_strdup(lines[i]);
                break;
            }
            g_strfreev(lines);
        }
    } while (FindNextVolumeW(handle, guid, ARRAYSIZE(guid)));

    FindVolumeClose(handle);
    return resp;
}

typedef enum {
    GUEST_SUSPEND_MODE_DISK,
    GUEST_SUSPEND_MODE_RAM
} GuestSuspendMode;

static void check_suspend_mode(GuestSuspendMode mode, Error **errp)
{
    SYSTEM_POWER_CAPABILITIES sys_pwr_caps;

    ZeroMemory(&sys_pwr_caps, sizeof(sys_pwr_caps));
    if (!GetPwrCapabilities(&sys_pwr_caps)) {
        error_setg(errp, QERR_QGA_COMMAND_FAILED,
                   "failed to determine guest suspend capabilities");
        return;
    }

    switch (mode) {
    case GUEST_SUSPEND_MODE_DISK:
        if (!sys_pwr_caps.SystemS4) {
            error_setg(errp, QERR_QGA_COMMAND_FAILED,
                       "suspend-to-disk not supported by OS");
        }
        break;
    case GUEST_SUSPEND_MODE_RAM:
        if (!sys_pwr_caps.SystemS3) {
            error_setg(errp, QERR_QGA_COMMAND_FAILED,
                       "suspend-to-ram not supported by OS");
        }
        break;
    default:
        abort();
    }
}

static DWORD WINAPI do_suspend(LPVOID opaque)
{
    GuestSuspendMode *mode = opaque;
    DWORD ret = 0;

    if (!SetSuspendState(*mode == GUEST_SUSPEND_MODE_DISK, TRUE, TRUE)) {
        g_autofree gchar *emsg = g_win32_error_message(GetLastError());
        slog("failed to suspend guest: %s", emsg);
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
    if (local_err) {
        goto out;
    }
    acquire_privilege(SE_SHUTDOWN_NAME, &local_err);
    if (local_err) {
        goto out;
    }
    execute_async(do_suspend, mode, &local_err);

out:
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
    if (local_err) {
        goto out;
    }
    acquire_privilege(SE_SHUTDOWN_NAME, &local_err);
    if (local_err) {
        goto out;
    }
    execute_async(do_suspend, mode, &local_err);

out:
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
    size_t str_size;

    str_size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    /* add 1 to str_size for NULL terminator */
    str = g_malloc(str_size + 1);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, str_size, NULL, NULL);
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

static int64_t guest_ip_prefix(IP_ADAPTER_UNICAST_ADDRESS *ip_addr)
{
    /* For Windows Vista/2008 and newer, use the OnLinkPrefixLength
     * field to obtain the prefix.
     */
    return ip_addr->OnLinkPrefixLength;
}

#define INTERFACE_PATH_BUF_SZ 512

static DWORD get_interface_index(const char *guid)
{
    ULONG index;
    DWORD status;
    wchar_t wbuf[INTERFACE_PATH_BUF_SZ];
    snwprintf(wbuf, INTERFACE_PATH_BUF_SZ, L"\\device\\tcpip_%s", guid);
    wbuf[INTERFACE_PATH_BUF_SZ - 1] = 0;
    status = GetAdapterIndex (wbuf, &index);
    if (status != NO_ERROR) {
        return (DWORD)~0;
    } else {
        return index;
    }
}

typedef NETIOAPI_API (WINAPI *GetIfEntry2Func)(PMIB_IF_ROW2 Row);

static int guest_get_network_stats(const char *name,
                                   GuestNetworkInterfaceStat *stats)
{
    OSVERSIONINFO os_ver;

    os_ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&os_ver);
    if (os_ver.dwMajorVersion >= 6) {
        MIB_IF_ROW2 a_mid_ifrow;
        GetIfEntry2Func getifentry2_ex;
        DWORD if_index = 0;
        HMODULE module = GetModuleHandle("iphlpapi");
        PVOID func = GetProcAddress(module, "GetIfEntry2");

        if (func == NULL) {
            return -1;
        }

        getifentry2_ex = (GetIfEntry2Func)func;
        if_index = get_interface_index(name);
        if (if_index == (DWORD)~0) {
            return -1;
        }

        memset(&a_mid_ifrow, 0, sizeof(a_mid_ifrow));
        a_mid_ifrow.InterfaceIndex = if_index;
        if (NO_ERROR == getifentry2_ex(&a_mid_ifrow)) {
            stats->rx_bytes = a_mid_ifrow.InOctets;
            stats->rx_packets = a_mid_ifrow.InUcastPkts;
            stats->rx_errs = a_mid_ifrow.InErrors;
            stats->rx_dropped = a_mid_ifrow.InDiscards;
            stats->tx_bytes = a_mid_ifrow.OutOctets;
            stats->tx_packets = a_mid_ifrow.OutUcastPkts;
            stats->tx_errs = a_mid_ifrow.OutErrors;
            stats->tx_dropped = a_mid_ifrow.OutDiscards;
            return 0;
        }
    }
    return -1;
}

GuestNetworkInterfaceList *qmp_guest_network_get_interfaces(Error **errp)
{
    IP_ADAPTER_ADDRESSES *adptr_addrs, *addr;
    IP_ADAPTER_UNICAST_ADDRESS *ip_addr = NULL;
    GuestNetworkInterfaceList *head = NULL, **tail = &head;
    GuestIpAddressList *head_addr, **tail_addr;
    GuestNetworkInterface *info;
    GuestNetworkInterfaceStat *interface_stat = NULL;
    GuestIpAddress *address_item = NULL;
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

        QAPI_LIST_APPEND(tail, info);

        info->name = guest_wctomb_dup(addr->FriendlyName);

        if (addr->PhysicalAddressLength != 0) {
            mac_addr = addr->PhysicalAddress;

            info->hardware_address =
                g_strdup_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                                (int) mac_addr[0], (int) mac_addr[1],
                                (int) mac_addr[2], (int) mac_addr[3],
                                (int) mac_addr[4], (int) mac_addr[5]);

            info->has_hardware_address = true;
        }

        head_addr = NULL;
        tail_addr = &head_addr;
        for (ip_addr = addr->FirstUnicastAddress;
                ip_addr;
                ip_addr = ip_addr->Next) {
            addr_str = guest_addr_to_str(ip_addr, errp);
            if (addr_str == NULL) {
                continue;
            }

            address_item = g_malloc0(sizeof(*address_item));

            QAPI_LIST_APPEND(tail_addr, address_item);

            address_item->ip_address = addr_str;
            address_item->prefix = guest_ip_prefix(ip_addr);
            if (ip_addr->Address.lpSockaddr->sa_family == AF_INET) {
                address_item->ip_address_type = GUEST_IP_ADDRESS_TYPE_IPV4;
            } else if (ip_addr->Address.lpSockaddr->sa_family == AF_INET6) {
                address_item->ip_address_type = GUEST_IP_ADDRESS_TYPE_IPV6;
            }
        }
        if (head_addr) {
            info->has_ip_addresses = true;
            info->ip_addresses = head_addr;
        }
        if (!info->has_statistics) {
            interface_stat = g_malloc0(sizeof(*interface_stat));
            if (guest_get_network_stats(addr->AdapterName,
                interface_stat) == -1) {
                info->has_statistics = false;
                g_free(interface_stat);
            } else {
                info->statistics = interface_stat;
                info->has_statistics = true;
            }
        }
    }
    WSACleanup();
out:
    g_free(adptr_addrs);
    return head;
}

static int64_t filetime_to_ns(const FILETIME *tf)
{
    return ((((int64_t)tf->dwHighDateTime << 32) | tf->dwLowDateTime)
            - W32_FT_OFFSET) * 100;
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
         *
         * Instead, a workaround is to use the Windows win32tm command to
         * resync the time using the Windows Time service.
         */
        LPVOID msg_buffer;
        DWORD ret_flags;

        HRESULT hr = system("w32tm /resync /nowait");

        if (GetLastError() != 0) {
            strerror_s((LPTSTR) & msg_buffer, 0, errno);
            error_setg(errp, "system(...) failed: %s", (LPCTSTR)msg_buffer);
        } else if (hr != 0) {
            if (hr == HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE)) {
                error_setg(errp, "Windows Time service not running on the "
                                 "guest");
            } else {
                if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                   FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                                   (DWORD)hr, MAKELANGID(LANG_NEUTRAL,
                                   SUBLANG_DEFAULT), (LPTSTR) & msg_buffer, 0,
                                   NULL)) {
                    error_setg(errp, "w32tm failed with error (0x%lx), couldn'"
                                     "t retrieve error message", hr);
                } else {
                    error_setg(errp, "w32tm failed with error (0x%lx): %s", hr,
                               (LPCTSTR)msg_buffer);
                    LocalFree(msg_buffer);
                }
            }
        } else if (!InternetGetConnectedState(&ret_flags, 0)) {
            error_setg(errp, "No internet connection on guest, sync not "
                             "accurate");
        }
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
    GuestLogicalProcessorList *head, **tail;
    Error *local_err = NULL;
    int64_t current;

    ptr = pslpi = NULL;
    length = 0;
    current = 0;
    head = NULL;
    tail = &head;

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

                    vcpu = g_malloc0(sizeof *vcpu);
                    vcpu->logical_id = current++;
                    vcpu->online = true;
                    vcpu->has_can_offline = true;

                    QAPI_LIST_APPEND(tail, vcpu);
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

/* add unsupported commands to the list of blocked RPCs */
GList *ga_command_init_blockedrpcs(GList *blockedrpcs)
{
    const char *list_unsupported[] = {
        "guest-suspend-hybrid",
        "guest-set-vcpus",
        "guest-get-memory-blocks", "guest-set-memory-blocks",
        "guest-get-memory-block-size", "guest-get-memory-block-info",
        NULL};
    char **p = (char **)list_unsupported;

    while (*p) {
        blockedrpcs = g_list_append(blockedrpcs, g_strdup(*p++));
    }

    if (!vss_init(true)) {
        g_debug("vss_init failed, vss commands are going to be disabled");
        const char *list[] = {
            "guest-get-fsinfo", "guest-fsfreeze-status",
            "guest-fsfreeze-freeze", "guest-fsfreeze-thaw", NULL};
        p = (char **)list;

        while (*p) {
            blockedrpcs = g_list_append(blockedrpcs, g_strdup(*p++));
        }
    }

    return blockedrpcs;
}

/* register init/cleanup routines for stateful command groups */
void ga_command_state_init(GAState *s, GACommandState *cs)
{
    if (!vss_initialized()) {
        ga_command_state_add(cs, NULL, guest_fsfreeze_cleanup);
    }
}

/* MINGW is missing two fields: IncomingFrames & OutgoingFrames */
typedef struct _GA_WTSINFOA {
    WTS_CONNECTSTATE_CLASS State;
    DWORD SessionId;
    DWORD IncomingBytes;
    DWORD OutgoingBytes;
    DWORD IncomingFrames;
    DWORD OutgoingFrames;
    DWORD IncomingCompressedBytes;
    DWORD OutgoingCompressedBy;
    CHAR WinStationName[WINSTATIONNAME_LENGTH];
    CHAR Domain[DOMAIN_LENGTH];
    CHAR UserName[USERNAME_LENGTH + 1];
    LARGE_INTEGER ConnectTime;
    LARGE_INTEGER DisconnectTime;
    LARGE_INTEGER LastInputTime;
    LARGE_INTEGER LogonTime;
    LARGE_INTEGER CurrentTime;

} GA_WTSINFOA;

GuestUserList *qmp_guest_get_users(Error **errp)
{
#define QGA_NANOSECONDS 10000000

    GHashTable *cache = NULL;
    GuestUserList *head = NULL, **tail = &head;

    DWORD buffer_size = 0, count = 0, i = 0;
    GA_WTSINFOA *info = NULL;
    WTS_SESSION_INFOA *entries = NULL;
    GuestUser *user = NULL;
    gpointer value = NULL;
    INT64 login = 0;
    double login_time = 0;

    cache = g_hash_table_new(g_str_hash, g_str_equal);

    if (WTSEnumerateSessionsA(NULL, 0, 1, &entries, &count)) {
        for (i = 0; i < count; ++i) {
            buffer_size = 0;
            info = NULL;
            if (WTSQuerySessionInformationA(
                NULL,
                entries[i].SessionId,
                WTSSessionInfo,
                (LPSTR *)&info,
                &buffer_size
            )) {

                if (strlen(info->UserName) == 0) {
                    WTSFreeMemory(info);
                    continue;
                }

                login = info->LogonTime.QuadPart;
                login -= W32_FT_OFFSET;
                login_time = ((double)login) / QGA_NANOSECONDS;

                if (g_hash_table_contains(cache, info->UserName)) {
                    value = g_hash_table_lookup(cache, info->UserName);
                    user = (GuestUser *)value;
                    if (user->login_time > login_time) {
                        user->login_time = login_time;
                    }
                } else {
                    user = g_new0(GuestUser, 1);

                    user->user = g_strdup(info->UserName);
                    user->domain = g_strdup(info->Domain);
                    user->has_domain = true;

                    user->login_time = login_time;

                    g_hash_table_add(cache, user->user);

                    QAPI_LIST_APPEND(tail, user);
                }
            }
            WTSFreeMemory(info);
        }
        WTSFreeMemory(entries);
    }
    g_hash_table_destroy(cache);
    return head;
}

typedef struct _ga_matrix_lookup_t {
    int major;
    int minor;
    char const *version;
    char const *version_id;
} ga_matrix_lookup_t;

static ga_matrix_lookup_t const WIN_VERSION_MATRIX[2][7] = {
    {
        /* Desktop editions */
        { 5, 0, "Microsoft Windows 2000",   "2000"},
        { 5, 1, "Microsoft Windows XP",     "xp"},
        { 6, 0, "Microsoft Windows Vista",  "vista"},
        { 6, 1, "Microsoft Windows 7"       "7"},
        { 6, 2, "Microsoft Windows 8",      "8"},
        { 6, 3, "Microsoft Windows 8.1",    "8.1"},
        { 0, 0, 0}
    },{
        /* Server editions */
        { 5, 2, "Microsoft Windows Server 2003",        "2003"},
        { 6, 0, "Microsoft Windows Server 2008",        "2008"},
        { 6, 1, "Microsoft Windows Server 2008 R2",     "2008r2"},
        { 6, 2, "Microsoft Windows Server 2012",        "2012"},
        { 6, 3, "Microsoft Windows Server 2012 R2",     "2012r2"},
        { 0, 0, 0},
        { 0, 0, 0}
    }
};

typedef struct _ga_win_10_0_t {
    int first_build;
    char const *version;
    char const *version_id;
} ga_win_10_0_t;

static ga_win_10_0_t const WIN_10_0_SERVER_VERSION_MATRIX[4] = {
    {14393, "Microsoft Windows Server 2016",    "2016"},
    {17763, "Microsoft Windows Server 2019",    "2019"},
    {20344, "Microsoft Windows Server 2022",    "2022"},
    {0, 0}
};

static ga_win_10_0_t const WIN_10_0_CLIENT_VERSION_MATRIX[3] = {
    {10240, "Microsoft Windows 10",    "10"},
    {22000, "Microsoft Windows 11",    "11"},
    {0, 0}
};

static void ga_get_win_version(RTL_OSVERSIONINFOEXW *info, Error **errp)
{
    typedef NTSTATUS(WINAPI *rtl_get_version_t)(
        RTL_OSVERSIONINFOEXW *os_version_info_ex);

    info->dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOEXW);

    HMODULE module = GetModuleHandle("ntdll");
    PVOID fun = GetProcAddress(module, "RtlGetVersion");
    if (fun == NULL) {
        error_setg(errp, QERR_QGA_COMMAND_FAILED,
            "Failed to get address of RtlGetVersion");
        return;
    }

    rtl_get_version_t rtl_get_version = (rtl_get_version_t)fun;
    rtl_get_version(info);
    return;
}

static char *ga_get_win_name(OSVERSIONINFOEXW const *os_version, bool id)
{
    DWORD major = os_version->dwMajorVersion;
    DWORD minor = os_version->dwMinorVersion;
    DWORD build = os_version->dwBuildNumber;
    int tbl_idx = (os_version->wProductType != VER_NT_WORKSTATION);
    ga_matrix_lookup_t const *table = WIN_VERSION_MATRIX[tbl_idx];
    ga_win_10_0_t const *win_10_0_table = tbl_idx ?
        WIN_10_0_SERVER_VERSION_MATRIX : WIN_10_0_CLIENT_VERSION_MATRIX;
    ga_win_10_0_t const *win_10_0_version = NULL;
    while (table->version != NULL) {
        if (major == 10 && minor == 0) {
            while (win_10_0_table->version != NULL) {
                if (build >= win_10_0_table->first_build) {
                    win_10_0_version = win_10_0_table;
                }
                win_10_0_table++;
            }
            if (win_10_0_table) {
                if (id) {
                    return g_strdup(win_10_0_version->version_id);
                } else {
                    return g_strdup(win_10_0_version->version);
                }
            }
        } else if (major == table->major && minor == table->minor) {
            if (id) {
                return g_strdup(table->version_id);
            } else {
                return g_strdup(table->version);
            }
        }
        ++table;
    }
    slog("failed to lookup Windows version: major=%lu, minor=%lu",
        major, minor);
    return g_strdup("N/A");
}

static char *ga_get_win_product_name(Error **errp)
{
    HKEY key = INVALID_HANDLE_VALUE;
    DWORD size = 128;
    char *result = g_malloc0(size);
    LONG err = ERROR_SUCCESS;

    err = RegOpenKeyA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      &key);
    if (err != ERROR_SUCCESS) {
        error_setg_win32(errp, err, "failed to open registry key");
        g_free(result);
        return NULL;
    }

    err = RegQueryValueExA(key, "ProductName", NULL, NULL,
                            (LPBYTE)result, &size);
    if (err == ERROR_MORE_DATA) {
        slog("ProductName longer than expected (%lu bytes), retrying",
                size);
        g_free(result);
        result = NULL;
        if (size > 0) {
            result = g_malloc0(size);
            err = RegQueryValueExA(key, "ProductName", NULL, NULL,
                                    (LPBYTE)result, &size);
        }
    }
    if (err != ERROR_SUCCESS) {
        error_setg_win32(errp, err, "failed to retrive ProductName");
        goto fail;
    }

    RegCloseKey(key);
    return result;

fail:
    if (key != INVALID_HANDLE_VALUE) {
        RegCloseKey(key);
    }
    g_free(result);
    return NULL;
}

static char *ga_get_current_arch(void)
{
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    char *result = NULL;
    switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        result = g_strdup("x86_64");
        break;
    case PROCESSOR_ARCHITECTURE_ARM:
        result = g_strdup("arm");
        break;
    case PROCESSOR_ARCHITECTURE_IA64:
        result = g_strdup("ia64");
        break;
    case PROCESSOR_ARCHITECTURE_INTEL:
        result = g_strdup("x86");
        break;
    case PROCESSOR_ARCHITECTURE_UNKNOWN:
    default:
        slog("unknown processor architecture 0x%0x",
            info.wProcessorArchitecture);
        result = g_strdup("unknown");
        break;
    }
    return result;
}

GuestOSInfo *qmp_guest_get_osinfo(Error **errp)
{
    Error *local_err = NULL;
    OSVERSIONINFOEXW os_version = {0};
    bool server;
    char *product_name;
    GuestOSInfo *info;

    ga_get_win_version(&os_version, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }

    server = os_version.wProductType != VER_NT_WORKSTATION;
    product_name = ga_get_win_product_name(errp);
    if (product_name == NULL) {
        return NULL;
    }

    info = g_new0(GuestOSInfo, 1);

    info->has_kernel_version = true;
    info->kernel_version = g_strdup_printf("%lu.%lu",
        os_version.dwMajorVersion,
        os_version.dwMinorVersion);
    info->has_kernel_release = true;
    info->kernel_release = g_strdup_printf("%lu",
        os_version.dwBuildNumber);
    info->has_machine = true;
    info->machine = ga_get_current_arch();

    info->has_id = true;
    info->id = g_strdup("mswindows");
    info->has_name = true;
    info->name = g_strdup("Microsoft Windows");
    info->has_pretty_name = true;
    info->pretty_name = product_name;
    info->has_version = true;
    info->version = ga_get_win_name(&os_version, false);
    info->has_version_id = true;
    info->version_id = ga_get_win_name(&os_version, true);
    info->has_variant = true;
    info->variant = g_strdup(server ? "server" : "client");
    info->has_variant_id = true;
    info->variant_id = g_strdup(server ? "server" : "client");

    return info;
}

/*
 * Safely get device property. Returned strings are using wide characters.
 * Caller is responsible for freeing the buffer.
 */
static LPBYTE cm_get_property(DEVINST devInst, const DEVPROPKEY *propName,
    PDEVPROPTYPE propType)
{
    CONFIGRET cr;
    g_autofree LPBYTE buffer = NULL;
    ULONG buffer_len = 0;

    /* First query for needed space */
    cr = CM_Get_DevNode_PropertyW(devInst, propName, propType,
        buffer, &buffer_len, 0);
    if (cr != CR_SUCCESS && cr != CR_BUFFER_SMALL) {

        slog("failed to get property size, error=0x%lx", cr);
        return NULL;
    }
    buffer = g_new0(BYTE, buffer_len + 1);
    cr = CM_Get_DevNode_PropertyW(devInst, propName, propType,
        buffer, &buffer_len, 0);
    if (cr != CR_SUCCESS) {
        slog("failed to get device property, error=0x%lx", cr);
        return NULL;
    }
    return g_steal_pointer(&buffer);
}

static GStrv ga_get_hardware_ids(DEVINST devInstance)
{
    GArray *values = NULL;
    DEVPROPTYPE cm_type;
    LPWSTR id;
    g_autofree LPWSTR property = (LPWSTR)cm_get_property(devInstance,
        &qga_DEVPKEY_Device_HardwareIds, &cm_type);
    if (property == NULL) {
        slog("failed to get hardware IDs");
        return NULL;
    }
    if (*property == '\0') {
        /* empty list */
        return NULL;
    }
    values = g_array_new(TRUE, TRUE, sizeof(gchar *));
    for (id = property; '\0' != *id; id += lstrlenW(id) + 1) {
        gchar *id8 = g_utf16_to_utf8(id, -1, NULL, NULL, NULL);
        g_array_append_val(values, id8);
    }
    return (GStrv)g_array_free(values, FALSE);
}

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/install/identifiers-for-pci-devices
 */
#define DEVICE_PCI_RE "PCI\\\\VEN_(1AF4|1B36)&DEV_([0-9A-B]{4})(&|$)"

GuestDeviceInfoList *qmp_guest_get_devices(Error **errp)
{
    GuestDeviceInfoList *head = NULL, **tail = &head;
    HDEVINFO dev_info = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA dev_info_data;
    int i, j;
    GError *gerr = NULL;
    g_autoptr(GRegex) device_pci_re = NULL;
    DEVPROPTYPE cm_type;

    device_pci_re = g_regex_new(DEVICE_PCI_RE,
        G_REGEX_ANCHORED | G_REGEX_OPTIMIZE, 0,
        &gerr);
    g_assert(device_pci_re != NULL);

    dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
    dev_info = SetupDiGetClassDevs(0, 0, 0, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (dev_info == INVALID_HANDLE_VALUE) {
        error_setg(errp, "failed to get device tree");
        return NULL;
    }

    slog("enumerating devices");
    for (i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
        bool skip = true;
        g_autofree LPWSTR name = NULL;
        g_autofree LPFILETIME date = NULL;
        g_autofree LPWSTR version = NULL;
        g_auto(GStrv) hw_ids = NULL;
        g_autoptr(GuestDeviceInfo) device = g_new0(GuestDeviceInfo, 1);
        g_autofree char *vendor_id = NULL;
        g_autofree char *device_id = NULL;

        name = (LPWSTR)cm_get_property(dev_info_data.DevInst,
            &qga_DEVPKEY_NAME, &cm_type);
        if (name == NULL) {
            slog("failed to get device description");
            continue;
        }
        device->driver_name = g_utf16_to_utf8(name, -1, NULL, NULL, NULL);
        if (device->driver_name == NULL) {
            error_setg(errp, "conversion to utf8 failed (driver name)");
            return NULL;
        }
        slog("querying device: %s", device->driver_name);
        hw_ids = ga_get_hardware_ids(dev_info_data.DevInst);
        if (hw_ids == NULL) {
            continue;
        }
        for (j = 0; hw_ids[j] != NULL; j++) {
            g_autoptr(GMatchInfo) match_info;
            GuestDeviceIdPCI *id;
            if (!g_regex_match(device_pci_re, hw_ids[j], 0, &match_info)) {
                continue;
            }
            skip = false;

            vendor_id = g_match_info_fetch(match_info, 1);
            device_id = g_match_info_fetch(match_info, 2);

            device->id = g_new0(GuestDeviceId, 1);
            device->has_id = true;
            device->id->type = GUEST_DEVICE_TYPE_PCI;
            id = &device->id->u.pci;
            id->vendor_id = g_ascii_strtoull(vendor_id, NULL, 16);
            id->device_id = g_ascii_strtoull(device_id, NULL, 16);

            break;
        }
        if (skip) {
            continue;
        }

        version = (LPWSTR)cm_get_property(dev_info_data.DevInst,
            &qga_DEVPKEY_Device_DriverVersion, &cm_type);
        if (version == NULL) {
            slog("failed to get driver version");
            continue;
        }
        device->driver_version = g_utf16_to_utf8(version, -1, NULL,
            NULL, NULL);
        if (device->driver_version == NULL) {
            error_setg(errp, "conversion to utf8 failed (driver version)");
            return NULL;
        }
        device->has_driver_version = true;

        date = (LPFILETIME)cm_get_property(dev_info_data.DevInst,
            &qga_DEVPKEY_Device_DriverDate, &cm_type);
        if (date == NULL) {
            slog("failed to get driver date");
            continue;
        }
        device->driver_date = filetime_to_ns(date);
        device->has_driver_date = true;

        slog("driver: %s\ndriver version: %" PRId64 ",%s\n",
             device->driver_name, device->driver_date,
             device->driver_version);
        QAPI_LIST_APPEND(tail, g_steal_pointer(&device));
    }

    if (dev_info != INVALID_HANDLE_VALUE) {
        SetupDiDestroyDeviceInfoList(dev_info);
    }
    return head;
}

char *qga_get_host_name(Error **errp)
{
    wchar_t tmp[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = G_N_ELEMENTS(tmp);

    if (GetComputerNameW(tmp, &size) == 0) {
        error_setg_win32(errp, GetLastError(), "failed close handle");
        return NULL;
    }

    return g_utf16_to_utf8(tmp, size, NULL, NULL, NULL);
}

GuestDiskStatsInfoList *qmp_guest_get_diskstats(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestCpuStatsList *qmp_guest_get_cpustats(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

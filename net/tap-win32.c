/*
 *  TAP-Win32 -- A kernel driver to provide virtual tap device functionality
 *               on Windows.  Originally derived from the CIPE-Win32
 *               project by Damion K. Wilson, with extensive modifications by
 *               James Yonan.
 *
 *  All source code which derives from the CIPE-Win32 project is
 *  Copyright (C) Damion K. Wilson, 2003, and is released under the
 *  GPL version 2 (see below).
 *
 *  All other source code is Copyright (C) James Yonan, 2003-2004,
 *  and is released under the GPL version 2 (see below).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program (see the file COPYING included with this
 *  distribution); if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "tap_int.h"

#include "clients.h"            /* net_init_tap */
#include "net/eth.h"
#include "net/net.h"
#include "net/tap.h"            /* tap_has_ufo, ... */
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include <windows.h>
#include <winioctl.h>

//=============
// TAP IOCTLs
//=============

#define TAP_CONTROL_CODE(request,method) \
  CTL_CODE (FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)

#define TAP_IOCTL_GET_MAC               TAP_CONTROL_CODE (1, METHOD_BUFFERED)
#define TAP_IOCTL_GET_VERSION           TAP_CONTROL_CODE (2, METHOD_BUFFERED)
#define TAP_IOCTL_GET_MTU               TAP_CONTROL_CODE (3, METHOD_BUFFERED)
#define TAP_IOCTL_GET_INFO              TAP_CONTROL_CODE (4, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_POINT_TO_POINT TAP_CONTROL_CODE (5, METHOD_BUFFERED)
#define TAP_IOCTL_SET_MEDIA_STATUS      TAP_CONTROL_CODE (6, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_MASQ      TAP_CONTROL_CODE (7, METHOD_BUFFERED)
#define TAP_IOCTL_GET_LOG_LINE          TAP_CONTROL_CODE (8, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_SET_OPT   TAP_CONTROL_CODE (9, METHOD_BUFFERED)

//=================
// Registry keys
//=================

#define ADAPTER_KEY "SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}"

#define NETWORK_CONNECTIONS_KEY "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}"

//======================
// Filesystem prefixes
//======================

#define USERMODEDEVICEDIR "\\\\.\\Global\\"
#define TAPSUFFIX         ".tap"


//======================
// Compile time configuration
//======================

//#define DEBUG_TAP_WIN32

/* FIXME: The asynch write path appears to be broken at
 * present. WriteFile() ignores the lpNumberOfBytesWritten parameter
 * for overlapped writes, with the result we return zero bytes sent,
 * and after handling a single packet, receive is disabled for this
 * interface. */
/* #define TUN_ASYNCHRONOUS_WRITES 1 */

#define TUN_BUFFER_SIZE 1560
#define TUN_MAX_BUFFER_COUNT 32

/*
 * The data member "buffer" must be the first element in the tun_buffer
 * structure. See the function, tap_win32_free_buffer.
 */
typedef struct tun_buffer_s {
    unsigned char buffer [TUN_BUFFER_SIZE];
    unsigned long read_size;
    struct tun_buffer_s* next;
} tun_buffer_t;

typedef struct tap_win32_overlapped {
    HANDLE handle;
    HANDLE read_event;
    HANDLE write_event;
    HANDLE output_queue_semaphore;
    HANDLE free_list_semaphore;
    HANDLE tap_semaphore;
    CRITICAL_SECTION output_queue_cs;
    CRITICAL_SECTION free_list_cs;
    OVERLAPPED read_overlapped;
    OVERLAPPED write_overlapped;
    tun_buffer_t buffers[TUN_MAX_BUFFER_COUNT];
    tun_buffer_t* free_list;
    tun_buffer_t* output_queue_front;
    tun_buffer_t* output_queue_back;
} tap_win32_overlapped_t;

static tap_win32_overlapped_t tap_overlapped;

static tun_buffer_t* get_buffer_from_free_list(tap_win32_overlapped_t* const overlapped)
{
    tun_buffer_t* buffer = NULL;
    WaitForSingleObject(overlapped->free_list_semaphore, INFINITE);
    EnterCriticalSection(&overlapped->free_list_cs);
    buffer = overlapped->free_list;
//    assert(buffer != NULL);
    overlapped->free_list = buffer->next;
    LeaveCriticalSection(&overlapped->free_list_cs);
    buffer->next = NULL;
    return buffer;
}

static void put_buffer_on_free_list(tap_win32_overlapped_t* const overlapped, tun_buffer_t* const buffer)
{
    EnterCriticalSection(&overlapped->free_list_cs);
    buffer->next = overlapped->free_list;
    overlapped->free_list = buffer;
    LeaveCriticalSection(&overlapped->free_list_cs);
    ReleaseSemaphore(overlapped->free_list_semaphore, 1, NULL);
}

static tun_buffer_t* get_buffer_from_output_queue(tap_win32_overlapped_t* const overlapped, const int block)
{
    tun_buffer_t* buffer = NULL;
    DWORD result, timeout = block ? INFINITE : 0L;

    // Non-blocking call
    result = WaitForSingleObject(overlapped->output_queue_semaphore, timeout);

    switch (result)
    {
        // The semaphore object was signaled.
        case WAIT_OBJECT_0:
            EnterCriticalSection(&overlapped->output_queue_cs);

            buffer = overlapped->output_queue_front;
            overlapped->output_queue_front = buffer->next;

            if(overlapped->output_queue_front == NULL) {
                overlapped->output_queue_back = NULL;
            }

            LeaveCriticalSection(&overlapped->output_queue_cs);
            break;

        // Semaphore was nonsignaled, so a time-out occurred.
        case WAIT_TIMEOUT:
            // Cannot open another window.
            break;
    }

    return buffer;
}

static tun_buffer_t* get_buffer_from_output_queue_immediate (tap_win32_overlapped_t* const overlapped)
{
    return get_buffer_from_output_queue(overlapped, 0);
}

static void put_buffer_on_output_queue(tap_win32_overlapped_t* const overlapped, tun_buffer_t* const buffer)
{
    EnterCriticalSection(&overlapped->output_queue_cs);

    if(overlapped->output_queue_front == NULL && overlapped->output_queue_back == NULL) {
        overlapped->output_queue_front = overlapped->output_queue_back = buffer;
    } else {
        buffer->next = NULL;
        overlapped->output_queue_back->next = buffer;
        overlapped->output_queue_back = buffer;
    }

    LeaveCriticalSection(&overlapped->output_queue_cs);

    ReleaseSemaphore(overlapped->output_queue_semaphore, 1, NULL);
}


static int is_tap_win32_dev(const char *guid)
{
    HKEY netcard_key;
    LONG status;
    DWORD len;
    int i = 0;

    status = RegOpenKeyEx(
        HKEY_LOCAL_MACHINE,
        ADAPTER_KEY,
        0,
        KEY_READ,
        &netcard_key);

    if (status != ERROR_SUCCESS) {
        return FALSE;
    }

    for (;;) {
        char enum_name[256];
        g_autofree char *unit_string = NULL;
        HKEY unit_key;
        char component_id_string[] = "ComponentId";
        char component_id[256];
        char net_cfg_instance_id_string[] = "NetCfgInstanceId";
        char net_cfg_instance_id[256];
        DWORD data_type;

        len = sizeof (enum_name);
        status = RegEnumKeyEx(
            netcard_key,
            i,
            enum_name,
            &len,
            NULL,
            NULL,
            NULL,
            NULL);

        if (status == ERROR_NO_MORE_ITEMS)
            break;
        else if (status != ERROR_SUCCESS) {
            return FALSE;
        }

        unit_string = g_strdup_printf("%s\\%s", ADAPTER_KEY, enum_name);

        status = RegOpenKeyEx(
            HKEY_LOCAL_MACHINE,
            unit_string,
            0,
            KEY_READ,
            &unit_key);

        if (status != ERROR_SUCCESS) {
            return FALSE;
        } else {
            len = sizeof (component_id);
            status = RegQueryValueEx(
                unit_key,
                component_id_string,
                NULL,
                &data_type,
                (LPBYTE)component_id,
                &len);

            if (!(status != ERROR_SUCCESS || data_type != REG_SZ)) {
                len = sizeof (net_cfg_instance_id);
                status = RegQueryValueEx(
                    unit_key,
                    net_cfg_instance_id_string,
                    NULL,
                    &data_type,
                    (LPBYTE)net_cfg_instance_id,
                    &len);

                if (status == ERROR_SUCCESS && data_type == REG_SZ) {
                    if (/* !strcmp (component_id, TAP_COMPONENT_ID) &&*/
                        !strcmp (net_cfg_instance_id, guid)) {
                        RegCloseKey (unit_key);
                        RegCloseKey (netcard_key);
                        return TRUE;
                    }
                }
            }
            RegCloseKey (unit_key);
        }
        ++i;
    }

    RegCloseKey (netcard_key);
    return FALSE;
}

static int get_device_guid(
    char *name,
    int name_size,
    char *actual_name,
    int actual_name_size)
{
    LONG status;
    HKEY control_net_key;
    DWORD len;
    int i = 0;
    int stop = 0;

    status = RegOpenKeyEx(
        HKEY_LOCAL_MACHINE,
        NETWORK_CONNECTIONS_KEY,
        0,
        KEY_READ,
        &control_net_key);

    if (status != ERROR_SUCCESS) {
        return -1;
    }

    while (!stop)
    {
        char enum_name[256];
        g_autofree char *connection_string = NULL;
        HKEY connection_key;
        char name_data[256];
        DWORD name_type;
        const char name_string[] = "Name";

        len = sizeof (enum_name);
        status = RegEnumKeyEx(
            control_net_key,
            i,
            enum_name,
            &len,
            NULL,
            NULL,
            NULL,
            NULL);

        if (status == ERROR_NO_MORE_ITEMS)
            break;
        else if (status != ERROR_SUCCESS) {
            return -1;
        }

        connection_string = g_strdup_printf("%s\\%s\\Connection",
             NETWORK_CONNECTIONS_KEY, enum_name);

        status = RegOpenKeyEx(
            HKEY_LOCAL_MACHINE,
            connection_string,
            0,
            KEY_READ,
            &connection_key);

        if (status == ERROR_SUCCESS) {
            len = sizeof (name_data);
            status = RegQueryValueEx(
                connection_key,
                name_string,
                NULL,
                &name_type,
                (LPBYTE)name_data,
                &len);

            if (status != ERROR_SUCCESS || name_type != REG_SZ) {
                ++i;
                continue;
            }
            else {
                if (is_tap_win32_dev(enum_name)) {
                    snprintf(name, name_size, "%s", enum_name);
                    if (actual_name) {
                        if (strcmp(actual_name, "") != 0) {
                            if (strcmp(name_data, actual_name) != 0) {
                                RegCloseKey (connection_key);
                                ++i;
                                continue;
                            }
                        }
                        else {
                            snprintf(actual_name, actual_name_size, "%s", name_data);
                        }
                    }
                    stop = 1;
                }
            }

            RegCloseKey (connection_key);
        }
        ++i;
    }

    RegCloseKey (control_net_key);

    if (stop == 0)
        return -1;

    return 0;
}

static int tap_win32_set_status(HANDLE handle, int status)
{
    unsigned long len = 0;

    return DeviceIoControl(handle, TAP_IOCTL_SET_MEDIA_STATUS,
                &status, sizeof (status),
                &status, sizeof (status), &len, NULL);
}

static void tap_win32_overlapped_init(tap_win32_overlapped_t* const overlapped, const HANDLE handle)
{
    overlapped->handle = handle;

    overlapped->read_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    overlapped->write_event = CreateEvent(NULL, FALSE, FALSE, NULL);

    overlapped->read_overlapped.Offset = 0;
    overlapped->read_overlapped.OffsetHigh = 0;
    overlapped->read_overlapped.hEvent = overlapped->read_event;

    overlapped->write_overlapped.Offset = 0;
    overlapped->write_overlapped.OffsetHigh = 0;
    overlapped->write_overlapped.hEvent = overlapped->write_event;

    InitializeCriticalSection(&overlapped->output_queue_cs);
    InitializeCriticalSection(&overlapped->free_list_cs);

    overlapped->output_queue_semaphore = CreateSemaphore(
        NULL,   // default security attributes
        0,   // initial count
        TUN_MAX_BUFFER_COUNT,   // maximum count
        NULL);  // unnamed semaphore

    if(!overlapped->output_queue_semaphore)  {
        fprintf(stderr, "error creating output queue semaphore!\n");
    }

    overlapped->free_list_semaphore = CreateSemaphore(
        NULL,   // default security attributes
        TUN_MAX_BUFFER_COUNT,   // initial count
        TUN_MAX_BUFFER_COUNT,   // maximum count
        NULL);  // unnamed semaphore

    if(!overlapped->free_list_semaphore)  {
        fprintf(stderr, "error creating free list semaphore!\n");
    }

    overlapped->free_list = overlapped->output_queue_front = overlapped->output_queue_back = NULL;

    {
        unsigned index;
        for(index = 0; index < TUN_MAX_BUFFER_COUNT; index++) {
            tun_buffer_t* element = &overlapped->buffers[index];
            element->next = overlapped->free_list;
            overlapped->free_list = element;
        }
    }
    /* To count buffers, initially no-signal. */
    overlapped->tap_semaphore = CreateSemaphore(NULL, 0, TUN_MAX_BUFFER_COUNT, NULL);
    if(!overlapped->tap_semaphore)
        fprintf(stderr, "error creating tap_semaphore.\n");
}

static int tap_win32_write(tap_win32_overlapped_t *overlapped,
                           const void *buffer, unsigned long size)
{
    unsigned long write_size;
    BOOL result;
    DWORD error;

#ifdef TUN_ASYNCHRONOUS_WRITES
    result = GetOverlappedResult( overlapped->handle, &overlapped->write_overlapped,
                                  &write_size, FALSE);

    if (!result && GetLastError() == ERROR_IO_INCOMPLETE)
        WaitForSingleObject(overlapped->write_event, INFINITE);
#endif

    result = WriteFile(overlapped->handle, buffer, size,
                       &write_size, &overlapped->write_overlapped);

#ifdef TUN_ASYNCHRONOUS_WRITES
    /* FIXME: we can't sensibly set write_size here, without waiting
     * for the IO to complete! Moreover, we can't return zero,
     * because that will disable receive on this interface, and we
     * also can't assume it will succeed and return the full size,
     * because that will result in the buffer being reclaimed while
     * the IO is in progress. */
#error Async writes are broken. Please disable TUN_ASYNCHRONOUS_WRITES.
#else /* !TUN_ASYNCHRONOUS_WRITES */
    if (!result) {
        error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            result = GetOverlappedResult(overlapped->handle,
                                         &overlapped->write_overlapped,
                                         &write_size, TRUE);
        }
    }
#endif

    if (!result) {
#ifdef DEBUG_TAP_WIN32
        LPTSTR msgbuf;
        error = GetLastError();
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM,
                      NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      &msgbuf, 0, NULL);
        fprintf(stderr, "Tap-Win32: Error WriteFile %d - %s\n", error, msgbuf);
        LocalFree(msgbuf);
#endif
        return 0;
    }

    return write_size;
}

static DWORD WINAPI tap_win32_thread_entry(LPVOID param)
{
    tap_win32_overlapped_t *overlapped = (tap_win32_overlapped_t*)param;
    unsigned long read_size;
    BOOL result;
    DWORD dwError;
    tun_buffer_t* buffer = get_buffer_from_free_list(overlapped);


    for (;;) {
        result = ReadFile(overlapped->handle,
                          buffer->buffer,
                          sizeof(buffer->buffer),
                          &read_size,
                          &overlapped->read_overlapped);
        if (!result) {
            dwError = GetLastError();
            if (dwError == ERROR_IO_PENDING) {
                WaitForSingleObject(overlapped->read_event, INFINITE);
                result = GetOverlappedResult( overlapped->handle, &overlapped->read_overlapped,
                                              &read_size, FALSE);
                if (!result) {
#ifdef DEBUG_TAP_WIN32
                    LPVOID lpBuffer;
                    dwError = GetLastError();
                    FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                                   NULL, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   (LPTSTR) & lpBuffer, 0, NULL );
                    fprintf(stderr, "Tap-Win32: Error GetOverlappedResult %d - %s\n", dwError, lpBuffer);
                    LocalFree( lpBuffer );
#endif
                }
            } else {
#ifdef DEBUG_TAP_WIN32
                LPVOID lpBuffer;
                FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                               NULL, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               (LPTSTR) & lpBuffer, 0, NULL );
                fprintf(stderr, "Tap-Win32: Error ReadFile %d - %s\n", dwError, lpBuffer);
                LocalFree( lpBuffer );
#endif
            }
        }

        if(read_size > 0) {
            buffer->read_size = read_size;
            put_buffer_on_output_queue(overlapped, buffer);
            ReleaseSemaphore(overlapped->tap_semaphore, 1, NULL);
            buffer = get_buffer_from_free_list(overlapped);
        }
    }

    return 0;
}

static int tap_win32_read(tap_win32_overlapped_t *overlapped,
                          uint8_t **pbuf, int max_size)
{
    int size = 0;

    tun_buffer_t* buffer = get_buffer_from_output_queue_immediate(overlapped);

    if(buffer != NULL) {
        *pbuf = buffer->buffer;
        size = (int)buffer->read_size;
        if(size > max_size) {
            size = max_size;
        }
    }

    return size;
}

static void tap_win32_free_buffer(tap_win32_overlapped_t *overlapped,
                                  uint8_t *pbuf)
{
    tun_buffer_t* buffer = (tun_buffer_t*)pbuf;
    put_buffer_on_free_list(overlapped, buffer);
}

static int tap_win32_open(tap_win32_overlapped_t **phandle,
                          const char *preferred_name)
{
    g_autofree char *device_path = NULL;
    char device_guid[0x100];
    int rc;
    HANDLE handle;
    BOOL bret;
    char name_buffer[0x100] = {0, };
    struct {
        unsigned long major;
        unsigned long minor;
        unsigned long debug;
    } version;
    DWORD version_len;
    DWORD idThread;

    if (preferred_name != NULL) {
        snprintf(name_buffer, sizeof(name_buffer), "%s", preferred_name);
    }

    rc = get_device_guid(device_guid, sizeof(device_guid), name_buffer, sizeof(name_buffer));
    if (rc)
        return -1;

    device_path = g_strdup_printf("%s%s%s",
              USERMODEDEVICEDIR,
              device_guid,
              TAPSUFFIX);

    handle = CreateFile (
        device_path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        0,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED,
        0 );

    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    bret = DeviceIoControl(handle, TAP_IOCTL_GET_VERSION,
                           &version, sizeof (version),
                           &version, sizeof (version), &version_len, NULL);

    if (bret == FALSE) {
        CloseHandle(handle);
        return -1;
    }

    if (!tap_win32_set_status(handle, TRUE)) {
        return -1;
    }

    tap_win32_overlapped_init(&tap_overlapped, handle);

    *phandle = &tap_overlapped;

    CreateThread(NULL, 0, tap_win32_thread_entry,
                 (LPVOID)&tap_overlapped, 0, &idThread);
    return 0;
}

/********************************************/

 typedef struct TAPState {
     NetClientState nc;
     tap_win32_overlapped_t *handle;
 } TAPState;

static void tap_cleanup(NetClientState *nc)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);

    qemu_del_wait_object(s->handle->tap_semaphore, NULL, NULL);

    /* FIXME: need to kill thread and close file handle:
       tap_win32_close(s);
    */
}

static ssize_t tap_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);

    return tap_win32_write(s->handle, buf, size);
}

static void tap_win32_send(void *opaque)
{
    TAPState *s = opaque;
    uint8_t *buf, *orig_buf;
    int max_size = 4096;
    int size;
    uint8_t min_pkt[ETH_ZLEN];
    size_t min_pktsz = sizeof(min_pkt);

    size = tap_win32_read(s->handle, &buf, max_size);
    if (size > 0) {
        orig_buf = buf;

        if (net_peer_needs_padding(&s->nc)) {
            if (eth_pad_short_frame(min_pkt, &min_pktsz, buf, size)) {
                buf = min_pkt;
                size = min_pktsz;
            }
        }

        qemu_send_packet(&s->nc, buf, size);
        tap_win32_free_buffer(s->handle, orig_buf);
    }
}

struct vhost_net *tap_get_vhost_net(NetClientState *nc)
{
    return NULL;
}

static NetClientInfo net_tap_win32_info = {
    .type = NET_CLIENT_DRIVER_TAP,
    .size = sizeof(TAPState),
    .receive = tap_receive,
    .cleanup = tap_cleanup,
};

static int tap_win32_init(NetClientState *peer, const char *model,
                          const char *name, const char *ifname)
{
    NetClientState *nc;
    TAPState *s;
    tap_win32_overlapped_t *handle;

    if (tap_win32_open(&handle, ifname) < 0) {
        printf("tap: Could not open '%s'\n", ifname);
        return -1;
    }

    nc = qemu_new_net_client(&net_tap_win32_info, peer, model, name);

    s = DO_UPCAST(TAPState, nc, nc);

    qemu_set_info_str(&s->nc, "tap: ifname=%s", ifname);

    s->handle = handle;

    qemu_add_wait_object(s->handle->tap_semaphore, tap_win32_send, s);

    return 0;
}

int net_init_tap(const Netdev *netdev, const char *name,
                 NetClientState *peer, Error **errp)
{
    /* FIXME error_setg(errp, ...) on failure */
    const NetdevTapOptions *tap;

    assert(netdev->type == NET_CLIENT_DRIVER_TAP);
    tap = &netdev->u.tap;

    if (!tap->ifname) {
        error_report("tap: no interface name");
        return -1;
    }

    if (tap_win32_init(peer, "tap", name, tap->ifname) == -1) {
        return -1;
    }

    return 0;
}

int tap_enable(NetClientState *nc)
{
    abort();
}

int tap_disable(NetClientState *nc)
{
    abort();
}

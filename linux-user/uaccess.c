/* User memory access */
#include "qemu/osdep.h"
#include "qemu/cutils.h"

#include "qemu.h"

void *lock_user(int type, abi_ulong guest_addr, size_t len, bool copy)
{
    void *host_addr;

    guest_addr = cpu_untagged_addr(thread_cpu, guest_addr);
    if (!access_ok_untagged(type, guest_addr, len)) {
        return NULL;
    }
    host_addr = g2h_untagged(guest_addr);
#ifdef DEBUG_REMAP
    if (copy) {
        host_addr = g_memdup(host_addr, len);
    } else {
        host_addr = g_malloc0(len);
    }
#endif
    return host_addr;
}

#ifdef DEBUG_REMAP
void unlock_user(void *host_ptr, abi_ulong guest_addr, size_t len);
{
    void *host_ptr_conv;

    if (!host_ptr) {
        return;
    }
    host_ptr_conv = g2h(thread_cpu, guest_addr);
    if (host_ptr == host_ptr_conv) {
        return;
    }
    if (len != 0) {
        memcpy(host_ptr_conv, host_ptr, len);
    }
    g_free(host_ptr);
}
#endif

void *lock_user_string(abi_ulong guest_addr)
{
    ssize_t len = target_strlen(guest_addr);
    if (len < 0) {
        return NULL;
    }
    return lock_user(VERIFY_READ, guest_addr, (size_t)len + 1, 1);
}

/* copy_from_user() and copy_to_user() are usually used to copy data
 * buffers between the target and host.  These internally perform
 * locking/unlocking of the memory.
 */
int copy_from_user(void *hptr, abi_ulong gaddr, size_t len)
{
    int ret = 0;
    void *ghptr = lock_user(VERIFY_READ, gaddr, len, 1);

    if (ghptr) {
        memcpy(hptr, ghptr, len);
        unlock_user(ghptr, gaddr, 0);
    } else {
        ret = -TARGET_EFAULT;
    }
    return ret;
}

int copy_to_user(abi_ulong gaddr, void *hptr, size_t len)
{
    int ret = 0;
    void *ghptr = lock_user(VERIFY_WRITE, gaddr, len, 0);

    if (ghptr) {
        memcpy(ghptr, hptr, len);
        unlock_user(ghptr, gaddr, len);
    } else {
        ret = -TARGET_EFAULT;
    }

    return ret;
}

/* Return the length of a string in target memory or -TARGET_EFAULT if
   access error  */
ssize_t target_strlen(abi_ulong guest_addr1)
{
    uint8_t *ptr;
    abi_ulong guest_addr;
    size_t max_len, len;

    guest_addr = guest_addr1;
    for(;;) {
        max_len = TARGET_PAGE_SIZE - (guest_addr & ~TARGET_PAGE_MASK);
        ptr = lock_user(VERIFY_READ, guest_addr, max_len, 1);
        if (!ptr)
            return -TARGET_EFAULT;
        len = qemu_strnlen((const char *)ptr, max_len);
        unlock_user(ptr, guest_addr, 0);
        guest_addr += len;
        /* we don't allow wrapping or integer overflow */
        if (guest_addr == 0 || (guest_addr - guest_addr1) > 0x7fffffff) {
            return -TARGET_EFAULT;
        }
        if (len != max_len) {
            break;
        }
    }
    return guest_addr - guest_addr1;
}

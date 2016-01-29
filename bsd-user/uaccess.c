/* User memory access */
#include "qemu/osdep.h"

#include "qemu.h"

/* copy_from_user() and copy_to_user() are usually used to copy data
 * buffers between the target and host.  These internally perform
 * locking/unlocking of the memory.
 */
abi_long copy_from_user(void *hptr, abi_ulong gaddr, size_t len)
{
    abi_long ret = 0;
    void *ghptr;

    if ((ghptr = lock_user(VERIFY_READ, gaddr, len, 1))) {
        memcpy(hptr, ghptr, len);
        unlock_user(ghptr, gaddr, 0);
    } else
        ret = -TARGET_EFAULT;

    return ret;
}


abi_long copy_to_user(abi_ulong gaddr, void *hptr, size_t len)
{
    abi_long ret = 0;
    void *ghptr;

    if ((ghptr = lock_user(VERIFY_WRITE, gaddr, len, 0))) {
        memcpy(ghptr, hptr, len);
        unlock_user(ghptr, gaddr, len);
    } else
        ret = -TARGET_EFAULT;

    return ret;
}

/* Return the length of a string in target memory or -TARGET_EFAULT if
   access error  */
abi_long target_strlen(abi_ulong guest_addr1)
{
    uint8_t *ptr;
    abi_ulong guest_addr;
    int max_len, len;

    guest_addr = guest_addr1;
    for(;;) {
        max_len = TARGET_PAGE_SIZE - (guest_addr & ~TARGET_PAGE_MASK);
        ptr = lock_user(VERIFY_READ, guest_addr, max_len, 1);
        if (!ptr)
            return -TARGET_EFAULT;
        len = qemu_strnlen((char *)ptr, max_len);
        unlock_user(ptr, guest_addr, 0);
        guest_addr += len;
        /* we don't allow wrapping or integer overflow */
        if (guest_addr == 0 ||
            (guest_addr - guest_addr1) > 0x7fffffff)
            return -TARGET_EFAULT;
        if (len != max_len)
            break;
    }
    return guest_addr - guest_addr1;
}

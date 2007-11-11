/* User memory access */
#include <stdio.h>
#include <string.h>

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


/* Return the length of a string in target memory.  */
/* FIXME - this doesn't check access_ok() - it's rather complicated to
 * do it correctly because we need to check the bytes in a page and then
 * skip to the next page and check the bytes there until we find the
 * terminator.  There should be a general function to do this that
 * can look for any byte terminator in a buffer - not strlen().
 */
abi_long target_strlen(abi_ulong gaddr)
{
    return strlen(g2h(gaddr));
}

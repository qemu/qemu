/*
 *  FreeBSD sysctl() and sysarch() system call emulation
 *
 *  Copyright (c) 2013-15 Stacey D. Son
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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "target_arch_sysarch.h"

#include <sys/sysctl.h>

/*
 * Length for the fixed length types.
 * 0 means variable length for strings and structures
 * Compare with sys/kern_sysctl.c ctl_size
 * Note: Not all types appear to be used in-tree.
 */
static const int guest_ctl_size[CTLTYPE + 1] = {
        [CTLTYPE_INT] = sizeof(abi_int),
        [CTLTYPE_UINT] = sizeof(abi_uint),
        [CTLTYPE_LONG] = sizeof(abi_long),
        [CTLTYPE_ULONG] = sizeof(abi_ulong),
        [CTLTYPE_S8] = sizeof(int8_t),
        [CTLTYPE_S16] = sizeof(int16_t),
        [CTLTYPE_S32] = sizeof(int32_t),
        [CTLTYPE_S64] = sizeof(int64_t),
        [CTLTYPE_U8] = sizeof(uint8_t),
        [CTLTYPE_U16] = sizeof(uint16_t),
        [CTLTYPE_U32] = sizeof(uint32_t),
        [CTLTYPE_U64] = sizeof(uint64_t),
};

static const int host_ctl_size[CTLTYPE + 1] = {
        [CTLTYPE_INT] = sizeof(int),
        [CTLTYPE_UINT] = sizeof(u_int),
        [CTLTYPE_LONG] = sizeof(long),
        [CTLTYPE_ULONG] = sizeof(u_long),
        [CTLTYPE_S8] = sizeof(int8_t),
        [CTLTYPE_S16] = sizeof(int16_t),
        [CTLTYPE_S32] = sizeof(int32_t),
        [CTLTYPE_S64] = sizeof(int64_t),
        [CTLTYPE_U8] = sizeof(uint8_t),
        [CTLTYPE_U16] = sizeof(uint16_t),
        [CTLTYPE_U32] = sizeof(uint32_t),
        [CTLTYPE_U64] = sizeof(uint64_t),
};

#ifdef TARGET_ABI32
/*
 * Limit the amount of available memory to be most of the 32-bit address
 * space. 0x100c000 was arrived at through trial and error as a good
 * definition of 'most'.
 */
static const abi_ulong guest_max_mem = UINT32_MAX - 0x100c000 + 1;

static abi_ulong cap_memory(uint64_t mem)
{
    return MIN(guest_max_mem, mem);
}
#endif

static abi_ulong scale_to_guest_pages(uint64_t pages)
{
    /* Scale pages from host to guest */
    pages = muldiv64(pages, qemu_real_host_page_size(), TARGET_PAGE_SIZE);
#ifdef TARGET_ABI32
    /* cap pages if need be */
    pages = MIN(pages, guest_max_mem / (abi_ulong)TARGET_PAGE_SIZE);
#endif
    return pages;
}

#ifdef TARGET_ABI32
/* Used only for TARGET_ABI32 */
static abi_long h2g_long_sat(long l)
{
    if (l > INT32_MAX) {
        l = INT32_MAX;
    } else if (l < INT32_MIN) {
        l = INT32_MIN;
    }
    return l;
}

static abi_ulong h2g_ulong_sat(u_long ul)
{
    return MIN(ul, UINT32_MAX);
}
#endif

/*
 * placeholder until bsd-user downstream upstreams this with its thread support
 */
#define bsd_get_ncpu() 1

/*
 * This uses the undocumented oidfmt interface to find the kind of a requested
 * sysctl, see /sys/kern/kern_sysctl.c:sysctl_sysctl_oidfmt() (compare to
 * src/sbin/sysctl/sysctl.c)
 */
static int oidfmt(int *oid, int len, char *fmt, uint32_t *kind)
{
    int qoid[CTL_MAXNAME + 2];
    uint8_t buf[BUFSIZ];
    int i;
    size_t j;

    qoid[0] = CTL_SYSCTL;
    qoid[1] = CTL_SYSCTL_OIDFMT;
    memcpy(qoid + 2, oid, len * sizeof(int));

    j = sizeof(buf);
    i = sysctl(qoid, len + 2, buf, &j, 0, 0);
    if (i) {
        return i;
    }

    if (kind) {
        *kind = *(uint32_t *)buf;
    }

    if (fmt) {
        strcpy(fmt, (char *)(buf + sizeof(uint32_t)));
    }
    return 0;
}

/*
 * Convert the old value from host to guest.
 *
 * For LONG and ULONG on ABI32, we need to 'down convert' the 8 byte quantities
 * to 4 bytes. The caller setup a buffer in host memory to get this data from
 * the kernel and pass it to us. We do the down conversion and adjust the length
 * so the caller knows what to write as the returned length into the target when
 * it copies the down converted values into the target.
 *
 * For normal integral types, we just need to byte swap. No size changes.
 *
 * For strings and node data, there's no conversion needed.
 *
 * For opaque data, per sysctl OID converts take care of it.
 */
static void h2g_old_sysctl(void *holdp, size_t *holdlen, uint32_t kind)
{
    size_t len;
    int hlen, glen;
    uint8_t *hp, *gp;

    /*
     * Although rare, we can have arrays of sysctl. Both sysctl_old_ddb in
     * kern_sysctl.c and show_var in sbin/sysctl/sysctl.c have code that loops
     * this way.  *holdlen has been set by the kernel to the host's length.
     * Only LONG and ULONG on ABI32 have different sizes: see below.
     */
    gp = hp = (uint8_t *)holdp;
    len = 0;
    hlen = host_ctl_size[kind & CTLTYPE];
    glen = guest_ctl_size[kind & CTLTYPE];

    /*
     * hlen == 0 for CTLTYPE_STRING and CTLTYPE_NODE, which need no conversion
     * as well as CTLTYPE_OPAQUE, which needs special converters.
     */
    if (hlen == 0) {
        return;
    }

    while (len < *holdlen) {
        if (hlen == glen) {
            switch (hlen) {
            case 1:
                /* Nothing needed: no byteswapping and assigning in place */
                break;
            case 2:
                *(uint16_t *)gp = tswap16(*(uint16_t *)hp);
                break;
            case 4:
                *(uint32_t *)gp = tswap32(*(uint32_t *)hp);
                break;
            case 8:
                *(uint64_t *)gp = tswap64(*(uint64_t *)hp);
                break;
            default:
                g_assert_not_reached();
            }
        } else {
#ifdef TARGET_ABI32
            /*
             * Saturating assignment for the only two types that differ between
             * 32-bit and 64-bit machines. All other integral types have the
             * same, fixed size and will be converted w/o loss of precision
             * in the above switch.
             */
            switch (kind & CTLTYPE) {
            case CTLTYPE_LONG:
                *(abi_long *)gp = tswap32(h2g_long_sat(*(long *)hp));
                break;
            case CTLTYPE_ULONG:
                *(abi_ulong *)gp = tswap32(h2g_ulong_sat(*(u_long *)hp));
                break;
            default:
                g_assert_not_reached();
            }
#else
            g_assert_not_reached();
#endif
        }
        gp += glen;
        hp += hlen;
        len += hlen;
    }
#ifdef TARGET_ABI32
    if (hlen != glen) {
        *holdlen = (*holdlen / hlen) * glen;
    }
#endif
}

/*
 * Convert the undocmented name2oid sysctl data for the target.
 */
static inline void sysctl_name2oid(uint32_t *holdp, size_t holdlen)
{
    size_t i, num = holdlen / sizeof(uint32_t);

    for (i = 0; i < num; i++) {
        holdp[i] = tswap32(holdp[i]);
    }
}

static inline void sysctl_oidfmt(uint32_t *holdp)
{
    /* byte swap the kind */
    holdp[0] = tswap32(holdp[0]);
}

static abi_long do_freebsd_sysctl_oid(CPUArchState *env, int32_t *snamep,
        int32_t namelen, void *holdp, size_t *holdlenp, void *hnewp,
        size_t newlen)
{
    uint32_t kind = 0;
    abi_long ret;
    size_t holdlen, oldlen;
#ifdef TARGET_ABI32
    void *old_holdp;
#endif

    holdlen = oldlen = *holdlenp;
    oidfmt(snamep, namelen, NULL, &kind);

    /* Handle some arch/emulator dependent sysctl()'s here. */
    switch (snamep[0]) {
    case CTL_KERN:
        switch (snamep[1]) {
        case KERN_USRSTACK:
            if (oldlen) {
                (*(abi_ulong *)holdp) = tswapal(TARGET_USRSTACK);
            }
            holdlen = sizeof(abi_ulong);
            ret = 0;
            goto out;

        case KERN_PS_STRINGS:
            if (oldlen) {
                (*(abi_ulong *)holdp) = tswapal(TARGET_PS_STRINGS);
            }
            holdlen = sizeof(abi_ulong);
            ret = 0;
            goto out;

        default:
            break;
        }
        break;

    case CTL_HW:
        switch (snamep[1]) {
        case HW_MACHINE:
            holdlen = sizeof(TARGET_HW_MACHINE);
            if (holdp) {
                strlcpy(holdp, TARGET_HW_MACHINE, oldlen);
            }
            ret = 0;
            goto out;

        case HW_MACHINE_ARCH:
        {
            holdlen = sizeof(TARGET_HW_MACHINE_ARCH);
            if (holdp) {
                strlcpy(holdp, TARGET_HW_MACHINE_ARCH, oldlen);
            }
            ret = 0;
            goto out;
        }
        case HW_NCPU:
            if (oldlen) {
                (*(abi_int *)holdp) = tswap32(bsd_get_ncpu());
            }
            holdlen = sizeof(int32_t);
            ret = 0;
            goto out;
#if defined(TARGET_ARM)
        case HW_FLOATINGPT:
            if (oldlen) {
                ARMCPU *cpu = env_archcpu(env);
                *(abi_int *)holdp = cpu_isar_feature(aa32_vfp, cpu);
            }
            holdlen = sizeof(abi_int);
            ret = 0;
            goto out;
#endif


#ifdef TARGET_ABI32
        case HW_PHYSMEM:
        case HW_USERMEM:
        case HW_REALMEM:
            holdlen = sizeof(abi_ulong);
            ret = 0;

            if (oldlen) {
                int mib[2] = {snamep[0], snamep[1]};
                unsigned long lvalue;
                size_t len = sizeof(lvalue);

                if (sysctl(mib, 2, &lvalue, &len, NULL, 0) == -1) {
                    ret = -1;
                } else {
                    lvalue = cap_memory(lvalue);
                    (*(abi_ulong *)holdp) = tswapal((abi_ulong)lvalue);
                }
            }
            goto out;
#endif

        default:
        {
            static int oid_hw_availpages;
            static int oid_hw_pagesizes;

            if (!oid_hw_availpages) {
                int real_oid[CTL_MAXNAME + 2];
                size_t len = sizeof(real_oid) / sizeof(int);

                if (sysctlnametomib("hw.availpages", real_oid, &len) >= 0) {
                    oid_hw_availpages = real_oid[1];
                }
            }
            if (!oid_hw_pagesizes) {
                int real_oid[CTL_MAXNAME + 2];
                size_t len = sizeof(real_oid) / sizeof(int);

                if (sysctlnametomib("hw.pagesizes", real_oid, &len) >= 0) {
                    oid_hw_pagesizes = real_oid[1];
                }
            }

            if (oid_hw_availpages && snamep[1] == oid_hw_availpages) {
                long lvalue;
                size_t len = sizeof(lvalue);

                if (sysctlbyname("hw.availpages", &lvalue, &len, NULL, 0) == -1) {
                    ret = -1;
                } else {
                    if (oldlen) {
                        lvalue = scale_to_guest_pages(lvalue);
                        (*(abi_ulong *)holdp) = tswapal((abi_ulong)lvalue);
                    }
                    holdlen = sizeof(abi_ulong);
                    ret = 0;
                }
                goto out;
            }

            if (oid_hw_pagesizes && snamep[1] == oid_hw_pagesizes) {
                if (oldlen) {
                    (*(abi_ulong *)holdp) = tswapal((abi_ulong)TARGET_PAGE_SIZE);
                    ((abi_ulong *)holdp)[1] = 0;
                }
                holdlen = sizeof(abi_ulong) * 2;
                ret = 0;
                goto out;
            }
            break;
        }
        }
        break;

    default:
        break;
    }

#ifdef TARGET_ABI32
    /*
     * For long and ulong with a 64-bit host and a 32-bit target we have to do
     * special things. holdlen here is the length provided by the target to the
     * system call. So we allocate a buffer twice as large because longs are
     * twice as big on the host which will be writing them. In h2g_old_sysctl
     * we'll adjust them and adjust the length.
     */
    if (kind == CTLTYPE_LONG || kind == CTLTYPE_ULONG) {
        old_holdp = holdp;
        holdlen = holdlen * 2;
        holdp = g_malloc(holdlen);
    }
#endif

    ret = get_errno(sysctl(snamep, namelen, holdp, &holdlen, hnewp, newlen));
    if (!ret && (holdp != 0)) {

        if (snamep[0] == CTL_SYSCTL) {
            switch (snamep[1]) {
            case CTL_SYSCTL_NEXT:
            case CTL_SYSCTL_NAME2OID:
            case CTL_SYSCTL_NEXTNOSKIP:
                /*
                 * All of these return an OID array, so we need to convert to
                 * target.
                 */
                sysctl_name2oid(holdp, holdlen);
                break;

            case CTL_SYSCTL_OIDFMT:
                /* Handle oidfmt */
                sysctl_oidfmt(holdp);
                break;
            case CTL_SYSCTL_OIDDESCR:
            case CTL_SYSCTL_OIDLABEL:
            default:
                /* Handle it based on the type */
                h2g_old_sysctl(holdp, &holdlen, kind);
                /* NB: None of these are LONG or ULONG */
                break;
            }
        } else {
            /*
             * Need to convert from host to target. All the weird special cases
             * are handled above.
             */
            h2g_old_sysctl(holdp, &holdlen, kind);
#ifdef TARGET_ABI32
            /*
             * For the 32-bit on 64-bit case, for longs we need to copy the
             * now-converted buffer to the target and free the buffer.
             */
            if (kind == CTLTYPE_LONG || kind == CTLTYPE_ULONG) {
                memcpy(old_holdp, holdp, holdlen);
                g_free(holdp);
                holdp = old_holdp;
            }
#endif
        }
    }

out:
    *holdlenp = holdlen;
    return ret;
}

/*
 * This syscall was created to make sysctlbyname(3) more efficient, but we can't
 * really provide it in bsd-user.  Notably, we must always translate the names
 * independently since some sysctl values have to be faked for the target
 * environment, so it still has to break down to two syscalls for the underlying
 * implementation.
 */
abi_long do_freebsd_sysctlbyname(CPUArchState *env, abi_ulong namep,
        int32_t namelen, abi_ulong oldp, abi_ulong oldlenp, abi_ulong newp,
        abi_ulong newlen)
{
    abi_long ret = -TARGET_EFAULT;
    void *holdp = NULL, *hnewp = NULL;
    char *snamep = NULL;
    int oid[CTL_MAXNAME + 2];
    size_t holdlen, oidplen;
    abi_ulong oldlen = 0;

    /* oldlenp is read/write, pre-check here for write */
    if (oldlenp) {
        if (!access_ok(VERIFY_WRITE, oldlenp, sizeof(abi_ulong)) ||
            get_user_ual(oldlen, oldlenp)) {
            goto out;
        }
    }
    snamep = lock_user_string(namep);
    if (snamep == NULL) {
        goto out;
    }
    if (newp) {
        hnewp = lock_user(VERIFY_READ, newp, newlen, 1);
        if (hnewp == NULL) {
            goto out;
        }
    }
    if (oldp) {
        holdp = lock_user(VERIFY_WRITE, oldp, oldlen, 0);
        if (holdp == NULL) {
            goto out;
        }
    }
    holdlen = oldlen;

    oidplen = ARRAY_SIZE(oid);
    if (sysctlnametomib(snamep, oid, &oidplen) != 0) {
        ret = -TARGET_EINVAL;
        goto out;
    }

    ret = do_freebsd_sysctl_oid(env, oid, oidplen, holdp, &holdlen, hnewp,
        newlen);

    /*
     * writeability pre-checked above. __sysctl(2) returns ENOMEM and updates
     * oldlenp for the proper size to use.
     */
    if (oldlenp && (ret == 0 || ret == -TARGET_ENOMEM)) {
        put_user_ual(holdlen, oldlenp);
    }
out:
    unlock_user(snamep, namep, 0);
    unlock_user(holdp, oldp, ret == 0 ? holdlen : 0);
    unlock_user(hnewp, newp, 0);

    return ret;
}

abi_long do_freebsd_sysctl(CPUArchState *env, abi_ulong namep, int32_t namelen,
        abi_ulong oldp, abi_ulong oldlenp, abi_ulong newp, abi_ulong newlen)
{
    abi_long ret = -TARGET_EFAULT;
    void *hnamep, *holdp = NULL, *hnewp = NULL;
    size_t holdlen;
    abi_ulong oldlen = 0;
    int32_t *snamep = g_malloc(sizeof(int32_t) * namelen), *p, *q, i;

    /* oldlenp is read/write, pre-check here for write */
    if (oldlenp) {
        if (!access_ok(VERIFY_WRITE, oldlenp, sizeof(abi_ulong)) ||
            get_user_ual(oldlen, oldlenp)) {
            goto out;
        }
    }
    hnamep = lock_user(VERIFY_READ, namep, namelen, 1);
    if (hnamep == NULL) {
        goto out;
    }
    if (newp) {
        hnewp = lock_user(VERIFY_READ, newp, newlen, 1);
        if (hnewp == NULL) {
            goto out;
        }
    }
    if (oldp) {
        holdp = lock_user(VERIFY_WRITE, oldp, oldlen, 0);
        if (holdp == NULL) {
            goto out;
        }
    }
    holdlen = oldlen;
    for (p = hnamep, q = snamep, i = 0; i < namelen; p++, i++, q++) {
        *q = tswap32(*p);
    }

    ret = do_freebsd_sysctl_oid(env, snamep, namelen, holdp, &holdlen, hnewp,
        newlen);

    /*
     * writeability pre-checked above. __sysctl(2) returns ENOMEM and updates
     * oldlenp for the proper size to use.
     */
    if (oldlenp && (ret == 0 || ret == -TARGET_ENOMEM)) {
        put_user_ual(holdlen, oldlenp);
    }
    unlock_user(hnamep, namep, 0);
    unlock_user(holdp, oldp, ret == 0 ? holdlen : 0);
out:
    g_free(snamep);
    return ret;
}

/* sysarch() is architecture dependent. */
abi_long do_freebsd_sysarch(void *cpu_env, abi_long arg1, abi_long arg2)
{
    return do_freebsd_arch_sysarch(cpu_env, arg1, arg2);
}

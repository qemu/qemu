/*
 *  Darwin syscalls
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2006 Pierre d'Herbemont
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <mach/host_info.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/message.h>

#include <pthread.h>
#include <dirent.h>

#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/dirent.h>
#include <sys/uio.h>
#include <sys/termios.h>
#include <sys/ptrace.h>
#include <net/if.h>

#include <sys/param.h>
#include <sys/mount.h>

#include <sys/attr.h>

#include <mach/ndr.h>
#include <mach/mig_errors.h>

#include <sys/xattr.h>

#include "qemu.h"

//#define DEBUG_SYSCALL

#ifdef DEBUG_SYSCALL
# define DEBUG_FORCE_ENABLE_LOCAL() int __DEBUG_qemu_user_force_enable = 1
# define DEBUG_BEGIN_ENABLE  __DEBUG_qemu_user_force_enable = 1;
# define DEBUG_END_ENABLE  __DEBUG_qemu_user_force_enable = 0;

# define DEBUG_DISABLE_ALL() static int __DEBUG_qemu_user_force_enable = 0
# define DEBUG_ENABLE_ALL()  static int __DEBUG_qemu_user_force_enable = 1
    DEBUG_ENABLE_ALL();

# define DPRINTF(...) do { qemu_log(__VA_ARGS__); \
                           if(__DEBUG_qemu_user_force_enable) fprintf(stderr, __VA_ARGS__); \
                         } while(0)
#else
# define DEBUG_FORCE_ENABLE_LOCAL()
# define DEBUG_BEGIN_ENABLE
# define DEBUG_END_ENABLE

# define DPRINTF(...) do { qemu_log(__VA_ARGS__); } while(0)
#endif

enum {
    bswap_out = 0,
    bswap_in = 1
};

extern const char *interp_prefix;

static inline long get_errno(long ret)
{
    if (ret == -1)
        return -errno;
    else
        return ret;
}

static inline int is_error(long ret)
{
    return (unsigned long)ret >= (unsigned long)(-4096);
}

/* ------------------------------------------------------------
   Mach syscall handling
*/

void static inline print_description_msg_header(mach_msg_header_t *hdr)
{
    char *name = NULL;
    int i;
    struct { int number; char *name; } msg_name[] =
    {
        /* see http://fxr.watson.org/fxr/source/compat/mach/mach_namemap.c?v=NETBSD */
        { 200,      "host_info" },
        { 202,      "host_page_size" },
        { 206,      "host_get_clock_service" },
        { 206,      "host_get_clock_service" },
        { 206,      "host_get_clock_service" },
        { 306,      "host_get_clock_service" },
        { 3204,     "mach_port_allocate" },
        { 3206,     "mach_port_deallocate" },
        { 3404,     "mach_ports_lookup" },
        { 3409,     "mach_task_get_special_port" },
        { 3414,     "mach_task_get_exception_ports" },
        { 3418,     "mach_semaphore_create" },
        { 3504,     "mach_semaphore_create" },
        { 3509,     "mach_semaphore_create" },
        { 3518,     "semaphore_create" },
        { 3616,     "thread_policy" },
        { 3801,     "vm_allocate" },
        { 3802,     "vm_deallocate" },
        { 3802,     "vm_deallocate" },
        { 3803,     "vm_protect" },
        { 3812,     "vm_map" },
        { 4241776,  "lu_message_send_id" },  /* lookupd */
        { 4241876,  "lu_message_reply_id" }, /* lookupd */
    };

    for(i = 0; i < ARRAY_SIZE(msg_name); i++) {
        if(msg_name[i].number == hdr->msgh_id)
        {
            name = msg_name[i].name;
            break;
        }
    }
    if(!name)
        DPRINTF("unknown mach msg %d 0x%x\n", hdr->msgh_id, hdr->msgh_id);
    else
        DPRINTF("%s\n", name);
#if 0
    DPRINTF("Bits: %8x\n", hdr->msgh_bits);
    DPRINTF("Size: %8x\n", hdr->msgh_size);
    DPRINTF("Rmte: %8x\n", hdr->msgh_remote_port);
    DPRINTF("Locl: %8x\n", hdr->msgh_local_port);
    DPRINTF("Rsrv: %8x\n", hdr->msgh_reserved);

    DPRINTF("Id  : %8x\n", hdr->msgh_id);

    NDR_record_t *ndr = (NDR_record_t *)(hdr + 1);
    DPRINTF("hdr = %p, sizeof(hdr) = %x, NDR = %p\n", hdr, (unsigned int)sizeof(mach_msg_header_t), ndr);
    DPRINTF("%d %d %d %d %d %d %d %d\n",
           ndr->mig_vers, ndr->if_vers, ndr->reserved1, ndr->mig_encoding,
           ndr->int_rep, ndr->char_rep, ndr->float_rep, ndr->reserved2);
#endif
}

static inline void print_mach_msg_return(mach_msg_return_t ret)
{
    int i, found = 0;
#define MACH_MSG_RET(msg) { msg, #msg }
    struct { int code; char *name; } msg_name[] =
    {
        /* ref: http://darwinsource.opendarwin.org/10.4.2/xnu-792.2.4/osfmk/man/mach_msg.html */
        /* send message */
        MACH_MSG_RET(MACH_SEND_MSG_TOO_SMALL),
        MACH_MSG_RET(MACH_SEND_NO_BUFFER),
        MACH_MSG_RET(MACH_SEND_INVALID_DATA),
        MACH_MSG_RET(MACH_SEND_INVALID_HEADER),
        MACH_MSG_RET(MACH_SEND_INVALID_DEST),
        MACH_MSG_RET(MACH_SEND_INVALID_NOTIFY),
        MACH_MSG_RET(MACH_SEND_INVALID_REPLY),
        MACH_MSG_RET(MACH_SEND_INVALID_TRAILER),
        MACH_MSG_RET(MACH_SEND_INVALID_MEMORY),
        MACH_MSG_RET(MACH_SEND_INVALID_RIGHT),
        MACH_MSG_RET(MACH_SEND_INVALID_TYPE),
        MACH_MSG_RET(MACH_SEND_INTERRUPTED),
        MACH_MSG_RET(MACH_SEND_TIMED_OUT),

        MACH_MSG_RET(MACH_RCV_BODY_ERROR),
        MACH_MSG_RET(MACH_RCV_HEADER_ERROR),

        MACH_MSG_RET(MACH_RCV_IN_SET),
        MACH_MSG_RET(MACH_RCV_INTERRUPTED),

        MACH_MSG_RET(MACH_RCV_INVALID_DATA),
        MACH_MSG_RET(MACH_RCV_INVALID_NAME),
        MACH_MSG_RET(MACH_RCV_INVALID_NOTIFY),
        MACH_MSG_RET(MACH_RCV_INVALID_TRAILER),
        MACH_MSG_RET(MACH_RCV_INVALID_TYPE),

        MACH_MSG_RET(MACH_RCV_PORT_CHANGED),
        MACH_MSG_RET(MACH_RCV_PORT_DIED),

        MACH_MSG_RET(MACH_RCV_SCATTER_SMALL),
        MACH_MSG_RET(MACH_RCV_TIMED_OUT),
        MACH_MSG_RET(MACH_RCV_TOO_LARGE)
    };
#undef MACH_MSG_RET

    if( ret == MACH_MSG_SUCCESS)
        DPRINTF("MACH_MSG_SUCCESS\n");
    else
    {
        for( i = 0; i < ARRAY_SIZE(msg_name); i++) {
            if(msg_name[i].code == ret) {
                DPRINTF("%s\n", msg_name[i].name);
                found = 1;
                break;
            }
        }
        if(!found)
            qerror("unknow mach message ret code %d\n", ret);
    }
}

static inline void swap_mach_msg_header(mach_msg_header_t *hdr)
{
    hdr->msgh_bits = tswap32(hdr->msgh_bits);
    hdr->msgh_size = tswap32(hdr->msgh_size);
    hdr->msgh_remote_port = tswap32(hdr->msgh_remote_port);
    hdr->msgh_local_port = tswap32(hdr->msgh_local_port);
    hdr->msgh_reserved = tswap32(hdr->msgh_reserved);
    hdr->msgh_id = tswap32(hdr->msgh_id);
}

struct complex_msg {
            mach_msg_header_t hdr;
            mach_msg_body_t body;
};

static inline void swap_mach_msg_body(struct complex_msg *complex_msg, int bswap)
{
    mach_msg_port_descriptor_t *descr = (mach_msg_port_descriptor_t *)(complex_msg+1);
    int i,j;

    if(bswap == bswap_in)
        tswap32s(&complex_msg->body.msgh_descriptor_count);

    DPRINTF("body.msgh_descriptor_count %d\n", complex_msg->body.msgh_descriptor_count);

    for(i = 0; i < complex_msg->body.msgh_descriptor_count; i++) {
        switch(descr->type)
        {
            case MACH_MSG_PORT_DESCRIPTOR:
                tswap32s(&descr->name);
                descr++;
                break;
            case MACH_MSG_OOL_DESCRIPTOR:
            {
                mach_msg_ool_descriptor_t *ool = (void *)descr;
                tswap32s((uint32_t *)&ool->address);
                tswap32s(&ool->size);

                descr = (mach_msg_port_descriptor_t *)(ool+1);
                break;
            }
            case MACH_MSG_OOL_PORTS_DESCRIPTOR:
            {
                mach_msg_ool_ports_descriptor_t *ool_ports = (void *)descr;
                mach_port_name_t * port_names;

                if(bswap == bswap_in)
                {
                    tswap32s((uint32_t *)&ool_ports->address);
                    tswap32s(&ool_ports->count);
                }

                port_names = ool_ports->address;

                for(j = 0; j < ool_ports->count; j++)
                    tswap32s(&port_names[j]);

                if(bswap == bswap_out)
                {
                    tswap32s((uint32_t *)&ool_ports->address);
                    tswap32s(&ool_ports->count);
                }

                descr = (mach_msg_port_descriptor_t *)(ool_ports+1);
                break;
            }
            default: qerror("unknow mach msg descriptor type %x\n", descr->type);
        }
    }
    if(bswap == bswap_out)
        tswap32s(&complex_msg->body.msgh_descriptor_count);
}

static inline void swap_mach_msg(mach_msg_header_t *hdr, int bswap)
{
    if (bswap == bswap_out && hdr->msgh_bits & MACH_MSGH_BITS_COMPLEX)
        swap_mach_msg_body((struct complex_msg *)hdr, bswap);

    swap_mach_msg_header(hdr);

    if (bswap == bswap_in && hdr->msgh_bits & MACH_MSGH_BITS_COMPLEX)
        swap_mach_msg_body((struct complex_msg *)hdr, bswap);
}

static inline uint32_t target_mach_msg_trap(
        mach_msg_header_t *hdr, uint32_t options, uint32_t send_size,
        uint32_t rcv_size, uint32_t rcv_name, uint32_t time_out, uint32_t notify)
{
    extern int mach_msg_trap(mach_msg_header_t *, mach_msg_option_t,
          mach_msg_size_t, mach_msg_size_t, mach_port_t,
          mach_msg_timeout_t, mach_port_t);
    mach_msg_audit_trailer_t *trailer;
    mach_msg_id_t msg_id;
    uint32_t ret = 0;
    int i;

    swap_mach_msg(hdr, bswap_in);

    msg_id = hdr->msgh_id;

    print_description_msg_header(hdr);

    ret = mach_msg_trap(hdr, options, send_size, rcv_size, rcv_name, time_out, notify);

    print_mach_msg_return(ret);

    if( (options & MACH_RCV_MSG) && (REQUESTED_TRAILER_SIZE(options) > 0) )
    {
        /* XXX: the kernel always return the full trailer with MACH_SEND_MSG, so we should
                probably always bswap it  */
        /* warning: according to Mac OS X Internals (the book) msg_size might be expressed in
                    natural_t units but according to xnu/osfmk/mach/message.h: "The size of
                    the message must be specified in bytes" */
        trailer = (mach_msg_audit_trailer_t *)((uint8_t *)hdr + hdr->msgh_size);
        /* XXX: Should probably do that based on the option asked by the sender, but dealing
        with kernel answer seems more sound */
        switch(trailer->msgh_trailer_size)
        {
            case sizeof(mach_msg_audit_trailer_t):
                for(i = 0; i < 8; i++)
                    tswap32s(&trailer->msgh_audit.val[i]);
                /* Fall in mach_msg_security_trailer_t case */
            case sizeof(mach_msg_security_trailer_t):
                tswap32s(&trailer->msgh_sender.val[0]);
                tswap32s(&trailer->msgh_sender.val[1]);
                /* Fall in mach_msg_seqno_trailer_t case */
            case sizeof(mach_msg_seqno_trailer_t):
                tswap32s(&trailer->msgh_seqno);
                /* Fall in mach_msg_trailer_t case */
            case sizeof(mach_msg_trailer_t):
                tswap32s(&trailer->msgh_trailer_type);
                tswap32s(&trailer->msgh_trailer_size);
                break;
            case 0:
                /* Safer not to byteswap, but probably wrong */
                break;
            default:
                qerror("unknow trailer type given its size %d\n", trailer->msgh_trailer_size);
                break;
        }
    }

    /* Special message handling */
    switch (msg_id) {
        case 200: /* host_info */
        {
            mig_reply_error_t *err = (mig_reply_error_t *)hdr;
            struct {
                uint32_t unknow1;
                uint32_t max_cpus;
                uint32_t avail_cpus;
                uint32_t memory_size;
                uint32_t cpu_type;
                uint32_t cpu_subtype;
            } *data = (void *)(err+1);

            DPRINTF("maxcpu = 0x%x\n",   data->max_cpus);
            DPRINTF("numcpu = 0x%x\n",   data->avail_cpus);
            DPRINTF("memsize = 0x%x\n",  data->memory_size);

#if defined(TARGET_I386)
            data->cpu_type = CPU_TYPE_I386;
            DPRINTF("cpu_type changed to 0x%x(i386)\n", data->cpu_type);
            data->cpu_subtype = CPU_SUBTYPE_PENT;
            DPRINTF("cpu_subtype changed to 0x%x(i386_pent)\n", data->cpu_subtype);
#elif defined(TARGET_PPC)
            data->cpu_type = CPU_TYPE_POWERPC;
            DPRINTF("cpu_type changed to 0x%x(ppc)\n", data->cpu_type);
            data->cpu_subtype = CPU_SUBTYPE_POWERPC_750;
            DPRINTF("cpu_subtype changed to 0x%x(ppc_all)\n", data->cpu_subtype);
#else
# error target not supported
#endif
            break;
        }
        case 202: /* host_page_size */
        {
            mig_reply_error_t *err = (mig_reply_error_t *)hdr;
            uint32_t *pagesize = (uint32_t *)(err+1);

            DPRINTF("pagesize = %d\n", *pagesize);
            break;
        }
        default: break;
    }

    swap_mach_msg(hdr, bswap_out);

    return ret;
}

long do_mach_syscall(void *cpu_env, int num, uint32_t arg1, uint32_t arg2, uint32_t arg3,
                uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7,
                uint32_t arg8)
{
    extern uint32_t mach_reply_port(void);

    long ret = 0;

    arg1 = tswap32(arg1);
    arg2 = tswap32(arg2);
    arg3 = tswap32(arg3);
    arg4 = tswap32(arg4);
    arg5 = tswap32(arg5);
    arg6 = tswap32(arg6);
    arg7 = tswap32(arg7);
    arg8 = tswap32(arg8);

    DPRINTF("mach syscall %d : " , num);

    switch(num) {
    /* see xnu/osfmk/mach/syscall_sw.h */
    case -26:
        DPRINTF("mach_reply_port()\n");
        ret = mach_reply_port();
        break;
    case -27:
        DPRINTF("mach_thread_self()\n");
        ret = mach_thread_self();
        break;
    case -28:
        DPRINTF("mach_task_self()\n");
        ret = mach_task_self();
        break;
    case -29:
        DPRINTF("mach_host_self()\n");
        ret = mach_host_self();
        break;
    case -31:
        DPRINTF("mach_msg_trap(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x)\n",
                arg1, arg2, arg3, arg4, arg5, arg6, arg7);
        ret = target_mach_msg_trap((mach_msg_header_t *)arg1, arg2, arg3, arg4, arg5, arg6, arg7);
        break;
/* may need more translation if target arch is different from host */
#if (defined(TARGET_I386) && defined(__i386__)) || (defined(TARGET_PPC) && defined(__ppc__))
    case -33:
        DPRINTF("semaphore_signal_trap(0x%x)\n", arg1);
        ret = semaphore_signal_trap(arg1);
        break;
    case -34:
        DPRINTF("semaphore_signal_all_trap(0x%x)\n", arg1);
        ret = semaphore_signal_all_trap(arg1);
        break;
    case -35:
        DPRINTF("semaphore_signal_thread_trap(0x%x)\n", arg1, arg2);
        ret = semaphore_signal_thread_trap(arg1,arg2);
        break;
#endif
    case -36:
        DPRINTF("semaphore_wait_trap(0x%x)\n", arg1);
        extern int semaphore_wait_trap(int); // XXX: is there any header for that?
        ret = semaphore_wait_trap(arg1);
        break;
/* may need more translation if target arch is different from host */
#if (defined(TARGET_I386) && defined(__i386__)) || (defined(TARGET_PPC) && defined(__ppc__))
    case -37:
        DPRINTF("semaphore_wait_signal_trap(0x%x, 0x%x)\n", arg1, arg2);
        ret = semaphore_wait_signal_trap(arg1,arg2);
        break;
#endif
    case -43:
        DPRINTF("map_fd(0x%x, 0x%x, 0x%x, 0x%x, 0x%x)\n",
                arg1, arg2, arg3, arg4, arg5);
        ret = map_fd(arg1, arg2, (void*)arg3, arg4, arg5);
        tswap32s((uint32_t*)arg3);
        break;
/* may need more translation if target arch is different from host */
#if (defined(TARGET_I386) && defined(__i386__)) || (defined(TARGET_PPC) && defined(__ppc__))
    case -61:
        DPRINTF("syscall_thread_switch(0x%x, 0x%x, 0x%x)\n",
                arg1, arg2, arg3);
        ret = syscall_thread_switch(arg1, arg2, arg3);  // just a hint to the scheduler; can drop?
        break;
#endif
    case -89:
        DPRINTF("mach_timebase_info(0x%x)\n", arg1);
        struct mach_timebase_info info;
        ret = mach_timebase_info(&info);
        if(!is_error(ret))
        {
            struct mach_timebase_info *outInfo = (void*)arg1;
            outInfo->numer = tswap32(info.numer);
            outInfo->denom = tswap32(info.denom);
        }
        break;
    case -90:
        DPRINTF("mach_wait_until()\n");
        extern int mach_wait_until(uint64_t); // XXX: is there any header for that?
        ret = mach_wait_until(((uint64_t)arg2<<32) | (uint64_t)arg1);
        break;
    case -91:
        DPRINTF("mk_timer_create()\n");
        extern int mk_timer_create(); // XXX: is there any header for that?
        ret = mk_timer_create();
        break;
    case -92:
        DPRINTF("mk_timer_destroy()\n");
        extern int mk_timer_destroy(int); // XXX: is there any header for that?
        ret = mk_timer_destroy(arg1);
        break;
    case -93:
        DPRINTF("mk_timer_create()\n");
        extern int mk_timer_arm(int, uint64_t); // XXX: is there any header for that?
        ret = mk_timer_arm(arg1, ((uint64_t)arg3<<32) | (uint64_t)arg2);
        break;
    case -94:
        DPRINTF("mk_timer_cancel()\n");
        extern int mk_timer_cancel(int, uint64_t *); // XXX: is there any header for that?
        ret = mk_timer_cancel(arg1, (uint64_t *)arg2);
        if((!is_error(ret)) && arg2)
            tswap64s((uint64_t *)arg2);
        break;
    default:
        gemu_log("qemu: Unsupported mach syscall: %d(0x%x)\n", num, num);
        gdb_handlesig (cpu_env, SIGTRAP);
        exit(0);
        break;
    }
    return ret;
}

/* ------------------------------------------------------------
   thread type syscall handling
*/
long do_thread_syscall(void *cpu_env, int num, uint32_t arg1, uint32_t arg2, uint32_t arg3,
                uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7,
                uint32_t arg8)
{
    extern uint32_t cthread_set_self(uint32_t);
    extern uint32_t processor_facilities_used(void);
    long ret = 0;

    arg1 = tswap32(arg1);
    arg2 = tswap32(arg2);
    arg3 = tswap32(arg3);
    arg4 = tswap32(arg4);
    arg5 = tswap32(arg5);
    arg6 = tswap32(arg6);
    arg7 = tswap32(arg7);
    arg8 = tswap32(arg8);

    DPRINTF("thread syscall %d : " , num);

    switch(num) {
#ifdef TARGET_I386
    case 0x3:
#endif
    case 0x7FF1: /* cthread_set_self */
        DPRINTF("cthread_set_self(0x%x)\n", (unsigned int)arg1);
        ret = cthread_set_self(arg1);
#ifdef TARGET_I386
        /* we need to update the LDT with the address of the thread */
        write_dt((void *)(((CPUX86State *) cpu_env)->ldt.base + (4 * sizeof(uint64_t))), arg1, 1,
                 DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                 (3 << DESC_DPL_SHIFT) | (0x2 << DESC_TYPE_SHIFT));
        /* New i386 convention, %gs should be set to our this LDT entry */
        cpu_x86_load_seg(cpu_env, R_GS, 0x27);
        /* Old i386 convention, the kernel returns the selector for the cthread (pre-10.4.8?)*/
        ret = 0x27;
#endif
        break;
    case 0x7FF2: /* Called the super-fast pthread_self handler by the apple guys */
        DPRINTF("pthread_self()\n");
        ret = (uint32_t)pthread_self();
        break;
    case 0x7FF3:
        DPRINTF("processor_facilities_used()\n");
#ifdef __i386__
        qerror("processor_facilities_used: not implemented!\n");
#else
        ret = (uint32_t)processor_facilities_used();
#endif
        break;
    default:
        gemu_log("qemu: Unsupported thread syscall: %d(0x%x)\n", num, num);
        gdb_handlesig (cpu_env, SIGTRAP);
        exit(0);
        break;
    }
    return ret;
}

/* ------------------------------------------------------------
   ioctl handling
*/
static inline void byteswap_termios(struct termios *t)
{
    tswap32s((uint32_t*)&t->c_iflag);
    tswap32s((uint32_t*)&t->c_oflag);
    tswap32s((uint32_t*)&t->c_cflag);
    tswap32s((uint32_t*)&t->c_lflag);
    /* 20 (char) bytes then */
    tswap32s((uint32_t*)&t->c_ispeed);
    tswap32s((uint32_t*)&t->c_ospeed);
}

static inline void byteswap_winsize(struct winsize *w)
{
    tswap16s(&w->ws_row);
    tswap16s(&w->ws_col);
    tswap16s(&w->ws_xpixel);
    tswap16s(&w->ws_ypixel);
}

#define STRUCT(name, ...) STRUCT_ ## name,
#define STRUCT_SPECIAL(name) STRUCT_ ## name,
enum {
#include "ioctls_types.h"
};
#undef STRUCT
#undef STRUCT_SPECIAL

#define STRUCT(name, ...) const argtype struct_ ## name ## _def[] = {  __VA_ARGS__, TYPE_NULL };
#define STRUCT_SPECIAL(name)
#include "ioctls_types.h"
#undef STRUCT
#undef STRUCT_SPECIAL

typedef struct IOCTLEntry {
    unsigned int target_cmd;
    unsigned int host_cmd;
    const char *name;
    int access;
    const argtype arg_type[5];
} IOCTLEntry;

#define IOC_R 0x0001
#define IOC_W 0x0002
#define IOC_RW (IOC_R | IOC_W)

#define MAX_STRUCT_SIZE 4096

static IOCTLEntry ioctl_entries[] = {
#define IOCTL(cmd, access,  ...)                        \
    { cmd, cmd, #cmd, access, {  __VA_ARGS__ } },
#include "ioctls.h"
    { 0, 0, },
};

/* ??? Implement proper locking for ioctls.  */
static long do_ioctl(long fd, long cmd, long arg)
{
    const IOCTLEntry *ie;
    const argtype *arg_type;
    int ret;
    uint8_t buf_temp[MAX_STRUCT_SIZE];
    int target_size;
    void *argptr;

    ie = ioctl_entries;
    for(;;) {
        if (ie->target_cmd == 0) {
            gemu_log("Unsupported ioctl: cmd=0x%04lx\n", cmd);
            return -ENOSYS;
        }
        if (ie->target_cmd == cmd)
            break;
        ie++;
    }
    arg_type = ie->arg_type;
#if defined(DEBUG)
    gemu_log("ioctl: cmd=0x%04lx (%s)\n", cmd, ie->name);
#endif
    switch(arg_type[0]) {
    case TYPE_NULL:
        /* no argument */
        ret = get_errno(ioctl(fd, ie->host_cmd));
        break;
    case TYPE_PTRVOID:
    case TYPE_INT:
        /* int argment */
        ret = get_errno(ioctl(fd, ie->host_cmd, arg));
        break;
    case TYPE_PTR:
        arg_type++;
        target_size = thunk_type_size(arg_type, 0);
        switch(ie->access) {
        case IOC_R:
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            if (!is_error(ret)) {
                argptr = lock_user(arg, target_size, 0);
                thunk_convert(argptr, buf_temp, arg_type, THUNK_TARGET);
                unlock_user(argptr, arg, target_size);
            }
            break;
        case IOC_W:
            argptr = lock_user(arg, target_size, 1);
            thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
            unlock_user(argptr, arg, 0);
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            break;
        default:
        case IOC_RW:
            argptr = lock_user(arg, target_size, 1);
            thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
            unlock_user(argptr, arg, 0);
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            if (!is_error(ret)) {
                argptr = lock_user(arg, target_size, 0);
                thunk_convert(argptr, buf_temp, arg_type, THUNK_TARGET);
                unlock_user(argptr, arg, target_size);
            }
            break;
        }
        break;
    default:
        gemu_log("Unsupported ioctl type: cmd=0x%04lx type=%d\n", cmd, arg_type[0]);
        ret = -ENOSYS;
        break;
    }
    return ret;
}

/* ------------------------------------------------------------
   Unix syscall handling
*/

static inline void byteswap_attrlist(struct attrlist *a)
{
    tswap16s(&a->bitmapcount);
    tswap16s(&a->reserved);
    tswap32s(&a->commonattr);
    tswap32s(&a->volattr);
    tswap32s(&a->dirattr);
    tswap32s(&a->fileattr);
    tswap32s(&a->forkattr);
}

struct attrbuf_header {
    unsigned long length;
};

static inline void byteswap_attrbuf(struct attrbuf_header *attrbuf, struct attrlist *attrlist)
{
    DPRINTF("attrBuf.lenght %lx\n", attrbuf->length);
}

static inline void byteswap_statfs(struct statfs *s)
{
    tswap16s((uint16_t*)&s->f_otype);
    tswap16s((uint16_t*)&s->f_oflags);
    tswap32s((uint32_t*)&s->f_bsize);
    tswap32s((uint32_t*)&s->f_iosize);
    tswap32s((uint32_t*)&s->f_blocks);
    tswap32s((uint32_t*)&s->f_bfree);
    tswap32s((uint32_t*)&s->f_bavail);
    tswap32s((uint32_t*)&s->f_files);
    tswap32s((uint32_t*)&s->f_ffree);
    tswap32s((uint32_t*)&s->f_fsid.val[0]);
    tswap32s((uint32_t*)&s->f_fsid.val[1]);
    tswap16s((uint16_t*)&s->f_reserved1);
    tswap16s((uint16_t*)&s->f_type);
    tswap32s((uint32_t*)&s->f_flags);
}

static inline void byteswap_stat(struct stat *s)
{
    tswap32s((uint32_t*)&s->st_dev);
    tswap32s(&s->st_ino);
    tswap16s(&s->st_mode);
    tswap16s(&s->st_nlink);
    tswap32s(&s->st_uid);
    tswap32s(&s->st_gid);
    tswap32s((uint32_t*)&s->st_rdev);
    tswap32s((uint32_t*)&s->st_atimespec.tv_sec);
    tswap32s((uint32_t*)&s->st_atimespec.tv_nsec);
    tswap32s((uint32_t*)&s->st_mtimespec.tv_sec);
    tswap32s((uint32_t*)&s->st_mtimespec.tv_nsec);
    tswap32s((uint32_t*)&s->st_ctimespec.tv_sec);
    tswap32s((uint32_t*)&s->st_ctimespec.tv_nsec);
    tswap64s((uint64_t*)&s->st_size);
    tswap64s((uint64_t*)&s->st_blocks);
    tswap32s((uint32_t*)&s->st_blksize);
    tswap32s(&s->st_flags);
    tswap32s(&s->st_gen);
}

static inline void byteswap_dirents(struct dirent *d, int bytes)
{
    char *b;
    for( b = (char*)d; (int)b < (int)d+bytes; )
    {
        unsigned short s = ((struct dirent *)b)->d_reclen;
        tswap32s(&((struct dirent *)b)->d_ino);
        tswap16s(&((struct dirent *)b)->d_reclen);
        if(s<=0)
            break;
        b += s;
    }
}

static inline void byteswap_iovec(struct iovec *v, int n)
{
    int i;
    for(i = 0; i < n; i++)
    {
        tswap32s((uint32_t*)&v[i].iov_base);
        tswap32s((uint32_t*)&v[i].iov_len);
    }
}

static inline void byteswap_timeval(struct timeval *t)
{
    tswap32s((uint32_t*)&t->tv_sec);
    tswap32s((uint32_t*)&t->tv_usec);
}

long do_unix_syscall_indirect(void *cpu_env, int num);
long do_sync(void);
long do_exit(uint32_t arg1);
long do_getlogin(char *out, uint32_t size);
long do_open(char * arg1, uint32_t arg2, uint32_t arg3);
long do_getfsstat(struct statfs * arg1, uint32_t arg2, uint32_t arg3);
long do_sigprocmask(uint32_t arg1, uint32_t * arg2, uint32_t * arg3);
long do_execve(char* arg1, char ** arg2, char ** arg3);
long do_getgroups(uint32_t arg1, gid_t * arg2);
long do_gettimeofday(struct timeval * arg1, void * arg2);
long do_readv(uint32_t arg1, struct iovec * arg2, uint32_t arg3);
long do_writev(uint32_t arg1, struct iovec * arg2, uint32_t arg3);
long do_utimes(char * arg1, struct timeval * arg2);
long do_futimes(uint32_t arg1, struct timeval * arg2);
long do_statfs(char * arg1, struct statfs * arg2);
long do_fstatfs(uint32_t arg1, struct statfs * arg2);
long do_stat(char * arg1, struct stat * arg2);
long do_fstat(uint32_t arg1, struct stat * arg2);
long do_lstat(char * arg1, struct stat * arg2);
long do_getdirentries(uint32_t arg1, void* arg2, uint32_t arg3, void* arg4);
long do_lseek(void *cpu_env, int num);
long do___sysctl(int * name, uint32_t namelen, void * oldp, size_t * oldlenp, void * newp, size_t newlen  /* ignored */);
long do_getattrlist(void * arg1, void * arg2, void * arg3, uint32_t arg4, uint32_t arg5);
long do_getdirentriesattr(uint32_t arg1, void * arg2, void * arg3, size_t arg4, void * arg5, void * arg6, void* arg7, uint32_t arg8);
long do_fcntl(int fd, int cmd, int arg);

long no_syscall(void *cpu_env, int num);

long do_pread(uint32_t arg1, void * arg2, size_t arg3, off_t arg4)
{
    DPRINTF("0x%x, %p, 0x%lx, 0x%" PRIx64 "\n", arg1, arg2, arg3, arg4);
    long ret = pread(arg1, arg2, arg3, arg4);
    return ret;
}

long do_read(int d, void *buf, size_t nbytes)
{
    DPRINTF("0x%x, %p, 0x%lx\n", d, buf, nbytes);
    long ret = get_errno(read(d, buf, nbytes));
    if(!is_error(ret))
        DPRINTF("%x\n", *(uint32_t*)buf);
    return ret;
}

long unimpl_unix_syscall(void *cpu_env, int num);

typedef long (*syscall_function_t)(void *cpu_env, int num);


/* define a table that will handle the syscall number->function association */
#define VOID    void
#define INT     (uint32_t)get_int_arg(&i, cpu_env)
#define INT64   (uint64_t)get_int64_arg(&i, cpu_env)
#define UINT    (unsigned int)INT
#define PTR     (void*)INT

#define SIZE    INT
#define OFFSET  INT64

#define WRAPPER_CALL_DIRECT_0(function, args) long __qemu_##function(void *cpu_env) {  return (long)function(); }
#define WRAPPER_CALL_DIRECT_1(function, _arg1) long __qemu_##function(void *cpu_env) { int i = 0; typeof(_arg1) arg1 = _arg1;  return (long)function(arg1); }
#define WRAPPER_CALL_DIRECT_2(function, _arg1, _arg2) long __qemu_##function(void *cpu_env) { int i = 0;  typeof(_arg1) arg1 = _arg1; typeof(_arg2) arg2 = _arg2; return (long)function(arg1, arg2); }
#define WRAPPER_CALL_DIRECT_3(function, _arg1, _arg2, _arg3) long __qemu_##function(void *cpu_env) { int i = 0;   typeof(_arg1) arg1 = _arg1; typeof(_arg2) arg2 = _arg2; typeof(_arg3) arg3 = _arg3; return (long)function(arg1, arg2, arg3); }
#define WRAPPER_CALL_DIRECT_4(function, _arg1, _arg2, _arg3, _arg4) long __qemu_##function(void *cpu_env) { int i = 0;   typeof(_arg1) arg1 = _arg1; typeof(_arg2) arg2 = _arg2; typeof(_arg3) arg3 = _arg3; typeof(_arg4) arg4 = _arg4; return (long)function(arg1, arg2, arg3, arg4); }
#define WRAPPER_CALL_DIRECT_5(function, _arg1, _arg2, _arg3, _arg4, _arg5) long __qemu_##function(void *cpu_env) { int i = 0;   typeof(_arg1) arg1 = _arg1; typeof(_arg2) arg2 = _arg2; typeof(_arg3) arg3 = _arg3; typeof(_arg4) arg4 = _arg4; typeof(_arg5) arg5 = _arg5;  return (long)function(arg1, arg2, arg3, arg4, arg5); }
#define WRAPPER_CALL_DIRECT_6(function, _arg1, _arg2, _arg3, _arg4, _arg5, _arg6) long __qemu_##function(void *cpu_env) { int i = 0;   typeof(_arg1) arg1 = _arg1; typeof(_arg2) arg2 = _arg2; typeof(_arg3) arg3 = _arg3; typeof(_arg4) arg4 = _arg4; typeof(_arg5) arg5 = _arg5; typeof(_arg6) arg6 = _arg6;  return (long)function(arg1, arg2, arg3, arg4, arg5, arg6); }
#define WRAPPER_CALL_DIRECT_7(function, _arg1, _arg2, _arg3, _arg4, _arg5, _arg6, _arg7) long __qemu_##function(void *cpu_env) { int i = 0;   typeof(_arg1) arg1 = _arg1; typeof(_arg2) arg2 = _arg2; typeof(_arg3) arg3 = _arg3; typeof(_arg4) arg4 = _arg4; typeof(_arg5) arg5 = _arg5; typeof(_arg6) arg6 = _arg6; typeof(_arg7) arg7 = _arg7; return (long)function(arg1, arg2, arg3, arg4, arg5, arg6, arg7); }
#define WRAPPER_CALL_DIRECT_8(function, _arg1, _arg2, _arg3, _arg4, _arg5, _arg6, _arg7, _arg8) long __qemu_##function(void *cpu_env) { int i = 0;   typeof(_arg1) arg1 = _arg1; typeof(_arg2) arg2 = _arg2; typeof(_arg3) arg3 = _arg3; typeof(_arg4) arg4 = _arg4; typeof(_arg5) arg5 = _arg5; typeof(_arg6) arg6 = _arg6; typeof(_arg7) arg7 = _arg7; typeof(_arg8) arg8 = _arg8;  return (long)function(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8); }
#define WRAPPER_CALL_DIRECT(function, nargs, ...) WRAPPER_CALL_DIRECT_##nargs(function, __VA_ARGS__)
#define WRAPPER_CALL_NOERRNO(function, nargs, ...)  WRAPPER_CALL_DIRECT(function, nargs, __VA_ARGS__)
#define WRAPPER_CALL_INDIRECT(function, nargs, ...)
#define ENTRY(name, number, function, nargs, call_type, ...)  WRAPPER_##call_type(function, nargs, __VA_ARGS__)

#include "syscalls.h"

#undef ENTRY
#undef WRAPPER_CALL_DIRECT
#undef WRAPPER_CALL_NOERRNO
#undef WRAPPER_CALL_INDIRECT
#undef OFFSET
#undef SIZE
#undef INT
#undef PTR
#undef INT64

#define _ENTRY(name, number, function, nargs, call_type) [number] = {\
        name, \
        number, \
        (syscall_function_t)function, \
        nargs, \
        call_type  \
        },

#define ENTRY_CALL_DIRECT(name, number, function, nargs, call_type)  _ENTRY(name, number, __qemu_##function, nargs, call_type)
#define ENTRY_CALL_NOERRNO(name, number, function, nargs, call_type) ENTRY_CALL_DIRECT(name, number, function, nargs, call_type)
#define ENTRY_CALL_INDIRECT(name, number, function, nargs, call_type) _ENTRY(name, number, function, nargs, call_type)
#define ENTRY(name, number, function, nargs, call_type, ...) ENTRY_##call_type(name, number, function, nargs, call_type)

#define CALL_DIRECT 1
#define CALL_INDIRECT 2
#define CALL_NOERRNO  (CALL_DIRECT | 4 /* = 5 */)

struct unix_syscall {
    char * name;
    int number;
    syscall_function_t function;
    int nargs;
    int call_type;
} unix_syscall_table[SYS_MAXSYSCALL] = {
#include "syscalls.h"
};

#undef ENTRY
#undef _ENTRY
#undef ENTRY_CALL_DIRECT
#undef ENTRY_CALL_INDIRECT
#undef ENTRY_CALL_NOERRNO

/* Actual syscalls implementation */

long do_unix_syscall_indirect(void *cpu_env, int num)
{
    long ret;
    int new_num;
    int i = 0;

    new_num = get_int_arg(&i, cpu_env);
#ifdef TARGET_I386
    ((CPUX86State*)cpu_env)->regs[R_ESP] += 4;
    /* XXX: not necessary */
    ((CPUX86State*)cpu_env)->regs[R_EAX] = new_num;
#elif TARGET_PPC
    {
        int i;
        uint32_t **regs = ((CPUPPCState*)cpu_env)->gpr;
        for(i = 3; i < 11; i++)
            *regs[i] = *regs[i+1];
        /* XXX: not necessary */
        *regs[0] = new_num;
    }
#endif
    ret = do_unix_syscall(cpu_env, new_num);
#ifdef TARGET_I386
    ((CPUX86State*)cpu_env)->regs[R_ESP] -= 4;
    /* XXX: not necessary */
    ((CPUX86State*)cpu_env)->regs[R_EAX] = num;
#elif TARGET_PPC
    {
        int i;
        /* XXX: not really needed those regs are volatile across calls */
        uint32_t **regs = ((CPUPPCState*)cpu_env)->gpr;
        for(i = 11; i > 3; i--)
            *regs[i] = *regs[i-1];
        regs[3] = new_num;
        *regs[0] = num;
    }
#endif
    return ret;
}

long do_exit(uint32_t arg1)
{
    exit(arg1);
    /* not reached */
    return -1;
}

long do_sync(void)
{
    sync();
    return 0;
}

long do_getlogin(char *out, uint32_t size)
{
    char *login = getlogin();
    if(!login)
        return -1;
    memcpy(out, login, size);
    return 0;
}
long do_open(char * arg1, uint32_t arg2, uint32_t arg3)
{
    /* XXX: don't let the %s stay in there */
    DPRINTF("open(%s, 0x%x, 0x%x)\n", arg1, arg2, arg3);
    return get_errno(open(arg1, arg2, arg3));
}

long do_getfsstat(struct statfs * arg1, uint32_t arg2, uint32_t arg3)
{
    long ret;
    DPRINTF("getfsstat(%p, 0x%x, 0x%x)\n", arg1, arg2, arg3);
    ret = get_errno(getfsstat(arg1, arg2, arg3));
    if((!is_error(ret)) && arg1)
        byteswap_statfs(arg1);
    return ret;
}

long do_sigprocmask(uint32_t arg1, uint32_t * arg2, uint32_t * arg3)
{
    long ret;
    DPRINTF("sigprocmask(%d, %p, %p)\n", arg1, arg2, arg3);
    gemu_log("XXX: sigprocmask not tested (%d, %p, %p)\n", arg1, arg2, arg3);
    if(arg2)
        tswap32s(arg2);
    ret = get_errno(sigprocmask(arg1, (void *)arg2, (void *)arg3));
    if((!is_error(ret)) && arg3)
        tswap32s(arg3);
    if(arg2)
        tswap32s(arg2);
    return ret;
}

long do_execve(char* arg1, char ** arg2, char ** arg3)
{
    long ret;
    char **argv = arg2;
    char **envp = arg3;
    int argc;
    int envc;

    /* XXX: don't let the %s stay in here */
    DPRINTF("execve(%s, %p, %p)\n", arg1, arg2, arg3);

    for(argc = 0; argv[argc]; argc++);
    for(envc = 0; envp[envc]; envc++);

    argv = (char**)malloc(sizeof(char*)*argc);
    envp = (char**)malloc(sizeof(char*)*envc);

    for(; argc >= 0; argc--)
        argv[argc] = (char*)tswap32((uint32_t)(arg2)[argc]);

    for(; envc >= 0; envc--)
        envp[envc] = (char*)tswap32((uint32_t)(arg3)[envc]);

    ret = get_errno(execve(arg1, argv, envp));
    free(argv);
    free(envp);
    return ret;
}

long do_getgroups(uint32_t arg1, gid_t * arg2)
{
    long ret;
    int i;
    DPRINTF("getgroups(0x%x, %p)\n", arg1, arg2);
    ret = get_errno(getgroups(arg1, arg2));
    if(ret > 0)
        for(i = 0; i < arg1; i++)
            tswap32s(&arg2[i]);
    return ret;
}

long do_gettimeofday(struct timeval * arg1, void * arg2)
{
    long ret;
    DPRINTF("gettimeofday(%p, %p)\n",
            arg1, arg2);
    ret = get_errno(gettimeofday(arg1, arg2));
    if(!is_error(ret))
    {
        /* timezone no longer used according to the manpage, so don't bother with it */
        byteswap_timeval(arg1);
    }
    return ret;
}

long do_readv(uint32_t arg1, struct iovec * arg2, uint32_t arg3)
{
    long ret;
    DPRINTF("readv(0x%x, %p, 0x%x)\n", arg1, arg2, arg3);
    if(arg2)
        byteswap_iovec(arg2, arg3);
    ret = get_errno(readv(arg1, arg2, arg3));
    if((!is_error(ret)) && arg2)
        byteswap_iovec(arg2, arg3);
    return ret;
}

long do_writev(uint32_t arg1, struct iovec * arg2, uint32_t arg3)
{
    long ret;
    DPRINTF("writev(0x%x, %p, 0x%x)\n", arg1, arg2, arg3);
    if(arg2)
        byteswap_iovec(arg2, arg3);
    ret = get_errno(writev(arg1, arg2, arg3));
    if((!is_error(ret)) && arg2)
        byteswap_iovec(arg2, arg3);
    return ret;
}

long do_utimes(char * arg1, struct timeval * arg2)
{
    DPRINTF("utimes(%p, %p)\n", arg1, arg2);
    if(arg2)
    {
        byteswap_timeval(arg2);
        byteswap_timeval(arg2+1);
    }
    return get_errno(utimes(arg1, arg2));
}

long do_futimes(uint32_t arg1, struct timeval * arg2)
{
    DPRINTF("futimes(0x%x, %p)\n", arg1, arg2);
    if(arg2)
    {
        byteswap_timeval(arg2);
        byteswap_timeval(arg2+1);
    }
    return get_errno(futimes(arg1, arg2));
}

long do_statfs(char * arg1, struct statfs * arg2)
{
    long ret;
    DPRINTF("statfs(%p, %p)\n", arg1, arg2);
    ret = get_errno(statfs(arg1, arg2));
    if(!is_error(ret))
        byteswap_statfs(arg2);
    return ret;
}

long do_fstatfs(uint32_t arg1, struct statfs* arg2)
{
    long ret;
    DPRINTF("fstatfs(0x%x, %p)\n",
            arg1, arg2);
    ret = get_errno(fstatfs(arg1, arg2));
    if(!is_error(ret))
        byteswap_statfs(arg2);

    return ret;
}

long do_stat(char * arg1, struct stat * arg2)
{
    long ret;
    /* XXX: don't let the %s stay in there */
    DPRINTF("stat(%s, %p)\n", arg1, arg2);
    ret = get_errno(stat(arg1, arg2));
    if(!is_error(ret))
        byteswap_stat(arg2);
    return ret;
}

long do_fstat(uint32_t arg1, struct stat * arg2)
{
    long ret;
    DPRINTF("fstat(0x%x, %p)\n", arg1, arg2);
    ret = get_errno(fstat(arg1, arg2));
    if(!is_error(ret))
        byteswap_stat(arg2);
    return ret;
}

long do_lstat(char * arg1, struct stat * arg2)
{
    long ret;
    /* XXX: don't let the %s stay in there */
    DPRINTF("lstat(%s, %p)\n", (const char *)arg1, arg2);
    ret = get_errno(lstat(arg1, arg2));
    if(!is_error(ret))
        byteswap_stat(arg2);
    return ret;
}

long do_getdirentries(uint32_t arg1, void* arg2, uint32_t arg3, void* arg4)
{
    long ret;
    DPRINTF("getdirentries(0x%x, %p, 0x%x, %p)\n", arg1, arg2, arg3, arg4);
    if(arg4)
        tswap32s((uint32_t *)arg4);
    ret = get_errno(getdirentries(arg1, arg2, arg3, arg4));
    if(arg4)
        tswap32s((uint32_t *)arg4);
    if(!is_error(ret))
        byteswap_dirents(arg2, ret);
    return ret;
}

long do_lseek(void *cpu_env, int num)
{
    long ret;
    int i = 0;
    uint32_t arg1 = get_int_arg(&i, cpu_env);
    uint64_t offset = get_int64_arg(&i, cpu_env);
    uint32_t arg3 = get_int_arg(&i, cpu_env);
    uint64_t r = lseek(arg1, offset, arg3);
#ifdef TARGET_I386
    /* lowest word in eax, highest in edx */
    ret = r & 0xffffffff; /* will be set to eax after do_unix_syscall exit */
    ((CPUX86State *)cpu_env)->regs[R_EDX] = (uint32_t)((r >> 32) & 0xffffffff) ;
#elif defined TARGET_PPC
    ret = r & 0xffffffff; /* will be set to r3 after do_unix_syscall exit */
    ((CPUPPCState *)cpu_env)->gpr[4] = (uint32_t)((r >> 32) & 0xffffffff) ;
#else
    qerror("64 bit ret value on your arch?");
#endif
    return get_errno(ret);
}

void no_swap(void * oldp, int size)
{
}

void sysctl_tswap32s(void * oldp, int size)
{
    tswap32s(oldp);
}

void bswap_oid(uint32_t * oldp, int size)
{
    int count = size / sizeof(int);
    int i = 0;
    do { tswap32s(oldp + i); } while (++i < count);
}

void sysctl_usrstack(uint32_t * oldp, int size)
{
    DPRINTF("sysctl_usrstack: 0x%x\n", *oldp);
    tswap32s(oldp);
}

void sysctl_ncpu(uint32_t * ncpu, int size)
{
    *ncpu = 0x1;
    DPRINTF("sysctl_ncpu: 0x%x\n", *ncpu);
    tswap32s(ncpu);
}

void sysctl_exec(char * exec, int size)
{
    DPRINTF("sysctl_exec: %s\n", exec);
}

void sysctl_translate(char * exec, int size)
{
    DPRINTF("sysctl_translate: %s\n", exec);
}

struct sysctl_dir {
    int num;
    const char * name;
    void (*swap_func)(void *, int);
    struct sysctl_dir *childs;
};

#define ENTRYD(num, name, childs) { num, name, NULL, childs }
#define ENTRYE(num, name, func)   { num, name, (void (*)(void *, int))func, NULL  }
struct sysctl_dir sysctls_unspec[] = {
    ENTRYE(3,  "oip", bswap_oid),
    { 0, NULL, NULL, NULL }
};

struct sysctl_dir sysctls_kern[] = {
    ENTRYE(KERN_TRANSLATE,          "translate",    sysctl_translate), /* 44 */
    ENTRYE(KERN_EXEC,               "exec",         sysctl_exec), /* 45 */
    ENTRYE(KERN_USRSTACK32,          "KERN_USRSTACK32", sysctl_usrstack), /* 35 */
    ENTRYE(KERN_SHREG_PRIVATIZABLE,  "KERN_SHREG_PRIVATIZABLE", sysctl_tswap32s), /* 54 */
    { 0, NULL, NULL, NULL }
};

struct sysctl_dir sysctls_hw[] = {
    ENTRYE(HW_NCPU, "ncpud", sysctl_tswap32s),
    ENTRYE(104, "104", no_swap),
    ENTRYE(105, "105", no_swap),
    { 0, NULL, NULL, NULL }
};

struct sysctl_dir sysctls[] = {
    ENTRYD(CTL_UNSPEC, "unspec", sysctls_unspec),
    ENTRYD(CTL_KERN, "kern", sysctls_kern),
    ENTRYD(CTL_HW,   "hw",   sysctls_hw ),
    { 0, NULL, NULL, NULL }
};

#undef ENTRYE
#undef ENTRYD

static inline struct sysctl_dir * get_sysctl_entry_for_mib(int mib, struct sysctl_dir * sysctl_elmt)
{
    if(!sysctl_elmt)
        return NULL;
    for(; sysctl_elmt->name != NULL ; sysctl_elmt++) {
        if(sysctl_elmt->num == mib)
            return sysctl_elmt;
    }
    return NULL;
}

static inline long bswap_syctl(int * mib, int count, void *buf, int size)
{
    int i;
    struct sysctl_dir * sysctl = sysctls;
    struct sysctl_dir * ret = NULL;

    for(i = 0; i < count; i++) {

        if(!(ret = sysctl = get_sysctl_entry_for_mib(mib[i], sysctl))) {
            gemu_log("bswap_syctl: can't find mib %d\n", mib[i]);
            return -ENOTDIR;
        }
        if(!(sysctl = sysctl->childs))
            break;
    }

    if(ret->childs)
        qerror("we shouldn't have a directory element\n");

    ret->swap_func(buf, size);
    return 0;
}

static inline void print_syctl(int * mib, int count)
{
    int i;
    struct sysctl_dir * sysctl = sysctls;
    struct sysctl_dir * ret = NULL;

    for(i = 0; i < count; i++) {
        if(!(ret = sysctl = get_sysctl_entry_for_mib(mib[i], sysctl))){
            gemu_log("print_syctl: can't find mib %d\n", mib[i]);
            return;
        }
        DPRINTF("%s.", sysctl->name);
        if(!(sysctl = sysctl->childs))
            break;
    }
    DPRINTF("\n");
}

long do___sysctl(int * name, uint32_t namelen, void * oldp, size_t * oldlenp, void * newp, size_t newlen  /* ignored */)
{
    long ret = 0;
    int i;
    DPRINTF("sysctl(%p, 0x%x, %p, %p, %p, 0x%lx)\n",
            name, namelen, oldp, oldlenp, newp, newlen);
    if(name) {
        i = 0;
        do { tswap32s( name + i); } while (++i < namelen);
        print_syctl(name, namelen);
        //bswap_syctl(name, namelen, newp, newlen);
        tswap32s((uint32_t*)oldlenp);
    }

    if(name) /* Sometimes sysctl is called with no arg1, ignore */
        ret = get_errno(sysctl(name, namelen, oldp, oldlenp, newp, newlen));

#if defined(TARGET_I386) ^ defined(__i386__) || defined(TARGET_PPC) ^ defined(__ppc__)
    if (!is_error(ret) && bswap_syctl(name, namelen, oldp, *oldlenp) != 0) {
        return -ENOTDIR;
    }
#endif

    if(name) {
        //bswap_syctl(name, namelen, newp, newlen);
        tswap32s((uint32_t*)oldlenp);

        i = 0;
        do { tswap32s( name + i); } while (++i < namelen);
    }
    return ret;
}

long do_getattrlist(void * arg1, void * arg2, void * arg3, uint32_t arg4, uint32_t arg5)
{
    struct attrlist * attrlist = (void *)arg2;
    long ret;

#if defined(TARGET_I386) ^ defined(__i386__) || defined(TARGET_PPC) ^ defined(__ppc__)
    gemu_log("SYS_getdirentriesattr unimplemented\n");
    return -ENOTSUP;
#endif
    /* XXX: don't let the %s stay in there */
    DPRINTF("getattrlist(%s, %p, %p, 0x%x, 0x%x)\n",
            (char *)arg1, arg2, arg3, arg4, arg5);

    if(arg2) /* XXX: We should handle that in a copy especially
        if the structure is not writable */
        byteswap_attrlist(attrlist);

    ret = get_errno(getattrlist((const char* )arg1, attrlist, (void *)arg3, arg4, arg5));

    if(!is_error(ret))
    {
        byteswap_attrbuf((void *)arg3, attrlist);
        byteswap_attrlist(attrlist);
    }
    return ret;
}

long do_getdirentriesattr(uint32_t arg1, void * arg2, void * arg3, size_t arg4, void * arg5, void * arg6, void* arg7, uint32_t arg8)
{
    DPRINTF("getdirentriesattr(0x%x, %p, %p, 0x%lx, %p, %p, %p, 0x%x)\n",
            arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
#if defined(TARGET_I386) ^ defined(__i386__) || defined(TARGET_PPC) ^ defined(__ppc__)
    qerror("SYS_getdirentriesattr unimplemented\n");
#endif

    return get_errno(getdirentriesattr( arg1, (struct attrlist * )arg2, (void *)arg3, arg4,
                                       (unsigned long *)arg5, (unsigned long *)arg6,
                                       (unsigned long *)arg7, arg8));
}

static inline void bswap_flock(struct flock *f)
{
    tswap64s(&f->l_start);
    tswap64s(&f->l_len);
    tswap32s(&f->l_pid);
    tswap16s(&f->l_type);
    tswap16s(&f->l_whence);
}

static inline void bswap_fstore(struct fstore *f)
{
    tswap32s(&f->fst_flags);
    tswap32s(&f->fst_posmode);
    tswap64s(&f->fst_offset);
    tswap64s(&f->fst_length);
    tswap64s(&f->fst_bytesalloc);
}

static inline void bswap_radvisory(struct radvisory *f)
{
    tswap64s(&f->ra_offset);
    tswap32s(&f->ra_count);
}

static inline void bswap_fbootstraptransfer(struct fbootstraptransfer *f)
{
    tswap64s(&f->fbt_offset);
    tswap32s((uint32_t*)&f->fbt_length);
    tswap32s((uint32_t*)&f->fbt_buffer); /* XXX: this is a ptr */
}

static inline void bswap_log2phys(struct log2phys *f)
{
    tswap32s(&f->l2p_flags);
    tswap64s(&f->l2p_contigbytes);
    tswap64s(&f->l2p_devoffset);
}

static inline void bswap_fcntl_arg(int cmd, void * arg)
{
    switch(cmd)
    {
        case F_DUPFD:
        case F_GETFD:
        case F_SETFD:
        case F_GETFL:
        case F_SETFL:
        case F_GETOWN:
        case F_SETOWN:
        case F_SETSIZE:
        case F_RDAHEAD:
        case F_FULLFSYNC:
            break;
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
            bswap_flock(arg);
            break;
        case F_PREALLOCATE:
            bswap_fstore(arg);
            break;
        case F_RDADVISE:
            bswap_radvisory(arg);
            break;
        case F_READBOOTSTRAP:
        case F_WRITEBOOTSTRAP:
            bswap_fbootstraptransfer(arg);
            break;
        case F_LOG2PHYS:
            bswap_log2phys(arg);
            break;
        default:
            gemu_log("unknow cmd in fcntl\n");
    }
}

long do_fcntl(int fd, int cmd, int arg)
{
    long ret;
    bswap_fcntl_arg(cmd, (void *)arg);
    ret = get_errno(fcntl(fd, cmd, arg));
    if(!is_error(ret))
        bswap_fcntl_arg(cmd, (void *)arg);
    return ret;
}

long no_syscall(void *cpu_env, int num)
{
    /* XXX: We should probably fordward it to the host kernel */
    qerror("no unix syscall %d\n", num);
    /* not reached */
    return -1;
}

long unimpl_unix_syscall(void *cpu_env, int num)
{
    if( (num < 0) || (num > SYS_MAXSYSCALL-1) )
        qerror("unix syscall %d is out of unix syscall bounds (0-%d) " , num, SYS_MAXSYSCALL-1);

    gemu_log("qemu: Unsupported unix syscall %s %d\n", unix_syscall_table[num].name , num);
    gdb_handlesig (cpu_env, SIGTRAP);
    exit(-1);
}

long do_unix_syscall(void *cpu_env, int num)
{
    long ret = 0;

    DPRINTF("unix syscall %d: " , num);

    if( (num < 0) || (num > SYS_MAXSYSCALL-1) )
        qerror("unix syscall %d is out of unix syscall bounds (0-%d) " , num, SYS_MAXSYSCALL-1);

    DPRINTF("%s [%s]", unix_syscall_table[num].name, unix_syscall_table[num].call_type & CALL_DIRECT ? "direct" : "indirect" );
    ret = unix_syscall_table[num].function(cpu_env, num);

    if(!(unix_syscall_table[num].call_type & CALL_NOERRNO))
        ret = get_errno(ret);

    DPRINTF("[returned 0x%x(%d)]\n", (int)ret, (int)ret);
    return ret;
}

/* ------------------------------------------------------------
   syscall_init
*/
void syscall_init(void)
{
    /* Nothing yet */
}

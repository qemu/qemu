/*
 * Helper routines to provide target memory access for semihosting
 * syscalls in system emulation mode.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licenced under the GPL
 */

static inline uint32_t softmmu_tget32(CPUState *env, uint32_t addr)
{
    uint32_t val;

    cpu_memory_rw_debug(env, addr, (uint8_t *)&val, 4, 0);
    return tswap32(val);
}
static inline uint32_t softmmu_tget8(CPUState *env, uint32_t addr)
{
    uint8_t val;

    cpu_memory_rw_debug(env, addr, &val, 1, 0);
    return val;
}

#define get_user_u32(arg, p) ({ arg = softmmu_tget32(env, p) ; 0; })
#define get_user_u8(arg, p) ({ arg = softmmu_tget8(env, p) ; 0; })
#define get_user_ual(arg, p) get_user_u32(arg, p)

static inline void softmmu_tput32(CPUState *env, uint32_t addr, uint32_t val)
{
    val = tswap32(val);
    cpu_memory_rw_debug(env, addr, (uint8_t *)&val, 4, 1);
}
#define put_user_u32(arg, p) ({ softmmu_tput32(env, p, arg) ; 0; })
#define put_user_ual(arg, p) put_user_u32(arg, p)

static void *softmmu_lock_user(CPUState *env, uint32_t addr, uint32_t len,
                               int copy)
{
    uint8_t *p;
    /* TODO: Make this something that isn't fixed size.  */
    p = malloc(len);
    if (copy)
        cpu_memory_rw_debug(env, addr, p, len, 0);
    return p;
}
#define lock_user(type, p, len, copy) softmmu_lock_user(env, p, len, copy)
static char *softmmu_lock_user_string(CPUState *env, uint32_t addr)
{
    char *p;
    char *s;
    uint8_t c;
    /* TODO: Make this something that isn't fixed size.  */
    s = p = malloc(1024);
    do {
        cpu_memory_rw_debug(env, addr, &c, 1, 0);
        addr++;
        *(p++) = c;
    } while (c);
    return s;
}
#define lock_user_string(p) softmmu_lock_user_string(env, p)
static void softmmu_unlock_user(CPUState *env, void *p, target_ulong addr,
                                target_ulong len)
{
    if (len)
        cpu_memory_rw_debug(env, addr, p, len, 1);
    free(p);
}
#define unlock_user(s, args, len) softmmu_unlock_user(env, s, args, len)

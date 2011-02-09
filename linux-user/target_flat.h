/* If your arch needs to do custom stuff, create your own target_flat.h
 * header file in linux-user/<your arch>/
 */
#define flat_argvp_envp_on_stack()                           1
#define flat_reloc_valid(reloc, size)                        ((reloc) <= (size))
#define flat_old_ram_flag(flag)                              (flag)
#define flat_get_relocate_addr(relval)                       (relval)
#define flat_get_addr_from_rp(rp, relval, flags, persistent) (rp)
#define flat_set_persistent(relval, persistent)              (*persistent)
#define flat_put_addr_at_rp(rp, addr, relval)                put_user_ual(addr, rp)

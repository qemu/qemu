#define TARGET_ABI32
#define load_elf_binary load_elf_binary32
#define do_init_thread do_init_thread32

#include "elfload.c"

#undef load_elf_binary
#undef do_init_thread

int load_elf_binary(struct linux_binprm *bprm, struct target_pt_regs *regs,
                    struct image_info *info);

int load_elf_binary_multi(struct linux_binprm *bprm,
                          struct target_pt_regs *regs,
                          struct image_info *info)
{
    struct elfhdr *elf_ex;
    int retval;

    elf_ex = (struct elfhdr *) bprm->buf;          /* exec-header */
    if (elf_ex->e_ident[EI_CLASS] == ELFCLASS64) {
        retval = load_elf_binary(bprm, regs, info);
    } else {
        retval = load_elf_binary32(bprm, regs, info);
        if (personality(info->personality) == PER_LINUX)
            info->personality = PER_LINUX32;
    }

    return retval;
}

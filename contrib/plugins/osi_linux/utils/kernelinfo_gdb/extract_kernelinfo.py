import gdb
import sys
import re

file_out = sys.stdout


def print_size(structv, cfgmemb, cfgname):
    size = gdb.execute(f'printf "%u",(size_t) sizeof({structv})',to_string=True)
    print(f"{cfgname}.{cfgmemb} = {size}",file=file_out)

def print_offset(structp, memb, cfgname):
    offset = gdb.execute(f'printf "%d",(int)&(({structp}*)0)->{memb}',to_string=True)
    print(f"{cfgname}.{memb}_offset = {offset}",file=file_out)

def print_offset_from_member(structp, memb_base, memb_dest, cfgname):
    offset = gdb.execute(f'printf "%lld", (int64_t)&(({structp})*0)->{memb_dest} - (int64_t)&(({structp})*0)->{memb_base}',to_string=True)
    print(f"{cfgname}.{memb_dest}_offset = {offset}",file=file_out)

def get_symbol_as_string(name):
    return gdb.execute(f'printf "%s",{name}',to_string=True)

class KernelInfo(gdb.Command):
    def __init__(self):
        super(KernelInfo, self).__init__("kernel_info", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        global file_out
        if arg:
            file_out = open(arg, "w+")  
            print(f"Printing output to {arg}") 
        uts_release = get_symbol_as_string("init_uts_ns->name->release")
        uts_version = get_symbol_as_string("init_uts_ns->name->version")
        uts_machine = get_symbol_as_string("init_uts_ns->name->machine")
        print(f"name = {uts_release}|{uts_version}|{uts_machine}",file=file_out)
        release = get_symbol_as_string("init_uts_ns->name->release")
        versions = release.split(".")
        # version.c can have a bunch of junk - just extract a number
        versions[2] = re.search("\d+",versions[2]).group(0)
        print(f"version.a = {versions[0]}",file=file_out)
        print(f"version.b = {versions[1]}",file=file_out)
        print(f"version.c = {versions[2]}",file=file_out)

        # TODO: we should use this to generate the file. See issue 651
        print(f"#arch  = {uts_machine}", file=file_out)
        try:
            per_cpu_offset_addr = gdb.execute('printf "%llu", &__per_cpu_offset',to_string=True)
            per_cpu_offset_0_addr = gdb.execute('printf "%llu", __per_cpu_offset[0]',to_string=True)
        except:
            # This should be an arch-specific requirement, arm/mips don't have it (#651)
            assert("x86" not in uts_machine), "Failed to find __per_cpu_offset_data"
            per_cpu_offset_addr = 0
            per_cpu_offset_0_addr = 0
        print(f"task.per_cpu_offsets_addr = {per_cpu_offset_addr}",file=file_out)
        print(f"task.per_cpu_offset_0_addr = {per_cpu_offset_0_addr}",file=file_out)
        
        
        init_task_addr = gdb.execute('printf "%llu", &init_task',to_string=True)
        # current_task is only an x86 thing
        # It's defined in /arch/x86/kernel/cpu/common.c
        if "x86" in uts_machine:
            current_task_addr = gdb.execute('printf "%llu", &current_task',to_string=True)
        else:
            current_task_addr = init_task_addr

        print(f"task.current_task_addr = {current_task_addr}",file=file_out)
        print(f"task.init_addr = {init_task_addr}",file=file_out)
        print(f"#task.per_cpu_offsets_addr = {hex(int(per_cpu_offset_addr))[2:]}",file=file_out)
        print(f"#task.per_cpu_offset_0_addr = {hex(int(per_cpu_offset_0_addr))}",file=file_out)
        print(f"#task.current_task_addr = {hex(int(current_task_addr))}",file=file_out)
        print(f"#task.init_addr = {hex(int(init_task_addr))}",file=file_out)

        print_size("struct task_struct",                "size",         "task");
        print_offset("struct task_struct",      "tasks",            "task");
        print_offset("struct task_struct",      "pid",          "task");
        print_offset("struct task_struct",      "tgid",         "task");
        print_offset("struct task_struct",      "group_leader", "task");
        print_offset("struct task_struct",      "thread_group", "task");
        print_offset("struct task_struct",      "real_parent",  "task");
        print_offset("struct task_struct",      "parent",           "task");
        print_offset("struct task_struct",      "mm",               "task");
        print_offset("struct task_struct",      "stack",            "task");
        print_offset("struct task_struct",      "real_cred",        "task");
        print_offset("struct task_struct",      "cred",         "task");
        print_offset("struct task_struct",      "comm",         "task");
        print_size("((struct task_struct*)0)->comm",    "comm_size",    "task");
        print_offset("struct task_struct",      "files",            "task");
        print_offset("struct task_struct",      "start_time",           "task");
        print_offset("struct cred",             "uid",          "cred");
        print_offset("struct cred",             "gid",          "cred");
        print_offset("struct cred",             "euid",         "cred");
        print_offset("struct cred",             "egid",         "cred");
        print_size("struct mm_struct",          "size",         "mm");
        print_offset("struct mm_struct",            "mmap",         "mm");
        print_offset("struct mm_struct",            "pgd",          "mm");
        print_offset("struct mm_struct",            "arg_start",        "mm");
        print_offset("struct mm_struct",            "start_brk",        "mm");
        print_offset("struct mm_struct",            "brk",          "mm");
        print_offset("struct mm_struct",            "start_stack",  "mm");

        print_size("struct vm_area_struct",     "size",         "vma");
        print_offset("struct vm_area_struct",       "vm_mm",            "vma");
        print_offset("struct vm_area_struct",       "vm_start",     "vma");
        print_offset("struct vm_area_struct",       "vm_end",           "vma");
        print_offset("struct vm_area_struct",       "vm_next",      "vma");
        print_offset("struct vm_area_struct",       "vm_flags",     "vma");
        print_offset("struct vm_area_struct",       "vm_file",      "vma");

        # used in reading file information 
        # This is gross because the name doesn't match the symbol identifier.
        fstruct = "struct file"
        f_path_offset = gdb.execute(f'printf "%d",(int)&(({fstruct}*)0)->f_path.dentry',to_string=True)
        print(f"fs.f_path_dentry_offset = {f_path_offset}",file=file_out)
        offset = gdb.execute(f'printf "%d",(int)&(({fstruct}*)0)->f_path.mnt',to_string=True)
        print(f"fs.f_path_mnt_offset = {offset}",file=file_out)

        print_offset("struct file",             "f_pos",            "fs");
        print_offset("struct files_struct",     "fdt",          "fs");
        print_offset("struct files_struct",     "fdtab",            "fs");
        print_offset("struct fdtable",          "fd",               "fs");

        #  used for resolving path names */
        print_size("struct qstr",                   "size",         "qstr");
        print_offset("struct qstr",             "name",         "qstr");
        print_offset("struct dentry",               "d_name",                   "path");
        print_offset("struct dentry",               "d_iname",              "path");
        print_offset("struct dentry",               "d_parent",             "path");
        print_offset("struct dentry",               "d_op",                 "path");
        print_offset("struct dentry_operations",    "d_dname",              "path");
        print_offset("struct vfsmount",         "mnt_root",             "path");
        if int(versions[0]) >= 3:
            # fields in struct mount 
            print_offset_from_member("struct mount",    "mnt", "mnt_parent",        "path");
            print_offset_from_member("struct mount",    "mnt", "mnt_mountpoint",    "path");
        else:
            # fields in struct vfsmount 
            print_offset("struct vfsmount",         "mnt_parent",               "path");
            print_offset("struct vfsmount",         "mnt_mountpoint",           "path");
        if file_out != sys.stdout:
            file_out.close()

# This registers our class to the gdb runtime at "source" time.
KernelInfo()

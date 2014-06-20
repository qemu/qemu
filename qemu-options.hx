HXCOMM Use DEFHEADING() to define headings in both help text and texi
HXCOMM Text between STEXI and ETEXI are copied to texi version and
HXCOMM discarded from C version
HXCOMM DEF(option, HAS_ARG/0, opt_enum, opt_help, arch_mask) is used to
HXCOMM construct option structures, enums and help message for specified
HXCOMM architectures.
HXCOMM HXCOMM can be used for comments, discarded from both texi and C

DEFHEADING(Standard options:)
STEXI
@table @option
ETEXI

DEF("help", 0, QEMU_OPTION_h,
    "-h or -help     display this help and exit\n", QEMU_ARCH_ALL)
STEXI
@item -h
@findex -h
Display help and exit
ETEXI

DEF("version", 0, QEMU_OPTION_version,
    "-version        display version information and exit\n", QEMU_ARCH_ALL)
STEXI
@item -version
@findex -version
Display version information and exit
ETEXI

DEF("machine", HAS_ARG, QEMU_OPTION_machine, \
    "-machine [type=]name[,prop[=value][,...]]\n"
    "                selects emulated machine ('-machine help' for list)\n"
    "                property accel=accel1[:accel2[:...]] selects accelerator\n"
    "                supported accelerators are kvm, xen, tcg (default: tcg)\n"
    "                kernel_irqchip=on|off controls accelerated irqchip support\n"
    "                kvm_shadow_mem=size of KVM shadow MMU\n"
    "                dump-guest-core=on|off include guest memory in a core dump (default=on)\n"
    "                mem-merge=on|off controls memory merge support (default: on)\n",
    QEMU_ARCH_ALL)
STEXI
@item -machine [type=]@var{name}[,prop=@var{value}[,...]]
@findex -machine
Select the emulated machine by @var{name}. Use @code{-machine help} to list
available machines. Supported machine properties are:
@table @option
@item accel=@var{accels1}[:@var{accels2}[:...]]
This is used to enable an accelerator. Depending on the target architecture,
kvm, xen, or tcg can be available. By default, tcg is used. If there is more
than one accelerator specified, the next one is used if the previous one fails
to initialize.
@item kernel_irqchip=on|off
Enables in-kernel irqchip support for the chosen accelerator when available.
@item kvm_shadow_mem=size
Defines the size of the KVM shadow MMU.
@item dump-guest-core=on|off
Include guest memory in a core dump. The default is on.
@item mem-merge=on|off
Enables or disables memory merge support. This feature, when supported by
the host, de-duplicates identical memory pages among VMs instances
(enabled by default).
@end table
ETEXI

HXCOMM Deprecated by -machine
DEF("M", HAS_ARG, QEMU_OPTION_M, "", QEMU_ARCH_ALL)

DEF("cpu", HAS_ARG, QEMU_OPTION_cpu,
    "-cpu cpu        select CPU ('-cpu help' for list)\n", QEMU_ARCH_ALL)
STEXI
@item -cpu @var{model}
@findex -cpu
Select CPU model (@code{-cpu help} for list and additional feature selection)
ETEXI

DEF("smp", HAS_ARG, QEMU_OPTION_smp,
    "-smp [cpus=]n[,maxcpus=cpus][,cores=cores][,threads=threads][,sockets=sockets]\n"
    "                set the number of CPUs to 'n' [default=1]\n"
    "                maxcpus= maximum number of total cpus, including\n"
    "                offline CPUs for hotplug, etc\n"
    "                cores= number of CPU cores on one socket\n"
    "                threads= number of threads on one CPU core\n"
    "                sockets= number of discrete sockets in the system\n",
        QEMU_ARCH_ALL)
STEXI
@item -smp [cpus=]@var{n}[,cores=@var{cores}][,threads=@var{threads}][,sockets=@var{sockets}][,maxcpus=@var{maxcpus}]
@findex -smp
Simulate an SMP system with @var{n} CPUs. On the PC target, up to 255
CPUs are supported. On Sparc32 target, Linux limits the number of usable CPUs
to 4.
For the PC target, the number of @var{cores} per socket, the number
of @var{threads} per cores and the total number of @var{sockets} can be
specified. Missing values will be computed. If any on the three values is
given, the total number of CPUs @var{n} can be omitted. @var{maxcpus}
specifies the maximum number of hotpluggable CPUs.
ETEXI

DEF("numa", HAS_ARG, QEMU_OPTION_numa,
    "-numa node[,mem=size][,cpus=cpu[-cpu]][,nodeid=node]\n"
    "-numa node[,memdev=id][,cpus=cpu[-cpu]][,nodeid=node]\n", QEMU_ARCH_ALL)
STEXI
@item -numa node[,mem=@var{size}][,cpus=@var{cpu[-cpu]}][,nodeid=@var{node}]
@item -numa node[,memdev=@var{id}][,cpus=@var{cpu[-cpu]}][,nodeid=@var{node}]
@findex -numa
Simulate a multi node NUMA system. If @samp{mem}, @samp{memdev}
and @samp{cpus} are omitted, resources are split equally. Also, note
that the -@option{numa} option doesn't allocate any of the specified
resources. That is, it just assigns existing resources to NUMA nodes. This
means that one still has to use the @option{-m}, @option{-smp} options
to allocate RAM and VCPUs respectively, and possibly @option{-object}
to specify the memory backend for the @samp{memdev} suboption.

@samp{mem} and @samp{memdev} are mutually exclusive.  Furthermore, if one
node uses @samp{memdev}, all of them have to use it.
ETEXI

DEF("add-fd", HAS_ARG, QEMU_OPTION_add_fd,
    "-add-fd fd=fd,set=set[,opaque=opaque]\n"
    "                Add 'fd' to fd 'set'\n", QEMU_ARCH_ALL)
STEXI
@item -add-fd fd=@var{fd},set=@var{set}[,opaque=@var{opaque}]
@findex -add-fd

Add a file descriptor to an fd set.  Valid options are:

@table @option
@item fd=@var{fd}
This option defines the file descriptor of which a duplicate is added to fd set.
The file descriptor cannot be stdin, stdout, or stderr.
@item set=@var{set}
This option defines the ID of the fd set to add the file descriptor to.
@item opaque=@var{opaque}
This option defines a free-form string that can be used to describe @var{fd}.
@end table

You can open an image using pre-opened file descriptors from an fd set:
@example
qemu-system-i386
-add-fd fd=3,set=2,opaque="rdwr:/path/to/file"
-add-fd fd=4,set=2,opaque="rdonly:/path/to/file"
-drive file=/dev/fdset/2,index=0,media=disk
@end example
ETEXI

DEF("set", HAS_ARG, QEMU_OPTION_set,
    "-set group.id.arg=value\n"
    "                set <arg> parameter for item <id> of type <group>\n"
    "                i.e. -set drive.$id.file=/path/to/image\n", QEMU_ARCH_ALL)
STEXI
@item -set @var{group}.@var{id}.@var{arg}=@var{value}
@findex -set
Set parameter @var{arg} for item @var{id} of type @var{group}\n"
ETEXI

DEF("global", HAS_ARG, QEMU_OPTION_global,
    "-global driver.prop=value\n"
    "                set a global default for a driver property\n",
    QEMU_ARCH_ALL)
STEXI
@item -global @var{driver}.@var{prop}=@var{value}
@findex -global
Set default value of @var{driver}'s property @var{prop} to @var{value}, e.g.:

@example
qemu-system-i386 -global ide-drive.physical_block_size=4096 -drive file=file,if=ide,index=0,media=disk
@end example

In particular, you can use this to set driver properties for devices which are 
created automatically by the machine model. To create a device which is not 
created automatically and set properties on it, use -@option{device}.
ETEXI

DEF("boot", HAS_ARG, QEMU_OPTION_boot,
    "-boot [order=drives][,once=drives][,menu=on|off]\n"
    "      [,splash=sp_name][,splash-time=sp_time][,reboot-timeout=rb_time][,strict=on|off]\n"
    "                'drives': floppy (a), hard disk (c), CD-ROM (d), network (n)\n"
    "                'sp_name': the file's name that would be passed to bios as logo picture, if menu=on\n"
    "                'sp_time': the period that splash picture last if menu=on, unit is ms\n"
    "                'rb_timeout': the timeout before guest reboot when boot failed, unit is ms\n",
    QEMU_ARCH_ALL)
STEXI
@item -boot [order=@var{drives}][,once=@var{drives}][,menu=on|off][,splash=@var{sp_name}][,splash-time=@var{sp_time}][,reboot-timeout=@var{rb_timeout}][,strict=on|off]
@findex -boot
Specify boot order @var{drives} as a string of drive letters. Valid
drive letters depend on the target achitecture. The x86 PC uses: a, b
(floppy 1 and 2), c (first hard disk), d (first CD-ROM), n-p (Etherboot
from network adapter 1-4), hard disk boot is the default. To apply a
particular boot order only on the first startup, specify it via
@option{once}.

Interactive boot menus/prompts can be enabled via @option{menu=on} as far
as firmware/BIOS supports them. The default is non-interactive boot.

A splash picture could be passed to bios, enabling user to show it as logo,
when option splash=@var{sp_name} is given and menu=on, If firmware/BIOS
supports them. Currently Seabios for X86 system support it.
limitation: The splash file could be a jpeg file or a BMP file in 24 BPP
format(true color). The resolution should be supported by the SVGA mode, so
the recommended is 320x240, 640x480, 800x640.

A timeout could be passed to bios, guest will pause for @var{rb_timeout} ms
when boot failed, then reboot. If @var{rb_timeout} is '-1', guest will not
reboot, qemu passes '-1' to bios by default. Currently Seabios for X86
system support it.

Do strict boot via @option{strict=on} as far as firmware/BIOS
supports it. This only effects when boot priority is changed by
bootindex options. The default is non-strict boot.

@example
# try to boot from network first, then from hard disk
qemu-system-i386 -boot order=nc
# boot from CD-ROM first, switch back to default order after reboot
qemu-system-i386 -boot once=d
# boot with a splash picture for 5 seconds.
qemu-system-i386 -boot menu=on,splash=/root/boot.bmp,splash-time=5000
@end example

Note: The legacy format '-boot @var{drives}' is still supported but its
use is discouraged as it may be removed from future versions.
ETEXI

DEF("m", HAS_ARG, QEMU_OPTION_m,
    "-m[emory] [size=]megs[,slots=n,maxmem=size]\n"
    "                configure guest RAM\n"
    "                size: initial amount of guest memory (default: "
    stringify(DEFAULT_RAM_SIZE) "MiB)\n"
    "                slots: number of hotplug slots (default: none)\n"
    "                maxmem: maximum amount of guest memory (default: none)\n",
    QEMU_ARCH_ALL)
STEXI
@item -m [size=]@var{megs}
@findex -m
Set virtual RAM size to @var{megs} megabytes. Default is 128 MiB.  Optionally,
a suffix of ``M'' or ``G'' can be used to signify a value in megabytes or
gigabytes respectively. Optional pair @var{slots}, @var{maxmem} could be used
to set amount of hotluggable memory slots and possible maximum amount of memory.
ETEXI

DEF("mem-path", HAS_ARG, QEMU_OPTION_mempath,
    "-mem-path FILE  provide backing storage for guest RAM\n", QEMU_ARCH_ALL)
STEXI
@item -mem-path @var{path}
@findex -mem-path
Allocate guest RAM from a temporarily created file in @var{path}.
ETEXI

DEF("mem-prealloc", 0, QEMU_OPTION_mem_prealloc,
    "-mem-prealloc   preallocate guest memory (use with -mem-path)\n",
    QEMU_ARCH_ALL)
STEXI
@item -mem-prealloc
@findex -mem-prealloc
Preallocate memory when using -mem-path.
ETEXI

DEF("k", HAS_ARG, QEMU_OPTION_k,
    "-k language     use keyboard layout (for example 'fr' for French)\n",
    QEMU_ARCH_ALL)
STEXI
@item -k @var{language}
@findex -k
Use keyboard layout @var{language} (for example @code{fr} for
French). This option is only needed where it is not easy to get raw PC
keycodes (e.g. on Macs, with some X11 servers or with a VNC
display). You don't normally need to use it on PC/Linux or PC/Windows
hosts.

The available layouts are:
@example
ar  de-ch  es  fo     fr-ca  hu  ja  mk     no  pt-br  sv
da  en-gb  et  fr     fr-ch  is  lt  nl     pl  ru     th
de  en-us  fi  fr-be  hr     it  lv  nl-be  pt  sl     tr
@end example

The default is @code{en-us}.
ETEXI


DEF("audio-help", 0, QEMU_OPTION_audio_help,
    "-audio-help     print list of audio drivers and their options\n",
    QEMU_ARCH_ALL)
STEXI
@item -audio-help
@findex -audio-help
Will show the audio subsystem help: list of drivers, tunable
parameters.
ETEXI

DEF("soundhw", HAS_ARG, QEMU_OPTION_soundhw,
    "-soundhw c1,... enable audio support\n"
    "                and only specified sound cards (comma separated list)\n"
    "                use '-soundhw help' to get the list of supported cards\n"
    "                use '-soundhw all' to enable all of them\n", QEMU_ARCH_ALL)
STEXI
@item -soundhw @var{card1}[,@var{card2},...] or -soundhw all
@findex -soundhw
Enable audio and selected sound hardware. Use 'help' to print all
available sound hardware.

@example
qemu-system-i386 -soundhw sb16,adlib disk.img
qemu-system-i386 -soundhw es1370 disk.img
qemu-system-i386 -soundhw ac97 disk.img
qemu-system-i386 -soundhw hda disk.img
qemu-system-i386 -soundhw all disk.img
qemu-system-i386 -soundhw help
@end example

Note that Linux's i810_audio OSS kernel (for AC97) module might
require manually specifying clocking.

@example
modprobe i810_audio clocking=48000
@end example
ETEXI

DEF("balloon", HAS_ARG, QEMU_OPTION_balloon,
    "-balloon none   disable balloon device\n"
    "-balloon virtio[,addr=str]\n"
    "                enable virtio balloon device (default)\n", QEMU_ARCH_ALL)
STEXI
@item -balloon none
@findex -balloon
Disable balloon device.
@item -balloon virtio[,addr=@var{addr}]
Enable virtio balloon device (default), optionally with PCI address
@var{addr}.
ETEXI

DEF("device", HAS_ARG, QEMU_OPTION_device,
    "-device driver[,prop[=value][,...]]\n"
    "                add device (based on driver)\n"
    "                prop=value,... sets driver properties\n"
    "                use '-device help' to print all possible drivers\n"
    "                use '-device driver,help' to print all possible properties\n",
    QEMU_ARCH_ALL)
STEXI
@item -device @var{driver}[,@var{prop}[=@var{value}][,...]]
@findex -device
Add device @var{driver}.  @var{prop}=@var{value} sets driver
properties.  Valid properties depend on the driver.  To get help on
possible drivers and properties, use @code{-device help} and
@code{-device @var{driver},help}.
ETEXI

DEF("name", HAS_ARG, QEMU_OPTION_name,
    "-name string1[,process=string2][,debug-threads=on|off]\n"
    "                set the name of the guest\n"
    "                string1 sets the window title and string2 the process name (on Linux)\n"
    "                When debug-threads is enabled, individual threads are given a separate name (on Linux)\n"
    "                NOTE: The thread names are for debugging and not a stable API.\n",
    QEMU_ARCH_ALL)
STEXI
@item -name @var{name}
@findex -name
Sets the @var{name} of the guest.
This name will be displayed in the SDL window caption.
The @var{name} will also be used for the VNC server.
Also optionally set the top visible process name in Linux.
Naming of individual threads can also be enabled on Linux to aid debugging.
ETEXI

DEF("uuid", HAS_ARG, QEMU_OPTION_uuid,
    "-uuid %08x-%04x-%04x-%04x-%012x\n"
    "                specify machine UUID\n", QEMU_ARCH_ALL)
STEXI
@item -uuid @var{uuid}
@findex -uuid
Set system UUID.
ETEXI

STEXI
@end table
ETEXI
DEFHEADING()

DEFHEADING(Block device options:)
STEXI
@table @option
ETEXI

DEF("fda", HAS_ARG, QEMU_OPTION_fda,
    "-fda/-fdb file  use 'file' as floppy disk 0/1 image\n", QEMU_ARCH_ALL)
DEF("fdb", HAS_ARG, QEMU_OPTION_fdb, "", QEMU_ARCH_ALL)
STEXI
@item -fda @var{file}
@item -fdb @var{file}
@findex -fda
@findex -fdb
Use @var{file} as floppy disk 0/1 image (@pxref{disk_images}). You can
use the host floppy by using @file{/dev/fd0} as filename (@pxref{host_drives}).
ETEXI

DEF("hda", HAS_ARG, QEMU_OPTION_hda,
    "-hda/-hdb file  use 'file' as IDE hard disk 0/1 image\n", QEMU_ARCH_ALL)
DEF("hdb", HAS_ARG, QEMU_OPTION_hdb, "", QEMU_ARCH_ALL)
DEF("hdc", HAS_ARG, QEMU_OPTION_hdc,
    "-hdc/-hdd file  use 'file' as IDE hard disk 2/3 image\n", QEMU_ARCH_ALL)
DEF("hdd", HAS_ARG, QEMU_OPTION_hdd, "", QEMU_ARCH_ALL)
STEXI
@item -hda @var{file}
@item -hdb @var{file}
@item -hdc @var{file}
@item -hdd @var{file}
@findex -hda
@findex -hdb
@findex -hdc
@findex -hdd
Use @var{file} as hard disk 0, 1, 2 or 3 image (@pxref{disk_images}).
ETEXI

DEF("cdrom", HAS_ARG, QEMU_OPTION_cdrom,
    "-cdrom file     use 'file' as IDE cdrom image (cdrom is ide1 master)\n",
    QEMU_ARCH_ALL)
STEXI
@item -cdrom @var{file}
@findex -cdrom
Use @var{file} as CD-ROM image (you cannot use @option{-hdc} and
@option{-cdrom} at the same time). You can use the host CD-ROM by
using @file{/dev/cdrom} as filename (@pxref{host_drives}).
ETEXI

DEF("drive", HAS_ARG, QEMU_OPTION_drive,
    "-drive [file=file][,if=type][,bus=n][,unit=m][,media=d][,index=i]\n"
    "       [,cyls=c,heads=h,secs=s[,trans=t]][,snapshot=on|off]\n"
    "       [,cache=writethrough|writeback|none|directsync|unsafe][,format=f]\n"
    "       [,serial=s][,addr=A][,rerror=ignore|stop|report]\n"
    "       [,werror=ignore|stop|report|enospc][,id=name][,aio=threads|native]\n"
    "       [,readonly=on|off][,copy-on-read=on|off]\n"
    "       [,detect-zeroes=on|off|unmap]\n"
    "       [[,bps=b]|[[,bps_rd=r][,bps_wr=w]]]\n"
    "       [[,iops=i]|[[,iops_rd=r][,iops_wr=w]]]\n"
    "       [[,bps_max=bm]|[[,bps_rd_max=rm][,bps_wr_max=wm]]]\n"
    "       [[,iops_max=im]|[[,iops_rd_max=irm][,iops_wr_max=iwm]]]\n"
    "       [[,iops_size=is]]\n"
    "                use 'file' as a drive image\n", QEMU_ARCH_ALL)
STEXI
@item -drive @var{option}[,@var{option}[,@var{option}[,...]]]
@findex -drive

Define a new drive. Valid options are:

@table @option
@item file=@var{file}
This option defines which disk image (@pxref{disk_images}) to use with
this drive. If the filename contains comma, you must double it
(for instance, "file=my,,file" to use file "my,file").

Special files such as iSCSI devices can be specified using protocol
specific URLs. See the section for "Device URL Syntax" for more information.
@item if=@var{interface}
This option defines on which type on interface the drive is connected.
Available types are: ide, scsi, sd, mtd, floppy, pflash, virtio.
@item bus=@var{bus},unit=@var{unit}
These options define where is connected the drive by defining the bus number and
the unit id.
@item index=@var{index}
This option defines where is connected the drive by using an index in the list
of available connectors of a given interface type.
@item media=@var{media}
This option defines the type of the media: disk or cdrom.
@item cyls=@var{c},heads=@var{h},secs=@var{s}[,trans=@var{t}]
These options have the same definition as they have in @option{-hdachs}.
@item snapshot=@var{snapshot}
@var{snapshot} is "on" or "off" and controls snapshot mode for the given drive
(see @option{-snapshot}).
@item cache=@var{cache}
@var{cache} is "none", "writeback", "unsafe", "directsync" or "writethrough" and controls how the host cache is used to access block data.
@item aio=@var{aio}
@var{aio} is "threads", or "native" and selects between pthread based disk I/O and native Linux AIO.
@item discard=@var{discard}
@var{discard} is one of "ignore" (or "off") or "unmap" (or "on") and controls whether @dfn{discard} (also known as @dfn{trim} or @dfn{unmap}) requests are ignored or passed to the filesystem.  Some machine types may not support discard requests.
@item format=@var{format}
Specify which disk @var{format} will be used rather than detecting
the format.  Can be used to specifiy format=raw to avoid interpreting
an untrusted format header.
@item serial=@var{serial}
This option specifies the serial number to assign to the device.
@item addr=@var{addr}
Specify the controller's PCI address (if=virtio only).
@item werror=@var{action},rerror=@var{action}
Specify which @var{action} to take on write and read errors. Valid actions are:
"ignore" (ignore the error and try to continue), "stop" (pause QEMU),
"report" (report the error to the guest), "enospc" (pause QEMU only if the
host disk is full; report the error to the guest otherwise).
The default setting is @option{werror=enospc} and @option{rerror=report}.
@item readonly
Open drive @option{file} as read-only. Guest write attempts will fail.
@item copy-on-read=@var{copy-on-read}
@var{copy-on-read} is "on" or "off" and enables whether to copy read backing
file sectors into the image file.
@item detect-zeroes=@var{detect-zeroes}
@var{detect-zeroes} is "off", "on" or "unmap" and enables the automatic
conversion of plain zero writes by the OS to driver specific optimized
zero write commands. You may even choose "unmap" if @var{discard} is set
to "unmap" to allow a zero write to be converted to an UNMAP operation.
@end table

By default, the @option{cache=writeback} mode is used. It will report data
writes as completed as soon as the data is present in the host page cache.
This is safe as long as your guest OS makes sure to correctly flush disk caches
where needed. If your guest OS does not handle volatile disk write caches
correctly and your host crashes or loses power, then the guest may experience
data corruption.

For such guests, you should consider using @option{cache=writethrough}. This
means that the host page cache will be used to read and write data, but write
notification will be sent to the guest only after QEMU has made sure to flush
each write to the disk. Be aware that this has a major impact on performance.

The host page cache can be avoided entirely with @option{cache=none}.  This will
attempt to do disk IO directly to the guest's memory.  QEMU may still perform
an internal copy of the data. Note that this is considered a writeback mode and
the guest OS must handle the disk write cache correctly in order to avoid data
corruption on host crashes.

The host page cache can be avoided while only sending write notifications to
the guest when the data has been flushed to the disk using
@option{cache=directsync}.

In case you don't care about data integrity over host failures, use
@option{cache=unsafe}. This option tells QEMU that it never needs to write any
data to the disk but can instead keep things in cache. If anything goes wrong,
like your host losing power, the disk storage getting disconnected accidentally,
etc. your image will most probably be rendered unusable.   When using
the @option{-snapshot} option, unsafe caching is always used.

Copy-on-read avoids accessing the same backing file sectors repeatedly and is
useful when the backing file is over a slow network.  By default copy-on-read
is off.

Instead of @option{-cdrom} you can use:
@example
qemu-system-i386 -drive file=file,index=2,media=cdrom
@end example

Instead of @option{-hda}, @option{-hdb}, @option{-hdc}, @option{-hdd}, you can
use:
@example
qemu-system-i386 -drive file=file,index=0,media=disk
qemu-system-i386 -drive file=file,index=1,media=disk
qemu-system-i386 -drive file=file,index=2,media=disk
qemu-system-i386 -drive file=file,index=3,media=disk
@end example

You can open an image using pre-opened file descriptors from an fd set:
@example
qemu-system-i386
-add-fd fd=3,set=2,opaque="rdwr:/path/to/file"
-add-fd fd=4,set=2,opaque="rdonly:/path/to/file"
-drive file=/dev/fdset/2,index=0,media=disk
@end example

You can connect a CDROM to the slave of ide0:
@example
qemu-system-i386 -drive file=file,if=ide,index=1,media=cdrom
@end example

If you don't specify the "file=" argument, you define an empty drive:
@example
qemu-system-i386 -drive if=ide,index=1,media=cdrom
@end example

You can connect a SCSI disk with unit ID 6 on the bus #0:
@example
qemu-system-i386 -drive file=file,if=scsi,bus=0,unit=6
@end example

Instead of @option{-fda}, @option{-fdb}, you can use:
@example
qemu-system-i386 -drive file=file,index=0,if=floppy
qemu-system-i386 -drive file=file,index=1,if=floppy
@end example

By default, @var{interface} is "ide" and @var{index} is automatically
incremented:
@example
qemu-system-i386 -drive file=a -drive file=b"
@end example
is interpreted like:
@example
qemu-system-i386 -hda a -hdb b
@end example
ETEXI

DEF("mtdblock", HAS_ARG, QEMU_OPTION_mtdblock,
    "-mtdblock file  use 'file' as on-board Flash memory image\n",
    QEMU_ARCH_ALL)
STEXI
@item -mtdblock @var{file}
@findex -mtdblock
Use @var{file} as on-board Flash memory image.
ETEXI

DEF("sd", HAS_ARG, QEMU_OPTION_sd,
    "-sd file        use 'file' as SecureDigital card image\n", QEMU_ARCH_ALL)
STEXI
@item -sd @var{file}
@findex -sd
Use @var{file} as SecureDigital card image.
ETEXI

DEF("pflash", HAS_ARG, QEMU_OPTION_pflash,
    "-pflash file    use 'file' as a parallel flash image\n", QEMU_ARCH_ALL)
STEXI
@item -pflash @var{file}
@findex -pflash
Use @var{file} as a parallel flash image.
ETEXI

DEF("snapshot", 0, QEMU_OPTION_snapshot,
    "-snapshot       write to temporary files instead of disk image files\n",
    QEMU_ARCH_ALL)
STEXI
@item -snapshot
@findex -snapshot
Write to temporary files instead of disk image files. In this case,
the raw disk image you use is not written back. You can however force
the write back by pressing @key{C-a s} (@pxref{disk_images}).
ETEXI

DEF("hdachs", HAS_ARG, QEMU_OPTION_hdachs, \
    "-hdachs c,h,s[,t]\n" \
    "                force hard disk 0 physical geometry and the optional BIOS\n" \
    "                translation (t=none or lba) (usually QEMU can guess them)\n",
    QEMU_ARCH_ALL)
STEXI
@item -hdachs @var{c},@var{h},@var{s},[,@var{t}]
@findex -hdachs
Force hard disk 0 physical geometry (1 <= @var{c} <= 16383, 1 <=
@var{h} <= 16, 1 <= @var{s} <= 63) and optionally force the BIOS
translation mode (@var{t}=none, lba or auto). Usually QEMU can guess
all those parameters. This option is useful for old MS-DOS disk
images.
ETEXI

DEF("fsdev", HAS_ARG, QEMU_OPTION_fsdev,
    "-fsdev fsdriver,id=id[,path=path,][security_model={mapped-xattr|mapped-file|passthrough|none}]\n"
    " [,writeout=immediate][,readonly][,socket=socket|sock_fd=sock_fd]\n",
    QEMU_ARCH_ALL)

STEXI

@item -fsdev @var{fsdriver},id=@var{id},path=@var{path},[security_model=@var{security_model}][,writeout=@var{writeout}][,readonly][,socket=@var{socket}|sock_fd=@var{sock_fd}]
@findex -fsdev
Define a new file system device. Valid options are:
@table @option
@item @var{fsdriver}
This option specifies the fs driver backend to use.
Currently "local", "handle" and "proxy" file system drivers are supported.
@item id=@var{id}
Specifies identifier for this device
@item path=@var{path}
Specifies the export path for the file system device. Files under
this path will be available to the 9p client on the guest.
@item security_model=@var{security_model}
Specifies the security model to be used for this export path.
Supported security models are "passthrough", "mapped-xattr", "mapped-file" and "none".
In "passthrough" security model, files are stored using the same
credentials as they are created on the guest. This requires QEMU
to run as root. In "mapped-xattr" security model, some of the file
attributes like uid, gid, mode bits and link target are stored as
file attributes. For "mapped-file" these attributes are stored in the
hidden .virtfs_metadata directory. Directories exported by this security model cannot
interact with other unix tools. "none" security model is same as
passthrough except the sever won't report failures if it fails to
set file attributes like ownership. Security model is mandatory
only for local fsdriver. Other fsdrivers (like handle, proxy) don't take
security model as a parameter.
@item writeout=@var{writeout}
This is an optional argument. The only supported value is "immediate".
This means that host page cache will be used to read and write data but
write notification will be sent to the guest only when the data has been
reported as written by the storage subsystem.
@item readonly
Enables exporting 9p share as a readonly mount for guests. By default
read-write access is given.
@item socket=@var{socket}
Enables proxy filesystem driver to use passed socket file for communicating
with virtfs-proxy-helper
@item sock_fd=@var{sock_fd}
Enables proxy filesystem driver to use passed socket descriptor for
communicating with virtfs-proxy-helper. Usually a helper like libvirt
will create socketpair and pass one of the fds as sock_fd
@end table

-fsdev option is used along with -device driver "virtio-9p-pci".
@item -device virtio-9p-pci,fsdev=@var{id},mount_tag=@var{mount_tag}
Options for virtio-9p-pci driver are:
@table @option
@item fsdev=@var{id}
Specifies the id value specified along with -fsdev option
@item mount_tag=@var{mount_tag}
Specifies the tag name to be used by the guest to mount this export point
@end table

ETEXI

DEF("virtfs", HAS_ARG, QEMU_OPTION_virtfs,
    "-virtfs local,path=path,mount_tag=tag,security_model=[mapped-xattr|mapped-file|passthrough|none]\n"
    "        [,writeout=immediate][,readonly][,socket=socket|sock_fd=sock_fd]\n",
    QEMU_ARCH_ALL)

STEXI

@item -virtfs @var{fsdriver}[,path=@var{path}],mount_tag=@var{mount_tag}[,security_model=@var{security_model}][,writeout=@var{writeout}][,readonly][,socket=@var{socket}|sock_fd=@var{sock_fd}]
@findex -virtfs

The general form of a Virtual File system pass-through options are:
@table @option
@item @var{fsdriver}
This option specifies the fs driver backend to use.
Currently "local", "handle" and "proxy" file system drivers are supported.
@item id=@var{id}
Specifies identifier for this device
@item path=@var{path}
Specifies the export path for the file system device. Files under
this path will be available to the 9p client on the guest.
@item security_model=@var{security_model}
Specifies the security model to be used for this export path.
Supported security models are "passthrough", "mapped-xattr", "mapped-file" and "none".
In "passthrough" security model, files are stored using the same
credentials as they are created on the guest. This requires QEMU
to run as root. In "mapped-xattr" security model, some of the file
attributes like uid, gid, mode bits and link target are stored as
file attributes. For "mapped-file" these attributes are stored in the
hidden .virtfs_metadata directory. Directories exported by this security model cannot
interact with other unix tools. "none" security model is same as
passthrough except the sever won't report failures if it fails to
set file attributes like ownership. Security model is mandatory only
for local fsdriver. Other fsdrivers (like handle, proxy) don't take security
model as a parameter.
@item writeout=@var{writeout}
This is an optional argument. The only supported value is "immediate".
This means that host page cache will be used to read and write data but
write notification will be sent to the guest only when the data has been
reported as written by the storage subsystem.
@item readonly
Enables exporting 9p share as a readonly mount for guests. By default
read-write access is given.
@item socket=@var{socket}
Enables proxy filesystem driver to use passed socket file for
communicating with virtfs-proxy-helper. Usually a helper like libvirt
will create socketpair and pass one of the fds as sock_fd
@item sock_fd
Enables proxy filesystem driver to use passed 'sock_fd' as the socket
descriptor for interfacing with virtfs-proxy-helper
@end table
ETEXI

DEF("virtfs_synth", 0, QEMU_OPTION_virtfs_synth,
    "-virtfs_synth Create synthetic file system image\n",
    QEMU_ARCH_ALL)
STEXI
@item -virtfs_synth
@findex -virtfs_synth
Create synthetic file system image
ETEXI

STEXI
@end table
ETEXI
DEFHEADING()

DEFHEADING(USB options:)
STEXI
@table @option
ETEXI

DEF("usb", 0, QEMU_OPTION_usb,
    "-usb            enable the USB driver (will be the default soon)\n",
    QEMU_ARCH_ALL)
STEXI
@item -usb
@findex -usb
Enable the USB driver (will be the default soon)
ETEXI

DEF("usbdevice", HAS_ARG, QEMU_OPTION_usbdevice,
    "-usbdevice name add the host or guest USB device 'name'\n",
    QEMU_ARCH_ALL)
STEXI

@item -usbdevice @var{devname}
@findex -usbdevice
Add the USB device @var{devname}. @xref{usb_devices}.

@table @option

@item mouse
Virtual Mouse. This will override the PS/2 mouse emulation when activated.

@item tablet
Pointer device that uses absolute coordinates (like a touchscreen). This
means QEMU is able to report the mouse position without having to grab the
mouse. Also overrides the PS/2 mouse emulation when activated.

@item disk:[format=@var{format}]:@var{file}
Mass storage device based on file. The optional @var{format} argument
will be used rather than detecting the format. Can be used to specifiy
@code{format=raw} to avoid interpreting an untrusted format header.

@item host:@var{bus}.@var{addr}
Pass through the host device identified by @var{bus}.@var{addr} (Linux only).

@item host:@var{vendor_id}:@var{product_id}
Pass through the host device identified by @var{vendor_id}:@var{product_id}
(Linux only).

@item serial:[vendorid=@var{vendor_id}][,productid=@var{product_id}]:@var{dev}
Serial converter to host character device @var{dev}, see @code{-serial} for the
available devices.

@item braille
Braille device.  This will use BrlAPI to display the braille output on a real
or fake device.

@item net:@var{options}
Network adapter that supports CDC ethernet and RNDIS protocols.

@end table
ETEXI

STEXI
@end table
ETEXI
DEFHEADING()

DEFHEADING(Display options:)
STEXI
@table @option
ETEXI

DEF("display", HAS_ARG, QEMU_OPTION_display,
    "-display sdl[,frame=on|off][,alt_grab=on|off][,ctrl_grab=on|off]\n"
    "            [,window_close=on|off]|curses|none|\n"
    "            gtk[,grab_on_hover=on|off]|\n"
    "            vnc=<display>[,<optargs>]\n"
    "                select display type\n", QEMU_ARCH_ALL)
STEXI
@item -display @var{type}
@findex -display
Select type of display to use. This option is a replacement for the
old style -sdl/-curses/... options. Valid values for @var{type} are
@table @option
@item sdl
Display video output via SDL (usually in a separate graphics
window; see the SDL documentation for other possibilities).
@item curses
Display video output via curses. For graphics device models which
support a text mode, QEMU can display this output using a
curses/ncurses interface. Nothing is displayed when the graphics
device is in graphical mode or if the graphics device does not support
a text mode. Generally only the VGA device models support text mode.
@item none
Do not display video output. The guest will still see an emulated
graphics card, but its output will not be displayed to the QEMU
user. This option differs from the -nographic option in that it
only affects what is done with video output; -nographic also changes
the destination of the serial and parallel port data.
@item gtk
Display video output in a GTK window. This interface provides drop-down
menus and other UI elements to configure and control the VM during
runtime.
@item vnc
Start a VNC server on display <arg>
@end table
ETEXI

DEF("nographic", 0, QEMU_OPTION_nographic,
    "-nographic      disable graphical output and redirect serial I/Os to console\n",
    QEMU_ARCH_ALL)
STEXI
@item -nographic
@findex -nographic
Normally, QEMU uses SDL to display the VGA output. With this option,
you can totally disable graphical output so that QEMU is a simple
command line application. The emulated serial port is redirected on
the console and muxed with the monitor (unless redirected elsewhere
explicitly). Therefore, you can still use QEMU to debug a Linux kernel
with a serial console.  Use @key{C-a h} for help on switching between
the console and monitor.
ETEXI

DEF("curses", 0, QEMU_OPTION_curses,
    "-curses         use a curses/ncurses interface instead of SDL\n",
    QEMU_ARCH_ALL)
STEXI
@item -curses
@findex -curses
Normally, QEMU uses SDL to display the VGA output.  With this option,
QEMU can display the VGA output when in text mode using a
curses/ncurses interface.  Nothing is displayed in graphical mode.
ETEXI

DEF("no-frame", 0, QEMU_OPTION_no_frame,
    "-no-frame       open SDL window without a frame and window decorations\n",
    QEMU_ARCH_ALL)
STEXI
@item -no-frame
@findex -no-frame
Do not use decorations for SDL windows and start them using the whole
available screen space. This makes the using QEMU in a dedicated desktop
workspace more convenient.
ETEXI

DEF("alt-grab", 0, QEMU_OPTION_alt_grab,
    "-alt-grab       use Ctrl-Alt-Shift to grab mouse (instead of Ctrl-Alt)\n",
    QEMU_ARCH_ALL)
STEXI
@item -alt-grab
@findex -alt-grab
Use Ctrl-Alt-Shift to grab mouse (instead of Ctrl-Alt). Note that this also
affects the special keys (for fullscreen, monitor-mode switching, etc).
ETEXI

DEF("ctrl-grab", 0, QEMU_OPTION_ctrl_grab,
    "-ctrl-grab      use Right-Ctrl to grab mouse (instead of Ctrl-Alt)\n",
    QEMU_ARCH_ALL)
STEXI
@item -ctrl-grab
@findex -ctrl-grab
Use Right-Ctrl to grab mouse (instead of Ctrl-Alt). Note that this also
affects the special keys (for fullscreen, monitor-mode switching, etc).
ETEXI

DEF("no-quit", 0, QEMU_OPTION_no_quit,
    "-no-quit        disable SDL window close capability\n", QEMU_ARCH_ALL)
STEXI
@item -no-quit
@findex -no-quit
Disable SDL window close capability.
ETEXI

DEF("sdl", 0, QEMU_OPTION_sdl,
    "-sdl            enable SDL\n", QEMU_ARCH_ALL)
STEXI
@item -sdl
@findex -sdl
Enable SDL.
ETEXI

DEF("spice", HAS_ARG, QEMU_OPTION_spice,
    "-spice [port=port][,tls-port=secured-port][,x509-dir=<dir>]\n"
    "       [,x509-key-file=<file>][,x509-key-password=<file>]\n"
    "       [,x509-cert-file=<file>][,x509-cacert-file=<file>]\n"
    "       [,x509-dh-key-file=<file>][,addr=addr][,ipv4|ipv6]\n"
    "       [,tls-ciphers=<list>]\n"
    "       [,tls-channel=[main|display|cursor|inputs|record|playback]]\n"
    "       [,plaintext-channel=[main|display|cursor|inputs|record|playback]]\n"
    "       [,sasl][,password=<secret>][,disable-ticketing]\n"
    "       [,image-compression=[auto_glz|auto_lz|quic|glz|lz|off]]\n"
    "       [,jpeg-wan-compression=[auto|never|always]]\n"
    "       [,zlib-glz-wan-compression=[auto|never|always]]\n"
    "       [,streaming-video=[off|all|filter]][,disable-copy-paste]\n"
    "       [,disable-agent-file-xfer][,agent-mouse=[on|off]]\n"
    "       [,playback-compression=[on|off]][,seamless-migration=[on|off]]\n"
    "   enable spice\n"
    "   at least one of {port, tls-port} is mandatory\n",
    QEMU_ARCH_ALL)
STEXI
@item -spice @var{option}[,@var{option}[,...]]
@findex -spice
Enable the spice remote desktop protocol. Valid options are

@table @option

@item port=<nr>
Set the TCP port spice is listening on for plaintext channels.

@item addr=<addr>
Set the IP address spice is listening on.  Default is any address.

@item ipv4
@item ipv6
Force using the specified IP version.

@item password=<secret>
Set the password you need to authenticate.

@item sasl
Require that the client use SASL to authenticate with the spice.
The exact choice of authentication method used is controlled from the
system / user's SASL configuration file for the 'qemu' service. This
is typically found in /etc/sasl2/qemu.conf. If running QEMU as an
unprivileged user, an environment variable SASL_CONF_PATH can be used
to make it search alternate locations for the service config.
While some SASL auth methods can also provide data encryption (eg GSSAPI),
it is recommended that SASL always be combined with the 'tls' and
'x509' settings to enable use of SSL and server certificates. This
ensures a data encryption preventing compromise of authentication
credentials.

@item disable-ticketing
Allow client connects without authentication.

@item disable-copy-paste
Disable copy paste between the client and the guest.

@item disable-agent-file-xfer
Disable spice-vdagent based file-xfer between the client and the guest.

@item tls-port=<nr>
Set the TCP port spice is listening on for encrypted channels.

@item x509-dir=<dir>
Set the x509 file directory. Expects same filenames as -vnc $display,x509=$dir

@item x509-key-file=<file>
@item x509-key-password=<file>
@item x509-cert-file=<file>
@item x509-cacert-file=<file>
@item x509-dh-key-file=<file>
The x509 file names can also be configured individually.

@item tls-ciphers=<list>
Specify which ciphers to use.

@item tls-channel=[main|display|cursor|inputs|record|playback]
@item plaintext-channel=[main|display|cursor|inputs|record|playback]
Force specific channel to be used with or without TLS encryption.  The
options can be specified multiple times to configure multiple
channels.  The special name "default" can be used to set the default
mode.  For channels which are not explicitly forced into one mode the
spice client is allowed to pick tls/plaintext as he pleases.

@item image-compression=[auto_glz|auto_lz|quic|glz|lz|off]
Configure image compression (lossless).
Default is auto_glz.

@item jpeg-wan-compression=[auto|never|always]
@item zlib-glz-wan-compression=[auto|never|always]
Configure wan image compression (lossy for slow links).
Default is auto.

@item streaming-video=[off|all|filter]
Configure video stream detection.  Default is filter.

@item agent-mouse=[on|off]
Enable/disable passing mouse events via vdagent.  Default is on.

@item playback-compression=[on|off]
Enable/disable audio stream compression (using celt 0.5.1).  Default is on.

@item seamless-migration=[on|off]
Enable/disable spice seamless migration. Default is off.

@end table
ETEXI

DEF("portrait", 0, QEMU_OPTION_portrait,
    "-portrait       rotate graphical output 90 deg left (only PXA LCD)\n",
    QEMU_ARCH_ALL)
STEXI
@item -portrait
@findex -portrait
Rotate graphical output 90 deg left (only PXA LCD).
ETEXI

DEF("rotate", HAS_ARG, QEMU_OPTION_rotate,
    "-rotate <deg>   rotate graphical output some deg left (only PXA LCD)\n",
    QEMU_ARCH_ALL)
STEXI
@item -rotate @var{deg}
@findex -rotate
Rotate graphical output some deg left (only PXA LCD).
ETEXI

DEF("vga", HAS_ARG, QEMU_OPTION_vga,
    "-vga [std|cirrus|vmware|qxl|xenfb|tcx|cg3|none]\n"
    "                select video card type\n", QEMU_ARCH_ALL)
STEXI
@item -vga @var{type}
@findex -vga
Select type of VGA card to emulate. Valid values for @var{type} are
@table @option
@item cirrus
Cirrus Logic GD5446 Video card. All Windows versions starting from
Windows 95 should recognize and use this graphic card. For optimal
performances, use 16 bit color depth in the guest and the host OS.
(This one is the default)
@item std
Standard VGA card with Bochs VBE extensions.  If your guest OS
supports the VESA 2.0 VBE extensions (e.g. Windows XP) and if you want
to use high resolution modes (>= 1280x1024x16) then you should use
this option.
@item vmware
VMWare SVGA-II compatible adapter. Use it if you have sufficiently
recent XFree86/XOrg server or Windows guest with a driver for this
card.
@item qxl
QXL paravirtual graphic card.  It is VGA compatible (including VESA
2.0 VBE support).  Works best with qxl guest drivers installed though.
Recommended choice when using the spice protocol.
@item tcx
(sun4m only) Sun TCX framebuffer. This is the default framebuffer for
sun4m machines and offers both 8-bit and 24-bit colour depths at a
fixed resolution of 1024x768.
@item cg3
(sun4m only) Sun cgthree framebuffer. This is a simple 8-bit framebuffer
for sun4m machines available in both 1024x768 (OpenBIOS) and 1152x900 (OBP)
resolutions aimed at people wishing to run older Solaris versions.
@item none
Disable VGA card.
@end table
ETEXI

DEF("full-screen", 0, QEMU_OPTION_full_screen,
    "-full-screen    start in full screen\n", QEMU_ARCH_ALL)
STEXI
@item -full-screen
@findex -full-screen
Start in full screen.
ETEXI

DEF("g", 1, QEMU_OPTION_g ,
    "-g WxH[xDEPTH]  Set the initial graphical resolution and depth\n",
    QEMU_ARCH_PPC | QEMU_ARCH_SPARC)
STEXI
@item -g @var{width}x@var{height}[x@var{depth}]
@findex -g
Set the initial graphical resolution and depth (PPC, SPARC only).
ETEXI

DEF("vnc", HAS_ARG, QEMU_OPTION_vnc ,
    "-vnc display    start a VNC server on display\n", QEMU_ARCH_ALL)
STEXI
@item -vnc @var{display}[,@var{option}[,@var{option}[,...]]]
@findex -vnc
Normally, QEMU uses SDL to display the VGA output.  With this option,
you can have QEMU listen on VNC display @var{display} and redirect the VGA
display over the VNC session.  It is very useful to enable the usb
tablet device when using this option (option @option{-usbdevice
tablet}). When using the VNC display, you must use the @option{-k}
parameter to set the keyboard layout if you are not using en-us. Valid
syntax for the @var{display} is

@table @option

@item @var{host}:@var{d}

TCP connections will only be allowed from @var{host} on display @var{d}.
By convention the TCP port is 5900+@var{d}. Optionally, @var{host} can
be omitted in which case the server will accept connections from any host.

@item unix:@var{path}

Connections will be allowed over UNIX domain sockets where @var{path} is the
location of a unix socket to listen for connections on.

@item none

VNC is initialized but not started. The monitor @code{change} command
can be used to later start the VNC server.

@end table

Following the @var{display} value there may be one or more @var{option} flags
separated by commas. Valid options are

@table @option

@item reverse

Connect to a listening VNC client via a ``reverse'' connection. The
client is specified by the @var{display}. For reverse network
connections (@var{host}:@var{d},@code{reverse}), the @var{d} argument
is a TCP port number, not a display number.

@item websocket

Opens an additional TCP listening port dedicated to VNC Websocket connections.
By definition the Websocket port is 5700+@var{display}. If @var{host} is
specified connections will only be allowed from this host.
As an alternative the Websocket port could be specified by using
@code{websocket}=@var{port}.
TLS encryption for the Websocket connection is supported if the required
certificates are specified with the VNC option @option{x509}.

@item password

Require that password based authentication is used for client connections.

The password must be set separately using the @code{set_password} command in
the @ref{pcsys_monitor}. The syntax to change your password is:
@code{set_password <protocol> <password>} where <protocol> could be either
"vnc" or "spice".

If you would like to change <protocol> password expiration, you should use
@code{expire_password <protocol> <expiration-time>} where expiration time could
be one of the following options: now, never, +seconds or UNIX time of
expiration, e.g. +60 to make password expire in 60 seconds, or 1335196800
to make password expire on "Mon Apr 23 12:00:00 EDT 2012" (UNIX time for this
date and time).

You can also use keywords "now" or "never" for the expiration time to
allow <protocol> password to expire immediately or never expire.

@item tls

Require that client use TLS when communicating with the VNC server. This
uses anonymous TLS credentials so is susceptible to a man-in-the-middle
attack. It is recommended that this option be combined with either the
@option{x509} or @option{x509verify} options.

@item x509=@var{/path/to/certificate/dir}

Valid if @option{tls} is specified. Require that x509 credentials are used
for negotiating the TLS session. The server will send its x509 certificate
to the client. It is recommended that a password be set on the VNC server
to provide authentication of the client when this is used. The path following
this option specifies where the x509 certificates are to be loaded from.
See the @ref{vnc_security} section for details on generating certificates.

@item x509verify=@var{/path/to/certificate/dir}

Valid if @option{tls} is specified. Require that x509 credentials are used
for negotiating the TLS session. The server will send its x509 certificate
to the client, and request that the client send its own x509 certificate.
The server will validate the client's certificate against the CA certificate,
and reject clients when validation fails. If the certificate authority is
trusted, this is a sufficient authentication mechanism. You may still wish
to set a password on the VNC server as a second authentication layer. The
path following this option specifies where the x509 certificates are to
be loaded from. See the @ref{vnc_security} section for details on generating
certificates.

@item sasl

Require that the client use SASL to authenticate with the VNC server.
The exact choice of authentication method used is controlled from the
system / user's SASL configuration file for the 'qemu' service. This
is typically found in /etc/sasl2/qemu.conf. If running QEMU as an
unprivileged user, an environment variable SASL_CONF_PATH can be used
to make it search alternate locations for the service config.
While some SASL auth methods can also provide data encryption (eg GSSAPI),
it is recommended that SASL always be combined with the 'tls' and
'x509' settings to enable use of SSL and server certificates. This
ensures a data encryption preventing compromise of authentication
credentials. See the @ref{vnc_security} section for details on using
SASL authentication.

@item acl

Turn on access control lists for checking of the x509 client certificate
and SASL party. For x509 certs, the ACL check is made against the
certificate's distinguished name. This is something that looks like
@code{C=GB,O=ACME,L=Boston,CN=bob}. For SASL party, the ACL check is
made against the username, which depending on the SASL plugin, may
include a realm component, eg @code{bob} or @code{bob@@EXAMPLE.COM}.
When the @option{acl} flag is set, the initial access list will be
empty, with a @code{deny} policy. Thus no one will be allowed to
use the VNC server until the ACLs have been loaded. This can be
achieved using the @code{acl} monitor command.

@item lossy

Enable lossy compression methods (gradient, JPEG, ...). If this
option is set, VNC client may receive lossy framebuffer updates
depending on its encoding settings. Enabling this option can save
a lot of bandwidth at the expense of quality.

@item non-adaptive

Disable adaptive encodings. Adaptive encodings are enabled by default.
An adaptive encoding will try to detect frequently updated screen regions,
and send updates in these regions using a lossy encoding (like JPEG).
This can be really helpful to save bandwidth when playing videos. Disabling
adaptive encodings restores the original static behavior of encodings
like Tight.

@item share=[allow-exclusive|force-shared|ignore]

Set display sharing policy.  'allow-exclusive' allows clients to ask
for exclusive access.  As suggested by the rfb spec this is
implemented by dropping other connections.  Connecting multiple
clients in parallel requires all clients asking for a shared session
(vncviewer: -shared switch).  This is the default.  'force-shared'
disables exclusive client access.  Useful for shared desktop sessions,
where you don't want someone forgetting specify -shared disconnect
everybody else.  'ignore' completely ignores the shared flag and
allows everybody connect unconditionally.  Doesn't conform to the rfb
spec but is traditional QEMU behavior.

@end table
ETEXI

STEXI
@end table
ETEXI
ARCHHEADING(, QEMU_ARCH_I386)

ARCHHEADING(i386 target only:, QEMU_ARCH_I386)
STEXI
@table @option
ETEXI

DEF("win2k-hack", 0, QEMU_OPTION_win2k_hack,
    "-win2k-hack     use it when installing Windows 2000 to avoid a disk full bug\n",
    QEMU_ARCH_I386)
STEXI
@item -win2k-hack
@findex -win2k-hack
Use it when installing Windows 2000 to avoid a disk full bug. After
Windows 2000 is installed, you no longer need this option (this option
slows down the IDE transfers).
ETEXI

HXCOMM Deprecated by -rtc
DEF("rtc-td-hack", 0, QEMU_OPTION_rtc_td_hack, "", QEMU_ARCH_I386)

DEF("no-fd-bootchk", 0, QEMU_OPTION_no_fd_bootchk,
    "-no-fd-bootchk  disable boot signature checking for floppy disks\n",
    QEMU_ARCH_I386)
STEXI
@item -no-fd-bootchk
@findex -no-fd-bootchk
Disable boot signature checking for floppy disks in BIOS. May
be needed to boot from old floppy disks.
ETEXI

DEF("no-acpi", 0, QEMU_OPTION_no_acpi,
           "-no-acpi        disable ACPI\n", QEMU_ARCH_I386)
STEXI
@item -no-acpi
@findex -no-acpi
Disable ACPI (Advanced Configuration and Power Interface) support. Use
it if your guest OS complains about ACPI problems (PC target machine
only).
ETEXI

DEF("no-hpet", 0, QEMU_OPTION_no_hpet,
    "-no-hpet        disable HPET\n", QEMU_ARCH_I386)
STEXI
@item -no-hpet
@findex -no-hpet
Disable HPET support.
ETEXI

DEF("acpitable", HAS_ARG, QEMU_OPTION_acpitable,
    "-acpitable [sig=str][,rev=n][,oem_id=str][,oem_table_id=str][,oem_rev=n][,asl_compiler_id=str][,asl_compiler_rev=n][,{data|file}=file1[:file2]...]\n"
    "                ACPI table description\n", QEMU_ARCH_I386)
STEXI
@item -acpitable [sig=@var{str}][,rev=@var{n}][,oem_id=@var{str}][,oem_table_id=@var{str}][,oem_rev=@var{n}] [,asl_compiler_id=@var{str}][,asl_compiler_rev=@var{n}][,data=@var{file1}[:@var{file2}]...]
@findex -acpitable
Add ACPI table with specified header fields and context from specified files.
For file=, take whole ACPI table from the specified files, including all
ACPI headers (possible overridden by other options).
For data=, only data
portion of the table is used, all header information is specified in the
command line.
ETEXI

DEF("smbios", HAS_ARG, QEMU_OPTION_smbios,
    "-smbios file=binary\n"
    "                load SMBIOS entry from binary file\n"
    "-smbios type=0[,vendor=str][,version=str][,date=str][,release=%d.%d][,uefi=on|off]\n"
    "                specify SMBIOS type 0 fields\n"
    "-smbios type=1[,manufacturer=str][,product=str][,version=str][,serial=str]\n"
    "              [,uuid=uuid][,sku=str][,family=str]\n"
    "                specify SMBIOS type 1 fields\n", QEMU_ARCH_I386)
STEXI
@item -smbios file=@var{binary}
@findex -smbios
Load SMBIOS entry from binary file.

@item -smbios type=0[,vendor=@var{str}][,version=@var{str}][,date=@var{str}][,release=@var{%d.%d}][,uefi=on|off]
Specify SMBIOS type 0 fields

@item -smbios type=1[,manufacturer=@var{str}][,product=@var{str}] [,version=@var{str}][,serial=@var{str}][,uuid=@var{uuid}][,sku=@var{str}] [,family=@var{str}]
Specify SMBIOS type 1 fields
ETEXI

STEXI
@end table
ETEXI
DEFHEADING()

DEFHEADING(Network options:)
STEXI
@table @option
ETEXI

HXCOMM Legacy slirp options (now moved to -net user):
#ifdef CONFIG_SLIRP
DEF("tftp", HAS_ARG, QEMU_OPTION_tftp, "", QEMU_ARCH_ALL)
DEF("bootp", HAS_ARG, QEMU_OPTION_bootp, "", QEMU_ARCH_ALL)
DEF("redir", HAS_ARG, QEMU_OPTION_redir, "", QEMU_ARCH_ALL)
#ifndef _WIN32
DEF("smb", HAS_ARG, QEMU_OPTION_smb, "", QEMU_ARCH_ALL)
#endif
#endif

DEF("net", HAS_ARG, QEMU_OPTION_net,
    "-net nic[,vlan=n][,macaddr=mac][,model=type][,name=str][,addr=str][,vectors=v]\n"
    "                create a new Network Interface Card and connect it to VLAN 'n'\n"
#ifdef CONFIG_SLIRP
    "-net user[,vlan=n][,name=str][,net=addr[/mask]][,host=addr][,restrict=on|off]\n"
    "         [,hostname=host][,dhcpstart=addr][,dns=addr][,dnssearch=domain][,tftp=dir]\n"
    "         [,bootfile=f][,hostfwd=rule][,guestfwd=rule]"
#ifndef _WIN32
                                             "[,smb=dir[,smbserver=addr]]\n"
#endif
    "                connect the user mode network stack to VLAN 'n', configure its\n"
    "                DHCP server and enabled optional services\n"
#endif
#ifdef _WIN32
    "-net tap[,vlan=n][,name=str],ifname=name\n"
    "                connect the host TAP network interface to VLAN 'n'\n"
#else
    "-net tap[,vlan=n][,name=str][,fd=h][,fds=x:y:...:z][,ifname=name][,script=file][,downscript=dfile][,helper=helper][,sndbuf=nbytes][,vnet_hdr=on|off][,vhost=on|off][,vhostfd=h][,vhostfds=x:y:...:z][,vhostforce=on|off][,queues=n]\n"
    "                connect the host TAP network interface to VLAN 'n'\n"
    "                use network scripts 'file' (default=" DEFAULT_NETWORK_SCRIPT ")\n"
    "                to configure it and 'dfile' (default=" DEFAULT_NETWORK_DOWN_SCRIPT ")\n"
    "                to deconfigure it\n"
    "                use '[down]script=no' to disable script execution\n"
    "                use network helper 'helper' (default=" DEFAULT_BRIDGE_HELPER ") to\n"
    "                configure it\n"
    "                use 'fd=h' to connect to an already opened TAP interface\n"
    "                use 'fds=x:y:...:z' to connect to already opened multiqueue capable TAP interfaces\n"
    "                use 'sndbuf=nbytes' to limit the size of the send buffer (the\n"
    "                default is disabled 'sndbuf=0' to enable flow control set 'sndbuf=1048576')\n"
    "                use vnet_hdr=off to avoid enabling the IFF_VNET_HDR tap flag\n"
    "                use vnet_hdr=on to make the lack of IFF_VNET_HDR support an error condition\n"
    "                use vhost=on to enable experimental in kernel accelerator\n"
    "                    (only has effect for virtio guests which use MSIX)\n"
    "                use vhostforce=on to force vhost on for non-MSIX virtio guests\n"
    "                use 'vhostfd=h' to connect to an already opened vhost net device\n"
    "                use 'vhostfds=x:y:...:z to connect to multiple already opened vhost net devices\n"
    "                use 'queues=n' to specify the number of queues to be created for multiqueue TAP\n"
    "-net bridge[,vlan=n][,name=str][,br=bridge][,helper=helper]\n"
    "                connects a host TAP network interface to a host bridge device 'br'\n"
    "                (default=" DEFAULT_BRIDGE_INTERFACE ") using the program 'helper'\n"
    "                (default=" DEFAULT_BRIDGE_HELPER ")\n"
#endif
    "-net socket[,vlan=n][,name=str][,fd=h][,listen=[host]:port][,connect=host:port]\n"
    "                connect the vlan 'n' to another VLAN using a socket connection\n"
    "-net socket[,vlan=n][,name=str][,fd=h][,mcast=maddr:port[,localaddr=addr]]\n"
    "                connect the vlan 'n' to multicast maddr and port\n"
    "                use 'localaddr=addr' to specify the host address to send packets from\n"
    "-net socket[,vlan=n][,name=str][,fd=h][,udp=host:port][,localaddr=host:port]\n"
    "                connect the vlan 'n' to another VLAN using an UDP tunnel\n"
#ifdef CONFIG_VDE
    "-net vde[,vlan=n][,name=str][,sock=socketpath][,port=n][,group=groupname][,mode=octalmode]\n"
    "                connect the vlan 'n' to port 'n' of a vde switch running\n"
    "                on host and listening for incoming connections on 'socketpath'.\n"
    "                Use group 'groupname' and mode 'octalmode' to change default\n"
    "                ownership and permissions for communication port.\n"
#endif
#ifdef CONFIG_NETMAP
    "-net netmap,ifname=name[,devname=nmname]\n"
    "                attach to the existing netmap-enabled network interface 'name', or to a\n"
    "                VALE port (created on the fly) called 'name' ('nmname' is name of the \n"
    "                netmap device, defaults to '/dev/netmap')\n"
#endif
    "-net dump[,vlan=n][,file=f][,len=n]\n"
    "                dump traffic on vlan 'n' to file 'f' (max n bytes per packet)\n"
    "-net none       use it alone to have zero network devices. If no -net option\n"
    "                is provided, the default is '-net nic -net user'\n", QEMU_ARCH_ALL)
DEF("netdev", HAS_ARG, QEMU_OPTION_netdev,
    "-netdev ["
#ifdef CONFIG_SLIRP
    "user|"
#endif
    "tap|"
    "bridge|"
#ifdef CONFIG_VDE
    "vde|"
#endif
#ifdef CONFIG_NETMAP
    "netmap|"
#endif
    "vhost-user|"
    "socket|"
    "hubport],id=str[,option][,option][,...]\n", QEMU_ARCH_ALL)
STEXI
@item -net nic[,vlan=@var{n}][,macaddr=@var{mac}][,model=@var{type}] [,name=@var{name}][,addr=@var{addr}][,vectors=@var{v}]
@findex -net
Create a new Network Interface Card and connect it to VLAN @var{n} (@var{n}
= 0 is the default). The NIC is an e1000 by default on the PC
target. Optionally, the MAC address can be changed to @var{mac}, the
device address set to @var{addr} (PCI cards only),
and a @var{name} can be assigned for use in monitor commands.
Optionally, for PCI cards, you can specify the number @var{v} of MSI-X vectors
that the card should have; this option currently only affects virtio cards; set
@var{v} = 0 to disable MSI-X. If no @option{-net} option is specified, a single
NIC is created.  QEMU can emulate several different models of network card.
Valid values for @var{type} are
@code{virtio}, @code{i82551}, @code{i82557b}, @code{i82559er},
@code{ne2k_pci}, @code{ne2k_isa}, @code{pcnet}, @code{rtl8139},
@code{e1000}, @code{smc91c111}, @code{lance} and @code{mcf_fec}.
Not all devices are supported on all targets.  Use @code{-net nic,model=help}
for a list of available devices for your target.

@item -netdev user,id=@var{id}[,@var{option}][,@var{option}][,...]
@findex -netdev
@item -net user[,@var{option}][,@var{option}][,...]
Use the user mode network stack which requires no administrator
privilege to run. Valid options are:

@table @option
@item vlan=@var{n}
Connect user mode stack to VLAN @var{n} (@var{n} = 0 is the default).

@item id=@var{id}
@item name=@var{name}
Assign symbolic name for use in monitor commands.

@item net=@var{addr}[/@var{mask}]
Set IP network address the guest will see. Optionally specify the netmask,
either in the form a.b.c.d or as number of valid top-most bits. Default is
10.0.2.0/24.

@item host=@var{addr}
Specify the guest-visible address of the host. Default is the 2nd IP in the
guest network, i.e. x.x.x.2.

@item restrict=on|off
If this option is enabled, the guest will be isolated, i.e. it will not be
able to contact the host and no guest IP packets will be routed over the host
to the outside. This option does not affect any explicitly set forwarding rules.

@item hostname=@var{name}
Specifies the client hostname reported by the built-in DHCP server.

@item dhcpstart=@var{addr}
Specify the first of the 16 IPs the built-in DHCP server can assign. Default
is the 15th to 31st IP in the guest network, i.e. x.x.x.15 to x.x.x.31.

@item dns=@var{addr}
Specify the guest-visible address of the virtual nameserver. The address must
be different from the host address. Default is the 3rd IP in the guest network,
i.e. x.x.x.3.

@item dnssearch=@var{domain}
Provides an entry for the domain-search list sent by the built-in
DHCP server. More than one domain suffix can be transmitted by specifying
this option multiple times. If supported, this will cause the guest to
automatically try to append the given domain suffix(es) in case a domain name
can not be resolved.

Example:
@example
qemu -net user,dnssearch=mgmt.example.org,dnssearch=example.org [...]
@end example

@item tftp=@var{dir}
When using the user mode network stack, activate a built-in TFTP
server. The files in @var{dir} will be exposed as the root of a TFTP server.
The TFTP client on the guest must be configured in binary mode (use the command
@code{bin} of the Unix TFTP client).

@item bootfile=@var{file}
When using the user mode network stack, broadcast @var{file} as the BOOTP
filename. In conjunction with @option{tftp}, this can be used to network boot
a guest from a local directory.

Example (using pxelinux):
@example
qemu-system-i386 -hda linux.img -boot n -net user,tftp=/path/to/tftp/files,bootfile=/pxelinux.0
@end example

@item smb=@var{dir}[,smbserver=@var{addr}]
When using the user mode network stack, activate a built-in SMB
server so that Windows OSes can access to the host files in @file{@var{dir}}
transparently. The IP address of the SMB server can be set to @var{addr}. By
default the 4th IP in the guest network is used, i.e. x.x.x.4.

In the guest Windows OS, the line:
@example
10.0.2.4 smbserver
@end example
must be added in the file @file{C:\WINDOWS\LMHOSTS} (for windows 9x/Me)
or @file{C:\WINNT\SYSTEM32\DRIVERS\ETC\LMHOSTS} (Windows NT/2000).

Then @file{@var{dir}} can be accessed in @file{\\smbserver\qemu}.

Note that a SAMBA server must be installed on the host OS.
QEMU was tested successfully with smbd versions from Red Hat 9,
Fedora Core 3 and OpenSUSE 11.x.

@item hostfwd=[tcp|udp]:[@var{hostaddr}]:@var{hostport}-[@var{guestaddr}]:@var{guestport}
Redirect incoming TCP or UDP connections to the host port @var{hostport} to
the guest IP address @var{guestaddr} on guest port @var{guestport}. If
@var{guestaddr} is not specified, its value is x.x.x.15 (default first address
given by the built-in DHCP server). By specifying @var{hostaddr}, the rule can
be bound to a specific host interface. If no connection type is set, TCP is
used. This option can be given multiple times.

For example, to redirect host X11 connection from screen 1 to guest
screen 0, use the following:

@example
# on the host
qemu-system-i386 -net user,hostfwd=tcp:127.0.0.1:6001-:6000 [...]
# this host xterm should open in the guest X11 server
xterm -display :1
@end example

To redirect telnet connections from host port 5555 to telnet port on
the guest, use the following:

@example
# on the host
qemu-system-i386 -net user,hostfwd=tcp::5555-:23 [...]
telnet localhost 5555
@end example

Then when you use on the host @code{telnet localhost 5555}, you
connect to the guest telnet server.

@item guestfwd=[tcp]:@var{server}:@var{port}-@var{dev}
@item guestfwd=[tcp]:@var{server}:@var{port}-@var{cmd:command}
Forward guest TCP connections to the IP address @var{server} on port @var{port}
to the character device @var{dev} or to a program executed by @var{cmd:command}
which gets spawned for each connection. This option can be given multiple times.

You can either use a chardev directly and have that one used throughout QEMU's
lifetime, like in the following example:

@example
# open 10.10.1.1:4321 on bootup, connect 10.0.2.100:1234 to it whenever
# the guest accesses it
qemu -net user,guestfwd=tcp:10.0.2.100:1234-tcp:10.10.1.1:4321 [...]
@end example

Or you can execute a command on every TCP connection established by the guest,
so that QEMU behaves similar to an inetd process for that virtual server:

@example
# call "netcat 10.10.1.1 4321" on every TCP connection to 10.0.2.100:1234
# and connect the TCP stream to its stdin/stdout
qemu -net 'user,guestfwd=tcp:10.0.2.100:1234-cmd:netcat 10.10.1.1 4321'
@end example

@end table

Note: Legacy stand-alone options -tftp, -bootp, -smb and -redir are still
processed and applied to -net user. Mixing them with the new configuration
syntax gives undefined results. Their use for new applications is discouraged
as they will be removed from future versions.

@item -netdev tap,id=@var{id}[,fd=@var{h}][,ifname=@var{name}][,script=@var{file}][,downscript=@var{dfile}][,helper=@var{helper}]
@item -net tap[,vlan=@var{n}][,name=@var{name}][,fd=@var{h}][,ifname=@var{name}][,script=@var{file}][,downscript=@var{dfile}][,helper=@var{helper}]
Connect the host TAP network interface @var{name} to VLAN @var{n}.

Use the network script @var{file} to configure it and the network script
@var{dfile} to deconfigure it. If @var{name} is not provided, the OS
automatically provides one. The default network configure script is
@file{/etc/qemu-ifup} and the default network deconfigure script is
@file{/etc/qemu-ifdown}. Use @option{script=no} or @option{downscript=no}
to disable script execution.

If running QEMU as an unprivileged user, use the network helper
@var{helper} to configure the TAP interface. The default network
helper executable is @file{/path/to/qemu-bridge-helper}.

@option{fd}=@var{h} can be used to specify the handle of an already
opened host TAP interface.

Examples:

@example
#launch a QEMU instance with the default network script
qemu-system-i386 linux.img -net nic -net tap
@end example

@example
#launch a QEMU instance with two NICs, each one connected
#to a TAP device
qemu-system-i386 linux.img \
                 -net nic,vlan=0 -net tap,vlan=0,ifname=tap0 \
                 -net nic,vlan=1 -net tap,vlan=1,ifname=tap1
@end example

@example
#launch a QEMU instance with the default network helper to
#connect a TAP device to bridge br0
qemu-system-i386 linux.img \
                 -net nic -net tap,"helper=/path/to/qemu-bridge-helper"
@end example

@item -netdev bridge,id=@var{id}[,br=@var{bridge}][,helper=@var{helper}]
@item -net bridge[,vlan=@var{n}][,name=@var{name}][,br=@var{bridge}][,helper=@var{helper}]
Connect a host TAP network interface to a host bridge device.

Use the network helper @var{helper} to configure the TAP interface and
attach it to the bridge. The default network helper executable is
@file{/path/to/qemu-bridge-helper} and the default bridge
device is @file{br0}.

Examples:

@example
#launch a QEMU instance with the default network helper to
#connect a TAP device to bridge br0
qemu-system-i386 linux.img -net bridge -net nic,model=virtio
@end example

@example
#launch a QEMU instance with the default network helper to
#connect a TAP device to bridge qemubr0
qemu-system-i386 linux.img -net bridge,br=qemubr0 -net nic,model=virtio
@end example

@item -netdev socket,id=@var{id}[,fd=@var{h}][,listen=[@var{host}]:@var{port}][,connect=@var{host}:@var{port}]
@item -net socket[,vlan=@var{n}][,name=@var{name}][,fd=@var{h}] [,listen=[@var{host}]:@var{port}][,connect=@var{host}:@var{port}]

Connect the VLAN @var{n} to a remote VLAN in another QEMU virtual
machine using a TCP socket connection. If @option{listen} is
specified, QEMU waits for incoming connections on @var{port}
(@var{host} is optional). @option{connect} is used to connect to
another QEMU instance using the @option{listen} option. @option{fd}=@var{h}
specifies an already opened TCP socket.

Example:
@example
# launch a first QEMU instance
qemu-system-i386 linux.img \
                 -net nic,macaddr=52:54:00:12:34:56 \
                 -net socket,listen=:1234
# connect the VLAN 0 of this instance to the VLAN 0
# of the first instance
qemu-system-i386 linux.img \
                 -net nic,macaddr=52:54:00:12:34:57 \
                 -net socket,connect=127.0.0.1:1234
@end example

@item -netdev socket,id=@var{id}[,fd=@var{h}][,mcast=@var{maddr}:@var{port}[,localaddr=@var{addr}]]
@item -net socket[,vlan=@var{n}][,name=@var{name}][,fd=@var{h}][,mcast=@var{maddr}:@var{port}[,localaddr=@var{addr}]]

Create a VLAN @var{n} shared with another QEMU virtual
machines using a UDP multicast socket, effectively making a bus for
every QEMU with same multicast address @var{maddr} and @var{port}.
NOTES:
@enumerate
@item
Several QEMU can be running on different hosts and share same bus (assuming
correct multicast setup for these hosts).
@item
mcast support is compatible with User Mode Linux (argument @option{eth@var{N}=mcast}), see
@url{http://user-mode-linux.sf.net}.
@item
Use @option{fd=h} to specify an already opened UDP multicast socket.
@end enumerate

Example:
@example
# launch one QEMU instance
qemu-system-i386 linux.img \
                 -net nic,macaddr=52:54:00:12:34:56 \
                 -net socket,mcast=230.0.0.1:1234
# launch another QEMU instance on same "bus"
qemu-system-i386 linux.img \
                 -net nic,macaddr=52:54:00:12:34:57 \
                 -net socket,mcast=230.0.0.1:1234
# launch yet another QEMU instance on same "bus"
qemu-system-i386 linux.img \
                 -net nic,macaddr=52:54:00:12:34:58 \
                 -net socket,mcast=230.0.0.1:1234
@end example

Example (User Mode Linux compat.):
@example
# launch QEMU instance (note mcast address selected
# is UML's default)
qemu-system-i386 linux.img \
                 -net nic,macaddr=52:54:00:12:34:56 \
                 -net socket,mcast=239.192.168.1:1102
# launch UML
/path/to/linux ubd0=/path/to/root_fs eth0=mcast
@end example

Example (send packets from host's 1.2.3.4):
@example
qemu-system-i386 linux.img \
                 -net nic,macaddr=52:54:00:12:34:56 \
                 -net socket,mcast=239.192.168.1:1102,localaddr=1.2.3.4
@end example

@item -netdev vde,id=@var{id}[,sock=@var{socketpath}][,port=@var{n}][,group=@var{groupname}][,mode=@var{octalmode}]
@item -net vde[,vlan=@var{n}][,name=@var{name}][,sock=@var{socketpath}] [,port=@var{n}][,group=@var{groupname}][,mode=@var{octalmode}]
Connect VLAN @var{n} to PORT @var{n} of a vde switch running on host and
listening for incoming connections on @var{socketpath}. Use GROUP @var{groupname}
and MODE @var{octalmode} to change default ownership and permissions for
communication port. This option is only available if QEMU has been compiled
with vde support enabled.

Example:
@example
# launch vde switch
vde_switch -F -sock /tmp/myswitch
# launch QEMU instance
qemu-system-i386 linux.img -net nic -net vde,sock=/tmp/myswitch
@end example

@item -netdev hubport,id=@var{id},hubid=@var{hubid}

Create a hub port on QEMU "vlan" @var{hubid}.

The hubport netdev lets you connect a NIC to a QEMU "vlan" instead of a single
netdev.  @code{-net} and @code{-device} with parameter @option{vlan} create the
required hub automatically.

@item -netdev vhost-user,chardev=@var{id}[,vhostforce=on|off]

Establish a vhost-user netdev, backed by a chardev @var{id}. The chardev should
be a unix domain socket backed one. The vhost-user uses a specifically defined
protocol to pass vhost ioctl replacement messages to an application on the other
end of the socket. On non-MSIX guests, the feature can be forced with
@var{vhostforce}.

Example:
@example
qemu -m 512 -object memory-backend-file,id=mem,size=512M,mem-path=/hugetlbfs,share=on \
     -numa node,memdev=mem \
     -chardev socket,path=/path/to/socket \
     -netdev type=vhost-user,id=net0,chardev=chr0 \
     -device virtio-net-pci,netdev=net0
@end example

@item -net dump[,vlan=@var{n}][,file=@var{file}][,len=@var{len}]
Dump network traffic on VLAN @var{n} to file @var{file} (@file{qemu-vlan0.pcap} by default).
At most @var{len} bytes (64k by default) per packet are stored. The file format is
libpcap, so it can be analyzed with tools such as tcpdump or Wireshark.

@item -net none
Indicate that no network devices should be configured. It is used to
override the default configuration (@option{-net nic -net user}) which
is activated if no @option{-net} options are provided.
ETEXI

STEXI
@end table
ETEXI
DEFHEADING()

DEFHEADING(Character device options:)
STEXI

The general form of a character device option is:
@table @option
ETEXI

DEF("chardev", HAS_ARG, QEMU_OPTION_chardev,
    "-chardev null,id=id[,mux=on|off]\n"
    "-chardev socket,id=id[,host=host],port=host[,to=to][,ipv4][,ipv6][,nodelay]\n"
    "         [,server][,nowait][,telnet][,mux=on|off] (tcp)\n"
    "-chardev socket,id=id,path=path[,server][,nowait][,telnet],[mux=on|off] (unix)\n"
    "-chardev udp,id=id[,host=host],port=port[,localaddr=localaddr]\n"
    "         [,localport=localport][,ipv4][,ipv6][,mux=on|off]\n"
    "-chardev msmouse,id=id[,mux=on|off]\n"
    "-chardev vc,id=id[[,width=width][,height=height]][[,cols=cols][,rows=rows]]\n"
    "         [,mux=on|off]\n"
    "-chardev ringbuf,id=id[,size=size]\n"
    "-chardev file,id=id,path=path[,mux=on|off]\n"
    "-chardev pipe,id=id,path=path[,mux=on|off]\n"
#ifdef _WIN32
    "-chardev console,id=id[,mux=on|off]\n"
    "-chardev serial,id=id,path=path[,mux=on|off]\n"
#else
    "-chardev pty,id=id[,mux=on|off]\n"
    "-chardev stdio,id=id[,mux=on|off][,signal=on|off]\n"
#endif
#ifdef CONFIG_BRLAPI
    "-chardev braille,id=id[,mux=on|off]\n"
#endif
#if defined(__linux__) || defined(__sun__) || defined(__FreeBSD__) \
        || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    "-chardev serial,id=id,path=path[,mux=on|off]\n"
    "-chardev tty,id=id,path=path[,mux=on|off]\n"
#endif
#if defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
    "-chardev parallel,id=id,path=path[,mux=on|off]\n"
    "-chardev parport,id=id,path=path[,mux=on|off]\n"
#endif
#if defined(CONFIG_SPICE)
    "-chardev spicevmc,id=id,name=name[,debug=debug]\n"
    "-chardev spiceport,id=id,name=name[,debug=debug]\n"
#endif
    , QEMU_ARCH_ALL
)

STEXI
@item -chardev @var{backend} ,id=@var{id} [,mux=on|off] [,@var{options}]
@findex -chardev
Backend is one of:
@option{null},
@option{socket},
@option{udp},
@option{msmouse},
@option{vc},
@option{ringbuf},
@option{file},
@option{pipe},
@option{console},
@option{serial},
@option{pty},
@option{stdio},
@option{braille},
@option{tty},
@option{parallel},
@option{parport},
@option{spicevmc}.
@option{spiceport}.
The specific backend will determine the applicable options.

All devices must have an id, which can be any string up to 127 characters long.
It is used to uniquely identify this device in other command line directives.

A character device may be used in multiplexing mode by multiple front-ends.
The key sequence of @key{Control-a} and @key{c} will rotate the input focus
between attached front-ends. Specify @option{mux=on} to enable this mode.

Options to each backend are described below.

@item -chardev null ,id=@var{id}
A void device. This device will not emit any data, and will drop any data it
receives. The null backend does not take any options.

@item -chardev socket ,id=@var{id} [@var{TCP options} or @var{unix options}] [,server] [,nowait] [,telnet]

Create a two-way stream socket, which can be either a TCP or a unix socket. A
unix socket will be created if @option{path} is specified. Behaviour is
undefined if TCP options are specified for a unix socket.

@option{server} specifies that the socket shall be a listening socket.

@option{nowait} specifies that QEMU should not block waiting for a client to
connect to a listening socket.

@option{telnet} specifies that traffic on the socket should interpret telnet
escape sequences.

TCP and unix socket options are given below:

@table @option

@item TCP options: port=@var{port} [,host=@var{host}] [,to=@var{to}] [,ipv4] [,ipv6] [,nodelay]

@option{host} for a listening socket specifies the local address to be bound.
For a connecting socket species the remote host to connect to. @option{host} is
optional for listening sockets. If not specified it defaults to @code{0.0.0.0}.

@option{port} for a listening socket specifies the local port to be bound. For a
connecting socket specifies the port on the remote host to connect to.
@option{port} can be given as either a port number or a service name.
@option{port} is required.

@option{to} is only relevant to listening sockets. If it is specified, and
@option{port} cannot be bound, QEMU will attempt to bind to subsequent ports up
to and including @option{to} until it succeeds. @option{to} must be specified
as a port number.

@option{ipv4} and @option{ipv6} specify that either IPv4 or IPv6 must be used.
If neither is specified the socket may use either protocol.

@option{nodelay} disables the Nagle algorithm.

@item unix options: path=@var{path}

@option{path} specifies the local path of the unix socket. @option{path} is
required.

@end table

@item -chardev udp ,id=@var{id} [,host=@var{host}] ,port=@var{port} [,localaddr=@var{localaddr}] [,localport=@var{localport}] [,ipv4] [,ipv6]

Sends all traffic from the guest to a remote host over UDP.

@option{host} specifies the remote host to connect to. If not specified it
defaults to @code{localhost}.

@option{port} specifies the port on the remote host to connect to. @option{port}
is required.

@option{localaddr} specifies the local address to bind to. If not specified it
defaults to @code{0.0.0.0}.

@option{localport} specifies the local port to bind to. If not specified any
available local port will be used.

@option{ipv4} and @option{ipv6} specify that either IPv4 or IPv6 must be used.
If neither is specified the device may use either protocol.

@item -chardev msmouse ,id=@var{id}

Forward QEMU's emulated msmouse events to the guest. @option{msmouse} does not
take any options.

@item -chardev vc ,id=@var{id} [[,width=@var{width}] [,height=@var{height}]] [[,cols=@var{cols}] [,rows=@var{rows}]]

Connect to a QEMU text console. @option{vc} may optionally be given a specific
size.

@option{width} and @option{height} specify the width and height respectively of
the console, in pixels.

@option{cols} and @option{rows} specify that the console be sized to fit a text
console with the given dimensions.

@item -chardev ringbuf ,id=@var{id} [,size=@var{size}]

Create a ring buffer with fixed size @option{size}.
@var{size} must be a power of two, and defaults to @code{64K}).

@item -chardev file ,id=@var{id} ,path=@var{path}

Log all traffic received from the guest to a file.

@option{path} specifies the path of the file to be opened. This file will be
created if it does not already exist, and overwritten if it does. @option{path}
is required.

@item -chardev pipe ,id=@var{id} ,path=@var{path}

Create a two-way connection to the guest. The behaviour differs slightly between
Windows hosts and other hosts:

On Windows, a single duplex pipe will be created at
@file{\\.pipe\@option{path}}.

On other hosts, 2 pipes will be created called @file{@option{path}.in} and
@file{@option{path}.out}. Data written to @file{@option{path}.in} will be
received by the guest. Data written by the guest can be read from
@file{@option{path}.out}. QEMU will not create these fifos, and requires them to
be present.

@option{path} forms part of the pipe path as described above. @option{path} is
required.

@item -chardev console ,id=@var{id}

Send traffic from the guest to QEMU's standard output. @option{console} does not
take any options.

@option{console} is only available on Windows hosts.

@item -chardev serial ,id=@var{id} ,path=@option{path}

Send traffic from the guest to a serial device on the host.

On Unix hosts serial will actually accept any tty device,
not only serial lines.

@option{path} specifies the name of the serial device to open.

@item -chardev pty ,id=@var{id}

Create a new pseudo-terminal on the host and connect to it. @option{pty} does
not take any options.

@option{pty} is not available on Windows hosts.

@item -chardev stdio ,id=@var{id} [,signal=on|off]
Connect to standard input and standard output of the QEMU process.

@option{signal} controls if signals are enabled on the terminal, that includes
exiting QEMU with the key sequence @key{Control-c}. This option is enabled by
default, use @option{signal=off} to disable it.

@option{stdio} is not available on Windows hosts.

@item -chardev braille ,id=@var{id}

Connect to a local BrlAPI server. @option{braille} does not take any options.

@item -chardev tty ,id=@var{id} ,path=@var{path}

@option{tty} is only available on Linux, Sun, FreeBSD, NetBSD, OpenBSD and
DragonFlyBSD hosts.  It is an alias for @option{serial}.

@option{path} specifies the path to the tty. @option{path} is required.

@item -chardev parallel ,id=@var{id} ,path=@var{path}
@item -chardev parport ,id=@var{id} ,path=@var{path}

@option{parallel} is only available on Linux, FreeBSD and DragonFlyBSD hosts.

Connect to a local parallel port.

@option{path} specifies the path to the parallel port device. @option{path} is
required.

@item -chardev spicevmc ,id=@var{id} ,debug=@var{debug}, name=@var{name}

@option{spicevmc} is only available when spice support is built in.

@option{debug} debug level for spicevmc

@option{name} name of spice channel to connect to

Connect to a spice virtual machine channel, such as vdiport.

@item -chardev spiceport ,id=@var{id} ,debug=@var{debug}, name=@var{name}

@option{spiceport} is only available when spice support is built in.

@option{debug} debug level for spicevmc

@option{name} name of spice port to connect to

Connect to a spice port, allowing a Spice client to handle the traffic
identified by a name (preferably a fqdn).
ETEXI

STEXI
@end table
ETEXI
DEFHEADING()

DEFHEADING(Device URL Syntax:)
STEXI

In addition to using normal file images for the emulated storage devices,
QEMU can also use networked resources such as iSCSI devices. These are
specified using a special URL syntax.

@table @option
@item iSCSI
iSCSI support allows QEMU to access iSCSI resources directly and use as
images for the guest storage. Both disk and cdrom images are supported.

Syntax for specifying iSCSI LUNs is
``iscsi://<target-ip>[:<port>]/<target-iqn>/<lun>''

By default qemu will use the iSCSI initiator-name
'iqn.2008-11.org.linux-kvm[:<name>]' but this can also be set from the command
line or a configuration file.


Example (without authentication):
@example
qemu-system-i386 -iscsi initiator-name=iqn.2001-04.com.example:my-initiator \
                 -cdrom iscsi://192.0.2.1/iqn.2001-04.com.example/2 \
                 -drive file=iscsi://192.0.2.1/iqn.2001-04.com.example/1
@end example

Example (CHAP username/password via URL):
@example
qemu-system-i386 -drive file=iscsi://user%password@@192.0.2.1/iqn.2001-04.com.example/1
@end example

Example (CHAP username/password via environment variables):
@example
LIBISCSI_CHAP_USERNAME="user" \
LIBISCSI_CHAP_PASSWORD="password" \
qemu-system-i386 -drive file=iscsi://192.0.2.1/iqn.2001-04.com.example/1
@end example

iSCSI support is an optional feature of QEMU and only available when
compiled and linked against libiscsi.
ETEXI
DEF("iscsi", HAS_ARG, QEMU_OPTION_iscsi,
    "-iscsi [user=user][,password=password]\n"
    "       [,header-digest=CRC32C|CR32C-NONE|NONE-CRC32C|NONE\n"
    "       [,initiator-name=initiator-iqn][,id=target-iqn]\n"
    "                iSCSI session parameters\n", QEMU_ARCH_ALL)
STEXI

iSCSI parameters such as username and password can also be specified via
a configuration file. See qemu-doc for more information and examples.

@item NBD
QEMU supports NBD (Network Block Devices) both using TCP protocol as well
as Unix Domain Sockets.

Syntax for specifying a NBD device using TCP
``nbd:<server-ip>:<port>[:exportname=<export>]''

Syntax for specifying a NBD device using Unix Domain Sockets
``nbd:unix:<domain-socket>[:exportname=<export>]''


Example for TCP
@example
qemu-system-i386 --drive file=nbd:192.0.2.1:30000
@end example

Example for Unix Domain Sockets
@example
qemu-system-i386 --drive file=nbd:unix:/tmp/nbd-socket
@end example

@item SSH
QEMU supports SSH (Secure Shell) access to remote disks.

Examples:
@example
qemu-system-i386 -drive file=ssh://user@@host/path/to/disk.img
qemu-system-i386 -drive file.driver=ssh,file.user=user,file.host=host,file.port=22,file.path=/path/to/disk.img
@end example

Currently authentication must be done using ssh-agent.  Other
authentication methods may be supported in future.

@item Sheepdog
Sheepdog is a distributed storage system for QEMU.
QEMU supports using either local sheepdog devices or remote networked
devices.

Syntax for specifying a sheepdog device
@example
sheepdog[+tcp|+unix]://[host:port]/vdiname[?socket=path][#snapid|#tag]
@end example

Example
@example
qemu-system-i386 --drive file=sheepdog://192.0.2.1:30000/MyVirtualMachine
@end example

See also @url{http://http://www.osrg.net/sheepdog/}.

@item GlusterFS
GlusterFS is an user space distributed file system.
QEMU supports the use of GlusterFS volumes for hosting VM disk images using
TCP, Unix Domain Sockets and RDMA transport protocols.

Syntax for specifying a VM disk image on GlusterFS volume is
@example
gluster[+transport]://[server[:port]]/volname/image[?socket=...]
@end example


Example
@example
qemu-system-x86_64 --drive file=gluster://192.0.2.1/testvol/a.img
@end example

See also @url{http://www.gluster.org}.

@item HTTP/HTTPS/FTP/FTPS/TFTP
QEMU supports read-only access to files accessed over http(s), ftp(s) and tftp.

Syntax using a single filename:
@example
<protocol>://[<username>[:<password>]@@]<host>/<path>
@end example

where:
@table @option
@item protocol
'http', 'https', 'ftp', 'ftps', or 'tftp'.

@item username
Optional username for authentication to the remote server.

@item password
Optional password for authentication to the remote server.

@item host
Address of the remote server.

@item path
Path on the remote server, including any query string.
@end table

The following options are also supported:
@table @option
@item url
The full URL when passing options to the driver explicitly.

@item readahead
The amount of data to read ahead with each range request to the remote server.
This value may optionally have the suffix 'T', 'G', 'M', 'K', 'k' or 'b'. If it
does not have a suffix, it will be assumed to be in bytes. The value must be a
multiple of 512 bytes. It defaults to 256k.

@item sslverify
Whether to verify the remote server's certificate when connecting over SSL. It
can have the value 'on' or 'off'. It defaults to 'on'.
@end table

Note that when passing options to qemu explicitly, @option{driver} is the value
of <protocol>.

Example: boot from a remote Fedora 20 live ISO image
@example
qemu-system-x86_64 --drive media=cdrom,file=http://dl.fedoraproject.org/pub/fedora/linux/releases/20/Live/x86_64/Fedora-Live-Desktop-x86_64-20-1.iso,readonly

qemu-system-x86_64 --drive media=cdrom,file.driver=http,file.url=http://dl.fedoraproject.org/pub/fedora/linux/releases/20/Live/x86_64/Fedora-Live-Desktop-x86_64-20-1.iso,readonly
@end example

Example: boot from a remote Fedora 20 cloud image using a local overlay for
writes, copy-on-read, and a readahead of 64k
@example
qemu-img create -f qcow2 -o backing_file='json:@{"file.driver":"http",, "file.url":"https://dl.fedoraproject.org/pub/fedora/linux/releases/20/Images/x86_64/Fedora-x86_64-20-20131211.1-sda.qcow2",, "file.readahead":"64k"@}' /tmp/Fedora-x86_64-20-20131211.1-sda.qcow2

qemu-system-x86_64 -drive file=/tmp/Fedora-x86_64-20-20131211.1-sda.qcow2,copy-on-read=on
@end example

Example: boot from an image stored on a VMware vSphere server with a self-signed
certificate using a local overlay for writes and a readahead of 64k
@example
qemu-img create -f qcow2 -o backing_file='json:@{"file.driver":"https",, "file.url":"https://user:password@@vsphere.example.com/folder/test/test-flat.vmdk?dcPath=Datacenter&dsName=datastore1",, "file.sslverify":"off",, "file.readahead":"64k"@}' /tmp/test.qcow2

qemu-system-x86_64 -drive file=/tmp/test.qcow2
@end example
ETEXI

STEXI
@end table
ETEXI

DEFHEADING(Bluetooth(R) options:)
STEXI
@table @option
ETEXI

DEF("bt", HAS_ARG, QEMU_OPTION_bt, \
    "-bt hci,null    dumb bluetooth HCI - doesn't respond to commands\n" \
    "-bt hci,host[:id]\n" \
    "                use host's HCI with the given name\n" \
    "-bt hci[,vlan=n]\n" \
    "                emulate a standard HCI in virtual scatternet 'n'\n" \
    "-bt vhci[,vlan=n]\n" \
    "                add host computer to virtual scatternet 'n' using VHCI\n" \
    "-bt device:dev[,vlan=n]\n" \
    "                emulate a bluetooth device 'dev' in scatternet 'n'\n",
    QEMU_ARCH_ALL)
STEXI
@item -bt hci[...]
@findex -bt
Defines the function of the corresponding Bluetooth HCI.  -bt options
are matched with the HCIs present in the chosen machine type.  For
example when emulating a machine with only one HCI built into it, only
the first @code{-bt hci[...]} option is valid and defines the HCI's
logic.  The Transport Layer is decided by the machine type.  Currently
the machines @code{n800} and @code{n810} have one HCI and all other
machines have none.

@anchor{bt-hcis}
The following three types are recognized:

@table @option
@item -bt hci,null
(default) The corresponding Bluetooth HCI assumes no internal logic
and will not respond to any HCI commands or emit events.

@item -bt hci,host[:@var{id}]
(@code{bluez} only) The corresponding HCI passes commands / events
to / from the physical HCI identified by the name @var{id} (default:
@code{hci0}) on the computer running QEMU.  Only available on @code{bluez}
capable systems like Linux.

@item -bt hci[,vlan=@var{n}]
Add a virtual, standard HCI that will participate in the Bluetooth
scatternet @var{n} (default @code{0}).  Similarly to @option{-net}
VLANs, devices inside a bluetooth network @var{n} can only communicate
with other devices in the same network (scatternet).
@end table

@item -bt vhci[,vlan=@var{n}]
(Linux-host only) Create a HCI in scatternet @var{n} (default 0) attached
to the host bluetooth stack instead of to the emulated target.  This
allows the host and target machines to participate in a common scatternet
and communicate.  Requires the Linux @code{vhci} driver installed.  Can
be used as following:

@example
qemu-system-i386 [...OPTIONS...] -bt hci,vlan=5 -bt vhci,vlan=5
@end example

@item -bt device:@var{dev}[,vlan=@var{n}]
Emulate a bluetooth device @var{dev} and place it in network @var{n}
(default @code{0}).  QEMU can only emulate one type of bluetooth devices
currently:

@table @option
@item keyboard
Virtual wireless keyboard implementing the HIDP bluetooth profile.
@end table
ETEXI

STEXI
@end table
ETEXI
DEFHEADING()

#ifdef CONFIG_TPM
DEFHEADING(TPM device options:)

DEF("tpmdev", HAS_ARG, QEMU_OPTION_tpmdev, \
    "-tpmdev passthrough,id=id[,path=path][,cancel-path=path]\n"
    "                use path to provide path to a character device; default is /dev/tpm0\n"
    "                use cancel-path to provide path to TPM's cancel sysfs entry; if\n"
    "                not provided it will be searched for in /sys/class/misc/tpm?/device\n",
    QEMU_ARCH_ALL)
STEXI

The general form of a TPM device option is:
@table @option

@item -tpmdev @var{backend} ,id=@var{id} [,@var{options}]
@findex -tpmdev
Backend type must be:
@option{passthrough}.

The specific backend type will determine the applicable options.
The @code{-tpmdev} option creates the TPM backend and requires a
@code{-device} option that specifies the TPM frontend interface model.

Options to each backend are described below.

Use 'help' to print all available TPM backend types.
@example
qemu -tpmdev help
@end example

@item -tpmdev passthrough, id=@var{id}, path=@var{path}, cancel-path=@var{cancel-path}

(Linux-host only) Enable access to the host's TPM using the passthrough
driver.

@option{path} specifies the path to the host's TPM device, i.e., on
a Linux host this would be @code{/dev/tpm0}.
@option{path} is optional and by default @code{/dev/tpm0} is used.

@option{cancel-path} specifies the path to the host TPM device's sysfs
entry allowing for cancellation of an ongoing TPM command.
@option{cancel-path} is optional and by default QEMU will search for the
sysfs entry to use.

Some notes about using the host's TPM with the passthrough driver:

The TPM device accessed by the passthrough driver must not be
used by any other application on the host.

Since the host's firmware (BIOS/UEFI) has already initialized the TPM,
the VM's firmware (BIOS/UEFI) will not be able to initialize the
TPM again and may therefore not show a TPM-specific menu that would
otherwise allow the user to configure the TPM, e.g., allow the user to
enable/disable or activate/deactivate the TPM.
Further, if TPM ownership is released from within a VM then the host's TPM
will get disabled and deactivated. To enable and activate the
TPM again afterwards, the host has to be rebooted and the user is
required to enter the firmware's menu to enable and activate the TPM.
If the TPM is left disabled and/or deactivated most TPM commands will fail.

To create a passthrough TPM use the following two options:
@example
-tpmdev passthrough,id=tpm0 -device tpm-tis,tpmdev=tpm0
@end example
Note that the @code{-tpmdev} id is @code{tpm0} and is referenced by
@code{tpmdev=tpm0} in the device option.

@end table

ETEXI

DEFHEADING()

#endif

DEFHEADING(Linux/Multiboot boot specific:)
STEXI

When using these options, you can use a given Linux or Multiboot
kernel without installing it in the disk image. It can be useful
for easier testing of various kernels.

@table @option
ETEXI

DEF("kernel", HAS_ARG, QEMU_OPTION_kernel, \
    "-kernel bzImage use 'bzImage' as kernel image\n", QEMU_ARCH_ALL)
STEXI
@item -kernel @var{bzImage}
@findex -kernel
Use @var{bzImage} as kernel image. The kernel can be either a Linux kernel
or in multiboot format.
ETEXI

DEF("append", HAS_ARG, QEMU_OPTION_append, \
    "-append cmdline use 'cmdline' as kernel command line\n", QEMU_ARCH_ALL)
STEXI
@item -append @var{cmdline}
@findex -append
Use @var{cmdline} as kernel command line
ETEXI

DEF("initrd", HAS_ARG, QEMU_OPTION_initrd, \
           "-initrd file    use 'file' as initial ram disk\n", QEMU_ARCH_ALL)
STEXI
@item -initrd @var{file}
@findex -initrd
Use @var{file} as initial ram disk.

@item -initrd "@var{file1} arg=foo,@var{file2}"

This syntax is only available with multiboot.

Use @var{file1} and @var{file2} as modules and pass arg=foo as parameter to the
first module.
ETEXI

DEF("dtb", HAS_ARG, QEMU_OPTION_dtb, \
    "-dtb    file    use 'file' as device tree image\n", QEMU_ARCH_ALL)
STEXI
@item -dtb @var{file}
@findex -dtb
Use @var{file} as a device tree binary (dtb) image and pass it to the kernel
on boot.
ETEXI

STEXI
@end table
ETEXI
DEFHEADING()

DEFHEADING(Debug/Expert options:)
STEXI
@table @option
ETEXI

DEF("serial", HAS_ARG, QEMU_OPTION_serial, \
    "-serial dev     redirect the serial port to char device 'dev'\n",
    QEMU_ARCH_ALL)
STEXI
@item -serial @var{dev}
@findex -serial
Redirect the virtual serial port to host character device
@var{dev}. The default device is @code{vc} in graphical mode and
@code{stdio} in non graphical mode.

This option can be used several times to simulate up to 4 serial
ports.

Use @code{-serial none} to disable all serial ports.

Available character devices are:
@table @option
@item vc[:@var{W}x@var{H}]
Virtual console. Optionally, a width and height can be given in pixel with
@example
vc:800x600
@end example
It is also possible to specify width or height in characters:
@example
vc:80Cx24C
@end example
@item pty
[Linux only] Pseudo TTY (a new PTY is automatically allocated)
@item none
No device is allocated.
@item null
void device
@item chardev:@var{id}
Use a named character device defined with the @code{-chardev} option.
@item /dev/XXX
[Linux only] Use host tty, e.g. @file{/dev/ttyS0}. The host serial port
parameters are set according to the emulated ones.
@item /dev/parport@var{N}
[Linux only, parallel port only] Use host parallel port
@var{N}. Currently SPP and EPP parallel port features can be used.
@item file:@var{filename}
Write output to @var{filename}. No character can be read.
@item stdio
[Unix only] standard input/output
@item pipe:@var{filename}
name pipe @var{filename}
@item COM@var{n}
[Windows only] Use host serial port @var{n}
@item udp:[@var{remote_host}]:@var{remote_port}[@@[@var{src_ip}]:@var{src_port}]
This implements UDP Net Console.
When @var{remote_host} or @var{src_ip} are not specified
they default to @code{0.0.0.0}.
When not using a specified @var{src_port} a random port is automatically chosen.

If you just want a simple readonly console you can use @code{netcat} or
@code{nc}, by starting QEMU with: @code{-serial udp::4555} and nc as:
@code{nc -u -l -p 4555}. Any time QEMU writes something to that port it
will appear in the netconsole session.

If you plan to send characters back via netconsole or you want to stop
and start QEMU a lot of times, you should have QEMU use the same
source port each time by using something like @code{-serial
udp::4555@@:4556} to QEMU. Another approach is to use a patched
version of netcat which can listen to a TCP port and send and receive
characters via udp.  If you have a patched version of netcat which
activates telnet remote echo and single char transfer, then you can
use the following options to step up a netcat redirector to allow
telnet on port 5555 to access the QEMU port.
@table @code
@item QEMU Options:
-serial udp::4555@@:4556
@item netcat options:
-u -P 4555 -L 0.0.0.0:4556 -t -p 5555 -I -T
@item telnet options:
localhost 5555
@end table

@item tcp:[@var{host}]:@var{port}[,@var{server}][,nowait][,nodelay]
The TCP Net Console has two modes of operation.  It can send the serial
I/O to a location or wait for a connection from a location.  By default
the TCP Net Console is sent to @var{host} at the @var{port}.  If you use
the @var{server} option QEMU will wait for a client socket application
to connect to the port before continuing, unless the @code{nowait}
option was specified.  The @code{nodelay} option disables the Nagle buffering
algorithm.  If @var{host} is omitted, 0.0.0.0 is assumed. Only
one TCP connection at a time is accepted. You can use @code{telnet} to
connect to the corresponding character device.
@table @code
@item Example to send tcp console to 192.168.0.2 port 4444
-serial tcp:192.168.0.2:4444
@item Example to listen and wait on port 4444 for connection
-serial tcp::4444,server
@item Example to not wait and listen on ip 192.168.0.100 port 4444
-serial tcp:192.168.0.100:4444,server,nowait
@end table

@item telnet:@var{host}:@var{port}[,server][,nowait][,nodelay]
The telnet protocol is used instead of raw tcp sockets.  The options
work the same as if you had specified @code{-serial tcp}.  The
difference is that the port acts like a telnet server or client using
telnet option negotiation.  This will also allow you to send the
MAGIC_SYSRQ sequence if you use a telnet that supports sending the break
sequence.  Typically in unix telnet you do it with Control-] and then
type "send break" followed by pressing the enter key.

@item unix:@var{path}[,server][,nowait]
A unix domain socket is used instead of a tcp socket.  The option works the
same as if you had specified @code{-serial tcp} except the unix domain socket
@var{path} is used for connections.

@item mon:@var{dev_string}
This is a special option to allow the monitor to be multiplexed onto
another serial port.  The monitor is accessed with key sequence of
@key{Control-a} and then pressing @key{c}.
@var{dev_string} should be any one of the serial devices specified
above.  An example to multiplex the monitor onto a telnet server
listening on port 4444 would be:
@table @code
@item -serial mon:telnet::4444,server,nowait
@end table
When the monitor is multiplexed to stdio in this way, Ctrl+C will not terminate
QEMU any more but will be passed to the guest instead.

@item braille
Braille device.  This will use BrlAPI to display the braille output on a real
or fake device.

@item msmouse
Three button serial mouse. Configure the guest to use Microsoft protocol.
@end table
ETEXI

DEF("parallel", HAS_ARG, QEMU_OPTION_parallel, \
    "-parallel dev   redirect the parallel port to char device 'dev'\n",
    QEMU_ARCH_ALL)
STEXI
@item -parallel @var{dev}
@findex -parallel
Redirect the virtual parallel port to host device @var{dev} (same
devices as the serial port). On Linux hosts, @file{/dev/parportN} can
be used to use hardware devices connected on the corresponding host
parallel port.

This option can be used several times to simulate up to 3 parallel
ports.

Use @code{-parallel none} to disable all parallel ports.
ETEXI

DEF("monitor", HAS_ARG, QEMU_OPTION_monitor, \
    "-monitor dev    redirect the monitor to char device 'dev'\n",
    QEMU_ARCH_ALL)
STEXI
@item -monitor @var{dev}
@findex -monitor
Redirect the monitor to host device @var{dev} (same devices as the
serial port).
The default device is @code{vc} in graphical mode and @code{stdio} in
non graphical mode.
Use @code{-monitor none} to disable the default monitor.
ETEXI
DEF("qmp", HAS_ARG, QEMU_OPTION_qmp, \
    "-qmp dev        like -monitor but opens in 'control' mode\n",
    QEMU_ARCH_ALL)
STEXI
@item -qmp @var{dev}
@findex -qmp
Like -monitor but opens in 'control' mode.
ETEXI

DEF("mon", HAS_ARG, QEMU_OPTION_mon, \
    "-mon [chardev=]name[,mode=readline|control][,default]\n", QEMU_ARCH_ALL)
STEXI
@item -mon [chardev=]name[,mode=readline|control][,default]
@findex -mon
Setup monitor on chardev @var{name}.
ETEXI

DEF("debugcon", HAS_ARG, QEMU_OPTION_debugcon, \
    "-debugcon dev   redirect the debug console to char device 'dev'\n",
    QEMU_ARCH_ALL)
STEXI
@item -debugcon @var{dev}
@findex -debugcon
Redirect the debug console to host device @var{dev} (same devices as the
serial port).  The debug console is an I/O port which is typically port
0xe9; writing to that I/O port sends output to this device.
The default device is @code{vc} in graphical mode and @code{stdio} in
non graphical mode.
ETEXI

DEF("pidfile", HAS_ARG, QEMU_OPTION_pidfile, \
    "-pidfile file   write PID to 'file'\n", QEMU_ARCH_ALL)
STEXI
@item -pidfile @var{file}
@findex -pidfile
Store the QEMU process PID in @var{file}. It is useful if you launch QEMU
from a script.
ETEXI

DEF("singlestep", 0, QEMU_OPTION_singlestep, \
    "-singlestep     always run in singlestep mode\n", QEMU_ARCH_ALL)
STEXI
@item -singlestep
@findex -singlestep
Run the emulation in single step mode.
ETEXI

DEF("S", 0, QEMU_OPTION_S, \
    "-S              freeze CPU at startup (use 'c' to start execution)\n",
    QEMU_ARCH_ALL)
STEXI
@item -S
@findex -S
Do not start CPU at startup (you must type 'c' in the monitor).
ETEXI

DEF("realtime", HAS_ARG, QEMU_OPTION_realtime,
    "-realtime [mlock=on|off]\n"
    "                run qemu with realtime features\n"
    "                mlock=on|off controls mlock support (default: on)\n",
    QEMU_ARCH_ALL)
STEXI
@item -realtime mlock=on|off
@findex -realtime
Run qemu with realtime features.
mlocking qemu and guest memory can be enabled via @option{mlock=on}
(enabled by default).
ETEXI

DEF("gdb", HAS_ARG, QEMU_OPTION_gdb, \
    "-gdb dev        wait for gdb connection on 'dev'\n", QEMU_ARCH_ALL)
STEXI
@item -gdb @var{dev}
@findex -gdb
Wait for gdb connection on device @var{dev} (@pxref{gdb_usage}). Typical
connections will likely be TCP-based, but also UDP, pseudo TTY, or even
stdio are reasonable use case. The latter is allowing to start QEMU from
within gdb and establish the connection via a pipe:
@example
(gdb) target remote | exec qemu-system-i386 -gdb stdio ...
@end example
ETEXI

DEF("s", 0, QEMU_OPTION_s, \
    "-s              shorthand for -gdb tcp::" DEFAULT_GDBSTUB_PORT "\n",
    QEMU_ARCH_ALL)
STEXI
@item -s
@findex -s
Shorthand for -gdb tcp::1234, i.e. open a gdbserver on TCP port 1234
(@pxref{gdb_usage}).
ETEXI

DEF("d", HAS_ARG, QEMU_OPTION_d, \
    "-d item1,...    enable logging of specified items (use '-d help' for a list of log items)\n",
    QEMU_ARCH_ALL)
STEXI
@item -d @var{item1}[,...]
@findex -d
Enable logging of specified items. Use '-d help' for a list of log items.
ETEXI

DEF("D", HAS_ARG, QEMU_OPTION_D, \
    "-D logfile      output log to logfile (default stderr)\n",
    QEMU_ARCH_ALL)
STEXI
@item -D @var{logfile}
@findex -D
Output log in @var{logfile} instead of to stderr
ETEXI

DEF("L", HAS_ARG, QEMU_OPTION_L, \
    "-L path         set the directory for the BIOS, VGA BIOS and keymaps\n",
    QEMU_ARCH_ALL)
STEXI
@item -L  @var{path}
@findex -L
Set the directory for the BIOS, VGA BIOS and keymaps.
ETEXI

DEF("bios", HAS_ARG, QEMU_OPTION_bios, \
    "-bios file      set the filename for the BIOS\n", QEMU_ARCH_ALL)
STEXI
@item -bios @var{file}
@findex -bios
Set the filename for the BIOS.
ETEXI

DEF("enable-kvm", 0, QEMU_OPTION_enable_kvm, \
    "-enable-kvm     enable KVM full virtualization support\n", QEMU_ARCH_ALL)
STEXI
@item -enable-kvm
@findex -enable-kvm
Enable KVM full virtualization support. This option is only available
if KVM support is enabled when compiling.
ETEXI

DEF("xen-domid", HAS_ARG, QEMU_OPTION_xen_domid,
    "-xen-domid id   specify xen guest domain id\n", QEMU_ARCH_ALL)
DEF("xen-create", 0, QEMU_OPTION_xen_create,
    "-xen-create     create domain using xen hypercalls, bypassing xend\n"
    "                warning: should not be used when xend is in use\n",
    QEMU_ARCH_ALL)
DEF("xen-attach", 0, QEMU_OPTION_xen_attach,
    "-xen-attach     attach to existing xen domain\n"
    "                xend will use this when starting QEMU\n",
    QEMU_ARCH_ALL)
STEXI
@item -xen-domid @var{id}
@findex -xen-domid
Specify xen guest domain @var{id} (XEN only).
@item -xen-create
@findex -xen-create
Create domain using xen hypercalls, bypassing xend.
Warning: should not be used when xend is in use (XEN only).
@item -xen-attach
@findex -xen-attach
Attach to existing xen domain.
xend will use this when starting QEMU (XEN only).
ETEXI

DEF("no-reboot", 0, QEMU_OPTION_no_reboot, \
    "-no-reboot      exit instead of rebooting\n", QEMU_ARCH_ALL)
STEXI
@item -no-reboot
@findex -no-reboot
Exit instead of rebooting.
ETEXI

DEF("no-shutdown", 0, QEMU_OPTION_no_shutdown, \
    "-no-shutdown    stop before shutdown\n", QEMU_ARCH_ALL)
STEXI
@item -no-shutdown
@findex -no-shutdown
Don't exit QEMU on guest shutdown, but instead only stop the emulation.
This allows for instance switching to monitor to commit changes to the
disk image.
ETEXI

DEF("loadvm", HAS_ARG, QEMU_OPTION_loadvm, \
    "-loadvm [tag|id]\n" \
    "                start right away with a saved state (loadvm in monitor)\n",
    QEMU_ARCH_ALL)
STEXI
@item -loadvm @var{file}
@findex -loadvm
Start right away with a saved state (@code{loadvm} in monitor)
ETEXI

#ifndef _WIN32
DEF("daemonize", 0, QEMU_OPTION_daemonize, \
    "-daemonize      daemonize QEMU after initializing\n", QEMU_ARCH_ALL)
#endif
STEXI
@item -daemonize
@findex -daemonize
Daemonize the QEMU process after initialization.  QEMU will not detach from
standard IO until it is ready to receive connections on any of its devices.
This option is a useful way for external programs to launch QEMU without having
to cope with initialization race conditions.
ETEXI

DEF("option-rom", HAS_ARG, QEMU_OPTION_option_rom, \
    "-option-rom rom load a file, rom, into the option ROM space\n",
    QEMU_ARCH_ALL)
STEXI
@item -option-rom @var{file}
@findex -option-rom
Load the contents of @var{file} as an option ROM.
This option is useful to load things like EtherBoot.
ETEXI

DEF("clock", HAS_ARG, QEMU_OPTION_clock, \
    "-clock          force the use of the given methods for timer alarm.\n" \
    "                To see what timers are available use '-clock help'\n",
    QEMU_ARCH_ALL)
STEXI
@item -clock @var{method}
@findex -clock
Force the use of the given methods for timer alarm. To see what timers
are available use @code{-clock help}.
ETEXI

HXCOMM Options deprecated by -rtc
DEF("localtime", 0, QEMU_OPTION_localtime, "", QEMU_ARCH_ALL)
DEF("startdate", HAS_ARG, QEMU_OPTION_startdate, "", QEMU_ARCH_ALL)

DEF("rtc", HAS_ARG, QEMU_OPTION_rtc, \
    "-rtc [base=utc|localtime|date][,clock=host|rt|vm][,driftfix=none|slew]\n" \
    "                set the RTC base and clock, enable drift fix for clock ticks (x86 only)\n",
    QEMU_ARCH_ALL)

STEXI

@item -rtc [base=utc|localtime|@var{date}][,clock=host|vm][,driftfix=none|slew]
@findex -rtc
Specify @option{base} as @code{utc} or @code{localtime} to let the RTC start at the current
UTC or local time, respectively. @code{localtime} is required for correct date in
MS-DOS or Windows. To start at a specific point in time, provide @var{date} in the
format @code{2006-06-17T16:01:21} or @code{2006-06-17}. The default base is UTC.

By default the RTC is driven by the host system time. This allows using of the
RTC as accurate reference clock inside the guest, specifically if the host
time is smoothly following an accurate external reference clock, e.g. via NTP.
If you want to isolate the guest time from the host, you can set @option{clock}
to @code{rt} instead.  To even prevent it from progressing during suspension,
you can set it to @code{vm}.

Enable @option{driftfix} (i386 targets only) if you experience time drift problems,
specifically with Windows' ACPI HAL. This option will try to figure out how
many timer interrupts were not processed by the Windows guest and will
re-inject them.
ETEXI

DEF("icount", HAS_ARG, QEMU_OPTION_icount, \
    "-icount [N|auto]\n" \
    "                enable virtual instruction counter with 2^N clock ticks per\n" \
    "                instruction\n", QEMU_ARCH_ALL)
STEXI
@item -icount [@var{N}|auto]
@findex -icount
Enable virtual instruction counter.  The virtual cpu will execute one
instruction every 2^@var{N} ns of virtual time.  If @code{auto} is specified
then the virtual cpu speed will be automatically adjusted to keep virtual
time within a few seconds of real time.

Note that while this option can give deterministic behavior, it does not
provide cycle accurate emulation.  Modern CPUs contain superscalar out of
order cores with complex cache hierarchies.  The number of instructions
executed often has little or no correlation with actual performance.
ETEXI

DEF("watchdog", HAS_ARG, QEMU_OPTION_watchdog, \
    "-watchdog i6300esb|ib700\n" \
    "                enable virtual hardware watchdog [default=none]\n",
    QEMU_ARCH_ALL)
STEXI
@item -watchdog @var{model}
@findex -watchdog
Create a virtual hardware watchdog device.  Once enabled (by a guest
action), the watchdog must be periodically polled by an agent inside
the guest or else the guest will be restarted.

The @var{model} is the model of hardware watchdog to emulate.  Choices
for model are: @code{ib700} (iBASE 700) which is a very simple ISA
watchdog with a single timer, or @code{i6300esb} (Intel 6300ESB I/O
controller hub) which is a much more featureful PCI-based dual-timer
watchdog.  Choose a model for which your guest has drivers.

Use @code{-watchdog help} to list available hardware models.  Only one
watchdog can be enabled for a guest.
ETEXI

DEF("watchdog-action", HAS_ARG, QEMU_OPTION_watchdog_action, \
    "-watchdog-action reset|shutdown|poweroff|pause|debug|none\n" \
    "                action when watchdog fires [default=reset]\n",
    QEMU_ARCH_ALL)
STEXI
@item -watchdog-action @var{action}
@findex -watchdog-action

The @var{action} controls what QEMU will do when the watchdog timer
expires.
The default is
@code{reset} (forcefully reset the guest).
Other possible actions are:
@code{shutdown} (attempt to gracefully shutdown the guest),
@code{poweroff} (forcefully poweroff the guest),
@code{pause} (pause the guest),
@code{debug} (print a debug message and continue), or
@code{none} (do nothing).

Note that the @code{shutdown} action requires that the guest responds
to ACPI signals, which it may not be able to do in the sort of
situations where the watchdog would have expired, and thus
@code{-watchdog-action shutdown} is not recommended for production use.

Examples:

@table @code
@item -watchdog i6300esb -watchdog-action pause
@item -watchdog ib700
@end table
ETEXI

DEF("echr", HAS_ARG, QEMU_OPTION_echr, \
    "-echr chr       set terminal escape character instead of ctrl-a\n",
    QEMU_ARCH_ALL)
STEXI

@item -echr @var{numeric_ascii_value}
@findex -echr
Change the escape character used for switching to the monitor when using
monitor and serial sharing.  The default is @code{0x01} when using the
@code{-nographic} option.  @code{0x01} is equal to pressing
@code{Control-a}.  You can select a different character from the ascii
control keys where 1 through 26 map to Control-a through Control-z.  For
instance you could use the either of the following to change the escape
character to Control-t.
@table @code
@item -echr 0x14
@item -echr 20
@end table
ETEXI

DEF("virtioconsole", HAS_ARG, QEMU_OPTION_virtiocon, \
    "-virtioconsole c\n" \
    "                set virtio console\n", QEMU_ARCH_ALL)
STEXI
@item -virtioconsole @var{c}
@findex -virtioconsole
Set virtio console.

This option is maintained for backward compatibility.

Please use @code{-device virtconsole} for the new way of invocation.
ETEXI

DEF("show-cursor", 0, QEMU_OPTION_show_cursor, \
    "-show-cursor    show cursor\n", QEMU_ARCH_ALL)
STEXI
@item -show-cursor
@findex -show-cursor
Show cursor.
ETEXI

DEF("tb-size", HAS_ARG, QEMU_OPTION_tb_size, \
    "-tb-size n      set TB size\n", QEMU_ARCH_ALL)
STEXI
@item -tb-size @var{n}
@findex -tb-size
Set TB size.
ETEXI

DEF("incoming", HAS_ARG, QEMU_OPTION_incoming, \
    "-incoming p     prepare for incoming migration, listen on port p\n",
    QEMU_ARCH_ALL)
STEXI
@item -incoming @var{port}
@findex -incoming
Prepare for incoming migration, listen on @var{port}.
ETEXI

DEF("nodefaults", 0, QEMU_OPTION_nodefaults, \
    "-nodefaults     don't create default devices\n", QEMU_ARCH_ALL)
STEXI
@item -nodefaults
@findex -nodefaults
Don't create default devices. Normally, QEMU sets the default devices like serial
port, parallel port, virtual console, monitor device, VGA adapter, floppy and
CD-ROM drive and others. The @code{-nodefaults} option will disable all those
default devices.
ETEXI

#ifndef _WIN32
DEF("chroot", HAS_ARG, QEMU_OPTION_chroot, \
    "-chroot dir     chroot to dir just before starting the VM\n",
    QEMU_ARCH_ALL)
#endif
STEXI
@item -chroot @var{dir}
@findex -chroot
Immediately before starting guest execution, chroot to the specified
directory.  Especially useful in combination with -runas.
ETEXI

#ifndef _WIN32
DEF("runas", HAS_ARG, QEMU_OPTION_runas, \
    "-runas user     change to user id user just before starting the VM\n",
    QEMU_ARCH_ALL)
#endif
STEXI
@item -runas @var{user}
@findex -runas
Immediately before starting guest execution, drop root privileges, switching
to the specified user.
ETEXI

DEF("prom-env", HAS_ARG, QEMU_OPTION_prom_env,
    "-prom-env variable=value\n"
    "                set OpenBIOS nvram variables\n",
    QEMU_ARCH_PPC | QEMU_ARCH_SPARC)
STEXI
@item -prom-env @var{variable}=@var{value}
@findex -prom-env
Set OpenBIOS nvram @var{variable} to given @var{value} (PPC, SPARC only).
ETEXI
DEF("semihosting", 0, QEMU_OPTION_semihosting,
    "-semihosting    semihosting mode\n",
    QEMU_ARCH_ARM | QEMU_ARCH_M68K | QEMU_ARCH_XTENSA | QEMU_ARCH_LM32)
STEXI
@item -semihosting
@findex -semihosting
Semihosting mode (ARM, M68K, Xtensa only).
ETEXI
DEF("old-param", 0, QEMU_OPTION_old_param,
    "-old-param      old param mode\n", QEMU_ARCH_ARM)
STEXI
@item -old-param
@findex -old-param (ARM)
Old param mode (ARM only).
ETEXI

DEF("sandbox", HAS_ARG, QEMU_OPTION_sandbox, \
    "-sandbox <arg>  Enable seccomp mode 2 system call filter (default 'off').\n",
    QEMU_ARCH_ALL)
STEXI
@item -sandbox @var{arg}
@findex -sandbox
Enable Seccomp mode 2 system call filter. 'on' will enable syscall filtering and 'off' will
disable it.  The default is 'off'.
ETEXI

DEF("readconfig", HAS_ARG, QEMU_OPTION_readconfig,
    "-readconfig <file>\n", QEMU_ARCH_ALL)
STEXI
@item -readconfig @var{file}
@findex -readconfig
Read device configuration from @var{file}. This approach is useful when you want to spawn
QEMU process with many command line options but you don't want to exceed the command line
character limit.
ETEXI
DEF("writeconfig", HAS_ARG, QEMU_OPTION_writeconfig,
    "-writeconfig <file>\n"
    "                read/write config file\n", QEMU_ARCH_ALL)
STEXI
@item -writeconfig @var{file}
@findex -writeconfig
Write device configuration to @var{file}. The @var{file} can be either filename to save
command line and device configuration into file or dash @code{-}) character to print the
output to stdout. This can be later used as input file for @code{-readconfig} option.
ETEXI
DEF("nodefconfig", 0, QEMU_OPTION_nodefconfig,
    "-nodefconfig\n"
    "                do not load default config files at startup\n",
    QEMU_ARCH_ALL)
STEXI
@item -nodefconfig
@findex -nodefconfig
Normally QEMU loads configuration files from @var{sysconfdir} and @var{datadir} at startup.
The @code{-nodefconfig} option will prevent QEMU from loading any of those config files.
ETEXI
DEF("no-user-config", 0, QEMU_OPTION_nouserconfig,
    "-no-user-config\n"
    "                do not load user-provided config files at startup\n",
    QEMU_ARCH_ALL)
STEXI
@item -no-user-config
@findex -no-user-config
The @code{-no-user-config} option makes QEMU not load any of the user-provided
config files on @var{sysconfdir}, but won't make it skip the QEMU-provided config
files from @var{datadir}.
ETEXI
DEF("trace", HAS_ARG, QEMU_OPTION_trace,
    "-trace [events=<file>][,file=<file>]\n"
    "                specify tracing options\n",
    QEMU_ARCH_ALL)
STEXI
HXCOMM This line is not accurate, as some sub-options are backend-specific but
HXCOMM HX does not support conditional compilation of text.
@item -trace [events=@var{file}][,file=@var{file}]
@findex -trace

Specify tracing options.

@table @option
@item events=@var{file}
Immediately enable events listed in @var{file}.
The file must contain one event name (as listed in the @var{trace-events} file)
per line.
This option is only available if QEMU has been compiled with
either @var{simple} or @var{stderr} tracing backend.
@item file=@var{file}
Log output traces to @var{file}.

This option is only available if QEMU has been compiled with
the @var{simple} tracing backend.
@end table
ETEXI

HXCOMM Internal use
DEF("qtest", HAS_ARG, QEMU_OPTION_qtest, "", QEMU_ARCH_ALL)
DEF("qtest-log", HAS_ARG, QEMU_OPTION_qtest_log, "", QEMU_ARCH_ALL)

#ifdef __linux__
DEF("enable-fips", 0, QEMU_OPTION_enablefips,
    "-enable-fips    enable FIPS 140-2 compliance\n",
    QEMU_ARCH_ALL)
#endif
STEXI
@item -enable-fips
@findex -enable-fips
Enable FIPS 140-2 compliance mode.
ETEXI

HXCOMM Deprecated by -machine accel=tcg property
DEF("no-kvm", 0, QEMU_OPTION_no_kvm, "", QEMU_ARCH_I386)

HXCOMM Deprecated by kvm-pit driver properties
DEF("no-kvm-pit-reinjection", 0, QEMU_OPTION_no_kvm_pit_reinjection,
    "", QEMU_ARCH_I386)

HXCOMM Deprecated (ignored)
DEF("no-kvm-pit", 0, QEMU_OPTION_no_kvm_pit, "", QEMU_ARCH_I386)

HXCOMM Deprecated by -machine kernel_irqchip=on|off property
DEF("no-kvm-irqchip", 0, QEMU_OPTION_no_kvm_irqchip, "", QEMU_ARCH_I386)

HXCOMM Deprecated (ignored)
DEF("tdf", 0, QEMU_OPTION_tdf,"", QEMU_ARCH_ALL)

DEF("object", HAS_ARG, QEMU_OPTION_object,
    "-object TYPENAME[,PROP1=VALUE1,...]\n"
    "                create an new object of type TYPENAME setting properties\n"
    "                in the order they are specified.  Note that the 'id'\n"
    "                property must be set.  These objects are placed in the\n"
    "                '/objects' path.\n",
    QEMU_ARCH_ALL)
STEXI
@item -object @var{typename}[,@var{prop1}=@var{value1},...]
@findex -object
Create an new object of type @var{typename} setting properties
in the order they are specified.  Note that the 'id'
property must be set.  These objects are placed in the
'/objects' path.
ETEXI

DEF("msg", HAS_ARG, QEMU_OPTION_msg,
    "-msg timestamp[=on|off]\n"
    "                change the format of messages\n"
    "                on|off controls leading timestamps (default:on)\n",
    QEMU_ARCH_ALL)
STEXI
@item -msg timestamp[=on|off]
@findex -msg
prepend a timestamp to each log message.(default:on)
ETEXI

HXCOMM This is the last statement. Insert new options before this line!
STEXI
@end table
ETEXI

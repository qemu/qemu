HXCOMM Use DEFHEADING() to define headings in both help text and texi
HXCOMM Text between STEXI and ETEXI are copied to texi version and
HXCOMM discarded from C version
HXCOMM DEF(command, args, callback, arg_string, help) is used to construct
HXCOMM monitor commands
HXCOMM HXCOMM can be used for comments, discarded from both texi and C

STEXI
@table @option
ETEXI

    {
        .name       = "help|?",
        .args_type  = "name:s?",
        .params     = "[cmd]",
        .help       = "show the help",
        .mhandler.cmd = do_help_cmd,
    },

STEXI
@item help or ? [@var{cmd}]
@findex help
Show the help for all commands or just for command @var{cmd}.
ETEXI

    {
        .name       = "commit",
        .args_type  = "device:B",
        .params     = "device|all",
        .help       = "commit changes to the disk images (if -snapshot is used) or backing files",
        .mhandler.cmd = do_commit,
    },

STEXI
@item commit
@findex commit
Commit changes to the disk images (if -snapshot is used) or backing files.
ETEXI

    {
        .name       = "q|quit",
        .args_type  = "",
        .params     = "",
        .help       = "quit the emulator",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_quit,
    },

STEXI
@item q or quit
@findex quit
Quit the emulator.
ETEXI

    {
        .name       = "block_resize",
        .args_type  = "device:B,size:o",
        .params     = "device size",
        .help       = "resize a block image",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_block_resize,
    },

STEXI
@item block_resize
@findex block_resize
Resize a block image while a guest is running.  Usually requires guest
action to see the updated size.  Resize to a lower size is supported,
but should be used with extreme caution.  Note that this command only
resizes image files, it can not resize block devices like LVM volumes.
ETEXI


    {
        .name       = "eject",
        .args_type  = "force:-f,device:B",
        .params     = "[-f] device",
        .help       = "eject a removable medium (use -f to force it)",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_eject,
    },

STEXI
@item eject [-f] @var{device}
@findex eject
Eject a removable medium (use -f to force it).
ETEXI

    {
        .name       = "drive_del",
        .args_type  = "id:s",
        .params     = "device",
        .help       = "remove host block device",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_drive_del,
    },

STEXI
@item drive_del @var{device}
@findex drive_del
Remove host block device.  The result is that guest generated IO is no longer
submitted against the host device underlying the disk.  Once a drive has
been deleted, the QEMU Block layer returns -EIO which results in IO
errors in the guest for applications that are reading/writing to the device.
ETEXI

    {
        .name       = "change",
        .args_type  = "device:B,target:F,arg:s?",
        .params     = "device filename [format]",
        .help       = "change a removable medium, optional format",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_change,
    },

STEXI
@item change @var{device} @var{setting}
@findex change

Change the configuration of a device.

@table @option
@item change @var{diskdevice} @var{filename} [@var{format}]
Change the medium for a removable disk device to point to @var{filename}. eg

@example
(qemu) change ide1-cd0 /path/to/some.iso
@end example

@var{format} is optional.

@item change vnc @var{display},@var{options}
Change the configuration of the VNC server. The valid syntax for @var{display}
and @var{options} are described at @ref{sec_invocation}. eg

@example
(qemu) change vnc localhost:1
@end example

@item change vnc password [@var{password}]

Change the password associated with the VNC server. If the new password is not
supplied, the monitor will prompt for it to be entered. VNC passwords are only
significant up to 8 letters. eg

@example
(qemu) change vnc password
Password: ********
@end example

@end table
ETEXI

    {
        .name       = "screendump",
        .args_type  = "filename:F",
        .params     = "filename",
        .help       = "save screen into PPM image 'filename'",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_screen_dump,
    },

STEXI
@item screendump @var{filename}
@findex screendump
Save screen into PPM image @var{filename}.
ETEXI

    {
        .name       = "logfile",
        .args_type  = "filename:F",
        .params     = "filename",
        .help       = "output logs to 'filename'",
        .mhandler.cmd = do_logfile,
    },

STEXI
@item logfile @var{filename}
@findex logfile
Output logs to @var{filename}.
ETEXI

    {
        .name       = "trace-event",
        .args_type  = "name:s,option:b",
        .params     = "name on|off",
        .help       = "changes status of a specific trace event",
        .mhandler.cmd = do_trace_event_set_state,
    },

STEXI
@item trace-event
@findex trace-event
changes status of a trace event
ETEXI

#if defined(CONFIG_SIMPLE_TRACE)
    {
        .name       = "trace-file",
        .args_type  = "op:s?,arg:F?",
        .params     = "on|off|flush|set [arg]",
        .help       = "open, close, or flush trace file, or set a new file name",
        .mhandler.cmd = do_trace_file,
    },

STEXI
@item trace-file on|off|flush
@findex trace-file
Open, close, or flush the trace file.  If no argument is given, the status of the trace file is displayed.
ETEXI
#endif

    {
        .name       = "log",
        .args_type  = "items:s",
        .params     = "item1[,...]",
        .help       = "activate logging of the specified items to '/tmp/qemu.log'",
        .mhandler.cmd = do_log,
    },

STEXI
@item log @var{item1}[,...]
@findex log
Activate logging of the specified items to @file{/tmp/qemu.log}.
ETEXI

    {
        .name       = "savevm",
        .args_type  = "name:s?",
        .params     = "[tag|id]",
        .help       = "save a VM snapshot. If no tag or id are provided, a new snapshot is created",
        .mhandler.cmd = do_savevm,
    },

STEXI
@item savevm [@var{tag}|@var{id}]
@findex savevm
Create a snapshot of the whole virtual machine. If @var{tag} is
provided, it is used as human readable identifier. If there is already
a snapshot with the same tag or ID, it is replaced. More info at
@ref{vm_snapshots}.
ETEXI

    {
        .name       = "loadvm",
        .args_type  = "name:s",
        .params     = "tag|id",
        .help       = "restore a VM snapshot from its tag or id",
        .mhandler.cmd = do_loadvm,
    },

STEXI
@item loadvm @var{tag}|@var{id}
@findex loadvm
Set the whole virtual machine to the snapshot identified by the tag
@var{tag} or the unique snapshot ID @var{id}.
ETEXI

    {
        .name       = "delvm",
        .args_type  = "name:s",
        .params     = "tag|id",
        .help       = "delete a VM snapshot from its tag or id",
        .mhandler.cmd = do_delvm,
    },

STEXI
@item delvm @var{tag}|@var{id}
@findex delvm
Delete the snapshot identified by @var{tag} or @var{id}.
ETEXI

    {
        .name       = "singlestep",
        .args_type  = "option:s?",
        .params     = "[on|off]",
        .help       = "run emulation in singlestep mode or switch to normal mode",
        .mhandler.cmd = do_singlestep,
    },

STEXI
@item singlestep [off]
@findex singlestep
Run the emulation in single step mode.
If called with option off, the emulation returns to normal mode.
ETEXI

    {
        .name       = "stop",
        .args_type  = "",
        .params     = "",
        .help       = "stop emulation",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_stop,
    },

STEXI
@item stop
@findex stop
Stop emulation.
ETEXI

    {
        .name       = "c|cont",
        .args_type  = "",
        .params     = "",
        .help       = "resume emulation",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_cont,
    },

STEXI
@item c or cont
@findex cont
Resume emulation.
ETEXI

    {
        .name       = "gdbserver",
        .args_type  = "device:s?",
        .params     = "[device]",
        .help       = "start gdbserver on given device (default 'tcp::1234'), stop with 'none'",
        .mhandler.cmd = do_gdbserver,
    },

STEXI
@item gdbserver [@var{port}]
@findex gdbserver
Start gdbserver session (default @var{port}=1234)
ETEXI

    {
        .name       = "x",
        .args_type  = "fmt:/,addr:l",
        .params     = "/fmt addr",
        .help       = "virtual memory dump starting at 'addr'",
        .mhandler.cmd = do_memory_dump,
    },

STEXI
@item x/fmt @var{addr}
@findex x
Virtual memory dump starting at @var{addr}.
ETEXI

    {
        .name       = "xp",
        .args_type  = "fmt:/,addr:l",
        .params     = "/fmt addr",
        .help       = "physical memory dump starting at 'addr'",
        .mhandler.cmd = do_physical_memory_dump,
    },

STEXI
@item xp /@var{fmt} @var{addr}
@findex xp
Physical memory dump starting at @var{addr}.

@var{fmt} is a format which tells the command how to format the
data. Its syntax is: @option{/@{count@}@{format@}@{size@}}

@table @var
@item count
is the number of items to be dumped.

@item format
can be x (hex), d (signed decimal), u (unsigned decimal), o (octal),
c (char) or i (asm instruction).

@item size
can be b (8 bits), h (16 bits), w (32 bits) or g (64 bits). On x86,
@code{h} or @code{w} can be specified with the @code{i} format to
respectively select 16 or 32 bit code instruction size.

@end table

Examples:
@itemize
@item
Dump 10 instructions at the current instruction pointer:
@example
(qemu) x/10i $eip
0x90107063:  ret
0x90107064:  sti
0x90107065:  lea    0x0(%esi,1),%esi
0x90107069:  lea    0x0(%edi,1),%edi
0x90107070:  ret
0x90107071:  jmp    0x90107080
0x90107073:  nop
0x90107074:  nop
0x90107075:  nop
0x90107076:  nop
@end example

@item
Dump 80 16 bit values at the start of the video memory.
@smallexample
(qemu) xp/80hx 0xb8000
0x000b8000: 0x0b50 0x0b6c 0x0b65 0x0b78 0x0b38 0x0b36 0x0b2f 0x0b42
0x000b8010: 0x0b6f 0x0b63 0x0b68 0x0b73 0x0b20 0x0b56 0x0b47 0x0b41
0x000b8020: 0x0b42 0x0b69 0x0b6f 0x0b73 0x0b20 0x0b63 0x0b75 0x0b72
0x000b8030: 0x0b72 0x0b65 0x0b6e 0x0b74 0x0b2d 0x0b63 0x0b76 0x0b73
0x000b8040: 0x0b20 0x0b30 0x0b35 0x0b20 0x0b4e 0x0b6f 0x0b76 0x0b20
0x000b8050: 0x0b32 0x0b30 0x0b30 0x0b33 0x0720 0x0720 0x0720 0x0720
0x000b8060: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
0x000b8070: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
0x000b8080: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
0x000b8090: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
@end smallexample
@end itemize
ETEXI

    {
        .name       = "p|print",
        .args_type  = "fmt:/,val:l",
        .params     = "/fmt expr",
        .help       = "print expression value (use $reg for CPU register access)",
        .mhandler.cmd = do_print,
    },

STEXI
@item p or print/@var{fmt} @var{expr}
@findex print

Print expression value. Only the @var{format} part of @var{fmt} is
used.
ETEXI

    {
        .name       = "i",
        .args_type  = "fmt:/,addr:i,index:i.",
        .params     = "/fmt addr",
        .help       = "I/O port read",
        .mhandler.cmd = do_ioport_read,
    },

STEXI
Read I/O port.
ETEXI

    {
        .name       = "o",
        .args_type  = "fmt:/,addr:i,val:i",
        .params     = "/fmt addr value",
        .help       = "I/O port write",
        .mhandler.cmd = do_ioport_write,
    },

STEXI
Write to I/O port.
ETEXI

    {
        .name       = "sendkey",
        .args_type  = "string:s,hold_time:i?",
        .params     = "keys [hold_ms]",
        .help       = "send keys to the VM (e.g. 'sendkey ctrl-alt-f1', default hold time=100 ms)",
        .mhandler.cmd = do_sendkey,
    },

STEXI
@item sendkey @var{keys}
@findex sendkey

Send @var{keys} to the emulator. @var{keys} could be the name of the
key or @code{#} followed by the raw value in either decimal or hexadecimal
format. Use @code{-} to press several keys simultaneously. Example:
@example
sendkey ctrl-alt-f1
@end example

This command is useful to send keys that your graphical user interface
intercepts at low level, such as @code{ctrl-alt-f1} in X Window.
ETEXI

    {
        .name       = "system_reset",
        .args_type  = "",
        .params     = "",
        .help       = "reset the system",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_system_reset,
    },

STEXI
@item system_reset
@findex system_reset

Reset the system.
ETEXI

    {
        .name       = "system_powerdown",
        .args_type  = "",
        .params     = "",
        .help       = "send system power down event",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_system_powerdown,
    },

STEXI
@item system_powerdown
@findex system_powerdown

Power down the system (if supported).
ETEXI

    {
        .name       = "sum",
        .args_type  = "start:i,size:i",
        .params     = "addr size",
        .help       = "compute the checksum of a memory region",
        .mhandler.cmd = do_sum,
    },

STEXI
@item sum @var{addr} @var{size}
@findex sum

Compute the checksum of a memory region.
ETEXI

    {
        .name       = "usb_add",
        .args_type  = "devname:s",
        .params     = "device",
        .help       = "add USB device (e.g. 'host:bus.addr' or 'host:vendor_id:product_id')",
        .mhandler.cmd = do_usb_add,
    },

STEXI
@item usb_add @var{devname}
@findex usb_add

Add the USB device @var{devname}.  For details of available devices see
@ref{usb_devices}
ETEXI

    {
        .name       = "usb_del",
        .args_type  = "devname:s",
        .params     = "device",
        .help       = "remove USB device 'bus.addr'",
        .mhandler.cmd = do_usb_del,
    },

STEXI
@item usb_del @var{devname}
@findex usb_del

Remove the USB device @var{devname} from the QEMU virtual USB
hub. @var{devname} has the syntax @code{bus.addr}. Use the monitor
command @code{info usb} to see the devices you can remove.
ETEXI

    {
        .name       = "device_add",
        .args_type  = "device:O",
        .params     = "driver[,prop=value][,...]",
        .help       = "add device, like -device on the command line",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_device_add,
    },

STEXI
@item device_add @var{config}
@findex device_add

Add device.
ETEXI

    {
        .name       = "device_del",
        .args_type  = "id:s",
        .params     = "device",
        .help       = "remove device",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_device_del,
    },

STEXI
@item device_del @var{id}
@findex device_del

Remove device @var{id}.
ETEXI

    {
        .name       = "cpu",
        .args_type  = "index:i",
        .params     = "index",
        .help       = "set the default CPU",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_cpu_set,
    },

STEXI
@item cpu @var{index}
@findex cpu
Set the default CPU.
ETEXI

    {
        .name       = "mouse_move",
        .args_type  = "dx_str:s,dy_str:s,dz_str:s?",
        .params     = "dx dy [dz]",
        .help       = "send mouse move events",
        .mhandler.cmd = do_mouse_move,
    },

STEXI
@item mouse_move @var{dx} @var{dy} [@var{dz}]
@findex mouse_move
Move the active mouse to the specified coordinates @var{dx} @var{dy}
with optional scroll axis @var{dz}.
ETEXI

    {
        .name       = "mouse_button",
        .args_type  = "button_state:i",
        .params     = "state",
        .help       = "change mouse button state (1=L, 2=M, 4=R)",
        .mhandler.cmd = do_mouse_button,
    },

STEXI
@item mouse_button @var{val}
@findex mouse_button
Change the active mouse button state @var{val} (1=L, 2=M, 4=R).
ETEXI

    {
        .name       = "mouse_set",
        .args_type  = "index:i",
        .params     = "index",
        .help       = "set which mouse device receives events",
        .mhandler.cmd = do_mouse_set,
    },

STEXI
@item mouse_set @var{index}
@findex mouse_set
Set which mouse device receives events at given @var{index}, index
can be obtained with
@example
info mice
@end example
ETEXI

#ifdef HAS_AUDIO
    {
        .name       = "wavcapture",
        .args_type  = "path:F,freq:i?,bits:i?,nchannels:i?",
        .params     = "path [frequency [bits [channels]]]",
        .help       = "capture audio to a wave file (default frequency=44100 bits=16 channels=2)",
        .mhandler.cmd = do_wav_capture,
    },
#endif
STEXI
@item wavcapture @var{filename} [@var{frequency} [@var{bits} [@var{channels}]]]
@findex wavcapture
Capture audio into @var{filename}. Using sample rate @var{frequency}
bits per sample @var{bits} and number of channels @var{channels}.

Defaults:
@itemize @minus
@item Sample rate = 44100 Hz - CD quality
@item Bits = 16
@item Number of channels = 2 - Stereo
@end itemize
ETEXI

#ifdef HAS_AUDIO
    {
        .name       = "stopcapture",
        .args_type  = "n:i",
        .params     = "capture index",
        .help       = "stop capture",
        .mhandler.cmd = do_stop_capture,
    },
#endif
STEXI
@item stopcapture @var{index}
@findex stopcapture
Stop capture with a given @var{index}, index can be obtained with
@example
info capture
@end example
ETEXI

    {
        .name       = "memsave",
        .args_type  = "val:l,size:i,filename:s",
        .params     = "addr size file",
        .help       = "save to disk virtual memory dump starting at 'addr' of size 'size'",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_memory_save,
    },

STEXI
@item memsave @var{addr} @var{size} @var{file}
@findex memsave
save to disk virtual memory dump starting at @var{addr} of size @var{size}.
ETEXI

    {
        .name       = "pmemsave",
        .args_type  = "val:l,size:i,filename:s",
        .params     = "addr size file",
        .help       = "save to disk physical memory dump starting at 'addr' of size 'size'",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_physical_memory_save,
    },

STEXI
@item pmemsave @var{addr} @var{size} @var{file}
@findex pmemsave
save to disk physical memory dump starting at @var{addr} of size @var{size}.
ETEXI

    {
        .name       = "boot_set",
        .args_type  = "bootdevice:s",
        .params     = "bootdevice",
        .help       = "define new values for the boot device list",
        .mhandler.cmd = do_boot_set,
    },

STEXI
@item boot_set @var{bootdevicelist}
@findex boot_set

Define new values for the boot device list. Those values will override
the values specified on the command line through the @code{-boot} option.

The values that can be specified here depend on the machine type, but are
the same that can be specified in the @code{-boot} command line option.
ETEXI

#if defined(TARGET_I386)
    {
        .name       = "nmi",
        .args_type  = "",
        .params     = "",
        .help       = "inject an NMI on all guest's CPUs",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_inject_nmi,
    },
#endif
STEXI
@item nmi @var{cpu}
@findex nmi
Inject an NMI on the given CPU (x86 only).
ETEXI

    {
        .name       = "migrate",
        .args_type  = "detach:-d,blk:-b,inc:-i,uri:s",
        .params     = "[-d] [-b] [-i] uri",
        .help       = "migrate to URI (using -d to not wait for completion)"
		      "\n\t\t\t -b for migration without shared storage with"
		      " full copy of disk\n\t\t\t -i for migration without "
		      "shared storage with incremental copy of disk "
		      "(base image shared between src and destination)",
        .user_print = monitor_user_noop,	
	.mhandler.cmd_new = do_migrate,
    },


STEXI
@item migrate [-d] [-b] [-i] @var{uri}
@findex migrate
Migrate to @var{uri} (using -d to not wait for completion).
	-b for migration with full copy of disk
	-i for migration with incremental copy of disk (base image is shared)
ETEXI

    {
        .name       = "migrate_cancel",
        .args_type  = "",
        .params     = "",
        .help       = "cancel the current VM migration",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_migrate_cancel,
    },

STEXI
@item migrate_cancel
@findex migrate_cancel
Cancel the current VM migration.
ETEXI

    {
        .name       = "migrate_set_speed",
        .args_type  = "value:o",
        .params     = "value",
        .help       = "set maximum speed (in bytes) for migrations. "
	"Defaults to MB if no size suffix is specified, ie. B/K/M/G/T",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_migrate_set_speed,
    },

STEXI
@item migrate_set_speed @var{value}
@findex migrate_set_speed
Set maximum speed to @var{value} (in bytes) for migrations.
ETEXI

    {
        .name       = "migrate_set_downtime",
        .args_type  = "value:T",
        .params     = "value",
        .help       = "set maximum tolerated downtime (in seconds) for migrations",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_migrate_set_downtime,
    },

STEXI
@item migrate_set_downtime @var{second}
@findex migrate_set_downtime
Set maximum tolerated downtime (in seconds) for migration.
ETEXI

    {
        .name       = "client_migrate_info",
        .args_type  = "protocol:s,hostname:s,port:i?,tls-port:i?,cert-subject:s?",
        .params     = "protocol hostname port tls-port cert-subject",
        .help       = "send migration info to spice/vnc client",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = client_migrate_info,
    },

STEXI
@item client_migrate_info @var{protocol} @var{hostname} @var{port} @var{tls-port} @var{cert-subject}
@findex client_migrate_info
Set the spice/vnc connection info for the migration target.  The spice/vnc
server will ask the spice/vnc client to automatically reconnect using the
new parameters (if specified) once the vm migration finished successfully.
ETEXI

    {
        .name       = "snapshot_blkdev",
        .args_type  = "device:B,snapshot-file:s?,format:s?",
        .params     = "device [new-image-file] [format]",
        .help       = "initiates a live snapshot\n\t\t\t"
                      "of device. If a new image file is specified, the\n\t\t\t"
                      "new image file will become the new root image.\n\t\t\t"
                      "If format is specified, the snapshot file will\n\t\t\t"
                      "be created in that format. Otherwise the\n\t\t\t"
                      "snapshot will be internal! (currently unsupported)",
        .mhandler.cmd_new = do_snapshot_blkdev,
    },

STEXI
@item snapshot_blkdev
@findex snapshot_blkdev
Snapshot device, using snapshot file as target if provided
ETEXI

#if defined(TARGET_I386)
    {
        .name       = "drive_add",
        .args_type  = "pci_addr:s,opts:s",
        .params     = "[[<domain>:]<bus>:]<slot>\n"
                      "[file=file][,if=type][,bus=n]\n"
                      "[,unit=m][,media=d][index=i]\n"
                      "[,cyls=c,heads=h,secs=s[,trans=t]]\n"
                      "[snapshot=on|off][,cache=on|off]",
        .help       = "add drive to PCI storage controller",
        .mhandler.cmd = drive_hot_add,
    },
#endif

STEXI
@item drive_add
@findex drive_add
Add drive to PCI storage controller.
ETEXI

#if defined(TARGET_I386)
    {
        .name       = "pci_add",
        .args_type  = "pci_addr:s,type:s,opts:s?",
        .params     = "auto|[[<domain>:]<bus>:]<slot> nic|storage [[vlan=n][,macaddr=addr][,model=type]] [file=file][,if=type][,bus=nr]...",
        .help       = "hot-add PCI device",
        .mhandler.cmd = pci_device_hot_add,
    },
#endif

STEXI
@item pci_add
@findex pci_add
Hot-add PCI device.
ETEXI

#if defined(TARGET_I386)
    {
        .name       = "pci_del",
        .args_type  = "pci_addr:s",
        .params     = "[[<domain>:]<bus>:]<slot>",
        .help       = "hot remove PCI device",
        .mhandler.cmd = do_pci_device_hot_remove,
    },
#endif

STEXI
@item pci_del
@findex pci_del
Hot remove PCI device.
ETEXI

    {
        .name       = "pcie_aer_inject_error",
        .args_type  = "advisory_non_fatal:-a,correctable:-c,"
	              "id:s,error_status:s,"
	              "header0:i?,header1:i?,header2:i?,header3:i?,"
	              "prefix0:i?,prefix1:i?,prefix2:i?,prefix3:i?",
        .params     = "[-a] [-c] id "
                      "<error_status> [<tlp header> [<tlp header prefix>]]",
        .help       = "inject pcie aer error\n\t\t\t"
	              " -a for advisory non fatal error\n\t\t\t"
	              " -c for correctable error\n\t\t\t"
                      "<id> = qdev device id\n\t\t\t"
                      "<error_status> = error string or 32bit\n\t\t\t"
                      "<tlb header> = 32bit x 4\n\t\t\t"
                      "<tlb header prefix> = 32bit x 4",
        .user_print  = pcie_aer_inject_error_print,
        .mhandler.cmd_new = do_pcie_aer_inejct_error,
    },

STEXI
@item pcie_aer_inject_error
@findex pcie_aer_inject_error
Inject PCIe AER error
ETEXI

    {
        .name       = "host_net_add",
        .args_type  = "device:s,opts:s?",
        .params     = "tap|user|socket|vde|dump [options]",
        .help       = "add host VLAN client",
        .mhandler.cmd = net_host_device_add,
    },

STEXI
@item host_net_add
@findex host_net_add
Add host VLAN client.
ETEXI

    {
        .name       = "host_net_remove",
        .args_type  = "vlan_id:i,device:s",
        .params     = "vlan_id name",
        .help       = "remove host VLAN client",
        .mhandler.cmd = net_host_device_remove,
    },

STEXI
@item host_net_remove
@findex host_net_remove
Remove host VLAN client.
ETEXI

    {
        .name       = "netdev_add",
        .args_type  = "netdev:O",
        .params     = "[user|tap|socket],id=str[,prop=value][,...]",
        .help       = "add host network device",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_netdev_add,
    },

STEXI
@item netdev_add
@findex netdev_add
Add host network device.
ETEXI

    {
        .name       = "netdev_del",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "remove host network device",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_netdev_del,
    },

STEXI
@item netdev_del
@findex netdev_del
Remove host network device.
ETEXI

#ifdef CONFIG_SLIRP
    {
        .name       = "hostfwd_add",
        .args_type  = "arg1:s,arg2:s?,arg3:s?",
        .params     = "[vlan_id name] [tcp|udp]:[hostaddr]:hostport-[guestaddr]:guestport",
        .help       = "redirect TCP or UDP connections from host to guest (requires -net user)",
        .mhandler.cmd = net_slirp_hostfwd_add,
    },
#endif
STEXI
@item hostfwd_add
@findex hostfwd_add
Redirect TCP or UDP connections from host to guest (requires -net user).
ETEXI

#ifdef CONFIG_SLIRP
    {
        .name       = "hostfwd_remove",
        .args_type  = "arg1:s,arg2:s?,arg3:s?",
        .params     = "[vlan_id name] [tcp|udp]:[hostaddr]:hostport",
        .help       = "remove host-to-guest TCP or UDP redirection",
        .mhandler.cmd = net_slirp_hostfwd_remove,
    },

#endif
STEXI
@item hostfwd_remove
@findex hostfwd_remove
Remove host-to-guest TCP or UDP redirection.
ETEXI

    {
        .name       = "balloon",
        .args_type  = "value:M",
        .params     = "target",
        .help       = "request VM to change its memory allocation (in MB)",
        .user_print = monitor_user_noop,
        .mhandler.cmd_async = do_balloon,
        .flags      = MONITOR_CMD_ASYNC,
    },

STEXI
@item balloon @var{value}
@findex balloon
Request VM to change its memory allocation to @var{value} (in MB).
ETEXI

    {
        .name       = "set_link",
        .args_type  = "name:s,up:b",
        .params     = "name on|off",
        .help       = "change the link status of a network adapter",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_set_link,
    },

STEXI
@item set_link @var{name} [on|off]
@findex set_link
Switch link @var{name} on (i.e. up) or off (i.e. down).
ETEXI

    {
        .name       = "watchdog_action",
        .args_type  = "action:s",
        .params     = "[reset|shutdown|poweroff|pause|debug|none]",
        .help       = "change watchdog action",
        .mhandler.cmd = do_watchdog_action,
    },

STEXI
@item watchdog_action
@findex watchdog_action
Change watchdog action.
ETEXI

    {
        .name       = "acl_show",
        .args_type  = "aclname:s",
        .params     = "aclname",
        .help       = "list rules in the access control list",
        .mhandler.cmd = do_acl_show,
    },

STEXI
@item acl_show @var{aclname}
@findex acl_show
List all the matching rules in the access control list, and the default
policy. There are currently two named access control lists,
@var{vnc.x509dname} and @var{vnc.username} matching on the x509 client
certificate distinguished name, and SASL username respectively.
ETEXI

    {
        .name       = "acl_policy",
        .args_type  = "aclname:s,policy:s",
        .params     = "aclname allow|deny",
        .help       = "set default access control list policy",
        .mhandler.cmd = do_acl_policy,
    },

STEXI
@item acl_policy @var{aclname} @code{allow|deny}
@findex acl_policy
Set the default access control list policy, used in the event that
none of the explicit rules match. The default policy at startup is
always @code{deny}.
ETEXI

    {
        .name       = "acl_add",
        .args_type  = "aclname:s,match:s,policy:s,index:i?",
        .params     = "aclname match allow|deny [index]",
        .help       = "add a match rule to the access control list",
        .mhandler.cmd = do_acl_add,
    },

STEXI
@item acl_add @var{aclname} @var{match} @code{allow|deny} [@var{index}]
@findex acl_add
Add a match rule to the access control list, allowing or denying access.
The match will normally be an exact username or x509 distinguished name,
but can optionally include wildcard globs. eg @code{*@@EXAMPLE.COM} to
allow all users in the @code{EXAMPLE.COM} kerberos realm. The match will
normally be appended to the end of the ACL, but can be inserted
earlier in the list if the optional @var{index} parameter is supplied.
ETEXI

    {
        .name       = "acl_remove",
        .args_type  = "aclname:s,match:s",
        .params     = "aclname match",
        .help       = "remove a match rule from the access control list",
        .mhandler.cmd = do_acl_remove,
    },

STEXI
@item acl_remove @var{aclname} @var{match}
@findex acl_remove
Remove the specified match rule from the access control list.
ETEXI

    {
        .name       = "acl_reset",
        .args_type  = "aclname:s",
        .params     = "aclname",
        .help       = "reset the access control list",
        .mhandler.cmd = do_acl_reset,
    },

STEXI
@item acl_reset @var{aclname}
@findex acl_reset
Remove all matches from the access control list, and set the default
policy back to @code{deny}.
ETEXI

#if defined(TARGET_I386)

    {
        .name       = "mce",
        .args_type  = "broadcast:-b,cpu_index:i,bank:i,status:l,mcg_status:l,addr:l,misc:l",
        .params     = "[-b] cpu bank status mcgstatus addr misc",
        .help       = "inject a MCE on the given CPU [and broadcast to other CPUs with -b option]",
        .mhandler.cmd = do_inject_mce,
    },

#endif
STEXI
@item mce @var{cpu} @var{bank} @var{status} @var{mcgstatus} @var{addr} @var{misc}
@findex mce (x86)
Inject an MCE on the given CPU (x86 only).
ETEXI

    {
        .name       = "getfd",
        .args_type  = "fdname:s",
        .params     = "getfd name",
        .help       = "receive a file descriptor via SCM rights and assign it a name",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_getfd,
    },

STEXI
@item getfd @var{fdname}
@findex getfd
If a file descriptor is passed alongside this command using the SCM_RIGHTS
mechanism on unix sockets, it is stored using the name @var{fdname} for
later use by other monitor commands.
ETEXI

    {
        .name       = "closefd",
        .args_type  = "fdname:s",
        .params     = "closefd name",
        .help       = "close a file descriptor previously passed via SCM rights",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_closefd,
    },

STEXI
@item closefd @var{fdname}
@findex closefd
Close the file descriptor previously assigned to @var{fdname} using the
@code{getfd} command. This is only needed if the file descriptor was never
used by another monitor command.
ETEXI

    {
        .name       = "block_passwd",
        .args_type  = "device:B,password:s",
        .params     = "block_passwd device password",
        .help       = "set the password of encrypted block devices",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_block_set_passwd,
    },

STEXI
@item block_passwd @var{device} @var{password}
@findex block_passwd
Set the encrypted device @var{device} password to @var{password}
ETEXI

    {
        .name       = "set_password",
        .args_type  = "protocol:s,password:s,connected:s?",
        .params     = "protocol password action-if-connected",
        .help       = "set spice/vnc password",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = set_password,
    },

STEXI
@item set_password [ vnc | spice ] password [ action-if-connected ]
@findex set_password

Change spice/vnc password.  Use zero to make the password stay valid
forever.  @var{action-if-connected} specifies what should happen in
case a connection is established: @var{fail} makes the password change
fail.  @var{disconnect} changes the password and disconnects the
client.  @var{keep} changes the password and keeps the connection up.
@var{keep} is the default.
ETEXI

    {
        .name       = "expire_password",
        .args_type  = "protocol:s,time:s",
        .params     = "protocol time",
        .help       = "set spice/vnc password expire-time",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = expire_password,
    },

STEXI
@item expire_password [ vnc | spice ] expire-time
@findex expire_password

Specify when a password for spice/vnc becomes
invalid. @var{expire-time} accepts:

@table @var
@item now
Invalidate password instantly.

@item never
Password stays valid forever.

@item +nsec
Password stays valid for @var{nsec} seconds starting now.

@item nsec
Password is invalidated at the given time.  @var{nsec} are the seconds
passed since 1970, i.e. unix epoch.

@end table
ETEXI

    {
        .name       = "info",
        .args_type  = "item:s?",
        .params     = "[subcommand]",
        .help       = "show various information about the system state",
        .mhandler.cmd = do_info,
    },

STEXI
@item info @var{subcommand}
@findex info
Show various information about the system state.

@table @option
@item info version
show the version of QEMU
@item info network
show the various VLANs and the associated devices
@item info chardev
show the character devices
@item info block
show the block devices
@item info blockstats
show block device statistics
@item info registers
show the cpu registers
@item info cpus
show infos for each CPU
@item info history
show the command line history
@item info irq
show the interrupts statistics (if available)
@item info pic
show i8259 (PIC) state
@item info pci
show emulated PCI device info
@item info tlb
show virtual to physical memory mappings (i386, SH4 and SPARC only)
@item info mem
show the active virtual memory mappings (i386 only)
@item info jit
show dynamic compiler info
@item info numa
show NUMA information
@item info kvm
show KVM information
@item info usb
show USB devices plugged on the virtual USB hub
@item info usbhost
show all USB host devices
@item info profile
show profiling information
@item info capture
show information about active capturing
@item info snapshots
show list of VM snapshots
@item info status
show the current VM status (running|paused)
@item info pcmcia
show guest PCMCIA status
@item info mice
show which guest mouse is receiving events
@item info vnc
show the vnc server status
@item info name
show the current VM name
@item info uuid
show the current VM UUID
@item info cpustats
show CPU statistics
@item info usernet
show user network stack connection states
@item info migrate
show migration status
@item info balloon
show balloon information
@item info qtree
show device tree
@item info qdm
show qdev device model list
@item info roms
show roms
@end table
ETEXI

#ifdef CONFIG_TRACE_SIMPLE
STEXI
@item info trace
show contents of trace buffer
ETEXI
#endif

STEXI
@item info trace-events
show available trace events and their state
ETEXI

STEXI
@end table
ETEXI

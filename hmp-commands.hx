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
        .args_type  = "name:S?",
        .params     = "[cmd]",
        .help       = "show the help",
        .cmd        = do_help_cmd,
        .flags      = "p",
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
        .cmd        = hmp_commit,
    },

STEXI
@item commit
@findex commit
Commit changes to the disk images (if -snapshot is used) or backing files.
If the backing file is smaller than the snapshot, then the backing file will be
resized to be the same size as the snapshot.  If the snapshot is smaller than
the backing file, the backing file will not be truncated.  If you want the
backing file to match the size of the smaller snapshot, you can safely truncate
it yourself once the commit operation successfully completes.
ETEXI

    {
        .name       = "q|quit",
        .args_type  = "",
        .params     = "",
        .help       = "quit the emulator",
        .cmd        = hmp_quit,
    },

STEXI
@item q or quit
@findex quit
Quit the emulator.
ETEXI

    {
        .name       = "exit_preconfig",
        .args_type  = "",
        .params     = "",
        .help       = "exit the preconfig state",
        .cmd        = hmp_exit_preconfig,
        .flags      = "p",
    },

STEXI
@item exit_preconfig
@findex exit_preconfig
This command makes QEMU exit the preconfig state and proceed with
VM initialization using configuration data provided on the command line
and via the QMP monitor during the preconfig state. The command is only
available during the preconfig state (i.e. when the --preconfig command
line option was in use).
ETEXI

    {
        .name       = "block_resize",
        .args_type  = "device:B,size:o",
        .params     = "device size",
        .help       = "resize a block image",
        .cmd        = hmp_block_resize,
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
        .name       = "block_stream",
        .args_type  = "device:B,speed:o?,base:s?",
        .params     = "device [speed [base]]",
        .help       = "copy data from a backing file into a block device",
        .cmd        = hmp_block_stream,
    },

STEXI
@item block_stream
@findex block_stream
Copy data from a backing file into a block device.
ETEXI

    {
        .name       = "block_job_set_speed",
        .args_type  = "device:B,speed:o",
        .params     = "device speed",
        .help       = "set maximum speed for a background block operation",
        .cmd        = hmp_block_job_set_speed,
    },

STEXI
@item block_job_set_speed
@findex block_job_set_speed
Set maximum speed for a background block operation.
ETEXI

    {
        .name       = "block_job_cancel",
        .args_type  = "force:-f,device:B",
        .params     = "[-f] device",
        .help       = "stop an active background block operation (use -f"
                      "\n\t\t\t if you want to abort the operation immediately"
                      "\n\t\t\t instead of keep running until data is in sync)",
        .cmd        = hmp_block_job_cancel,
    },

STEXI
@item block_job_cancel
@findex block_job_cancel
Stop an active background block operation (streaming, mirroring).
ETEXI

    {
        .name       = "block_job_complete",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "stop an active background block operation",
        .cmd        = hmp_block_job_complete,
    },

STEXI
@item block_job_complete
@findex block_job_complete
Manually trigger completion of an active background block operation.
For mirroring, this will switch the device to the destination path.
ETEXI

    {
        .name       = "block_job_pause",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "pause an active background block operation",
        .cmd        = hmp_block_job_pause,
    },

STEXI
@item block_job_pause
@findex block_job_pause
Pause an active block streaming operation.
ETEXI

    {
        .name       = "block_job_resume",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "resume a paused background block operation",
        .cmd        = hmp_block_job_resume,
    },

STEXI
@item block_job_resume
@findex block_job_resume
Resume a paused block streaming operation.
ETEXI

    {
        .name       = "eject",
        .args_type  = "force:-f,device:B",
        .params     = "[-f] device",
        .help       = "eject a removable medium (use -f to force it)",
        .cmd        = hmp_eject,
    },

STEXI
@item eject [-f] @var{device}
@findex eject
Eject a removable medium (use -f to force it).
ETEXI

    {
        .name       = "drive_del",
        .args_type  = "id:B",
        .params     = "device",
        .help       = "remove host block device",
        .cmd        = hmp_drive_del,
    },

STEXI
@item drive_del @var{device}
@findex drive_del
Remove host block device.  The result is that guest generated IO is no longer
submitted against the host device underlying the disk.  Once a drive has
been deleted, the QEMU Block layer returns -EIO which results in IO
errors in the guest for applications that are reading/writing to the device.
These errors are always reported to the guest, regardless of the drive's error
actions (drive options rerror, werror).
ETEXI

    {
        .name       = "change",
        .args_type  = "device:B,target:F,arg:s?,read-only-mode:s?",
        .params     = "device filename [format [read-only-mode]]",
        .help       = "change a removable medium, optional format",
        .cmd        = hmp_change,
    },

STEXI
@item change @var{device} @var{setting}
@findex change
Change the configuration of a device.

@table @option
@item change @var{diskdevice} @var{filename} [@var{format} [@var{read-only-mode}]]
Change the medium for a removable disk device to point to @var{filename}. eg

@example
(qemu) change ide1-cd0 /path/to/some.iso
@end example

@var{format} is optional.

@var{read-only-mode} may be used to change the read-only status of the device.
It accepts the following values:

@table @var
@item retain
Retains the current status; this is the default.

@item read-only
Makes the device read-only.

@item read-write
Makes the device writable.
@end table

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
        .args_type  = "filename:F,device:s?,head:i?",
        .params     = "filename [device [head]]",
        .help       = "save screen from head 'head' of display device 'device' "
                      "into PPM image 'filename'",
        .cmd        = hmp_screendump,
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
        .cmd        = hmp_logfile,
    },

STEXI
@item logfile @var{filename}
@findex logfile
Output logs to @var{filename}.
ETEXI

    {
        .name       = "trace-event",
        .args_type  = "name:s,option:b,vcpu:i?",
        .params     = "name on|off [vcpu]",
        .help       = "changes status of a specific trace event "
                      "(vcpu: vCPU to set, default is all)",
        .cmd = hmp_trace_event,
        .command_completion = trace_event_completion,
    },

STEXI
@item trace-event
@findex trace-event
changes status of a trace event
ETEXI

#if defined(CONFIG_TRACE_SIMPLE)
    {
        .name       = "trace-file",
        .args_type  = "op:s?,arg:F?",
        .params     = "on|off|flush|set [arg]",
        .help       = "open, close, or flush trace file, or set a new file name",
        .cmd        = hmp_trace_file,
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
        .help       = "activate logging of the specified items",
        .cmd        = hmp_log,
    },

STEXI
@item log @var{item1}[,...]
@findex log
Activate logging of the specified items.
ETEXI

    {
        .name       = "savevm",
        .args_type  = "name:s?",
        .params     = "tag",
        .help       = "save a VM snapshot. If no tag is provided, a new snapshot is created",
        .cmd        = hmp_savevm,
    },

STEXI
@item savevm @var{tag}
@findex savevm
Create a snapshot of the whole virtual machine. If @var{tag} is
provided, it is used as human readable identifier. If there is already
a snapshot with the same tag, it is replaced. More info at
@ref{vm_snapshots}.

Since 4.0, savevm stopped allowing the snapshot id to be set, accepting
only @var{tag} as parameter.
ETEXI

    {
        .name       = "loadvm",
        .args_type  = "name:s",
        .params     = "tag",
        .help       = "restore a VM snapshot from its tag",
        .cmd        = hmp_loadvm,
        .command_completion = loadvm_completion,
    },

STEXI
@item loadvm @var{tag}
@findex loadvm
Set the whole virtual machine to the snapshot identified by the tag
@var{tag}.

Since 4.0, loadvm stopped accepting snapshot id as parameter.
ETEXI

    {
        .name       = "delvm",
        .args_type  = "name:s",
        .params     = "tag",
        .help       = "delete a VM snapshot from its tag",
        .cmd        = hmp_delvm,
        .command_completion = delvm_completion,
    },

STEXI
@item delvm @var{tag}
@findex delvm
Delete the snapshot identified by @var{tag}.

Since 4.0, delvm stopped deleting snapshots by snapshot id, accepting
only @var{tag} as parameter.
ETEXI

    {
        .name       = "singlestep",
        .args_type  = "option:s?",
        .params     = "[on|off]",
        .help       = "run emulation in singlestep mode or switch to normal mode",
        .cmd        = hmp_singlestep,
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
        .cmd        = hmp_stop,
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
        .cmd        = hmp_cont,
    },

STEXI
@item c or cont
@findex cont
Resume emulation.
ETEXI

    {
        .name       = "system_wakeup",
        .args_type  = "",
        .params     = "",
        .help       = "wakeup guest from suspend",
        .cmd        = hmp_system_wakeup,
    },

STEXI
@item system_wakeup
@findex system_wakeup
Wakeup guest from suspend.
ETEXI

    {
        .name       = "gdbserver",
        .args_type  = "device:s?",
        .params     = "[device]",
        .help       = "start gdbserver on given device (default 'tcp::1234'), stop with 'none'",
        .cmd        = hmp_gdbserver,
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
        .cmd        = hmp_memory_dump,
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
        .cmd        = hmp_physical_memory_dump,
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
        .name       = "gpa2hva",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the host virtual address corresponding to a guest physical address",
        .cmd        = hmp_gpa2hva,
    },

STEXI
@item gpa2hva @var{addr}
@findex gpa2hva
Print the host virtual address at which the guest's physical address @var{addr}
is mapped.
ETEXI

#ifdef CONFIG_LINUX
    {
        .name       = "gpa2hpa",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the host physical address corresponding to a guest physical address",
        .cmd        = hmp_gpa2hpa,
    },
#endif

STEXI
@item gpa2hpa @var{addr}
@findex gpa2hpa
Print the host physical address at which the guest's physical address @var{addr}
is mapped.
ETEXI

    {
        .name       = "gva2gpa",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the guest physical address corresponding to a guest virtual address",
        .cmd        = hmp_gva2gpa,
    },

STEXI
@item gva2gpa @var{addr}
@findex gva2gpa
Print the guest physical address at which the guest's virtual address @var{addr}
is mapped based on the mapping for the current CPU.
ETEXI

    {
        .name       = "p|print",
        .args_type  = "fmt:/,val:l",
        .params     = "/fmt expr",
        .help       = "print expression value (use $reg for CPU register access)",
        .cmd        = do_print,
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
        .cmd        = hmp_ioport_read,
    },

STEXI
@item i/@var{fmt} @var{addr} [.@var{index}]
@findex i
Read I/O port.
ETEXI

    {
        .name       = "o",
        .args_type  = "fmt:/,addr:i,val:i",
        .params     = "/fmt addr value",
        .help       = "I/O port write",
        .cmd        = hmp_ioport_write,
    },

STEXI
@item o/@var{fmt} @var{addr} @var{val}
@findex o
Write to I/O port.
ETEXI

    {
        .name       = "sendkey",
        .args_type  = "keys:s,hold-time:i?",
        .params     = "keys [hold_ms]",
        .help       = "send keys to the VM (e.g. 'sendkey ctrl-alt-f1', default hold time=100 ms)",
        .cmd        = hmp_sendkey,
        .command_completion = sendkey_completion,
    },

STEXI
@item sendkey @var{keys}
@findex sendkey
Send @var{keys} to the guest. @var{keys} could be the name of the
key or the raw value in hexadecimal format. Use @code{-} to press
several keys simultaneously. Example:
@example
sendkey ctrl-alt-f1
@end example

This command is useful to send keys that your graphical user interface
intercepts at low level, such as @code{ctrl-alt-f1} in X Window.
ETEXI
    {
        .name       = "sync-profile",
        .args_type  = "op:s?",
        .params     = "[on|off|reset]",
        .help       = "enable, disable or reset synchronization profiling. "
                      "With no arguments, prints whether profiling is on or off.",
        .cmd        = hmp_sync_profile,
    },

STEXI
@item sync-profile [on|off|reset]
@findex sync-profile
Enable, disable or reset synchronization profiling. With no arguments, prints
whether profiling is on or off.
ETEXI

    {
        .name       = "system_reset",
        .args_type  = "",
        .params     = "",
        .help       = "reset the system",
        .cmd        = hmp_system_reset,
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
        .cmd        = hmp_system_powerdown,
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
        .cmd        = hmp_sum,
    },

STEXI
@item sum @var{addr} @var{size}
@findex sum
Compute the checksum of a memory region.
ETEXI

    {
        .name       = "device_add",
        .args_type  = "device:O",
        .params     = "driver[,prop=value][,...]",
        .help       = "add device, like -device on the command line",
        .cmd        = hmp_device_add,
        .command_completion = device_add_completion,
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
        .cmd        = hmp_device_del,
        .command_completion = device_del_completion,
    },

STEXI
@item device_del @var{id}
@findex device_del
Remove device @var{id}. @var{id} may be a short ID
or a QOM object path.
ETEXI

    {
        .name       = "cpu",
        .args_type  = "index:i",
        .params     = "index",
        .help       = "set the default CPU",
        .cmd        = hmp_cpu,
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
        .cmd        = hmp_mouse_move,
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
        .cmd        = hmp_mouse_button,
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
        .cmd        = hmp_mouse_set,
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

    {
        .name       = "wavcapture",
        .args_type  = "path:F,freq:i?,bits:i?,nchannels:i?",
        .params     = "path [frequency [bits [channels]]]",
        .help       = "capture audio to a wave file (default frequency=44100 bits=16 channels=2)",
        .cmd        = hmp_wavcapture,
    },
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

    {
        .name       = "stopcapture",
        .args_type  = "n:i",
        .params     = "capture index",
        .help       = "stop capture",
        .cmd        = hmp_stopcapture,
    },
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
        .cmd        = hmp_memsave,
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
        .cmd        = hmp_pmemsave,
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
        .cmd        = hmp_boot_set,
    },

STEXI
@item boot_set @var{bootdevicelist}
@findex boot_set
Define new values for the boot device list. Those values will override
the values specified on the command line through the @code{-boot} option.

The values that can be specified here depend on the machine type, but are
the same that can be specified in the @code{-boot} command line option.
ETEXI

    {
        .name       = "nmi",
        .args_type  = "",
        .params     = "",
        .help       = "inject an NMI",
        .cmd        = hmp_nmi,
    },
STEXI
@item nmi @var{cpu}
@findex nmi
Inject an NMI on the default CPU (x86/s390) or all CPUs (ppc64).

ETEXI

    {
        .name       = "ringbuf_write",
        .args_type  = "device:s,data:s",
        .params     = "device data",
        .help       = "Write to a ring buffer character device",
        .cmd        = hmp_ringbuf_write,
        .command_completion = ringbuf_write_completion,
    },

STEXI
@item ringbuf_write @var{device} @var{data}
@findex ringbuf_write
Write @var{data} to ring buffer character device @var{device}.
@var{data} must be a UTF-8 string.

ETEXI

    {
        .name       = "ringbuf_read",
        .args_type  = "device:s,size:i",
        .params     = "device size",
        .help       = "Read from a ring buffer character device",
        .cmd        = hmp_ringbuf_read,
        .command_completion = ringbuf_write_completion,
    },

STEXI
@item ringbuf_read @var{device}
@findex ringbuf_read
Read and print up to @var{size} bytes from ring buffer character
device @var{device}.
Certain non-printable characters are printed \uXXXX, where XXXX is the
character code in hexadecimal.  Character \ is printed \\.
Bug: can screw up when the buffer contains invalid UTF-8 sequences,
NUL characters, after the ring buffer lost data, and when reading
stops because the size limit is reached.

ETEXI

    {
        .name       = "announce_self",
        .args_type  = "interfaces:s?,id:s?",
        .params     = "[interfaces] [id]",
        .help       = "Trigger GARP/RARP announcements",
        .cmd        = hmp_announce_self,
    },

STEXI
@item announce_self
@findex announce_self
Trigger a round of GARP/RARP broadcasts; this is useful for explicitly updating the
network infrastructure after a reconfiguration or some forms of migration.
The timings of the round are set by the migration announce parameters.
An optional comma separated @var{interfaces} list restricts the announce to the
named set of interfaces. An optional @var{id} can be used to start a separate announce
timer and to change the parameters of it later.
ETEXI

    {
        .name       = "migrate",
        .args_type  = "detach:-d,blk:-b,inc:-i,resume:-r,uri:s",
        .params     = "[-d] [-b] [-i] [-r] uri",
        .help       = "migrate to URI (using -d to not wait for completion)"
		      "\n\t\t\t -b for migration without shared storage with"
		      " full copy of disk\n\t\t\t -i for migration without "
		      "shared storage with incremental copy of disk "
		      "(base image shared between src and destination)"
                      "\n\t\t\t -r to resume a paused migration",
        .cmd        = hmp_migrate,
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
        .cmd        = hmp_migrate_cancel,
    },

STEXI
@item migrate_cancel
@findex migrate_cancel
Cancel the current VM migration.
ETEXI

    {
        .name       = "migrate_continue",
        .args_type  = "state:s",
        .params     = "state",
        .help       = "Continue migration from the given paused state",
        .cmd        = hmp_migrate_continue,
    },
STEXI
@item migrate_continue @var{state}
@findex migrate_continue
Continue migration from the paused state @var{state}
ETEXI

    {
        .name       = "migrate_incoming",
        .args_type  = "uri:s",
        .params     = "uri",
        .help       = "Continue an incoming migration from an -incoming defer",
        .cmd        = hmp_migrate_incoming,
    },

STEXI
@item migrate_incoming @var{uri}
@findex migrate_incoming
Continue an incoming migration using the @var{uri} (that has the same syntax
as the -incoming option).
ETEXI

    {
        .name       = "migrate_recover",
        .args_type  = "uri:s",
        .params     = "uri",
        .help       = "Continue a paused incoming postcopy migration",
        .cmd        = hmp_migrate_recover,
    },

STEXI
@item migrate_recover @var{uri}
@findex migrate_recover
Continue a paused incoming postcopy migration using the @var{uri}.
ETEXI

    {
        .name       = "migrate_pause",
        .args_type  = "",
        .params     = "",
        .help       = "Pause an ongoing migration (postcopy-only)",
        .cmd        = hmp_migrate_pause,
    },

STEXI
@item migrate_pause
@findex migrate_pause
Pause an ongoing migration.  Currently it only supports postcopy.
ETEXI

    {
        .name       = "migrate_set_cache_size",
        .args_type  = "value:o",
        .params     = "value",
        .help       = "set cache size (in bytes) for XBZRLE migrations,"
                      "the cache size will be rounded down to the nearest "
                      "power of 2.\n"
                      "The cache size affects the number of cache misses."
                      "In case of a high cache miss ratio you need to increase"
                      " the cache size",
        .cmd        = hmp_migrate_set_cache_size,
    },

STEXI
@item migrate_set_cache_size @var{value}
@findex migrate_set_cache_size
Set cache size to @var{value} (in bytes) for xbzrle migrations.
ETEXI

    {
        .name       = "migrate_set_speed",
        .args_type  = "value:o",
        .params     = "value",
        .help       = "set maximum speed (in bytes) for migrations. "
	"Defaults to MB if no size suffix is specified, ie. B/K/M/G/T",
        .cmd        = hmp_migrate_set_speed,
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
        .cmd        = hmp_migrate_set_downtime,
    },

STEXI
@item migrate_set_downtime @var{second}
@findex migrate_set_downtime
Set maximum tolerated downtime (in seconds) for migration.
ETEXI

    {
        .name       = "migrate_set_capability",
        .args_type  = "capability:s,state:b",
        .params     = "capability state",
        .help       = "Enable/Disable the usage of a capability for migration",
        .cmd        = hmp_migrate_set_capability,
        .command_completion = migrate_set_capability_completion,
    },

STEXI
@item migrate_set_capability @var{capability} @var{state}
@findex migrate_set_capability
Enable/Disable the usage of a capability @var{capability} for migration.
ETEXI

    {
        .name       = "migrate_set_parameter",
        .args_type  = "parameter:s,value:s",
        .params     = "parameter value",
        .help       = "Set the parameter for migration",
        .cmd        = hmp_migrate_set_parameter,
        .command_completion = migrate_set_parameter_completion,
    },

STEXI
@item migrate_set_parameter @var{parameter} @var{value}
@findex migrate_set_parameter
Set the parameter @var{parameter} for migration.
ETEXI

    {
        .name       = "migrate_start_postcopy",
        .args_type  = "",
        .params     = "",
        .help       = "Followup to a migration command to switch the migration"
                      " to postcopy mode. The postcopy-ram capability must "
                      "be set on both source and destination before the "
                      "original migration command .",
        .cmd        = hmp_migrate_start_postcopy,
    },

STEXI
@item migrate_start_postcopy
@findex migrate_start_postcopy
Switch in-progress migration to postcopy mode. Ignored after the end of
migration (or once already in postcopy).
ETEXI

    {
        .name       = "x_colo_lost_heartbeat",
        .args_type  = "",
        .params     = "",
        .help       = "Tell COLO that heartbeat is lost,\n\t\t\t"
                      "a failover or takeover is needed.",
        .cmd = hmp_x_colo_lost_heartbeat,
    },

STEXI
@item x_colo_lost_heartbeat
@findex x_colo_lost_heartbeat
Tell COLO that heartbeat is lost, a failover or takeover is needed.
ETEXI

    {
        .name       = "client_migrate_info",
        .args_type  = "protocol:s,hostname:s,port:i?,tls-port:i?,cert-subject:s?",
        .params     = "protocol hostname port tls-port cert-subject",
        .help       = "set migration information for remote display",
        .cmd        = hmp_client_migrate_info,
    },

STEXI
@item client_migrate_info @var{protocol} @var{hostname} @var{port} @var{tls-port} @var{cert-subject}
@findex client_migrate_info
Set migration information for remote display.  This makes the server
ask the client to automatically reconnect using the new parameters
once migration finished successfully.  Only implemented for SPICE.
ETEXI

    {
        .name       = "dump-guest-memory",
        .args_type  = "paging:-p,detach:-d,windmp:-w,zlib:-z,lzo:-l,snappy:-s,filename:F,begin:l?,length:l?",
        .params     = "[-p] [-d] [-z|-l|-s|-w] filename [begin length]",
        .help       = "dump guest memory into file 'filename'.\n\t\t\t"
                      "-p: do paging to get guest's memory mapping.\n\t\t\t"
                      "-d: return immediately (do not wait for completion).\n\t\t\t"
                      "-z: dump in kdump-compressed format, with zlib compression.\n\t\t\t"
                      "-l: dump in kdump-compressed format, with lzo compression.\n\t\t\t"
                      "-s: dump in kdump-compressed format, with snappy compression.\n\t\t\t"
                      "-w: dump in Windows crashdump format (can be used instead of ELF-dump converting),\n\t\t\t"
                      "    for Windows x64 guests with vmcoreinfo driver only.\n\t\t\t"
                      "begin: the starting physical address.\n\t\t\t"
                      "length: the memory size, in bytes.",
        .cmd        = hmp_dump_guest_memory,
    },

STEXI
@item dump-guest-memory [-p] @var{filename} @var{begin} @var{length}
@item dump-guest-memory [-z|-l|-s|-w] @var{filename}
@findex dump-guest-memory
Dump guest memory to @var{protocol}. The file can be processed with crash or
gdb. Without -z|-l|-s|-w, the dump format is ELF.
        -p: do paging to get guest's memory mapping.
        -z: dump in kdump-compressed format, with zlib compression.
        -l: dump in kdump-compressed format, with lzo compression.
        -s: dump in kdump-compressed format, with snappy compression.
        -w: dump in Windows crashdump format (can be used instead of ELF-dump converting),
            for Windows x64 guests with vmcoreinfo driver only
  filename: dump file name.
     begin: the starting physical address. It's optional, and should be
            specified together with length.
    length: the memory size, in bytes. It's optional, and should be specified
            together with begin.
ETEXI

#if defined(TARGET_S390X)
    {
        .name       = "dump-skeys",
        .args_type  = "filename:F",
        .params     = "",
        .help       = "Save guest storage keys into file 'filename'.\n",
        .cmd        = hmp_dump_skeys,
    },
#endif

STEXI
@item dump-skeys @var{filename}
@findex dump-skeys
Save guest storage keys to a file.
ETEXI

#if defined(TARGET_S390X)
    {
        .name       = "migration_mode",
        .args_type  = "mode:i",
        .params     = "mode",
        .help       = "Enables or disables migration mode\n",
        .cmd        = hmp_migrationmode,
    },
#endif

STEXI
@item migration_mode @var{mode}
@findex migration_mode
Enables or disables migration mode.
ETEXI

    {
        .name       = "snapshot_blkdev",
        .args_type  = "reuse:-n,device:B,snapshot-file:s?,format:s?",
        .params     = "[-n] device [new-image-file] [format]",
        .help       = "initiates a live snapshot\n\t\t\t"
                      "of device. If a new image file is specified, the\n\t\t\t"
                      "new image file will become the new root image.\n\t\t\t"
                      "If format is specified, the snapshot file will\n\t\t\t"
                      "be created in that format.\n\t\t\t"
                      "The default format is qcow2.  The -n flag requests QEMU\n\t\t\t"
                      "to reuse the image found in new-image-file, instead of\n\t\t\t"
                      "recreating it from scratch.",
        .cmd        = hmp_snapshot_blkdev,
    },

STEXI
@item snapshot_blkdev
@findex snapshot_blkdev
Snapshot device, using snapshot file as target if provided
ETEXI

    {
        .name       = "snapshot_blkdev_internal",
        .args_type  = "device:B,name:s",
        .params     = "device name",
        .help       = "take an internal snapshot of device.\n\t\t\t"
                      "The format of the image used by device must\n\t\t\t"
                      "support it, such as qcow2.\n\t\t\t",
        .cmd        = hmp_snapshot_blkdev_internal,
    },

STEXI
@item snapshot_blkdev_internal
@findex snapshot_blkdev_internal
Take an internal snapshot on device if it support
ETEXI

    {
        .name       = "snapshot_delete_blkdev_internal",
        .args_type  = "device:B,name:s,id:s?",
        .params     = "device name [id]",
        .help       = "delete an internal snapshot of device.\n\t\t\t"
                      "If id is specified, qemu will try delete\n\t\t\t"
                      "the snapshot matching both id and name.\n\t\t\t"
                      "The format of the image used by device must\n\t\t\t"
                      "support it, such as qcow2.\n\t\t\t",
        .cmd        = hmp_snapshot_delete_blkdev_internal,
    },

STEXI
@item snapshot_delete_blkdev_internal
@findex snapshot_delete_blkdev_internal
Delete an internal snapshot on device if it support
ETEXI

    {
        .name       = "drive_mirror",
        .args_type  = "reuse:-n,full:-f,device:B,target:s,format:s?",
        .params     = "[-n] [-f] device target [format]",
        .help       = "initiates live storage\n\t\t\t"
                      "migration for a device. The device's contents are\n\t\t\t"
                      "copied to the new image file, including data that\n\t\t\t"
                      "is written after the command is started.\n\t\t\t"
                      "The -n flag requests QEMU to reuse the image found\n\t\t\t"
                      "in new-image-file, instead of recreating it from scratch.\n\t\t\t"
                      "The -f flag requests QEMU to copy the whole disk,\n\t\t\t"
                      "so that the result does not need a backing file.\n\t\t\t",
        .cmd        = hmp_drive_mirror,
    },
STEXI
@item drive_mirror
@findex drive_mirror
Start mirroring a block device's writes to a new destination,
using the specified target.
ETEXI

    {
        .name       = "drive_backup",
        .args_type  = "reuse:-n,full:-f,compress:-c,device:B,target:s,format:s?",
        .params     = "[-n] [-f] [-c] device target [format]",
        .help       = "initiates a point-in-time\n\t\t\t"
                      "copy for a device. The device's contents are\n\t\t\t"
                      "copied to the new image file, excluding data that\n\t\t\t"
                      "is written after the command is started.\n\t\t\t"
                      "The -n flag requests QEMU to reuse the image found\n\t\t\t"
                      "in new-image-file, instead of recreating it from scratch.\n\t\t\t"
                      "The -f flag requests QEMU to copy the whole disk,\n\t\t\t"
                      "so that the result does not need a backing file.\n\t\t\t"
                      "The -c flag requests QEMU to compress backup data\n\t\t\t"
                      "(if the target format supports it).\n\t\t\t",
        .cmd        = hmp_drive_backup,
    },
STEXI
@item drive_backup
@findex drive_backup
Start a point-in-time copy of a block device to a specificed target.
ETEXI

    {
        .name       = "drive_add",
        .args_type  = "node:-n,pci_addr:s,opts:s",
        .params     = "[-n] [[<domain>:]<bus>:]<slot>\n"
                      "[file=file][,if=type][,bus=n]\n"
                      "[,unit=m][,media=d][,index=i]\n"
                      "[,snapshot=on|off][,cache=on|off]\n"
                      "[,readonly=on|off][,copy-on-read=on|off]",
        .help       = "add drive to PCI storage controller",
        .cmd        = hmp_drive_add,
    },

STEXI
@item drive_add
@findex drive_add
Add drive to PCI storage controller.
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
        .cmd        = hmp_pcie_aer_inject_error,
    },

STEXI
@item pcie_aer_inject_error
@findex pcie_aer_inject_error
Inject PCIe AER error
ETEXI

    {
        .name       = "netdev_add",
        .args_type  = "netdev:O",
        .params     = "[user|tap|socket|vde|bridge|hubport|netmap|vhost-user],id=str[,prop=value][,...]",
        .help       = "add host network device",
        .cmd        = hmp_netdev_add,
        .command_completion = netdev_add_completion,
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
        .cmd        = hmp_netdev_del,
        .command_completion = netdev_del_completion,
    },

STEXI
@item netdev_del
@findex netdev_del
Remove host network device.
ETEXI

    {
        .name       = "object_add",
        .args_type  = "object:O",
        .params     = "[qom-type=]type,id=str[,prop=value][,...]",
        .help       = "create QOM object",
        .cmd        = hmp_object_add,
        .command_completion = object_add_completion,
    },

STEXI
@item object_add
@findex object_add
Create QOM object.
ETEXI

    {
        .name       = "object_del",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "destroy QOM object",
        .cmd        = hmp_object_del,
        .command_completion = object_del_completion,
    },

STEXI
@item object_del
@findex object_del
Destroy QOM object.
ETEXI

#ifdef CONFIG_SLIRP
    {
        .name       = "hostfwd_add",
        .args_type  = "arg1:s,arg2:s?,arg3:s?",
        .params     = "[hub_id name]|[netdev_id] [tcp|udp]:[hostaddr]:hostport-[guestaddr]:guestport",
        .help       = "redirect TCP or UDP connections from host to guest (requires -net user)",
        .cmd        = hmp_hostfwd_add,
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
        .params     = "[hub_id name]|[netdev_id] [tcp|udp]:[hostaddr]:hostport",
        .help       = "remove host-to-guest TCP or UDP redirection",
        .cmd        = hmp_hostfwd_remove,
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
        .cmd        = hmp_balloon,
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
        .cmd        = hmp_set_link,
        .command_completion = set_link_completion,
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
        .cmd        = hmp_watchdog_action,
        .command_completion = watchdog_action_completion,
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
        .cmd        = hmp_acl_show,
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
        .cmd        = hmp_acl_policy,
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
        .cmd        = hmp_acl_add,
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
        .cmd        = hmp_acl_remove,
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
        .cmd        = hmp_acl_reset,
    },

STEXI
@item acl_reset @var{aclname}
@findex acl_reset
Remove all matches from the access control list, and set the default
policy back to @code{deny}.
ETEXI

    {
        .name       = "nbd_server_start",
        .args_type  = "all:-a,writable:-w,uri:s",
        .params     = "nbd_server_start [-a] [-w] host:port",
        .help       = "serve block devices on the given host and port",
        .cmd        = hmp_nbd_server_start,
    },
STEXI
@item nbd_server_start @var{host}:@var{port}
@findex nbd_server_start
Start an NBD server on the given host and/or port.  If the @option{-a}
option is included, all of the virtual machine's block devices that
have an inserted media on them are automatically exported; in this case,
the @option{-w} option makes the devices writable too.
ETEXI

    {
        .name       = "nbd_server_add",
        .args_type  = "writable:-w,device:B,name:s?",
        .params     = "nbd_server_add [-w] device [name]",
        .help       = "export a block device via NBD",
        .cmd        = hmp_nbd_server_add,
    },
STEXI
@item nbd_server_add @var{device} [ @var{name} ]
@findex nbd_server_add
Export a block device through QEMU's NBD server, which must be started
beforehand with @command{nbd_server_start}.  The @option{-w} option makes the
exported device writable too.  The export name is controlled by @var{name},
defaulting to @var{device}.
ETEXI

    {
        .name       = "nbd_server_remove",
        .args_type  = "force:-f,name:s",
        .params     = "nbd_server_remove [-f] name",
        .help       = "remove an export previously exposed via NBD",
        .cmd        = hmp_nbd_server_remove,
    },
STEXI
@item nbd_server_remove [-f] @var{name}
@findex nbd_server_remove
Stop exporting a block device through QEMU's NBD server, which was
previously started with @command{nbd_server_add}.  The @option{-f}
option forces the server to drop the export immediately even if
clients are connected; otherwise the command fails unless there are no
clients.
ETEXI

    {
        .name       = "nbd_server_stop",
        .args_type  = "",
        .params     = "nbd_server_stop",
        .help       = "stop serving block devices using the NBD protocol",
        .cmd        = hmp_nbd_server_stop,
    },
STEXI
@item nbd_server_stop
@findex nbd_server_stop
Stop the QEMU embedded NBD server.
ETEXI


#if defined(TARGET_I386)

    {
        .name       = "mce",
        .args_type  = "broadcast:-b,cpu_index:i,bank:i,status:l,mcg_status:l,addr:l,misc:l",
        .params     = "[-b] cpu bank status mcgstatus addr misc",
        .help       = "inject a MCE on the given CPU [and broadcast to other CPUs with -b option]",
        .cmd        = hmp_mce,
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
        .cmd        = hmp_getfd,
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
        .cmd        = hmp_closefd,
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
        .cmd        = hmp_block_passwd,
    },

STEXI
@item block_passwd @var{device} @var{password}
@findex block_passwd
Set the encrypted device @var{device} password to @var{password}

This command is now obsolete and will always return an error since 2.10
ETEXI

    {
        .name       = "block_set_io_throttle",
        .args_type  = "device:B,bps:l,bps_rd:l,bps_wr:l,iops:l,iops_rd:l,iops_wr:l",
        .params     = "device bps bps_rd bps_wr iops iops_rd iops_wr",
        .help       = "change I/O throttle limits for a block drive",
        .cmd        = hmp_block_set_io_throttle,
    },

STEXI
@item block_set_io_throttle @var{device} @var{bps} @var{bps_rd} @var{bps_wr} @var{iops} @var{iops_rd} @var{iops_wr}
@findex block_set_io_throttle
Change I/O throttle limits for a block drive to @var{bps} @var{bps_rd} @var{bps_wr} @var{iops} @var{iops_rd} @var{iops_wr}.
@var{device} can be a block device name, a qdev ID or a QOM path.
ETEXI

    {
        .name       = "set_password",
        .args_type  = "protocol:s,password:s,connected:s?",
        .params     = "protocol password action-if-connected",
        .help       = "set spice/vnc password",
        .cmd        = hmp_set_password,
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
        .cmd        = hmp_expire_password,
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
        .name       = "chardev-add",
        .args_type  = "args:s",
        .params     = "args",
        .help       = "add chardev",
        .cmd        = hmp_chardev_add,
        .command_completion = chardev_add_completion,
    },

STEXI
@item chardev-add args
@findex chardev-add
chardev-add accepts the same parameters as the -chardev command line switch.

ETEXI

    {
        .name       = "chardev-change",
        .args_type  = "id:s,args:s",
        .params     = "id args",
        .help       = "change chardev",
        .cmd        = hmp_chardev_change,
    },

STEXI
@item chardev-change args
@findex chardev-change
chardev-change accepts existing chardev @var{id} and then the same arguments
as the -chardev command line switch (except for "id").

ETEXI

    {
        .name       = "chardev-remove",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "remove chardev",
        .cmd        = hmp_chardev_remove,
        .command_completion = chardev_remove_completion,
    },

STEXI
@item chardev-remove id
@findex chardev-remove
Removes the chardev @var{id}.

ETEXI

    {
        .name       = "chardev-send-break",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "send a break on chardev",
        .cmd        = hmp_chardev_send_break,
        .command_completion = chardev_remove_completion,
    },

STEXI
@item chardev-send-break id
@findex chardev-send-break
Send a break on the chardev @var{id}.

ETEXI

    {
        .name       = "qemu-io",
        .args_type  = "device:B,command:s",
        .params     = "[device] \"[command]\"",
        .help       = "run a qemu-io command on a block device",
        .cmd        = hmp_qemu_io,
    },

STEXI
@item qemu-io @var{device} @var{command}
@findex qemu-io
Executes a qemu-io command on the given block device.

ETEXI

    {
        .name       = "cpu-add",
        .args_type  = "id:i",
        .params     = "id",
        .help       = "add cpu (deprecated, use device_add instead)",
        .cmd        = hmp_cpu_add,
    },

STEXI
@item cpu-add @var{id}
@findex cpu-add
Add CPU with id @var{id}.  This command is deprecated, please
+use @code{device_add} instead. For details, refer to
'docs/cpu-hotplug.rst'.
ETEXI

    {
        .name       = "qom-list",
        .args_type  = "path:s?",
        .params     = "path",
        .help       = "list QOM properties",
        .cmd        = hmp_qom_list,
        .flags      = "p",
    },

STEXI
@item qom-list [@var{path}]
Print QOM properties of object at location @var{path}
ETEXI

    {
        .name       = "qom-set",
        .args_type  = "path:s,property:s,value:s",
        .params     = "path property value",
        .help       = "set QOM property",
        .cmd        = hmp_qom_set,
        .flags      = "p",
    },

STEXI
@item qom-set @var{path} @var{property} @var{value}
Set QOM property @var{property} of object at location @var{path} to value @var{value}
ETEXI

    {
        .name       = "info",
        .args_type  = "item:s?",
        .params     = "[subcommand]",
        .help       = "show various information about the system state",
        .cmd        = hmp_info_help,
        .sub_table  = hmp_info_cmds,
        .flags      = "p",
    },

STEXI
@end table
ETEXI

HXCOMM Use DEFHEADING() to define headings in both help text and rST.
HXCOMM Text between SRST and ERST is copied to the rST version and
HXCOMM discarded from C version.
HXCOMM DEF(command, args, callback, arg_string, help) is used to construct
HXCOMM monitor commands
HXCOMM HXCOMM can be used for comments, discarded from both rST and C.


    {
        .name       = "help|?",
        .args_type  = "name:S?",
        .params     = "[cmd]",
        .help       = "show the help",
        .cmd        = do_help_cmd,
        .flags      = "p",
    },

SRST
``help`` or ``?`` [*cmd*]
  Show the help for all commands or just for command *cmd*.
ERST

    {
        .name       = "commit",
        .args_type  = "device:B",
        .params     = "device|all",
        .help       = "commit changes to the disk images (if -snapshot is used) or backing files",
        .cmd        = hmp_commit,
    },

SRST
``commit``
  Commit changes to the disk images (if -snapshot is used) or backing files.
  If the backing file is smaller than the snapshot, then the backing file
  will be resized to be the same size as the snapshot.  If the snapshot is
  smaller than the backing file, the backing file will not be truncated.
  If you want the backing file to match the size of the smaller snapshot,
  you can safely truncate it yourself once the commit operation successfully
  completes.
ERST

    {
        .name       = "quit|q",
        .args_type  = "",
        .params     = "",
        .help       = "quit the emulator",
        .cmd        = hmp_quit,
        .flags      = "p",
    },

SRST
``quit`` or ``q``
  Quit the emulator.
ERST

    {
        .name       = "exit_preconfig",
        .args_type  = "",
        .params     = "",
        .help       = "exit the preconfig state",
        .cmd        = hmp_exit_preconfig,
        .flags      = "p",
    },

SRST
``exit_preconfig``
  This command makes QEMU exit the preconfig state and proceed with
  VM initialization using configuration data provided on the command line
  and via the QMP monitor during the preconfig state. The command is only
  available during the preconfig state (i.e. when the --preconfig command
  line option was in use).
ERST

    {
        .name       = "block_resize",
        .args_type  = "device:B,size:o",
        .params     = "device size",
        .help       = "resize a block image",
        .cmd        = hmp_block_resize,
        .coroutine  = true,
    },

SRST
``block_resize``
  Resize a block image while a guest is running.  Usually requires guest
  action to see the updated size.  Resize to a lower size is supported,
  but should be used with extreme caution.  Note that this command only
  resizes image files, it can not resize block devices like LVM volumes.
ERST

    {
        .name       = "block_stream",
        .args_type  = "device:B,speed:o?,base:s?",
        .params     = "device [speed [base]]",
        .help       = "copy data from a backing file into a block device",
        .cmd        = hmp_block_stream,
    },

SRST
``block_stream``
  Copy data from a backing file into a block device.
ERST

    {
        .name       = "block_job_set_speed",
        .args_type  = "device:B,speed:o",
        .params     = "device speed",
        .help       = "set maximum speed for a background block operation",
        .cmd        = hmp_block_job_set_speed,
    },

SRST
``block_job_set_speed``
  Set maximum speed for a background block operation.
ERST

    {
        .name       = "block_job_cancel",
        .args_type  = "force:-f,device:B",
        .params     = "[-f] device",
        .help       = "stop an active background block operation (use -f"
                      "\n\t\t\t if you want to abort the operation immediately"
                      "\n\t\t\t instead of keep running until data is in sync)",
        .cmd        = hmp_block_job_cancel,
    },

SRST
``block_job_cancel``
  Stop an active background block operation (streaming, mirroring).
ERST

    {
        .name       = "block_job_complete",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "stop an active background block operation",
        .cmd        = hmp_block_job_complete,
    },

SRST
``block_job_complete``
  Manually trigger completion of an active background block operation.
  For mirroring, this will switch the device to the destination path.
ERST

    {
        .name       = "block_job_pause",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "pause an active background block operation",
        .cmd        = hmp_block_job_pause,
    },

SRST
``block_job_pause``
  Pause an active block streaming operation.
ERST

    {
        .name       = "block_job_resume",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "resume a paused background block operation",
        .cmd        = hmp_block_job_resume,
    },

SRST
``block_job_resume``
  Resume a paused block streaming operation.
ERST

    {
        .name       = "eject",
        .args_type  = "force:-f,device:B",
        .params     = "[-f] device",
        .help       = "eject a removable medium (use -f to force it)",
        .cmd        = hmp_eject,
    },

SRST
``eject [-f]`` *device*
  Eject a removable medium (use -f to force it).
ERST

    {
        .name       = "drive_del",
        .args_type  = "id:B",
        .params     = "device",
        .help       = "remove host block device",
        .cmd        = hmp_drive_del,
    },

SRST
``drive_del`` *device*
  Remove host block device.  The result is that guest generated IO is no longer
  submitted against the host device underlying the disk.  Once a drive has
  been deleted, the QEMU Block layer returns -EIO which results in IO
  errors in the guest for applications that are reading/writing to the device.
  These errors are always reported to the guest, regardless of the drive's error
  actions (drive options rerror, werror).
ERST

    {
        .name       = "change",
        .args_type  = "device:B,target:F,arg:s?,read-only-mode:s?",
        .params     = "device filename [format [read-only-mode]]",
        .help       = "change a removable medium, optional format",
        .cmd        = hmp_change,
    },

SRST
``change`` *device* *setting*
  Change the configuration of a device.

  ``change`` *diskdevice* *filename* [*format* [*read-only-mode*]]
    Change the medium for a removable disk device to point to *filename*. eg::

      (qemu) change ide1-cd0 /path/to/some.iso

    *format* is optional.

    *read-only-mode* may be used to change the read-only status of the device.
    It accepts the following values:

    retain
      Retains the current status; this is the default.

    read-only
      Makes the device read-only.

    read-write
      Makes the device writable.

  ``change vnc password`` [*password*]

    Change the password associated with the VNC server. If the new password
    is not supplied, the monitor will prompt for it to be entered. VNC
    passwords are only significant up to 8 letters. eg::

      (qemu) change vnc password
      Password: ********

ERST

    {
        .name       = "screendump",
        .args_type  = "filename:F,device:s?,head:i?",
        .params     = "filename [device [head]]",
        .help       = "save screen from head 'head' of display device 'device' "
                      "into PPM image 'filename'",
        .cmd        = hmp_screendump,
        .coroutine  = true,
    },

SRST
``screendump`` *filename*
  Save screen into PPM image *filename*.
ERST

    {
        .name       = "logfile",
        .args_type  = "filename:F",
        .params     = "filename",
        .help       = "output logs to 'filename'",
        .cmd        = hmp_logfile,
    },

SRST
``logfile`` *filename*
  Output logs to *filename*.
ERST

    {
        .name       = "trace-event",
        .args_type  = "name:s,option:b,vcpu:i?",
        .params     = "name on|off [vcpu]",
        .help       = "changes status of a specific trace event "
                      "(vcpu: vCPU to set, default is all)",
        .cmd = hmp_trace_event,
        .command_completion = trace_event_completion,
    },

SRST
``trace-event``
  changes status of a trace event
ERST

#if defined(CONFIG_TRACE_SIMPLE)
    {
        .name       = "trace-file",
        .args_type  = "op:s?,arg:F?",
        .params     = "on|off|flush|set [arg]",
        .help       = "open, close, or flush trace file, or set a new file name",
        .cmd        = hmp_trace_file,
    },

SRST
``trace-file on|off|flush``
  Open, close, or flush the trace file.  If no argument is given, the
  status of the trace file is displayed.
ERST
#endif

    {
        .name       = "log",
        .args_type  = "items:s",
        .params     = "item1[,...]",
        .help       = "activate logging of the specified items",
        .cmd        = hmp_log,
    },

SRST
``log`` *item1*\ [,...]
  Activate logging of the specified items.
ERST

    {
        .name       = "savevm",
        .args_type  = "name:s?",
        .params     = "tag",
        .help       = "save a VM snapshot. If no tag is provided, a new snapshot is created",
        .cmd        = hmp_savevm,
    },

SRST
``savevm`` *tag*
  Create a snapshot of the whole virtual machine. If *tag* is
  provided, it is used as human readable identifier. If there is already
  a snapshot with the same tag, it is replaced. More info at
  :ref:`vm_005fsnapshots`.

  Since 4.0, savevm stopped allowing the snapshot id to be set, accepting
  only *tag* as parameter.
ERST

    {
        .name       = "loadvm",
        .args_type  = "name:s",
        .params     = "tag",
        .help       = "restore a VM snapshot from its tag",
        .cmd        = hmp_loadvm,
        .command_completion = loadvm_completion,
    },

SRST
``loadvm`` *tag*
  Set the whole virtual machine to the snapshot identified by the tag
  *tag*.

  Since 4.0, loadvm stopped accepting snapshot id as parameter.
ERST

    {
        .name       = "delvm",
        .args_type  = "name:s",
        .params     = "tag",
        .help       = "delete a VM snapshot from its tag",
        .cmd        = hmp_delvm,
        .command_completion = delvm_completion,
    },

SRST
``delvm`` *tag*
  Delete the snapshot identified by *tag*.

  Since 4.0, delvm stopped deleting snapshots by snapshot id, accepting
  only *tag* as parameter.
ERST

    {
        .name       = "singlestep",
        .args_type  = "option:s?",
        .params     = "[on|off]",
        .help       = "run emulation in singlestep mode or switch to normal mode",
        .cmd        = hmp_singlestep,
    },

SRST
``singlestep [off]``
  Run the emulation in single step mode.
  If called with option off, the emulation returns to normal mode.
ERST

    {
        .name       = "stop|s",
        .args_type  = "",
        .params     = "",
        .help       = "stop emulation",
        .cmd        = hmp_stop,
    },

SRST
``stop`` or ``s``
  Stop emulation.
ERST

    {
        .name       = "cont|c",
        .args_type  = "",
        .params     = "",
        .help       = "resume emulation",
        .cmd        = hmp_cont,
    },

SRST
``cont`` or ``c``
  Resume emulation.
ERST

    {
        .name       = "system_wakeup",
        .args_type  = "",
        .params     = "",
        .help       = "wakeup guest from suspend",
        .cmd        = hmp_system_wakeup,
    },

SRST
``system_wakeup``
  Wakeup guest from suspend.
ERST

    {
        .name       = "gdbserver",
        .args_type  = "device:s?",
        .params     = "[device]",
        .help       = "start gdbserver on given device (default 'tcp::1234'), stop with 'none'",
        .cmd        = hmp_gdbserver,
    },

SRST
``gdbserver`` [*port*]
  Start gdbserver session (default *port*\=1234)
ERST

    {
        .name       = "x",
        .args_type  = "fmt:/,addr:l",
        .params     = "/fmt addr",
        .help       = "virtual memory dump starting at 'addr'",
        .cmd        = hmp_memory_dump,
    },

SRST
``x/``\ *fmt* *addr*
  Virtual memory dump starting at *addr*.
ERST

    {
        .name       = "xp",
        .args_type  = "fmt:/,addr:l",
        .params     = "/fmt addr",
        .help       = "physical memory dump starting at 'addr'",
        .cmd        = hmp_physical_memory_dump,
    },

SRST
``xp /``\ *fmt* *addr*
  Physical memory dump starting at *addr*.

  *fmt* is a format which tells the command how to format the
  data. Its syntax is: ``/{count}{format}{size}``

  *count*
    is the number of items to be dumped.
  *format*
    can be x (hex), d (signed decimal), u (unsigned decimal), o (octal),
    c (char) or i (asm instruction).
  *size*
    can be b (8 bits), h (16 bits), w (32 bits) or g (64 bits). On x86,
    ``h`` or ``w`` can be specified with the ``i`` format to
    respectively select 16 or 32 bit code instruction size.

  Examples:

  Dump 10 instructions at the current instruction pointer::

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

  Dump 80 16 bit values at the start of the video memory::

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

ERST

    {
        .name       = "gpa2hva",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the host virtual address corresponding to a guest physical address",
        .cmd        = hmp_gpa2hva,
    },

SRST
``gpa2hva`` *addr*
  Print the host virtual address at which the guest's physical address *addr*
  is mapped.
ERST

#ifdef CONFIG_LINUX
    {
        .name       = "gpa2hpa",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the host physical address corresponding to a guest physical address",
        .cmd        = hmp_gpa2hpa,
    },
#endif

SRST
``gpa2hpa`` *addr*
  Print the host physical address at which the guest's physical address *addr*
  is mapped.
ERST

    {
        .name       = "gva2gpa",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the guest physical address corresponding to a guest virtual address",
        .cmd        = hmp_gva2gpa,
    },

SRST
``gva2gpa`` *addr*
  Print the guest physical address at which the guest's virtual address *addr*
  is mapped based on the mapping for the current CPU.
ERST

    {
        .name       = "print|p",
        .args_type  = "fmt:/,val:l",
        .params     = "/fmt expr",
        .help       = "print expression value (use $reg for CPU register access)",
        .cmd        = do_print,
    },

SRST
``print`` or ``p/``\ *fmt* *expr*
  Print expression value. Only the *format* part of *fmt* is
  used.
ERST

    {
        .name       = "i",
        .args_type  = "fmt:/,addr:i,index:i.",
        .params     = "/fmt addr",
        .help       = "I/O port read",
        .cmd        = hmp_ioport_read,
    },

SRST
``i/``\ *fmt* *addr* [.\ *index*\ ]
  Read I/O port.
ERST

    {
        .name       = "o",
        .args_type  = "fmt:/,addr:i,val:i",
        .params     = "/fmt addr value",
        .help       = "I/O port write",
        .cmd        = hmp_ioport_write,
    },

SRST
``o/``\ *fmt* *addr* *val*
  Write to I/O port.
ERST

    {
        .name       = "sendkey",
        .args_type  = "keys:s,hold-time:i?",
        .params     = "keys [hold_ms]",
        .help       = "send keys to the VM (e.g. 'sendkey ctrl-alt-f1', default hold time=100 ms)",
        .cmd        = hmp_sendkey,
        .command_completion = sendkey_completion,
    },

SRST
``sendkey`` *keys*
  Send *keys* to the guest. *keys* could be the name of the
  key or the raw value in hexadecimal format. Use ``-`` to press
  several keys simultaneously. Example::

    sendkey ctrl-alt-f1

  This command is useful to send keys that your graphical user interface
  intercepts at low level, such as ``ctrl-alt-f1`` in X Window.
ERST
    {
        .name       = "sync-profile",
        .args_type  = "op:s?",
        .params     = "[on|off|reset]",
        .help       = "enable, disable or reset synchronization profiling. "
                      "With no arguments, prints whether profiling is on or off.",
        .cmd        = hmp_sync_profile,
    },

SRST
``sync-profile [on|off|reset]``
  Enable, disable or reset synchronization profiling. With no arguments, prints
  whether profiling is on or off.
ERST

    {
        .name       = "system_reset",
        .args_type  = "",
        .params     = "",
        .help       = "reset the system",
        .cmd        = hmp_system_reset,
    },

SRST
``system_reset``
  Reset the system.
ERST

    {
        .name       = "system_powerdown",
        .args_type  = "",
        .params     = "",
        .help       = "send system power down event",
        .cmd        = hmp_system_powerdown,
    },

SRST
``system_powerdown``
  Power down the system (if supported).
ERST

    {
        .name       = "sum",
        .args_type  = "start:i,size:i",
        .params     = "addr size",
        .help       = "compute the checksum of a memory region",
        .cmd        = hmp_sum,
    },

SRST
``sum`` *addr* *size*
  Compute the checksum of a memory region.
ERST

    {
        .name       = "device_add",
        .args_type  = "device:O",
        .params     = "driver[,prop=value][,...]",
        .help       = "add device, like -device on the command line",
        .cmd        = hmp_device_add,
        .command_completion = device_add_completion,
    },

SRST
``device_add`` *config*
  Add device.
ERST

    {
        .name       = "device_del",
        .args_type  = "id:s",
        .params     = "device",
        .help       = "remove device",
        .cmd        = hmp_device_del,
        .command_completion = device_del_completion,
    },

SRST
``device_del`` *id*
  Remove device *id*. *id* may be a short ID
  or a QOM object path.
ERST

    {
        .name       = "cpu",
        .args_type  = "index:i",
        .params     = "index",
        .help       = "set the default CPU",
        .cmd        = hmp_cpu,
    },

SRST
``cpu`` *index*
  Set the default CPU.
ERST

    {
        .name       = "mouse_move",
        .args_type  = "dx_str:s,dy_str:s,dz_str:s?",
        .params     = "dx dy [dz]",
        .help       = "send mouse move events",
        .cmd        = hmp_mouse_move,
    },

SRST
``mouse_move`` *dx* *dy* [*dz*]
  Move the active mouse to the specified coordinates *dx* *dy*
  with optional scroll axis *dz*.
ERST

    {
        .name       = "mouse_button",
        .args_type  = "button_state:i",
        .params     = "state",
        .help       = "change mouse button state (1=L, 2=M, 4=R)",
        .cmd        = hmp_mouse_button,
    },

SRST
``mouse_button`` *val*
  Change the active mouse button state *val* (1=L, 2=M, 4=R).
ERST

    {
        .name       = "mouse_set",
        .args_type  = "index:i",
        .params     = "index",
        .help       = "set which mouse device receives events",
        .cmd        = hmp_mouse_set,
    },

SRST
``mouse_set`` *index*
  Set which mouse device receives events at given *index*, index
  can be obtained with::

    info mice

ERST

    {
        .name       = "wavcapture",
        .args_type  = "path:F,audiodev:s,freq:i?,bits:i?,nchannels:i?",
        .params     = "path audiodev [frequency [bits [channels]]]",
        .help       = "capture audio to a wave file (default frequency=44100 bits=16 channels=2)",
        .cmd        = hmp_wavcapture,
    },
SRST
``wavcapture`` *filename* *audiodev* [*frequency* [*bits* [*channels*]]]
  Capture audio into *filename* from *audiodev*, using sample rate
  *frequency* bits per sample *bits* and number of channels
  *channels*.

  Defaults:

  - Sample rate = 44100 Hz - CD quality
  - Bits = 16
  - Number of channels = 2 - Stereo
ERST

    {
        .name       = "stopcapture",
        .args_type  = "n:i",
        .params     = "capture index",
        .help       = "stop capture",
        .cmd        = hmp_stopcapture,
    },
SRST
``stopcapture`` *index*
  Stop capture with a given *index*, index can be obtained with::

    info capture

ERST

    {
        .name       = "memsave",
        .args_type  = "val:l,size:i,filename:s",
        .params     = "addr size file",
        .help       = "save to disk virtual memory dump starting at 'addr' of size 'size'",
        .cmd        = hmp_memsave,
    },

SRST
``memsave`` *addr* *size* *file*
  save to disk virtual memory dump starting at *addr* of size *size*.
ERST

    {
        .name       = "pmemsave",
        .args_type  = "val:l,size:i,filename:s",
        .params     = "addr size file",
        .help       = "save to disk physical memory dump starting at 'addr' of size 'size'",
        .cmd        = hmp_pmemsave,
    },

SRST
``pmemsave`` *addr* *size* *file*
  save to disk physical memory dump starting at *addr* of size *size*.
ERST

    {
        .name       = "boot_set",
        .args_type  = "bootdevice:s",
        .params     = "bootdevice",
        .help       = "define new values for the boot device list",
        .cmd        = hmp_boot_set,
    },

SRST
``boot_set`` *bootdevicelist*
  Define new values for the boot device list. Those values will override
  the values specified on the command line through the ``-boot`` option.

  The values that can be specified here depend on the machine type, but are
  the same that can be specified in the ``-boot`` command line option.
ERST

    {
        .name       = "nmi",
        .args_type  = "",
        .params     = "",
        .help       = "inject an NMI",
        .cmd        = hmp_nmi,
    },
SRST
``nmi`` *cpu*
  Inject an NMI on the default CPU (x86/s390) or all CPUs (ppc64).
ERST

    {
        .name       = "ringbuf_write",
        .args_type  = "device:s,data:s",
        .params     = "device data",
        .help       = "Write to a ring buffer character device",
        .cmd        = hmp_ringbuf_write,
        .command_completion = ringbuf_write_completion,
    },

SRST
``ringbuf_write`` *device* *data*
  Write *data* to ring buffer character device *device*.
  *data* must be a UTF-8 string.
ERST

    {
        .name       = "ringbuf_read",
        .args_type  = "device:s,size:i",
        .params     = "device size",
        .help       = "Read from a ring buffer character device",
        .cmd        = hmp_ringbuf_read,
        .command_completion = ringbuf_write_completion,
    },

SRST
``ringbuf_read`` *device*
  Read and print up to *size* bytes from ring buffer character
  device *device*.
  Certain non-printable characters are printed ``\uXXXX``, where ``XXXX`` is the
  character code in hexadecimal.  Character ``\`` is printed ``\\``.
  Bug: can screw up when the buffer contains invalid UTF-8 sequences,
  NUL characters, after the ring buffer lost data, and when reading
  stops because the size limit is reached.
ERST

    {
        .name       = "announce_self",
        .args_type  = "interfaces:s?,id:s?",
        .params     = "[interfaces] [id]",
        .help       = "Trigger GARP/RARP announcements",
        .cmd        = hmp_announce_self,
    },

SRST
``announce_self``
  Trigger a round of GARP/RARP broadcasts; this is useful for explicitly
  updating the network infrastructure after a reconfiguration or some forms
  of migration. The timings of the round are set by the migration announce
  parameters. An optional comma separated *interfaces* list restricts the
  announce to the named set of interfaces. An optional *id* can be used to
  start a separate announce timer and to change the parameters of it later.
ERST

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


SRST
``migrate [-d] [-b] [-i]`` *uri*
  Migrate to *uri* (using -d to not wait for completion).

  ``-b``
    for migration with full copy of disk
  ``-i``
    for migration with incremental copy of disk (base image is shared)
ERST

    {
        .name       = "migrate_cancel",
        .args_type  = "",
        .params     = "",
        .help       = "cancel the current VM migration",
        .cmd        = hmp_migrate_cancel,
    },

SRST
``migrate_cancel``
  Cancel the current VM migration.
ERST

    {
        .name       = "migrate_continue",
        .args_type  = "state:s",
        .params     = "state",
        .help       = "Continue migration from the given paused state",
        .cmd        = hmp_migrate_continue,
    },
SRST
``migrate_continue`` *state*
  Continue migration from the paused state *state*
ERST

    {
        .name       = "migrate_incoming",
        .args_type  = "uri:s",
        .params     = "uri",
        .help       = "Continue an incoming migration from an -incoming defer",
        .cmd        = hmp_migrate_incoming,
    },

SRST
``migrate_incoming`` *uri*
  Continue an incoming migration using the *uri* (that has the same syntax
  as the ``-incoming`` option).
ERST

    {
        .name       = "migrate_recover",
        .args_type  = "uri:s",
        .params     = "uri",
        .help       = "Continue a paused incoming postcopy migration",
        .cmd        = hmp_migrate_recover,
    },

SRST
``migrate_recover`` *uri*
  Continue a paused incoming postcopy migration using the *uri*.
ERST

    {
        .name       = "migrate_pause",
        .args_type  = "",
        .params     = "",
        .help       = "Pause an ongoing migration (postcopy-only)",
        .cmd        = hmp_migrate_pause,
    },

SRST
``migrate_pause``
  Pause an ongoing migration.  Currently it only supports postcopy.
ERST

    {
        .name       = "migrate_set_capability",
        .args_type  = "capability:s,state:b",
        .params     = "capability state",
        .help       = "Enable/Disable the usage of a capability for migration",
        .cmd        = hmp_migrate_set_capability,
        .command_completion = migrate_set_capability_completion,
    },

SRST
``migrate_set_capability`` *capability* *state*
  Enable/Disable the usage of a capability *capability* for migration.
ERST

    {
        .name       = "migrate_set_parameter",
        .args_type  = "parameter:s,value:s",
        .params     = "parameter value",
        .help       = "Set the parameter for migration",
        .cmd        = hmp_migrate_set_parameter,
        .command_completion = migrate_set_parameter_completion,
    },

SRST
``migrate_set_parameter`` *parameter* *value*
  Set the parameter *parameter* for migration.
ERST

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

SRST
``migrate_start_postcopy``
  Switch in-progress migration to postcopy mode. Ignored after the end of
  migration (or once already in postcopy).
ERST

    {
        .name       = "x_colo_lost_heartbeat",
        .args_type  = "",
        .params     = "",
        .help       = "Tell COLO that heartbeat is lost,\n\t\t\t"
                      "a failover or takeover is needed.",
        .cmd = hmp_x_colo_lost_heartbeat,
    },

SRST
``x_colo_lost_heartbeat``
  Tell COLO that heartbeat is lost, a failover or takeover is needed.
ERST

    {
        .name       = "client_migrate_info",
        .args_type  = "protocol:s,hostname:s,port:i?,tls-port:i?,cert-subject:s?",
        .params     = "protocol hostname port tls-port cert-subject",
        .help       = "set migration information for remote display",
        .cmd        = hmp_client_migrate_info,
    },

SRST
``client_migrate_info`` *protocol* *hostname* *port* *tls-port* *cert-subject*
  Set migration information for remote display.  This makes the server
  ask the client to automatically reconnect using the new parameters
  once migration finished successfully.  Only implemented for SPICE.
ERST

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

SRST
``dump-guest-memory [-p]`` *filename* *begin* *length*
  \ 
``dump-guest-memory [-z|-l|-s|-w]`` *filename*
  Dump guest memory to *protocol*. The file can be processed with crash or
  gdb. Without ``-z|-l|-s|-w``, the dump format is ELF.

  ``-p``
    do paging to get guest's memory mapping.
  ``-z``
    dump in kdump-compressed format, with zlib compression.
  ``-l``
    dump in kdump-compressed format, with lzo compression.
  ``-s``
    dump in kdump-compressed format, with snappy compression.
  ``-w``
    dump in Windows crashdump format (can be used instead of ELF-dump converting),
    for Windows x64 guests with vmcoreinfo driver only
  *filename*
    dump file name.
  *begin*
    the starting physical address. It's optional, and should be
    specified together with *length*.
  *length*
    the memory size, in bytes. It's optional, and should be specified
    together with *begin*.

ERST

#if defined(TARGET_S390X)
    {
        .name       = "dump-skeys",
        .args_type  = "filename:F",
        .params     = "",
        .help       = "Save guest storage keys into file 'filename'.\n",
        .cmd        = hmp_dump_skeys,
    },
#endif

SRST
``dump-skeys`` *filename*
  Save guest storage keys to a file.
ERST

#if defined(TARGET_S390X)
    {
        .name       = "migration_mode",
        .args_type  = "mode:i",
        .params     = "mode",
        .help       = "Enables or disables migration mode\n",
        .cmd        = hmp_migrationmode,
    },
#endif

SRST
``migration_mode`` *mode*
  Enables or disables migration mode.
ERST

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

SRST
``snapshot_blkdev``
  Snapshot device, using snapshot file as target if provided
ERST

    {
        .name       = "snapshot_blkdev_internal",
        .args_type  = "device:B,name:s",
        .params     = "device name",
        .help       = "take an internal snapshot of device.\n\t\t\t"
                      "The format of the image used by device must\n\t\t\t"
                      "support it, such as qcow2.\n\t\t\t",
        .cmd        = hmp_snapshot_blkdev_internal,
    },

SRST
``snapshot_blkdev_internal``
  Take an internal snapshot on device if it support
ERST

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

SRST
``snapshot_delete_blkdev_internal``
  Delete an internal snapshot on device if it support
ERST

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
SRST
``drive_mirror``
  Start mirroring a block device's writes to a new destination,
  using the specified target.
ERST

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
SRST
``drive_backup``
  Start a point-in-time copy of a block device to a specified target.
ERST

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

SRST
``drive_add``
  Add drive to PCI storage controller.
ERST

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
                      "<tlp header> = 32bit x 4\n\t\t\t"
                      "<tlp header prefix> = 32bit x 4",
        .cmd        = hmp_pcie_aer_inject_error,
    },

SRST
``pcie_aer_inject_error``
  Inject PCIe AER error
ERST

    {
        .name       = "netdev_add",
        .args_type  = "netdev:O",
        .params     = "[user|tap|socket|vde|bridge|hubport|netmap|vhost-user],id=str[,prop=value][,...]",
        .help       = "add host network device",
        .cmd        = hmp_netdev_add,
        .command_completion = netdev_add_completion,
        .flags      = "p",
    },

SRST
``netdev_add``
  Add host network device.
ERST

    {
        .name       = "netdev_del",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "remove host network device",
        .cmd        = hmp_netdev_del,
        .command_completion = netdev_del_completion,
        .flags      = "p",
    },

SRST
``netdev_del``
  Remove host network device.
ERST

    {
        .name       = "object_add",
        .args_type  = "object:S",
        .params     = "[qom-type=]type,id=str[,prop=value][,...]",
        .help       = "create QOM object",
        .cmd        = hmp_object_add,
        .command_completion = object_add_completion,
        .flags      = "p",
    },

SRST
``object_add``
  Create QOM object.
ERST

    {
        .name       = "object_del",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "destroy QOM object",
        .cmd        = hmp_object_del,
        .command_completion = object_del_completion,
        .flags      = "p",
    },

SRST
``object_del``
  Destroy QOM object.
ERST

#ifdef CONFIG_SLIRP
    {
        .name       = "hostfwd_add",
        .args_type  = "arg1:s,arg2:s?",
        .params     = "[netdev_id] [tcp|udp]:[hostaddr]:hostport-[guestaddr]:guestport",
        .help       = "redirect TCP or UDP connections from host to guest (requires -net user)",
        .cmd        = hmp_hostfwd_add,
    },
#endif
SRST
``hostfwd_add``
  Redirect TCP or UDP connections from host to guest (requires -net user).
ERST

#ifdef CONFIG_SLIRP
    {
        .name       = "hostfwd_remove",
        .args_type  = "arg1:s,arg2:s?",
        .params     = "[netdev_id] [tcp|udp]:[hostaddr]:hostport",
        .help       = "remove host-to-guest TCP or UDP redirection",
        .cmd        = hmp_hostfwd_remove,
    },

#endif
SRST
``hostfwd_remove``
  Remove host-to-guest TCP or UDP redirection.
ERST

    {
        .name       = "balloon",
        .args_type  = "value:M",
        .params     = "target",
        .help       = "request VM to change its memory allocation (in MB)",
        .cmd        = hmp_balloon,
    },

SRST
``balloon`` *value*
  Request VM to change its memory allocation to *value* (in MB).
ERST

    {
        .name       = "set_link",
        .args_type  = "name:s,up:b",
        .params     = "name on|off",
        .help       = "change the link status of a network adapter",
        .cmd        = hmp_set_link,
        .command_completion = set_link_completion,
    },

SRST
``set_link`` *name* ``[on|off]``
  Switch link *name* on (i.e. up) or off (i.e. down).
ERST

    {
        .name       = "watchdog_action",
        .args_type  = "action:s",
        .params     = "[reset|shutdown|poweroff|pause|debug|none]",
        .help       = "change watchdog action",
        .cmd        = hmp_watchdog_action,
        .command_completion = watchdog_action_completion,
    },

SRST
``watchdog_action``
  Change watchdog action.
ERST

    {
        .name       = "nbd_server_start",
        .args_type  = "all:-a,writable:-w,uri:s",
        .params     = "nbd_server_start [-a] [-w] host:port",
        .help       = "serve block devices on the given host and port",
        .cmd        = hmp_nbd_server_start,
    },
SRST
``nbd_server_start`` *host*:*port*
  Start an NBD server on the given host and/or port.  If the ``-a``
  option is included, all of the virtual machine's block devices that
  have an inserted media on them are automatically exported; in this case,
  the ``-w`` option makes the devices writable too.
ERST

    {
        .name       = "nbd_server_add",
        .args_type  = "writable:-w,device:B,name:s?",
        .params     = "nbd_server_add [-w] device [name]",
        .help       = "export a block device via NBD",
        .cmd        = hmp_nbd_server_add,
    },
SRST
``nbd_server_add`` *device* [ *name* ]
  Export a block device through QEMU's NBD server, which must be started
  beforehand with ``nbd_server_start``.  The ``-w`` option makes the
  exported device writable too.  The export name is controlled by *name*,
  defaulting to *device*.
ERST

    {
        .name       = "nbd_server_remove",
        .args_type  = "force:-f,name:s",
        .params     = "nbd_server_remove [-f] name",
        .help       = "remove an export previously exposed via NBD",
        .cmd        = hmp_nbd_server_remove,
    },
SRST
``nbd_server_remove [-f]`` *name*
  Stop exporting a block device through QEMU's NBD server, which was
  previously started with ``nbd_server_add``.  The ``-f``
  option forces the server to drop the export immediately even if
  clients are connected; otherwise the command fails unless there are no
  clients.
ERST

    {
        .name       = "nbd_server_stop",
        .args_type  = "",
        .params     = "nbd_server_stop",
        .help       = "stop serving block devices using the NBD protocol",
        .cmd        = hmp_nbd_server_stop,
    },
SRST
``nbd_server_stop``
  Stop the QEMU embedded NBD server.
ERST


#if defined(TARGET_I386)

    {
        .name       = "mce",
        .args_type  = "broadcast:-b,cpu_index:i,bank:i,status:l,mcg_status:l,addr:l,misc:l",
        .params     = "[-b] cpu bank status mcgstatus addr misc",
        .help       = "inject a MCE on the given CPU [and broadcast to other CPUs with -b option]",
        .cmd        = hmp_mce,
    },

#endif
SRST
``mce`` *cpu* *bank* *status* *mcgstatus* *addr* *misc*
  Inject an MCE on the given CPU (x86 only).
ERST

    {
        .name       = "getfd",
        .args_type  = "fdname:s",
        .params     = "getfd name",
        .help       = "receive a file descriptor via SCM rights and assign it a name",
        .cmd        = hmp_getfd,
    },

SRST
``getfd`` *fdname*
  If a file descriptor is passed alongside this command using the SCM_RIGHTS
  mechanism on unix sockets, it is stored using the name *fdname* for
  later use by other monitor commands.
ERST

    {
        .name       = "closefd",
        .args_type  = "fdname:s",
        .params     = "closefd name",
        .help       = "close a file descriptor previously passed via SCM rights",
        .cmd        = hmp_closefd,
    },

SRST
``closefd`` *fdname*
  Close the file descriptor previously assigned to *fdname* using the
  ``getfd`` command. This is only needed if the file descriptor was never
  used by another monitor command.
ERST

    {
        .name       = "block_set_io_throttle",
        .args_type  = "device:B,bps:l,bps_rd:l,bps_wr:l,iops:l,iops_rd:l,iops_wr:l",
        .params     = "device bps bps_rd bps_wr iops iops_rd iops_wr",
        .help       = "change I/O throttle limits for a block drive",
        .cmd        = hmp_block_set_io_throttle,
    },

SRST
``block_set_io_throttle`` *device* *bps* *bps_rd* *bps_wr* *iops* *iops_rd* *iops_wr*
  Change I/O throttle limits for a block drive to
  *bps* *bps_rd* *bps_wr* *iops* *iops_rd* *iops_wr*.
  *device* can be a block device name, a qdev ID or a QOM path.
ERST

    {
        .name       = "set_password",
        .args_type  = "protocol:s,password:s,connected:s?",
        .params     = "protocol password action-if-connected",
        .help       = "set spice/vnc password",
        .cmd        = hmp_set_password,
    },

SRST
``set_password [ vnc | spice ] password [ action-if-connected ]``
  Change spice/vnc password.  *action-if-connected* specifies what
  should happen in case a connection is established: *fail* makes the
  password change fail.  *disconnect* changes the password and
  disconnects the client.  *keep* changes the password and keeps the
  connection up.  *keep* is the default.
ERST

    {
        .name       = "expire_password",
        .args_type  = "protocol:s,time:s",
        .params     = "protocol time",
        .help       = "set spice/vnc password expire-time",
        .cmd        = hmp_expire_password,
    },

SRST
``expire_password [ vnc | spice ]`` *expire-time*
  Specify when a password for spice/vnc becomes
  invalid. *expire-time* accepts:

  ``now``
    Invalidate password instantly.
  ``never``
    Password stays valid forever.
  ``+``\ *nsec*
    Password stays valid for *nsec* seconds starting now.
  *nsec*
    Password is invalidated at the given time.  *nsec* are the seconds
    passed since 1970, i.e. unix epoch.

ERST

    {
        .name       = "chardev-add",
        .args_type  = "args:s",
        .params     = "args",
        .help       = "add chardev",
        .cmd        = hmp_chardev_add,
        .command_completion = chardev_add_completion,
    },

SRST
``chardev-add`` *args*
  chardev-add accepts the same parameters as the -chardev command line switch.
ERST

    {
        .name       = "chardev-change",
        .args_type  = "id:s,args:s",
        .params     = "id args",
        .help       = "change chardev",
        .cmd        = hmp_chardev_change,
    },

SRST
``chardev-change`` *args*
  chardev-change accepts existing chardev *id* and then the same arguments
  as the -chardev command line switch (except for "id").
ERST

    {
        .name       = "chardev-remove",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "remove chardev",
        .cmd        = hmp_chardev_remove,
        .command_completion = chardev_remove_completion,
    },

SRST
``chardev-remove`` *id*
  Removes the chardev *id*.
ERST

    {
        .name       = "chardev-send-break",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "send a break on chardev",
        .cmd        = hmp_chardev_send_break,
        .command_completion = chardev_remove_completion,
    },

SRST
``chardev-send-break`` *id*
  Send a break on the chardev *id*.
ERST

    {
        .name       = "qemu-io",
        .args_type  = "qdev:-d,device:B,command:s",
        .params     = "[-d] [device] \"[command]\"",
        .help       = "run a qemu-io command on a block device\n\t\t\t"
                      "-d: [device] is a device ID rather than a "
                      "drive ID or node name",
        .cmd        = hmp_qemu_io,
    },

SRST
``qemu-io`` *device* *command*
  Executes a qemu-io command on the given block device.
ERST

    {
        .name       = "qom-list",
        .args_type  = "path:s?",
        .params     = "path",
        .help       = "list QOM properties",
        .cmd        = hmp_qom_list,
        .flags      = "p",
    },

SRST
``qom-list`` [*path*]
  Print QOM properties of object at location *path*
ERST

    {
        .name       = "qom-get",
        .args_type  = "path:s,property:s",
        .params     = "path property",
        .help       = "print QOM property",
        .cmd        = hmp_qom_get,
        .flags      = "p",
    },

SRST
``qom-get`` *path* *property*
  Print QOM property *property* of object at location *path*
ERST

    {
        .name       = "qom-set",
        .args_type  = "json:-j,path:s,property:s,value:S",
        .params     = "[-j] path property value",
        .help       = "set QOM property.\n\t\t\t"
                      "-j: the value is specified in json format.",
        .cmd        = hmp_qom_set,
        .flags      = "p",
    },

SRST
``qom-set`` *path* *property* *value*
  Set QOM property *property* of object at location *path* to value *value*
ERST

    {
        .name       = "replay_break",
        .args_type  = "icount:l",
        .params     = "icount",
        .help       = "set breakpoint at the specified instruction count",
        .cmd        = hmp_replay_break,
    },

SRST
``replay_break`` *icount*
  Set replay breakpoint at instruction count *icount*.
  Execution stops when the specified instruction is reached.
  There can be at most one breakpoint. When breakpoint is set, any prior
  one is removed.  The breakpoint may be set only in replay mode and only
  "in the future", i.e. at instruction counts greater than the current one.
  The current instruction count can be observed with ``info replay``.
ERST

    {
        .name       = "replay_delete_break",
        .args_type  = "",
        .params     = "",
        .help       = "remove replay breakpoint",
        .cmd        = hmp_replay_delete_break,
    },

SRST
``replay_delete_break``
  Remove replay breakpoint which was previously set with ``replay_break``.
  The command is ignored when there are no replay breakpoints.
ERST

    {
        .name       = "replay_seek",
        .args_type  = "icount:l",
        .params     = "icount",
        .help       = "replay execution to the specified instruction count",
        .cmd        = hmp_replay_seek,
    },

SRST
``replay_seek`` *icount*
  Automatically proceed to the instruction count *icount*, when
  replaying the execution. The command automatically loads nearest
  snapshot and replays the execution to find the desired instruction.
  When there is no preceding snapshot or the execution is not replayed,
  then the command fails.
  *icount* for the reference may be observed with ``info replay`` command.
ERST

    {
        .name       = "info",
        .args_type  = "item:s?",
        .params     = "[subcommand]",
        .help       = "show various information about the system state",
        .cmd        = hmp_info_help,
        .sub_table  = hmp_info_cmds,
        .flags      = "p",
    },

SRST
``calc_dirty_rate`` *second*
  Start a round of dirty rate measurement with the period specified in *second*.
  The result of the dirty rate measurement may be observed with ``info
  dirty_rate`` command.
ERST

    {
        .name       = "calc_dirty_rate",
        .args_type  = "dirty_ring:-r,dirty_bitmap:-b,second:l,sample_pages_per_GB:l?",
        .params     = "[-r] [-b] second [sample_pages_per_GB]",
        .help       = "start a round of guest dirty rate measurement (using -r to"
                      "\n\t\t\t specify dirty ring as the method of calculation and"
                      "\n\t\t\t -b to specify dirty bitmap as method of calculation)",
        .cmd        = hmp_calc_dirty_rate,
    },

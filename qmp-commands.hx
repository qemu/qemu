HXCOMM QMP dispatch table and documentation
HXCOMM Text between SQMP and EQMP is copied to the QMP documention file and
HXCOMM does not show up in the other formats.

SQMP
                        QMP Supported Commands
                        ----------------------

This document describes all commands currently supported by QMP.

Most of the time their usage is exactly the same as in the user Monitor, this
means that any other document which also describe commands (the manpage,
QEMU's manual, etc) can and should be consulted.

QMP has two types of commands: regular and query commands. Regular commands
usually change the Virtual Machine's state someway, while query commands just
return information. The sections below are divided accordingly.

It's important to observe that all communication examples are formatted in
a reader-friendly way, so that they're easier to understand. However, in real
protocol usage, they're emitted as a single line.

Also, the following notation is used to denote data flow:

-> data issued by the Client
<- Server data response

Please, refer to the QMP specification (QMP/qmp-spec.txt) for detailed
information on the Server command and response formats.

NOTE: This document is temporary and will be replaced soon.

1. Stability Considerations
===========================

The current QMP command set (described in this file) may be useful for a
number of use cases, however it's limited and several commands have bad
defined semantics, specially with regard to command completion.

These problems are going to be solved incrementally in the next QEMU releases
and we're going to establish a deprecation policy for badly defined commands.

If you're planning to adopt QMP, please observe the following:

    1. The deprecation policy will take effect and be documented soon, please
       check the documentation of each used command as soon as a new release of
       QEMU is available

    2. DO NOT rely on anything which is not explicit documented

    3. Errors, in special, are not documented. Applications should NOT check
       for specific errors classes or data (it's strongly recommended to only
       check for the "error" key)

2. Regular Commands
===================

Server's responses in the examples below are always a success response, please
refer to the QMP specification for more details on error responses.

EQMP

    {
        .name       = "quit",
        .args_type  = "",
        .params     = "",
        .help       = "quit the emulator",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_quit,
    },

SQMP
quit
----

Quit the emulator.

Arguments: None.

Example:

-> { "execute": "quit" }
<- { "return": {} }

EQMP

    {
        .name       = "eject",
        .args_type  = "force:-f,device:B",
        .params     = "[-f] device",
        .help       = "eject a removable medium (use -f to force it)",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_eject,
    },

SQMP
eject
-----

Eject a removable medium.

Arguments: 

- force: force ejection (json-bool, optional)
- device: device name (json-string)

Example:

-> { "execute": "eject", "arguments": { "device": "ide1-cd0" } }
<- { "return": {} }

Note: The "force" argument defaults to false.

EQMP

    {
        .name       = "change",
        .args_type  = "device:B,target:F,arg:s?",
        .params     = "device filename [format]",
        .help       = "change a removable medium, optional format",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_change,
    },

SQMP
change
------

Change a removable medium or VNC configuration.

Arguments:

- "device": device name (json-string)
- "target": filename or item (json-string)
- "arg": additional argument (json-string, optional)

Examples:

1. Change a removable medium

-> { "execute": "change",
             "arguments": { "device": "ide1-cd0",
                            "target": "/srv/images/Fedora-12-x86_64-DVD.iso" } }
<- { "return": {} }

2. Change VNC password

-> { "execute": "change",
             "arguments": { "device": "vnc", "target": "password",
                            "arg": "foobar1" } }
<- { "return": {} }

EQMP

    {
        .name       = "screendump",
        .args_type  = "filename:F",
        .params     = "filename",
        .help       = "save screen into PPM image 'filename'",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_screen_dump,
    },

SQMP
screendump
----------

Save screen into PPM image.

Arguments:

- "filename": file path (json-string)

Example:

-> { "execute": "screendump", "arguments": { "filename": "/tmp/image" } }
<- { "return": {} }

EQMP

    {
        .name       = "stop",
        .args_type  = "",
        .params     = "",
        .help       = "stop emulation",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_stop,
    },

SQMP
stop
----

Stop the emulator.

Arguments: None.

Example:

-> { "execute": "stop" }
<- { "return": {} }

EQMP

    {
        .name       = "cont",
        .args_type  = "",
        .params     = "",
        .help       = "resume emulation",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_cont,
    },

SQMP
cont
----

Resume emulation.

Arguments: None.

Example:

-> { "execute": "cont" }
<- { "return": {} }

EQMP

    {
        .name       = "system_reset",
        .args_type  = "",
        .params     = "",
        .help       = "reset the system",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_system_reset,
    },

SQMP
system_reset
------------

Reset the system.

Arguments: None.

Example:

-> { "execute": "system_reset" }
<- { "return": {} }

EQMP

    {
        .name       = "system_powerdown",
        .args_type  = "",
        .params     = "",
        .help       = "send system power down event",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_system_powerdown,
    },

SQMP
system_powerdown
----------------

Send system power down event.

Arguments: None.

Example:

-> { "execute": "system_powerdown" }
<- { "return": {} }

EQMP

    {
        .name       = "device_add",
        .args_type  = "device:O",
        .params     = "driver[,prop=value][,...]",
        .help       = "add device, like -device on the command line",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_device_add,
    },

SQMP
device_add
----------

Add a device.

Arguments:

- "driver": the name of the new device's driver (json-string)
- "bus": the device's parent bus (device tree path, json-string, optional)
- "id": the device's ID, must be unique (json-string)
- device properties

Example:

-> { "execute": "device_add", "arguments": { "driver": "e1000", "id": "net1" } }
<- { "return": {} }

Notes:

(1) For detailed information about this command, please refer to the
    'docs/qdev-device-use.txt' file.

(2) It's possible to list device properties by running QEMU with the
    "-device DEVICE,\?" command-line argument, where DEVICE is the device's name

EQMP

    {
        .name       = "device_del",
        .args_type  = "id:s",
        .params     = "device",
        .help       = "remove device",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_device_del,
    },

SQMP
device_del
----------

Remove a device.

Arguments:

- "id": the device's ID (json-string)

Example:

-> { "execute": "device_del", "arguments": { "id": "net1" } }
<- { "return": {} }

EQMP

    {
        .name       = "cpu",
        .args_type  = "index:i",
        .params     = "index",
        .help       = "set the default CPU",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_cpu_set,
    },

SQMP
cpu
---

Set the default CPU.

Arguments:

- "index": the CPU's index (json-int)

Example:

-> { "execute": "cpu", "arguments": { "index": 0 } }
<- { "return": {} }

Note: CPUs' indexes are obtained with the 'query-cpus' command.

EQMP

    {
        .name       = "memsave",
        .args_type  = "val:l,size:i,filename:s",
        .params     = "addr size file",
        .help       = "save to disk virtual memory dump starting at 'addr' of size 'size'",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_memory_save,
    },

SQMP
memsave
-------

Save to disk virtual memory dump starting at 'val' of size 'size'.

Arguments:

- "val": the starting address (json-int)
- "size": the memory size, in bytes (json-int)
- "filename": file path (json-string)

Example:

-> { "execute": "memsave",
             "arguments": { "val": 10,
                            "size": 100,
                            "filename": "/tmp/virtual-mem-dump" } }
<- { "return": {} }

Note: Depends on the current CPU.

EQMP

    {
        .name       = "pmemsave",
        .args_type  = "val:l,size:i,filename:s",
        .params     = "addr size file",
        .help       = "save to disk physical memory dump starting at 'addr' of size 'size'",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_physical_memory_save,
    },

SQMP
pmemsave
--------

Save to disk physical memory dump starting at 'val' of size 'size'.

Arguments:

- "val": the starting address (json-int)
- "size": the memory size, in bytes (json-int)
- "filename": file path (json-string)

Example:

-> { "execute": "pmemsave",
             "arguments": { "val": 10,
                            "size": 100,
                            "filename": "/tmp/physical-mem-dump" } }
<- { "return": {} }

EQMP

    {
        .name       = "inject-nmi",
        .args_type  = "",
        .params     = "",
        .help       = "",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_inject_nmi,
    },

SQMP
inject-nmi
----------

Inject an NMI on guest's CPUs.

Arguments: None.

Example:

-> { "execute": "inject-nmi" }
<- { "return": {} }

Note: inject-nmi is only supported for x86 guest currently, it will
      returns "Unsupported" error for non-x86 guest.

EQMP

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

SQMP
migrate
-------

Migrate to URI.

Arguments:

- "blk": block migration, full disk copy (json-bool, optional)
- "inc": incremental disk copy (json-bool, optional)
- "uri": Destination URI (json-string)

Example:

-> { "execute": "migrate", "arguments": { "uri": "tcp:0:4446" } }
<- { "return": {} }

Notes:

(1) The 'query-migrate' command should be used to check migration's progress
    and final result (this information is provided by the 'status' member)
(2) All boolean arguments default to false
(3) The user Monitor's "detach" argument is invalid in QMP and should not
    be used

EQMP

    {
        .name       = "migrate_cancel",
        .args_type  = "",
        .params     = "",
        .help       = "cancel the current VM migration",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_migrate_cancel,
    },

SQMP
migrate_cancel
--------------

Cancel the current migration.

Arguments: None.

Example:

-> { "execute": "migrate_cancel" }
<- { "return": {} }

EQMP

    {
        .name       = "migrate_set_speed",
        .args_type  = "value:o",
        .params     = "value",
        .help       = "set maximum speed (in bytes) for migrations",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_migrate_set_speed,
    },

SQMP
migrate_set_speed
-----------------

Set maximum speed for migrations.

Arguments:

- "value": maximum speed, in bytes per second (json-int)

Example:

-> { "execute": "migrate_set_speed", "arguments": { "value": 1024 } }
<- { "return": {} }

EQMP

    {
        .name       = "migrate_set_downtime",
        .args_type  = "value:T",
        .params     = "value",
        .help       = "set maximum tolerated downtime (in seconds) for migrations",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_migrate_set_downtime,
    },

SQMP
migrate_set_downtime
--------------------

Set maximum tolerated downtime (in seconds) for migrations.

Arguments:

- "value": maximum downtime (json-number)

Example:

-> { "execute": "migrate_set_downtime", "arguments": { "value": 0.1 } }
<- { "return": {} }

EQMP

    {
        .name       = "client_migrate_info",
        .args_type  = "protocol:s,hostname:s,port:i?,tls-port:i?,cert-subject:s?",
        .params     = "protocol hostname port tls-port cert-subject",
        .help       = "send migration info to spice/vnc client",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = client_migrate_info,
    },

SQMP
client_migrate_info
------------------

Set the spice/vnc connection info for the migration target.  The spice/vnc
server will ask the spice/vnc client to automatically reconnect using the
new parameters (if specified) once the vm migration finished successfully.

Arguments:

- "protocol":     protocol: "spice" or "vnc" (json-string)
- "hostname":     migration target hostname (json-string)
- "port":         spice/vnc tcp port for plaintext channels (json-int, optional)
- "tls-port":     spice tcp port for tls-secured channels (json-int, optional)
- "cert-subject": server certificate subject (json-string, optional)

Example:

-> { "execute": "client_migrate_info",
     "arguments": { "protocol": "spice",
                    "hostname": "virt42.lab.kraxel.org",
                    "port": 1234 } }
<- { "return": {} }

EQMP

    {
        .name       = "netdev_add",
        .args_type  = "netdev:O",
        .params     = "[user|tap|socket],id=str[,prop=value][,...]",
        .help       = "add host network device",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_netdev_add,
    },

SQMP
netdev_add
----------

Add host network device.

Arguments:

- "type": the device type, "tap", "user", ... (json-string)
- "id": the device's ID, must be unique (json-string)
- device options

Example:

-> { "execute": "netdev_add", "arguments": { "type": "user", "id": "netdev1" } }
<- { "return": {} }

Note: The supported device options are the same ones supported by the '-net'
      command-line argument, which are listed in the '-help' output or QEMU's
      manual

EQMP

    {
        .name       = "netdev_del",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "remove host network device",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_netdev_del,
    },

SQMP
netdev_del
----------

Remove host network device.

Arguments:

- "id": the device's ID, must be unique (json-string)

Example:

-> { "execute": "netdev_del", "arguments": { "id": "netdev1" } }
<- { "return": {} }


EQMP

    {
        .name       = "block_resize",
        .args_type  = "device:B,size:o",
        .params     = "device size",
        .help       = "resize a block image",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_block_resize,
    },

SQMP
block_resize
------------

Resize a block image while a guest is running.

Arguments:

- "device": the device's ID, must be unique (json-string)
- "size": new size

Example:

-> { "execute": "block_resize", "arguments": { "device": "scratch", "size": 1073741824 } }
<- { "return": {} }

EQMP

    {
        .name       = "blockdev-snapshot-sync",
        .args_type  = "device:B,snapshot-file:s?,format:s?",
        .params     = "device [new-image-file] [format]",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_snapshot_blkdev,
    },

SQMP
blockdev-snapshot-sync
----------------------

Synchronous snapshot of a block device. snapshot-file specifies the
target of the new image. If the file exists, or if it is a device, the
snapshot will be created in the existing file/device. If does not
exist, a new file will be created. format specifies the format of the
snapshot image, default is qcow2.

Arguments:

- "device": device name to snapshot (json-string)
- "snapshot-file": name of new image file (json-string)
- "format": format of new image (json-string, optional)

Example:

-> { "execute": "blockdev-snapshot", "arguments": { "device": "ide-hd0",
                                                    "snapshot-file":
                                                    "/some/place/my-image",
                                                    "format": "qcow2" } }
<- { "return": {} }

EQMP

    {
        .name       = "balloon",
        .args_type  = "value:M",
        .params     = "target",
        .help       = "request VM to change its memory allocation (in MB)",
        .user_print = monitor_user_noop,
        .mhandler.cmd_async = do_balloon,
        .flags      = MONITOR_CMD_ASYNC,
    },

SQMP
balloon
-------

Request VM to change its memory allocation (in bytes).

Arguments:

- "value": New memory allocation (json-int)

Example:

-> { "execute": "balloon", "arguments": { "value": 536870912 } }
<- { "return": {} }

EQMP

    {
        .name       = "set_link",
        .args_type  = "name:s,up:b",
        .params     = "name on|off",
        .help       = "change the link status of a network adapter",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_set_link,
    },

SQMP
set_link
--------

Change the link status of a network adapter.

Arguments:

- "name": network device name (json-string)
- "up": status is up (json-bool)

Example:

-> { "execute": "set_link", "arguments": { "name": "e1000.0", "up": false } }
<- { "return": {} }

EQMP

    {
        .name       = "getfd",
        .args_type  = "fdname:s",
        .params     = "getfd name",
        .help       = "receive a file descriptor via SCM rights and assign it a name",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_getfd,
    },

SQMP
getfd
-----

Receive a file descriptor via SCM rights and assign it a name.

Arguments:

- "fdname": file descriptor name (json-string)

Example:

-> { "execute": "getfd", "arguments": { "fdname": "fd1" } }
<- { "return": {} }

EQMP

    {
        .name       = "closefd",
        .args_type  = "fdname:s",
        .params     = "closefd name",
        .help       = "close a file descriptor previously passed via SCM rights",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_closefd,
    },

SQMP
closefd
-------

Close a file descriptor previously passed via SCM rights.

Arguments:

- "fdname": file descriptor name (json-string)

Example:

-> { "execute": "closefd", "arguments": { "fdname": "fd1" } }
<- { "return": {} }

EQMP

    {
        .name       = "block_passwd",
        .args_type  = "device:B,password:s",
        .params     = "block_passwd device password",
        .help       = "set the password of encrypted block devices",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_block_set_passwd,
    },

SQMP
block_passwd
------------

Set the password of encrypted block devices.

Arguments:

- "device": device name (json-string)
- "password": password (json-string)

Example:

-> { "execute": "block_passwd", "arguments": { "device": "ide0-hd0",
                                               "password": "12345" } }
<- { "return": {} }

EQMP

    {
        .name       = "set_password",
        .args_type  = "protocol:s,password:s,connected:s?",
        .params     = "protocol password action-if-connected",
        .help       = "set spice/vnc password",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = set_password,
    },

SQMP
set_password
------------

Set the password for vnc/spice protocols.

Arguments:

- "protocol": protocol name (json-string)
- "password": password (json-string)
- "connected": [ keep | disconnect | fail ] (josn-string, optional)

Example:

-> { "execute": "set_password", "arguments": { "protocol": "vnc",
                                               "password": "secret" } }
<- { "return": {} }

EQMP

    {
        .name       = "expire_password",
        .args_type  = "protocol:s,time:s",
        .params     = "protocol time",
        .help       = "set spice/vnc password expire-time",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = expire_password,
    },

SQMP
expire_password
---------------

Set the password expire time for vnc/spice protocols.

Arguments:

- "protocol": protocol name (json-string)
- "time": [ now | never | +secs | secs ] (json-string)

Example:

-> { "execute": "expire_password", "arguments": { "protocol": "vnc",
                                                  "time": "+60" } }
<- { "return": {} }

EQMP

    {
        .name       = "add_client",
        .args_type  = "protocol:s,fdname:s,skipauth:b?",
        .params     = "protocol fdname skipauth",
        .help       = "add a graphics client",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = add_graphics_client,
    },

SQMP
add_client
----------

Add a graphics client

Arguments:

- "protocol": protocol name (json-string)
- "fdname": file descriptor name (json-string)

Example:

-> { "execute": "add_client", "arguments": { "protocol": "vnc",
                                             "fdname": "myclient" } }
<- { "return": {} }

EQMP
    {
        .name       = "qmp_capabilities",
        .args_type  = "",
        .params     = "",
        .help       = "enable QMP capabilities",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_qmp_capabilities,
    },

SQMP
qmp_capabilities
----------------

Enable QMP capabilities.

Arguments: None.

Example:

-> { "execute": "qmp_capabilities" }
<- { "return": {} }

Note: This command must be issued before issuing any other command.

EQMP

    {
        .name       = "human-monitor-command",
        .args_type  = "command-line:s,cpu-index:i?",
        .params     = "",
        .help       = "",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = do_hmp_passthrough,
    },

SQMP
human-monitor-command
---------------------

Execute a Human Monitor command.

Arguments: 

- command-line: the command name and its arguments, just like the
                Human Monitor's shell (json-string)
- cpu-index: select the CPU number to be used by commands which access CPU
             data, like 'info registers'. The Monitor selects CPU 0 if this
             argument is not provided (json-int, optional)

Example:

-> { "execute": "human-monitor-command", "arguments": { "command-line": "info kvm" } }
<- { "return": "kvm support: enabled\r\n" }

Notes:

(1) The Human Monitor is NOT an stable interface, this means that command
    names, arguments and responses can change or be removed at ANY time.
    Applications that rely on long term stability guarantees should NOT
    use this command

(2) Limitations:

    o This command is stateless, this means that commands that depend
      on state information (such as getfd) might not work

    o Commands that prompt the user for data (eg. 'cont' when the block
      device is encrypted) don't currently work

3. Query Commands
=================

HXCOMM Each query command below is inside a SQMP/EQMP section, do NOT change
HXCOMM this! We will possibly move query commands definitions inside those
HXCOMM sections, just like regular commands.

EQMP

SQMP
query-version
-------------

Show QEMU version.

Return a json-object with the following information:

- "qemu": A json-object containing three integer values:
    - "major": QEMU's major version (json-int)
    - "minor": QEMU's minor version (json-int)
    - "micro": QEMU's micro version (json-int)
- "package": package's version (json-string)

Example:

-> { "execute": "query-version" }
<- {
      "return":{
         "qemu":{
            "major":0,
            "minor":11,
            "micro":5
         },
         "package":""
      }
   }

EQMP

SQMP
query-commands
--------------

List QMP available commands.

Each command is represented by a json-object, the returned value is a json-array
of all commands.

Each json-object contain:

- "name": command's name (json-string)

Example:

-> { "execute": "query-commands" }
<- {
      "return":[
         {
            "name":"query-balloon"
         },
         {
            "name":"system_powerdown"
         }
      ]
   }

Note: This example has been shortened as the real response is too long.

EQMP

SQMP
query-chardev
-------------

Each device is represented by a json-object. The returned value is a json-array
of all devices.

Each json-object contain the following:

- "label": device's label (json-string)
- "filename": device's file (json-string)

Example:

-> { "execute": "query-chardev" }
<- {
      "return":[
         {
            "label":"monitor",
            "filename":"stdio"
         },
         {
            "label":"serial0",
            "filename":"vc"
         }
      ]
   }

EQMP

SQMP
query-block
-----------

Show the block devices.

Each block device information is stored in a json-object and the returned value
is a json-array of all devices.

Each json-object contain the following:

- "device": device name (json-string)
- "type": device type (json-string)
         - deprecated, retained for backward compatibility
         - Possible values: "unknown"
- "removable": true if the device is removable, false otherwise (json-bool)
- "locked": true if the device is locked, false otherwise (json-bool)
- "inserted": only present if the device is inserted, it is a json-object
   containing the following:
         - "file": device file name (json-string)
         - "ro": true if read-only, false otherwise (json-bool)
         - "drv": driver format name (json-string)
             - Possible values: "blkdebug", "bochs", "cloop", "cow", "dmg",
                                "file", "file", "ftp", "ftps", "host_cdrom",
                                "host_device", "host_floppy", "http", "https",
                                "nbd", "parallels", "qcow", "qcow2", "raw",
                                "tftp", "vdi", "vmdk", "vpc", "vvfat"
         - "backing_file": backing file name (json-string, optional)
         - "encrypted": true if encrypted, false otherwise (json-bool)

Example:

-> { "execute": "query-block" }
<- {
      "return":[
         {
            "device":"ide0-hd0",
            "locked":false,
            "removable":false,
            "inserted":{
               "ro":false,
               "drv":"qcow2",
               "encrypted":false,
               "file":"disks/test.img"
            },
            "type":"unknown"
         },
         {
            "device":"ide1-cd0",
            "locked":false,
            "removable":true,
            "type":"unknown"
         },
         {
            "device":"floppy0",
            "locked":false,
            "removable":true,
            "type":"unknown"
         },
         {
            "device":"sd0",
            "locked":false,
            "removable":true,
            "type":"unknown"
         }
      ]
   }

EQMP

SQMP
query-blockstats
----------------

Show block device statistics.

Each device statistic information is stored in a json-object and the returned
value is a json-array of all devices.

Each json-object contain the following:

- "device": device name (json-string)
- "stats": A json-object with the statistics information, it contains:
    - "rd_bytes": bytes read (json-int)
    - "wr_bytes": bytes written (json-int)
    - "rd_operations": read operations (json-int)
    - "wr_operations": write operations (json-int)
    - "wr_highest_offset": Highest offset of a sector written since the
                           BlockDriverState has been opened (json-int)
- "parent": Contains recursively the statistics of the underlying
            protocol (e.g. the host file for a qcow2 image). If there is
            no underlying protocol, this field is omitted
            (json-object, optional)

Example:

-> { "execute": "query-blockstats" }
<- {
      "return":[
         {
            "device":"ide0-hd0",
            "parent":{
               "stats":{
                  "wr_highest_offset":3686448128,
                  "wr_bytes":9786368,
                  "wr_operations":751,
                  "rd_bytes":122567168,
                  "rd_operations":36772
               }
            },
            "stats":{
               "wr_highest_offset":2821110784,
               "wr_bytes":9786368,
               "wr_operations":692,
               "rd_bytes":122739200,
               "rd_operations":36604
            }
         },
         {
            "device":"ide1-cd0",
            "stats":{
               "wr_highest_offset":0,
               "wr_bytes":0,
               "wr_operations":0,
               "rd_bytes":0,
               "rd_operations":0
            }
         },
         {
            "device":"floppy0",
            "stats":{
               "wr_highest_offset":0,
               "wr_bytes":0,
               "wr_operations":0,
               "rd_bytes":0,
               "rd_operations":0
            }
         },
         {
            "device":"sd0",
            "stats":{
               "wr_highest_offset":0,
               "wr_bytes":0,
               "wr_operations":0,
               "rd_bytes":0,
               "rd_operations":0
            }
         }
      ]
   }

EQMP

SQMP
query-cpus
----------

Show CPU information.

Return a json-array. Each CPU is represented by a json-object, which contains:

- "CPU": CPU index (json-int)
- "current": true if this is the current CPU, false otherwise (json-bool)
- "halted": true if the cpu is halted, false otherwise (json-bool)
- Current program counter. The key's name depends on the architecture:
     "pc": i386/x86_64 (json-int)
     "nip": PPC (json-int)
     "pc" and "npc": sparc (json-int)
     "PC": mips (json-int)
- "thread_id": ID of the underlying host thread (json-int)

Example:

-> { "execute": "query-cpus" }
<- {
      "return":[
         {
            "CPU":0,
            "current":true,
            "halted":false,
            "pc":3227107138
            "thread_id":3134
         },
         {
            "CPU":1,
            "current":false,
            "halted":true,
            "pc":7108165
            "thread_id":3135
         }
      ]
   }

EQMP

SQMP
query-pci
---------

PCI buses and devices information.

The returned value is a json-array of all buses. Each bus is represented by
a json-object, which has a key with a json-array of all PCI devices attached
to it. Each device is represented by a json-object.

The bus json-object contains the following:

- "bus": bus number (json-int)
- "devices": a json-array of json-objects, each json-object represents a
             PCI device

The PCI device json-object contains the following:

- "bus": identical to the parent's bus number (json-int)
- "slot": slot number (json-int)
- "function": function number (json-int)
- "class_info": a json-object containing:
     - "desc": device class description (json-string, optional)
     - "class": device class number (json-int)
- "id": a json-object containing:
     - "device": device ID (json-int)
     - "vendor": vendor ID (json-int)
- "irq": device's IRQ if assigned (json-int, optional)
- "qdev_id": qdev id string (json-string)
- "pci_bridge": It's a json-object, only present if this device is a
                PCI bridge, contains:
     - "bus": bus number (json-int)
     - "secondary": secondary bus number (json-int)
     - "subordinate": subordinate bus number (json-int)
     - "io_range": I/O memory range information, a json-object with the
                   following members:
                 - "base": base address, in bytes (json-int)
                 - "limit": limit address, in bytes (json-int)
     - "memory_range": memory range information, a json-object with the
                       following members:
                 - "base": base address, in bytes (json-int)
                 - "limit": limit address, in bytes (json-int)
     - "prefetchable_range": Prefetchable memory range information, a
                             json-object with the following members:
                 - "base": base address, in bytes (json-int)
                 - "limit": limit address, in bytes (json-int)
     - "devices": a json-array of PCI devices if there's any attached, each
                  each element is represented by a json-object, which contains
                  the same members of the 'PCI device json-object' described
                  above (optional)
- "regions": a json-array of json-objects, each json-object represents a
             memory region of this device

The memory range json-object contains the following:

- "base": base memory address (json-int)
- "limit": limit value (json-int)

The region json-object can be an I/O region or a memory region, an I/O region
json-object contains the following:

- "type": "io" (json-string, fixed)
- "bar": BAR number (json-int)
- "address": memory address (json-int)
- "size": memory size (json-int)

A memory region json-object contains the following:

- "type": "memory" (json-string, fixed)
- "bar": BAR number (json-int)
- "address": memory address (json-int)
- "size": memory size (json-int)
- "mem_type_64": true or false (json-bool)
- "prefetch": true or false (json-bool)

Example:

-> { "execute": "query-pci" }
<- {
      "return":[
         {
            "bus":0,
            "devices":[
               {
                  "bus":0,
                  "qdev_id":"",
                  "slot":0,
                  "class_info":{
                     "class":1536,
                     "desc":"Host bridge"
                  },
                  "id":{
                     "device":32902,
                     "vendor":4663
                  },
                  "function":0,
                  "regions":[
   
                  ]
               },
               {
                  "bus":0,
                  "qdev_id":"",
                  "slot":1,
                  "class_info":{
                     "class":1537,
                     "desc":"ISA bridge"
                  },
                  "id":{
                     "device":32902,
                     "vendor":28672
                  },
                  "function":0,
                  "regions":[
   
                  ]
               },
               {
                  "bus":0,
                  "qdev_id":"",
                  "slot":1,
                  "class_info":{
                     "class":257,
                     "desc":"IDE controller"
                  },
                  "id":{
                     "device":32902,
                     "vendor":28688
                  },
                  "function":1,
                  "regions":[
                     {
                        "bar":4,
                        "size":16,
                        "address":49152,
                        "type":"io"
                     }
                  ]
               },
               {
                  "bus":0,
                  "qdev_id":"",
                  "slot":2,
                  "class_info":{
                     "class":768,
                     "desc":"VGA controller"
                  },
                  "id":{
                     "device":4115,
                     "vendor":184
                  },
                  "function":0,
                  "regions":[
                     {
                        "prefetch":true,
                        "mem_type_64":false,
                        "bar":0,
                        "size":33554432,
                        "address":4026531840,
                        "type":"memory"
                     },
                     {
                        "prefetch":false,
                        "mem_type_64":false,
                        "bar":1,
                        "size":4096,
                        "address":4060086272,
                        "type":"memory"
                     },
                     {
                        "prefetch":false,
                        "mem_type_64":false,
                        "bar":6,
                        "size":65536,
                        "address":-1,
                        "type":"memory"
                     }
                  ]
               },
               {
                  "bus":0,
                  "qdev_id":"",
                  "irq":11,
                  "slot":4,
                  "class_info":{
                     "class":1280,
                     "desc":"RAM controller"
                  },
                  "id":{
                     "device":6900,
                     "vendor":4098
                  },
                  "function":0,
                  "regions":[
                     {
                        "bar":0,
                        "size":32,
                        "address":49280,
                        "type":"io"
                     }
                  ]
               }
            ]
         }
      ]
   }

Note: This example has been shortened as the real response is too long.

EQMP

SQMP
query-kvm
---------

Show KVM information.

Return a json-object with the following information:

- "enabled": true if KVM support is enabled, false otherwise (json-bool)
- "present": true if QEMU has KVM support, false otherwise (json-bool)

Example:

-> { "execute": "query-kvm" }
<- { "return": { "enabled": true, "present": true } }

EQMP

SQMP
query-status
------------

Return a json-object with the following information:

- "running": true if the VM is running, or false if it is paused (json-bool)
- "singlestep": true if the VM is in single step mode,
                false otherwise (json-bool)

Example:

-> { "execute": "query-status" }
<- { "return": { "running": true, "singlestep": false } }

EQMP

SQMP
query-mice
----------

Show VM mice information.

Each mouse is represented by a json-object, the returned value is a json-array
of all mice.

The mouse json-object contains the following:

- "name": mouse's name (json-string)
- "index": mouse's index (json-int)
- "current": true if this mouse is receiving events, false otherwise (json-bool)
- "absolute": true if the mouse generates absolute input events (json-bool)

Example:

-> { "execute": "query-mice" }
<- {
      "return":[
         {
            "name":"QEMU Microsoft Mouse",
            "index":0,
            "current":false,
            "absolute":false
         },
         {
            "name":"QEMU PS/2 Mouse",
            "index":1,
            "current":true,
            "absolute":true
         }
      ]
   }

EQMP

SQMP
query-vnc
---------

Show VNC server information.

Return a json-object with server information. Connected clients are returned
as a json-array of json-objects.

The main json-object contains the following:

- "enabled": true or false (json-bool)
- "host": server's IP address (json-string)
- "family": address family (json-string)
         - Possible values: "ipv4", "ipv6", "unix", "unknown"
- "service": server's port number (json-string)
- "auth": authentication method (json-string)
         - Possible values: "invalid", "none", "ra2", "ra2ne", "sasl", "tight",
                            "tls", "ultra", "unknown", "vencrypt", "vencrypt",
                            "vencrypt+plain", "vencrypt+tls+none",
                            "vencrypt+tls+plain", "vencrypt+tls+sasl",
                            "vencrypt+tls+vnc", "vencrypt+x509+none",
                            "vencrypt+x509+plain", "vencrypt+x509+sasl",
                            "vencrypt+x509+vnc", "vnc"
- "clients": a json-array of all connected clients

Clients are described by a json-object, each one contain the following:

- "host": client's IP address (json-string)
- "family": address family (json-string)
         - Possible values: "ipv4", "ipv6", "unix", "unknown"
- "service": client's port number (json-string)
- "x509_dname": TLS dname (json-string, optional)
- "sasl_username": SASL username (json-string, optional)

Example:

-> { "execute": "query-vnc" }
<- {
      "return":{
         "enabled":true,
         "host":"0.0.0.0",
         "service":"50402",
         "auth":"vnc",
         "family":"ipv4",
         "clients":[
            {
               "host":"127.0.0.1",
               "service":"50401",
               "family":"ipv4"
            }
         ]
      }
   }

EQMP

SQMP
query-spice
-----------

Show SPICE server information.

Return a json-object with server information. Connected clients are returned
as a json-array of json-objects.

The main json-object contains the following:

- "enabled": true or false (json-bool)
- "host": server's IP address (json-string)
- "port": server's port number (json-int, optional)
- "tls-port": server's port number (json-int, optional)
- "auth": authentication method (json-string)
         - Possible values: "none", "spice"
- "channels": a json-array of all active channels clients

Channels are described by a json-object, each one contain the following:

- "host": client's IP address (json-string)
- "family": address family (json-string)
         - Possible values: "ipv4", "ipv6", "unix", "unknown"
- "port": client's port number (json-string)
- "connection-id": spice connection id.  All channels with the same id
                   belong to the same spice session (json-int)
- "channel-type": channel type.  "1" is the main control channel, filter for
                  this one if you want track spice sessions only (json-int)
- "channel-id": channel id.  Usually "0", might be different needed when
                multiple channels of the same type exist, such as multiple
                display channels in a multihead setup (json-int)
- "tls": whevener the channel is encrypted (json-bool)

Example:

-> { "execute": "query-spice" }
<- {
      "return": {
         "enabled": true,
         "auth": "spice",
         "port": 5920,
         "tls-port": 5921,
         "host": "0.0.0.0",
         "channels": [
            {
               "port": "54924",
               "family": "ipv4",
               "channel-type": 1,
               "connection-id": 1804289383,
               "host": "127.0.0.1",
               "channel-id": 0,
               "tls": true
            },
            {
               "port": "36710",
               "family": "ipv4",
               "channel-type": 4,
               "connection-id": 1804289383,
               "host": "127.0.0.1",
               "channel-id": 0,
               "tls": false
            },
            [ ... more channels follow ... ]
         ]
      }
   }

EQMP

SQMP
query-name
----------

Show VM name.

Return a json-object with the following information:

- "name": VM's name (json-string, optional)

Example:

-> { "execute": "query-name" }
<- { "return": { "name": "qemu-name" } }

EQMP

SQMP
query-uuid
----------

Show VM UUID.

Return a json-object with the following information:

- "UUID": Universally Unique Identifier (json-string)

Example:

-> { "execute": "query-uuid" }
<- { "return": { "UUID": "550e8400-e29b-41d4-a716-446655440000" } }

EQMP

SQMP
query-migrate
-------------

Migration status.

Return a json-object. If migration is active there will be another json-object
with RAM migration status and if block migration is active another one with
block migration status.

The main json-object contains the following:

- "status": migration status (json-string)
     - Possible values: "active", "completed", "failed", "cancelled"
- "ram": only present if "status" is "active", it is a json-object with the
  following RAM information (in bytes):
         - "transferred": amount transferred (json-int)
         - "remaining": amount remaining (json-int)
         - "total": total (json-int)
- "disk": only present if "status" is "active" and it is a block migration,
  it is a json-object with the following disk information (in bytes):
         - "transferred": amount transferred (json-int)
         - "remaining": amount remaining (json-int)
         - "total": total (json-int)

Examples:

1. Before the first migration

-> { "execute": "query-migrate" }
<- { "return": {} }

2. Migration is done and has succeeded

-> { "execute": "query-migrate" }
<- { "return": { "status": "completed" } }

3. Migration is done and has failed

-> { "execute": "query-migrate" }
<- { "return": { "status": "failed" } }

4. Migration is being performed and is not a block migration:

-> { "execute": "query-migrate" }
<- {
      "return":{
         "status":"active",
         "ram":{
            "transferred":123,
            "remaining":123,
            "total":246
         }
      }
   }

5. Migration is being performed and is a block migration:

-> { "execute": "query-migrate" }
<- {
      "return":{
         "status":"active",
         "ram":{
            "total":1057024,
            "remaining":1053304,
            "transferred":3720
         },
         "disk":{
            "total":20971520,
            "remaining":20880384,
            "transferred":91136
         }
      }
   }

EQMP

SQMP
query-balloon
-------------

Show balloon information.

Make an asynchronous request for balloon info. When the request completes a
json-object will be returned containing the following data:

- "actual": current balloon value in bytes (json-int)
- "mem_swapped_in": Amount of memory swapped in bytes (json-int, optional)
- "mem_swapped_out": Amount of memory swapped out in bytes (json-int, optional)
- "major_page_faults": Number of major faults (json-int, optional)
- "minor_page_faults": Number of minor faults (json-int, optional)
- "free_mem": Total amount of free and unused memory in
              bytes (json-int, optional)
- "total_mem": Total amount of available memory in bytes (json-int, optional)

Example:

-> { "execute": "query-balloon" }
<- {
      "return":{
         "actual":1073741824,
         "mem_swapped_in":0,
         "mem_swapped_out":0,
         "major_page_faults":142,
         "minor_page_faults":239245,
         "free_mem":1014185984,
         "total_mem":1044668416
      }
   }

EQMP


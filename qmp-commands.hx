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
        .mhandler.cmd_new = qmp_marshal_input_quit,
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
        .mhandler.cmd_new = qmp_marshal_input_eject,
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
        .mhandler.cmd_new = qmp_marshal_input_change,
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
        .mhandler.cmd_new = qmp_marshal_input_screendump,
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
        .mhandler.cmd_new = qmp_marshal_input_stop,
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
        .mhandler.cmd_new = qmp_marshal_input_cont,
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
        .name       = "system_wakeup",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_system_wakeup,
    },

SQMP
system_wakeup
-------------

Wakeup guest from suspend.

Arguments: None.

Example:

-> { "execute": "system_wakeup" }
<- { "return": {} }

EQMP

    {
        .name       = "system_reset",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_system_reset,
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
        .mhandler.cmd_new = qmp_marshal_input_system_powerdown,
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
        .mhandler.cmd_new = qmp_marshal_input_device_del,
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
        .name       = "send-key",
        .args_type  = "keys:O,hold-time:i?",
        .mhandler.cmd_new = qmp_marshal_input_send_key,
    },

SQMP
send-key
----------

Send keys to VM.

Arguments:

keys array:
    - "key": key sequence (a json-array of key enum values)

- hold-time: time to delay key up events, milliseconds. Defaults to 100
             (json-int, optional)

Example:

-> { "execute": "send-key",
     "arguments": { 'keys': [ 'ctrl', 'alt', 'delete' ] } }
<- { "return": {} }

EQMP

    {
        .name       = "cpu",
        .args_type  = "index:i",
        .mhandler.cmd_new = qmp_marshal_input_cpu,
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
        .args_type  = "val:l,size:i,filename:s,cpu:i?",
        .mhandler.cmd_new = qmp_marshal_input_memsave,
    },

SQMP
memsave
-------

Save to disk virtual memory dump starting at 'val' of size 'size'.

Arguments:

- "val": the starting address (json-int)
- "size": the memory size, in bytes (json-int)
- "filename": file path (json-string)
- "cpu": virtual CPU index (json-int, optional)

Example:

-> { "execute": "memsave",
             "arguments": { "val": 10,
                            "size": 100,
                            "filename": "/tmp/virtual-mem-dump" } }
<- { "return": {} }

EQMP

    {
        .name       = "pmemsave",
        .args_type  = "val:l,size:i,filename:s",
        .mhandler.cmd_new = qmp_marshal_input_pmemsave,
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
        .mhandler.cmd_new = qmp_marshal_input_inject_nmi,
    },

SQMP
inject-nmi
----------

Inject an NMI on guest's CPUs.

Arguments: None.

Example:

-> { "execute": "inject-nmi" }
<- { "return": {} }

Note: inject-nmi fails when the guest doesn't support injecting.
      Currently, only x86 guests do.

EQMP

    {
        .name       = "ringbuf-write",
        .args_type  = "device:s,data:s,format:s?",
        .mhandler.cmd_new = qmp_marshal_input_ringbuf_write,
    },

SQMP
ringbuf-write
-------------

Write to a ring buffer character device.

Arguments:

- "device": ring buffer character device name (json-string)
- "data": data to write (json-string)
- "format": data format (json-string, optional)
          - Possible values: "utf8" (default), "base64"
            Bug: invalid base64 is currently not rejected.
            Whitespace *is* invalid.

Example:

-> { "execute": "ringbuf-write",
                "arguments": { "device": "foo",
                               "data": "abcdefgh",
                               "format": "utf8" } }
<- { "return": {} }

EQMP

    {
        .name       = "ringbuf-read",
        .args_type  = "device:s,size:i,format:s?",
        .mhandler.cmd_new = qmp_marshal_input_ringbuf_read,
    },

SQMP
ringbuf-read
-------------

Read from a ring buffer character device.

Arguments:

- "device": ring buffer character device name (json-string)
- "size": how many bytes to read at most (json-int)
          - Number of data bytes, not number of characters in encoded data
- "format": data format (json-string, optional)
          - Possible values: "utf8" (default), "base64"
          - Naturally, format "utf8" works only when the ring buffer
            contains valid UTF-8 text.  Invalid UTF-8 sequences get
            replaced.  Bug: replacement doesn't work.  Bug: can screw
            up on encountering NUL characters, after the ring buffer
            lost data, and when reading stops because the size limit
            is reached.

Example:

-> { "execute": "ringbuf-read",
                "arguments": { "device": "foo",
                               "size": 1000,
                               "format": "utf8" } }
<- {"return": "abcdefgh"}

EQMP

    {
        .name       = "xen-save-devices-state",
        .args_type  = "filename:F",
    .mhandler.cmd_new = qmp_marshal_input_xen_save_devices_state,
    },

SQMP
xen-save-devices-state
-------

Save the state of all devices to file. The RAM and the block devices
of the VM are not saved by this command.

Arguments:

- "filename": the file to save the state of the devices to as binary
data. See xen-save-devices-state.txt for a description of the binary
format.

Example:

-> { "execute": "xen-save-devices-state",
     "arguments": { "filename": "/tmp/save" } }
<- { "return": {} }

EQMP

    {
        .name       = "xen-set-global-dirty-log",
        .args_type  = "enable:b",
        .mhandler.cmd_new = qmp_marshal_input_xen_set_global_dirty_log,
    },

SQMP
xen-set-global-dirty-log
-------

Enable or disable the global dirty log mode.

Arguments:

- "enable": Enable it or disable it.

Example:

-> { "execute": "xen-set-global-dirty-log",
     "arguments": { "enable": true } }
<- { "return": {} }

EQMP

    {
        .name       = "migrate",
        .args_type  = "detach:-d,blk:-b,inc:-i,uri:s",
        .mhandler.cmd_new = qmp_marshal_input_migrate,
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
        .mhandler.cmd_new = qmp_marshal_input_migrate_cancel,
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
        .name       = "migrate-set-cache-size",
        .args_type  = "value:o",
        .mhandler.cmd_new = qmp_marshal_input_migrate_set_cache_size,
    },

SQMP
migrate-set-cache-size
---------------------

Set cache size to be used by XBZRLE migration, the cache size will be rounded
down to the nearest power of 2

Arguments:

- "value": cache size in bytes (json-int)

Example:

-> { "execute": "migrate-set-cache-size", "arguments": { "value": 536870912 } }
<- { "return": {} }

EQMP
    {
        .name       = "query-migrate-cache-size",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_migrate_cache_size,
    },

SQMP
query-migrate-cache-size
---------------------

Show cache size to be used by XBZRLE migration

returns a json-object with the following information:
- "size" : json-int

Example:

-> { "execute": "query-migrate-cache-size" }
<- { "return": 67108864 }

EQMP

    {
        .name       = "migrate_set_speed",
        .args_type  = "value:o",
        .mhandler.cmd_new = qmp_marshal_input_migrate_set_speed,
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
        .mhandler.cmd_new = qmp_marshal_input_migrate_set_downtime,
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
        .mhandler.cmd_async = client_migrate_info,
        .flags      = MONITOR_CMD_ASYNC,
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
        .name       = "dump-guest-memory",
        .args_type  = "paging:b,protocol:s,begin:i?,end:i?",
        .params     = "-p protocol [begin] [length]",
        .help       = "dump guest memory to file",
        .user_print = monitor_user_noop,
        .mhandler.cmd_new = qmp_marshal_input_dump_guest_memory,
    },

SQMP
dump


Dump guest memory to file. The file can be processed with crash or gdb.

Arguments:

- "paging": do paging to get guest's memory mapping (json-bool)
- "protocol": destination file(started with "file:") or destination file
              descriptor (started with "fd:") (json-string)
- "begin": the starting physical address. It's optional, and should be specified
           with length together (json-int)
- "length": the memory size, in bytes. It's optional, and should be specified
            with begin together (json-int)

Example:

-> { "execute": "dump-guest-memory", "arguments": { "protocol": "fd:dump" } }
<- { "return": {} }

Notes:

(1) All boolean arguments default to false

EQMP

    {
        .name       = "netdev_add",
        .args_type  = "netdev:O",
        .mhandler.cmd_new = qmp_netdev_add,
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

Note: The supported device options are the same ones supported by the '-netdev'
      command-line argument, which are listed in the '-help' output or QEMU's
      manual

EQMP

    {
        .name       = "netdev_del",
        .args_type  = "id:s",
        .mhandler.cmd_new = qmp_marshal_input_netdev_del,
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
        .mhandler.cmd_new = qmp_marshal_input_block_resize,
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
        .name       = "block-stream",
        .args_type  = "device:B,base:s?,speed:o?,on-error:s?",
        .mhandler.cmd_new = qmp_marshal_input_block_stream,
    },

    {
        .name       = "block-commit",
        .args_type  = "device:B,base:s?,top:s,speed:o?",
        .mhandler.cmd_new = qmp_marshal_input_block_commit,
    },

    {
        .name       = "block-job-set-speed",
        .args_type  = "device:B,speed:o",
        .mhandler.cmd_new = qmp_marshal_input_block_job_set_speed,
    },

    {
        .name       = "block-job-cancel",
        .args_type  = "device:B,force:b?",
        .mhandler.cmd_new = qmp_marshal_input_block_job_cancel,
    },
    {
        .name       = "block-job-pause",
        .args_type  = "device:B",
        .mhandler.cmd_new = qmp_marshal_input_block_job_pause,
    },
    {
        .name       = "block-job-resume",
        .args_type  = "device:B",
        .mhandler.cmd_new = qmp_marshal_input_block_job_resume,
    },
    {
        .name       = "block-job-complete",
        .args_type  = "device:B",
        .mhandler.cmd_new = qmp_marshal_input_block_job_complete,
    },
    {
        .name       = "transaction",
        .args_type  = "actions:q",
        .mhandler.cmd_new = qmp_marshal_input_transaction,
    },

SQMP
transaction
-----------

Atomically operate on one or more block devices.  The only supported
operation for now is snapshotting.  If there is any failure performing
any of the operations, all snapshots for the group are abandoned, and
the original disks pre-snapshot attempt are used.

A list of dictionaries is accepted, that contains the actions to be performed.
For snapshots this is the device, the file to use for the new snapshot,
and the format.  The default format, if not specified, is qcow2.

Each new snapshot defaults to being created by QEMU (wiping any
contents if the file already exists), but it is also possible to reuse
an externally-created file.  In the latter case, you should ensure that
the new image file has the same contents as the current one; QEMU cannot
perform any meaningful check.  Typically this is achieved by using the
current image file as the backing file for the new image.

Arguments:

actions array:
    - "type": the operation to perform.  The only supported
      value is "blockdev-snapshot-sync". (json-string)
    - "data": a dictionary.  The contents depend on the value
      of "type".  When "type" is "blockdev-snapshot-sync":
      - "device": device name to snapshot (json-string)
      - "snapshot-file": name of new image file (json-string)
      - "format": format of new image (json-string, optional)
      - "mode": whether and how QEMU should create the snapshot file
        (NewImageMode, optional, default "absolute-paths")

Example:

-> { "execute": "transaction",
     "arguments": { "actions": [
         { 'type': 'blockdev-snapshot-sync', 'data' : { "device": "ide-hd0",
                                         "snapshot-file": "/some/place/my-image",
                                         "format": "qcow2" } },
         { 'type': 'blockdev-snapshot-sync', 'data' : { "device": "ide-hd1",
                                         "snapshot-file": "/some/place/my-image2",
                                         "mode": "existing",
                                         "format": "qcow2" } } ] } }
<- { "return": {} }

EQMP

    {
        .name       = "blockdev-snapshot-sync",
        .args_type  = "device:B,snapshot-file:s,format:s?,mode:s?",
        .mhandler.cmd_new = qmp_marshal_input_blockdev_snapshot_sync,
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
- "mode": whether and how QEMU should create the snapshot file
  (NewImageMode, optional, default "absolute-paths")
- "format": format of new image (json-string, optional)

Example:

-> { "execute": "blockdev-snapshot-sync", "arguments": { "device": "ide-hd0",
                                                         "snapshot-file":
                                                        "/some/place/my-image",
                                                        "format": "qcow2" } }
<- { "return": {} }

EQMP

    {
        .name       = "drive-mirror",
        .args_type  = "sync:s,device:B,target:s,speed:i?,mode:s?,format:s?,"
                      "on-source-error:s?,on-target-error:s?,"
                      "granularity:i?,buf-size:i?",
        .mhandler.cmd_new = qmp_marshal_input_drive_mirror,
    },

SQMP
drive-mirror
------------

Start mirroring a block device's writes to a new destination. target
specifies the target of the new image. If the file exists, or if it is
a device, it will be used as the new destination for writes. If it does not
exist, a new file will be created. format specifies the format of the
mirror image, default is to probe if mode='existing', else the format
of the source.

Arguments:

- "device": device name to operate on (json-string)
- "target": name of new image file (json-string)
- "format": format of new image (json-string, optional)
- "mode": how an image file should be created into the target
  file/device (NewImageMode, optional, default 'absolute-paths')
- "speed": maximum speed of the streaming job, in bytes per second
  (json-int)
- "granularity": granularity of the dirty bitmap, in bytes (json-int, optional)
- "buf_size": maximum amount of data in flight from source to target, in bytes
  (json-int, default 10M)
- "sync": what parts of the disk image should be copied to the destination;
  possibilities include "full" for all the disk, "top" for only the sectors
  allocated in the topmost image, or "none" to only replicate new I/O
  (MirrorSyncMode).
- "on-source-error": the action to take on an error on the source
  (BlockdevOnError, default 'report')
- "on-target-error": the action to take on an error on the target
  (BlockdevOnError, default 'report')

The default value of the granularity is the image cluster size clamped
between 4096 and 65536, if the image format defines one.  If the format
does not define a cluster size, the default value of the granularity
is 65536.


Example:

-> { "execute": "drive-mirror", "arguments": { "device": "ide-hd0",
                                               "target": "/some/place/my-image",
                                               "sync": "full",
                                               "format": "qcow2" } }
<- { "return": {} }

EQMP

    {
        .name       = "balloon",
        .args_type  = "value:M",
        .mhandler.cmd_new = qmp_marshal_input_balloon,
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
        .mhandler.cmd_new = qmp_marshal_input_set_link,
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
        .mhandler.cmd_new = qmp_marshal_input_getfd,
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

Notes:

(1) If the name specified by the "fdname" argument already exists,
    the file descriptor assigned to it will be closed and replaced
    by the received file descriptor.
(2) The 'closefd' command can be used to explicitly close the file
    descriptor when it is no longer needed.

EQMP

    {
        .name       = "closefd",
        .args_type  = "fdname:s",
        .params     = "closefd name",
        .help       = "close a file descriptor previously passed via SCM rights",
        .mhandler.cmd_new = qmp_marshal_input_closefd,
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
        .name       = "add-fd",
        .args_type  = "fdset-id:i?,opaque:s?",
        .params     = "add-fd fdset-id opaque",
        .help       = "Add a file descriptor, that was passed via SCM rights, to an fd set",
        .mhandler.cmd_new = qmp_marshal_input_add_fd,
    },

SQMP
add-fd
-------

Add a file descriptor, that was passed via SCM rights, to an fd set.

Arguments:

- "fdset-id": The ID of the fd set to add the file descriptor to.
              (json-int, optional)
- "opaque": A free-form string that can be used to describe the fd.
            (json-string, optional)

Return a json-object with the following information:

- "fdset-id": The ID of the fd set that the fd was added to. (json-int)
- "fd": The file descriptor that was received via SCM rights and added to the
        fd set. (json-int)

Example:

-> { "execute": "add-fd", "arguments": { "fdset-id": 1 } }
<- { "return": { "fdset-id": 1, "fd": 3 } }

Notes:

(1) The list of fd sets is shared by all monitor connections.
(2) If "fdset-id" is not specified, a new fd set will be created.

EQMP

     {
        .name       = "remove-fd",
        .args_type  = "fdset-id:i,fd:i?",
        .params     = "remove-fd fdset-id fd",
        .help       = "Remove a file descriptor from an fd set",
        .mhandler.cmd_new = qmp_marshal_input_remove_fd,
    },

SQMP
remove-fd
---------

Remove a file descriptor from an fd set.

Arguments:

- "fdset-id": The ID of the fd set that the file descriptor belongs to.
              (json-int)
- "fd": The file descriptor that is to be removed. (json-int, optional)

Example:

-> { "execute": "remove-fd", "arguments": { "fdset-id": 1, "fd": 3 } }
<- { "return": {} }

Notes:

(1) The list of fd sets is shared by all monitor connections.
(2) If "fd" is not specified, all file descriptors in "fdset-id" will be
    removed.

EQMP

    {
        .name       = "query-fdsets",
        .args_type  = "",
        .help       = "Return information describing all fd sets",
        .mhandler.cmd_new = qmp_marshal_input_query_fdsets,
    },

SQMP
query-fdsets
-------------

Return information describing all fd sets.

Arguments: None

Example:

-> { "execute": "query-fdsets" }
<- { "return": [
       {
         "fds": [
           {
             "fd": 30,
             "opaque": "rdonly:/path/to/file"
           },
           {
             "fd": 24,
             "opaque": "rdwr:/path/to/file"
           }
         ],
         "fdset-id": 1
       },
       {
         "fds": [
           {
             "fd": 28
           },
           {
             "fd": 29
           }
         ],
         "fdset-id": 0
       }
     ]
   }

Note: The list of fd sets is shared by all monitor connections.

EQMP

    {
        .name       = "block_passwd",
        .args_type  = "device:B,password:s",
        .mhandler.cmd_new = qmp_marshal_input_block_passwd,
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
        .name       = "block_set_io_throttle",
        .args_type  = "device:B,bps:l,bps_rd:l,bps_wr:l,iops:l,iops_rd:l,iops_wr:l",
        .mhandler.cmd_new = qmp_marshal_input_block_set_io_throttle,
    },

SQMP
block_set_io_throttle
------------

Change I/O throttle limits for a block drive.

Arguments:

- "device": device name (json-string)
- "bps":  total throughput limit in bytes per second(json-int)
- "bps_rd":  read throughput limit in bytes per second(json-int)
- "bps_wr":  read throughput limit in bytes per second(json-int)
- "iops":  total I/O operations per second(json-int)
- "iops_rd":  read I/O operations per second(json-int)
- "iops_wr":  write I/O operations per second(json-int)

Example:

-> { "execute": "block_set_io_throttle", "arguments": { "device": "virtio0",
                                               "bps": "1000000",
                                               "bps_rd": "0",
                                               "bps_wr": "0",
                                               "iops": "0",
                                               "iops_rd": "0",
                                               "iops_wr": "0" } }
<- { "return": {} }

EQMP

    {
        .name       = "set_password",
        .args_type  = "protocol:s,password:s,connected:s?",
        .mhandler.cmd_new = qmp_marshal_input_set_password,
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
        .mhandler.cmd_new = qmp_marshal_input_expire_password,
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
        .args_type  = "protocol:s,fdname:s,skipauth:b?,tls:b?",
        .mhandler.cmd_new = qmp_marshal_input_add_client,
    },

SQMP
add_client
----------

Add a graphics client

Arguments:

- "protocol": protocol name (json-string)
- "fdname": file descriptor name (json-string)
- "skipauth": whether to skip authentication (json-bool, optional)
- "tls": whether to perform TLS (json-bool, optional)

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
        .mhandler.cmd_new = qmp_marshal_input_human_monitor_command,
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

    {
        .name       = "query-version",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_version,
    },

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

    {
        .name       = "query-commands",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_commands,
    },

SQMP
query-events
--------------

List QMP available events.

Each event is represented by a json-object, the returned value is a json-array
of all events.

Each json-object contains:

- "name": event's name (json-string)

Example:

-> { "execute": "query-events" }
<- {
      "return":[
         {
            "name":"SHUTDOWN"
         },
         {
            "name":"RESET"
         }
      ]
   }

Note: This example has been shortened as the real response is too long.

EQMP

    {
        .name       = "query-events",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_events,
    },

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

    {
        .name       = "query-chardev",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_chardev,
    },

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
- "tray_open": only present if removable, true if the device has a tray,
               and it is open (json-bool)
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
         - "backing_file_depth": number of files in the backing file chain (json-int)
         - "encrypted": true if encrypted, false otherwise (json-bool)
         - "bps": limit total bytes per second (json-int)
         - "bps_rd": limit read bytes per second (json-int)
         - "bps_wr": limit write bytes per second (json-int)
         - "iops": limit total I/O operations per second (json-int)
         - "iops_rd": limit read operations per second (json-int)
         - "iops_wr": limit write operations per second (json-int)

- "io-status": I/O operation status, only present if the device supports it
               and the VM is configured to stop on errors. It's always reset
               to "ok" when the "cont" command is issued (json_string, optional)
             - Possible values: "ok", "failed", "nospace"

Example:

-> { "execute": "query-block" }
<- {
      "return":[
         {
            "io-status": "ok",
            "device":"ide0-hd0",
            "locked":false,
            "removable":false,
            "inserted":{
               "ro":false,
               "drv":"qcow2",
               "encrypted":false,
               "file":"disks/test.img",
               "backing_file_depth":0,
               "bps":1000000,
               "bps_rd":0,
               "bps_wr":0,
               "iops":1000000,
               "iops_rd":0,
               "iops_wr":0,
            },
            "type":"unknown"
         },
         {
            "io-status": "ok",
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

    {
        .name       = "query-block",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_block,
    },

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
    - "flush_operations": cache flush operations (json-int)
    - "wr_total_time_ns": total time spend on writes in nano-seconds (json-int)
    - "rd_total_time_ns": total time spend on reads in nano-seconds (json-int)
    - "flush_total_time_ns": total time spend on cache flushes in nano-seconds (json-int)
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
                  "wr_total_times_ns":313253456
                  "rd_total_times_ns":3465673657
                  "flush_total_times_ns":49653
                  "flush_operations":61,
               }
            },
            "stats":{
               "wr_highest_offset":2821110784,
               "wr_bytes":9786368,
               "wr_operations":692,
               "rd_bytes":122739200,
               "rd_operations":36604
               "flush_operations":51,
               "wr_total_times_ns":313253456
               "rd_total_times_ns":3465673657
               "flush_total_times_ns":49653
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
               "flush_operations":0,
               "wr_total_times_ns":0
               "rd_total_times_ns":0
               "flush_total_times_ns":0
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
               "flush_operations":0,
               "wr_total_times_ns":0
               "rd_total_times_ns":0
               "flush_total_times_ns":0
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
               "flush_operations":0,
               "wr_total_times_ns":0
               "rd_total_times_ns":0
               "flush_total_times_ns":0
            }
         }
      ]
   }

EQMP

    {
        .name       = "query-blockstats",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_blockstats,
    },

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

    {
        .name       = "query-cpus",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_cpus,
    },

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

    {
        .name       = "query-pci",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_pci,
    },

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

    {
        .name       = "query-kvm",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_kvm,
    },

SQMP
query-status
------------

Return a json-object with the following information:

- "running": true if the VM is running, or false if it is paused (json-bool)
- "singlestep": true if the VM is in single step mode,
                false otherwise (json-bool)
- "status": one of the following values (json-string)
    "debug" - QEMU is running on a debugger
    "inmigrate" - guest is paused waiting for an incoming migration
    "internal-error" - An internal error that prevents further guest
    execution has occurred
    "io-error" - the last IOP has failed and the device is configured
    to pause on I/O errors
    "paused" - guest has been paused via the 'stop' command
    "postmigrate" - guest is paused following a successful 'migrate'
    "prelaunch" - QEMU was started with -S and guest has not started
    "finish-migrate" - guest is paused to finish the migration process
    "restore-vm" - guest is paused to restore VM state
    "running" - guest is actively running
    "save-vm" - guest is paused to save the VM state
    "shutdown" - guest is shut down (and -no-shutdown is in use)
    "watchdog" - the watchdog action is configured to pause and
     has been triggered

Example:

-> { "execute": "query-status" }
<- { "return": { "running": true, "singlestep": false, "status": "running" } }

EQMP
    
    {
        .name       = "query-status",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_status,
    },

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

    {
        .name       = "query-mice",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_mice,
    },

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

    {
        .name       = "query-vnc",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_vnc,
    },

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

#if defined(CONFIG_SPICE)
    {
        .name       = "query-spice",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_spice,
    },
#endif

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

    {
        .name       = "query-name",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_name,
    },

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

    {
        .name       = "query-uuid",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_uuid,
    },

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
- "total-time": total amount of ms since migration started.  If
                migration has ended, it returns the total migration
		 time (json-int)
- "downtime": only present when migration has finished correctly
              total amount in ms for downtime that happened (json-int)
- "expected-downtime": only present while migration is active
                total amount in ms for downtime that was calculated on
		the last bitmap round (json-int)
- "ram": only present if "status" is "active", it is a json-object with the
  following RAM information (in bytes):
         - "transferred": amount transferred (json-int)
         - "remaining": amount remaining (json-int)
         - "total": total (json-int)
         - "duplicate": number of duplicated pages (json-int)
         - "normal" : number of normal pages transferred (json-int)
         - "normal-bytes" : number of normal bytes transferred (json-int)
- "disk": only present if "status" is "active" and it is a block migration,
  it is a json-object with the following disk information (in bytes):
         - "transferred": amount transferred (json-int)
         - "remaining": amount remaining (json-int)
         - "total": total (json-int)
- "xbzrle-cache": only present if XBZRLE is active.
  It is a json-object with the following XBZRLE information:
         - "cache-size": XBZRLE cache size
         - "bytes": total XBZRLE bytes transferred
         - "pages": number of XBZRLE compressed pages
         - "cache-miss": number of cache misses
         - "overflow": number of XBZRLE overflows
Examples:

1. Before the first migration

-> { "execute": "query-migrate" }
<- { "return": {} }

2. Migration is done and has succeeded

-> { "execute": "query-migrate" }
<- { "return": {
        "status": "completed",
        "ram":{
          "transferred":123,
          "remaining":123,
          "total":246,
          "total-time":12345,
          "downtime":12345,
          "duplicate":123,
          "normal":123,
          "normal-bytes":123456
        }
     }
   }

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
            "total":246,
            "total-time":12345,
            "expected-downtime":12345,
            "duplicate":123,
            "normal":123,
            "normal-bytes":123456
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
            "transferred":3720,
            "total-time":12345,
            "expected-downtime":12345,
            "duplicate":123,
            "normal":123,
            "normal-bytes":123456
         },
         "disk":{
            "total":20971520,
            "remaining":20880384,
            "transferred":91136
         }
      }
   }

6. Migration is being performed and XBZRLE is active:

-> { "execute": "query-migrate" }
<- {
      "return":{
         "status":"active",
         "capabilities" : [ { "capability": "xbzrle", "state" : true } ],
         "ram":{
            "total":1057024,
            "remaining":1053304,
            "transferred":3720,
            "total-time":12345,
            "expected-downtime":12345,
            "duplicate":10,
            "normal":3333,
            "normal-bytes":3412992
         },
         "xbzrle-cache":{
            "cache-size":67108864,
            "bytes":20971520,
            "pages":2444343,
            "cache-miss":2244,
            "overflow":34434
         }
      }
   }

EQMP

    {
        .name       = "query-migrate",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_migrate,
    },

SQMP
migrate-set-capabilities
-------

Enable/Disable migration capabilities

- "xbzrle": xbzrle support

Arguments:

Example:

-> { "execute": "migrate-set-capabilities" , "arguments":
     { "capabilities": [ { "capability": "xbzrle", "state": true } ] } }

EQMP

    {
        .name       = "migrate-set-capabilities",
        .args_type  = "capabilities:O",
        .params     = "capability:s,state:b",
	.mhandler.cmd_new = qmp_marshal_input_migrate_set_capabilities,
    },
SQMP
query-migrate-capabilities
-------

Query current migration capabilities

- "capabilities": migration capabilities state
         - "xbzrle" : XBZRLE state (json-bool)

Arguments:

Example:

-> { "execute": "query-migrate-capabilities" }
<- { "return": [ { "state": false, "capability": "xbzrle" } ] }

EQMP

    {
        .name       = "query-migrate-capabilities",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_migrate_capabilities,
    },

SQMP
query-balloon
-------------

Show balloon information.

Make an asynchronous request for balloon info. When the request completes a
json-object will be returned containing the following data:

- "actual": current balloon value in bytes (json-int)

Example:

-> { "execute": "query-balloon" }
<- {
      "return":{
         "actual":1073741824,
      }
   }

EQMP

    {
        .name       = "query-balloon",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_balloon,
    },

    {
        .name       = "query-block-jobs",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_block_jobs,
    },

    {
        .name       = "qom-list",
        .args_type  = "path:s",
        .mhandler.cmd_new = qmp_marshal_input_qom_list,
    },

    {
        .name       = "qom-set",
	.args_type  = "path:s,property:s,value:q",
	.mhandler.cmd_new = qmp_qom_set,
    },

    {
        .name       = "qom-get",
	.args_type  = "path:s,property:s",
	.mhandler.cmd_new = qmp_qom_get,
    },

    {
        .name       = "nbd-server-start",
        .args_type  = "addr:q",
        .mhandler.cmd_new = qmp_marshal_input_nbd_server_start,
    },
    {
        .name       = "nbd-server-add",
        .args_type  = "device:B,writable:b?",
        .mhandler.cmd_new = qmp_marshal_input_nbd_server_add,
    },
    {
        .name       = "nbd-server-stop",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_nbd_server_stop,
    },

    {
        .name       = "change-vnc-password",
        .args_type  = "password:s",
        .mhandler.cmd_new = qmp_marshal_input_change_vnc_password,
    },
    {
        .name       = "qom-list-types",
        .args_type  = "implements:s?,abstract:b?",
        .mhandler.cmd_new = qmp_marshal_input_qom_list_types,
    },

    {
        .name       = "device-list-properties",
        .args_type  = "typename:s",
        .mhandler.cmd_new = qmp_marshal_input_device_list_properties,
    },

    {
        .name       = "query-machines",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_machines,
    },

    {
        .name       = "query-cpu-definitions",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_cpu_definitions,
    },

    {
        .name       = "query-target",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_target,
    },

    {
        .name       = "query-tpm",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_tpm,
    },

    {
        .name       = "query-tpm-models",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_tpm_models,
    },

    {
        .name       = "query-tpm-types",
        .args_type  = "",
        .mhandler.cmd_new = qmp_marshal_input_query_tpm_types,
    },

    {
        .name       = "chardev-add",
        .args_type  = "id:s,backend:q",
        .mhandler.cmd_new = qmp_marshal_input_chardev_add,
    },

SQMP
chardev-add
----------------

Add a chardev.

Arguments:

- "id": the chardev's ID, must be unique (json-string)
- "backend": chardev backend type + parameters

Examples:

-> { "execute" : "chardev-add",
     "arguments" : { "id" : "foo",
                     "backend" : { "type" : "null", "data" : {} } } }
<- { "return": {} }

-> { "execute" : "chardev-add",
     "arguments" : { "id" : "bar",
                     "backend" : { "type" : "file",
                                   "data" : { "out" : "/tmp/bar.log" } } } }
<- { "return": {} }

-> { "execute" : "chardev-add",
     "arguments" : { "id" : "baz",
                     "backend" : { "type" : "pty", "data" : {} } } }
<- { "return": { "pty" : "/dev/pty/42" } }

EQMP

    {
        .name       = "chardev-remove",
        .args_type  = "id:s",
        .mhandler.cmd_new = qmp_marshal_input_chardev_remove,
    },


SQMP
chardev-remove
--------------

Remove a chardev.

Arguments:

- "id": the chardev's ID, must exist and not be in use (json-string)

Example:

-> { "execute": "chardev-remove", "arguments": { "id" : "foo" } }
<- { "return": {} }

EQMP


In addition to using normal file images for the emulated storage
devices, QEMU can also use networked resources such as iSCSI devices.
These are specified using a special URL syntax.

``iSCSI``
   iSCSI support allows QEMU to access iSCSI resources directly and use
   as images for the guest storage. Both disk and cdrom images are
   supported.

   Syntax for specifying iSCSI LUNs is
   "iscsi://<target-ip>[:<port>]/<target-iqn>/<lun>"

   By default qemu will use the iSCSI initiator-name
   'iqn.2008-11.org.linux-kvm[:<name>]' but this can also be set from
   the command line or a configuration file.

   Since version Qemu 2.4 it is possible to specify a iSCSI request
   timeout to detect stalled requests and force a reestablishment of the
   session. The timeout is specified in seconds. The default is 0 which
   means no timeout. Libiscsi 1.15.0 or greater is required for this
   feature.

   Example (without authentication):

   .. parsed-literal::

      |qemu_system| -iscsi initiator-name=iqn.2001-04.com.example:my-initiator \\
                       -cdrom iscsi://192.0.2.1/iqn.2001-04.com.example/2 \\
                       -drive file=iscsi://192.0.2.1/iqn.2001-04.com.example/1

   Example (CHAP username/password via URL):

   .. parsed-literal::

      |qemu_system| -drive file=iscsi://user%password@192.0.2.1/iqn.2001-04.com.example/1

   Example (CHAP username/password via environment variables):

   .. parsed-literal::

      LIBISCSI_CHAP_USERNAME="user" \\
      LIBISCSI_CHAP_PASSWORD="password" \\
      |qemu_system| -drive file=iscsi://192.0.2.1/iqn.2001-04.com.example/1

``NBD``
   QEMU supports NBD (Network Block Devices) both using TCP protocol as
   well as Unix Domain Sockets. With TCP, the default port is 10809.

   Syntax for specifying a NBD device using TCP, in preferred URI form:
   "nbd://<server-ip>[:<port>]/[<export>]"

   Syntax for specifying a NBD device using Unix Domain Sockets;
   remember that '?' is a shell glob character and may need quoting:
   "nbd+unix:///[<export>]?socket=<domain-socket>"

   Older syntax that is also recognized:
   "nbd:<server-ip>:<port>[:exportname=<export>]"

   Syntax for specifying a NBD device using Unix Domain Sockets
   "nbd:unix:<domain-socket>[:exportname=<export>]"

   Example for TCP

   .. parsed-literal::

      |qemu_system| --drive file=nbd:192.0.2.1:30000

   Example for Unix Domain Sockets

   .. parsed-literal::

      |qemu_system| --drive file=nbd:unix:/tmp/nbd-socket

``SSH``
   QEMU supports SSH (Secure Shell) access to remote disks.

   Examples:

   .. parsed-literal::

      |qemu_system| -drive file=ssh://user@host/path/to/disk.img
      |qemu_system| -drive file.driver=ssh,file.user=user,file.host=host,file.port=22,file.path=/path/to/disk.img

   Currently authentication must be done using ssh-agent. Other
   authentication methods may be supported in future.

``GlusterFS``
   GlusterFS is a user space distributed file system. QEMU supports the
   use of GlusterFS volumes for hosting VM disk images using TCP, Unix
   Domain Sockets and RDMA transport protocols.

   Syntax for specifying a VM disk image on GlusterFS volume is

   .. parsed-literal::

      URI:
      gluster[+type]://[host[:port]]/volume/path[?socket=...][,debug=N][,logfile=...]

      JSON:
      'json:{"driver":"qcow2","file":{"driver":"gluster","volume":"testvol","path":"a.img","debug":N,"logfile":"...",
                                       "server":[{"type":"tcp","host":"...","port":"..."},
                                                 {"type":"unix","socket":"..."}]}}'

   Example

   .. parsed-literal::

      URI:
      |qemu_system| --drive file=gluster://192.0.2.1/testvol/a.img,
                                     file.debug=9,file.logfile=/var/log/qemu-gluster.log

      JSON:
      |qemu_system| 'json:{"driver":"qcow2",
                                "file":{"driver":"gluster",
                                         "volume":"testvol","path":"a.img",
                                         "debug":9,"logfile":"/var/log/qemu-gluster.log",
                                         "server":[{"type":"tcp","host":"1.2.3.4","port":24007},
                                                   {"type":"unix","socket":"/var/run/glusterd.socket"}]}}'
      |qemu_system| -drive driver=qcow2,file.driver=gluster,file.volume=testvol,file.path=/path/a.img,
                                            file.debug=9,file.logfile=/var/log/qemu-gluster.log,
                                            file.server.0.type=tcp,file.server.0.host=1.2.3.4,file.server.0.port=24007,
                                            file.server.1.type=unix,file.server.1.socket=/var/run/glusterd.socket

   See also http://www.gluster.org.

``HTTP/HTTPS/FTP/FTPS``
   QEMU supports read-only access to files accessed over http(s) and
   ftp(s).

   Syntax using a single filename:

   ::

      <protocol>://[<username>[:<password>]@]<host>/<path>

   where:

   ``protocol``
      'http', 'https', 'ftp', or 'ftps'.

   ``username``
      Optional username for authentication to the remote server.

   ``password``
      Optional password for authentication to the remote server.

   ``host``
      Address of the remote server.

   ``path``
      Path on the remote server, including any query string.

   The following options are also supported:

   ``url``
      The full URL when passing options to the driver explicitly.

   ``readahead``
      The amount of data to read ahead with each range request to the
      remote server. This value may optionally have the suffix 'T', 'G',
      'M', 'K', 'k' or 'b'. If it does not have a suffix, it will be
      assumed to be in bytes. The value must be a multiple of 512 bytes.
      It defaults to 256k.

   ``sslverify``
      Whether to verify the remote server's certificate when connecting
      over SSL. It can have the value 'on' or 'off'. It defaults to
      'on'.

   ``cookie``
      Send this cookie (it can also be a list of cookies separated by
      ';') with each outgoing request. Only supported when using
      protocols such as HTTP which support cookies, otherwise ignored.

   ``timeout``
      Set the timeout in seconds of the CURL connection. This timeout is
      the time that CURL waits for a response from the remote server to
      get the size of the image to be downloaded. If not set, the
      default timeout of 5 seconds is used.

   Note that when passing options to qemu explicitly, ``driver`` is the
   value of <protocol>.

   Example: boot from a remote Fedora 20 live ISO image

   .. parsed-literal::

      |qemu_system_x86| --drive media=cdrom,file=https://archives.fedoraproject.org/pub/archive/fedora/linux/releases/20/Live/x86_64/Fedora-Live-Desktop-x86_64-20-1.iso,readonly

      |qemu_system_x86| --drive media=cdrom,file.driver=http,file.url=http://archives.fedoraproject.org/pub/fedora/linux/releases/20/Live/x86_64/Fedora-Live-Desktop-x86_64-20-1.iso,readonly

   Example: boot from a remote Fedora 20 cloud image using a local
   overlay for writes, copy-on-read, and a readahead of 64k

   .. parsed-literal::

      qemu-img create -f qcow2 -o backing_file='json:{"file.driver":"http",, "file.url":"http://archives.fedoraproject.org/pub/archive/fedora/linux/releases/20/Images/x86_64/Fedora-x86_64-20-20131211.1-sda.qcow2",, "file.readahead":"64k"}' /tmp/Fedora-x86_64-20-20131211.1-sda.qcow2

      |qemu_system_x86| -drive file=/tmp/Fedora-x86_64-20-20131211.1-sda.qcow2,copy-on-read=on

   Example: boot from an image stored on a VMware vSphere server with a
   self-signed certificate using a local overlay for writes, a readahead
   of 64k and a timeout of 10 seconds.

   .. parsed-literal::

      qemu-img create -f qcow2 -o backing_file='json:{"file.driver":"https",, "file.url":"https://user:password@vsphere.example.com/folder/test/test-flat.vmdk?dcPath=Datacenter&dsName=datastore1",, "file.sslverify":"off",, "file.readahead":"64k",, "file.timeout":10}' /tmp/test.qcow2

      |qemu_system_x86| -drive file=/tmp/test.qcow2

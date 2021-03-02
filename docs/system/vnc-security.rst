.. _VNC security:

VNC security
------------

The VNC server capability provides access to the graphical console of
the guest VM across the network. This has a number of security
considerations depending on the deployment scenarios.

.. _vnc_005fsec_005fnone:

Without passwords
~~~~~~~~~~~~~~~~~

The simplest VNC server setup does not include any form of
authentication. For this setup it is recommended to restrict it to
listen on a UNIX domain socket only. For example

.. parsed-literal::

   |qemu_system| [...OPTIONS...] -vnc unix:/home/joebloggs/.qemu-myvm-vnc

This ensures that only users on local box with read/write access to that
path can access the VNC server. To securely access the VNC server from a
remote machine, a combination of netcat+ssh can be used to provide a
secure tunnel.

.. _vnc_005fsec_005fpassword:

With passwords
~~~~~~~~~~~~~~

The VNC protocol has limited support for password based authentication.
Since the protocol limits passwords to 8 characters it should not be
considered to provide high security. The password can be fairly easily
brute-forced by a client making repeat connections. For this reason, a
VNC server using password authentication should be restricted to only
listen on the loopback interface or UNIX domain sockets. Password
authentication is not supported when operating in FIPS 140-2 compliance
mode as it requires the use of the DES cipher. Password authentication
is requested with the ``password`` option, and then once QEMU is running
the password is set with the monitor. Until the monitor is used to set
the password all clients will be rejected.

.. parsed-literal::

   |qemu_system| [...OPTIONS...] -vnc :1,password=on -monitor stdio
   (qemu) change vnc password
   Password: ********
   (qemu)

.. _vnc_005fsec_005fcertificate:

With x509 certificates
~~~~~~~~~~~~~~~~~~~~~~

The QEMU VNC server also implements the VeNCrypt extension allowing use
of TLS for encryption of the session, and x509 certificates for
authentication. The use of x509 certificates is strongly recommended,
because TLS on its own is susceptible to man-in-the-middle attacks.
Basic x509 certificate support provides a secure session, but no
authentication. This allows any client to connect, and provides an
encrypted session.

.. parsed-literal::

   |qemu_system| [...OPTIONS...] \
     -object tls-creds-x509,id=tls0,dir=/etc/pki/qemu,endpoint=server,verify-peer=off \
     -vnc :1,tls-creds=tls0 -monitor stdio

In the above example ``/etc/pki/qemu`` should contain at least three
files, ``ca-cert.pem``, ``server-cert.pem`` and ``server-key.pem``.
Unprivileged users will want to use a private directory, for example
``$HOME/.pki/qemu``. NB the ``server-key.pem`` file should be protected
with file mode 0600 to only be readable by the user owning it.

.. _vnc_005fsec_005fcertificate_005fverify:

With x509 certificates and client verification
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Certificates can also provide a means to authenticate the client
connecting. The server will request that the client provide a
certificate, which it will then validate against the CA certificate.
This is a good choice if deploying in an environment with a private
internal certificate authority. It uses the same syntax as previously,
but with ``verify-peer`` set to ``on`` instead.

.. parsed-literal::

   |qemu_system| [...OPTIONS...] \
     -object tls-creds-x509,id=tls0,dir=/etc/pki/qemu,endpoint=server,verify-peer=on \
     -vnc :1,tls-creds=tls0 -monitor stdio

.. _vnc_005fsec_005fcertificate_005fpw:

With x509 certificates, client verification and passwords
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Finally, the previous method can be combined with VNC password
authentication to provide two layers of authentication for clients.

.. parsed-literal::

   |qemu_system| [...OPTIONS...] \
     -object tls-creds-x509,id=tls0,dir=/etc/pki/qemu,endpoint=server,verify-peer=on \
     -vnc :1,tls-creds=tls0,password=on -monitor stdio
   (qemu) change vnc password
   Password: ********
   (qemu)

.. _vnc_005fsec_005fsasl:

With SASL authentication
~~~~~~~~~~~~~~~~~~~~~~~~

The SASL authentication method is a VNC extension, that provides an
easily extendable, pluggable authentication method. This allows for
integration with a wide range of authentication mechanisms, such as PAM,
GSSAPI/Kerberos, LDAP, SQL databases, one-time keys and more. The
strength of the authentication depends on the exact mechanism
configured. If the chosen mechanism also provides a SSF layer, then it
will encrypt the datastream as well.

Refer to the later docs on how to choose the exact SASL mechanism used
for authentication, but assuming use of one supporting SSF, then QEMU
can be launched with:

.. parsed-literal::

   |qemu_system| [...OPTIONS...] -vnc :1,sasl=on -monitor stdio

.. _vnc_005fsec_005fcertificate_005fsasl:

With x509 certificates and SASL authentication
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If the desired SASL authentication mechanism does not supported SSF
layers, then it is strongly advised to run it in combination with TLS
and x509 certificates. This provides securely encrypted data stream,
avoiding risk of compromising of the security credentials. This can be
enabled, by combining the 'sasl' option with the aforementioned TLS +
x509 options:

.. parsed-literal::

   |qemu_system| [...OPTIONS...] \
     -object tls-creds-x509,id=tls0,dir=/etc/pki/qemu,endpoint=server,verify-peer=on \
     -vnc :1,tls-creds=tls0,sasl=on -monitor stdio

.. _vnc_005fsetup_005fsasl:

Configuring SASL mechanisms
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following documentation assumes use of the Cyrus SASL implementation
on a Linux host, but the principles should apply to any other SASL
implementation or host. When SASL is enabled, the mechanism
configuration will be loaded from system default SASL service config
/etc/sasl2/qemu.conf. If running QEMU as an unprivileged user, an
environment variable SASL_CONF_PATH can be used to make it search
alternate locations for the service config file.

If the TLS option is enabled for VNC, then it will provide session
encryption, otherwise the SASL mechanism will have to provide
encryption. In the latter case the list of possible plugins that can be
used is drastically reduced. In fact only the GSSAPI SASL mechanism
provides an acceptable level of security by modern standards. Previous
versions of QEMU referred to the DIGEST-MD5 mechanism, however, it has
multiple serious flaws described in detail in RFC 6331 and thus should
never be used any more. The SCRAM-SHA-1 mechanism provides a simple
username/password auth facility similar to DIGEST-MD5, but does not
support session encryption, so can only be used in combination with TLS.

When not using TLS the recommended configuration is

::

   mech_list: gssapi
   keytab: /etc/qemu/krb5.tab

This says to use the 'GSSAPI' mechanism with the Kerberos v5 protocol,
with the server principal stored in /etc/qemu/krb5.tab. For this to work
the administrator of your KDC must generate a Kerberos principal for the
server, with a name of 'qemu/somehost.example.com@EXAMPLE.COM' replacing
'somehost.example.com' with the fully qualified host name of the machine
running QEMU, and 'EXAMPLE.COM' with the Kerberos Realm.

When using TLS, if username+password authentication is desired, then a
reasonable configuration is

::

   mech_list: scram-sha-1
   sasldb_path: /etc/qemu/passwd.db

The ``saslpasswd2`` program can be used to populate the ``passwd.db``
file with accounts.

Other SASL configurations will be left as an exercise for the reader.
Note that all mechanisms, except GSSAPI, should be combined with use of
TLS to ensure a secure data channel.

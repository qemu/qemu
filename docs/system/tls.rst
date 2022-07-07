.. _network_005ftls:

TLS setup for network services
------------------------------

Almost all network services in QEMU have the ability to use TLS for
session data encryption, along with x509 certificates for simple client
authentication. What follows is a description of how to generate
certificates suitable for usage with QEMU, and applies to the VNC
server, character devices with the TCP backend, NBD server and client,
and migration server and client.

At a high level, QEMU requires certificates and private keys to be
provided in PEM format. Aside from the core fields, the certificates
should include various extension data sets, including v3 basic
constraints data, key purpose, key usage and subject alt name.

The GnuTLS package includes a command called ``certtool`` which can be
used to easily generate certificates and keys in the required format
with expected data present. Alternatively a certificate management
service may be used.

At a minimum it is necessary to setup a certificate authority, and issue
certificates to each server. If using x509 certificates for
authentication, then each client will also need to be issued a
certificate.

Assuming that the QEMU network services will only ever be exposed to
clients on a private intranet, there is no need to use a commercial
certificate authority to create certificates. A self-signed CA is
sufficient, and in fact likely to be more secure since it removes the
ability of malicious 3rd parties to trick the CA into mis-issuing certs
for impersonating your services. The only likely exception where a
commercial CA might be desirable is if enabling the VNC websockets
server and exposing it directly to remote browser clients. In such a
case it might be useful to use a commercial CA to avoid needing to
install custom CA certs in the web browsers.

The recommendation is for the server to keep its certificates in either
``/etc/pki/qemu`` or for unprivileged users in ``$HOME/.pki/qemu``.

.. _tls_005fgenerate_005fca:

Setup the Certificate Authority
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This step only needs to be performed once per organization /
organizational unit. First the CA needs a private key. This key must be
kept VERY secret and secure. If this key is compromised the entire trust
chain of the certificates issued with it is lost.

::

   # certtool --generate-privkey > ca-key.pem

To generate a self-signed certificate requires one core piece of
information, the name of the organization. A template file ``ca.info``
should be populated with the desired data to avoid having to deal with
interactive prompts from certtool::

   # cat > ca.info <<EOF
   cn = Name of your organization
   ca
   cert_signing_key
   EOF
   # certtool --generate-self-signed \
              --load-privkey ca-key.pem \
              --template ca.info \
              --outfile ca-cert.pem

The ``ca`` keyword in the template sets the v3 basic constraints
extension to indicate this certificate is for a CA, while
``cert_signing_key`` sets the key usage extension to indicate this will
be used for signing other keys. The generated ``ca-cert.pem`` file
should be copied to all servers and clients wishing to utilize TLS
support in the VNC server. The ``ca-key.pem`` must not be
disclosed/copied anywhere except the host responsible for issuing
certificates.

.. _tls_005fgenerate_005fserver:

Issuing server certificates
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each server (or host) needs to be issued with a key and certificate.
When connecting the certificate is sent to the client which validates it
against the CA certificate. The core pieces of information for a server
certificate are the hostnames and/or IP addresses that will be used by
clients when connecting. The hostname / IP address that the client
specifies when connecting will be validated against the hostname(s) and
IP address(es) recorded in the server certificate, and if no match is
found the client will close the connection.

Thus it is recommended that the server certificate include both the
fully qualified and unqualified hostnames. If the server will have
permanently assigned IP address(es), and clients are likely to use them
when connecting, they may also be included in the certificate. Both IPv4
and IPv6 addresses are supported. Historically certificates only
included 1 hostname in the ``CN`` field, however, usage of this field
for validation is now deprecated. Instead modern TLS clients will
validate against the Subject Alt Name extension data, which allows for
multiple entries. In the future usage of the ``CN`` field may be
discontinued entirely, so providing SAN extension data is strongly
recommended.

On the host holding the CA, create template files containing the
information for each server, and use it to issue server certificates.

::

   # cat > server-hostNNN.info <<EOF
   organization = Name  of your organization
   cn = hostNNN.foo.example.com
   dns_name = hostNNN
   dns_name = hostNNN.foo.example.com
   ip_address = 10.0.1.87
   ip_address = 192.8.0.92
   ip_address = 2620:0:cafe::87
   ip_address = 2001:24::92
   tls_www_server
   encryption_key
   signing_key
   EOF
   # certtool --generate-privkey > server-hostNNN-key.pem
   # certtool --generate-certificate \
              --load-ca-certificate ca-cert.pem \
              --load-ca-privkey ca-key.pem \
              --load-privkey server-hostNNN-key.pem \
              --template server-hostNNN.info \
              --outfile server-hostNNN-cert.pem

The ``dns_name`` and ``ip_address`` fields in the template are setting
the subject alt name extension data. The ``tls_www_server`` keyword is
the key purpose extension to indicate this certificate is intended for
usage in a web server. Although QEMU network services are not in fact
HTTP servers (except for VNC websockets), setting this key purpose is
still recommended. The ``encryption_key`` and ``signing_key`` keyword is
the key usage extension to indicate this certificate is intended for
usage in the data session.

The ``server-hostNNN-key.pem`` and ``server-hostNNN-cert.pem`` files
should now be securely copied to the server for which they were
generated, and renamed to ``server-key.pem`` and ``server-cert.pem``
when added to the ``/etc/pki/qemu`` directory on the target host. The
``server-key.pem`` file is security sensitive and should be kept
protected with file mode 0600 to prevent disclosure.

.. _tls_005fgenerate_005fclient:

Issuing client certificates
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The QEMU x509 TLS credential setup defaults to enabling client
verification using certificates, providing a simple authentication
mechanism. If this default is used, each client also needs to be issued
a certificate. The client certificate contains enough metadata to
uniquely identify the client with the scope of the certificate
authority. The client certificate would typically include fields for
organization, state, city, building, etc.

Once again on the host holding the CA, create template files containing
the information for each client, and use it to issue client
certificates.

::

   # cat > client-hostNNN.info <<EOF
   country = GB
   state = London
   locality = City Of London
   organization = Name of your organization
   cn = hostNNN.foo.example.com
   tls_www_client
   encryption_key
   signing_key
   EOF
   # certtool --generate-privkey > client-hostNNN-key.pem
   # certtool --generate-certificate \
              --load-ca-certificate ca-cert.pem \
              --load-ca-privkey ca-key.pem \
              --load-privkey client-hostNNN-key.pem \
              --template client-hostNNN.info \
              --outfile client-hostNNN-cert.pem

The subject alt name extension data is not required for clients, so
the ``dns_name`` and ``ip_address`` fields are not included. The
``tls_www_client`` keyword is the key purpose extension to indicate this
certificate is intended for usage in a web client. Although QEMU network
clients are not in fact HTTP clients, setting this key purpose is still
recommended. The ``encryption_key`` and ``signing_key`` keyword is the
key usage extension to indicate this certificate is intended for usage
in the data session.

The ``client-hostNNN-key.pem`` and ``client-hostNNN-cert.pem`` files
should now be securely copied to the client for which they were
generated, and renamed to ``client-key.pem`` and ``client-cert.pem``
when added to the ``/etc/pki/qemu`` directory on the target host. The
``client-key.pem`` file is security sensitive and should be kept
protected with file mode 0600 to prevent disclosure.

If a single host is going to be using TLS in both a client and server
role, it is possible to create a single certificate to cover both roles.
This would be quite common for the migration and NBD services, where a
QEMU process will be started by accepting a TLS protected incoming
migration, and later itself be migrated out to another host. To generate
a single certificate, simply include the template data from both the
client and server instructions in one.

::

   # cat > both-hostNNN.info <<EOF
   country = GB
   state = London
   locality = City Of London
   organization = Name of your organization
   cn = hostNNN.foo.example.com
   dns_name = hostNNN
   dns_name = hostNNN.foo.example.com
   ip_address = 10.0.1.87
   ip_address = 192.8.0.92
   ip_address = 2620:0:cafe::87
   ip_address = 2001:24::92
   tls_www_server
   tls_www_client
   encryption_key
   signing_key
   EOF
   # certtool --generate-privkey > both-hostNNN-key.pem
   # certtool --generate-certificate \
              --load-ca-certificate ca-cert.pem \
              --load-ca-privkey ca-key.pem \
              --load-privkey both-hostNNN-key.pem \
              --template both-hostNNN.info \
              --outfile both-hostNNN-cert.pem

When copying the PEM files to the target host, save them twice, once as
``server-cert.pem`` and ``server-key.pem``, and again as
``client-cert.pem`` and ``client-key.pem``.

.. _tls_005fcreds_005fsetup:

TLS x509 credential configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

QEMU has a standard mechanism for loading x509 credentials that will be
used for network services and clients. It requires specifying the
``tls-creds-x509`` class name to the ``--object`` command line argument
for the system emulators. Each set of credentials loaded should be given
a unique string identifier via the ``id`` parameter. A single set of TLS
credentials can be used for multiple network backends, so VNC,
migration, NBD, character devices can all share the same credentials.
Note, however, that credentials for use in a client endpoint must be
loaded separately from those used in a server endpoint.

When specifying the object, the ``dir`` parameters specifies which
directory contains the credential files. This directory is expected to
contain files with the names mentioned previously, ``ca-cert.pem``,
``server-key.pem``, ``server-cert.pem``, ``client-key.pem`` and
``client-cert.pem`` as appropriate. It is also possible to include a set
of pre-generated Diffie-Hellman (DH) parameters in a file
``dh-params.pem``, which can be created using the
``certtool --generate-dh-params`` command. If omitted, QEMU will
dynamically generate DH parameters when loading the credentials.

The ``endpoint`` parameter indicates whether the credentials will be
used for a network client or server, and determines which PEM files are
loaded.

The ``verify`` parameter determines whether x509 certificate validation
should be performed. This defaults to enabled, meaning clients will
always validate the server hostname against the certificate subject alt
name fields and/or CN field. It also means that servers will request
that clients provide a certificate and validate them. Verification
should never be turned off for client endpoints, however, it may be
turned off for server endpoints if an alternative mechanism is used to
authenticate clients. For example, the VNC server can use SASL to
authenticate clients instead.

To load server credentials with client certificate validation enabled

.. parsed-literal::

   |qemu_system| -object tls-creds-x509,id=tls0,dir=/etc/pki/qemu,endpoint=server

while to load client credentials use

.. parsed-literal::

   |qemu_system| -object tls-creds-x509,id=tls0,dir=/etc/pki/qemu,endpoint=client

Network services which support TLS will all have a ``tls-creds``
parameter which expects the ID of the TLS credentials object. For
example with VNC:

.. parsed-literal::

   |qemu_system| -vnc 0.0.0.0:0,tls-creds=tls0

.. _tls_005fpsk:

TLS Pre-Shared Keys (PSK)
~~~~~~~~~~~~~~~~~~~~~~~~~

Instead of using certificates, you may also use TLS Pre-Shared Keys
(TLS-PSK). This can be simpler to set up than certificates but is less
scalable.

Use the GnuTLS ``psktool`` program to generate a ``keys.psk`` file
containing one or more usernames and random keys::

   mkdir -m 0700 /tmp/keys
   psktool -u rich -p /tmp/keys/keys.psk

TLS-enabled servers such as ``qemu-nbd`` can use this directory like so::

   qemu-nbd \
     -t -x / \
     --object tls-creds-psk,id=tls0,endpoint=server,dir=/tmp/keys \
     --tls-creds tls0 \
     image.qcow2

When connecting from a qemu-based client you must specify the directory
containing ``keys.psk`` and an optional username (defaults to "qemu")::

   qemu-img info \
     --object tls-creds-psk,id=tls0,dir=/tmp/keys,username=rich,endpoint=client \
     --image-opts \
     file.driver=nbd,file.host=localhost,file.port=10809,file.tls-creds=tls0,file.export=/

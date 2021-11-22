.. _client authorization:

Client authorization
--------------------

When configuring a QEMU network backend with either TLS certificates or SASL
authentication, access will be granted if the client successfully proves
their identity. If the authorization identity database is scoped to the QEMU
client this may be sufficient. It is common, however, for the identity database
to be much broader and thus authentication alone does not enable sufficient
access control. In this case QEMU provides a flexible system for enforcing
finer grained authorization on clients post-authentication.

Identity providers
~~~~~~~~~~~~~~~~~~

At the time of writing there are two authentication frameworks used by QEMU
that emit an identity upon completion.

 * TLS x509 certificate distinguished name.

   When configuring the QEMU backend as a network server with TLS, there
   are a choice of credentials to use. The most common scenario is to utilize
   x509 certificates. The simplest configuration only involves issuing
   certificates to the servers, allowing the client to avoid a MITM attack
   against their intended server.

   It is possible, however, to enable mutual verification by requiring that
   the client provide a certificate to the server to prove its own identity.
   This is done by setting the property ``verify-peer=yes`` on the
   ``tls-creds-x509`` object, which is in fact the default.

   When peer verification is enabled, client will need to be issued with a
   certificate by the same certificate authority as the server. If this is
   still not sufficiently strong access control the Distinguished Name of
   the certificate can be used as an identity in the QEMU authorization
   framework.

 * SASL username.

   When configuring the QEMU backend as a network server with SASL, upon
   completion of the SASL authentication mechanism, a username will be
   provided. The format of this username will vary depending on the choice
   of mechanism configured for SASL. It might be a simple UNIX style user
   ``joebloggs``, while if using Kerberos/GSSAPI it can have a realm
   attached ``joebloggs@QEMU.ORG``.  Whatever format the username is presented
   in, it can be used with the QEMU authorization framework.

Authorization drivers
~~~~~~~~~~~~~~~~~~~~~

The QEMU authorization framework is a general purpose design with choice of
user customizable drivers. These are provided as objects that can be
created at startup using the ``-object`` argument, or at runtime using the
``object_add`` monitor command.

Simple
^^^^^^

This authorization driver provides a simple mechanism for granting access
based on an exact match against a single identity. This is useful when it is
known that only a single client is to be allowed access.

A possible use case would be when configuring QEMU for an incoming live
migration. It is known exactly which source QEMU the migration is expected
to arrive from. The x509 certificate associated with this source QEMU would
thus be used as the identity to match against. Alternatively if the virtual
machine is dedicated to a specific tenant, then the VNC server would be
configured with SASL and the username of only that tenant listed.

To create an instance of this driver via QMP:

::

   {
     "execute": "object-add",
     "arguments": {
       "qom-type": "authz-simple",
       "id": "authz0",
       "identity": "fred"
     }
   }


Or via the command line

::

   -object authz-simple,id=authz0,identity=fred


List
^^^^

In some network backends it will be desirable to grant access to a range of
clients. This authorization driver provides a list mechanism for granting
access by matching identities against a list of permitted one. Each match
rule has an associated policy and a catch all policy applies if no rule
matches. The match can either be done as an exact string comparison, or can
use the shell-like glob syntax, which allows for use of wildcards.

To create an instance of this class via QMP:

::

   {
     "execute": "object-add",
     "arguments": {
       "qom-type": "authz-list",
       "id": "authz0",
       "rules": [
          { "match": "fred", "policy": "allow", "format": "exact" },
          { "match": "bob", "policy": "allow", "format": "exact" },
          { "match": "danb", "policy": "deny", "format": "exact" },
          { "match": "dan*", "policy": "allow", "format": "glob" }
       ],
       "policy": "deny"
     }
   }


Due to the way this driver requires setting nested properties, creating
it on the command line will require use of the JSON syntax for ``-object``.
In most cases, however, the next driver will be more suitable.

List file
^^^^^^^^^

This is a variant on the previous driver that allows for a more dynamic
access control policy by storing the match rules in a standalone file
that can be reloaded automatically upon change.

To create an instance of this class via QMP:

::

   {
     "execute": "object-add",
     "arguments": {
       "qom-type": "authz-list-file",
       "id": "authz0",
       "filename": "/etc/qemu/myvm-vnc.acl",
       "refresh": true
     }
   }


If ``refresh`` is ``yes``, inotify is used to monitor for changes
to the file and auto-reload the rules.

The ``myvm-vnc.acl`` file should contain the match rules in a format that
closely matches the previous driver:

::

   {
     "rules": [
       { "match": "fred", "policy": "allow", "format": "exact" },
       { "match": "bob", "policy": "allow", "format": "exact" },
       { "match": "danb", "policy": "deny", "format": "exact" },
       { "match": "dan*", "policy": "allow", "format": "glob" }
     ],
     "policy": "deny"
   }


The object can be created on the command line using

::

   -object authz-list-file,id=authz0,\
           filename=/etc/qemu/myvm-vnc.acl,refresh=on


PAM
^^^

In some scenarios it might be desirable to integrate with authorization
mechanisms that are implemented outside of QEMU. In order to allow maximum
flexibility, QEMU provides a driver that uses the ``PAM`` framework.

To create an instance of this class via QMP:

::

   {
     "execute": "object-add",
     "arguments": {
       "qom-type": "authz-pam",
       "id": "authz0",
       "parameters": {
         "service": "qemu-vnc-tls"
       }
     }
   }


The driver only uses the PAM "account" verification
subsystem. The above config would require a config
file /etc/pam.d/qemu-vnc-tls. For a simple file
lookup it would contain

::

   account requisite  pam_listfile.so item=user sense=allow \
           file=/etc/qemu/vnc.allow


The external file would then contain a list of usernames.
If x509 cert was being used as the username, a suitable
entry would match the distinguished name:

::

   CN=laptop.berrange.com,O=Berrange Home,L=London,ST=London,C=GB


On the command line it can be created using

::

   -object authz-pam,id=authz0,service=qemu-vnc-tls


There are a variety of PAM plugins that can be used which are not illustrated
here, and it is possible to implement brand new plugins using the PAM API.


Connecting backends
~~~~~~~~~~~~~~~~~~~

The authorization driver is created using the ``-object`` argument and then
needs to be associated with a network service. The authorization driver object
will be given a unique ID that needs to be referenced.

The property to set in the network service will vary depending on the type of
identity to verify. By convention, any network server backend that uses TLS
will provide ``tls-authz`` property, while any server using SASL will provide
a ``sasl-authz`` property.

Thus an example using SASL and authorization for the VNC server would look
like:

::

   $QEMU --object authz-simple,id=authz0,identity=fred \
         --vnc 0.0.0.0:1,sasl,sasl-authz=authz0

While to validate both the x509 certificate and SASL username:

::

   echo "CN=laptop.qemu.org,O=QEMU Project,L=London,ST=London,C=GB" >> tls.acl
   $QEMU --object authz-simple,id=authz0,identity=fred \
         --object authz-list-file,id=authz1,filename=tls.acl \
	 --object tls-creds-x509,id=tls0,dir=/etc/qemu/tls,verify-peer=yes \
         --vnc 0.0.0.0:1,sasl,sasl-authz=auth0,tls-creds=tls0,tls-authz=authz1

.. _secret data:

Providing secret data to QEMU
-----------------------------

There are a variety of objects in QEMU which require secret data to be provided
by the administrator or management application. For example, network block
devices often require a password, LUKS block devices require a passphrase to
unlock key material, remote desktop services require an access password.
QEMU has a general purpose mechanism for providing secret data to QEMU in a
secure manner, using the ``secret`` object type.

At startup this can be done using the ``-object secret,...`` command line
argument. At runtime this can be done using the ``object_add`` QMP / HMP
monitor commands. The examples that follow will illustrate use of ``-object``
command lines, but they all apply equivalentely in QMP / HMP. When creating
a ``secret`` object it must be given a unique ID string. This ID is then
used to identify the object when configuring the thing which need the data.


INSECURE: Passing secrets as clear text inline
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**The following should never be done in a production environment or on a
multi-user host. Command line arguments are usually visible in the process
listings and are often collected in log files by system monitoring agents
or bug reporting tools. QMP/HMP commands and their arguments are also often
logged and attached to bug reports. This all risks compromising secrets that
are passed inline.**

For the convenience of people debugging / developing with QEMU, it is possible
to pass secret data inline on the command line.

::

   -object secret,id=secvnc0,data=87539319


Again it is possible to provide the data in base64 encoded format, which is
particularly useful if the data contains binary characters that would clash
with argument parsing.

::

   -object secret,id=secvnc0,data=ODc1MzkzMTk=,format=base64


**Note: base64 encoding does not provide any security benefit.**

Passing secrets as clear text via a file
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The simplest approach to providing data securely is to use a file to store
the secret:

::

   -object secret,id=secvnc0,file=vnc-password.txt


In this example the file ``vnc-password.txt`` contains the plain text secret
data. It is important to note that the contents of the file are treated as an
opaque blob. The entire raw file contents is used as the value, thus it is
important not to mistakenly add any trailing newline character in the file if
this newline is not intended to be part of the secret data.

In some cases it might be more convenient to pass the secret data in base64
format and have QEMU decode to get the raw bytes before use:

::

   -object secret,id=sec0,file=vnc-password.txt,format=base64


The file should generally be given mode ``0600`` or ``0400`` permissions, and
have its user/group ownership set to the same account that the QEMU process
will be launched under. If using mandatory access control such as SELinux, then
the file should be labelled to only grant access to the specific QEMU process
that needs access. This will prevent other processes/users from compromising the
secret data.


Passing secrets as cipher text inline
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To address the insecurity of passing secrets inline as clear text, it is
possible to configure a second secret as an AES key to use for decrypting
the data.

The secret used as the AES key must always be configured using the file based
storage mechanism:

::

   -object secret,id=secmaster,file=masterkey.data,format=base64


In this case the ``masterkey.data`` file would be initialized with 32
cryptographically secure random bytes, which are then base64 encoded.
The contents of this file will by used as an AES-256 key to encrypt the
real secret that can now be safely passed to QEMU inline as cipher text

::

   -object secret,id=secvnc0,keyid=secmaster,data=BASE64-CIPHERTEXT,iv=BASE64-IV,format=base64


In this example ``BASE64-CIPHERTEXT`` is the result of AES-256-CBC encrypting
the secret with ``masterkey.data`` and then base64 encoding the ciphertext.
The ``BASE64-IV`` data is 16 random bytes which have been base64 encrypted.
These bytes are used as the initialization vector for the AES-256-CBC value.

A single master key can be used to encrypt all subsequent secrets, **but it is
critical that a different initialization vector is used for every secret**.

Passing secrets via the Linux keyring
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The earlier mechanisms described are platform agnostic. If using QEMU on a Linux
host, it is further possible to pass secrets to QEMU using the Linux keyring:

::

   -object secret_keyring,id=secvnc0,serial=1729


This instructs QEMU to load data from the Linux keyring secret identified by
the serial number ``1729``. It is possible to combine use of the keyring with
other features mentioned earlier such as base64 encoding:

::

   -object secret_keyring,id=secvnc0,serial=1729,format=base64


and also encryption with a master key:

::

   -object secret_keyring,id=secvnc0,keyid=secmaster,serial=1729,iv=BASE64-IV


Best practice
~~~~~~~~~~~~~

It is recommended for production deployments to use a master key secret, and
then pass all subsequent inline secrets encrypted with the master key.

Each QEMU instance must have a distinct master key, and that must be generated
from a cryptographically secure random data source. The master key should be
deleted immediately upon QEMU shutdown. If passing the master key as a file,
the key file must have access control rules applied that restrict access to
just the one QEMU process that is intended to use it. Alternatively the Linux
keyring can be used to pass the master key to QEMU.

The secrets for individual QEMU device backends must all then be encrypted
with this master key.

This procedure helps ensure that the individual secrets for QEMU backends will
not be compromised, even if ``-object`` CLI args or ``object_add`` monitor
commands are collected in log files and attached to public bug support tickets.
The only item that needs strongly protecting is the master key file.

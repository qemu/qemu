QEMU NBD protocol support
=========================

QEMU supports the NBD protocol, and has an internal NBD client (see
``block/nbd.c``), an internal NBD server (see ``blockdev-nbd.c``), and an
external NBD server tool (see ``qemu-nbd.c``). The common code is placed
in ``nbd/*``.

The NBD protocol is specified here:
https://github.com/NetworkBlockDevice/nbd/blob/master/doc/proto.md

The following paragraphs describe some specific properties of NBD
protocol realization in QEMU.

Metadata namespaces
-------------------

QEMU supports the ``base:allocation`` metadata context as defined in the
NBD protocol specification, and also defines an additional metadata
namespace ``qemu``.

``qemu`` namespace
------------------

The ``qemu`` namespace currently contains two available metadata context
types.  The first is related to exposing the contents of a dirty
bitmap alongside the associated disk contents.  That metadata context
is named with the following form::

    qemu:dirty-bitmap:<dirty-bitmap-export-name>

Each dirty-bitmap metadata context defines only one flag for extents
in reply for ``NBD_CMD_BLOCK_STATUS``:

bit 0:
  ``NBD_STATE_DIRTY``, set when the extent is "dirty"

The second is related to exposing the source of various extents within
the image, with a single metadata context named::

    qemu:allocation-depth

In the allocation depth context, the entire 32-bit value represents a
depth of which layer in a thin-provisioned backing chain provided the
data (0 for unallocated, 1 for the active layer, 2 for the first
backing layer, and so forth).

For ``NBD_OPT_LIST_META_CONTEXT`` the following queries are supported
in addition to the specific ``qemu:allocation-depth`` and
``qemu:dirty-bitmap:<dirty-bitmap-export-name>``:

``qemu:``
  returns list of all available metadata contexts in the namespace
``qemu:dirty-bitmap:``
  returns list of all available dirty-bitmap metadata contexts

Features by version
-------------------

The following list documents which qemu version first implemented
various features (both as a server exposing the feature, and as a
client taking advantage of the feature when present), to make it
easier to plan for cross-version interoperability.  Note that in
several cases, the initial release containing a feature may require
additional patches from the corresponding stable branch to fix bugs in
the operation of that feature.

2.6
  ``NBD_OPT_STARTTLS`` with TLS X.509 Certificates
2.8
  ``NBD_CMD_WRITE_ZEROES``
2.10
  ``NBD_OPT_GO``, ``NBD_INFO_BLOCK``
2.11
  ``NBD_OPT_STRUCTURED_REPLY``
2.12
  ``NBD_CMD_BLOCK_STATUS`` for ``base:allocation``
3.0
  ``NBD_OPT_STARTTLS`` with TLS Pre-Shared Keys (PSK),
  ``NBD_CMD_BLOCK_STATUS`` for ``qemu:dirty-bitmap:``, ``NBD_CMD_CACHE``
4.2
  ``NBD_FLAG_CAN_MULTI_CONN`` for shareable read-only exports,
  ``NBD_CMD_FLAG_FAST_ZERO``
5.2
  ``NBD_CMD_BLOCK_STATUS`` for ``qemu:allocation-depth``
7.1
  ``NBD_FLAG_CAN_MULTI_CONN`` for shareable writable exports
8.2
  ``NBD_OPT_EXTENDED_HEADERS``, ``NBD_FLAG_BLOCK_STATUS_PAYLOAD``

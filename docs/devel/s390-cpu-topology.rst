QAPI interface for S390 CPU topology
====================================

The following sections will explain the QAPI interface for S390 CPU topology
with the help of exemplary output.
For this, let's assume that QEMU has been started with the following
command, defining 4 CPUs, where CPU[0] is defined by the -smp argument and will
have default values:

.. code-block:: bash

 qemu-system-s390x \
    -enable-kvm \
    -cpu z14,ctop=on \
    -smp 1,drawers=3,books=3,sockets=2,cores=2,maxcpus=36 \
    -device z14-s390x-cpu,core-id=19,entitlement=high \
    -device z14-s390x-cpu,core-id=11,entitlement=low \
    -device z14-s390x-cpu,core-id=12,entitlement=high \
   ...

Additions to query-cpus-fast
----------------------------

The command query-cpus-fast allows querying the topology tree and
modifiers for all configured vCPUs.

.. code-block:: QMP

 { "execute": "query-cpus-fast" }
 {
  "return": [
    {
      "dedicated": false,
      "thread-id": 536993,
      "props": {
        "core-id": 0,
        "socket-id": 0,
        "drawer-id": 0,
        "book-id": 0
      },
      "cpu-state": "operating",
      "entitlement": "medium",
      "qom-path": "/machine/unattached/device[0]",
      "cpu-index": 0,
      "target": "s390x"
    },
    {
      "dedicated": false,
      "thread-id": 537003,
      "props": {
        "core-id": 19,
        "socket-id": 1,
        "drawer-id": 0,
        "book-id": 2
      },
      "cpu-state": "operating",
      "entitlement": "high",
      "qom-path": "/machine/peripheral-anon/device[0]",
      "cpu-index": 19,
      "target": "s390x"
    },
    {
      "dedicated": false,
      "thread-id": 537004,
      "props": {
        "core-id": 11,
        "socket-id": 1,
        "drawer-id": 0,
        "book-id": 1
      },
      "cpu-state": "operating",
      "entitlement": "low",
      "qom-path": "/machine/peripheral-anon/device[1]",
      "cpu-index": 11,
      "target": "s390x"
    },
    {
      "dedicated": true,
      "thread-id": 537005,
      "props": {
        "core-id": 12,
        "socket-id": 0,
        "drawer-id": 3,
        "book-id": 2
      },
      "cpu-state": "operating",
      "entitlement": "high",
      "qom-path": "/machine/peripheral-anon/device[2]",
      "cpu-index": 12,
      "target": "s390x"
    }
  ]
 }


QAPI command: set-cpu-topology
------------------------------

The command set-cpu-topology allows modifying the topology tree
or the topology modifiers of a vCPU in the configuration.

.. code-block:: QMP

    { "execute": "set-cpu-topology",
      "arguments": {
         "core-id": 11,
         "socket-id": 0,
         "book-id": 0,
         "drawer-id": 0,
         "entitlement": "low",
         "dedicated": false
      }
    }
    {"return": {}}

The core-id parameter is the only mandatory parameter and every
unspecified parameter keeps its previous value.

QAPI event CPU_POLARIZATION_CHANGE
----------------------------------

When a guest requests a modification of the polarization,
QEMU sends a CPU_POLARIZATION_CHANGE event.

When requesting the change, the guest only specifies horizontal or
vertical polarization.
It is the job of the entity administrating QEMU to set the dedication and fine
grained vertical entitlement in response to this event.

Note that a vertical polarized dedicated vCPU can only have a high
entitlement, giving 6 possibilities for vCPU polarization:

- Horizontal
- Horizontal dedicated
- Vertical low
- Vertical medium
- Vertical high
- Vertical high dedicated

Example of the event received when the guest issues the CPU instruction
Perform Topology Function PTF(0) to request an horizontal polarization:

.. code-block:: QMP

  {
    "timestamp": {
      "seconds": 1687870305,
      "microseconds": 566299
    },
    "event": "CPU_POLARIZATION_CHANGE",
    "data": {
      "polarization": "horizontal"
    }
  }

QAPI query command: query-s390x-cpu-polarization
------------------------------------------------

The query command query-s390x-cpu-polarization returns the current
CPU polarization of the machine.
In this case the guest previously issued a PTF(1) to request vertical polarization:

.. code-block:: QMP

    { "execute": "query-s390x-cpu-polarization" }
    {
        "return": {
          "polarization": "vertical"
        }
    }

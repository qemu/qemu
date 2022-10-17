/*
 * Virtio Support
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qapi-commands-virtio.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qapi-visit-virtio.h"
#include "qapi/qmp/qjson.h"
#include "cpu.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"
#include "hw/virtio/virtio.h"
#include "migration/qemu-file-types.h"
#include "qemu/atomic.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-access.h"
#include "sysemu/dma.h"
#include "sysemu/runstate.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/vhost_types.h"
#include "standard-headers/linux/virtio_blk.h"
#include "standard-headers/linux/virtio_console.h"
#include "standard-headers/linux/virtio_gpu.h"
#include "standard-headers/linux/virtio_net.h"
#include "standard-headers/linux/virtio_scsi.h"
#include "standard-headers/linux/virtio_i2c.h"
#include "standard-headers/linux/virtio_balloon.h"
#include "standard-headers/linux/virtio_iommu.h"
#include "standard-headers/linux/virtio_mem.h"
#include "standard-headers/linux/virtio_vsock.h"
#include CONFIG_DEVICES

/* QAPI list of realized VirtIODevices */
static QTAILQ_HEAD(, VirtIODevice) virtio_list;

/*
 * Maximum size of virtio device config space
 */
#define VHOST_USER_MAX_CONFIG_SIZE 256

#define FEATURE_ENTRY(name, desc) (qmp_virtio_feature_map_t) \
    { .virtio_bit = name, .feature_desc = desc }

enum VhostUserProtocolFeature {
    VHOST_USER_PROTOCOL_F_MQ = 0,
    VHOST_USER_PROTOCOL_F_LOG_SHMFD = 1,
    VHOST_USER_PROTOCOL_F_RARP = 2,
    VHOST_USER_PROTOCOL_F_REPLY_ACK = 3,
    VHOST_USER_PROTOCOL_F_NET_MTU = 4,
    VHOST_USER_PROTOCOL_F_SLAVE_REQ = 5,
    VHOST_USER_PROTOCOL_F_CROSS_ENDIAN = 6,
    VHOST_USER_PROTOCOL_F_CRYPTO_SESSION = 7,
    VHOST_USER_PROTOCOL_F_PAGEFAULT = 8,
    VHOST_USER_PROTOCOL_F_CONFIG = 9,
    VHOST_USER_PROTOCOL_F_SLAVE_SEND_FD = 10,
    VHOST_USER_PROTOCOL_F_HOST_NOTIFIER = 11,
    VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD = 12,
    VHOST_USER_PROTOCOL_F_RESET_DEVICE = 13,
    VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS = 14,
    VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS = 15,
    VHOST_USER_PROTOCOL_F_MAX
};

/* Virtio transport features mapping */
static qmp_virtio_feature_map_t virtio_transport_map[] = {
    /* Virtio device transport features */
#ifndef VIRTIO_CONFIG_NO_LEGACY
    FEATURE_ENTRY(VIRTIO_F_NOTIFY_ON_EMPTY, \
            "VIRTIO_F_NOTIFY_ON_EMPTY: Notify when device runs out of avail. "
            "descs. on VQ"),
    FEATURE_ENTRY(VIRTIO_F_ANY_LAYOUT, \
            "VIRTIO_F_ANY_LAYOUT: Device accepts arbitrary desc. layouts"),
#endif /* !VIRTIO_CONFIG_NO_LEGACY */
    FEATURE_ENTRY(VIRTIO_F_VERSION_1, \
            "VIRTIO_F_VERSION_1: Device compliant for v1 spec (legacy)"),
    FEATURE_ENTRY(VIRTIO_F_IOMMU_PLATFORM, \
            "VIRTIO_F_IOMMU_PLATFORM: Device can be used on IOMMU platform"),
    FEATURE_ENTRY(VIRTIO_F_RING_PACKED, \
            "VIRTIO_F_RING_PACKED: Device supports packed VQ layout"),
    FEATURE_ENTRY(VIRTIO_F_IN_ORDER, \
            "VIRTIO_F_IN_ORDER: Device uses buffers in same order as made "
            "available by driver"),
    FEATURE_ENTRY(VIRTIO_F_ORDER_PLATFORM, \
            "VIRTIO_F_ORDER_PLATFORM: Memory accesses ordered by platform"),
    FEATURE_ENTRY(VIRTIO_F_SR_IOV, \
            "VIRTIO_F_SR_IOV: Device supports single root I/O virtualization"),
    /* Virtio ring transport features */
    FEATURE_ENTRY(VIRTIO_RING_F_INDIRECT_DESC, \
            "VIRTIO_RING_F_INDIRECT_DESC: Indirect descriptors supported"),
    FEATURE_ENTRY(VIRTIO_RING_F_EVENT_IDX, \
            "VIRTIO_RING_F_EVENT_IDX: Used & avail. event fields enabled"),
    { -1, "" }
};

/* Vhost-user protocol features mapping */
static qmp_virtio_feature_map_t vhost_user_protocol_map[] = {
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_MQ, \
            "VHOST_USER_PROTOCOL_F_MQ: Multiqueue protocol supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_LOG_SHMFD, \
            "VHOST_USER_PROTOCOL_F_LOG_SHMFD: Shared log memory fd supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_RARP, \
            "VHOST_USER_PROTOCOL_F_RARP: Vhost-user back-end RARP broadcasting "
            "supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_REPLY_ACK, \
            "VHOST_USER_PROTOCOL_F_REPLY_ACK: Requested operation status ack. "
            "supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_NET_MTU, \
            "VHOST_USER_PROTOCOL_F_NET_MTU: Expose host MTU to guest supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_SLAVE_REQ, \
            "VHOST_USER_PROTOCOL_F_SLAVE_REQ: Socket fd for back-end initiated "
            "requests supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_CROSS_ENDIAN, \
            "VHOST_USER_PROTOCOL_F_CROSS_ENDIAN: Endianness of VQs for legacy "
            "devices supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_CRYPTO_SESSION, \
            "VHOST_USER_PROTOCOL_F_CRYPTO_SESSION: Session creation for crypto "
            "operations supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_PAGEFAULT, \
            "VHOST_USER_PROTOCOL_F_PAGEFAULT: Request servicing on userfaultfd "
            "for accessed pages supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_CONFIG, \
            "VHOST_USER_PROTOCOL_F_CONFIG: Vhost-user messaging for virtio "
            "device configuration space supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_SLAVE_SEND_FD, \
            "VHOST_USER_PROTOCOL_F_SLAVE_SEND_FD: Slave fd communication "
            "channel supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_HOST_NOTIFIER, \
            "VHOST_USER_PROTOCOL_F_HOST_NOTIFIER: Host notifiers for specified "
            "VQs supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD, \
            "VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD: Shared inflight I/O buffers "
            "supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_RESET_DEVICE, \
            "VHOST_USER_PROTOCOL_F_RESET_DEVICE: Disabling all rings and "
            "resetting internal device state supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS, \
            "VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS: In-band messaging "
            "supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS, \
            "VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS: Configuration for "
            "memory slots supported"),
    { -1, "" }
};

/* virtio device configuration statuses */
static qmp_virtio_feature_map_t virtio_config_status_map[] = {
    FEATURE_ENTRY(VIRTIO_CONFIG_S_DRIVER_OK, \
            "VIRTIO_CONFIG_S_DRIVER_OK: Driver setup and ready"),
    FEATURE_ENTRY(VIRTIO_CONFIG_S_FEATURES_OK, \
            "VIRTIO_CONFIG_S_FEATURES_OK: Feature negotiation complete"),
    FEATURE_ENTRY(VIRTIO_CONFIG_S_DRIVER, \
            "VIRTIO_CONFIG_S_DRIVER: Guest OS compatible with device"),
    FEATURE_ENTRY(VIRTIO_CONFIG_S_NEEDS_RESET, \
            "VIRTIO_CONFIG_S_NEEDS_RESET: Irrecoverable error, device needs "
            "reset"),
    FEATURE_ENTRY(VIRTIO_CONFIG_S_FAILED, \
            "VIRTIO_CONFIG_S_FAILED: Error in guest, device failed"),
    FEATURE_ENTRY(VIRTIO_CONFIG_S_ACKNOWLEDGE, \
            "VIRTIO_CONFIG_S_ACKNOWLEDGE: Valid virtio device found"),
    { -1, "" }
};

/* virtio-blk features mapping */
qmp_virtio_feature_map_t virtio_blk_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_BLK_F_SIZE_MAX, \
            "VIRTIO_BLK_F_SIZE_MAX: Max segment size is size_max"),
    FEATURE_ENTRY(VIRTIO_BLK_F_SEG_MAX, \
            "VIRTIO_BLK_F_SEG_MAX: Max segments in a request is seg_max"),
    FEATURE_ENTRY(VIRTIO_BLK_F_GEOMETRY, \
            "VIRTIO_BLK_F_GEOMETRY: Legacy geometry available"),
    FEATURE_ENTRY(VIRTIO_BLK_F_RO, \
            "VIRTIO_BLK_F_RO: Device is read-only"),
    FEATURE_ENTRY(VIRTIO_BLK_F_BLK_SIZE, \
            "VIRTIO_BLK_F_BLK_SIZE: Block size of disk available"),
    FEATURE_ENTRY(VIRTIO_BLK_F_TOPOLOGY, \
            "VIRTIO_BLK_F_TOPOLOGY: Topology information available"),
    FEATURE_ENTRY(VIRTIO_BLK_F_MQ, \
            "VIRTIO_BLK_F_MQ: Multiqueue supported"),
    FEATURE_ENTRY(VIRTIO_BLK_F_DISCARD, \
            "VIRTIO_BLK_F_DISCARD: Discard command supported"),
    FEATURE_ENTRY(VIRTIO_BLK_F_WRITE_ZEROES, \
            "VIRTIO_BLK_F_WRITE_ZEROES: Write zeroes command supported"),
#ifndef VIRTIO_BLK_NO_LEGACY
    FEATURE_ENTRY(VIRTIO_BLK_F_BARRIER, \
            "VIRTIO_BLK_F_BARRIER: Request barriers supported"),
    FEATURE_ENTRY(VIRTIO_BLK_F_SCSI, \
            "VIRTIO_BLK_F_SCSI: SCSI packet commands supported"),
    FEATURE_ENTRY(VIRTIO_BLK_F_FLUSH, \
            "VIRTIO_BLK_F_FLUSH: Flush command supported"),
    FEATURE_ENTRY(VIRTIO_BLK_F_CONFIG_WCE, \
            "VIRTIO_BLK_F_CONFIG_WCE: Cache writeback and writethrough modes "
            "supported"),
#endif /* !VIRTIO_BLK_NO_LEGACY */
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};

/* virtio-serial features mapping */
qmp_virtio_feature_map_t virtio_serial_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_CONSOLE_F_SIZE, \
            "VIRTIO_CONSOLE_F_SIZE: Host providing console size"),
    FEATURE_ENTRY(VIRTIO_CONSOLE_F_MULTIPORT, \
            "VIRTIO_CONSOLE_F_MULTIPORT: Multiple ports for device supported"),
    FEATURE_ENTRY(VIRTIO_CONSOLE_F_EMERG_WRITE, \
            "VIRTIO_CONSOLE_F_EMERG_WRITE: Emergency write supported"),
    { -1, "" }
};

/* virtio-gpu features mapping */
qmp_virtio_feature_map_t virtio_gpu_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_GPU_F_VIRGL, \
            "VIRTIO_GPU_F_VIRGL: Virgl 3D mode supported"),
    FEATURE_ENTRY(VIRTIO_GPU_F_EDID, \
            "VIRTIO_GPU_F_EDID: EDID metadata supported"),
    FEATURE_ENTRY(VIRTIO_GPU_F_RESOURCE_UUID, \
            "VIRTIO_GPU_F_RESOURCE_UUID: Resource UUID assigning supported"),
    FEATURE_ENTRY(VIRTIO_GPU_F_RESOURCE_BLOB, \
            "VIRTIO_GPU_F_RESOURCE_BLOB: Size-based blob resources supported"),
    FEATURE_ENTRY(VIRTIO_GPU_F_CONTEXT_INIT, \
            "VIRTIO_GPU_F_CONTEXT_INIT: Context types and synchronization "
            "timelines supported"),
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};

/* virtio-input features mapping */
qmp_virtio_feature_map_t virtio_input_feature_map[] = {
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};

/* virtio-net features mapping */
qmp_virtio_feature_map_t virtio_net_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_NET_F_CSUM, \
            "VIRTIO_NET_F_CSUM: Device handling packets with partial checksum "
            "supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_GUEST_CSUM, \
            "VIRTIO_NET_F_GUEST_CSUM: Driver handling packets with partial "
            "checksum supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_CTRL_GUEST_OFFLOADS, \
            "VIRTIO_NET_F_CTRL_GUEST_OFFLOADS: Control channel offloading "
            "reconfig. supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_MTU, \
            "VIRTIO_NET_F_MTU: Device max MTU reporting supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_MAC, \
            "VIRTIO_NET_F_MAC: Device has given MAC address"),
    FEATURE_ENTRY(VIRTIO_NET_F_GUEST_TSO4, \
            "VIRTIO_NET_F_GUEST_TSO4: Driver can receive TSOv4"),
    FEATURE_ENTRY(VIRTIO_NET_F_GUEST_TSO6, \
            "VIRTIO_NET_F_GUEST_TSO6: Driver can receive TSOv6"),
    FEATURE_ENTRY(VIRTIO_NET_F_GUEST_ECN, \
            "VIRTIO_NET_F_GUEST_ECN: Driver can receive TSO with ECN"),
    FEATURE_ENTRY(VIRTIO_NET_F_GUEST_UFO, \
            "VIRTIO_NET_F_GUEST_UFO: Driver can receive UFO"),
    FEATURE_ENTRY(VIRTIO_NET_F_HOST_TSO4, \
            "VIRTIO_NET_F_HOST_TSO4: Device can receive TSOv4"),
    FEATURE_ENTRY(VIRTIO_NET_F_HOST_TSO6, \
            "VIRTIO_NET_F_HOST_TSO6: Device can receive TSOv6"),
    FEATURE_ENTRY(VIRTIO_NET_F_HOST_ECN, \
            "VIRTIO_NET_F_HOST_ECN: Device can receive TSO with ECN"),
    FEATURE_ENTRY(VIRTIO_NET_F_HOST_UFO, \
            "VIRTIO_NET_F_HOST_UFO: Device can receive UFO"),
    FEATURE_ENTRY(VIRTIO_NET_F_MRG_RXBUF, \
            "VIRTIO_NET_F_MRG_RXBUF: Driver can merge receive buffers"),
    FEATURE_ENTRY(VIRTIO_NET_F_STATUS, \
            "VIRTIO_NET_F_STATUS: Configuration status field available"),
    FEATURE_ENTRY(VIRTIO_NET_F_CTRL_VQ, \
            "VIRTIO_NET_F_CTRL_VQ: Control channel available"),
    FEATURE_ENTRY(VIRTIO_NET_F_CTRL_RX, \
            "VIRTIO_NET_F_CTRL_RX: Control channel RX mode supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_CTRL_VLAN, \
            "VIRTIO_NET_F_CTRL_VLAN: Control channel VLAN filtering supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_CTRL_RX_EXTRA, \
            "VIRTIO_NET_F_CTRL_RX_EXTRA: Extra RX mode control supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_GUEST_ANNOUNCE, \
            "VIRTIO_NET_F_GUEST_ANNOUNCE: Driver sending gratuitous packets "
            "supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_MQ, \
            "VIRTIO_NET_F_MQ: Multiqueue with automatic receive steering "
            "supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_CTRL_MAC_ADDR, \
            "VIRTIO_NET_F_CTRL_MAC_ADDR: MAC address set through control "
            "channel"),
    FEATURE_ENTRY(VIRTIO_NET_F_HASH_REPORT, \
            "VIRTIO_NET_F_HASH_REPORT: Hash reporting supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_RSS, \
            "VIRTIO_NET_F_RSS: RSS RX steering supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_RSC_EXT, \
            "VIRTIO_NET_F_RSC_EXT: Extended coalescing info supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_STANDBY, \
            "VIRTIO_NET_F_STANDBY: Device acting as standby for primary "
            "device with same MAC addr. supported"),
    FEATURE_ENTRY(VIRTIO_NET_F_SPEED_DUPLEX, \
            "VIRTIO_NET_F_SPEED_DUPLEX: Device set linkspeed and duplex"),
#ifndef VIRTIO_NET_NO_LEGACY
    FEATURE_ENTRY(VIRTIO_NET_F_GSO, \
            "VIRTIO_NET_F_GSO: Handling GSO-type packets supported"),
#endif /* !VIRTIO_NET_NO_LEGACY */
    FEATURE_ENTRY(VHOST_NET_F_VIRTIO_NET_HDR, \
            "VHOST_NET_F_VIRTIO_NET_HDR: Virtio-net headers for RX and TX "
            "packets supported"),
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};

/* virtio-scsi features mapping */
qmp_virtio_feature_map_t virtio_scsi_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_SCSI_F_INOUT, \
            "VIRTIO_SCSI_F_INOUT: Requests including read and writable data "
            "buffers suppoted"),
    FEATURE_ENTRY(VIRTIO_SCSI_F_HOTPLUG, \
            "VIRTIO_SCSI_F_HOTPLUG: Reporting and handling hot-plug events "
            "supported"),
    FEATURE_ENTRY(VIRTIO_SCSI_F_CHANGE, \
            "VIRTIO_SCSI_F_CHANGE: Reporting and handling LUN changes "
            "supported"),
    FEATURE_ENTRY(VIRTIO_SCSI_F_T10_PI, \
            "VIRTIO_SCSI_F_T10_PI: T10 info included in request header"),
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};

/* virtio/vhost-user-fs features mapping */
qmp_virtio_feature_map_t virtio_fs_feature_map[] = {
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};

/* virtio/vhost-user-i2c features mapping */
qmp_virtio_feature_map_t virtio_i2c_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_I2C_F_ZERO_LENGTH_REQUEST, \
            "VIRTIO_I2C_F_ZERO_LEGNTH_REQUEST: Zero length requests supported"),
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};

/* virtio/vhost-vsock features mapping */
qmp_virtio_feature_map_t virtio_vsock_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_VSOCK_F_SEQPACKET, \
            "VIRTIO_VSOCK_F_SEQPACKET: SOCK_SEQPACKET supported"),
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};

/* virtio-balloon features mapping */
qmp_virtio_feature_map_t virtio_balloon_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_BALLOON_F_MUST_TELL_HOST, \
            "VIRTIO_BALLOON_F_MUST_TELL_HOST: Tell host before reclaiming "
            "pages"),
    FEATURE_ENTRY(VIRTIO_BALLOON_F_STATS_VQ, \
            "VIRTIO_BALLOON_F_STATS_VQ: Guest memory stats VQ available"),
    FEATURE_ENTRY(VIRTIO_BALLOON_F_DEFLATE_ON_OOM, \
            "VIRTIO_BALLOON_F_DEFLATE_ON_OOM: Deflate balloon when guest OOM"),
    FEATURE_ENTRY(VIRTIO_BALLOON_F_FREE_PAGE_HINT, \
            "VIRTIO_BALLOON_F_FREE_PAGE_HINT: VQ reporting free pages enabled"),
    FEATURE_ENTRY(VIRTIO_BALLOON_F_PAGE_POISON, \
            "VIRTIO_BALLOON_F_PAGE_POISON: Guest page poisoning enabled"),
    FEATURE_ENTRY(VIRTIO_BALLOON_F_REPORTING, \
            "VIRTIO_BALLOON_F_REPORTING: Page reporting VQ enabled"),
    { -1, "" }
};

/* virtio-crypto features mapping */
qmp_virtio_feature_map_t virtio_crypto_feature_map[] = {
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    { -1, "" }
};

/* virtio-iommu features mapping */
qmp_virtio_feature_map_t virtio_iommu_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_IOMMU_F_INPUT_RANGE, \
            "VIRTIO_IOMMU_F_INPUT_RANGE: Range of available virtual addrs. "
            "available"),
    FEATURE_ENTRY(VIRTIO_IOMMU_F_DOMAIN_RANGE, \
            "VIRTIO_IOMMU_F_DOMAIN_RANGE: Number of supported domains "
            "available"),
    FEATURE_ENTRY(VIRTIO_IOMMU_F_MAP_UNMAP, \
            "VIRTIO_IOMMU_F_MAP_UNMAP: Map and unmap requests available"),
    FEATURE_ENTRY(VIRTIO_IOMMU_F_BYPASS, \
            "VIRTIO_IOMMU_F_BYPASS: Endpoints not attached to domains are in "
            "bypass mode"),
    FEATURE_ENTRY(VIRTIO_IOMMU_F_PROBE, \
            "VIRTIO_IOMMU_F_PROBE: Probe requests available"),
    FEATURE_ENTRY(VIRTIO_IOMMU_F_MMIO, \
            "VIRTIO_IOMMU_F_MMIO: VIRTIO_IOMMU_MAP_F_MMIO flag available"),
    FEATURE_ENTRY(VIRTIO_IOMMU_F_BYPASS_CONFIG, \
            "VIRTIO_IOMMU_F_BYPASS_CONFIG: Bypass field of IOMMU config "
            "available"),
    { -1, "" }
};

/* virtio-mem features mapping */
qmp_virtio_feature_map_t virtio_mem_feature_map[] = {
#ifndef CONFIG_ACPI
    FEATURE_ENTRY(VIRTIO_MEM_F_ACPI_PXM, \
            "VIRTIO_MEM_F_ACPI_PXM: node_id is an ACPI PXM and is valid"),
#endif /* !CONFIG_ACPI */
    FEATURE_ENTRY(VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE, \
            "VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE: Unplugged memory cannot be "
            "accessed"),
    { -1, "" }
};

/* virtio-rng features mapping */
qmp_virtio_feature_map_t virtio_rng_feature_map[] = {
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};

/*
 * The alignment to use between consumer and producer parts of vring.
 * x86 pagesize again. This is the default, used by transports like PCI
 * which don't provide a means for the guest to tell the host the alignment.
 */
#define VIRTIO_PCI_VRING_ALIGN         4096

typedef struct VRingDesc
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} VRingDesc;

typedef struct VRingPackedDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t id;
    uint16_t flags;
} VRingPackedDesc;

typedef struct VRingAvail
{
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} VRingAvail;

typedef struct VRingUsedElem
{
    uint32_t id;
    uint32_t len;
} VRingUsedElem;

typedef struct VRingUsed
{
    uint16_t flags;
    uint16_t idx;
    VRingUsedElem ring[];
} VRingUsed;

typedef struct VRingMemoryRegionCaches {
    struct rcu_head rcu;
    MemoryRegionCache desc;
    MemoryRegionCache avail;
    MemoryRegionCache used;
} VRingMemoryRegionCaches;

typedef struct VRing
{
    unsigned int num;
    unsigned int num_default;
    unsigned int align;
    hwaddr desc;
    hwaddr avail;
    hwaddr used;
    VRingMemoryRegionCaches *caches;
} VRing;

typedef struct VRingPackedDescEvent {
    uint16_t off_wrap;
    uint16_t flags;
} VRingPackedDescEvent ;

struct VirtQueue
{
    VRing vring;
    VirtQueueElement *used_elems;

    /* Next head to pop */
    uint16_t last_avail_idx;
    bool last_avail_wrap_counter;

    /* Last avail_idx read from VQ. */
    uint16_t shadow_avail_idx;
    bool shadow_avail_wrap_counter;

    uint16_t used_idx;
    bool used_wrap_counter;

    /* Last used index value we have signalled on */
    uint16_t signalled_used;

    /* Last used index value we have signalled on */
    bool signalled_used_valid;

    /* Notification enabled? */
    bool notification;

    uint16_t queue_index;

    unsigned int inuse;

    uint16_t vector;
    VirtIOHandleOutput handle_output;
    VirtIODevice *vdev;
    EventNotifier guest_notifier;
    EventNotifier host_notifier;
    bool host_notifier_enabled;
    QLIST_ENTRY(VirtQueue) node;
};

const char *virtio_device_names[] = {
    [VIRTIO_ID_NET] = "virtio-net",
    [VIRTIO_ID_BLOCK] = "virtio-blk",
    [VIRTIO_ID_CONSOLE] = "virtio-serial",
    [VIRTIO_ID_RNG] = "virtio-rng",
    [VIRTIO_ID_BALLOON] = "virtio-balloon",
    [VIRTIO_ID_IOMEM] = "virtio-iomem",
    [VIRTIO_ID_RPMSG] = "virtio-rpmsg",
    [VIRTIO_ID_SCSI] = "virtio-scsi",
    [VIRTIO_ID_9P] = "virtio-9p",
    [VIRTIO_ID_MAC80211_WLAN] = "virtio-mac-wlan",
    [VIRTIO_ID_RPROC_SERIAL] = "virtio-rproc-serial",
    [VIRTIO_ID_CAIF] = "virtio-caif",
    [VIRTIO_ID_MEMORY_BALLOON] = "virtio-mem-balloon",
    [VIRTIO_ID_GPU] = "virtio-gpu",
    [VIRTIO_ID_CLOCK] = "virtio-clk",
    [VIRTIO_ID_INPUT] = "virtio-input",
    [VIRTIO_ID_VSOCK] = "vhost-vsock",
    [VIRTIO_ID_CRYPTO] = "virtio-crypto",
    [VIRTIO_ID_SIGNAL_DIST] = "virtio-signal",
    [VIRTIO_ID_PSTORE] = "virtio-pstore",
    [VIRTIO_ID_IOMMU] = "virtio-iommu",
    [VIRTIO_ID_MEM] = "virtio-mem",
    [VIRTIO_ID_SOUND] = "virtio-sound",
    [VIRTIO_ID_FS] = "virtio-user-fs",
    [VIRTIO_ID_PMEM] = "virtio-pmem",
    [VIRTIO_ID_RPMB] = "virtio-rpmb",
    [VIRTIO_ID_MAC80211_HWSIM] = "virtio-mac-hwsim",
    [VIRTIO_ID_VIDEO_ENCODER] = "virtio-vid-encoder",
    [VIRTIO_ID_VIDEO_DECODER] = "virtio-vid-decoder",
    [VIRTIO_ID_SCMI] = "virtio-scmi",
    [VIRTIO_ID_NITRO_SEC_MOD] = "virtio-nitro-sec-mod",
    [VIRTIO_ID_I2C_ADAPTER] = "vhost-user-i2c",
    [VIRTIO_ID_WATCHDOG] = "virtio-watchdog",
    [VIRTIO_ID_CAN] = "virtio-can",
    [VIRTIO_ID_DMABUF] = "virtio-dmabuf",
    [VIRTIO_ID_PARAM_SERV] = "virtio-param-serv",
    [VIRTIO_ID_AUDIO_POLICY] = "virtio-audio-pol",
    [VIRTIO_ID_BT] = "virtio-bluetooth",
    [VIRTIO_ID_GPIO] = "virtio-gpio"
};

static const char *virtio_id_to_name(uint16_t device_id)
{
    assert(device_id < G_N_ELEMENTS(virtio_device_names));
    const char *name = virtio_device_names[device_id];
    assert(name != NULL);
    return name;
}

/* Called within call_rcu().  */
static void virtio_free_region_cache(VRingMemoryRegionCaches *caches)
{
    assert(caches != NULL);
    address_space_cache_destroy(&caches->desc);
    address_space_cache_destroy(&caches->avail);
    address_space_cache_destroy(&caches->used);
    g_free(caches);
}

static void virtio_virtqueue_reset_region_cache(struct VirtQueue *vq)
{
    VRingMemoryRegionCaches *caches;

    caches = qatomic_read(&vq->vring.caches);
    qatomic_rcu_set(&vq->vring.caches, NULL);
    if (caches) {
        call_rcu(caches, virtio_free_region_cache, rcu);
    }
}

static void virtio_init_region_cache(VirtIODevice *vdev, int n)
{
    VirtQueue *vq = &vdev->vq[n];
    VRingMemoryRegionCaches *old = vq->vring.caches;
    VRingMemoryRegionCaches *new = NULL;
    hwaddr addr, size;
    int64_t len;
    bool packed;


    addr = vq->vring.desc;
    if (!addr) {
        goto out_no_cache;
    }
    new = g_new0(VRingMemoryRegionCaches, 1);
    size = virtio_queue_get_desc_size(vdev, n);
    packed = virtio_vdev_has_feature(vq->vdev, VIRTIO_F_RING_PACKED) ?
                                   true : false;
    len = address_space_cache_init(&new->desc, vdev->dma_as,
                                   addr, size, packed);
    if (len < size) {
        virtio_error(vdev, "Cannot map desc");
        goto err_desc;
    }

    size = virtio_queue_get_used_size(vdev, n);
    len = address_space_cache_init(&new->used, vdev->dma_as,
                                   vq->vring.used, size, true);
    if (len < size) {
        virtio_error(vdev, "Cannot map used");
        goto err_used;
    }

    size = virtio_queue_get_avail_size(vdev, n);
    len = address_space_cache_init(&new->avail, vdev->dma_as,
                                   vq->vring.avail, size, false);
    if (len < size) {
        virtio_error(vdev, "Cannot map avail");
        goto err_avail;
    }

    qatomic_rcu_set(&vq->vring.caches, new);
    if (old) {
        call_rcu(old, virtio_free_region_cache, rcu);
    }
    return;

err_avail:
    address_space_cache_destroy(&new->avail);
err_used:
    address_space_cache_destroy(&new->used);
err_desc:
    address_space_cache_destroy(&new->desc);
out_no_cache:
    g_free(new);
    virtio_virtqueue_reset_region_cache(vq);
}

/* virt queue functions */
void virtio_queue_update_rings(VirtIODevice *vdev, int n)
{
    VRing *vring = &vdev->vq[n].vring;

    if (!vring->num || !vring->desc || !vring->align) {
        /* not yet setup -> nothing to do */
        return;
    }
    vring->avail = vring->desc + vring->num * sizeof(VRingDesc);
    vring->used = vring_align(vring->avail +
                              offsetof(VRingAvail, ring[vring->num]),
                              vring->align);
    virtio_init_region_cache(vdev, n);
}

/* Called within rcu_read_lock().  */
static void vring_split_desc_read(VirtIODevice *vdev, VRingDesc *desc,
                                  MemoryRegionCache *cache, int i)
{
    address_space_read_cached(cache, i * sizeof(VRingDesc),
                              desc, sizeof(VRingDesc));
    virtio_tswap64s(vdev, &desc->addr);
    virtio_tswap32s(vdev, &desc->len);
    virtio_tswap16s(vdev, &desc->flags);
    virtio_tswap16s(vdev, &desc->next);
}

static void vring_packed_event_read(VirtIODevice *vdev,
                                    MemoryRegionCache *cache,
                                    VRingPackedDescEvent *e)
{
    hwaddr off_off = offsetof(VRingPackedDescEvent, off_wrap);
    hwaddr off_flags = offsetof(VRingPackedDescEvent, flags);

    e->flags = virtio_lduw_phys_cached(vdev, cache, off_flags);
    /* Make sure flags is seen before off_wrap */
    smp_rmb();
    e->off_wrap = virtio_lduw_phys_cached(vdev, cache, off_off);
    virtio_tswap16s(vdev, &e->flags);
}

static void vring_packed_off_wrap_write(VirtIODevice *vdev,
                                        MemoryRegionCache *cache,
                                        uint16_t off_wrap)
{
    hwaddr off = offsetof(VRingPackedDescEvent, off_wrap);

    virtio_stw_phys_cached(vdev, cache, off, off_wrap);
    address_space_cache_invalidate(cache, off, sizeof(off_wrap));
}

static void vring_packed_flags_write(VirtIODevice *vdev,
                                     MemoryRegionCache *cache, uint16_t flags)
{
    hwaddr off = offsetof(VRingPackedDescEvent, flags);

    virtio_stw_phys_cached(vdev, cache, off, flags);
    address_space_cache_invalidate(cache, off, sizeof(flags));
}

/* Called within rcu_read_lock().  */
static VRingMemoryRegionCaches *vring_get_region_caches(struct VirtQueue *vq)
{
    return qatomic_rcu_read(&vq->vring.caches);
}

/* Called within rcu_read_lock().  */
static inline uint16_t vring_avail_flags(VirtQueue *vq)
{
    VRingMemoryRegionCaches *caches = vring_get_region_caches(vq);
    hwaddr pa = offsetof(VRingAvail, flags);

    if (!caches) {
        return 0;
    }

    return virtio_lduw_phys_cached(vq->vdev, &caches->avail, pa);
}

/* Called within rcu_read_lock().  */
static inline uint16_t vring_avail_idx(VirtQueue *vq)
{
    VRingMemoryRegionCaches *caches = vring_get_region_caches(vq);
    hwaddr pa = offsetof(VRingAvail, idx);

    if (!caches) {
        return 0;
    }

    vq->shadow_avail_idx = virtio_lduw_phys_cached(vq->vdev, &caches->avail, pa);
    return vq->shadow_avail_idx;
}

/* Called within rcu_read_lock().  */
static inline uint16_t vring_avail_ring(VirtQueue *vq, int i)
{
    VRingMemoryRegionCaches *caches = vring_get_region_caches(vq);
    hwaddr pa = offsetof(VRingAvail, ring[i]);

    if (!caches) {
        return 0;
    }

    return virtio_lduw_phys_cached(vq->vdev, &caches->avail, pa);
}

/* Called within rcu_read_lock().  */
static inline uint16_t vring_get_used_event(VirtQueue *vq)
{
    return vring_avail_ring(vq, vq->vring.num);
}

/* Called within rcu_read_lock().  */
static inline void vring_used_write(VirtQueue *vq, VRingUsedElem *uelem,
                                    int i)
{
    VRingMemoryRegionCaches *caches = vring_get_region_caches(vq);
    hwaddr pa = offsetof(VRingUsed, ring[i]);

    if (!caches) {
        return;
    }

    virtio_tswap32s(vq->vdev, &uelem->id);
    virtio_tswap32s(vq->vdev, &uelem->len);
    address_space_write_cached(&caches->used, pa, uelem, sizeof(VRingUsedElem));
    address_space_cache_invalidate(&caches->used, pa, sizeof(VRingUsedElem));
}

/* Called within rcu_read_lock(). */
static inline uint16_t vring_used_flags(VirtQueue *vq)
{
    VRingMemoryRegionCaches *caches = vring_get_region_caches(vq);
    hwaddr pa = offsetof(VRingUsed, flags);

    if (!caches) {
        return 0;
    }

    return virtio_lduw_phys_cached(vq->vdev, &caches->used, pa);
}

/* Called within rcu_read_lock().  */
static uint16_t vring_used_idx(VirtQueue *vq)
{
    VRingMemoryRegionCaches *caches = vring_get_region_caches(vq);
    hwaddr pa = offsetof(VRingUsed, idx);

    if (!caches) {
        return 0;
    }

    return virtio_lduw_phys_cached(vq->vdev, &caches->used, pa);
}

/* Called within rcu_read_lock().  */
static inline void vring_used_idx_set(VirtQueue *vq, uint16_t val)
{
    VRingMemoryRegionCaches *caches = vring_get_region_caches(vq);
    hwaddr pa = offsetof(VRingUsed, idx);

    if (caches) {
        virtio_stw_phys_cached(vq->vdev, &caches->used, pa, val);
        address_space_cache_invalidate(&caches->used, pa, sizeof(val));
    }

    vq->used_idx = val;
}

/* Called within rcu_read_lock().  */
static inline void vring_used_flags_set_bit(VirtQueue *vq, int mask)
{
    VRingMemoryRegionCaches *caches = vring_get_region_caches(vq);
    VirtIODevice *vdev = vq->vdev;
    hwaddr pa = offsetof(VRingUsed, flags);
    uint16_t flags;

    if (!caches) {
        return;
    }

    flags = virtio_lduw_phys_cached(vq->vdev, &caches->used, pa);
    virtio_stw_phys_cached(vdev, &caches->used, pa, flags | mask);
    address_space_cache_invalidate(&caches->used, pa, sizeof(flags));
}

/* Called within rcu_read_lock().  */
static inline void vring_used_flags_unset_bit(VirtQueue *vq, int mask)
{
    VRingMemoryRegionCaches *caches = vring_get_region_caches(vq);
    VirtIODevice *vdev = vq->vdev;
    hwaddr pa = offsetof(VRingUsed, flags);
    uint16_t flags;

    if (!caches) {
        return;
    }

    flags = virtio_lduw_phys_cached(vq->vdev, &caches->used, pa);
    virtio_stw_phys_cached(vdev, &caches->used, pa, flags & ~mask);
    address_space_cache_invalidate(&caches->used, pa, sizeof(flags));
}

/* Called within rcu_read_lock().  */
static inline void vring_set_avail_event(VirtQueue *vq, uint16_t val)
{
    VRingMemoryRegionCaches *caches;
    hwaddr pa;
    if (!vq->notification) {
        return;
    }

    caches = vring_get_region_caches(vq);
    if (!caches) {
        return;
    }

    pa = offsetof(VRingUsed, ring[vq->vring.num]);
    virtio_stw_phys_cached(vq->vdev, &caches->used, pa, val);
    address_space_cache_invalidate(&caches->used, pa, sizeof(val));
}

static void virtio_queue_split_set_notification(VirtQueue *vq, int enable)
{
    RCU_READ_LOCK_GUARD();

    if (virtio_vdev_has_feature(vq->vdev, VIRTIO_RING_F_EVENT_IDX)) {
        vring_set_avail_event(vq, vring_avail_idx(vq));
    } else if (enable) {
        vring_used_flags_unset_bit(vq, VRING_USED_F_NO_NOTIFY);
    } else {
        vring_used_flags_set_bit(vq, VRING_USED_F_NO_NOTIFY);
    }
    if (enable) {
        /* Expose avail event/used flags before caller checks the avail idx. */
        smp_mb();
    }
}

static void virtio_queue_packed_set_notification(VirtQueue *vq, int enable)
{
    uint16_t off_wrap;
    VRingPackedDescEvent e;
    VRingMemoryRegionCaches *caches;

    RCU_READ_LOCK_GUARD();
    caches = vring_get_region_caches(vq);
    if (!caches) {
        return;
    }

    vring_packed_event_read(vq->vdev, &caches->used, &e);

    if (!enable) {
        e.flags = VRING_PACKED_EVENT_FLAG_DISABLE;
    } else if (virtio_vdev_has_feature(vq->vdev, VIRTIO_RING_F_EVENT_IDX)) {
        off_wrap = vq->shadow_avail_idx | vq->shadow_avail_wrap_counter << 15;
        vring_packed_off_wrap_write(vq->vdev, &caches->used, off_wrap);
        /* Make sure off_wrap is wrote before flags */
        smp_wmb();
        e.flags = VRING_PACKED_EVENT_FLAG_DESC;
    } else {
        e.flags = VRING_PACKED_EVENT_FLAG_ENABLE;
    }

    vring_packed_flags_write(vq->vdev, &caches->used, e.flags);
    if (enable) {
        /* Expose avail event/used flags before caller checks the avail idx. */
        smp_mb();
    }
}

bool virtio_queue_get_notification(VirtQueue *vq)
{
    return vq->notification;
}

void virtio_queue_set_notification(VirtQueue *vq, int enable)
{
    vq->notification = enable;

    if (!vq->vring.desc) {
        return;
    }

    if (virtio_vdev_has_feature(vq->vdev, VIRTIO_F_RING_PACKED)) {
        virtio_queue_packed_set_notification(vq, enable);
    } else {
        virtio_queue_split_set_notification(vq, enable);
    }
}

int virtio_queue_ready(VirtQueue *vq)
{
    return vq->vring.avail != 0;
}

static void vring_packed_desc_read_flags(VirtIODevice *vdev,
                                         uint16_t *flags,
                                         MemoryRegionCache *cache,
                                         int i)
{
    hwaddr off = i * sizeof(VRingPackedDesc) + offsetof(VRingPackedDesc, flags);

    *flags = virtio_lduw_phys_cached(vdev, cache, off);
}

static void vring_packed_desc_read(VirtIODevice *vdev,
                                   VRingPackedDesc *desc,
                                   MemoryRegionCache *cache,
                                   int i, bool strict_order)
{
    hwaddr off = i * sizeof(VRingPackedDesc);

    vring_packed_desc_read_flags(vdev, &desc->flags, cache, i);

    if (strict_order) {
        /* Make sure flags is read before the rest fields. */
        smp_rmb();
    }

    address_space_read_cached(cache, off + offsetof(VRingPackedDesc, addr),
                              &desc->addr, sizeof(desc->addr));
    address_space_read_cached(cache, off + offsetof(VRingPackedDesc, id),
                              &desc->id, sizeof(desc->id));
    address_space_read_cached(cache, off + offsetof(VRingPackedDesc, len),
                              &desc->len, sizeof(desc->len));
    virtio_tswap64s(vdev, &desc->addr);
    virtio_tswap16s(vdev, &desc->id);
    virtio_tswap32s(vdev, &desc->len);
}

static void vring_packed_desc_write_data(VirtIODevice *vdev,
                                         VRingPackedDesc *desc,
                                         MemoryRegionCache *cache,
                                         int i)
{
    hwaddr off_id = i * sizeof(VRingPackedDesc) +
                    offsetof(VRingPackedDesc, id);
    hwaddr off_len = i * sizeof(VRingPackedDesc) +
                    offsetof(VRingPackedDesc, len);

    virtio_tswap32s(vdev, &desc->len);
    virtio_tswap16s(vdev, &desc->id);
    address_space_write_cached(cache, off_id, &desc->id, sizeof(desc->id));
    address_space_cache_invalidate(cache, off_id, sizeof(desc->id));
    address_space_write_cached(cache, off_len, &desc->len, sizeof(desc->len));
    address_space_cache_invalidate(cache, off_len, sizeof(desc->len));
}

static void vring_packed_desc_write_flags(VirtIODevice *vdev,
                                          VRingPackedDesc *desc,
                                          MemoryRegionCache *cache,
                                          int i)
{
    hwaddr off = i * sizeof(VRingPackedDesc) + offsetof(VRingPackedDesc, flags);

    virtio_stw_phys_cached(vdev, cache, off, desc->flags);
    address_space_cache_invalidate(cache, off, sizeof(desc->flags));
}

static void vring_packed_desc_write(VirtIODevice *vdev,
                                    VRingPackedDesc *desc,
                                    MemoryRegionCache *cache,
                                    int i, bool strict_order)
{
    vring_packed_desc_write_data(vdev, desc, cache, i);
    if (strict_order) {
        /* Make sure data is wrote before flags. */
        smp_wmb();
    }
    vring_packed_desc_write_flags(vdev, desc, cache, i);
}

static inline bool is_desc_avail(uint16_t flags, bool wrap_counter)
{
    bool avail, used;

    avail = !!(flags & (1 << VRING_PACKED_DESC_F_AVAIL));
    used = !!(flags & (1 << VRING_PACKED_DESC_F_USED));
    return (avail != used) && (avail == wrap_counter);
}

/* Fetch avail_idx from VQ memory only when we really need to know if
 * guest has added some buffers.
 * Called within rcu_read_lock().  */
static int virtio_queue_empty_rcu(VirtQueue *vq)
{
    if (virtio_device_disabled(vq->vdev)) {
        return 1;
    }

    if (unlikely(!vq->vring.avail)) {
        return 1;
    }

    if (vq->shadow_avail_idx != vq->last_avail_idx) {
        return 0;
    }

    return vring_avail_idx(vq) == vq->last_avail_idx;
}

static int virtio_queue_split_empty(VirtQueue *vq)
{
    bool empty;

    if (virtio_device_disabled(vq->vdev)) {
        return 1;
    }

    if (unlikely(!vq->vring.avail)) {
        return 1;
    }

    if (vq->shadow_avail_idx != vq->last_avail_idx) {
        return 0;
    }

    RCU_READ_LOCK_GUARD();
    empty = vring_avail_idx(vq) == vq->last_avail_idx;
    return empty;
}

/* Called within rcu_read_lock().  */
static int virtio_queue_packed_empty_rcu(VirtQueue *vq)
{
    struct VRingPackedDesc desc;
    VRingMemoryRegionCaches *cache;

    if (unlikely(!vq->vring.desc)) {
        return 1;
    }

    cache = vring_get_region_caches(vq);
    if (!cache) {
        return 1;
    }

    vring_packed_desc_read_flags(vq->vdev, &desc.flags, &cache->desc,
                                 vq->last_avail_idx);

    return !is_desc_avail(desc.flags, vq->last_avail_wrap_counter);
}

static int virtio_queue_packed_empty(VirtQueue *vq)
{
    RCU_READ_LOCK_GUARD();
    return virtio_queue_packed_empty_rcu(vq);
}

int virtio_queue_empty(VirtQueue *vq)
{
    if (virtio_vdev_has_feature(vq->vdev, VIRTIO_F_RING_PACKED)) {
        return virtio_queue_packed_empty(vq);
    } else {
        return virtio_queue_split_empty(vq);
    }
}

static void virtqueue_unmap_sg(VirtQueue *vq, const VirtQueueElement *elem,
                               unsigned int len)
{
    AddressSpace *dma_as = vq->vdev->dma_as;
    unsigned int offset;
    int i;

    offset = 0;
    for (i = 0; i < elem->in_num; i++) {
        size_t size = MIN(len - offset, elem->in_sg[i].iov_len);

        dma_memory_unmap(dma_as, elem->in_sg[i].iov_base,
                         elem->in_sg[i].iov_len,
                         DMA_DIRECTION_FROM_DEVICE, size);

        offset += size;
    }

    for (i = 0; i < elem->out_num; i++)
        dma_memory_unmap(dma_as, elem->out_sg[i].iov_base,
                         elem->out_sg[i].iov_len,
                         DMA_DIRECTION_TO_DEVICE,
                         elem->out_sg[i].iov_len);
}

/* virtqueue_detach_element:
 * @vq: The #VirtQueue
 * @elem: The #VirtQueueElement
 * @len: number of bytes written
 *
 * Detach the element from the virtqueue.  This function is suitable for device
 * reset or other situations where a #VirtQueueElement is simply freed and will
 * not be pushed or discarded.
 */
void virtqueue_detach_element(VirtQueue *vq, const VirtQueueElement *elem,
                              unsigned int len)
{
    vq->inuse -= elem->ndescs;
    virtqueue_unmap_sg(vq, elem, len);
}

static void virtqueue_split_rewind(VirtQueue *vq, unsigned int num)
{
    vq->last_avail_idx -= num;
}

static void virtqueue_packed_rewind(VirtQueue *vq, unsigned int num)
{
    if (vq->last_avail_idx < num) {
        vq->last_avail_idx = vq->vring.num + vq->last_avail_idx - num;
        vq->last_avail_wrap_counter ^= 1;
    } else {
        vq->last_avail_idx -= num;
    }
}

/* virtqueue_unpop:
 * @vq: The #VirtQueue
 * @elem: The #VirtQueueElement
 * @len: number of bytes written
 *
 * Pretend the most recent element wasn't popped from the virtqueue.  The next
 * call to virtqueue_pop() will refetch the element.
 */
void virtqueue_unpop(VirtQueue *vq, const VirtQueueElement *elem,
                     unsigned int len)
{

    if (virtio_vdev_has_feature(vq->vdev, VIRTIO_F_RING_PACKED)) {
        virtqueue_packed_rewind(vq, 1);
    } else {
        virtqueue_split_rewind(vq, 1);
    }

    virtqueue_detach_element(vq, elem, len);
}

/* virtqueue_rewind:
 * @vq: The #VirtQueue
 * @num: Number of elements to push back
 *
 * Pretend that elements weren't popped from the virtqueue.  The next
 * virtqueue_pop() will refetch the oldest element.
 *
 * Use virtqueue_unpop() instead if you have a VirtQueueElement.
 *
 * Returns: true on success, false if @num is greater than the number of in use
 * elements.
 */
bool virtqueue_rewind(VirtQueue *vq, unsigned int num)
{
    if (num > vq->inuse) {
        return false;
    }

    vq->inuse -= num;
    if (virtio_vdev_has_feature(vq->vdev, VIRTIO_F_RING_PACKED)) {
        virtqueue_packed_rewind(vq, num);
    } else {
        virtqueue_split_rewind(vq, num);
    }
    return true;
}

static void virtqueue_split_fill(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len, unsigned int idx)
{
    VRingUsedElem uelem;

    if (unlikely(!vq->vring.used)) {
        return;
    }

    idx = (idx + vq->used_idx) % vq->vring.num;

    uelem.id = elem->index;
    uelem.len = len;
    vring_used_write(vq, &uelem, idx);
}

static void virtqueue_packed_fill(VirtQueue *vq, const VirtQueueElement *elem,
                                  unsigned int len, unsigned int idx)
{
    vq->used_elems[idx].index = elem->index;
    vq->used_elems[idx].len = len;
    vq->used_elems[idx].ndescs = elem->ndescs;
}

static void virtqueue_packed_fill_desc(VirtQueue *vq,
                                       const VirtQueueElement *elem,
                                       unsigned int idx,
                                       bool strict_order)
{
    uint16_t head;
    VRingMemoryRegionCaches *caches;
    VRingPackedDesc desc = {
        .id = elem->index,
        .len = elem->len,
    };
    bool wrap_counter = vq->used_wrap_counter;

    if (unlikely(!vq->vring.desc)) {
        return;
    }

    head = vq->used_idx + idx;
    if (head >= vq->vring.num) {
        head -= vq->vring.num;
        wrap_counter ^= 1;
    }
    if (wrap_counter) {
        desc.flags |= (1 << VRING_PACKED_DESC_F_AVAIL);
        desc.flags |= (1 << VRING_PACKED_DESC_F_USED);
    } else {
        desc.flags &= ~(1 << VRING_PACKED_DESC_F_AVAIL);
        desc.flags &= ~(1 << VRING_PACKED_DESC_F_USED);
    }

    caches = vring_get_region_caches(vq);
    if (!caches) {
        return;
    }

    vring_packed_desc_write(vq->vdev, &desc, &caches->desc, head, strict_order);
}

/* Called within rcu_read_lock().  */
void virtqueue_fill(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len, unsigned int idx)
{
    trace_virtqueue_fill(vq, elem, len, idx);

    virtqueue_unmap_sg(vq, elem, len);

    if (virtio_device_disabled(vq->vdev)) {
        return;
    }

    if (virtio_vdev_has_feature(vq->vdev, VIRTIO_F_RING_PACKED)) {
        virtqueue_packed_fill(vq, elem, len, idx);
    } else {
        virtqueue_split_fill(vq, elem, len, idx);
    }
}

/* Called within rcu_read_lock().  */
static void virtqueue_split_flush(VirtQueue *vq, unsigned int count)
{
    uint16_t old, new;

    if (unlikely(!vq->vring.used)) {
        return;
    }

    /* Make sure buffer is written before we update index. */
    smp_wmb();
    trace_virtqueue_flush(vq, count);
    old = vq->used_idx;
    new = old + count;
    vring_used_idx_set(vq, new);
    vq->inuse -= count;
    if (unlikely((int16_t)(new - vq->signalled_used) < (uint16_t)(new - old)))
        vq->signalled_used_valid = false;
}

static void virtqueue_packed_flush(VirtQueue *vq, unsigned int count)
{
    unsigned int i, ndescs = 0;

    if (unlikely(!vq->vring.desc)) {
        return;
    }

    for (i = 1; i < count; i++) {
        virtqueue_packed_fill_desc(vq, &vq->used_elems[i], i, false);
        ndescs += vq->used_elems[i].ndescs;
    }
    virtqueue_packed_fill_desc(vq, &vq->used_elems[0], 0, true);
    ndescs += vq->used_elems[0].ndescs;

    vq->inuse -= ndescs;
    vq->used_idx += ndescs;
    if (vq->used_idx >= vq->vring.num) {
        vq->used_idx -= vq->vring.num;
        vq->used_wrap_counter ^= 1;
        vq->signalled_used_valid = false;
    }
}

void virtqueue_flush(VirtQueue *vq, unsigned int count)
{
    if (virtio_device_disabled(vq->vdev)) {
        vq->inuse -= count;
        return;
    }

    if (virtio_vdev_has_feature(vq->vdev, VIRTIO_F_RING_PACKED)) {
        virtqueue_packed_flush(vq, count);
    } else {
        virtqueue_split_flush(vq, count);
    }
}

void virtqueue_push(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len)
{
    RCU_READ_LOCK_GUARD();
    virtqueue_fill(vq, elem, len, 0);
    virtqueue_flush(vq, 1);
}

/* Called within rcu_read_lock().  */
static int virtqueue_num_heads(VirtQueue *vq, unsigned int idx)
{
    uint16_t num_heads = vring_avail_idx(vq) - idx;

    /* Check it isn't doing very strange things with descriptor numbers. */
    if (num_heads > vq->vring.num) {
        virtio_error(vq->vdev, "Guest moved used index from %u to %u",
                     idx, vq->shadow_avail_idx);
        return -EINVAL;
    }
    /* On success, callers read a descriptor at vq->last_avail_idx.
     * Make sure descriptor read does not bypass avail index read. */
    if (num_heads) {
        smp_rmb();
    }

    return num_heads;
}

/* Called within rcu_read_lock().  */
static bool virtqueue_get_head(VirtQueue *vq, unsigned int idx,
                               unsigned int *head)
{
    /* Grab the next descriptor number they're advertising, and increment
     * the index we've seen. */
    *head = vring_avail_ring(vq, idx % vq->vring.num);

    /* If their number is silly, that's a fatal mistake. */
    if (*head >= vq->vring.num) {
        virtio_error(vq->vdev, "Guest says index %u is available", *head);
        return false;
    }

    return true;
}

enum {
    VIRTQUEUE_READ_DESC_ERROR = -1,
    VIRTQUEUE_READ_DESC_DONE = 0,   /* end of chain */
    VIRTQUEUE_READ_DESC_MORE = 1,   /* more buffers in chain */
};

static int virtqueue_split_read_next_desc(VirtIODevice *vdev, VRingDesc *desc,
                                          MemoryRegionCache *desc_cache,
                                          unsigned int max, unsigned int *next)
{
    /* If this descriptor says it doesn't chain, we're done. */
    if (!(desc->flags & VRING_DESC_F_NEXT)) {
        return VIRTQUEUE_READ_DESC_DONE;
    }

    /* Check they're not leading us off end of descriptors. */
    *next = desc->next;
    /* Make sure compiler knows to grab that: we don't want it changing! */
    smp_wmb();

    if (*next >= max) {
        virtio_error(vdev, "Desc next is %u", *next);
        return VIRTQUEUE_READ_DESC_ERROR;
    }

    vring_split_desc_read(vdev, desc, desc_cache, *next);
    return VIRTQUEUE_READ_DESC_MORE;
}

/* Called within rcu_read_lock().  */
static void virtqueue_split_get_avail_bytes(VirtQueue *vq,
                            unsigned int *in_bytes, unsigned int *out_bytes,
                            unsigned max_in_bytes, unsigned max_out_bytes,
                            VRingMemoryRegionCaches *caches)
{
    VirtIODevice *vdev = vq->vdev;
    unsigned int max, idx;
    unsigned int total_bufs, in_total, out_total;
    MemoryRegionCache indirect_desc_cache = MEMORY_REGION_CACHE_INVALID;
    int64_t len = 0;
    int rc;

    idx = vq->last_avail_idx;
    total_bufs = in_total = out_total = 0;

    max = vq->vring.num;

    while ((rc = virtqueue_num_heads(vq, idx)) > 0) {
        MemoryRegionCache *desc_cache = &caches->desc;
        unsigned int num_bufs;
        VRingDesc desc;
        unsigned int i;

        num_bufs = total_bufs;

        if (!virtqueue_get_head(vq, idx++, &i)) {
            goto err;
        }

        vring_split_desc_read(vdev, &desc, desc_cache, i);

        if (desc.flags & VRING_DESC_F_INDIRECT) {
            if (!desc.len || (desc.len % sizeof(VRingDesc))) {
                virtio_error(vdev, "Invalid size for indirect buffer table");
                goto err;
            }

            /* If we've got too many, that implies a descriptor loop. */
            if (num_bufs >= max) {
                virtio_error(vdev, "Looped descriptor");
                goto err;
            }

            /* loop over the indirect descriptor table */
            len = address_space_cache_init(&indirect_desc_cache,
                                           vdev->dma_as,
                                           desc.addr, desc.len, false);
            desc_cache = &indirect_desc_cache;
            if (len < desc.len) {
                virtio_error(vdev, "Cannot map indirect buffer");
                goto err;
            }

            max = desc.len / sizeof(VRingDesc);
            num_bufs = i = 0;
            vring_split_desc_read(vdev, &desc, desc_cache, i);
        }

        do {
            /* If we've got too many, that implies a descriptor loop. */
            if (++num_bufs > max) {
                virtio_error(vdev, "Looped descriptor");
                goto err;
            }

            if (desc.flags & VRING_DESC_F_WRITE) {
                in_total += desc.len;
            } else {
                out_total += desc.len;
            }
            if (in_total >= max_in_bytes && out_total >= max_out_bytes) {
                goto done;
            }

            rc = virtqueue_split_read_next_desc(vdev, &desc, desc_cache, max, &i);
        } while (rc == VIRTQUEUE_READ_DESC_MORE);

        if (rc == VIRTQUEUE_READ_DESC_ERROR) {
            goto err;
        }

        if (desc_cache == &indirect_desc_cache) {
            address_space_cache_destroy(&indirect_desc_cache);
            total_bufs++;
        } else {
            total_bufs = num_bufs;
        }
    }

    if (rc < 0) {
        goto err;
    }

done:
    address_space_cache_destroy(&indirect_desc_cache);
    if (in_bytes) {
        *in_bytes = in_total;
    }
    if (out_bytes) {
        *out_bytes = out_total;
    }
    return;

err:
    in_total = out_total = 0;
    goto done;
}

static int virtqueue_packed_read_next_desc(VirtQueue *vq,
                                           VRingPackedDesc *desc,
                                           MemoryRegionCache
                                           *desc_cache,
                                           unsigned int max,
                                           unsigned int *next,
                                           bool indirect)
{
    /* If this descriptor says it doesn't chain, we're done. */
    if (!indirect && !(desc->flags & VRING_DESC_F_NEXT)) {
        return VIRTQUEUE_READ_DESC_DONE;
    }

    ++*next;
    if (*next == max) {
        if (indirect) {
            return VIRTQUEUE_READ_DESC_DONE;
        } else {
            (*next) -= vq->vring.num;
        }
    }

    vring_packed_desc_read(vq->vdev, desc, desc_cache, *next, false);
    return VIRTQUEUE_READ_DESC_MORE;
}

/* Called within rcu_read_lock().  */
static void virtqueue_packed_get_avail_bytes(VirtQueue *vq,
                                             unsigned int *in_bytes,
                                             unsigned int *out_bytes,
                                             unsigned max_in_bytes,
                                             unsigned max_out_bytes,
                                             VRingMemoryRegionCaches *caches)
{
    VirtIODevice *vdev = vq->vdev;
    unsigned int max, idx;
    unsigned int total_bufs, in_total, out_total;
    MemoryRegionCache *desc_cache;
    MemoryRegionCache indirect_desc_cache = MEMORY_REGION_CACHE_INVALID;
    int64_t len = 0;
    VRingPackedDesc desc;
    bool wrap_counter;

    idx = vq->last_avail_idx;
    wrap_counter = vq->last_avail_wrap_counter;
    total_bufs = in_total = out_total = 0;

    max = vq->vring.num;

    for (;;) {
        unsigned int num_bufs = total_bufs;
        unsigned int i = idx;
        int rc;

        desc_cache = &caches->desc;
        vring_packed_desc_read(vdev, &desc, desc_cache, idx, true);
        if (!is_desc_avail(desc.flags, wrap_counter)) {
            break;
        }

        if (desc.flags & VRING_DESC_F_INDIRECT) {
            if (desc.len % sizeof(VRingPackedDesc)) {
                virtio_error(vdev, "Invalid size for indirect buffer table");
                goto err;
            }

            /* If we've got too many, that implies a descriptor loop. */
            if (num_bufs >= max) {
                virtio_error(vdev, "Looped descriptor");
                goto err;
            }

            /* loop over the indirect descriptor table */
            len = address_space_cache_init(&indirect_desc_cache,
                                           vdev->dma_as,
                                           desc.addr, desc.len, false);
            desc_cache = &indirect_desc_cache;
            if (len < desc.len) {
                virtio_error(vdev, "Cannot map indirect buffer");
                goto err;
            }

            max = desc.len / sizeof(VRingPackedDesc);
            num_bufs = i = 0;
            vring_packed_desc_read(vdev, &desc, desc_cache, i, false);
        }

        do {
            /* If we've got too many, that implies a descriptor loop. */
            if (++num_bufs > max) {
                virtio_error(vdev, "Looped descriptor");
                goto err;
            }

            if (desc.flags & VRING_DESC_F_WRITE) {
                in_total += desc.len;
            } else {
                out_total += desc.len;
            }
            if (in_total >= max_in_bytes && out_total >= max_out_bytes) {
                goto done;
            }

            rc = virtqueue_packed_read_next_desc(vq, &desc, desc_cache, max,
                                                 &i, desc_cache ==
                                                 &indirect_desc_cache);
        } while (rc == VIRTQUEUE_READ_DESC_MORE);

        if (desc_cache == &indirect_desc_cache) {
            address_space_cache_destroy(&indirect_desc_cache);
            total_bufs++;
            idx++;
        } else {
            idx += num_bufs - total_bufs;
            total_bufs = num_bufs;
        }

        if (idx >= vq->vring.num) {
            idx -= vq->vring.num;
            wrap_counter ^= 1;
        }
    }

    /* Record the index and wrap counter for a kick we want */
    vq->shadow_avail_idx = idx;
    vq->shadow_avail_wrap_counter = wrap_counter;
done:
    address_space_cache_destroy(&indirect_desc_cache);
    if (in_bytes) {
        *in_bytes = in_total;
    }
    if (out_bytes) {
        *out_bytes = out_total;
    }
    return;

err:
    in_total = out_total = 0;
    goto done;
}

void virtqueue_get_avail_bytes(VirtQueue *vq, unsigned int *in_bytes,
                               unsigned int *out_bytes,
                               unsigned max_in_bytes, unsigned max_out_bytes)
{
    uint16_t desc_size;
    VRingMemoryRegionCaches *caches;

    RCU_READ_LOCK_GUARD();

    if (unlikely(!vq->vring.desc)) {
        goto err;
    }

    caches = vring_get_region_caches(vq);
    if (!caches) {
        goto err;
    }

    desc_size = virtio_vdev_has_feature(vq->vdev, VIRTIO_F_RING_PACKED) ?
                                sizeof(VRingPackedDesc) : sizeof(VRingDesc);
    if (caches->desc.len < vq->vring.num * desc_size) {
        virtio_error(vq->vdev, "Cannot map descriptor ring");
        goto err;
    }

    if (virtio_vdev_has_feature(vq->vdev, VIRTIO_F_RING_PACKED)) {
        virtqueue_packed_get_avail_bytes(vq, in_bytes, out_bytes,
                                         max_in_bytes, max_out_bytes,
                                         caches);
    } else {
        virtqueue_split_get_avail_bytes(vq, in_bytes, out_bytes,
                                        max_in_bytes, max_out_bytes,
                                        caches);
    }

    return;
err:
    if (in_bytes) {
        *in_bytes = 0;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }
}

int virtqueue_avail_bytes(VirtQueue *vq, unsigned int in_bytes,
                          unsigned int out_bytes)
{
    unsigned int in_total, out_total;

    virtqueue_get_avail_bytes(vq, &in_total, &out_total, in_bytes, out_bytes);
    return in_bytes <= in_total && out_bytes <= out_total;
}

static bool virtqueue_map_desc(VirtIODevice *vdev, unsigned int *p_num_sg,
                               hwaddr *addr, struct iovec *iov,
                               unsigned int max_num_sg, bool is_write,
                               hwaddr pa, size_t sz)
{
    bool ok = false;
    unsigned num_sg = *p_num_sg;
    assert(num_sg <= max_num_sg);

    if (!sz) {
        virtio_error(vdev, "virtio: zero sized buffers are not allowed");
        goto out;
    }

    while (sz) {
        hwaddr len = sz;

        if (num_sg == max_num_sg) {
            virtio_error(vdev, "virtio: too many write descriptors in "
                               "indirect table");
            goto out;
        }

        iov[num_sg].iov_base = dma_memory_map(vdev->dma_as, pa, &len,
                                              is_write ?
                                              DMA_DIRECTION_FROM_DEVICE :
                                              DMA_DIRECTION_TO_DEVICE,
                                              MEMTXATTRS_UNSPECIFIED);
        if (!iov[num_sg].iov_base) {
            virtio_error(vdev, "virtio: bogus descriptor or out of resources");
            goto out;
        }

        iov[num_sg].iov_len = len;
        addr[num_sg] = pa;

        sz -= len;
        pa += len;
        num_sg++;
    }
    ok = true;

out:
    *p_num_sg = num_sg;
    return ok;
}

/* Only used by error code paths before we have a VirtQueueElement (therefore
 * virtqueue_unmap_sg() can't be used).  Assumes buffers weren't written to
 * yet.
 */
static void virtqueue_undo_map_desc(unsigned int out_num, unsigned int in_num,
                                    struct iovec *iov)
{
    unsigned int i;

    for (i = 0; i < out_num + in_num; i++) {
        int is_write = i >= out_num;

        cpu_physical_memory_unmap(iov->iov_base, iov->iov_len, is_write, 0);
        iov++;
    }
}

static void virtqueue_map_iovec(VirtIODevice *vdev, struct iovec *sg,
                                hwaddr *addr, unsigned int num_sg,
                                bool is_write)
{
    unsigned int i;
    hwaddr len;

    for (i = 0; i < num_sg; i++) {
        len = sg[i].iov_len;
        sg[i].iov_base = dma_memory_map(vdev->dma_as,
                                        addr[i], &len, is_write ?
                                        DMA_DIRECTION_FROM_DEVICE :
                                        DMA_DIRECTION_TO_DEVICE,
                                        MEMTXATTRS_UNSPECIFIED);
        if (!sg[i].iov_base) {
            error_report("virtio: error trying to map MMIO memory");
            exit(1);
        }
        if (len != sg[i].iov_len) {
            error_report("virtio: unexpected memory split");
            exit(1);
        }
    }
}

void virtqueue_map(VirtIODevice *vdev, VirtQueueElement *elem)
{
    virtqueue_map_iovec(vdev, elem->in_sg, elem->in_addr, elem->in_num, true);
    virtqueue_map_iovec(vdev, elem->out_sg, elem->out_addr, elem->out_num,
                                                                        false);
}

static void *virtqueue_alloc_element(size_t sz, unsigned out_num, unsigned in_num)
{
    VirtQueueElement *elem;
    size_t in_addr_ofs = QEMU_ALIGN_UP(sz, __alignof__(elem->in_addr[0]));
    size_t out_addr_ofs = in_addr_ofs + in_num * sizeof(elem->in_addr[0]);
    size_t out_addr_end = out_addr_ofs + out_num * sizeof(elem->out_addr[0]);
    size_t in_sg_ofs = QEMU_ALIGN_UP(out_addr_end, __alignof__(elem->in_sg[0]));
    size_t out_sg_ofs = in_sg_ofs + in_num * sizeof(elem->in_sg[0]);
    size_t out_sg_end = out_sg_ofs + out_num * sizeof(elem->out_sg[0]);

    assert(sz >= sizeof(VirtQueueElement));
    elem = g_malloc(out_sg_end);
    trace_virtqueue_alloc_element(elem, sz, in_num, out_num);
    elem->out_num = out_num;
    elem->in_num = in_num;
    elem->in_addr = (void *)elem + in_addr_ofs;
    elem->out_addr = (void *)elem + out_addr_ofs;
    elem->in_sg = (void *)elem + in_sg_ofs;
    elem->out_sg = (void *)elem + out_sg_ofs;
    return elem;
}

static void *virtqueue_split_pop(VirtQueue *vq, size_t sz)
{
    unsigned int i, head, max;
    VRingMemoryRegionCaches *caches;
    MemoryRegionCache indirect_desc_cache = MEMORY_REGION_CACHE_INVALID;
    MemoryRegionCache *desc_cache;
    int64_t len;
    VirtIODevice *vdev = vq->vdev;
    VirtQueueElement *elem = NULL;
    unsigned out_num, in_num, elem_entries;
    hwaddr addr[VIRTQUEUE_MAX_SIZE];
    struct iovec iov[VIRTQUEUE_MAX_SIZE];
    VRingDesc desc;
    int rc;

    RCU_READ_LOCK_GUARD();
    if (virtio_queue_empty_rcu(vq)) {
        goto done;
    }
    /* Needed after virtio_queue_empty(), see comment in
     * virtqueue_num_heads(). */
    smp_rmb();

    /* When we start there are none of either input nor output. */
    out_num = in_num = elem_entries = 0;

    max = vq->vring.num;

    if (vq->inuse >= vq->vring.num) {
        virtio_error(vdev, "Virtqueue size exceeded");
        goto done;
    }

    if (!virtqueue_get_head(vq, vq->last_avail_idx++, &head)) {
        goto done;
    }

    if (virtio_vdev_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX)) {
        vring_set_avail_event(vq, vq->last_avail_idx);
    }

    i = head;

    caches = vring_get_region_caches(vq);
    if (!caches) {
        virtio_error(vdev, "Region caches not initialized");
        goto done;
    }

    if (caches->desc.len < max * sizeof(VRingDesc)) {
        virtio_error(vdev, "Cannot map descriptor ring");
        goto done;
    }

    desc_cache = &caches->desc;
    vring_split_desc_read(vdev, &desc, desc_cache, i);
    if (desc.flags & VRING_DESC_F_INDIRECT) {
        if (!desc.len || (desc.len % sizeof(VRingDesc))) {
            virtio_error(vdev, "Invalid size for indirect buffer table");
            goto done;
        }

        /* loop over the indirect descriptor table */
        len = address_space_cache_init(&indirect_desc_cache, vdev->dma_as,
                                       desc.addr, desc.len, false);
        desc_cache = &indirect_desc_cache;
        if (len < desc.len) {
            virtio_error(vdev, "Cannot map indirect buffer");
            goto done;
        }

        max = desc.len / sizeof(VRingDesc);
        i = 0;
        vring_split_desc_read(vdev, &desc, desc_cache, i);
    }

    /* Collect all the descriptors */
    do {
        bool map_ok;

        if (desc.flags & VRING_DESC_F_WRITE) {
            map_ok = virtqueue_map_desc(vdev, &in_num, addr + out_num,
                                        iov + out_num,
                                        VIRTQUEUE_MAX_SIZE - out_num, true,
                                        desc.addr, desc.len);
        } else {
            if (in_num) {
                virtio_error(vdev, "Incorrect order for descriptors");
                goto err_undo_map;
            }
            map_ok = virtqueue_map_desc(vdev, &out_num, addr, iov,
                                        VIRTQUEUE_MAX_SIZE, false,
                                        desc.addr, desc.len);
        }
        if (!map_ok) {
            goto err_undo_map;
        }

        /* If we've got too many, that implies a descriptor loop. */
        if (++elem_entries > max) {
            virtio_error(vdev, "Looped descriptor");
            goto err_undo_map;
        }

        rc = virtqueue_split_read_next_desc(vdev, &desc, desc_cache, max, &i);
    } while (rc == VIRTQUEUE_READ_DESC_MORE);

    if (rc == VIRTQUEUE_READ_DESC_ERROR) {
        goto err_undo_map;
    }

    /* Now copy what we have collected and mapped */
    elem = virtqueue_alloc_element(sz, out_num, in_num);
    elem->index = head;
    elem->ndescs = 1;
    for (i = 0; i < out_num; i++) {
        elem->out_addr[i] = addr[i];
        elem->out_sg[i] = iov[i];
    }
    for (i = 0; i < in_num; i++) {
        elem->in_addr[i] = addr[out_num + i];
        elem->in_sg[i] = iov[out_num + i];
    }

    vq->inuse++;

    trace_virtqueue_pop(vq, elem, elem->in_num, elem->out_num);
done:
    address_space_cache_destroy(&indirect_desc_cache);

    return elem;

err_undo_map:
    virtqueue_undo_map_desc(out_num, in_num, iov);
    goto done;
}

static void *virtqueue_packed_pop(VirtQueue *vq, size_t sz)
{
    unsigned int i, max;
    VRingMemoryRegionCaches *caches;
    MemoryRegionCache indirect_desc_cache = MEMORY_REGION_CACHE_INVALID;
    MemoryRegionCache *desc_cache;
    int64_t len;
    VirtIODevice *vdev = vq->vdev;
    VirtQueueElement *elem = NULL;
    unsigned out_num, in_num, elem_entries;
    hwaddr addr[VIRTQUEUE_MAX_SIZE];
    struct iovec iov[VIRTQUEUE_MAX_SIZE];
    VRingPackedDesc desc;
    uint16_t id;
    int rc;

    RCU_READ_LOCK_GUARD();
    if (virtio_queue_packed_empty_rcu(vq)) {
        goto done;
    }

    /* When we start there are none of either input nor output. */
    out_num = in_num = elem_entries = 0;

    max = vq->vring.num;

    if (vq->inuse >= vq->vring.num) {
        virtio_error(vdev, "Virtqueue size exceeded");
        goto done;
    }

    i = vq->last_avail_idx;

    caches = vring_get_region_caches(vq);
    if (!caches) {
        virtio_error(vdev, "Region caches not initialized");
        goto done;
    }

    if (caches->desc.len < max * sizeof(VRingDesc)) {
        virtio_error(vdev, "Cannot map descriptor ring");
        goto done;
    }

    desc_cache = &caches->desc;
    vring_packed_desc_read(vdev, &desc, desc_cache, i, true);
    id = desc.id;
    if (desc.flags & VRING_DESC_F_INDIRECT) {
        if (desc.len % sizeof(VRingPackedDesc)) {
            virtio_error(vdev, "Invalid size for indirect buffer table");
            goto done;
        }

        /* loop over the indirect descriptor table */
        len = address_space_cache_init(&indirect_desc_cache, vdev->dma_as,
                                       desc.addr, desc.len, false);
        desc_cache = &indirect_desc_cache;
        if (len < desc.len) {
            virtio_error(vdev, "Cannot map indirect buffer");
            goto done;
        }

        max = desc.len / sizeof(VRingPackedDesc);
        i = 0;
        vring_packed_desc_read(vdev, &desc, desc_cache, i, false);
    }

    /* Collect all the descriptors */
    do {
        bool map_ok;

        if (desc.flags & VRING_DESC_F_WRITE) {
            map_ok = virtqueue_map_desc(vdev, &in_num, addr + out_num,
                                        iov + out_num,
                                        VIRTQUEUE_MAX_SIZE - out_num, true,
                                        desc.addr, desc.len);
        } else {
            if (in_num) {
                virtio_error(vdev, "Incorrect order for descriptors");
                goto err_undo_map;
            }
            map_ok = virtqueue_map_desc(vdev, &out_num, addr, iov,
                                        VIRTQUEUE_MAX_SIZE, false,
                                        desc.addr, desc.len);
        }
        if (!map_ok) {
            goto err_undo_map;
        }

        /* If we've got too many, that implies a descriptor loop. */
        if (++elem_entries > max) {
            virtio_error(vdev, "Looped descriptor");
            goto err_undo_map;
        }

        rc = virtqueue_packed_read_next_desc(vq, &desc, desc_cache, max, &i,
                                             desc_cache ==
                                             &indirect_desc_cache);
    } while (rc == VIRTQUEUE_READ_DESC_MORE);

    /* Now copy what we have collected and mapped */
    elem = virtqueue_alloc_element(sz, out_num, in_num);
    for (i = 0; i < out_num; i++) {
        elem->out_addr[i] = addr[i];
        elem->out_sg[i] = iov[i];
    }
    for (i = 0; i < in_num; i++) {
        elem->in_addr[i] = addr[out_num + i];
        elem->in_sg[i] = iov[out_num + i];
    }

    elem->index = id;
    elem->ndescs = (desc_cache == &indirect_desc_cache) ? 1 : elem_entries;
    vq->last_avail_idx += elem->ndescs;
    vq->inuse += elem->ndescs;

    if (vq->last_avail_idx >= vq->vring.num) {
        vq->last_avail_idx -= vq->vring.num;
        vq->last_avail_wrap_counter ^= 1;
    }

    vq->shadow_avail_idx = vq->last_avail_idx;
    vq->shadow_avail_wrap_counter = vq->last_avail_wrap_counter;

    trace_virtqueue_pop(vq, elem, elem->in_num, elem->out_num);
done:
    address_space_cache_destroy(&indirect_desc_cache);

    return elem;

err_undo_map:
    virtqueue_undo_map_desc(out_num, in_num, iov);
    goto done;
}

void *virtqueue_pop(VirtQueue *vq, size_t sz)
{
    if (virtio_device_disabled(vq->vdev)) {
        return NULL;
    }

    if (virtio_vdev_has_feature(vq->vdev, VIRTIO_F_RING_PACKED)) {
        return virtqueue_packed_pop(vq, sz);
    } else {
        return virtqueue_split_pop(vq, sz);
    }
}

static unsigned int virtqueue_packed_drop_all(VirtQueue *vq)
{
    VRingMemoryRegionCaches *caches;
    MemoryRegionCache *desc_cache;
    unsigned int dropped = 0;
    VirtQueueElement elem = {};
    VirtIODevice *vdev = vq->vdev;
    VRingPackedDesc desc;

    RCU_READ_LOCK_GUARD();

    caches = vring_get_region_caches(vq);
    if (!caches) {
        return 0;
    }

    desc_cache = &caches->desc;

    virtio_queue_set_notification(vq, 0);

    while (vq->inuse < vq->vring.num) {
        unsigned int idx = vq->last_avail_idx;
        /*
         * works similar to virtqueue_pop but does not map buffers
         * and does not allocate any memory.
         */
        vring_packed_desc_read(vdev, &desc, desc_cache,
                               vq->last_avail_idx , true);
        if (!is_desc_avail(desc.flags, vq->last_avail_wrap_counter)) {
            break;
        }
        elem.index = desc.id;
        elem.ndescs = 1;
        while (virtqueue_packed_read_next_desc(vq, &desc, desc_cache,
                                               vq->vring.num, &idx, false)) {
            ++elem.ndescs;
        }
        /*
         * immediately push the element, nothing to unmap
         * as both in_num and out_num are set to 0.
         */
        virtqueue_push(vq, &elem, 0);
        dropped++;
        vq->last_avail_idx += elem.ndescs;
        if (vq->last_avail_idx >= vq->vring.num) {
            vq->last_avail_idx -= vq->vring.num;
            vq->last_avail_wrap_counter ^= 1;
        }
    }

    return dropped;
}

static unsigned int virtqueue_split_drop_all(VirtQueue *vq)
{
    unsigned int dropped = 0;
    VirtQueueElement elem = {};
    VirtIODevice *vdev = vq->vdev;
    bool fEventIdx = virtio_vdev_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX);

    while (!virtio_queue_empty(vq) && vq->inuse < vq->vring.num) {
        /* works similar to virtqueue_pop but does not map buffers
        * and does not allocate any memory */
        smp_rmb();
        if (!virtqueue_get_head(vq, vq->last_avail_idx, &elem.index)) {
            break;
        }
        vq->inuse++;
        vq->last_avail_idx++;
        if (fEventIdx) {
            vring_set_avail_event(vq, vq->last_avail_idx);
        }
        /* immediately push the element, nothing to unmap
         * as both in_num and out_num are set to 0 */
        virtqueue_push(vq, &elem, 0);
        dropped++;
    }

    return dropped;
}

/* virtqueue_drop_all:
 * @vq: The #VirtQueue
 * Drops all queued buffers and indicates them to the guest
 * as if they are done. Useful when buffers can not be
 * processed but must be returned to the guest.
 */
unsigned int virtqueue_drop_all(VirtQueue *vq)
{
    struct VirtIODevice *vdev = vq->vdev;

    if (virtio_device_disabled(vq->vdev)) {
        return 0;
    }

    if (virtio_vdev_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        return virtqueue_packed_drop_all(vq);
    } else {
        return virtqueue_split_drop_all(vq);
    }
}

/* Reading and writing a structure directly to QEMUFile is *awful*, but
 * it is what QEMU has always done by mistake.  We can change it sooner
 * or later by bumping the version number of the affected vm states.
 * In the meanwhile, since the in-memory layout of VirtQueueElement
 * has changed, we need to marshal to and from the layout that was
 * used before the change.
 */
typedef struct VirtQueueElementOld {
    unsigned int index;
    unsigned int out_num;
    unsigned int in_num;
    hwaddr in_addr[VIRTQUEUE_MAX_SIZE];
    hwaddr out_addr[VIRTQUEUE_MAX_SIZE];
    struct iovec in_sg[VIRTQUEUE_MAX_SIZE];
    struct iovec out_sg[VIRTQUEUE_MAX_SIZE];
} VirtQueueElementOld;

void *qemu_get_virtqueue_element(VirtIODevice *vdev, QEMUFile *f, size_t sz)
{
    VirtQueueElement *elem;
    VirtQueueElementOld data;
    int i;

    qemu_get_buffer(f, (uint8_t *)&data, sizeof(VirtQueueElementOld));

    /* TODO: teach all callers that this can fail, and return failure instead
     * of asserting here.
     * This is just one thing (there are probably more) that must be
     * fixed before we can allow NDEBUG compilation.
     */
    assert(ARRAY_SIZE(data.in_addr) >= data.in_num);
    assert(ARRAY_SIZE(data.out_addr) >= data.out_num);

    elem = virtqueue_alloc_element(sz, data.out_num, data.in_num);
    elem->index = data.index;

    for (i = 0; i < elem->in_num; i++) {
        elem->in_addr[i] = data.in_addr[i];
    }

    for (i = 0; i < elem->out_num; i++) {
        elem->out_addr[i] = data.out_addr[i];
    }

    for (i = 0; i < elem->in_num; i++) {
        /* Base is overwritten by virtqueue_map.  */
        elem->in_sg[i].iov_base = 0;
        elem->in_sg[i].iov_len = data.in_sg[i].iov_len;
    }

    for (i = 0; i < elem->out_num; i++) {
        /* Base is overwritten by virtqueue_map.  */
        elem->out_sg[i].iov_base = 0;
        elem->out_sg[i].iov_len = data.out_sg[i].iov_len;
    }

    if (virtio_host_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        qemu_get_be32s(f, &elem->ndescs);
    }

    virtqueue_map(vdev, elem);
    return elem;
}

void qemu_put_virtqueue_element(VirtIODevice *vdev, QEMUFile *f,
                                VirtQueueElement *elem)
{
    VirtQueueElementOld data;
    int i;

    memset(&data, 0, sizeof(data));
    data.index = elem->index;
    data.in_num = elem->in_num;
    data.out_num = elem->out_num;

    for (i = 0; i < elem->in_num; i++) {
        data.in_addr[i] = elem->in_addr[i];
    }

    for (i = 0; i < elem->out_num; i++) {
        data.out_addr[i] = elem->out_addr[i];
    }

    for (i = 0; i < elem->in_num; i++) {
        /* Base is overwritten by virtqueue_map when loading.  Do not
         * save it, as it would leak the QEMU address space layout.  */
        data.in_sg[i].iov_len = elem->in_sg[i].iov_len;
    }

    for (i = 0; i < elem->out_num; i++) {
        /* Do not save iov_base as above.  */
        data.out_sg[i].iov_len = elem->out_sg[i].iov_len;
    }

    if (virtio_host_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        qemu_put_be32s(f, &elem->ndescs);
    }

    qemu_put_buffer(f, (uint8_t *)&data, sizeof(VirtQueueElementOld));
}

/* virtio device */
static void virtio_notify_vector(VirtIODevice *vdev, uint16_t vector)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (virtio_device_disabled(vdev)) {
        return;
    }

    if (k->notify) {
        k->notify(qbus->parent, vector);
    }
}

void virtio_update_irq(VirtIODevice *vdev)
{
    virtio_notify_vector(vdev, VIRTIO_NO_VECTOR);
}

static int virtio_validate_features(VirtIODevice *vdev)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);

    if (virtio_host_has_feature(vdev, VIRTIO_F_IOMMU_PLATFORM) &&
        !virtio_vdev_has_feature(vdev, VIRTIO_F_IOMMU_PLATFORM)) {
        return -EFAULT;
    }

    if (k->validate_features) {
        return k->validate_features(vdev);
    } else {
        return 0;
    }
}

int virtio_set_status(VirtIODevice *vdev, uint8_t val)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    trace_virtio_set_status(vdev, val);

    if (virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        if (!(vdev->status & VIRTIO_CONFIG_S_FEATURES_OK) &&
            val & VIRTIO_CONFIG_S_FEATURES_OK) {
            int ret = virtio_validate_features(vdev);

            if (ret) {
                return ret;
            }
        }
    }

    if ((vdev->status & VIRTIO_CONFIG_S_DRIVER_OK) !=
        (val & VIRTIO_CONFIG_S_DRIVER_OK)) {
        virtio_set_started(vdev, val & VIRTIO_CONFIG_S_DRIVER_OK);
    }

    if (k->set_status) {
        k->set_status(vdev, val);
    }
    vdev->status = val;

    return 0;
}

static enum virtio_device_endian virtio_default_endian(void)
{
    if (target_words_bigendian()) {
        return VIRTIO_DEVICE_ENDIAN_BIG;
    } else {
        return VIRTIO_DEVICE_ENDIAN_LITTLE;
    }
}

static enum virtio_device_endian virtio_current_cpu_endian(void)
{
    if (cpu_virtio_is_big_endian(current_cpu)) {
        return VIRTIO_DEVICE_ENDIAN_BIG;
    } else {
        return VIRTIO_DEVICE_ENDIAN_LITTLE;
    }
}

static void __virtio_queue_reset(VirtIODevice *vdev, uint32_t i)
{
    vdev->vq[i].vring.desc = 0;
    vdev->vq[i].vring.avail = 0;
    vdev->vq[i].vring.used = 0;
    vdev->vq[i].last_avail_idx = 0;
    vdev->vq[i].shadow_avail_idx = 0;
    vdev->vq[i].used_idx = 0;
    vdev->vq[i].last_avail_wrap_counter = true;
    vdev->vq[i].shadow_avail_wrap_counter = true;
    vdev->vq[i].used_wrap_counter = true;
    virtio_queue_set_vector(vdev, i, VIRTIO_NO_VECTOR);
    vdev->vq[i].signalled_used = 0;
    vdev->vq[i].signalled_used_valid = false;
    vdev->vq[i].notification = true;
    vdev->vq[i].vring.num = vdev->vq[i].vring.num_default;
    vdev->vq[i].inuse = 0;
    virtio_virtqueue_reset_region_cache(&vdev->vq[i]);
}

void virtio_queue_reset(VirtIODevice *vdev, uint32_t queue_index)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);

    if (k->queue_reset) {
        k->queue_reset(vdev, queue_index);
    }

    __virtio_queue_reset(vdev, queue_index);
}

void virtio_reset(void *opaque)
{
    VirtIODevice *vdev = opaque;
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    int i;

    virtio_set_status(vdev, 0);
    if (current_cpu) {
        /* Guest initiated reset */
        vdev->device_endian = virtio_current_cpu_endian();
    } else {
        /* System reset */
        vdev->device_endian = virtio_default_endian();
    }

    if (k->reset) {
        k->reset(vdev);
    }

    vdev->start_on_kick = false;
    vdev->started = false;
    vdev->broken = false;
    vdev->guest_features = 0;
    vdev->queue_sel = 0;
    vdev->status = 0;
    vdev->disabled = false;
    qatomic_set(&vdev->isr, 0);
    vdev->config_vector = VIRTIO_NO_VECTOR;
    virtio_notify_vector(vdev, vdev->config_vector);

    for(i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        __virtio_queue_reset(vdev, i);
    }
}

uint32_t virtio_config_readb(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint8_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = ldub_p(vdev->config + addr);
    return val;
}

uint32_t virtio_config_readw(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint16_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = lduw_p(vdev->config + addr);
    return val;
}

uint32_t virtio_config_readl(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = ldl_p(vdev->config + addr);
    return val;
}

void virtio_config_writeb(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint8_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stb_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_config_writew(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint16_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stw_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_config_writel(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stl_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

uint32_t virtio_config_modern_readb(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint8_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = ldub_p(vdev->config + addr);
    return val;
}

uint32_t virtio_config_modern_readw(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint16_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = lduw_le_p(vdev->config + addr);
    return val;
}

uint32_t virtio_config_modern_readl(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = ldl_le_p(vdev->config + addr);
    return val;
}

void virtio_config_modern_writeb(VirtIODevice *vdev,
                                 uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint8_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stb_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_config_modern_writew(VirtIODevice *vdev,
                                 uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint16_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stw_le_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_config_modern_writel(VirtIODevice *vdev,
                                 uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stl_le_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_queue_set_addr(VirtIODevice *vdev, int n, hwaddr addr)
{
    if (!vdev->vq[n].vring.num) {
        return;
    }
    vdev->vq[n].vring.desc = addr;
    virtio_queue_update_rings(vdev, n);
}

hwaddr virtio_queue_get_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.desc;
}

void virtio_queue_set_rings(VirtIODevice *vdev, int n, hwaddr desc,
                            hwaddr avail, hwaddr used)
{
    if (!vdev->vq[n].vring.num) {
        return;
    }
    vdev->vq[n].vring.desc = desc;
    vdev->vq[n].vring.avail = avail;
    vdev->vq[n].vring.used = used;
    virtio_init_region_cache(vdev, n);
}

void virtio_queue_set_num(VirtIODevice *vdev, int n, int num)
{
    /* Don't allow guest to flip queue between existent and
     * nonexistent states, or to set it to an invalid size.
     */
    if (!!num != !!vdev->vq[n].vring.num ||
        num > VIRTQUEUE_MAX_SIZE ||
        num < 0) {
        return;
    }
    vdev->vq[n].vring.num = num;
}

VirtQueue *virtio_vector_first_queue(VirtIODevice *vdev, uint16_t vector)
{
    return QLIST_FIRST(&vdev->vector_queues[vector]);
}

VirtQueue *virtio_vector_next_queue(VirtQueue *vq)
{
    return QLIST_NEXT(vq, node);
}

int virtio_queue_get_num(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.num;
}

int virtio_queue_get_max_num(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.num_default;
}

int virtio_get_num_queues(VirtIODevice *vdev)
{
    int i;

    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        if (!virtio_queue_get_num(vdev, i)) {
            break;
        }
    }

    return i;
}

void virtio_queue_set_align(VirtIODevice *vdev, int n, int align)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    /* virtio-1 compliant devices cannot change the alignment */
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        error_report("tried to modify queue alignment for virtio-1 device");
        return;
    }
    /* Check that the transport told us it was going to do this
     * (so a buggy transport will immediately assert rather than
     * silently failing to migrate this state)
     */
    assert(k->has_variable_vring_alignment);

    if (align) {
        vdev->vq[n].vring.align = align;
        virtio_queue_update_rings(vdev, n);
    }
}

static void virtio_queue_notify_vq(VirtQueue *vq)
{
    if (vq->vring.desc && vq->handle_output) {
        VirtIODevice *vdev = vq->vdev;

        if (unlikely(vdev->broken)) {
            return;
        }

        trace_virtio_queue_notify(vdev, vq - vdev->vq, vq);
        vq->handle_output(vdev, vq);

        if (unlikely(vdev->start_on_kick)) {
            virtio_set_started(vdev, true);
        }
    }
}

void virtio_queue_notify(VirtIODevice *vdev, int n)
{
    VirtQueue *vq = &vdev->vq[n];

    if (unlikely(!vq->vring.desc || vdev->broken)) {
        return;
    }

    trace_virtio_queue_notify(vdev, vq - vdev->vq, vq);
    if (vq->host_notifier_enabled) {
        event_notifier_set(&vq->host_notifier);
    } else if (vq->handle_output) {
        vq->handle_output(vdev, vq);

        if (unlikely(vdev->start_on_kick)) {
            virtio_set_started(vdev, true);
        }
    }
}

uint16_t virtio_queue_vector(VirtIODevice *vdev, int n)
{
    return n < VIRTIO_QUEUE_MAX ? vdev->vq[n].vector :
        VIRTIO_NO_VECTOR;
}

void virtio_queue_set_vector(VirtIODevice *vdev, int n, uint16_t vector)
{
    VirtQueue *vq = &vdev->vq[n];

    if (n < VIRTIO_QUEUE_MAX) {
        if (vdev->vector_queues &&
            vdev->vq[n].vector != VIRTIO_NO_VECTOR) {
            QLIST_REMOVE(vq, node);
        }
        vdev->vq[n].vector = vector;
        if (vdev->vector_queues &&
            vector != VIRTIO_NO_VECTOR) {
            QLIST_INSERT_HEAD(&vdev->vector_queues[vector], vq, node);
        }
    }
}

VirtQueue *virtio_add_queue(VirtIODevice *vdev, int queue_size,
                            VirtIOHandleOutput handle_output)
{
    int i;

    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num == 0)
            break;
    }

    if (i == VIRTIO_QUEUE_MAX || queue_size > VIRTQUEUE_MAX_SIZE)
        abort();

    vdev->vq[i].vring.num = queue_size;
    vdev->vq[i].vring.num_default = queue_size;
    vdev->vq[i].vring.align = VIRTIO_PCI_VRING_ALIGN;
    vdev->vq[i].handle_output = handle_output;
    vdev->vq[i].used_elems = g_new0(VirtQueueElement, queue_size);

    return &vdev->vq[i];
}

void virtio_delete_queue(VirtQueue *vq)
{
    vq->vring.num = 0;
    vq->vring.num_default = 0;
    vq->handle_output = NULL;
    g_free(vq->used_elems);
    vq->used_elems = NULL;
    virtio_virtqueue_reset_region_cache(vq);
}

void virtio_del_queue(VirtIODevice *vdev, int n)
{
    if (n < 0 || n >= VIRTIO_QUEUE_MAX) {
        abort();
    }

    virtio_delete_queue(&vdev->vq[n]);
}

static void virtio_set_isr(VirtIODevice *vdev, int value)
{
    uint8_t old = qatomic_read(&vdev->isr);

    /* Do not write ISR if it does not change, so that its cacheline remains
     * shared in the common case where the guest does not read it.
     */
    if ((old & value) != value) {
        qatomic_or(&vdev->isr, value);
    }
}

/* Called within rcu_read_lock(). */
static bool virtio_split_should_notify(VirtIODevice *vdev, VirtQueue *vq)
{
    uint16_t old, new;
    bool v;
    /* We need to expose used array entries before checking used event. */
    smp_mb();
    /* Always notify when queue is empty (when feature acknowledge) */
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_NOTIFY_ON_EMPTY) &&
        !vq->inuse && virtio_queue_empty(vq)) {
        return true;
    }

    if (!virtio_vdev_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX)) {
        return !(vring_avail_flags(vq) & VRING_AVAIL_F_NO_INTERRUPT);
    }

    v = vq->signalled_used_valid;
    vq->signalled_used_valid = true;
    old = vq->signalled_used;
    new = vq->signalled_used = vq->used_idx;
    return !v || vring_need_event(vring_get_used_event(vq), new, old);
}

static bool vring_packed_need_event(VirtQueue *vq, bool wrap,
                                    uint16_t off_wrap, uint16_t new,
                                    uint16_t old)
{
    int off = off_wrap & ~(1 << 15);

    if (wrap != off_wrap >> 15) {
        off -= vq->vring.num;
    }

    return vring_need_event(off, new, old);
}

/* Called within rcu_read_lock(). */
static bool virtio_packed_should_notify(VirtIODevice *vdev, VirtQueue *vq)
{
    VRingPackedDescEvent e;
    uint16_t old, new;
    bool v;
    VRingMemoryRegionCaches *caches;

    caches = vring_get_region_caches(vq);
    if (!caches) {
        return false;
    }

    vring_packed_event_read(vdev, &caches->avail, &e);

    old = vq->signalled_used;
    new = vq->signalled_used = vq->used_idx;
    v = vq->signalled_used_valid;
    vq->signalled_used_valid = true;

    if (e.flags == VRING_PACKED_EVENT_FLAG_DISABLE) {
        return false;
    } else if (e.flags == VRING_PACKED_EVENT_FLAG_ENABLE) {
        return true;
    }

    return !v || vring_packed_need_event(vq, vq->used_wrap_counter,
                                         e.off_wrap, new, old);
}

/* Called within rcu_read_lock().  */
static bool virtio_should_notify(VirtIODevice *vdev, VirtQueue *vq)
{
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        return virtio_packed_should_notify(vdev, vq);
    } else {
        return virtio_split_should_notify(vdev, vq);
    }
}

void virtio_notify_irqfd(VirtIODevice *vdev, VirtQueue *vq)
{
    WITH_RCU_READ_LOCK_GUARD() {
        if (!virtio_should_notify(vdev, vq)) {
            return;
        }
    }

    trace_virtio_notify_irqfd(vdev, vq);

    /*
     * virtio spec 1.0 says ISR bit 0 should be ignored with MSI, but
     * windows drivers included in virtio-win 1.8.0 (circa 2015) are
     * incorrectly polling this bit during crashdump and hibernation
     * in MSI mode, causing a hang if this bit is never updated.
     * Recent releases of Windows do not really shut down, but rather
     * log out and hibernate to make the next startup faster.  Hence,
     * this manifested as a more serious hang during shutdown with
     *
     * Next driver release from 2016 fixed this problem, so working around it
     * is not a must, but it's easy to do so let's do it here.
     *
     * Note: it's safe to update ISR from any thread as it was switched
     * to an atomic operation.
     */
    virtio_set_isr(vq->vdev, 0x1);
    event_notifier_set(&vq->guest_notifier);
}

static void virtio_irq(VirtQueue *vq)
{
    virtio_set_isr(vq->vdev, 0x1);
    virtio_notify_vector(vq->vdev, vq->vector);
}

void virtio_notify(VirtIODevice *vdev, VirtQueue *vq)
{
    WITH_RCU_READ_LOCK_GUARD() {
        if (!virtio_should_notify(vdev, vq)) {
            return;
        }
    }

    trace_virtio_notify(vdev, vq);
    virtio_irq(vq);
}

void virtio_notify_config(VirtIODevice *vdev)
{
    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK))
        return;

    virtio_set_isr(vdev, 0x3);
    vdev->generation++;
    virtio_notify_vector(vdev, vdev->config_vector);
}

static bool virtio_device_endian_needed(void *opaque)
{
    VirtIODevice *vdev = opaque;

    assert(vdev->device_endian != VIRTIO_DEVICE_ENDIAN_UNKNOWN);
    if (!virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        return vdev->device_endian != virtio_default_endian();
    }
    /* Devices conforming to VIRTIO 1.0 or later are always LE. */
    return vdev->device_endian != VIRTIO_DEVICE_ENDIAN_LITTLE;
}

static bool virtio_64bit_features_needed(void *opaque)
{
    VirtIODevice *vdev = opaque;

    return (vdev->host_features >> 32) != 0;
}

static bool virtio_virtqueue_needed(void *opaque)
{
    VirtIODevice *vdev = opaque;

    return virtio_host_has_feature(vdev, VIRTIO_F_VERSION_1);
}

static bool virtio_packed_virtqueue_needed(void *opaque)
{
    VirtIODevice *vdev = opaque;

    return virtio_host_has_feature(vdev, VIRTIO_F_RING_PACKED);
}

static bool virtio_ringsize_needed(void *opaque)
{
    VirtIODevice *vdev = opaque;
    int i;

    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num != vdev->vq[i].vring.num_default) {
            return true;
        }
    }
    return false;
}

static bool virtio_extra_state_needed(void *opaque)
{
    VirtIODevice *vdev = opaque;
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    return k->has_extra_state &&
        k->has_extra_state(qbus->parent);
}

static bool virtio_broken_needed(void *opaque)
{
    VirtIODevice *vdev = opaque;

    return vdev->broken;
}

static bool virtio_started_needed(void *opaque)
{
    VirtIODevice *vdev = opaque;

    return vdev->started;
}

static bool virtio_disabled_needed(void *opaque)
{
    VirtIODevice *vdev = opaque;

    return vdev->disabled;
}

static const VMStateDescription vmstate_virtqueue = {
    .name = "virtqueue_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(vring.avail, struct VirtQueue),
        VMSTATE_UINT64(vring.used, struct VirtQueue),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_packed_virtqueue = {
    .name = "packed_virtqueue_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(last_avail_idx, struct VirtQueue),
        VMSTATE_BOOL(last_avail_wrap_counter, struct VirtQueue),
        VMSTATE_UINT16(used_idx, struct VirtQueue),
        VMSTATE_BOOL(used_wrap_counter, struct VirtQueue),
        VMSTATE_UINT32(inuse, struct VirtQueue),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_virtqueues = {
    .name = "virtio/virtqueues",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_virtqueue_needed,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_POINTER_KNOWN(vq, struct VirtIODevice,
                      VIRTIO_QUEUE_MAX, 0, vmstate_virtqueue, VirtQueue),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_packed_virtqueues = {
    .name = "virtio/packed_virtqueues",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_packed_virtqueue_needed,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_POINTER_KNOWN(vq, struct VirtIODevice,
                      VIRTIO_QUEUE_MAX, 0, vmstate_packed_virtqueue, VirtQueue),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ringsize = {
    .name = "ringsize_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(vring.num_default, struct VirtQueue),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_ringsize = {
    .name = "virtio/ringsize",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_ringsize_needed,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_POINTER_KNOWN(vq, struct VirtIODevice,
                      VIRTIO_QUEUE_MAX, 0, vmstate_ringsize, VirtQueue),
        VMSTATE_END_OF_LIST()
    }
};

static int get_extra_state(QEMUFile *f, void *pv, size_t size,
                           const VMStateField *field)
{
    VirtIODevice *vdev = pv;
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (!k->load_extra_state) {
        return -1;
    } else {
        return k->load_extra_state(qbus->parent, f);
    }
}

static int put_extra_state(QEMUFile *f, void *pv, size_t size,
                           const VMStateField *field, JSONWriter *vmdesc)
{
    VirtIODevice *vdev = pv;
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    k->save_extra_state(qbus->parent, f);
    return 0;
}

static const VMStateInfo vmstate_info_extra_state = {
    .name = "virtqueue_extra_state",
    .get = get_extra_state,
    .put = put_extra_state,
};

static const VMStateDescription vmstate_virtio_extra_state = {
    .name = "virtio/extra_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_extra_state_needed,
    .fields = (VMStateField[]) {
        {
            .name         = "extra_state",
            .version_id   = 0,
            .field_exists = NULL,
            .size         = 0,
            .info         = &vmstate_info_extra_state,
            .flags        = VMS_SINGLE,
            .offset       = 0,
        },
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_device_endian = {
    .name = "virtio/device_endian",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_device_endian_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(device_endian, VirtIODevice),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_64bit_features = {
    .name = "virtio/64bit_features",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_64bit_features_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(guest_features, VirtIODevice),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_broken = {
    .name = "virtio/broken",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_broken_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(broken, VirtIODevice),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_started = {
    .name = "virtio/started",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_started_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(started, VirtIODevice),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_disabled = {
    .name = "virtio/disabled",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_disabled_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(disabled, VirtIODevice),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio = {
    .name = "virtio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_virtio_device_endian,
        &vmstate_virtio_64bit_features,
        &vmstate_virtio_virtqueues,
        &vmstate_virtio_ringsize,
        &vmstate_virtio_broken,
        &vmstate_virtio_extra_state,
        &vmstate_virtio_started,
        &vmstate_virtio_packed_virtqueues,
        &vmstate_virtio_disabled,
        NULL
    }
};

int virtio_save(VirtIODevice *vdev, QEMUFile *f)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t guest_features_lo = (vdev->guest_features & 0xffffffff);
    int i;

    if (k->save_config) {
        k->save_config(qbus->parent, f);
    }

    qemu_put_8s(f, &vdev->status);
    qemu_put_8s(f, &vdev->isr);
    qemu_put_be16s(f, &vdev->queue_sel);
    qemu_put_be32s(f, &guest_features_lo);
    qemu_put_be32(f, vdev->config_len);
    qemu_put_buffer(f, vdev->config, vdev->config_len);

    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num == 0)
            break;
    }

    qemu_put_be32(f, i);

    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num == 0)
            break;

        qemu_put_be32(f, vdev->vq[i].vring.num);
        if (k->has_variable_vring_alignment) {
            qemu_put_be32(f, vdev->vq[i].vring.align);
        }
        /*
         * Save desc now, the rest of the ring addresses are saved in
         * subsections for VIRTIO-1 devices.
         */
        qemu_put_be64(f, vdev->vq[i].vring.desc);
        qemu_put_be16s(f, &vdev->vq[i].last_avail_idx);
        if (k->save_queue) {
            k->save_queue(qbus->parent, i, f);
        }
    }

    if (vdc->save != NULL) {
        vdc->save(vdev, f);
    }

    if (vdc->vmsd) {
        int ret = vmstate_save_state(f, vdc->vmsd, vdev, NULL);
        if (ret) {
            return ret;
        }
    }

    /* Subsections */
    return vmstate_save_state(f, &vmstate_virtio, vdev, NULL);
}

/* A wrapper for use as a VMState .put function */
static int virtio_device_put(QEMUFile *f, void *opaque, size_t size,
                              const VMStateField *field, JSONWriter *vmdesc)
{
    return virtio_save(VIRTIO_DEVICE(opaque), f);
}

/* A wrapper for use as a VMState .get function */
static int virtio_device_get(QEMUFile *f, void *opaque, size_t size,
                             const VMStateField *field)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(opaque);
    DeviceClass *dc = DEVICE_CLASS(VIRTIO_DEVICE_GET_CLASS(vdev));

    return virtio_load(vdev, f, dc->vmsd->version_id);
}

const VMStateInfo  virtio_vmstate_info = {
    .name = "virtio",
    .get = virtio_device_get,
    .put = virtio_device_put,
};

static int virtio_set_features_nocheck(VirtIODevice *vdev, uint64_t val)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    bool bad = (val & ~(vdev->host_features)) != 0;

    val &= vdev->host_features;
    if (k->set_features) {
        k->set_features(vdev, val);
    }
    vdev->guest_features = val;
    return bad ? -1 : 0;
}

int virtio_set_features(VirtIODevice *vdev, uint64_t val)
{
    int ret;
    /*
     * The driver must not attempt to set features after feature negotiation
     * has finished.
     */
    if (vdev->status & VIRTIO_CONFIG_S_FEATURES_OK) {
        return -EINVAL;
    }

    if (val & (1ull << VIRTIO_F_BAD_FEATURE)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: guest driver for %s has enabled UNUSED(30) feature bit!\n",
                      __func__, vdev->name);
    }

    ret = virtio_set_features_nocheck(vdev, val);
    if (virtio_vdev_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX)) {
        /* VIRTIO_RING_F_EVENT_IDX changes the size of the caches.  */
        int i;
        for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
            if (vdev->vq[i].vring.num != 0) {
                virtio_init_region_cache(vdev, i);
            }
        }
    }
    if (!ret) {
        if (!virtio_device_started(vdev, vdev->status) &&
            !virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
            vdev->start_on_kick = true;
        }
    }
    return ret;
}

size_t virtio_get_config_size(const VirtIOConfigSizeParams *params,
                              uint64_t host_features)
{
    size_t config_size = params->min_size;
    const VirtIOFeature *feature_sizes = params->feature_sizes;
    size_t i;

    for (i = 0; feature_sizes[i].flags != 0; i++) {
        if (host_features & feature_sizes[i].flags) {
            config_size = MAX(feature_sizes[i].end, config_size);
        }
    }

    assert(config_size <= params->max_size);
    return config_size;
}

int virtio_load(VirtIODevice *vdev, QEMUFile *f, int version_id)
{
    int i, ret;
    int32_t config_len;
    uint32_t num;
    uint32_t features;
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);

    /*
     * We poison the endianness to ensure it does not get used before
     * subsections have been loaded.
     */
    vdev->device_endian = VIRTIO_DEVICE_ENDIAN_UNKNOWN;

    if (k->load_config) {
        ret = k->load_config(qbus->parent, f);
        if (ret)
            return ret;
    }

    qemu_get_8s(f, &vdev->status);
    qemu_get_8s(f, &vdev->isr);
    qemu_get_be16s(f, &vdev->queue_sel);
    if (vdev->queue_sel >= VIRTIO_QUEUE_MAX) {
        return -1;
    }
    qemu_get_be32s(f, &features);

    /*
     * Temporarily set guest_features low bits - needed by
     * virtio net load code testing for VIRTIO_NET_F_CTRL_GUEST_OFFLOADS
     * VIRTIO_NET_F_GUEST_ANNOUNCE and VIRTIO_NET_F_CTRL_VQ.
     *
     * Note: devices should always test host features in future - don't create
     * new dependencies like this.
     */
    vdev->guest_features = features;

    config_len = qemu_get_be32(f);

    /*
     * There are cases where the incoming config can be bigger or smaller
     * than what we have; so load what we have space for, and skip
     * any excess that's in the stream.
     */
    qemu_get_buffer(f, vdev->config, MIN(config_len, vdev->config_len));

    while (config_len > vdev->config_len) {
        qemu_get_byte(f);
        config_len--;
    }

    num = qemu_get_be32(f);

    if (num > VIRTIO_QUEUE_MAX) {
        error_report("Invalid number of virtqueues: 0x%x", num);
        return -1;
    }

    for (i = 0; i < num; i++) {
        vdev->vq[i].vring.num = qemu_get_be32(f);
        if (k->has_variable_vring_alignment) {
            vdev->vq[i].vring.align = qemu_get_be32(f);
        }
        vdev->vq[i].vring.desc = qemu_get_be64(f);
        qemu_get_be16s(f, &vdev->vq[i].last_avail_idx);
        vdev->vq[i].signalled_used_valid = false;
        vdev->vq[i].notification = true;

        if (!vdev->vq[i].vring.desc && vdev->vq[i].last_avail_idx) {
            error_report("VQ %d address 0x0 "
                         "inconsistent with Host index 0x%x",
                         i, vdev->vq[i].last_avail_idx);
            return -1;
        }
        if (k->load_queue) {
            ret = k->load_queue(qbus->parent, i, f);
            if (ret)
                return ret;
        }
    }

    virtio_notify_vector(vdev, VIRTIO_NO_VECTOR);

    if (vdc->load != NULL) {
        ret = vdc->load(vdev, f, version_id);
        if (ret) {
            return ret;
        }
    }

    if (vdc->vmsd) {
        ret = vmstate_load_state(f, vdc->vmsd, vdev, version_id);
        if (ret) {
            return ret;
        }
    }

    /* Subsections */
    ret = vmstate_load_state(f, &vmstate_virtio, vdev, 1);
    if (ret) {
        return ret;
    }

    if (vdev->device_endian == VIRTIO_DEVICE_ENDIAN_UNKNOWN) {
        vdev->device_endian = virtio_default_endian();
    }

    if (virtio_64bit_features_needed(vdev)) {
        /*
         * Subsection load filled vdev->guest_features.  Run them
         * through virtio_set_features to sanity-check them against
         * host_features.
         */
        uint64_t features64 = vdev->guest_features;
        if (virtio_set_features_nocheck(vdev, features64) < 0) {
            error_report("Features 0x%" PRIx64 " unsupported. "
                         "Allowed features: 0x%" PRIx64,
                         features64, vdev->host_features);
            return -1;
        }
    } else {
        if (virtio_set_features_nocheck(vdev, features) < 0) {
            error_report("Features 0x%x unsupported. "
                         "Allowed features: 0x%" PRIx64,
                         features, vdev->host_features);
            return -1;
        }
    }

    if (!virtio_device_started(vdev, vdev->status) &&
        !virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        vdev->start_on_kick = true;
    }

    RCU_READ_LOCK_GUARD();
    for (i = 0; i < num; i++) {
        if (vdev->vq[i].vring.desc) {
            uint16_t nheads;

            /*
             * VIRTIO-1 devices migrate desc, used, and avail ring addresses so
             * only the region cache needs to be set up.  Legacy devices need
             * to calculate used and avail ring addresses based on the desc
             * address.
             */
            if (virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
                virtio_init_region_cache(vdev, i);
            } else {
                virtio_queue_update_rings(vdev, i);
            }

            if (virtio_vdev_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
                vdev->vq[i].shadow_avail_idx = vdev->vq[i].last_avail_idx;
                vdev->vq[i].shadow_avail_wrap_counter =
                                        vdev->vq[i].last_avail_wrap_counter;
                continue;
            }

            nheads = vring_avail_idx(&vdev->vq[i]) - vdev->vq[i].last_avail_idx;
            /* Check it isn't doing strange things with descriptor numbers. */
            if (nheads > vdev->vq[i].vring.num) {
                virtio_error(vdev, "VQ %d size 0x%x Guest index 0x%x "
                             "inconsistent with Host index 0x%x: delta 0x%x",
                             i, vdev->vq[i].vring.num,
                             vring_avail_idx(&vdev->vq[i]),
                             vdev->vq[i].last_avail_idx, nheads);
                vdev->vq[i].used_idx = 0;
                vdev->vq[i].shadow_avail_idx = 0;
                vdev->vq[i].inuse = 0;
                continue;
            }
            vdev->vq[i].used_idx = vring_used_idx(&vdev->vq[i]);
            vdev->vq[i].shadow_avail_idx = vring_avail_idx(&vdev->vq[i]);

            /*
             * Some devices migrate VirtQueueElements that have been popped
             * from the avail ring but not yet returned to the used ring.
             * Since max ring size < UINT16_MAX it's safe to use modulo
             * UINT16_MAX + 1 subtraction.
             */
            vdev->vq[i].inuse = (uint16_t)(vdev->vq[i].last_avail_idx -
                                vdev->vq[i].used_idx);
            if (vdev->vq[i].inuse > vdev->vq[i].vring.num) {
                error_report("VQ %d size 0x%x < last_avail_idx 0x%x - "
                             "used_idx 0x%x",
                             i, vdev->vq[i].vring.num,
                             vdev->vq[i].last_avail_idx,
                             vdev->vq[i].used_idx);
                return -1;
            }
        }
    }

    if (vdc->post_load) {
        ret = vdc->post_load(vdev);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

void virtio_cleanup(VirtIODevice *vdev)
{
    qemu_del_vm_change_state_handler(vdev->vmstate);
}

static void virtio_vmstate_change(void *opaque, bool running, RunState state)
{
    VirtIODevice *vdev = opaque;
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    bool backend_run = running && virtio_device_started(vdev, vdev->status);
    vdev->vm_running = running;

    if (backend_run) {
        virtio_set_status(vdev, vdev->status);
    }

    if (k->vmstate_change) {
        k->vmstate_change(qbus->parent, backend_run);
    }

    if (!backend_run) {
        virtio_set_status(vdev, vdev->status);
    }
}

void virtio_instance_init_common(Object *proxy_obj, void *data,
                                 size_t vdev_size, const char *vdev_name)
{
    DeviceState *vdev = data;

    object_initialize_child_with_props(proxy_obj, "virtio-backend", vdev,
                                       vdev_size, vdev_name, &error_abort,
                                       NULL);
    qdev_alias_all_properties(vdev, proxy_obj);
}

void virtio_init(VirtIODevice *vdev, uint16_t device_id, size_t config_size)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int i;
    int nvectors = k->query_nvectors ? k->query_nvectors(qbus->parent) : 0;

    if (nvectors) {
        vdev->vector_queues =
            g_malloc0(sizeof(*vdev->vector_queues) * nvectors);
    }

    vdev->start_on_kick = false;
    vdev->started = false;
    vdev->vhost_started = false;
    vdev->device_id = device_id;
    vdev->status = 0;
    qatomic_set(&vdev->isr, 0);
    vdev->queue_sel = 0;
    vdev->config_vector = VIRTIO_NO_VECTOR;
    vdev->vq = g_new0(VirtQueue, VIRTIO_QUEUE_MAX);
    vdev->vm_running = runstate_is_running();
    vdev->broken = false;
    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        vdev->vq[i].vector = VIRTIO_NO_VECTOR;
        vdev->vq[i].vdev = vdev;
        vdev->vq[i].queue_index = i;
        vdev->vq[i].host_notifier_enabled = false;
    }

    vdev->name = virtio_id_to_name(device_id);
    vdev->config_len = config_size;
    if (vdev->config_len) {
        vdev->config = g_malloc0(config_size);
    } else {
        vdev->config = NULL;
    }
    vdev->vmstate = qdev_add_vm_change_state_handler(DEVICE(vdev),
            virtio_vmstate_change, vdev);
    vdev->device_endian = virtio_default_endian();
    vdev->use_guest_notifier_mask = true;
}

/*
 * Only devices that have already been around prior to defining the virtio
 * standard support legacy mode; this includes devices not specified in the
 * standard. All newer devices conform to the virtio standard only.
 */
bool virtio_legacy_allowed(VirtIODevice *vdev)
{
    switch (vdev->device_id) {
    case VIRTIO_ID_NET:
    case VIRTIO_ID_BLOCK:
    case VIRTIO_ID_CONSOLE:
    case VIRTIO_ID_RNG:
    case VIRTIO_ID_BALLOON:
    case VIRTIO_ID_RPMSG:
    case VIRTIO_ID_SCSI:
    case VIRTIO_ID_9P:
    case VIRTIO_ID_RPROC_SERIAL:
    case VIRTIO_ID_CAIF:
        return true;
    default:
        return false;
    }
}

bool virtio_legacy_check_disabled(VirtIODevice *vdev)
{
    return vdev->disable_legacy_check;
}

hwaddr virtio_queue_get_desc_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.desc;
}

bool virtio_queue_enabled_legacy(VirtIODevice *vdev, int n)
{
    return virtio_queue_get_desc_addr(vdev, n) != 0;
}

bool virtio_queue_enabled(VirtIODevice *vdev, int n)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (k->queue_enabled) {
        return k->queue_enabled(qbus->parent, n);
    }
    return virtio_queue_enabled_legacy(vdev, n);
}

hwaddr virtio_queue_get_avail_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.avail;
}

hwaddr virtio_queue_get_used_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.used;
}

hwaddr virtio_queue_get_desc_size(VirtIODevice *vdev, int n)
{
    return sizeof(VRingDesc) * vdev->vq[n].vring.num;
}

hwaddr virtio_queue_get_avail_size(VirtIODevice *vdev, int n)
{
    int s;

    if (virtio_vdev_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        return sizeof(struct VRingPackedDescEvent);
    }

    s = virtio_vdev_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX) ? 2 : 0;
    return offsetof(VRingAvail, ring) +
        sizeof(uint16_t) * vdev->vq[n].vring.num + s;
}

hwaddr virtio_queue_get_used_size(VirtIODevice *vdev, int n)
{
    int s;

    if (virtio_vdev_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        return sizeof(struct VRingPackedDescEvent);
    }

    s = virtio_vdev_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX) ? 2 : 0;
    return offsetof(VRingUsed, ring) +
        sizeof(VRingUsedElem) * vdev->vq[n].vring.num + s;
}

static unsigned int virtio_queue_packed_get_last_avail_idx(VirtIODevice *vdev,
                                                           int n)
{
    unsigned int avail, used;

    avail = vdev->vq[n].last_avail_idx;
    avail |= ((uint16_t)vdev->vq[n].last_avail_wrap_counter) << 15;

    used = vdev->vq[n].used_idx;
    used |= ((uint16_t)vdev->vq[n].used_wrap_counter) << 15;

    return avail | used << 16;
}

static uint16_t virtio_queue_split_get_last_avail_idx(VirtIODevice *vdev,
                                                      int n)
{
    return vdev->vq[n].last_avail_idx;
}

unsigned int virtio_queue_get_last_avail_idx(VirtIODevice *vdev, int n)
{
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        return virtio_queue_packed_get_last_avail_idx(vdev, n);
    } else {
        return virtio_queue_split_get_last_avail_idx(vdev, n);
    }
}

static void virtio_queue_packed_set_last_avail_idx(VirtIODevice *vdev,
                                                   int n, unsigned int idx)
{
    struct VirtQueue *vq = &vdev->vq[n];

    vq->last_avail_idx = vq->shadow_avail_idx = idx & 0x7fff;
    vq->last_avail_wrap_counter =
        vq->shadow_avail_wrap_counter = !!(idx & 0x8000);
    idx >>= 16;
    vq->used_idx = idx & 0x7ffff;
    vq->used_wrap_counter = !!(idx & 0x8000);
}

static void virtio_queue_split_set_last_avail_idx(VirtIODevice *vdev,
                                                  int n, unsigned int idx)
{
        vdev->vq[n].last_avail_idx = idx;
        vdev->vq[n].shadow_avail_idx = idx;
}

void virtio_queue_set_last_avail_idx(VirtIODevice *vdev, int n,
                                     unsigned int idx)
{
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        virtio_queue_packed_set_last_avail_idx(vdev, n, idx);
    } else {
        virtio_queue_split_set_last_avail_idx(vdev, n, idx);
    }
}

static void virtio_queue_packed_restore_last_avail_idx(VirtIODevice *vdev,
                                                       int n)
{
    /* We don't have a reference like avail idx in shared memory */
    return;
}

static void virtio_queue_split_restore_last_avail_idx(VirtIODevice *vdev,
                                                      int n)
{
    RCU_READ_LOCK_GUARD();
    if (vdev->vq[n].vring.desc) {
        vdev->vq[n].last_avail_idx = vring_used_idx(&vdev->vq[n]);
        vdev->vq[n].shadow_avail_idx = vdev->vq[n].last_avail_idx;
    }
}

void virtio_queue_restore_last_avail_idx(VirtIODevice *vdev, int n)
{
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        virtio_queue_packed_restore_last_avail_idx(vdev, n);
    } else {
        virtio_queue_split_restore_last_avail_idx(vdev, n);
    }
}

static void virtio_queue_packed_update_used_idx(VirtIODevice *vdev, int n)
{
    /* used idx was updated through set_last_avail_idx() */
    return;
}

static void virtio_split_packed_update_used_idx(VirtIODevice *vdev, int n)
{
    RCU_READ_LOCK_GUARD();
    if (vdev->vq[n].vring.desc) {
        vdev->vq[n].used_idx = vring_used_idx(&vdev->vq[n]);
    }
}

void virtio_queue_update_used_idx(VirtIODevice *vdev, int n)
{
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        return virtio_queue_packed_update_used_idx(vdev, n);
    } else {
        return virtio_split_packed_update_used_idx(vdev, n);
    }
}

void virtio_queue_invalidate_signalled_used(VirtIODevice *vdev, int n)
{
    vdev->vq[n].signalled_used_valid = false;
}

VirtQueue *virtio_get_queue(VirtIODevice *vdev, int n)
{
    return vdev->vq + n;
}

uint16_t virtio_get_queue_index(VirtQueue *vq)
{
    return vq->queue_index;
}

static void virtio_queue_guest_notifier_read(EventNotifier *n)
{
    VirtQueue *vq = container_of(n, VirtQueue, guest_notifier);
    if (event_notifier_test_and_clear(n)) {
        virtio_irq(vq);
    }
}

void virtio_queue_set_guest_notifier_fd_handler(VirtQueue *vq, bool assign,
                                                bool with_irqfd)
{
    if (assign && !with_irqfd) {
        event_notifier_set_handler(&vq->guest_notifier,
                                   virtio_queue_guest_notifier_read);
    } else {
        event_notifier_set_handler(&vq->guest_notifier, NULL);
    }
    if (!assign) {
        /* Test and clear notifier before closing it,
         * in case poll callback didn't have time to run. */
        virtio_queue_guest_notifier_read(&vq->guest_notifier);
    }
}

EventNotifier *virtio_queue_get_guest_notifier(VirtQueue *vq)
{
    return &vq->guest_notifier;
}

static void virtio_queue_host_notifier_aio_poll_begin(EventNotifier *n)
{
    VirtQueue *vq = container_of(n, VirtQueue, host_notifier);

    virtio_queue_set_notification(vq, 0);
}

static bool virtio_queue_host_notifier_aio_poll(void *opaque)
{
    EventNotifier *n = opaque;
    VirtQueue *vq = container_of(n, VirtQueue, host_notifier);

    return vq->vring.desc && !virtio_queue_empty(vq);
}

static void virtio_queue_host_notifier_aio_poll_ready(EventNotifier *n)
{
    VirtQueue *vq = container_of(n, VirtQueue, host_notifier);

    virtio_queue_notify_vq(vq);
}

static void virtio_queue_host_notifier_aio_poll_end(EventNotifier *n)
{
    VirtQueue *vq = container_of(n, VirtQueue, host_notifier);

    /* Caller polls once more after this to catch requests that race with us */
    virtio_queue_set_notification(vq, 1);
}

void virtio_queue_aio_attach_host_notifier(VirtQueue *vq, AioContext *ctx)
{
    aio_set_event_notifier(ctx, &vq->host_notifier, true,
                           virtio_queue_host_notifier_read,
                           virtio_queue_host_notifier_aio_poll,
                           virtio_queue_host_notifier_aio_poll_ready);
    aio_set_event_notifier_poll(ctx, &vq->host_notifier,
                                virtio_queue_host_notifier_aio_poll_begin,
                                virtio_queue_host_notifier_aio_poll_end);
}

/*
 * Same as virtio_queue_aio_attach_host_notifier() but without polling. Use
 * this for rx virtqueues and similar cases where the virtqueue handler
 * function does not pop all elements. When the virtqueue is left non-empty
 * polling consumes CPU cycles and should not be used.
 */
void virtio_queue_aio_attach_host_notifier_no_poll(VirtQueue *vq, AioContext *ctx)
{
    aio_set_event_notifier(ctx, &vq->host_notifier, true,
                           virtio_queue_host_notifier_read,
                           NULL, NULL);
}

void virtio_queue_aio_detach_host_notifier(VirtQueue *vq, AioContext *ctx)
{
    aio_set_event_notifier(ctx, &vq->host_notifier, true, NULL, NULL, NULL);
    /* Test and clear notifier before after disabling event,
     * in case poll callback didn't have time to run. */
    virtio_queue_host_notifier_read(&vq->host_notifier);
}

void virtio_queue_host_notifier_read(EventNotifier *n)
{
    VirtQueue *vq = container_of(n, VirtQueue, host_notifier);
    if (event_notifier_test_and_clear(n)) {
        virtio_queue_notify_vq(vq);
    }
}

EventNotifier *virtio_queue_get_host_notifier(VirtQueue *vq)
{
    return &vq->host_notifier;
}

void virtio_queue_set_host_notifier_enabled(VirtQueue *vq, bool enabled)
{
    vq->host_notifier_enabled = enabled;
}

int virtio_queue_set_host_notifier_mr(VirtIODevice *vdev, int n,
                                      MemoryRegion *mr, bool assign)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (k->set_host_notifier_mr) {
        return k->set_host_notifier_mr(qbus->parent, n, mr, assign);
    }

    return -1;
}

void virtio_device_set_child_bus_name(VirtIODevice *vdev, char *bus_name)
{
    g_free(vdev->bus_name);
    vdev->bus_name = g_strdup(bus_name);
}

void G_GNUC_PRINTF(2, 3) virtio_error(VirtIODevice *vdev, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vreport(fmt, ap);
    va_end(ap);

    if (virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        vdev->status = vdev->status | VIRTIO_CONFIG_S_NEEDS_RESET;
        virtio_notify_config(vdev);
    }

    vdev->broken = true;
}

static void virtio_memory_listener_commit(MemoryListener *listener)
{
    VirtIODevice *vdev = container_of(listener, VirtIODevice, listener);
    int i;

    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num == 0) {
            break;
        }
        virtio_init_region_cache(vdev, i);
    }
}

static void virtio_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(dev);
    Error *err = NULL;

    /* Devices should either use vmsd or the load/save methods */
    assert(!vdc->vmsd || !vdc->load);

    if (vdc->realize != NULL) {
        vdc->realize(dev, &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
    }

    virtio_bus_device_plugged(vdev, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        vdc->unrealize(dev);
        return;
    }

    vdev->listener.commit = virtio_memory_listener_commit;
    vdev->listener.name = "virtio";
    memory_listener_register(&vdev->listener, vdev->dma_as);
    QTAILQ_INSERT_TAIL(&virtio_list, vdev, next);
}

static void virtio_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(dev);

    memory_listener_unregister(&vdev->listener);
    virtio_bus_device_unplugged(vdev);

    if (vdc->unrealize != NULL) {
        vdc->unrealize(dev);
    }

    QTAILQ_REMOVE(&virtio_list, vdev, next);
    g_free(vdev->bus_name);
    vdev->bus_name = NULL;
}

static void virtio_device_free_virtqueues(VirtIODevice *vdev)
{
    int i;
    if (!vdev->vq) {
        return;
    }

    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num == 0) {
            break;
        }
        virtio_virtqueue_reset_region_cache(&vdev->vq[i]);
    }
    g_free(vdev->vq);
}

static void virtio_device_instance_finalize(Object *obj)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(obj);

    virtio_device_free_virtqueues(vdev);

    g_free(vdev->config);
    g_free(vdev->vector_queues);
}

static Property virtio_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIODevice, host_features),
    DEFINE_PROP_BOOL("use-started", VirtIODevice, use_started, true),
    DEFINE_PROP_BOOL("use-disabled-flag", VirtIODevice, use_disabled_flag, true),
    DEFINE_PROP_BOOL("x-disable-legacy-check", VirtIODevice,
                     disable_legacy_check, false),
    DEFINE_PROP_END_OF_LIST(),
};

static int virtio_device_start_ioeventfd_impl(VirtIODevice *vdev)
{
    VirtioBusState *qbus = VIRTIO_BUS(qdev_get_parent_bus(DEVICE(vdev)));
    int i, n, r, err;

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();
    for (n = 0; n < VIRTIO_QUEUE_MAX; n++) {
        VirtQueue *vq = &vdev->vq[n];
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }
        r = virtio_bus_set_host_notifier(qbus, n, true);
        if (r < 0) {
            err = r;
            goto assign_error;
        }
        event_notifier_set_handler(&vq->host_notifier,
                                   virtio_queue_host_notifier_read);
    }

    for (n = 0; n < VIRTIO_QUEUE_MAX; n++) {
        /* Kick right away to begin processing requests already in vring */
        VirtQueue *vq = &vdev->vq[n];
        if (!vq->vring.num) {
            continue;
        }
        event_notifier_set(&vq->host_notifier);
    }
    memory_region_transaction_commit();
    return 0;

assign_error:
    i = n; /* save n for a second iteration after transaction is committed. */
    while (--n >= 0) {
        VirtQueue *vq = &vdev->vq[n];
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }

        event_notifier_set_handler(&vq->host_notifier, NULL);
        r = virtio_bus_set_host_notifier(qbus, n, false);
        assert(r >= 0);
    }
    /*
     * The transaction expects the ioeventfds to be open when it
     * commits. Do it now, before the cleanup loop.
     */
    memory_region_transaction_commit();

    while (--i >= 0) {
        if (!virtio_queue_get_num(vdev, i)) {
            continue;
        }
        virtio_bus_cleanup_host_notifier(qbus, i);
    }
    return err;
}

int virtio_device_start_ioeventfd(VirtIODevice *vdev)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);

    return virtio_bus_start_ioeventfd(vbus);
}

static void virtio_device_stop_ioeventfd_impl(VirtIODevice *vdev)
{
    VirtioBusState *qbus = VIRTIO_BUS(qdev_get_parent_bus(DEVICE(vdev)));
    int n, r;

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();
    for (n = 0; n < VIRTIO_QUEUE_MAX; n++) {
        VirtQueue *vq = &vdev->vq[n];

        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }
        event_notifier_set_handler(&vq->host_notifier, NULL);
        r = virtio_bus_set_host_notifier(qbus, n, false);
        assert(r >= 0);
    }
    /*
     * The transaction expects the ioeventfds to be open when it
     * commits. Do it now, before the cleanup loop.
     */
    memory_region_transaction_commit();

    for (n = 0; n < VIRTIO_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }
        virtio_bus_cleanup_host_notifier(qbus, n);
    }
}

int virtio_device_grab_ioeventfd(VirtIODevice *vdev)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);

    return virtio_bus_grab_ioeventfd(vbus);
}

void virtio_device_release_ioeventfd(VirtIODevice *vdev)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);

    virtio_bus_release_ioeventfd(vbus);
}

static void virtio_device_class_init(ObjectClass *klass, void *data)
{
    /* Set the default value here. */
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = virtio_device_realize;
    dc->unrealize = virtio_device_unrealize;
    dc->bus_type = TYPE_VIRTIO_BUS;
    device_class_set_props(dc, virtio_properties);
    vdc->start_ioeventfd = virtio_device_start_ioeventfd_impl;
    vdc->stop_ioeventfd = virtio_device_stop_ioeventfd_impl;

    vdc->legacy_features |= VIRTIO_LEGACY_FEATURES;

    QTAILQ_INIT(&virtio_list);
}

bool virtio_device_ioeventfd_enabled(VirtIODevice *vdev)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);

    return virtio_bus_ioeventfd_enabled(vbus);
}

VirtioInfoList *qmp_x_query_virtio(Error **errp)
{
    VirtioInfoList *list = NULL;
    VirtioInfoList *node;
    VirtIODevice *vdev;

    QTAILQ_FOREACH(vdev, &virtio_list, next) {
        DeviceState *dev = DEVICE(vdev);
        Error *err = NULL;
        QObject *obj = qmp_qom_get(dev->canonical_path, "realized", &err);

        if (err == NULL) {
            GString *is_realized = qobject_to_json_pretty(obj, true);
            /* virtio device is NOT realized, remove it from list */
            if (!strncmp(is_realized->str, "false", 4)) {
                QTAILQ_REMOVE(&virtio_list, vdev, next);
            } else {
                node = g_new0(VirtioInfoList, 1);
                node->value = g_new(VirtioInfo, 1);
                node->value->path = g_strdup(dev->canonical_path);
                node->value->name = g_strdup(vdev->name);
                QAPI_LIST_PREPEND(list, node->value);
            }
           g_string_free(is_realized, true);
        }
        qobject_unref(obj);
    }

    return list;
}

static VirtIODevice *virtio_device_find(const char *path)
{
    VirtIODevice *vdev;

    QTAILQ_FOREACH(vdev, &virtio_list, next) {
        DeviceState *dev = DEVICE(vdev);

        if (strcmp(dev->canonical_path, path) != 0) {
            continue;
        }

        Error *err = NULL;
        QObject *obj = qmp_qom_get(dev->canonical_path, "realized", &err);
        if (err == NULL) {
            GString *is_realized = qobject_to_json_pretty(obj, true);
            /* virtio device is NOT realized, remove it from list */
            if (!strncmp(is_realized->str, "false", 4)) {
                g_string_free(is_realized, true);
                qobject_unref(obj);
                QTAILQ_REMOVE(&virtio_list, vdev, next);
                return NULL;
            }
            g_string_free(is_realized, true);
        } else {
            /* virtio device doesn't exist in QOM tree */
            QTAILQ_REMOVE(&virtio_list, vdev, next);
            qobject_unref(obj);
            return NULL;
        }
        /* device exists in QOM tree & is realized */
        qobject_unref(obj);
        return vdev;
    }
    return NULL;
}

#define CONVERT_FEATURES(type, map, is_status, bitmap)   \
    ({                                                   \
        type *list = NULL;                               \
        type *node;                                      \
        for (i = 0; map[i].virtio_bit != -1; i++) {      \
            if (is_status) {                             \
                bit = map[i].virtio_bit;                 \
            }                                            \
            else {                                       \
                bit = 1ULL << map[i].virtio_bit;         \
            }                                            \
            if ((bitmap & bit) == 0) {                   \
                continue;                                \
            }                                            \
            node = g_new0(type, 1);                      \
            node->value = g_strdup(map[i].feature_desc); \
            node->next = list;                           \
            list = node;                                 \
            bitmap ^= bit;                               \
        }                                                \
        list;                                            \
    })

static VirtioDeviceStatus *qmp_decode_status(uint8_t bitmap)
{
    VirtioDeviceStatus *status;
    uint8_t bit;
    int i;

    status = g_new0(VirtioDeviceStatus, 1);
    status->statuses = CONVERT_FEATURES(strList, virtio_config_status_map,
                                        1, bitmap);
    status->has_unknown_statuses = bitmap != 0;
    if (status->has_unknown_statuses) {
        status->unknown_statuses = bitmap;
    }

    return status;
}

static VhostDeviceProtocols *qmp_decode_protocols(uint64_t bitmap)
{
    VhostDeviceProtocols *vhu_protocols;
    uint64_t bit;
    int i;

    vhu_protocols = g_new0(VhostDeviceProtocols, 1);
    vhu_protocols->protocols =
                    CONVERT_FEATURES(strList,
                                     vhost_user_protocol_map, 0, bitmap);
    vhu_protocols->has_unknown_protocols = bitmap != 0;
    if (vhu_protocols->has_unknown_protocols) {
        vhu_protocols->unknown_protocols = bitmap;
    }

    return vhu_protocols;
}

static VirtioDeviceFeatures *qmp_decode_features(uint16_t device_id,
                                                 uint64_t bitmap)
{
    VirtioDeviceFeatures *features;
    uint64_t bit;
    int i;

    features = g_new0(VirtioDeviceFeatures, 1);
    features->has_dev_features = true;

    /* transport features */
    features->transports = CONVERT_FEATURES(strList, virtio_transport_map, 0,
                                            bitmap);

    /* device features */
    switch (device_id) {
#ifdef CONFIG_VIRTIO_SERIAL
    case VIRTIO_ID_CONSOLE:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_serial_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_BLK
    case VIRTIO_ID_BLOCK:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_blk_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_GPU
    case VIRTIO_ID_GPU:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_gpu_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_NET
    case VIRTIO_ID_NET:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_net_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_SCSI
    case VIRTIO_ID_SCSI:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_scsi_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_BALLOON
    case VIRTIO_ID_BALLOON:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_balloon_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_IOMMU
    case VIRTIO_ID_IOMMU:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_iommu_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_INPUT
    case VIRTIO_ID_INPUT:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_input_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VHOST_USER_FS
    case VIRTIO_ID_FS:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_fs_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VHOST_VSOCK
    case VIRTIO_ID_VSOCK:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_vsock_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_CRYPTO
    case VIRTIO_ID_CRYPTO:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_crypto_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_MEM
    case VIRTIO_ID_MEM:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_mem_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_I2C_ADAPTER
    case VIRTIO_ID_I2C_ADAPTER:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_i2c_feature_map, 0, bitmap);
        break;
#endif
#ifdef CONFIG_VIRTIO_RNG
    case VIRTIO_ID_RNG:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_rng_feature_map, 0, bitmap);
        break;
#endif
    /* No features */
    case VIRTIO_ID_9P:
    case VIRTIO_ID_PMEM:
    case VIRTIO_ID_IOMEM:
    case VIRTIO_ID_RPMSG:
    case VIRTIO_ID_CLOCK:
    case VIRTIO_ID_MAC80211_WLAN:
    case VIRTIO_ID_MAC80211_HWSIM:
    case VIRTIO_ID_RPROC_SERIAL:
    case VIRTIO_ID_MEMORY_BALLOON:
    case VIRTIO_ID_CAIF:
    case VIRTIO_ID_SIGNAL_DIST:
    case VIRTIO_ID_PSTORE:
    case VIRTIO_ID_SOUND:
    case VIRTIO_ID_BT:
    case VIRTIO_ID_RPMB:
    case VIRTIO_ID_VIDEO_ENCODER:
    case VIRTIO_ID_VIDEO_DECODER:
    case VIRTIO_ID_SCMI:
    case VIRTIO_ID_NITRO_SEC_MOD:
    case VIRTIO_ID_WATCHDOG:
    case VIRTIO_ID_CAN:
    case VIRTIO_ID_DMABUF:
    case VIRTIO_ID_PARAM_SERV:
    case VIRTIO_ID_AUDIO_POLICY:
    case VIRTIO_ID_GPIO:
        break;
    default:
        g_assert_not_reached();
    }

    features->has_unknown_dev_features = bitmap != 0;
    if (features->has_unknown_dev_features) {
        features->unknown_dev_features = bitmap;
    }

    return features;
}

VirtioStatus *qmp_x_query_virtio_status(const char *path, Error **errp)
{
    VirtIODevice *vdev;
    VirtioStatus *status;

    vdev = virtio_device_find(path);
    if (vdev == NULL) {
        error_setg(errp, "Path %s is not a VirtIODevice", path);
        return NULL;
    }

    status = g_new0(VirtioStatus, 1);
    status->name = g_strdup(vdev->name);
    status->device_id = vdev->device_id;
    status->vhost_started = vdev->vhost_started;
    status->guest_features = qmp_decode_features(vdev->device_id,
                                                 vdev->guest_features);
    status->host_features = qmp_decode_features(vdev->device_id,
                                                vdev->host_features);
    status->backend_features = qmp_decode_features(vdev->device_id,
                                                   vdev->backend_features);

    switch (vdev->device_endian) {
    case VIRTIO_DEVICE_ENDIAN_LITTLE:
        status->device_endian = g_strdup("little");
        break;
    case VIRTIO_DEVICE_ENDIAN_BIG:
        status->device_endian = g_strdup("big");
        break;
    default:
        status->device_endian = g_strdup("unknown");
        break;
    }

    status->num_vqs = virtio_get_num_queues(vdev);
    status->status = qmp_decode_status(vdev->status);
    status->isr = vdev->isr;
    status->queue_sel = vdev->queue_sel;
    status->vm_running = vdev->vm_running;
    status->broken = vdev->broken;
    status->disabled = vdev->disabled;
    status->use_started = vdev->use_started;
    status->started = vdev->started;
    status->start_on_kick = vdev->start_on_kick;
    status->disable_legacy_check = vdev->disable_legacy_check;
    status->bus_name = g_strdup(vdev->bus_name);
    status->use_guest_notifier_mask = vdev->use_guest_notifier_mask;
    status->has_vhost_dev = vdev->vhost_started;

    if (vdev->vhost_started) {
        VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
        struct vhost_dev *hdev = vdc->get_vhost(vdev);

        status->vhost_dev = g_new0(VhostStatus, 1);
        status->vhost_dev->n_mem_sections = hdev->n_mem_sections;
        status->vhost_dev->n_tmp_sections = hdev->n_tmp_sections;
        status->vhost_dev->nvqs = hdev->nvqs;
        status->vhost_dev->vq_index = hdev->vq_index;
        status->vhost_dev->features =
            qmp_decode_features(vdev->device_id, hdev->features);
        status->vhost_dev->acked_features =
            qmp_decode_features(vdev->device_id, hdev->acked_features);
        status->vhost_dev->backend_features =
            qmp_decode_features(vdev->device_id, hdev->backend_features);
        status->vhost_dev->protocol_features =
            qmp_decode_protocols(hdev->protocol_features);
        status->vhost_dev->max_queues = hdev->max_queues;
        status->vhost_dev->backend_cap = hdev->backend_cap;
        status->vhost_dev->log_enabled = hdev->log_enabled;
        status->vhost_dev->log_size = hdev->log_size;
    }

    return status;
}

VirtVhostQueueStatus *qmp_x_query_virtio_vhost_queue_status(const char *path,
                                                            uint16_t queue,
                                                            Error **errp)
{
    VirtIODevice *vdev;
    VirtVhostQueueStatus *status;

    vdev = virtio_device_find(path);
    if (vdev == NULL) {
        error_setg(errp, "Path %s is not a VirtIODevice", path);
        return NULL;
    }

    if (!vdev->vhost_started) {
        error_setg(errp, "Error: vhost device has not started yet");
        return NULL;
    }

    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
    struct vhost_dev *hdev = vdc->get_vhost(vdev);

    if (queue < hdev->vq_index || queue >= hdev->vq_index + hdev->nvqs) {
        error_setg(errp, "Invalid vhost virtqueue number %d", queue);
        return NULL;
    }

    status = g_new0(VirtVhostQueueStatus, 1);
    status->name = g_strdup(vdev->name);
    status->kick = hdev->vqs[queue].kick;
    status->call = hdev->vqs[queue].call;
    status->desc = (uintptr_t)hdev->vqs[queue].desc;
    status->avail = (uintptr_t)hdev->vqs[queue].avail;
    status->used = (uintptr_t)hdev->vqs[queue].used;
    status->num = hdev->vqs[queue].num;
    status->desc_phys = hdev->vqs[queue].desc_phys;
    status->desc_size = hdev->vqs[queue].desc_size;
    status->avail_phys = hdev->vqs[queue].avail_phys;
    status->avail_size = hdev->vqs[queue].avail_size;
    status->used_phys = hdev->vqs[queue].used_phys;
    status->used_size = hdev->vqs[queue].used_size;

    return status;
}

VirtQueueStatus *qmp_x_query_virtio_queue_status(const char *path,
                                                 uint16_t queue,
                                                 Error **errp)
{
    VirtIODevice *vdev;
    VirtQueueStatus *status;

    vdev = virtio_device_find(path);
    if (vdev == NULL) {
        error_setg(errp, "Path %s is not a VirtIODevice", path);
        return NULL;
    }

    if (queue >= VIRTIO_QUEUE_MAX || !virtio_queue_get_num(vdev, queue)) {
        error_setg(errp, "Invalid virtqueue number %d", queue);
        return NULL;
    }

    status = g_new0(VirtQueueStatus, 1);
    status->name = g_strdup(vdev->name);
    status->queue_index = vdev->vq[queue].queue_index;
    status->inuse = vdev->vq[queue].inuse;
    status->vring_num = vdev->vq[queue].vring.num;
    status->vring_num_default = vdev->vq[queue].vring.num_default;
    status->vring_align = vdev->vq[queue].vring.align;
    status->vring_desc = vdev->vq[queue].vring.desc;
    status->vring_avail = vdev->vq[queue].vring.avail;
    status->vring_used = vdev->vq[queue].vring.used;
    status->used_idx = vdev->vq[queue].used_idx;
    status->signalled_used = vdev->vq[queue].signalled_used;
    status->signalled_used_valid = vdev->vq[queue].signalled_used_valid;

    if (vdev->vhost_started) {
        VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
        struct vhost_dev *hdev = vdc->get_vhost(vdev);

        /* check if vq index exists for vhost as well  */
        if (queue >= hdev->vq_index && queue < hdev->vq_index + hdev->nvqs) {
            status->has_last_avail_idx = true;

            int vhost_vq_index =
                hdev->vhost_ops->vhost_get_vq_index(hdev, queue);
            struct vhost_vring_state state = {
                .index = vhost_vq_index,
            };

            status->last_avail_idx =
                hdev->vhost_ops->vhost_get_vring_base(hdev, &state);
        }
    } else {
        status->has_shadow_avail_idx = true;
        status->has_last_avail_idx = true;
        status->last_avail_idx = vdev->vq[queue].last_avail_idx;
        status->shadow_avail_idx = vdev->vq[queue].shadow_avail_idx;
    }

    return status;
}

static strList *qmp_decode_vring_desc_flags(uint16_t flags)
{
    strList *list = NULL;
    strList *node;
    int i;

    struct {
        uint16_t flag;
        const char *value;
    } map[] = {
        { VRING_DESC_F_NEXT, "next" },
        { VRING_DESC_F_WRITE, "write" },
        { VRING_DESC_F_INDIRECT, "indirect" },
        { 1 << VRING_PACKED_DESC_F_AVAIL, "avail" },
        { 1 << VRING_PACKED_DESC_F_USED, "used" },
        { 0, "" }
    };

    for (i = 0; map[i].flag; i++) {
        if ((map[i].flag & flags) == 0) {
            continue;
        }
        node = g_malloc0(sizeof(strList));
        node->value = g_strdup(map[i].value);
        node->next = list;
        list = node;
    }

    return list;
}

VirtioQueueElement *qmp_x_query_virtio_queue_element(const char *path,
                                                     uint16_t queue,
                                                     bool has_index,
                                                     uint16_t index,
                                                     Error **errp)
{
    VirtIODevice *vdev;
    VirtQueue *vq;
    VirtioQueueElement *element = NULL;

    vdev = virtio_device_find(path);
    if (vdev == NULL) {
        error_setg(errp, "Path %s is not a VirtIO device", path);
        return NULL;
    }

    if (queue >= VIRTIO_QUEUE_MAX || !virtio_queue_get_num(vdev, queue)) {
        error_setg(errp, "Invalid virtqueue number %d", queue);
        return NULL;
    }
    vq = &vdev->vq[queue];

    if (virtio_vdev_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
        error_setg(errp, "Packed ring not supported");
        return NULL;
    } else {
        unsigned int head, i, max;
        VRingMemoryRegionCaches *caches;
        MemoryRegionCache indirect_desc_cache = MEMORY_REGION_CACHE_INVALID;
        MemoryRegionCache *desc_cache;
        VRingDesc desc;
        VirtioRingDescList *list = NULL;
        VirtioRingDescList *node;
        int rc; int ndescs;

        RCU_READ_LOCK_GUARD();

        max = vq->vring.num;

        if (!has_index) {
            head = vring_avail_ring(vq, vq->last_avail_idx % vq->vring.num);
        } else {
            head = vring_avail_ring(vq, index % vq->vring.num);
        }
        i = head;

        caches = vring_get_region_caches(vq);
        if (!caches) {
            error_setg(errp, "Region caches not initialized");
            return NULL;
        }
        if (caches->desc.len < max * sizeof(VRingDesc)) {
            error_setg(errp, "Cannot map descriptor ring");
            return NULL;
        }

        desc_cache = &caches->desc;
        vring_split_desc_read(vdev, &desc, desc_cache, i);
        if (desc.flags & VRING_DESC_F_INDIRECT) {
            int64_t len;
            len = address_space_cache_init(&indirect_desc_cache, vdev->dma_as,
                                           desc.addr, desc.len, false);
            desc_cache = &indirect_desc_cache;
            if (len < desc.len) {
                error_setg(errp, "Cannot map indirect buffer");
                goto done;
            }

            max = desc.len / sizeof(VRingDesc);
            i = 0;
            vring_split_desc_read(vdev, &desc, desc_cache, i);
        }

        element = g_new0(VirtioQueueElement, 1);
        element->avail = g_new0(VirtioRingAvail, 1);
        element->used = g_new0(VirtioRingUsed, 1);
        element->name = g_strdup(vdev->name);
        element->index = head;
        element->avail->flags = vring_avail_flags(vq);
        element->avail->idx = vring_avail_idx(vq);
        element->avail->ring = head;
        element->used->flags = vring_used_flags(vq);
        element->used->idx = vring_used_idx(vq);
        ndescs = 0;

        do {
            /* A buggy driver may produce an infinite loop */
            if (ndescs >= max) {
                break;
            }
            node = g_new0(VirtioRingDescList, 1);
            node->value = g_new0(VirtioRingDesc, 1);
            node->value->addr = desc.addr;
            node->value->len = desc.len;
            node->value->flags = qmp_decode_vring_desc_flags(desc.flags);
            node->next = list;
            list = node;

            ndescs++;
            rc = virtqueue_split_read_next_desc(vdev, &desc, desc_cache,
                                                max, &i);
        } while (rc == VIRTQUEUE_READ_DESC_MORE);
        element->descs = list;
done:
        address_space_cache_destroy(&indirect_desc_cache);
    }

    return element;
}

static const TypeInfo virtio_device_info = {
    .name = TYPE_VIRTIO_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(VirtIODevice),
    .class_init = virtio_device_class_init,
    .instance_finalize = virtio_device_instance_finalize,
    .abstract = true,
    .class_size = sizeof(VirtioDeviceClass),
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_device_info);
}

type_init(virtio_register_types)

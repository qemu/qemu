/*
 * Virtio QMP helpers
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "virtio-qmp.h"

#include "qapi/error.h"
#include "qapi/qapi-commands-virtio.h"
#include "qapi/qapi-commands-qom.h"
#include "qobject/qobject.h"
#include "qobject/qjson.h"
#include "hw/virtio/vhost-user.h"

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
#include "standard-headers/linux/virtio_gpio.h"

#include CONFIG_DEVICES

#define FEATURE_ENTRY(name, desc) (qmp_virtio_feature_map_t) \
    { .virtio_bit = name, .feature_desc = desc }

/* Virtio transport features mapping */
static const qmp_virtio_feature_map_t virtio_transport_map[] = {
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
    FEATURE_ENTRY(VIRTIO_F_RING_RESET, \
            "VIRTIO_F_RING_RESET: Driver can reset a queue individually"),
    /* Virtio ring transport features */
    FEATURE_ENTRY(VIRTIO_RING_F_INDIRECT_DESC, \
            "VIRTIO_RING_F_INDIRECT_DESC: Indirect descriptors supported"),
    FEATURE_ENTRY(VIRTIO_RING_F_EVENT_IDX, \
            "VIRTIO_RING_F_EVENT_IDX: Used & avail. event fields enabled"),
    { -1, "" }
};

/* Vhost-user protocol features mapping */
static const qmp_virtio_feature_map_t vhost_user_protocol_map[] = {
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
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_BACKEND_REQ, \
            "VHOST_USER_PROTOCOL_F_BACKEND_REQ: Socket fd for back-end initiated "
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
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_BACKEND_SEND_FD, \
            "VHOST_USER_PROTOCOL_F_BACKEND_SEND_FD: Backend fd communication "
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
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_STATUS, \
            "VHOST_USER_PROTOCOL_F_STATUS: Querying and notifying back-end "
            "device status supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_SHARED_OBJECT, \
            "VHOST_USER_PROTOCOL_F_SHARED_OBJECT: Backend shared object "
            "supported"),
    FEATURE_ENTRY(VHOST_USER_PROTOCOL_F_DEVICE_STATE, \
            "VHOST_USER_PROTOCOL_F_DEVICE_STATE: Backend device state transfer "
            "supported"),
    { -1, "" }
};

/* virtio device configuration statuses */
static const qmp_virtio_feature_map_t virtio_config_status_map[] = {
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
#ifdef CONFIG_VIRTIO_BLK
static const qmp_virtio_feature_map_t virtio_blk_feature_map[] = {
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
    FEATURE_ENTRY(VIRTIO_BLK_F_SECURE_ERASE, \
            "VIRTIO_BLK_F_SECURE_ERASE: Secure erase supported"),
    FEATURE_ENTRY(VIRTIO_BLK_F_ZONED, \
            "VIRTIO_BLK_F_ZONED: Zoned block devices"),
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
#endif

/* virtio-serial features mapping */
#ifdef CONFIG_VIRTIO_SERIAL
static const qmp_virtio_feature_map_t virtio_serial_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_CONSOLE_F_SIZE, \
            "VIRTIO_CONSOLE_F_SIZE: Host providing console size"),
    FEATURE_ENTRY(VIRTIO_CONSOLE_F_MULTIPORT, \
            "VIRTIO_CONSOLE_F_MULTIPORT: Multiple ports for device supported"),
    FEATURE_ENTRY(VIRTIO_CONSOLE_F_EMERG_WRITE, \
            "VIRTIO_CONSOLE_F_EMERG_WRITE: Emergency write supported"),
    { -1, "" }
};
#endif

/* virtio-gpu features mapping */
#ifdef CONFIG_VIRTIO_GPU
static const qmp_virtio_feature_map_t virtio_gpu_feature_map[] = {
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
#endif

/* virtio-input features mapping */
#ifdef CONFIG_VIRTIO_INPUT
static const qmp_virtio_feature_map_t virtio_input_feature_map[] = {
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};
#endif

/* virtio-net features mapping */
#ifdef CONFIG_VIRTIO_NET
static const qmp_virtio_feature_map_t virtio_net_feature_map[] = {
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
    FEATURE_ENTRY(VIRTIO_NET_F_NOTF_COAL, \
            "VIRTIO_NET_F_NOTF_COAL: Device supports coalescing notifications"),
    FEATURE_ENTRY(VIRTIO_NET_F_GUEST_USO4, \
            "VIRTIO_NET_F_GUEST_USO4: Driver can receive USOv4"),
    FEATURE_ENTRY(VIRTIO_NET_F_GUEST_USO6, \
            "VIRTIO_NET_F_GUEST_USO4: Driver can receive USOv6"),
    FEATURE_ENTRY(VIRTIO_NET_F_HOST_USO, \
            "VIRTIO_NET_F_HOST_USO: Device can receive USO"),
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
#endif

/* virtio-scsi features mapping */
#ifdef CONFIG_VIRTIO_SCSI
static const qmp_virtio_feature_map_t virtio_scsi_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_SCSI_F_INOUT, \
            "VIRTIO_SCSI_F_INOUT: Requests including read and writable data "
            "buffers supported"),
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
#endif

/* virtio/vhost-user-fs features mapping */
#ifdef CONFIG_VHOST_USER_FS
static const qmp_virtio_feature_map_t virtio_fs_feature_map[] = {
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};
#endif

/* virtio/vhost-user-i2c features mapping */
#ifdef CONFIG_VIRTIO_I2C_ADAPTER
static const qmp_virtio_feature_map_t virtio_i2c_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_I2C_F_ZERO_LENGTH_REQUEST, \
            "VIRTIO_I2C_F_ZERO_LEGNTH_REQUEST: Zero length requests supported"),
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};
#endif

/* virtio/vhost-vsock features mapping */
#ifdef CONFIG_VHOST_VSOCK
static const qmp_virtio_feature_map_t virtio_vsock_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_VSOCK_F_SEQPACKET, \
            "VIRTIO_VSOCK_F_SEQPACKET: SOCK_SEQPACKET supported"),
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};
#endif

/* virtio-balloon features mapping */
#ifdef CONFIG_VIRTIO_BALLOON
static const qmp_virtio_feature_map_t virtio_balloon_feature_map[] = {
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
#endif

/* virtio-crypto features mapping */
#ifdef CONFIG_VIRTIO_CRYPTO
static const qmp_virtio_feature_map_t virtio_crypto_feature_map[] = {
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    { -1, "" }
};
#endif

/* virtio-iommu features mapping */
#ifdef CONFIG_VIRTIO_IOMMU
static const qmp_virtio_feature_map_t virtio_iommu_feature_map[] = {
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
#endif

/* virtio-mem features mapping */
#ifdef CONFIG_VIRTIO_MEM
static const qmp_virtio_feature_map_t virtio_mem_feature_map[] = {
#ifndef CONFIG_ACPI
    FEATURE_ENTRY(VIRTIO_MEM_F_ACPI_PXM, \
            "VIRTIO_MEM_F_ACPI_PXM: node_id is an ACPI PXM and is valid"),
#endif /* !CONFIG_ACPI */
    FEATURE_ENTRY(VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE, \
            "VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE: Unplugged memory cannot be "
            "accessed"),
    FEATURE_ENTRY(VIRTIO_MEM_F_PERSISTENT_SUSPEND, \
            "VIRTIO_MEM_F_PERSISTENT_SUSPND: Plugged memory will remain "
            "plugged when suspending+resuming"),
    { -1, "" }
};
#endif

/* virtio-rng features mapping */
#ifdef CONFIG_VIRTIO_RNG
static const qmp_virtio_feature_map_t virtio_rng_feature_map[] = {
    FEATURE_ENTRY(VHOST_F_LOG_ALL, \
            "VHOST_F_LOG_ALL: Logging write descriptors supported"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};
#endif

/* virtio/vhost-gpio features mapping */
#ifdef CONFIG_VHOST_USER_GPIO
static const qmp_virtio_feature_map_t virtio_gpio_feature_map[] = {
    FEATURE_ENTRY(VIRTIO_GPIO_F_IRQ, \
            "VIRTIO_GPIO_F_IRQ: Device supports interrupts on GPIO lines"),
    FEATURE_ENTRY(VHOST_USER_F_PROTOCOL_FEATURES, \
            "VHOST_USER_F_PROTOCOL_FEATURES: Vhost-user protocol features "
            "negotiation supported"),
    { -1, "" }
};
#endif

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

VirtioDeviceStatus *qmp_decode_status(uint8_t bitmap)
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

VhostDeviceProtocols *qmp_decode_protocols(uint64_t bitmap)
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

VirtioDeviceFeatures *qmp_decode_features(uint16_t device_id, uint64_t bitmap)
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
#ifdef CONFIG_VHOST_USER_GPIO
    case VIRTIO_ID_GPIO:
        features->dev_features =
            CONVERT_FEATURES(strList, virtio_gpio_feature_map, 0, bitmap);
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

static int query_dev_child(Object *child, void *opaque)
{
    VirtioInfoList **vdevs = opaque;
    Object *dev = object_dynamic_cast(child, TYPE_VIRTIO_DEVICE);
    if (dev != NULL && DEVICE(dev)->realized) {
        VirtIODevice *vdev = VIRTIO_DEVICE(dev);
        VirtioInfo *info = g_new(VirtioInfo, 1);

        /* Get canonical path & name of device */
        info->path = object_get_canonical_path(dev);
        info->name = g_strdup(vdev->name);
        QAPI_LIST_PREPEND(*vdevs, info);
    }
    return 0;
}

VirtioInfoList *qmp_x_query_virtio(Error **errp)
{
    VirtioInfoList *vdevs = NULL;

    /* Query the QOM composition tree recursively for virtio devices */
    object_child_foreach_recursive(object_get_root(), query_dev_child, &vdevs);
    if (vdevs == NULL) {
        error_setg(errp, "No virtio devices found");
    }
    return vdevs;
}

VirtIODevice *qmp_find_virtio_device(const char *path)
{
    /* Verify the canonical path is a realized virtio device */
    Object *dev = object_dynamic_cast(object_resolve_path(path, NULL),
                                      TYPE_VIRTIO_DEVICE);
    if (!dev || !DEVICE(dev)->realized) {
        return NULL;
    }
    return VIRTIO_DEVICE(dev);
}

VirtioStatus *qmp_x_query_virtio_status(const char *path, Error **errp)
{
    VirtIODevice *vdev;
    VirtioStatus *status;

    vdev = qmp_find_virtio_device(path);
    if (vdev == NULL) {
        error_setg(errp, "Path %s is not a realized VirtIODevice", path);
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

    vdev = qmp_find_virtio_device(path);
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

/*
 * QEMU Bluetooth HCI helpers.
 *
 * Copyright (C) 2007 OpenMoko, Inc.
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * Useful definitions taken from BlueZ project's headers.
 * Copyright (C) 2000-2001  Qualcomm Incorporated
 * Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
 * Copyright (C) 2002-2006  Marcel Holtmann <marcel@holtmann.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* BD Address */
typedef struct {
    uint8_t b[6];
} __attribute__((packed)) bdaddr_t;

#define BDADDR_ANY	(&(bdaddr_t) {{0, 0, 0, 0, 0, 0}})
#define BDADDR_ALL	(&(bdaddr_t) {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}})
#define BDADDR_LOCAL	(&(bdaddr_t) {{0, 0, 0, 0xff, 0xff, 0xff}})

/* Copy, swap, convert BD Address */
static inline int bacmp(const bdaddr_t *ba1, const bdaddr_t *ba2)
{
    return memcmp(ba1, ba2, sizeof(bdaddr_t));
}
static inline void bacpy(bdaddr_t *dst, const bdaddr_t *src)
{
    memcpy(dst, src, sizeof(bdaddr_t));
}

#define BAINIT(orig)	{ .b = {		\
    (orig)->b[0], (orig)->b[1], (orig)->b[2],	\
    (orig)->b[3], (orig)->b[4], (orig)->b[5],	\
}, }

/* The twisted structures of a bluetooth environment */
struct bt_device_s;
struct bt_scatternet_s;
struct bt_piconet_s;
struct bt_link_s;

struct bt_scatternet_s {
    struct bt_device_s *slave;
};

struct bt_link_s {
    struct bt_device_s *slave, *host;
    uint16_t handle;		/* Master (host) side handle */
    uint16_t acl_interval;
    enum {
        acl_active,
        acl_hold,
        acl_sniff,
        acl_parked,
    } acl_mode;
};

struct bt_device_s {
    int lt_addr;
    bdaddr_t bd_addr;
    int mtu;
    int setup;
    struct bt_scatternet_s *net;

    uint8_t key[16];
    int key_present;
    uint8_t class[3];

    uint8_t reject_reason;

    uint64_t lmp_caps;
    const char *lmp_name;
    void (*lmp_connection_request)(struct bt_link_s *link);
    void (*lmp_connection_complete)(struct bt_link_s *link);
    void (*lmp_disconnect_master)(struct bt_link_s *link);
    void (*lmp_disconnect_slave)(struct bt_link_s *link);
    void (*lmp_acl_data)(struct bt_link_s *link, const uint8_t *data,
                    int start, int len);
    void (*lmp_acl_resp)(struct bt_link_s *link, const uint8_t *data,
                    int start, int len);
    void (*lmp_mode_change)(struct bt_link_s *link);

    void (*handle_destroy)(struct bt_device_s *device);
    struct bt_device_s *next;	/* Next in the piconet/scatternet */

    int inquiry_scan;
    int page_scan;

    uint16_t clkoff;	/* Note: Always little-endian */
};

/* bt.c */
void bt_device_init(struct bt_device_s *dev, struct bt_scatternet_s *net);
void bt_device_done(struct bt_device_s *dev);

/* bt-hci.c */
struct HCIInfo *bt_new_hci(struct bt_scatternet_s *net);

/* bt-vhci.c */
void bt_vhci_init(struct HCIInfo *info);

/* bt-hci-csr.c */
enum {
    csrhci_pin_reset,
    csrhci_pin_wakeup,
    __csrhci_pins,
};
qemu_irq *csrhci_pins_get(CharDriverState *chr);
CharDriverState *uart_hci_init(qemu_irq wakeup);

/* bt-l2cap.c */
struct bt_l2cap_device_s;
struct bt_l2cap_conn_params_s;
struct bt_l2cap_psm_s;
void bt_l2cap_device_init(struct bt_l2cap_device_s *dev,
                struct bt_scatternet_s *net);
void bt_l2cap_device_done(struct bt_l2cap_device_s *dev);
void bt_l2cap_psm_register(struct bt_l2cap_device_s *dev, int psm,
                int min_mtu, int (*new_channel)(struct bt_l2cap_device_s *dev,
                        struct bt_l2cap_conn_params_s *params));

struct bt_l2cap_device_s {
    struct bt_device_s device;
    struct bt_l2cap_psm_s *first_psm;
};

struct bt_l2cap_conn_params_s {
    /* Input */
    uint8_t *(*sdu_out)(struct bt_l2cap_conn_params_s *chan, int len);
    void (*sdu_submit)(struct bt_l2cap_conn_params_s *chan);
    int remote_mtu;
    /* Output */
    void *opaque;
    void (*sdu_in)(void *opaque, const uint8_t *data, int len);
    void (*close)(void *opaque);
};

enum bt_l2cap_psm_predef {
    BT_PSM_SDP		= 0x0001,
    BT_PSM_RFCOMM	= 0x0003,
    BT_PSM_TELEPHONY	= 0x0005,
    BT_PSM_TCS		= 0x0007,
    BT_PSM_BNEP		= 0x000f,
    BT_PSM_HID_CTRL	= 0x0011,
    BT_PSM_HID_INTR	= 0x0013,
    BT_PSM_UPNP		= 0x0015,
    BT_PSM_AVCTP	= 0x0017,
    BT_PSM_AVDTP	= 0x0019,
};

/* bt-sdp.c */
void bt_l2cap_sdp_init(struct bt_l2cap_device_s *dev);

/* bt-hid.c */
struct bt_device_s *bt_mouse_init(struct bt_scatternet_s *net);
struct bt_device_s *bt_tablet_init(struct bt_scatternet_s *net);
struct bt_device_s *bt_keyboard_init(struct bt_scatternet_s *net);

/* Link Management Protocol layer defines */

#define LLID_ACLU_CONT		0x1
#define LLID_ACLU_START		0x2
#define LLID_ACLC		0x3

enum lmp_pdu_type {
    LMP_NAME_REQ		= 0x0001,
    LMP_NAME_RES		= 0x0002,
    LMP_ACCEPTED		= 0x0003,
    LMP_NOT_ACCEPTED		= 0x0004,
    LMP_CLKOFFSET_REQ		= 0x0005,
    LMP_CLKOFFSET_RES		= 0x0006,
    LMP_DETACH			= 0x0007,
    LMP_IN_RAND			= 0x0008,
    LMP_COMB_KEY		= 0x0009,
    LMP_UNIT_KEY		= 0x000a,
    LMP_AU_RAND			= 0x000b,
    LMP_SRES			= 0x000c,
    LMP_TEMP_RAND		= 0x000d,
    LMP_TEMP_KEY		= 0x000e,
    LMP_CRYPT_MODE_REQ		= 0x000f,
    LMP_CRYPT_KEY_SIZE_REQ	= 0x0010,
    LMP_START_ENCRYPT_REQ	= 0x0011,
    LMP_STOP_ENCRYPT_REQ	= 0x0012,
    LMP_SWITCH_REQ		= 0x0013,
    LMP_HOLD			= 0x0014,
    LMP_HOLD_REQ		= 0x0015,
    LMP_SNIFF_REQ		= 0x0017,
    LMP_UNSNIFF_REQ		= 0x0018,
    LMP_LMP_PARK_REQ		= 0x0019,
    LMP_SET_BCAST_SCAN_WND	= 0x001b,
    LMP_MODIFY_BEACON		= 0x001c,
    LMP_UNPARK_BD_ADDR_REQ	= 0x001d,
    LMP_UNPARK_PM_ADDR_REQ	= 0x001e,
    LMP_INCR_POWER_REQ		= 0x001f,
    LMP_DECR_POWER_REQ		= 0x0020,
    LMP_MAX_POWER		= 0x0021,
    LMP_MIN_POWER		= 0x0022,
    LMP_AUTO_RATE		= 0x0023,
    LMP_PREFERRED_RATE		= 0x0024,
    LMP_VERSION_REQ		= 0x0025,
    LMP_VERSION_RES		= 0x0026,
    LMP_FEATURES_REQ		= 0x0027,
    LMP_FEATURES_RES		= 0x0028,
    LMP_QUALITY_OF_SERVICE	= 0x0029,
    LMP_QOS_REQ			= 0x002a,
    LMP_RM_SCO_LINK_REQ		= 0x002b,
    LMP_SCO_LINK_REQ		= 0x002c,
    LMP_MAX_SLOT		= 0x002d,
    LMP_MAX_SLOT_REQ		= 0x002e,
    LMP_TIMING_ACCURACY_REQ	= 0x002f,
    LMP_TIMING_ACCURACY_RES	= 0x0030,
    LMP_SETUP_COMPLETE		= 0x0031,
    LMP_USE_SEMIPERM_KEY	= 0x0032,
    LMP_HOST_CONNECTION_REQ	= 0x0033,
    LMP_SLOT_OFFSET		= 0x0034,
    LMP_PAGE_MODE_REQ		= 0x0035,
    LMP_PAGE_SCAN_MODE_REQ	= 0x0036,
    LMP_SUPERVISION_TIMEOUT	= 0x0037,
    LMP_TEST_ACTIVATE		= 0x0038,
    LMP_TEST_CONTROL		= 0x0039,
    LMP_CRYPT_KEY_MASK_REQ	= 0x003a,
    LMP_CRYPT_KEY_MASK_RES	= 0x003b,
    LMP_SET_AFH			= 0x003c,
    LMP_ACCEPTED_EXT		= 0x7f01,
    LMP_NOT_ACCEPTED_EXT	= 0x7f02,
    LMP_FEATURES_REQ_EXT	= 0x7f03,
    LMP_FEATURES_RES_EXT	= 0x7f04,
    LMP_PACKET_TYPE_TBL_REQ	= 0x7f0b,
    LMP_ESCO_LINK_REQ		= 0x7f0c,
    LMP_RM_ESCO_LINK_REQ	= 0x7f0d,
    LMP_CHANNEL_CLASS_REQ	= 0x7f10,
    LMP_CHANNEL_CLASS		= 0x7f11,
};

/* Host Controller Interface layer defines */

enum hci_packet_type {
    HCI_COMMAND_PKT		= 0x01,
    HCI_ACLDATA_PKT		= 0x02,
    HCI_SCODATA_PKT		= 0x03,
    HCI_EVENT_PKT		= 0x04,
    HCI_VENDOR_PKT		= 0xff,
};

enum bt_packet_type {
    HCI_2DH1	= 1 << 1,
    HCI_3DH1	= 1 << 2,
    HCI_DM1	= 1 << 3,
    HCI_DH1	= 1 << 4,
    HCI_2DH3	= 1 << 8,
    HCI_3DH3	= 1 << 9,
    HCI_DM3	= 1 << 10,
    HCI_DH3	= 1 << 11,
    HCI_2DH5	= 1 << 12,
    HCI_3DH5	= 1 << 13,
    HCI_DM5	= 1 << 14,
    HCI_DH5	= 1 << 15,
};

enum sco_packet_type {
    HCI_HV1	= 1 << 5,
    HCI_HV2	= 1 << 6,
    HCI_HV3	= 1 << 7,
};

enum ev_packet_type {
    HCI_EV3	= 1 << 3,
    HCI_EV4	= 1 << 4,
    HCI_EV5	= 1 << 5,
    HCI_2EV3	= 1 << 6,
    HCI_3EV3	= 1 << 7,
    HCI_2EV5	= 1 << 8,
    HCI_3EV5	= 1 << 9,
};

enum hci_error_code {
    HCI_SUCCESS				= 0x00,
    HCI_UNKNOWN_COMMAND			= 0x01,
    HCI_NO_CONNECTION			= 0x02,
    HCI_HARDWARE_FAILURE		= 0x03,
    HCI_PAGE_TIMEOUT			= 0x04,
    HCI_AUTHENTICATION_FAILURE		= 0x05,
    HCI_PIN_OR_KEY_MISSING		= 0x06,
    HCI_MEMORY_FULL			= 0x07,
    HCI_CONNECTION_TIMEOUT		= 0x08,
    HCI_MAX_NUMBER_OF_CONNECTIONS	= 0x09,
    HCI_MAX_NUMBER_OF_SCO_CONNECTIONS	= 0x0a,
    HCI_ACL_CONNECTION_EXISTS		= 0x0b,
    HCI_COMMAND_DISALLOWED		= 0x0c,
    HCI_REJECTED_LIMITED_RESOURCES	= 0x0d,
    HCI_REJECTED_SECURITY		= 0x0e,
    HCI_REJECTED_PERSONAL		= 0x0f,
    HCI_HOST_TIMEOUT			= 0x10,
    HCI_UNSUPPORTED_FEATURE		= 0x11,
    HCI_INVALID_PARAMETERS		= 0x12,
    HCI_OE_USER_ENDED_CONNECTION	= 0x13,
    HCI_OE_LOW_RESOURCES		= 0x14,
    HCI_OE_POWER_OFF			= 0x15,
    HCI_CONNECTION_TERMINATED		= 0x16,
    HCI_REPEATED_ATTEMPTS		= 0x17,
    HCI_PAIRING_NOT_ALLOWED		= 0x18,
    HCI_UNKNOWN_LMP_PDU			= 0x19,
    HCI_UNSUPPORTED_REMOTE_FEATURE	= 0x1a,
    HCI_SCO_OFFSET_REJECTED		= 0x1b,
    HCI_SCO_INTERVAL_REJECTED		= 0x1c,
    HCI_AIR_MODE_REJECTED		= 0x1d,
    HCI_INVALID_LMP_PARAMETERS		= 0x1e,
    HCI_UNSPECIFIED_ERROR		= 0x1f,
    HCI_UNSUPPORTED_LMP_PARAMETER_VALUE	= 0x20,
    HCI_ROLE_CHANGE_NOT_ALLOWED		= 0x21,
    HCI_LMP_RESPONSE_TIMEOUT		= 0x22,
    HCI_LMP_ERROR_TRANSACTION_COLLISION	= 0x23,
    HCI_LMP_PDU_NOT_ALLOWED		= 0x24,
    HCI_ENCRYPTION_MODE_NOT_ACCEPTED	= 0x25,
    HCI_UNIT_LINK_KEY_USED		= 0x26,
    HCI_QOS_NOT_SUPPORTED		= 0x27,
    HCI_INSTANT_PASSED			= 0x28,
    HCI_PAIRING_NOT_SUPPORTED		= 0x29,
    HCI_TRANSACTION_COLLISION		= 0x2a,
    HCI_QOS_UNACCEPTABLE_PARAMETER	= 0x2c,
    HCI_QOS_REJECTED			= 0x2d,
    HCI_CLASSIFICATION_NOT_SUPPORTED	= 0x2e,
    HCI_INSUFFICIENT_SECURITY		= 0x2f,
    HCI_PARAMETER_OUT_OF_RANGE		= 0x30,
    HCI_ROLE_SWITCH_PENDING		= 0x32,
    HCI_SLOT_VIOLATION			= 0x34,
    HCI_ROLE_SWITCH_FAILED		= 0x35,
};

enum acl_flag_bits {
    ACL_CONT		= 1 << 0,
    ACL_START		= 1 << 1,
    ACL_ACTIVE_BCAST	= 1 << 2,
    ACL_PICO_BCAST	= 1 << 3,
};

enum baseband_link_type {
    SCO_LINK		= 0x00,
    ACL_LINK		= 0x01,
};

enum lmp_feature_bits0 {
    LMP_3SLOT		= 1 << 0,
    LMP_5SLOT		= 1 << 1,
    LMP_ENCRYPT		= 1 << 2,
    LMP_SOFFSET		= 1 << 3,
    LMP_TACCURACY	= 1 << 4,
    LMP_RSWITCH		= 1 << 5,
    LMP_HOLD_MODE	= 1 << 6,
    LMP_SNIFF_MODE	= 1 << 7,
};

enum lmp_feature_bits1 {
    LMP_PARK		= 1 << 0,
    LMP_RSSI		= 1 << 1,
    LMP_QUALITY		= 1 << 2,
    LMP_SCO		= 1 << 3,
    LMP_HV2		= 1 << 4,
    LMP_HV3		= 1 << 5,
    LMP_ULAW		= 1 << 6,
    LMP_ALAW		= 1 << 7,
};

enum lmp_feature_bits2 {
    LMP_CVSD		= 1 << 0,
    LMP_PSCHEME		= 1 << 1,
    LMP_PCONTROL	= 1 << 2,
    LMP_TRSP_SCO	= 1 << 3,
    LMP_BCAST_ENC	= 1 << 7,
};

enum lmp_feature_bits3 {
    LMP_EDR_ACL_2M	= 1 << 1,
    LMP_EDR_ACL_3M	= 1 << 2,
    LMP_ENH_ISCAN	= 1 << 3,
    LMP_ILACE_ISCAN	= 1 << 4,
    LMP_ILACE_PSCAN	= 1 << 5,
    LMP_RSSI_INQ	= 1 << 6,
    LMP_ESCO		= 1 << 7,
};

enum lmp_feature_bits4 {
    LMP_EV4		= 1 << 0,
    LMP_EV5		= 1 << 1,
    LMP_AFH_CAP_SLV	= 1 << 3,
    LMP_AFH_CLS_SLV	= 1 << 4,
    LMP_EDR_3SLOT	= 1 << 7,
};

enum lmp_feature_bits5 {
    LMP_EDR_5SLOT	= 1 << 0,
    LMP_SNIFF_SUBR	= 1 << 1,
    LMP_AFH_CAP_MST	= 1 << 3,
    LMP_AFH_CLS_MST	= 1 << 4,
    LMP_EDR_ESCO_2M	= 1 << 5,
    LMP_EDR_ESCO_3M	= 1 << 6,
    LMP_EDR_3S_ESCO	= 1 << 7,
};

enum lmp_feature_bits6 {
    LMP_EXT_INQ		= 1 << 0,
};

enum lmp_feature_bits7 {
    LMP_EXT_FEAT	= 1 << 7,
};

enum hci_link_policy {
    HCI_LP_RSWITCH	= 1 << 0,
    HCI_LP_HOLD		= 1 << 1,
    HCI_LP_SNIFF	= 1 << 2,
    HCI_LP_PARK		= 1 << 3,
};

enum hci_link_mode {
    HCI_LM_ACCEPT	= 1 << 15,
    HCI_LM_MASTER	= 1 << 0,
    HCI_LM_AUTH		= 1 << 1,
    HCI_LM_ENCRYPT	= 1 << 2,
    HCI_LM_TRUSTED	= 1 << 3,
    HCI_LM_RELIABLE	= 1 << 4,
    HCI_LM_SECURE	= 1 << 5,
};

/* HCI Commands */

/* Link Control */
#define OGF_LINK_CTL		0x01

#define OCF_INQUIRY			0x0001
typedef struct {
    uint8_t	lap[3];
    uint8_t	length;		/* 1.28s units */
    uint8_t	num_rsp;
} __attribute__ ((packed)) inquiry_cp;
#define INQUIRY_CP_SIZE 5

typedef struct {
    uint8_t		status;
    bdaddr_t	bdaddr;
} __attribute__ ((packed)) status_bdaddr_rp;
#define STATUS_BDADDR_RP_SIZE 7

#define OCF_INQUIRY_CANCEL		0x0002

#define OCF_PERIODIC_INQUIRY		0x0003
typedef struct {
    uint16_t	max_period;	/* 1.28s units */
    uint16_t	min_period;	/* 1.28s units */
    uint8_t	lap[3];
    uint8_t	length;		/* 1.28s units */
    uint8_t	num_rsp;
} __attribute__ ((packed)) periodic_inquiry_cp;
#define PERIODIC_INQUIRY_CP_SIZE 9

#define OCF_EXIT_PERIODIC_INQUIRY	0x0004

#define OCF_CREATE_CONN			0x0005
typedef struct {
    bdaddr_t	bdaddr;
    uint16_t	pkt_type;
    uint8_t	pscan_rep_mode;
    uint8_t	pscan_mode;
    uint16_t	clock_offset;
    uint8_t	role_switch;
} __attribute__ ((packed)) create_conn_cp;
#define CREATE_CONN_CP_SIZE 13

#define OCF_DISCONNECT			0x0006
typedef struct {
    uint16_t	handle;
    uint8_t	reason;
} __attribute__ ((packed)) disconnect_cp;
#define DISCONNECT_CP_SIZE 3

#define OCF_ADD_SCO			0x0007
typedef struct {
    uint16_t	handle;
    uint16_t	pkt_type;
} __attribute__ ((packed)) add_sco_cp;
#define ADD_SCO_CP_SIZE 4

#define OCF_CREATE_CONN_CANCEL		0x0008
typedef struct {
    uint8_t	status;
    bdaddr_t	bdaddr;
} __attribute__ ((packed)) create_conn_cancel_cp;
#define CREATE_CONN_CANCEL_CP_SIZE 6

typedef struct {
    uint8_t	status;
    bdaddr_t	bdaddr;
} __attribute__ ((packed)) create_conn_cancel_rp;
#define CREATE_CONN_CANCEL_RP_SIZE 7

#define OCF_ACCEPT_CONN_REQ		0x0009
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	role;
} __attribute__ ((packed)) accept_conn_req_cp;
#define ACCEPT_CONN_REQ_CP_SIZE	7

#define OCF_REJECT_CONN_REQ		0x000A
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	reason;
} __attribute__ ((packed)) reject_conn_req_cp;
#define REJECT_CONN_REQ_CP_SIZE	7

#define OCF_LINK_KEY_REPLY		0x000B
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	link_key[16];
} __attribute__ ((packed)) link_key_reply_cp;
#define LINK_KEY_REPLY_CP_SIZE 22

#define OCF_LINK_KEY_NEG_REPLY		0x000C

#define OCF_PIN_CODE_REPLY		0x000D
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	pin_len;
    uint8_t	pin_code[16];
} __attribute__ ((packed)) pin_code_reply_cp;
#define PIN_CODE_REPLY_CP_SIZE 23

#define OCF_PIN_CODE_NEG_REPLY		0x000E

#define OCF_SET_CONN_PTYPE		0x000F
typedef struct {
    uint16_t	 handle;
    uint16_t	 pkt_type;
} __attribute__ ((packed)) set_conn_ptype_cp;
#define SET_CONN_PTYPE_CP_SIZE 4

#define OCF_AUTH_REQUESTED		0x0011
typedef struct {
    uint16_t	 handle;
} __attribute__ ((packed)) auth_requested_cp;
#define AUTH_REQUESTED_CP_SIZE 2

#define OCF_SET_CONN_ENCRYPT		0x0013
typedef struct {
    uint16_t	handle;
    uint8_t	encrypt;
} __attribute__ ((packed)) set_conn_encrypt_cp;
#define SET_CONN_ENCRYPT_CP_SIZE 3

#define OCF_CHANGE_CONN_LINK_KEY	0x0015
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) change_conn_link_key_cp;
#define CHANGE_CONN_LINK_KEY_CP_SIZE 2

#define OCF_MASTER_LINK_KEY		0x0017
typedef struct {
    uint8_t	key_flag;
} __attribute__ ((packed)) master_link_key_cp;
#define MASTER_LINK_KEY_CP_SIZE 1

#define OCF_REMOTE_NAME_REQ		0x0019
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	pscan_rep_mode;
    uint8_t	pscan_mode;
    uint16_t	clock_offset;
} __attribute__ ((packed)) remote_name_req_cp;
#define REMOTE_NAME_REQ_CP_SIZE 10

#define OCF_REMOTE_NAME_REQ_CANCEL	0x001A
typedef struct {
    bdaddr_t	bdaddr;
} __attribute__ ((packed)) remote_name_req_cancel_cp;
#define REMOTE_NAME_REQ_CANCEL_CP_SIZE 6

typedef struct {
    uint8_t		status;
    bdaddr_t	bdaddr;
} __attribute__ ((packed)) remote_name_req_cancel_rp;
#define REMOTE_NAME_REQ_CANCEL_RP_SIZE 7

#define OCF_READ_REMOTE_FEATURES	0x001B
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) read_remote_features_cp;
#define READ_REMOTE_FEATURES_CP_SIZE 2

#define OCF_READ_REMOTE_EXT_FEATURES	0x001C
typedef struct {
    uint16_t	handle;
    uint8_t	page_num;
} __attribute__ ((packed)) read_remote_ext_features_cp;
#define READ_REMOTE_EXT_FEATURES_CP_SIZE 3

#define OCF_READ_REMOTE_VERSION		0x001D
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) read_remote_version_cp;
#define READ_REMOTE_VERSION_CP_SIZE 2

#define OCF_READ_CLOCK_OFFSET		0x001F
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) read_clock_offset_cp;
#define READ_CLOCK_OFFSET_CP_SIZE 2

#define OCF_READ_LMP_HANDLE		0x0020
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) read_lmp_handle_cp;
#define READ_LMP_HANDLE_CP_SIZE 2

typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	lmp_handle;
    uint32_t	reserved;
} __attribute__ ((packed)) read_lmp_handle_rp;
#define READ_LMP_HANDLE_RP_SIZE 8

#define OCF_SETUP_SYNC_CONN		0x0028
typedef struct {
    uint16_t	handle;
    uint32_t	tx_bandwith;
    uint32_t	rx_bandwith;
    uint16_t	max_latency;
    uint16_t	voice_setting;
    uint8_t	retrans_effort;
    uint16_t	pkt_type;
} __attribute__ ((packed)) setup_sync_conn_cp;
#define SETUP_SYNC_CONN_CP_SIZE 17

#define OCF_ACCEPT_SYNC_CONN_REQ	0x0029
typedef struct {
    bdaddr_t	bdaddr;
    uint32_t	tx_bandwith;
    uint32_t	rx_bandwith;
    uint16_t	max_latency;
    uint16_t	voice_setting;
    uint8_t	retrans_effort;
    uint16_t	pkt_type;
} __attribute__ ((packed)) accept_sync_conn_req_cp;
#define ACCEPT_SYNC_CONN_REQ_CP_SIZE 21

#define OCF_REJECT_SYNC_CONN_REQ	0x002A
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	reason;
} __attribute__ ((packed)) reject_sync_conn_req_cp;
#define REJECT_SYNC_CONN_REQ_CP_SIZE 7

/* Link Policy */
#define OGF_LINK_POLICY		0x02

#define OCF_HOLD_MODE			0x0001
typedef struct {
    uint16_t	handle;
    uint16_t	max_interval;
    uint16_t	min_interval;
} __attribute__ ((packed)) hold_mode_cp;
#define HOLD_MODE_CP_SIZE 6

#define OCF_SNIFF_MODE			0x0003
typedef struct {
    uint16_t	handle;
    uint16_t	max_interval;
    uint16_t	min_interval;
    uint16_t	attempt;
    uint16_t	timeout;
} __attribute__ ((packed)) sniff_mode_cp;
#define SNIFF_MODE_CP_SIZE 10

#define OCF_EXIT_SNIFF_MODE		0x0004
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) exit_sniff_mode_cp;
#define EXIT_SNIFF_MODE_CP_SIZE 2

#define OCF_PARK_MODE			0x0005
typedef struct {
    uint16_t	handle;
    uint16_t	max_interval;
    uint16_t	min_interval;
} __attribute__ ((packed)) park_mode_cp;
#define PARK_MODE_CP_SIZE 6

#define OCF_EXIT_PARK_MODE		0x0006
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) exit_park_mode_cp;
#define EXIT_PARK_MODE_CP_SIZE 2

#define OCF_QOS_SETUP			0x0007
typedef struct {
    uint8_t	service_type;		/* 1 = best effort */
    uint32_t	token_rate;		/* Byte per seconds */
    uint32_t	peak_bandwidth;		/* Byte per seconds */
    uint32_t	latency;		/* Microseconds */
    uint32_t	delay_variation;	/* Microseconds */
} __attribute__ ((packed)) hci_qos;
#define HCI_QOS_CP_SIZE 17
typedef struct {
    uint16_t 	handle;
    uint8_t 	flags;			/* Reserved */
    hci_qos 	qos;
} __attribute__ ((packed)) qos_setup_cp;
#define QOS_SETUP_CP_SIZE (3 + HCI_QOS_CP_SIZE)

#define OCF_ROLE_DISCOVERY		0x0009
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) role_discovery_cp;
#define ROLE_DISCOVERY_CP_SIZE 2
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	role;
} __attribute__ ((packed)) role_discovery_rp;
#define ROLE_DISCOVERY_RP_SIZE 4

#define OCF_SWITCH_ROLE			0x000B
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	role;
} __attribute__ ((packed)) switch_role_cp;
#define SWITCH_ROLE_CP_SIZE 7

#define OCF_READ_LINK_POLICY		0x000C
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) read_link_policy_cp;
#define READ_LINK_POLICY_CP_SIZE 2
typedef struct {
    uint8_t 	status;
    uint16_t	handle;
    uint16_t	policy;
} __attribute__ ((packed)) read_link_policy_rp;
#define READ_LINK_POLICY_RP_SIZE 5

#define OCF_WRITE_LINK_POLICY		0x000D
typedef struct {
    uint16_t	handle;
    uint16_t	policy;
} __attribute__ ((packed)) write_link_policy_cp;
#define WRITE_LINK_POLICY_CP_SIZE 4
typedef struct {
    uint8_t 	status;
    uint16_t	handle;
} __attribute__ ((packed)) write_link_policy_rp;
#define WRITE_LINK_POLICY_RP_SIZE 3

#define OCF_READ_DEFAULT_LINK_POLICY	0x000E

#define OCF_WRITE_DEFAULT_LINK_POLICY	0x000F

#define OCF_FLOW_SPECIFICATION		0x0010

#define OCF_SNIFF_SUBRATE		0x0011
typedef struct {
    uint16_t	handle;
    uint16_t	max_remote_latency;
    uint16_t	max_local_latency;
    uint16_t	min_remote_timeout;
    uint16_t	min_local_timeout;
} __attribute__ ((packed)) sniff_subrate_cp;
#define SNIFF_SUBRATE_CP_SIZE 10

/* Host Controller and Baseband */
#define OGF_HOST_CTL		0x03

#define OCF_SET_EVENT_MASK		0x0001
typedef struct {
    uint8_t	mask[8];
} __attribute__ ((packed)) set_event_mask_cp;
#define SET_EVENT_MASK_CP_SIZE 8

#define OCF_RESET			0x0003

#define OCF_SET_EVENT_FLT		0x0005
typedef struct {
    uint8_t	flt_type;
    uint8_t	cond_type;
    uint8_t	condition[0];
} __attribute__ ((packed)) set_event_flt_cp;
#define SET_EVENT_FLT_CP_SIZE 2

enum bt_filter_type {
    FLT_CLEAR_ALL		= 0x00,
    FLT_INQ_RESULT		= 0x01,
    FLT_CONN_SETUP		= 0x02,
};
enum inq_result_cond_type {
    INQ_RESULT_RETURN_ALL	= 0x00,
    INQ_RESULT_RETURN_CLASS	= 0x01,
    INQ_RESULT_RETURN_BDADDR	= 0x02,
};
enum conn_setup_cond_type {
    CONN_SETUP_ALLOW_ALL	= 0x00,
    CONN_SETUP_ALLOW_CLASS	= 0x01,
    CONN_SETUP_ALLOW_BDADDR	= 0x02,
};
enum conn_setup_cond {
    CONN_SETUP_AUTO_OFF		= 0x01,
    CONN_SETUP_AUTO_ON		= 0x02,
};

#define OCF_FLUSH			0x0008
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) flush_cp;
#define FLUSH_CP_SIZE 2

typedef struct {
    uint8_t	status;
    uint16_t	handle;
} __attribute__ ((packed)) flush_rp;
#define FLUSH_RP_SIZE 3

#define OCF_READ_PIN_TYPE		0x0009
typedef struct {
    uint8_t	status;
    uint8_t	pin_type;
} __attribute__ ((packed)) read_pin_type_rp;
#define READ_PIN_TYPE_RP_SIZE 2

#define OCF_WRITE_PIN_TYPE		0x000A
typedef struct {
    uint8_t	pin_type;
} __attribute__ ((packed)) write_pin_type_cp;
#define WRITE_PIN_TYPE_CP_SIZE 1

#define OCF_CREATE_NEW_UNIT_KEY		0x000B

#define OCF_READ_STORED_LINK_KEY	0x000D
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	read_all;
} __attribute__ ((packed)) read_stored_link_key_cp;
#define READ_STORED_LINK_KEY_CP_SIZE 7
typedef struct {
    uint8_t	status;
    uint16_t	max_keys;
    uint16_t	num_keys;
} __attribute__ ((packed)) read_stored_link_key_rp;
#define READ_STORED_LINK_KEY_RP_SIZE 5

#define OCF_WRITE_STORED_LINK_KEY	0x0011
typedef struct {
    uint8_t	num_keys;
    /* variable length part */
} __attribute__ ((packed)) write_stored_link_key_cp;
#define WRITE_STORED_LINK_KEY_CP_SIZE 1
typedef struct {
    uint8_t	status;
    uint8_t	num_keys;
} __attribute__ ((packed)) write_stored_link_key_rp;
#define READ_WRITE_LINK_KEY_RP_SIZE 2

#define OCF_DELETE_STORED_LINK_KEY	0x0012
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	delete_all;
} __attribute__ ((packed)) delete_stored_link_key_cp;
#define DELETE_STORED_LINK_KEY_CP_SIZE 7
typedef struct {
    uint8_t	status;
    uint16_t	num_keys;
} __attribute__ ((packed)) delete_stored_link_key_rp;
#define DELETE_STORED_LINK_KEY_RP_SIZE 3

#define OCF_CHANGE_LOCAL_NAME		0x0013
typedef struct {
    char	name[248];
} __attribute__ ((packed)) change_local_name_cp;
#define CHANGE_LOCAL_NAME_CP_SIZE 248 

#define OCF_READ_LOCAL_NAME		0x0014
typedef struct {
    uint8_t	status;
    char	name[248];
} __attribute__ ((packed)) read_local_name_rp;
#define READ_LOCAL_NAME_RP_SIZE 249 

#define OCF_READ_CONN_ACCEPT_TIMEOUT	0x0015
typedef struct {
    uint8_t	status;
    uint16_t	timeout;
} __attribute__ ((packed)) read_conn_accept_timeout_rp;
#define READ_CONN_ACCEPT_TIMEOUT_RP_SIZE 3

#define OCF_WRITE_CONN_ACCEPT_TIMEOUT	0x0016
typedef struct {
    uint16_t	timeout;
} __attribute__ ((packed)) write_conn_accept_timeout_cp;
#define WRITE_CONN_ACCEPT_TIMEOUT_CP_SIZE 2

#define OCF_READ_PAGE_TIMEOUT		0x0017
typedef struct {
    uint8_t	status;
    uint16_t	timeout;
} __attribute__ ((packed)) read_page_timeout_rp;
#define READ_PAGE_TIMEOUT_RP_SIZE 3

#define OCF_WRITE_PAGE_TIMEOUT		0x0018
typedef struct {
    uint16_t	timeout;
} __attribute__ ((packed)) write_page_timeout_cp;
#define WRITE_PAGE_TIMEOUT_CP_SIZE 2

#define OCF_READ_SCAN_ENABLE		0x0019
typedef struct {
    uint8_t	status;
    uint8_t	enable;
} __attribute__ ((packed)) read_scan_enable_rp;
#define READ_SCAN_ENABLE_RP_SIZE 2

#define OCF_WRITE_SCAN_ENABLE		0x001A
typedef struct {
    uint8_t	scan_enable;
} __attribute__ ((packed)) write_scan_enable_cp;
#define WRITE_SCAN_ENABLE_CP_SIZE 1

enum scan_enable_bits {
    SCAN_DISABLED		= 0,
    SCAN_INQUIRY		= 1 << 0,
    SCAN_PAGE			= 1 << 1,
};

#define OCF_READ_PAGE_ACTIVITY		0x001B
typedef struct {
    uint8_t	status;
    uint16_t	interval;
    uint16_t	window;
} __attribute__ ((packed)) read_page_activity_rp;
#define READ_PAGE_ACTIVITY_RP_SIZE 5

#define OCF_WRITE_PAGE_ACTIVITY		0x001C
typedef struct {
    uint16_t	interval;
    uint16_t	window;
} __attribute__ ((packed)) write_page_activity_cp;
#define WRITE_PAGE_ACTIVITY_CP_SIZE 4

#define OCF_READ_INQ_ACTIVITY		0x001D
typedef struct {
    uint8_t	status;
    uint16_t	interval;
    uint16_t	window;
} __attribute__ ((packed)) read_inq_activity_rp;
#define READ_INQ_ACTIVITY_RP_SIZE 5

#define OCF_WRITE_INQ_ACTIVITY		0x001E
typedef struct {
    uint16_t	interval;
    uint16_t	window;
} __attribute__ ((packed)) write_inq_activity_cp;
#define WRITE_INQ_ACTIVITY_CP_SIZE 4

#define OCF_READ_AUTH_ENABLE		0x001F

#define OCF_WRITE_AUTH_ENABLE		0x0020

#define AUTH_DISABLED		0x00
#define AUTH_ENABLED		0x01

#define OCF_READ_ENCRYPT_MODE		0x0021

#define OCF_WRITE_ENCRYPT_MODE		0x0022

#define ENCRYPT_DISABLED	0x00
#define ENCRYPT_P2P		0x01
#define ENCRYPT_BOTH		0x02

#define OCF_READ_CLASS_OF_DEV		0x0023
typedef struct {
    uint8_t	status;
    uint8_t	dev_class[3];
} __attribute__ ((packed)) read_class_of_dev_rp;
#define READ_CLASS_OF_DEV_RP_SIZE 4 

#define OCF_WRITE_CLASS_OF_DEV		0x0024
typedef struct {
    uint8_t	dev_class[3];
} __attribute__ ((packed)) write_class_of_dev_cp;
#define WRITE_CLASS_OF_DEV_CP_SIZE 3

#define OCF_READ_VOICE_SETTING		0x0025
typedef struct {
    uint8_t	status;
    uint16_t	voice_setting;
} __attribute__ ((packed)) read_voice_setting_rp;
#define READ_VOICE_SETTING_RP_SIZE 3

#define OCF_WRITE_VOICE_SETTING		0x0026
typedef struct {
    uint16_t	voice_setting;
} __attribute__ ((packed)) write_voice_setting_cp;
#define WRITE_VOICE_SETTING_CP_SIZE 2

#define OCF_READ_AUTOMATIC_FLUSH_TIMEOUT	0x0027

#define OCF_WRITE_AUTOMATIC_FLUSH_TIMEOUT	0x0028

#define OCF_READ_NUM_BROADCAST_RETRANS	0x0029

#define OCF_WRITE_NUM_BROADCAST_RETRANS	0x002A

#define OCF_READ_HOLD_MODE_ACTIVITY	0x002B

#define OCF_WRITE_HOLD_MODE_ACTIVITY	0x002C

#define OCF_READ_TRANSMIT_POWER_LEVEL	0x002D
typedef struct {
    uint16_t	handle;
    uint8_t	type;
} __attribute__ ((packed)) read_transmit_power_level_cp;
#define READ_TRANSMIT_POWER_LEVEL_CP_SIZE 3
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    int8_t	level;
} __attribute__ ((packed)) read_transmit_power_level_rp;
#define READ_TRANSMIT_POWER_LEVEL_RP_SIZE 4

#define OCF_HOST_BUFFER_SIZE		0x0033
typedef struct {
    uint16_t	acl_mtu;
    uint8_t	sco_mtu;
    uint16_t	acl_max_pkt;
    uint16_t	sco_max_pkt;
} __attribute__ ((packed)) host_buffer_size_cp;
#define HOST_BUFFER_SIZE_CP_SIZE 7

#define OCF_HOST_NUMBER_OF_COMPLETED_PACKETS	0x0035

#define OCF_READ_LINK_SUPERVISION_TIMEOUT	0x0036
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint16_t	link_sup_to;
} __attribute__ ((packed)) read_link_supervision_timeout_rp;
#define READ_LINK_SUPERVISION_TIMEOUT_RP_SIZE 5

#define OCF_WRITE_LINK_SUPERVISION_TIMEOUT	0x0037
typedef struct {
    uint16_t	handle;
    uint16_t	link_sup_to;
} __attribute__ ((packed)) write_link_supervision_timeout_cp;
#define WRITE_LINK_SUPERVISION_TIMEOUT_CP_SIZE 4
typedef struct {
    uint8_t	status;
    uint16_t	handle;
} __attribute__ ((packed)) write_link_supervision_timeout_rp;
#define WRITE_LINK_SUPERVISION_TIMEOUT_RP_SIZE 3

#define OCF_READ_NUM_SUPPORTED_IAC	0x0038

#define MAX_IAC_LAP 0x40
#define OCF_READ_CURRENT_IAC_LAP	0x0039
typedef struct {
    uint8_t	status;
    uint8_t	num_current_iac;
    uint8_t	lap[MAX_IAC_LAP][3];
} __attribute__ ((packed)) read_current_iac_lap_rp;
#define READ_CURRENT_IAC_LAP_RP_SIZE 2+3*MAX_IAC_LAP

#define OCF_WRITE_CURRENT_IAC_LAP	0x003A
typedef struct {
    uint8_t	num_current_iac;
    uint8_t	lap[MAX_IAC_LAP][3];
} __attribute__ ((packed)) write_current_iac_lap_cp;
#define WRITE_CURRENT_IAC_LAP_CP_SIZE 1+3*MAX_IAC_LAP

#define OCF_READ_PAGE_SCAN_PERIOD_MODE	0x003B

#define OCF_WRITE_PAGE_SCAN_PERIOD_MODE	0x003C

#define OCF_READ_PAGE_SCAN_MODE		0x003D

#define OCF_WRITE_PAGE_SCAN_MODE	0x003E

#define OCF_SET_AFH_CLASSIFICATION	0x003F
typedef struct {
    uint8_t	map[10];
} __attribute__ ((packed)) set_afh_classification_cp;
#define SET_AFH_CLASSIFICATION_CP_SIZE 10
typedef struct {
    uint8_t	status;
} __attribute__ ((packed)) set_afh_classification_rp;
#define SET_AFH_CLASSIFICATION_RP_SIZE 1

#define OCF_READ_INQUIRY_SCAN_TYPE	0x0042
typedef struct {
    uint8_t	status;
    uint8_t	type;
} __attribute__ ((packed)) read_inquiry_scan_type_rp;
#define READ_INQUIRY_SCAN_TYPE_RP_SIZE 2

#define OCF_WRITE_INQUIRY_SCAN_TYPE	0x0043
typedef struct {
    uint8_t	type;
} __attribute__ ((packed)) write_inquiry_scan_type_cp;
#define WRITE_INQUIRY_SCAN_TYPE_CP_SIZE 1
typedef struct {
    uint8_t	status;
} __attribute__ ((packed)) write_inquiry_scan_type_rp;
#define WRITE_INQUIRY_SCAN_TYPE_RP_SIZE 1

#define OCF_READ_INQUIRY_MODE		0x0044
typedef struct {
    uint8_t	status;
    uint8_t	mode;
} __attribute__ ((packed)) read_inquiry_mode_rp;
#define READ_INQUIRY_MODE_RP_SIZE 2

#define OCF_WRITE_INQUIRY_MODE		0x0045
typedef struct {
    uint8_t	mode;
} __attribute__ ((packed)) write_inquiry_mode_cp;
#define WRITE_INQUIRY_MODE_CP_SIZE 1
typedef struct {
    uint8_t	status;
} __attribute__ ((packed)) write_inquiry_mode_rp;
#define WRITE_INQUIRY_MODE_RP_SIZE 1

#define OCF_READ_PAGE_SCAN_TYPE		0x0046

#define OCF_WRITE_PAGE_SCAN_TYPE	0x0047

#define OCF_READ_AFH_MODE		0x0048
typedef struct {
    uint8_t	status;
    uint8_t	mode;
} __attribute__ ((packed)) read_afh_mode_rp;
#define READ_AFH_MODE_RP_SIZE 2

#define OCF_WRITE_AFH_MODE		0x0049
typedef struct {
    uint8_t	mode;
} __attribute__ ((packed)) write_afh_mode_cp;
#define WRITE_AFH_MODE_CP_SIZE 1
typedef struct {
    uint8_t	status;
} __attribute__ ((packed)) write_afh_mode_rp;
#define WRITE_AFH_MODE_RP_SIZE 1

#define OCF_READ_EXT_INQUIRY_RESPONSE	0x0051
typedef struct {
    uint8_t	status;
    uint8_t	fec;
    uint8_t	data[240];
} __attribute__ ((packed)) read_ext_inquiry_response_rp;
#define READ_EXT_INQUIRY_RESPONSE_RP_SIZE 242

#define OCF_WRITE_EXT_INQUIRY_RESPONSE	0x0052
typedef struct {
    uint8_t	fec;
    uint8_t	data[240];
} __attribute__ ((packed)) write_ext_inquiry_response_cp;
#define WRITE_EXT_INQUIRY_RESPONSE_CP_SIZE 241
typedef struct {
    uint8_t	status;
} __attribute__ ((packed)) write_ext_inquiry_response_rp;
#define WRITE_EXT_INQUIRY_RESPONSE_RP_SIZE 1

/* Informational Parameters */
#define OGF_INFO_PARAM		0x04

#define OCF_READ_LOCAL_VERSION		0x0001
typedef struct {
    uint8_t	status;
    uint8_t	hci_ver;
    uint16_t	hci_rev;
    uint8_t	lmp_ver;
    uint16_t	manufacturer;
    uint16_t	lmp_subver;
} __attribute__ ((packed)) read_local_version_rp;
#define READ_LOCAL_VERSION_RP_SIZE 9

#define OCF_READ_LOCAL_COMMANDS		0x0002
typedef struct {
    uint8_t	status;
    uint8_t	commands[64];
} __attribute__ ((packed)) read_local_commands_rp;
#define READ_LOCAL_COMMANDS_RP_SIZE 65

#define OCF_READ_LOCAL_FEATURES		0x0003
typedef struct {
    uint8_t	status;
    uint8_t	features[8];
} __attribute__ ((packed)) read_local_features_rp;
#define READ_LOCAL_FEATURES_RP_SIZE 9

#define OCF_READ_LOCAL_EXT_FEATURES	0x0004
typedef struct {
    uint8_t	page_num;
} __attribute__ ((packed)) read_local_ext_features_cp;
#define READ_LOCAL_EXT_FEATURES_CP_SIZE 1
typedef struct {
    uint8_t	status;
    uint8_t	page_num;
    uint8_t	max_page_num;
    uint8_t	features[8];
} __attribute__ ((packed)) read_local_ext_features_rp;
#define READ_LOCAL_EXT_FEATURES_RP_SIZE 11

#define OCF_READ_BUFFER_SIZE		0x0005
typedef struct {
    uint8_t	status;
    uint16_t	acl_mtu;
    uint8_t	sco_mtu;
    uint16_t	acl_max_pkt;
    uint16_t	sco_max_pkt;
} __attribute__ ((packed)) read_buffer_size_rp;
#define READ_BUFFER_SIZE_RP_SIZE 8

#define OCF_READ_COUNTRY_CODE		0x0007
typedef struct {
    uint8_t	status;
    uint8_t	country_code;
} __attribute__ ((packed)) read_country_code_rp;
#define READ_COUNTRY_CODE_RP_SIZE 2

#define OCF_READ_BD_ADDR		0x0009
typedef struct {
    uint8_t	status;
    bdaddr_t	bdaddr;
} __attribute__ ((packed)) read_bd_addr_rp;
#define READ_BD_ADDR_RP_SIZE 7

/* Status params */
#define OGF_STATUS_PARAM	0x05

#define OCF_READ_FAILED_CONTACT_COUNTER		0x0001
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	counter;
} __attribute__ ((packed)) read_failed_contact_counter_rp;
#define READ_FAILED_CONTACT_COUNTER_RP_SIZE 4

#define OCF_RESET_FAILED_CONTACT_COUNTER	0x0002
typedef struct {
    uint8_t	status;
    uint16_t	handle;
} __attribute__ ((packed)) reset_failed_contact_counter_rp;
#define RESET_FAILED_CONTACT_COUNTER_RP_SIZE 4

#define OCF_READ_LINK_QUALITY		0x0003
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) read_link_quality_cp;
#define READ_LINK_QUALITY_CP_SIZE 4

typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	link_quality;
} __attribute__ ((packed)) read_link_quality_rp;
#define READ_LINK_QUALITY_RP_SIZE 4

#define OCF_READ_RSSI			0x0005
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    int8_t	rssi;
} __attribute__ ((packed)) read_rssi_rp;
#define READ_RSSI_RP_SIZE 4

#define OCF_READ_AFH_MAP		0x0006
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	mode;
    uint8_t	map[10];
} __attribute__ ((packed)) read_afh_map_rp;
#define READ_AFH_MAP_RP_SIZE 14

#define OCF_READ_CLOCK			0x0007
typedef struct {
    uint16_t	handle;
    uint8_t	which_clock;
} __attribute__ ((packed)) read_clock_cp;
#define READ_CLOCK_CP_SIZE 3
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint32_t	clock;
    uint16_t	accuracy;
} __attribute__ ((packed)) read_clock_rp;
#define READ_CLOCK_RP_SIZE 9

/* Testing commands */
#define OGF_TESTING_CMD		0x3e

/* Vendor specific commands */
#define OGF_VENDOR_CMD		0x3f

/* HCI Events */

#define EVT_INQUIRY_COMPLETE		0x01

#define EVT_INQUIRY_RESULT		0x02
typedef struct {
    uint8_t	num_responses;
    bdaddr_t	bdaddr;
    uint8_t	pscan_rep_mode;
    uint8_t	pscan_period_mode;
    uint8_t	pscan_mode;
    uint8_t	dev_class[3];
    uint16_t	clock_offset;
} __attribute__ ((packed)) inquiry_info;
#define INQUIRY_INFO_SIZE 14

#define EVT_CONN_COMPLETE		0x03
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    bdaddr_t	bdaddr;
    uint8_t	link_type;
    uint8_t	encr_mode;
} __attribute__ ((packed)) evt_conn_complete;
#define EVT_CONN_COMPLETE_SIZE 11

#define EVT_CONN_REQUEST		0x04
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	dev_class[3];
    uint8_t	link_type;
} __attribute__ ((packed)) evt_conn_request;
#define EVT_CONN_REQUEST_SIZE 10

#define EVT_DISCONN_COMPLETE		0x05
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	reason;
} __attribute__ ((packed)) evt_disconn_complete;
#define EVT_DISCONN_COMPLETE_SIZE 4

#define EVT_AUTH_COMPLETE		0x06
typedef struct {
    uint8_t	status;
    uint16_t	handle;
} __attribute__ ((packed)) evt_auth_complete;
#define EVT_AUTH_COMPLETE_SIZE 3

#define EVT_REMOTE_NAME_REQ_COMPLETE	0x07
typedef struct {
    uint8_t	status;
    bdaddr_t	bdaddr;
    char	name[248];
} __attribute__ ((packed)) evt_remote_name_req_complete;
#define EVT_REMOTE_NAME_REQ_COMPLETE_SIZE 255

#define EVT_ENCRYPT_CHANGE		0x08
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	encrypt;
} __attribute__ ((packed)) evt_encrypt_change;
#define EVT_ENCRYPT_CHANGE_SIZE 5

#define EVT_CHANGE_CONN_LINK_KEY_COMPLETE	0x09
typedef struct {
    uint8_t	status;
    uint16_t	handle;
}  __attribute__ ((packed)) evt_change_conn_link_key_complete;
#define EVT_CHANGE_CONN_LINK_KEY_COMPLETE_SIZE 3

#define EVT_MASTER_LINK_KEY_COMPLETE		0x0A
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	key_flag;
} __attribute__ ((packed)) evt_master_link_key_complete;
#define EVT_MASTER_LINK_KEY_COMPLETE_SIZE 4

#define EVT_READ_REMOTE_FEATURES_COMPLETE	0x0B
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	features[8];
} __attribute__ ((packed)) evt_read_remote_features_complete;
#define EVT_READ_REMOTE_FEATURES_COMPLETE_SIZE 11

#define EVT_READ_REMOTE_VERSION_COMPLETE	0x0C
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	lmp_ver;
    uint16_t	manufacturer;
    uint16_t	lmp_subver;
} __attribute__ ((packed)) evt_read_remote_version_complete;
#define EVT_READ_REMOTE_VERSION_COMPLETE_SIZE 8

#define EVT_QOS_SETUP_COMPLETE		0x0D
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	flags;			/* Reserved */
    hci_qos	qos;
} __attribute__ ((packed)) evt_qos_setup_complete;
#define EVT_QOS_SETUP_COMPLETE_SIZE (4 + HCI_QOS_CP_SIZE)

#define EVT_CMD_COMPLETE 		0x0E
typedef struct {
    uint8_t	ncmd;
    uint16_t	opcode;
} __attribute__ ((packed)) evt_cmd_complete;
#define EVT_CMD_COMPLETE_SIZE 3

#define EVT_CMD_STATUS 			0x0F
typedef struct {
    uint8_t	status;
    uint8_t	ncmd;
    uint16_t	opcode;
} __attribute__ ((packed)) evt_cmd_status;
#define EVT_CMD_STATUS_SIZE 4

#define EVT_HARDWARE_ERROR		0x10
typedef struct {
    uint8_t	code;
} __attribute__ ((packed)) evt_hardware_error;
#define EVT_HARDWARE_ERROR_SIZE 1

#define EVT_FLUSH_OCCURRED		0x11
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) evt_flush_occurred;
#define EVT_FLUSH_OCCURRED_SIZE 2

#define EVT_ROLE_CHANGE			0x12
typedef struct {
    uint8_t	status;
    bdaddr_t	bdaddr;
    uint8_t	role;
} __attribute__ ((packed)) evt_role_change;
#define EVT_ROLE_CHANGE_SIZE 8

#define EVT_NUM_COMP_PKTS		0x13
typedef struct {
    uint8_t	num_hndl;
    struct {
        uint16_t handle;
        uint16_t num_packets;
    } connection[0];
} __attribute__ ((packed)) evt_num_comp_pkts;
#define EVT_NUM_COMP_PKTS_SIZE(num_hndl) (1 + 4 * (num_hndl))

#define EVT_MODE_CHANGE			0x14
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	mode;
    uint16_t	interval;
} __attribute__ ((packed)) evt_mode_change;
#define EVT_MODE_CHANGE_SIZE 6

#define EVT_RETURN_LINK_KEYS		0x15
typedef struct {
    uint8_t	num_keys;
    /* variable length part */
} __attribute__ ((packed)) evt_return_link_keys;
#define EVT_RETURN_LINK_KEYS_SIZE 1

#define EVT_PIN_CODE_REQ		0x16
typedef struct {
    bdaddr_t	bdaddr;
} __attribute__ ((packed)) evt_pin_code_req;
#define EVT_PIN_CODE_REQ_SIZE 6

#define EVT_LINK_KEY_REQ		0x17
typedef struct {
    bdaddr_t	bdaddr;
} __attribute__ ((packed)) evt_link_key_req;
#define EVT_LINK_KEY_REQ_SIZE 6

#define EVT_LINK_KEY_NOTIFY		0x18
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	link_key[16];
    uint8_t	key_type;
} __attribute__ ((packed)) evt_link_key_notify;
#define EVT_LINK_KEY_NOTIFY_SIZE 23

#define EVT_LOOPBACK_COMMAND		0x19

#define EVT_DATA_BUFFER_OVERFLOW	0x1A
typedef struct {
    uint8_t	link_type;
} __attribute__ ((packed)) evt_data_buffer_overflow;
#define EVT_DATA_BUFFER_OVERFLOW_SIZE 1

#define EVT_MAX_SLOTS_CHANGE		0x1B
typedef struct {
    uint16_t	handle;
    uint8_t	max_slots;
} __attribute__ ((packed)) evt_max_slots_change;
#define EVT_MAX_SLOTS_CHANGE_SIZE 3

#define EVT_READ_CLOCK_OFFSET_COMPLETE	0x1C
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint16_t	clock_offset;
} __attribute__ ((packed)) evt_read_clock_offset_complete;
#define EVT_READ_CLOCK_OFFSET_COMPLETE_SIZE 5

#define EVT_CONN_PTYPE_CHANGED		0x1D
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint16_t	ptype;
} __attribute__ ((packed)) evt_conn_ptype_changed;
#define EVT_CONN_PTYPE_CHANGED_SIZE 5

#define EVT_QOS_VIOLATION		0x1E
typedef struct {
    uint16_t	handle;
} __attribute__ ((packed)) evt_qos_violation;
#define EVT_QOS_VIOLATION_SIZE 2

#define EVT_PSCAN_REP_MODE_CHANGE	0x20
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	pscan_rep_mode;
} __attribute__ ((packed)) evt_pscan_rep_mode_change;
#define EVT_PSCAN_REP_MODE_CHANGE_SIZE 7

#define EVT_FLOW_SPEC_COMPLETE		0x21
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	flags;
    uint8_t	direction;
    hci_qos	qos;
} __attribute__ ((packed)) evt_flow_spec_complete;
#define EVT_FLOW_SPEC_COMPLETE_SIZE (5 + HCI_QOS_CP_SIZE)

#define EVT_INQUIRY_RESULT_WITH_RSSI	0x22
typedef struct {
    uint8_t	num_responses;
    bdaddr_t	bdaddr;
    uint8_t	pscan_rep_mode;
    uint8_t	pscan_period_mode;
    uint8_t	dev_class[3];
    uint16_t	clock_offset;
    int8_t	rssi;
} __attribute__ ((packed)) inquiry_info_with_rssi;
#define INQUIRY_INFO_WITH_RSSI_SIZE 15
typedef struct {
    uint8_t	num_responses;
    bdaddr_t	bdaddr;
    uint8_t	pscan_rep_mode;
    uint8_t	pscan_period_mode;
    uint8_t	pscan_mode;
    uint8_t	dev_class[3];
    uint16_t	clock_offset;
    int8_t	rssi;
} __attribute__ ((packed)) inquiry_info_with_rssi_and_pscan_mode;
#define INQUIRY_INFO_WITH_RSSI_AND_PSCAN_MODE_SIZE 16

#define EVT_READ_REMOTE_EXT_FEATURES_COMPLETE	0x23
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	page_num;
    uint8_t	max_page_num;
    uint8_t	features[8];
} __attribute__ ((packed)) evt_read_remote_ext_features_complete;
#define EVT_READ_REMOTE_EXT_FEATURES_COMPLETE_SIZE 13

#define EVT_SYNC_CONN_COMPLETE		0x2C
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    bdaddr_t	bdaddr;
    uint8_t	link_type;
    uint8_t	trans_interval;
    uint8_t	retrans_window;
    uint16_t	rx_pkt_len;
    uint16_t	tx_pkt_len;
    uint8_t	air_mode;
} __attribute__ ((packed)) evt_sync_conn_complete;
#define EVT_SYNC_CONN_COMPLETE_SIZE 17

#define EVT_SYNC_CONN_CHANGED		0x2D
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint8_t	trans_interval;
    uint8_t	retrans_window;
    uint16_t	rx_pkt_len;
    uint16_t	tx_pkt_len;
} __attribute__ ((packed)) evt_sync_conn_changed;
#define EVT_SYNC_CONN_CHANGED_SIZE 9

#define EVT_SNIFF_SUBRATE		0x2E
typedef struct {
    uint8_t	status;
    uint16_t	handle;
    uint16_t	max_remote_latency;
    uint16_t	max_local_latency;
    uint16_t	min_remote_timeout;
    uint16_t	min_local_timeout;
} __attribute__ ((packed)) evt_sniff_subrate;
#define EVT_SNIFF_SUBRATE_SIZE 11

#define EVT_EXTENDED_INQUIRY_RESULT	0x2F
typedef struct {
    bdaddr_t	bdaddr;
    uint8_t	pscan_rep_mode;
    uint8_t	pscan_period_mode;
    uint8_t	dev_class[3];
    uint16_t	clock_offset;
    int8_t	rssi;
    uint8_t	data[240];
} __attribute__ ((packed)) extended_inquiry_info;
#define EXTENDED_INQUIRY_INFO_SIZE 254

#define EVT_TESTING			0xFE

#define EVT_VENDOR			0xFF

/* Command opcode pack/unpack */
#define cmd_opcode_pack(ogf, ocf)	(uint16_t)((ocf & 0x03ff)|(ogf << 10))
#define cmd_opcode_ogf(op)		(op >> 10)
#define cmd_opcode_ocf(op)		(op & 0x03ff)

/* ACL handle and flags pack/unpack */
#define acl_handle_pack(h, f)	(uint16_t)(((h) & 0x0fff)|((f) << 12))
#define acl_handle(h)		((h) & 0x0fff)
#define acl_flags(h)		((h) >> 12)

/* HCI Packet structures */
#define HCI_COMMAND_HDR_SIZE	3
#define HCI_EVENT_HDR_SIZE	2
#define HCI_ACL_HDR_SIZE	4
#define HCI_SCO_HDR_SIZE	3

struct hci_command_hdr {
    uint16_t 	opcode;		/* OCF & OGF */
    uint8_t	plen;
} __attribute__ ((packed));

struct hci_event_hdr {
    uint8_t	evt;
    uint8_t	plen;
} __attribute__ ((packed));

struct hci_acl_hdr {
    uint16_t	handle;		/* Handle & Flags(PB, BC) */
    uint16_t	dlen;
} __attribute__ ((packed));

struct hci_sco_hdr {
    uint16_t	handle;
    uint8_t	dlen;
} __attribute__ ((packed));

/* L2CAP layer defines */

enum bt_l2cap_lm_bits {
    L2CAP_LM_MASTER	= 1 << 0,
    L2CAP_LM_AUTH	= 1 << 1,
    L2CAP_LM_ENCRYPT	= 1 << 2,
    L2CAP_LM_TRUSTED	= 1 << 3,
    L2CAP_LM_RELIABLE	= 1 << 4,
    L2CAP_LM_SECURE	= 1 << 5,
};

enum bt_l2cap_cid_predef {
    L2CAP_CID_INVALID	= 0x0000,
    L2CAP_CID_SIGNALLING= 0x0001,
    L2CAP_CID_GROUP	= 0x0002,
    L2CAP_CID_ALLOC	= 0x0040,
};

/* L2CAP command codes */
enum bt_l2cap_cmd {
    L2CAP_COMMAND_REJ	= 1,
    L2CAP_CONN_REQ,
    L2CAP_CONN_RSP,
    L2CAP_CONF_REQ,
    L2CAP_CONF_RSP,
    L2CAP_DISCONN_REQ,
    L2CAP_DISCONN_RSP,
    L2CAP_ECHO_REQ,
    L2CAP_ECHO_RSP,
    L2CAP_INFO_REQ,
    L2CAP_INFO_RSP,
};

enum bt_l2cap_sar_bits {
    L2CAP_SAR_NO_SEG	= 0,
    L2CAP_SAR_START,
    L2CAP_SAR_END,
    L2CAP_SAR_CONT,
};

/* L2CAP structures */
typedef struct {
    uint16_t	len;
    uint16_t	cid;
    uint8_t	data[0];
} __attribute__ ((packed)) l2cap_hdr;
#define L2CAP_HDR_SIZE 4

typedef struct {
    uint8_t	code;
    uint8_t	ident;
    uint16_t	len;
} __attribute__ ((packed)) l2cap_cmd_hdr;
#define L2CAP_CMD_HDR_SIZE 4

typedef struct {
    uint16_t	reason;
} __attribute__ ((packed)) l2cap_cmd_rej;
#define L2CAP_CMD_REJ_SIZE 2

typedef struct {
    uint16_t	dcid;
    uint16_t	scid;
} __attribute__ ((packed)) l2cap_cmd_rej_cid;
#define L2CAP_CMD_REJ_CID_SIZE 4

/* reject reason */
enum bt_l2cap_rej_reason {
    L2CAP_REJ_CMD_NOT_UNDERSTOOD = 0,
    L2CAP_REJ_SIG_TOOBIG,
    L2CAP_REJ_CID_INVAL,
};

typedef struct {
    uint16_t	psm;
    uint16_t	scid;
} __attribute__ ((packed)) l2cap_conn_req;
#define L2CAP_CONN_REQ_SIZE 4

typedef struct {
    uint16_t	dcid;
    uint16_t	scid;
    uint16_t	result;
    uint16_t	status;
} __attribute__ ((packed)) l2cap_conn_rsp;
#define L2CAP_CONN_RSP_SIZE 8

/* connect result */
enum bt_l2cap_conn_res {
    L2CAP_CR_SUCCESS	= 0,
    L2CAP_CR_PEND,
    L2CAP_CR_BAD_PSM,
    L2CAP_CR_SEC_BLOCK,
    L2CAP_CR_NO_MEM,
};

/* connect status */
enum bt_l2cap_conn_stat {
    L2CAP_CS_NO_INFO	= 0,
    L2CAP_CS_AUTHEN_PEND,
    L2CAP_CS_AUTHOR_PEND,
};

typedef struct {
    uint16_t	dcid;
    uint16_t	flags;
    uint8_t	data[0];
} __attribute__ ((packed)) l2cap_conf_req;
#define L2CAP_CONF_REQ_SIZE(datalen) (4 + (datalen))

typedef struct {
    uint16_t	scid;
    uint16_t	flags;
    uint16_t	result;
    uint8_t	data[0];
} __attribute__ ((packed)) l2cap_conf_rsp;
#define L2CAP_CONF_RSP_SIZE(datalen) (6 + datalen)

enum bt_l2cap_conf_res {
    L2CAP_CONF_SUCCESS	= 0,
    L2CAP_CONF_UNACCEPT,
    L2CAP_CONF_REJECT,
    L2CAP_CONF_UNKNOWN,
};

typedef struct {
    uint8_t	type;
    uint8_t	len;
    uint8_t	val[0];
} __attribute__ ((packed)) l2cap_conf_opt;
#define L2CAP_CONF_OPT_SIZE 2

enum bt_l2cap_conf_val {
    L2CAP_CONF_MTU	= 1,
    L2CAP_CONF_FLUSH_TO,
    L2CAP_CONF_QOS,
    L2CAP_CONF_RFC,
    L2CAP_CONF_RFC_MODE	= L2CAP_CONF_RFC,
};

typedef struct {
    uint8_t	flags;
    uint8_t	service_type;
    uint32_t	token_rate;
    uint32_t	token_bucket_size;
    uint32_t	peak_bandwidth;
    uint32_t	latency;
    uint32_t	delay_variation;
} __attribute__ ((packed)) l2cap_conf_opt_qos;
#define L2CAP_CONF_OPT_QOS_SIZE 22

enum bt_l2cap_conf_opt_qos_st {
    L2CAP_CONF_QOS_NO_TRAFFIC = 0x00,
    L2CAP_CONF_QOS_BEST_EFFORT,
    L2CAP_CONF_QOS_GUARANTEED,
};

#define L2CAP_CONF_QOS_WILDCARD	0xffffffff

enum bt_l2cap_mode {
    L2CAP_MODE_BASIC	= 0,
    L2CAP_MODE_RETRANS	= 1,
    L2CAP_MODE_FLOWCTL	= 2,
};

typedef struct {
    uint16_t	dcid;
    uint16_t	scid;
} __attribute__ ((packed)) l2cap_disconn_req;
#define L2CAP_DISCONN_REQ_SIZE 4

typedef struct {
    uint16_t	dcid;
    uint16_t	scid;
} __attribute__ ((packed)) l2cap_disconn_rsp;
#define L2CAP_DISCONN_RSP_SIZE 4

typedef struct {
    uint16_t	type;
} __attribute__ ((packed)) l2cap_info_req;
#define L2CAP_INFO_REQ_SIZE 2

typedef struct {
    uint16_t	type;
    uint16_t	result;
    uint8_t	data[0];
} __attribute__ ((packed)) l2cap_info_rsp;
#define L2CAP_INFO_RSP_SIZE 4

/* info type */
enum bt_l2cap_info_type {
    L2CAP_IT_CL_MTU	= 1,
    L2CAP_IT_FEAT_MASK,
};

/* info result */
enum bt_l2cap_info_result {
    L2CAP_IR_SUCCESS	= 0,
    L2CAP_IR_NOTSUPP,
};

/* Service Discovery Protocol defines */
/* Note that all multibyte values in lower layer protocols (above in this file)
 * are little-endian while SDP is big-endian.  */

/* Protocol UUIDs */
enum sdp_proto_uuid {
    SDP_UUID		= 0x0001,
    UDP_UUID		= 0x0002,
    RFCOMM_UUID		= 0x0003,
    TCP_UUID		= 0x0004,
    TCS_BIN_UUID	= 0x0005,
    TCS_AT_UUID		= 0x0006,
    OBEX_UUID		= 0x0008,
    IP_UUID		= 0x0009,
    FTP_UUID		= 0x000a,
    HTTP_UUID		= 0x000c,
    WSP_UUID		= 0x000e,
    BNEP_UUID		= 0x000f,
    UPNP_UUID		= 0x0010,
    HIDP_UUID		= 0x0011,
    HCRP_CTRL_UUID	= 0x0012,
    HCRP_DATA_UUID	= 0x0014,
    HCRP_NOTE_UUID	= 0x0016,
    AVCTP_UUID		= 0x0017,
    AVDTP_UUID		= 0x0019,
    CMTP_UUID		= 0x001b,
    UDI_UUID		= 0x001d,
    MCAP_CTRL_UUID	= 0x001e,
    MCAP_DATA_UUID	= 0x001f,
    L2CAP_UUID		= 0x0100,
};

/*
 * Service class identifiers of standard services and service groups
 */
enum service_class_id {
    SDP_SERVER_SVCLASS_ID		= 0x1000,
    BROWSE_GRP_DESC_SVCLASS_ID		= 0x1001,
    PUBLIC_BROWSE_GROUP			= 0x1002,
    SERIAL_PORT_SVCLASS_ID		= 0x1101,
    LAN_ACCESS_SVCLASS_ID		= 0x1102,
    DIALUP_NET_SVCLASS_ID		= 0x1103,
    IRMC_SYNC_SVCLASS_ID		= 0x1104,
    OBEX_OBJPUSH_SVCLASS_ID		= 0x1105,
    OBEX_FILETRANS_SVCLASS_ID		= 0x1106,
    IRMC_SYNC_CMD_SVCLASS_ID		= 0x1107,
    HEADSET_SVCLASS_ID			= 0x1108,
    CORDLESS_TELEPHONY_SVCLASS_ID	= 0x1109,
    AUDIO_SOURCE_SVCLASS_ID		= 0x110a,
    AUDIO_SINK_SVCLASS_ID		= 0x110b,
    AV_REMOTE_TARGET_SVCLASS_ID		= 0x110c,
    ADVANCED_AUDIO_SVCLASS_ID		= 0x110d,
    AV_REMOTE_SVCLASS_ID		= 0x110e,
    VIDEO_CONF_SVCLASS_ID		= 0x110f,
    INTERCOM_SVCLASS_ID			= 0x1110,
    FAX_SVCLASS_ID			= 0x1111,
    HEADSET_AGW_SVCLASS_ID		= 0x1112,
    WAP_SVCLASS_ID			= 0x1113,
    WAP_CLIENT_SVCLASS_ID		= 0x1114,
    PANU_SVCLASS_ID			= 0x1115,
    NAP_SVCLASS_ID			= 0x1116,
    GN_SVCLASS_ID			= 0x1117,
    DIRECT_PRINTING_SVCLASS_ID		= 0x1118,
    REFERENCE_PRINTING_SVCLASS_ID	= 0x1119,
    IMAGING_SVCLASS_ID			= 0x111a,
    IMAGING_RESPONDER_SVCLASS_ID	= 0x111b,
    IMAGING_ARCHIVE_SVCLASS_ID		= 0x111c,
    IMAGING_REFOBJS_SVCLASS_ID		= 0x111d,
    HANDSFREE_SVCLASS_ID		= 0x111e,
    HANDSFREE_AGW_SVCLASS_ID		= 0x111f,
    DIRECT_PRT_REFOBJS_SVCLASS_ID	= 0x1120,
    REFLECTED_UI_SVCLASS_ID		= 0x1121,
    BASIC_PRINTING_SVCLASS_ID		= 0x1122,
    PRINTING_STATUS_SVCLASS_ID		= 0x1123,
    HID_SVCLASS_ID			= 0x1124,
    HCR_SVCLASS_ID			= 0x1125,
    HCR_PRINT_SVCLASS_ID		= 0x1126,
    HCR_SCAN_SVCLASS_ID			= 0x1127,
    CIP_SVCLASS_ID			= 0x1128,
    VIDEO_CONF_GW_SVCLASS_ID		= 0x1129,
    UDI_MT_SVCLASS_ID			= 0x112a,
    UDI_TA_SVCLASS_ID			= 0x112b,
    AV_SVCLASS_ID			= 0x112c,
    SAP_SVCLASS_ID			= 0x112d,
    PBAP_PCE_SVCLASS_ID			= 0x112e,
    PBAP_PSE_SVCLASS_ID			= 0x112f,
    PBAP_SVCLASS_ID			= 0x1130,
    PNP_INFO_SVCLASS_ID			= 0x1200,
    GENERIC_NETWORKING_SVCLASS_ID	= 0x1201,
    GENERIC_FILETRANS_SVCLASS_ID	= 0x1202,
    GENERIC_AUDIO_SVCLASS_ID		= 0x1203,
    GENERIC_TELEPHONY_SVCLASS_ID	= 0x1204,
    UPNP_SVCLASS_ID			= 0x1205,
    UPNP_IP_SVCLASS_ID			= 0x1206,
    UPNP_PAN_SVCLASS_ID			= 0x1300,
    UPNP_LAP_SVCLASS_ID			= 0x1301,
    UPNP_L2CAP_SVCLASS_ID		= 0x1302,
    VIDEO_SOURCE_SVCLASS_ID		= 0x1303,
    VIDEO_SINK_SVCLASS_ID		= 0x1304,
    VIDEO_DISTRIBUTION_SVCLASS_ID	= 0x1305,
    MDP_SVCLASS_ID			= 0x1400,
    MDP_SOURCE_SVCLASS_ID		= 0x1401,
    MDP_SINK_SVCLASS_ID			= 0x1402,
    APPLE_AGENT_SVCLASS_ID		= 0x2112,
};

/*
 * Standard profile descriptor identifiers; note these
 * may be identical to some of the service classes defined above
 */
#define SDP_SERVER_PROFILE_ID		SDP_SERVER_SVCLASS_ID
#define BROWSE_GRP_DESC_PROFILE_ID	BROWSE_GRP_DESC_SVCLASS_ID
#define SERIAL_PORT_PROFILE_ID		SERIAL_PORT_SVCLASS_ID
#define LAN_ACCESS_PROFILE_ID		LAN_ACCESS_SVCLASS_ID
#define DIALUP_NET_PROFILE_ID		DIALUP_NET_SVCLASS_ID
#define IRMC_SYNC_PROFILE_ID		IRMC_SYNC_SVCLASS_ID
#define OBEX_OBJPUSH_PROFILE_ID		OBEX_OBJPUSH_SVCLASS_ID
#define OBEX_FILETRANS_PROFILE_ID	OBEX_FILETRANS_SVCLASS_ID
#define IRMC_SYNC_CMD_PROFILE_ID	IRMC_SYNC_CMD_SVCLASS_ID
#define HEADSET_PROFILE_ID		HEADSET_SVCLASS_ID
#define CORDLESS_TELEPHONY_PROFILE_ID	CORDLESS_TELEPHONY_SVCLASS_ID
#define AUDIO_SOURCE_PROFILE_ID		AUDIO_SOURCE_SVCLASS_ID
#define AUDIO_SINK_PROFILE_ID		AUDIO_SINK_SVCLASS_ID
#define AV_REMOTE_TARGET_PROFILE_ID	AV_REMOTE_TARGET_SVCLASS_ID
#define ADVANCED_AUDIO_PROFILE_ID	ADVANCED_AUDIO_SVCLASS_ID
#define AV_REMOTE_PROFILE_ID		AV_REMOTE_SVCLASS_ID
#define VIDEO_CONF_PROFILE_ID		VIDEO_CONF_SVCLASS_ID
#define INTERCOM_PROFILE_ID		INTERCOM_SVCLASS_ID
#define FAX_PROFILE_ID			FAX_SVCLASS_ID
#define HEADSET_AGW_PROFILE_ID		HEADSET_AGW_SVCLASS_ID
#define WAP_PROFILE_ID			WAP_SVCLASS_ID
#define WAP_CLIENT_PROFILE_ID		WAP_CLIENT_SVCLASS_ID
#define PANU_PROFILE_ID			PANU_SVCLASS_ID
#define NAP_PROFILE_ID			NAP_SVCLASS_ID
#define GN_PROFILE_ID			GN_SVCLASS_ID
#define DIRECT_PRINTING_PROFILE_ID	DIRECT_PRINTING_SVCLASS_ID
#define REFERENCE_PRINTING_PROFILE_ID	REFERENCE_PRINTING_SVCLASS_ID
#define IMAGING_PROFILE_ID		IMAGING_SVCLASS_ID
#define IMAGING_RESPONDER_PROFILE_ID	IMAGING_RESPONDER_SVCLASS_ID
#define IMAGING_ARCHIVE_PROFILE_ID	IMAGING_ARCHIVE_SVCLASS_ID
#define IMAGING_REFOBJS_PROFILE_ID	IMAGING_REFOBJS_SVCLASS_ID
#define HANDSFREE_PROFILE_ID		HANDSFREE_SVCLASS_ID
#define HANDSFREE_AGW_PROFILE_ID	HANDSFREE_AGW_SVCLASS_ID
#define DIRECT_PRT_REFOBJS_PROFILE_ID	DIRECT_PRT_REFOBJS_SVCLASS_ID
#define REFLECTED_UI_PROFILE_ID		REFLECTED_UI_SVCLASS_ID
#define BASIC_PRINTING_PROFILE_ID	BASIC_PRINTING_SVCLASS_ID
#define PRINTING_STATUS_PROFILE_ID	PRINTING_STATUS_SVCLASS_ID
#define HID_PROFILE_ID			HID_SVCLASS_ID
#define HCR_PROFILE_ID			HCR_SCAN_SVCLASS_ID
#define HCR_PRINT_PROFILE_ID		HCR_PRINT_SVCLASS_ID
#define HCR_SCAN_PROFILE_ID		HCR_SCAN_SVCLASS_ID
#define CIP_PROFILE_ID			CIP_SVCLASS_ID
#define VIDEO_CONF_GW_PROFILE_ID	VIDEO_CONF_GW_SVCLASS_ID
#define UDI_MT_PROFILE_ID		UDI_MT_SVCLASS_ID
#define UDI_TA_PROFILE_ID		UDI_TA_SVCLASS_ID
#define AV_PROFILE_ID			AV_SVCLASS_ID
#define SAP_PROFILE_ID			SAP_SVCLASS_ID
#define PBAP_PCE_PROFILE_ID		PBAP_PCE_SVCLASS_ID
#define PBAP_PSE_PROFILE_ID		PBAP_PSE_SVCLASS_ID
#define PBAP_PROFILE_ID			PBAP_SVCLASS_ID
#define PNP_INFO_PROFILE_ID		PNP_INFO_SVCLASS_ID
#define GENERIC_NETWORKING_PROFILE_ID	GENERIC_NETWORKING_SVCLASS_ID
#define GENERIC_FILETRANS_PROFILE_ID	GENERIC_FILETRANS_SVCLASS_ID
#define GENERIC_AUDIO_PROFILE_ID	GENERIC_AUDIO_SVCLASS_ID
#define GENERIC_TELEPHONY_PROFILE_ID	GENERIC_TELEPHONY_SVCLASS_ID
#define UPNP_PROFILE_ID			UPNP_SVCLASS_ID
#define UPNP_IP_PROFILE_ID		UPNP_IP_SVCLASS_ID
#define UPNP_PAN_PROFILE_ID		UPNP_PAN_SVCLASS_ID
#define UPNP_LAP_PROFILE_ID		UPNP_LAP_SVCLASS_ID
#define UPNP_L2CAP_PROFILE_ID		UPNP_L2CAP_SVCLASS_ID
#define VIDEO_SOURCE_PROFILE_ID		VIDEO_SOURCE_SVCLASS_ID
#define VIDEO_SINK_PROFILE_ID		VIDEO_SINK_SVCLASS_ID
#define VIDEO_DISTRIBUTION_PROFILE_ID	VIDEO_DISTRIBUTION_SVCLASS_ID
#define MDP_PROFILE_ID			MDP_SVCLASS_ID
#define MDP_SOURCE_PROFILE_ID		MDP_SROUCE_SVCLASS_ID
#define MDP_SINK_PROFILE_ID		MDP_SINK_SVCLASS_ID
#define APPLE_AGENT_PROFILE_ID		APPLE_AGENT_SVCLASS_ID

/* Data Representation */
enum bt_sdp_data_type {
    SDP_DTYPE_NIL	= 0 << 3,
    SDP_DTYPE_UINT	= 1 << 3,
    SDP_DTYPE_SINT	= 2 << 3,
    SDP_DTYPE_UUID	= 3 << 3,
    SDP_DTYPE_STRING	= 4 << 3,
    SDP_DTYPE_BOOL	= 5 << 3,
    SDP_DTYPE_SEQ	= 6 << 3,
    SDP_DTYPE_ALT	= 7 << 3,
    SDP_DTYPE_URL	= 8 << 3,
};

enum bt_sdp_data_size {
    SDP_DSIZE_1		= 0,
    SDP_DSIZE_2,
    SDP_DSIZE_4,
    SDP_DSIZE_8,
    SDP_DSIZE_16,
    SDP_DSIZE_NEXT1,
    SDP_DSIZE_NEXT2,
    SDP_DSIZE_NEXT4,
    SDP_DSIZE_MASK = SDP_DSIZE_NEXT4,
};

enum bt_sdp_cmd {
    SDP_ERROR_RSP		= 0x01,
    SDP_SVC_SEARCH_REQ		= 0x02,
    SDP_SVC_SEARCH_RSP		= 0x03,
    SDP_SVC_ATTR_REQ		= 0x04,
    SDP_SVC_ATTR_RSP		= 0x05,
    SDP_SVC_SEARCH_ATTR_REQ	= 0x06,
    SDP_SVC_SEARCH_ATTR_RSP	= 0x07,
};

enum bt_sdp_errorcode {
    SDP_INVALID_VERSION		= 0x0001,
    SDP_INVALID_RECORD_HANDLE	= 0x0002,
    SDP_INVALID_SYNTAX		= 0x0003,
    SDP_INVALID_PDU_SIZE	= 0x0004,
    SDP_INVALID_CSTATE		= 0x0005,
};

/*
 * String identifiers are based on the SDP spec stating that
 * "base attribute id of the primary (universal) language must be 0x0100"
 *
 * Other languages should have their own offset; e.g.:
 * #define XXXLangBase yyyy
 * #define AttrServiceName_XXX	0x0000+XXXLangBase
 */
#define SDP_PRIMARY_LANG_BASE 		0x0100

enum bt_sdp_attribute_id {
    SDP_ATTR_RECORD_HANDLE			= 0x0000,
    SDP_ATTR_SVCLASS_ID_LIST			= 0x0001,
    SDP_ATTR_RECORD_STATE			= 0x0002,
    SDP_ATTR_SERVICE_ID				= 0x0003,
    SDP_ATTR_PROTO_DESC_LIST			= 0x0004,
    SDP_ATTR_BROWSE_GRP_LIST			= 0x0005,
    SDP_ATTR_LANG_BASE_ATTR_ID_LIST		= 0x0006,
    SDP_ATTR_SVCINFO_TTL			= 0x0007,
    SDP_ATTR_SERVICE_AVAILABILITY		= 0x0008,
    SDP_ATTR_PFILE_DESC_LIST			= 0x0009,
    SDP_ATTR_DOC_URL				= 0x000a,
    SDP_ATTR_CLNT_EXEC_URL			= 0x000b,
    SDP_ATTR_ICON_URL				= 0x000c,
    SDP_ATTR_ADD_PROTO_DESC_LIST		= 0x000d,

    SDP_ATTR_SVCNAME_PRIMARY			= SDP_PRIMARY_LANG_BASE + 0,
    SDP_ATTR_SVCDESC_PRIMARY			= SDP_PRIMARY_LANG_BASE + 1,
    SDP_ATTR_SVCPROV_PRIMARY			= SDP_PRIMARY_LANG_BASE + 2,

    SDP_ATTR_GROUP_ID				= 0x0200,
    SDP_ATTR_IP_SUBNET				= 0x0200,

    /* SDP */
    SDP_ATTR_VERSION_NUM_LIST			= 0x0200,
    SDP_ATTR_SVCDB_STATE			= 0x0201,

    SDP_ATTR_SERVICE_VERSION			= 0x0300,
    SDP_ATTR_EXTERNAL_NETWORK			= 0x0301,
    SDP_ATTR_SUPPORTED_DATA_STORES_LIST		= 0x0301,
    SDP_ATTR_FAX_CLASS1_SUPPORT			= 0x0302,
    SDP_ATTR_REMOTE_AUDIO_VOLUME_CONTROL	= 0x0302,
    SDP_ATTR_FAX_CLASS20_SUPPORT		= 0x0303,
    SDP_ATTR_SUPPORTED_FORMATS_LIST		= 0x0303,
    SDP_ATTR_FAX_CLASS2_SUPPORT			= 0x0304,
    SDP_ATTR_AUDIO_FEEDBACK_SUPPORT		= 0x0305,
    SDP_ATTR_NETWORK_ADDRESS			= 0x0306,
    SDP_ATTR_WAP_GATEWAY			= 0x0307,
    SDP_ATTR_HOMEPAGE_URL			= 0x0308,
    SDP_ATTR_WAP_STACK_TYPE			= 0x0309,
    SDP_ATTR_SECURITY_DESC			= 0x030a,
    SDP_ATTR_NET_ACCESS_TYPE			= 0x030b,
    SDP_ATTR_MAX_NET_ACCESSRATE			= 0x030c,
    SDP_ATTR_IP4_SUBNET				= 0x030d,
    SDP_ATTR_IP6_SUBNET				= 0x030e,
    SDP_ATTR_SUPPORTED_CAPABILITIES		= 0x0310,
    SDP_ATTR_SUPPORTED_FEATURES			= 0x0311,
    SDP_ATTR_SUPPORTED_FUNCTIONS		= 0x0312,
    SDP_ATTR_TOTAL_IMAGING_DATA_CAPACITY	= 0x0313,
    SDP_ATTR_SUPPORTED_REPOSITORIES		= 0x0314,

    /* PnP Information */
    SDP_ATTR_SPECIFICATION_ID			= 0x0200,
    SDP_ATTR_VENDOR_ID				= 0x0201,
    SDP_ATTR_PRODUCT_ID				= 0x0202,
    SDP_ATTR_VERSION				= 0x0203,
    SDP_ATTR_PRIMARY_RECORD			= 0x0204,
    SDP_ATTR_VENDOR_ID_SOURCE			= 0x0205,

    /* BT HID */
    SDP_ATTR_DEVICE_RELEASE_NUMBER		= 0x0200,
    SDP_ATTR_PARSER_VERSION			= 0x0201,
    SDP_ATTR_DEVICE_SUBCLASS			= 0x0202,
    SDP_ATTR_COUNTRY_CODE			= 0x0203,
    SDP_ATTR_VIRTUAL_CABLE			= 0x0204,
    SDP_ATTR_RECONNECT_INITIATE			= 0x0205,
    SDP_ATTR_DESCRIPTOR_LIST			= 0x0206,
    SDP_ATTR_LANG_ID_BASE_LIST			= 0x0207,
    SDP_ATTR_SDP_DISABLE			= 0x0208,
    SDP_ATTR_BATTERY_POWER			= 0x0209,
    SDP_ATTR_REMOTE_WAKEUP			= 0x020a,
    SDP_ATTR_PROFILE_VERSION			= 0x020b,
    SDP_ATTR_SUPERVISION_TIMEOUT		= 0x020c,
    SDP_ATTR_NORMALLY_CONNECTABLE		= 0x020d,
    SDP_ATTR_BOOT_DEVICE			= 0x020e,
};

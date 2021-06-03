/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (C) 2021 OpenSynergy GmbH
 */
#ifndef VIRTIO_SND_IF_H
#define VIRTIO_SND_IF_H

#include "standard-headers/linux/virtio_types.h"

/*******************************************************************************
 * CONFIGURATION SPACE
 */
struct virtio_snd_config {
	/* # of available physical jacks */
	uint32_t jacks;
	/* # of available PCM streams */
	uint32_t streams;
	/* # of available channel maps */
	uint32_t chmaps;
};

enum {
	/* device virtqueue indexes */
	VIRTIO_SND_VQ_CONTROL = 0,
	VIRTIO_SND_VQ_EVENT,
	VIRTIO_SND_VQ_TX,
	VIRTIO_SND_VQ_RX,
	/* # of device virtqueues */
	VIRTIO_SND_VQ_MAX
};

/*******************************************************************************
 * COMMON DEFINITIONS
 */

/* supported dataflow directions */
enum {
	VIRTIO_SND_D_OUTPUT = 0,
	VIRTIO_SND_D_INPUT
};

enum {
	/* jack control request types */
	VIRTIO_SND_R_JACK_INFO = 1,
	VIRTIO_SND_R_JACK_REMAP,

	/* PCM control request types */
	VIRTIO_SND_R_PCM_INFO = 0x0100,
	VIRTIO_SND_R_PCM_SET_PARAMS,
	VIRTIO_SND_R_PCM_PREPARE,
	VIRTIO_SND_R_PCM_RELEASE,
	VIRTIO_SND_R_PCM_START,
	VIRTIO_SND_R_PCM_STOP,

	/* channel map control request types */
	VIRTIO_SND_R_CHMAP_INFO = 0x0200,

	/* jack event types */
	VIRTIO_SND_EVT_JACK_CONNECTED = 0x1000,
	VIRTIO_SND_EVT_JACK_DISCONNECTED,

	/* PCM event types */
	VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED = 0x1100,
	VIRTIO_SND_EVT_PCM_XRUN,

	/* common status codes */
	VIRTIO_SND_S_OK = 0x8000,
	VIRTIO_SND_S_BAD_MSG,
	VIRTIO_SND_S_NOT_SUPP,
	VIRTIO_SND_S_IO_ERR
};

/* common header */
struct virtio_snd_hdr {
	uint32_t code;
};

/* event notification */
struct virtio_snd_event {
	/* VIRTIO_SND_EVT_XXX */
	struct virtio_snd_hdr hdr;
	/* optional event data */
	uint32_t data;
};

/* common control request to query an item information */
struct virtio_snd_query_info {
	/* VIRTIO_SND_R_XXX_INFO */
	struct virtio_snd_hdr hdr;
	/* item start identifier */
	uint32_t start_id;
	/* item count to query */
	uint32_t count;
	/* item information size in bytes */
	uint32_t size;
};

/* common item information header */
struct virtio_snd_info {
	/* function group node id (High Definition Audio Specification 7.1.2) */
	uint32_t hda_fn_nid;
};

/*******************************************************************************
 * JACK CONTROL MESSAGES
 */
struct virtio_snd_jack_hdr {
	/* VIRTIO_SND_R_JACK_XXX */
	struct virtio_snd_hdr hdr;
	/* 0 ... virtio_snd_config::jacks - 1 */
	uint32_t jack_id;
};

/* supported jack features */
enum {
	VIRTIO_SND_JACK_F_REMAP = 0
};

struct virtio_snd_jack_info {
	/* common header */
	struct virtio_snd_info hdr;
	/* supported feature bit map (1 << VIRTIO_SND_JACK_F_XXX) */
	uint32_t features;
	/* pin configuration (High Definition Audio Specification 7.3.3.31) */
	uint32_t hda_reg_defconf;
	/* pin capabilities (High Definition Audio Specification 7.3.4.9) */
	uint32_t hda_reg_caps;
	/* current jack connection status (0: disconnected, 1: connected) */
	uint8_t connected;

	uint8_t padding[7];
};

/* jack remapping control request */
struct virtio_snd_jack_remap {
	/* .code = VIRTIO_SND_R_JACK_REMAP */
	struct virtio_snd_jack_hdr hdr;
	/* selected association number */
	uint32_t association;
	/* selected sequence number */
	uint32_t sequence;
};

/*******************************************************************************
 * PCM CONTROL MESSAGES
 */
struct virtio_snd_pcm_hdr {
	/* VIRTIO_SND_R_PCM_XXX */
	struct virtio_snd_hdr hdr;
	/* 0 ... virtio_snd_config::streams - 1 */
	uint32_t stream_id;
};

/* supported PCM stream features */
enum {
	VIRTIO_SND_PCM_F_SHMEM_HOST = 0,
	VIRTIO_SND_PCM_F_SHMEM_GUEST,
	VIRTIO_SND_PCM_F_MSG_POLLING,
	VIRTIO_SND_PCM_F_EVT_SHMEM_PERIODS,
	VIRTIO_SND_PCM_F_EVT_XRUNS
};

/* supported PCM sample formats */
enum {
	/* analog formats (width / physical width) */
	VIRTIO_SND_PCM_FMT_IMA_ADPCM = 0,	/*  4 /  4 bits */
	VIRTIO_SND_PCM_FMT_MU_LAW,		/*  8 /  8 bits */
	VIRTIO_SND_PCM_FMT_A_LAW,		/*  8 /  8 bits */
	VIRTIO_SND_PCM_FMT_S8,			/*  8 /  8 bits */
	VIRTIO_SND_PCM_FMT_U8,			/*  8 /  8 bits */
	VIRTIO_SND_PCM_FMT_S16,			/* 16 / 16 bits */
	VIRTIO_SND_PCM_FMT_U16,			/* 16 / 16 bits */
	VIRTIO_SND_PCM_FMT_S18_3,		/* 18 / 24 bits */
	VIRTIO_SND_PCM_FMT_U18_3,		/* 18 / 24 bits */
	VIRTIO_SND_PCM_FMT_S20_3,		/* 20 / 24 bits */
	VIRTIO_SND_PCM_FMT_U20_3,		/* 20 / 24 bits */
	VIRTIO_SND_PCM_FMT_S24_3,		/* 24 / 24 bits */
	VIRTIO_SND_PCM_FMT_U24_3,		/* 24 / 24 bits */
	VIRTIO_SND_PCM_FMT_S20,			/* 20 / 32 bits */
	VIRTIO_SND_PCM_FMT_U20,			/* 20 / 32 bits */
	VIRTIO_SND_PCM_FMT_S24,			/* 24 / 32 bits */
	VIRTIO_SND_PCM_FMT_U24,			/* 24 / 32 bits */
	VIRTIO_SND_PCM_FMT_S32,			/* 32 / 32 bits */
	VIRTIO_SND_PCM_FMT_U32,			/* 32 / 32 bits */
	VIRTIO_SND_PCM_FMT_FLOAT,		/* 32 / 32 bits */
	VIRTIO_SND_PCM_FMT_FLOAT64,		/* 64 / 64 bits */
	/* digital formats (width / physical width) */
	VIRTIO_SND_PCM_FMT_DSD_U8,		/*  8 /  8 bits */
	VIRTIO_SND_PCM_FMT_DSD_U16,		/* 16 / 16 bits */
	VIRTIO_SND_PCM_FMT_DSD_U32,		/* 32 / 32 bits */
	VIRTIO_SND_PCM_FMT_IEC958_SUBFRAME	/* 32 / 32 bits */
};

/* supported PCM frame rates */
enum {
	VIRTIO_SND_PCM_RATE_5512 = 0,
	VIRTIO_SND_PCM_RATE_8000,
	VIRTIO_SND_PCM_RATE_11025,
	VIRTIO_SND_PCM_RATE_16000,
	VIRTIO_SND_PCM_RATE_22050,
	VIRTIO_SND_PCM_RATE_32000,
	VIRTIO_SND_PCM_RATE_44100,
	VIRTIO_SND_PCM_RATE_48000,
	VIRTIO_SND_PCM_RATE_64000,
	VIRTIO_SND_PCM_RATE_88200,
	VIRTIO_SND_PCM_RATE_96000,
	VIRTIO_SND_PCM_RATE_176400,
	VIRTIO_SND_PCM_RATE_192000,
	VIRTIO_SND_PCM_RATE_384000
};

struct virtio_snd_pcm_info {
	/* common header */
	struct virtio_snd_info hdr;
	/* supported feature bit map (1 << VIRTIO_SND_PCM_F_XXX) */
	uint32_t features;
	/* supported sample format bit map (1 << VIRTIO_SND_PCM_FMT_XXX) */
	uint64_t formats;
	/* supported frame rate bit map (1 << VIRTIO_SND_PCM_RATE_XXX) */
	uint64_t rates;
	/* dataflow direction (VIRTIO_SND_D_XXX) */
	uint8_t direction;
	/* minimum # of supported channels */
	uint8_t channels_min;
	/* maximum # of supported channels */
	uint8_t channels_max;

	uint8_t padding[5];
};

/* set PCM stream format */
struct virtio_snd_pcm_set_params {
	/* .code = VIRTIO_SND_R_PCM_SET_PARAMS */
	struct virtio_snd_pcm_hdr hdr;
	/* size of the hardware buffer */
	uint32_t buffer_bytes;
	/* size of the hardware period */
	uint32_t period_bytes;
	/* selected feature bit map (1 << VIRTIO_SND_PCM_F_XXX) */
	uint32_t features;
	/* selected # of channels */
	uint8_t channels;
	/* selected sample format (VIRTIO_SND_PCM_FMT_XXX) */
	uint8_t format;
	/* selected frame rate (VIRTIO_SND_PCM_RATE_XXX) */
	uint8_t rate;

	uint8_t padding;
};

/*******************************************************************************
 * PCM I/O MESSAGES
 */

/* I/O request header */
struct virtio_snd_pcm_xfer {
	/* 0 ... virtio_snd_config::streams - 1 */
	uint32_t stream_id;
};

/* I/O request status */
struct virtio_snd_pcm_status {
	/* VIRTIO_SND_S_XXX */
	uint32_t status;
	/* current device latency */
	uint32_t latency_bytes;
};

/*******************************************************************************
 * CHANNEL MAP CONTROL MESSAGES
 */
struct virtio_snd_chmap_hdr {
	/* VIRTIO_SND_R_CHMAP_XXX */
	struct virtio_snd_hdr hdr;
	/* 0 ... virtio_snd_config::chmaps - 1 */
	uint32_t chmap_id;
};

/* standard channel position definition */
enum {
	VIRTIO_SND_CHMAP_NONE = 0,	/* undefined */
	VIRTIO_SND_CHMAP_NA,		/* silent */
	VIRTIO_SND_CHMAP_MONO,		/* mono stream */
	VIRTIO_SND_CHMAP_FL,		/* front left */
	VIRTIO_SND_CHMAP_FR,		/* front right */
	VIRTIO_SND_CHMAP_RL,		/* rear left */
	VIRTIO_SND_CHMAP_RR,		/* rear right */
	VIRTIO_SND_CHMAP_FC,		/* front center */
	VIRTIO_SND_CHMAP_LFE,		/* low frequency (LFE) */
	VIRTIO_SND_CHMAP_SL,		/* side left */
	VIRTIO_SND_CHMAP_SR,		/* side right */
	VIRTIO_SND_CHMAP_RC,		/* rear center */
	VIRTIO_SND_CHMAP_FLC,		/* front left center */
	VIRTIO_SND_CHMAP_FRC,		/* front right center */
	VIRTIO_SND_CHMAP_RLC,		/* rear left center */
	VIRTIO_SND_CHMAP_RRC,		/* rear right center */
	VIRTIO_SND_CHMAP_FLW,		/* front left wide */
	VIRTIO_SND_CHMAP_FRW,		/* front right wide */
	VIRTIO_SND_CHMAP_FLH,		/* front left high */
	VIRTIO_SND_CHMAP_FCH,		/* front center high */
	VIRTIO_SND_CHMAP_FRH,		/* front right high */
	VIRTIO_SND_CHMAP_TC,		/* top center */
	VIRTIO_SND_CHMAP_TFL,		/* top front left */
	VIRTIO_SND_CHMAP_TFR,		/* top front right */
	VIRTIO_SND_CHMAP_TFC,		/* top front center */
	VIRTIO_SND_CHMAP_TRL,		/* top rear left */
	VIRTIO_SND_CHMAP_TRR,		/* top rear right */
	VIRTIO_SND_CHMAP_TRC,		/* top rear center */
	VIRTIO_SND_CHMAP_TFLC,		/* top front left center */
	VIRTIO_SND_CHMAP_TFRC,		/* top front right center */
	VIRTIO_SND_CHMAP_TSL,		/* top side left */
	VIRTIO_SND_CHMAP_TSR,		/* top side right */
	VIRTIO_SND_CHMAP_LLFE,		/* left LFE */
	VIRTIO_SND_CHMAP_RLFE,		/* right LFE */
	VIRTIO_SND_CHMAP_BC,		/* bottom center */
	VIRTIO_SND_CHMAP_BLC,		/* bottom left center */
	VIRTIO_SND_CHMAP_BRC		/* bottom right center */
};

/* maximum possible number of channels */
#define VIRTIO_SND_CHMAP_MAX_SIZE	18

struct virtio_snd_chmap_info {
	/* common header */
	struct virtio_snd_info hdr;
	/* dataflow direction (VIRTIO_SND_D_XXX) */
	uint8_t direction;
	/* # of valid channel position values */
	uint8_t channels;
	/* channel position values (VIRTIO_SND_CHMAP_XXX) */
	uint8_t positions[VIRTIO_SND_CHMAP_MAX_SIZE];
};

#endif /* VIRTIO_SND_IF_H */

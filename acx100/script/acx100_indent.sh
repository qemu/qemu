#!/bin/bash

INDENT=`which indent`

if [ -z $INDENT ]
then
	echo GNU indent not found in \$PATH. Install it and come back.
	exit 1
fi

$INDENT -nbad -bap -bbo -nbc -br -brs -ncdb -ce -ci4 -cli0 -ncs -d0 -di1 -nfc1 \
-nfca -hnl -i8 -ip0 -l80 -lp -npcs -nprs -npsl -saf -sai -saw -sbi0 -nsc -nsob \
-nss -ts8 -T acx100_t -T acx100_metacmd_t -T firmware_image_t -T mac_t \
-T memmap_t -T p80211hdr_t -T wlan_fr_assocreq_t -T wlan_fr_assocresp_t \
-T wlan_fr_authen_t -T wlan_fr_beacon_t -T wlan_fr_deauthen_t \
-T wlan_fr_disassoc_t -T wlan_fr_ibssatim_t -T wlan_fr_probereq_t \
-T wlan_fr_proberesp_t -T wlan_fr_reassocreq_t -T wlan_fr_reassocresp_t \
-T wlandevice_t -T TIWLAN_DC -T UINT -T UINT8 -T UINT16 -T UINT32 $*

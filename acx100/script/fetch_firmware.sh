#!/bin/sh

DL_DIR=/tmp/acx100_driver_download
ACX100_FILE_LOCATIONS="
ftp://ftp.dlink.com/Wireless/dwl520+/Driver/dwl520+_drivers_307.zip
ftp://ftp.dlink.co.uk/wireless/dwl-520+/dwl-520+_drv_v3.06_1007_inc_utility.zip
ftp://ftp.dlink.de/dwl-products/dwl-520PLUS/Treiber_Firmware/dwl520+_driver_eng_3.07.zip
"
ACX111_FILE_LOCATIONS="
ftp://ftp.dlink.co.uk/wireless/dwl-g650+_rev_Ax/dwl-g650+_drv_v1.0.zip
ftp://ftp.dlink.de/dwl-products/dwl-g650PLUS/Treiber_Firmware/dwlg650plus_WPA-utility-driver_2.02.zip
ftp://ftp.dlink.co.uk/wireless/dwl-g650+_rev_Ax/dwl-g650+_rev_ax_drv_v204.zip
" # v204 untested (may not work), thus at end of list

find_driver_dir()
{
  ACXDIR=`pwd`
  [ -f $ACXDIR/scripts/start_nets.sh ] && return
  if [ -f $ACXDIR/../scripts/start_net.sh ]; then
    ACXDIR="$ACXDIR/.."
    return
  fi
  echo "Couldn't determine base directory of ACX1xx driver, ABORTING!"
  echo "Please restart this script from the driver's root directory!"
  exit
}

find_card()
{
  LSPCI=`which lspci`
  IDS_ACX100="104c:8400 104c:8401"
  IDS_ACX111="104c:9066"

  echo
  echo "Searching for ACX1xx cards on this system..."
  let CARD_TYPE=0
  if [ -z "$LSPCI" ]; then
    echo "lspci not found! (package pciutils): Cannot determine wireless card type!"
    let CARD_TYPE=0
    return
  fi
  LSPCI_OUT=`${LSPCI} -n`
  for card in $IDS_ACX100; do
    if [ -n "`echo $LSPCI_OUT|grep $card`" ]; then
      echo "DETECTED ACX100-based wireless card!"
      let CARD_TYPE=1
    fi
  done
  for card in $IDS_ACX111; do
    if [ -n "`echo $LSPCI_OUT|grep $card`" ]; then
      echo "DETECTED ACX111-based wireless card!"
      let CARD_TYPE=2
    fi
  done
  if [ $CARD_TYPE -eq 0 ]; then
    echo "COULD NOT DETECT any ACX100- or ACX111-based wireless cards on this system."
  fi
}

find_downloader()
{
  echo
  echo "Locating a suitable download tool..."
  WGET=`which wget`
  if test -n $WGET; then
    DL_STRING="$WGET -c -t 3 -T 20 --passive-ftp"
    return
  fi
  CURL=`which curl`
  if test -n $CURL; then
    DL_STRING="$CURL -0 --connect-timeout 20"
    return
  fi
  SNARF=`which snarf`
  if test -n $SNARF; then
    DL_STRING="$SNARF"
    return
  fi
  echo "None of the download tools wget, curl or snarf found on the system:"
  echo "Cannot download a driver package containing firmware files, ABORTING!"
  echo "Please report!!!"
  exit 1
}

ask_user()
{
  echo
  echo Which firmware files package would you like to download?
  echo
  echo "a) for ACX100 (TNETW1100) chipset based cards"
  echo "b) for ACX111 (TNETW1130/1230) chipset based cards"
  echo "c) for both chipsets"
  echo "d) none"
  echo -n "> "
  read choice
  case "$choice" in
	a|A)
	let CARD_TYPE=1
	;;
	b|B)
	let CARD_TYPE=2
	;;
	c|C)
	let CARD_TYPE=0
	;;
	d|D)
	let CARD_TYPE=255
	;;
	*)
	echo "Invalid choice, ABORTING!"
	exit
  esac
}

download_files()
{
  mkdir -p $DL_DIR

  echo "Please let me know immediately when a download link doesn't exist any more! (in the latest version of this driver) andi@lisas.de"
  pushd $DL_DIR 1>/dev/null
    if [ $CARD_TYPE -lt 2 ]; then # 0 or 1
      echo "Downloading ACX100 firmware package..."
      for site in $ACX100_FILE_LOCATIONS; do
	FILE="`basename $site`"
	${DL_STRING} "$site"
	[ -f "$FILE" ] && break
      done
    fi
    if [ $CARD_TYPE -eq 0 -o $CARD_TYPE -eq 2 ]; then
      echo "Downloading ACX111 firmware package..."
      for site in $ACX111_FILE_LOCATIONS; do
	FILE="`basename $site`"
	${DL_STRING} "$site"
	[ -f "$FILE" ] && break
      done
    fi
  popd 1>/dev/null
}

extract_firmware()
{
  UNZIP=`which unzip`
  if [ -z "$UNZIP" ]; then
    # FIXME: gzip is rumoured to be semi-compatible with .zip. Usable?
    echo "COULD NOT LOCATE required utility unzip: unable to extract firmware files, ABORTING unpacking (files left in $DL_DIR)!"
    exit
  fi
  pushd $DL_DIR 1>/dev/null
    for file in `find . -iname "*.zip"`; do
      echo -n "Extracting driver file $file..."
      ${UNZIP} -u $file
      echo " Done."
    done
    mkdir -p "$ACXDIR/firmware"
    if [ $CARD_TYPE -lt 2 ]; then
      for files in RADIO0d.BIN RADIO11.BIN RADIO15.BIN WLANGEN.bin; do
	FILE="`find . -name "$files"|tail -n 1`"
	# need to convert to uppercase
	if [ -n "`echo "$FILE"|grep WLANGEN.bin`" ]; then
	  cp "$FILE" "$ACXDIR"/firmware/WLANGEN.BIN
	else
	  cp "$FILE" "$ACXDIR"/firmware/
	fi
      done
    fi
    if [ $CARD_TYPE -ne 1 ]; then
      for files in TIACX111.BIN; do
	FILE="`find . -name "$files"|tail -n 1`"
	cp "$FILE" "$ACXDIR"/firmware/
      done
    fi
  popd 1>/dev/null
}

# --- main ---

find_driver_dir

find_downloader

find_card

ask_user

download_files

extract_firmware

echo "FINISHED! (hopefully!) If something failed, then please report it!"
echo
echo "Please also note that these firmware files may not work with your"
echo "particular card! In this case try getting different firmware files for"
echo "your chipset (preferrably ones provided by the vendor of your card)."

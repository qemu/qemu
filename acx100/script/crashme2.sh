#!/bin/sh

# use this script to do severe and brutal crash testing.
# and make sure to load the driver AFTER having started the script,
# and also make sure to eject/insert the card multiple times for even more fun...
# all that's left for me to say is: Good luck! And don't forget to save your work! ;-))

echo Run this script in one terminal, then load the driver module in another one.
echo
echo I will delay for 10 seconds now. You may use this time to abort or switch to a logging console...
sleep 10

IFACE=wlan0
IWC="/sbin/iwconfig $IFACE"
IFC="/sbin/ifconfig $IFACE"
USE_RANDOM=0

# With logging, sleep and sync
logger=logger
sleep="sleep 1"
sync=sync
# ..or without
logger="#off"
sleep="#off"
sync="#off"

while true; do
  let IDX=RANDOM%8+1
  case "$IDX" in
    1)
      CMD="$IWC mode Managed"
    ;;
    2)
      CMD="$IWC mode Ad-Hoc"
    ;;
    3)
      if [ $USE_RANDOM = 1 ]; then
	let LEN=RANDOM/500
	echo getting $LEN bytes random data
	ESSID=`dd if=/dev/urandom bs=1 count=$LEN`
      else
	ESSID=sdfkjgsdfdaSD/cSZDFgdlkrdtjhfacklsjczxc/vb.x?FG?
      fi
      CMD="$IWC essid $ESSID"
    ;;
    4)
      if [ $USE_RANDOM = 1 ]; then
	let LEN=RANDOM/500
	echo getting $LEN bytes random data
	NICK=`dd if=/dev/urandom bs=1 count=$LEN`
      else
	NICK=sdfkjgsdfdaSD/cSZDFgdlkrdtjhfacklsjczxc/vb.x?FG?
      fi
      CMD="$IWC essid $NICK"
    ;;
    5)
      let CHAN=RANDOM/500
      CMD="$IWC channel $CHAN"
    ;;
    6)
      let RATE=RANDOM/1000
      CMD="$IWC rate ${RATE}M"
    ;;
    7)
      CMD="$IFC up"
    ;;
    8)
      CMD="$IFC down"
    ;;
    *)
      echo BUG in $0!
      exit
    ;;
  esac

  eval $logger "probing: $CMD"
  eval $sleep
  eval $sync
  echo "probing: $CMD"
  $CMD
  echo "...exit code:$?"
done 2>&1 | tee crashme.$$.log

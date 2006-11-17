#!/bin/bash

#########################################
# start_net script                      #
# acx100 project                        #
# acx100.sourceforge.net                #
# edited by arnie <urnotwelcome@gmx.de> #
#########################################
# with modifications by craig           #
# summary at end of file                #
#########################################

# Please edit below

# syntax is: VARIABLENAME=VALUE, with _no_ spaces in between
# make sure to _preserve_ any double-quotes (")
# text beginning with the comment delimiter (#) is ignored
# make sure to _preserve_ at least one space before any
# comment delimiters (#) that do not begin a line
# "uncommenting" a line means to remove it's leading "#" character

ESSID="network_down" # THIS IS CASE SeNsItIvE!! any == associate to any ESSID
# Default rate configured as 11Mbps to not cause speed problems (while
# using auto rate) or connection problems (while not using auto rate)
# with non-22Mbps hardware...
RATE=11M
AUTORATE=1 # only disable auto rate if you know what you're doing...
CHAN=1 # it's useful to try to stick to channels 1, 6 or 11 only, since these don't overlap with other channels
#SHORTPREAMBLE=1 # set a value of 1 in order to force "Short Preamble" (incompatible with very old WLAN hardware!) instead of peer autodetect
#TXPOWER=20 # 0..20 (dBm) (18dBm is firmware default) overly large setting might perhaps destroy your radio eventually!
MODE=Managed # Managed for infrastructure, Ad-hoc for peer-to-peer. NOTE: Auto mode is not supported any more, you HAVE to select a specific mode!
DEBUG=0xb # 0xffff for maximum debug info, 0 for none

# WEP Key(s)
# ascii keys (passphrase) should look like this: KEY="s:asciikey"
# hex keys should look like this: KEY="4378c2f43a"

# most wep users will want to use this line
KEY=""

# alternatively, you can uncomment and use these lines to
# set all 4 possible WEP keys
#KEY1="1234567890"  #WEP64
#KEY2="1234567890"
#KEY3="1234567890"
#KEY4="1234567890"
# you must select which of the 4 keys above to use here:
#KEY="[1]" # for KEY1, "[2]" for KEY2, etc

ALG=open # open == Open System, restricted == Shared Key

#IP address

USE_DHCP=0 # set to 1 for auto configuration instead of fixed IP setting

IP=192.168.1.98 # set this if you did not set USE_DHCP=1
NETMASK=255.255.255.0 # set this if you did not set USE_DHCP=1
GATEWAY=192.168.1.254 # set this if you did not set USE_DHCP=1

LED_OFF=1 # set to 1 to turn off the power LED to save power

MTU_576=0 # set to 1 if you have buffer management problems

# DO NOT EDIT BELOW THIS LINE
##################################################################


if test "$UID" != "0"; then echo "You are not root. To insert the module into your kernel, you need to be root. Enter su and try again. Bailing..."; exit 1; fi

SYNC=`which sync`
INSMOD=`which insmod`
MODPROBE=`which modprobe`
IFCONF=`which ifconfig`
IWCONF=`which iwconfig`
IWPRIV=`which iwpriv`
ROUTE=`which route`
SCRIPT_AT=`dirname $0`

# while we check for all 3, we run them in this
# "preferred" order: dhcpcd, pump, dhclient
# so if more than one exists it's ok

which dhcpcd &> /dev/null
if [ $? -eq 0 ]; then DHCPCD=`which dhcpcd`; fi

which pump &> /dev/null
if [ $? -eq 0 ]; then PUMP=`which pump`; fi

which dhclient &> /dev/null
if [ $? -eq 0 ]; then DHCLIENT=`which dhclient`; fi

if test -z "$SYNC"; then echo "sync not found. Go get a sane Linux system. Bailing..."; exit 1; fi
if test -z "$INSMOD"; then echo "insmod not found. Go get a sane Linux system. Bailing..."; exit 1; fi
if test -z "$IFCONF"; then echo "ifconfig not found. I can insert the module for you, but you won't be able to configure your interface."; CONTINUE=ASK; fi
if test -z "$IWCONF"; then echo "iwconfig not found. Make sure it is installed. The interface might work without, though."; CONTINUE=ASK; fi

if test -n "$CONTINUE"; then echo -n "Problems encountered. Do you want to continue? [n] "; read ANSWER
case $ANSWER in  ( y | Y | Yes | YES | yes | j | J | ja | Ja | JA ) ;;
		 ( * ) exit 1 ;;
esac
fi

case "`uname -r`" in
	2.4*)
		MODULE_AT="${SCRIPT_AT}/../src/acx_pci.o"
		;;
	*)
		MODULE_AT="${SCRIPT_AT}/../src/acx_pci.ko"
		;;
esac

if test ! -r "$MODULE_AT"; then echo "Module not found or not readable.
Have you built it? This script expects it to be at ../src/acx_pci.[k]o, relative to the script's location. Bailing..."; exit 1; fi

# check whether any file name of the required main firmware file is
# available in the acx100/firmware or global firmware directory

# FIRMWARE_AT has to be given as an absolute path!!
for FIRMWARE_AT in "${SCRIPT_AT}/../firmware" "/usr/share/acx"; do
  for FW_FILE in WLANGEN.BIN TIACX111.BIN FwRad16.bin FW1130.BIN; do
    if test -r "$FIRMWARE_AT/$FW_FILE"; then
      #echo A firmware file has been found at "$FIRMWARE_AT/$FW_FILE"
      FW_FOUND=1
      break 2
    fi
  done
done

if test "$FW_FOUND" != "1"; then
  echo "Firmware not found or not readable. Have you placed it in the firmware directory or run make extract_firmware once? This script expects it to be either at ../firmware/WLANGEN.BIN (or ../firmware/TIACX111.BIN for the ACX111 chip), relative to the script's location, OR have you placed it in the default directory:/usr/share/acx?. Bailing...";
  exit 1;
fi

if test $AUTORATE != "1"; then
  if test "$RATE" != "11M"; then echo "Transfer rate is not set to 11
  Mbps, but $RATE (and not using auto rate either). If something doesn't work, try 11 Mbps or auto rate."; fi
fi

test "$AUTORATE" = "1" && AUTO=auto || AUTO=

# for better debugging
# set -x
#echo 8 > /proc/sys/kernel/printk


# just in case ;)
$SYNC

sleep 1

DEV=wlan0 # this may become wlan1, wlan2, etc depending on if any other wlanX devices are found

if test -n "`lsmod | grep acx_pci`"; then ${SCRIPT_AT}/stop_net;fi

# now that the interface is "down", let's also check for and remove
# those *really* old modules that might be supplied with some distributions

lsmod | grep acx100_pci &> /dev/null
if test "$?" = "0"; then
	rmmod acx100_pci;
	echo "NOTICE: Found a very old version of the driver loaded (acx100_pci), removing."
	# we could also add the old module's name to the blacklist, though this
	# only benefits hotplug devices and might be considered invasive
	# echo acx100_pci >> /etc/hotplug/blacklist
fi

# before inserting the module, let's check for the presence of existing wlan devices
# and if necessary, adjust our $DEV variable to be wlan1, wlan2, etc.
# these could be other wireless drivers' devs, or the device created by acx_usb

MAX_WLANS=9 # failsafe break counter
while true
do
	# at this point $DEV is always "wlan0"
	$IFCONF $DEV &> /dev/null
	if test "$?" = "0"; then
		echo -n "$DEV exists, "
		DIGIT=`echo $DEV | cut -d 'n' -f 2`
		DIGIT=`expr $DIGIT + 1`
		DEV="wlan${DIGIT}"
		echo -n "trying $DEV..."
	else
		echo "using $DEV."
		break
	fi

	# failsafe break
	if test $MAX_WLANS -eq 0; then break;fi
	MAX_WLANS=`expr $MAX_WLANS - 1`
done

$MODPROBE -q firmware_class # Linux 2.6.x hotplug firmware loading support
$INSMOD $MODULE_AT debug=$DEBUG firmware_dir=$FIRMWARE_AT
if test "$?" = "0"; then echo "Module successfully inserted."; else echo "Error while inserting module! Bailing..."; exit 1; fi


# before we get too involved in trying to setup $DEV, let's verify that it exists
$IFCONF $DEV &> /dev/null
if test "$?" = "0"; then # $DEV exists

	if test -n "$IWCONF"; then

		if test -n "$RATE"; then
		echo Setting rate to $RATE $AUTO.
		$IWCONF $DEV rate $RATE $AUTO
		test "$?" != "0" && echo Failed.
		fi
		if test -n "$CHAN"; then
		echo Setting channel $CHAN.
		$IWCONF $DEV channel $CHAN
		test "$?" != "0" && echo Failed.
		fi
		if test -n "$SHORTPREAMBLE"; then
		echo Setting short preamble to $SHORTPREAMBLE.
		$IWPRIV $DEV SetSPreamble $SHORTPREAMBLE
		test "$?" != "0" && echo Failed.
		sleep 1
		fi
		if test -n "$TXPOWER"; then
		echo Setting Tx power level to $TXPOWER dBm.
		$IWCONF $DEV txpower $TXPOWER
		test "$?" != "0" && echo Failed.
		sleep 1
		fi

		echo Going to try to join or setup ESSID $ESSID.
		$IWCONF $DEV essid "$ESSID"
		test "$?" != "0" && echo Failed.

		if test -n "$MODE"; then
		echo Setting mode to $MODE.
		$IWCONF $DEV mode $MODE
		test "$?" != "0" && echo Failed.
		fi


		if test -n "$KEY1"; then
		echo Setting key 1 to $KEY1, algorithm $ALG.
		$IWCONF $DEV key $ALG "$KEY1" [1]
		test "$?" != "0" && echo Failed.
		fi

		if test -n "$KEY2"; then
		echo Setting key 2 to $KEY2, algorithm $ALG.
		$IWCONF $DEV key $ALG "$KEY2" [2]
		test "$?" != "0" && echo Failed.
		fi

		if test -n "$KEY3"; then
		echo Setting key 3 to $KEY3, algorithm $ALG.
		$IWCONF $DEV key $ALG "$KEY3" [3]
		test "$?" != "0" && echo Failed.
		fi

		if test -n "$KEY4"; then
		echo Setting key 4 to $KEY4, algorithm $ALG.
		$IWCONF $DEV key $ALG "$KEY4" [4]
		test "$?" != "0" && echo Failed.
		fi

		# this is now placed after the "KEY%D" stuff
		# to support the "KEY=[1]" option

		if test -n "$KEY"; then
		echo Setting key to $KEY, algorithm $ALG.
		$IWCONF $DEV key "$KEY" $ALG
		test "$?" != "0" && echo Failed.
		fi

	fi # end "if found(iwconfig)"

	# for notebook use - a power LED is sooo useless anyway ;-))
	if test "$LED_OFF" -eq 1; then
		test -n "$IWPRIV" && "$IWPRIV" $DEV SetLEDPower 0
		echo Setting power LED to off.
	fi

	# It shouldn't hurt to bring the device up, and dhcp seems to like it that way
	$IFCONF $DEV up
	sleep 1

	# if they want dhcp or they've set to managed mode, then we
	# take up to 10 seconds to wait for something to show up
	# in iwconfig besides zeros, we don't want to give the user
	# the wrong impression re: success/failure and mainly we don't
	# want to bother with a dhcp attempt without association
	# we could also use /proc/driver/acx_$DEV instead ??

	# check MODE for some form of the word "managed", case-insensitive
	echo $MODE | grep -ic managed &> /dev/null

	if test "$?" = "0" -o $USE_DHCP -eq 1; then # begin test for association
		WAIT_ASSOC=10
		echo -n "Waiting for association..."

		while true
		do
			echo -n "$WAIT_ASSOC "

			if test "`$IWCONF $DEV | grep -c 00:00:00:00:00:00`" = "0"; then
				echo "OK."

				# ok, have association, now verify that the card associated with
				# the desired AP, it could easily have found a stray linksys instead ;^}
				if test -n "$ESSID"; then
					echo "$ESSID" | grep -ic any &> /dev/null # don't bother checking "essid=any"
					if test "$?" = "0" -a "`$IWCONF $DEV | grep -c $ESSID`" = "0"; then
						echo "NOTICE: $DEV associated, but NOT with $ESSID!"
					fi
				fi
				break
			fi

			WAIT_ASSOC=`expr $WAIT_ASSOC - 1`

			if test "$WAIT_ASSOC" = "0"; then
				echo FAILED.
				# if they wanted dhcp, tell them the bad news
				if test $USE_DHCP -eq 1; then
					echo "Error: $DEV failed to associate, can't use DHCP for IP address."
					USE_DHCP=0;
				fi
				break
			fi

			# we *could* issue an iwconfig here at the end of each loop:
			# $IWCONF $DEV essid $ESSID
			# I'm not sure if it would help or hinder...it isn't necessary w/my hardware

			sleep 1 # give it a second
		done
	fi # end test for association, if mode=managed or USE_DHCP=1


	if test $USE_DHCP -eq 1; then
		# now we fetch an IP address from DHCP
		# first, try dhcpcd:
		if test -n "$DHCPCD"; then
			echo -n "Attempting to use $DHCPCD for DHCP, this may take a moment..."
			rm -f /etc/dhcpc/dhcpcd-$DEV.pid > /dev/null
			$DHCPCD -d $DEV -t 5 &> /dev/null
			if test "$?" = "0"; then
				echo "OK."
				echo "Interface has been set up successfully.";
			else echo "FAILED"
			fi
		# no dhcpcd was found, next we try pump:
		elif test -n "$PUMP"; then
			echo -n "Attempting to use $PUMP for DHCP, this may take a moment..."
			$PUMP -i $DEV &> /dev/null
			if test "$?" = "0"; then
				echo "OK."
				echo "Interface has been set up successfully.";
			else echo "FAILED"
			fi
		# no dhcpcd or pump was found, finally we try dhclient;
		elif test -n "$DHCLIENT"; then
			echo -n "Attempting to use $DHCLIENT for DHCP, this may take a moment..."
			rm -f /var/run/dhclient.pid
			$DHCLIENT $DEV &> /dev/null
			if test "$?" = "0"; then
				echo "OK."
				echo "Interface has been set up successfully.";
			else echo "FAILED"
			fi
		else # dhcpcd, pump, and dhclient not found, inform user and bail
			echo "ERROR: USE_DHCP=1 , but no dhcp clients could be found"
			echo "Bailing..."
			exit 1;
		fi #end check for usable dhcp client
	else # wants manual config
		# Hehe, this can be done after iwconfigs now :)
		$IFCONF $DEV $IP netmask $NETMASK
		if test "$?" != "0"; then
			echo "Error in \"$IFCONF $DEV $IP netmask $NETMASK\". Bailing..."; exit 1;
		else
			echo "Interface has been set up successfully.";
			test -n "$GATEWAY" && $ROUTE add default gw $GATEWAY $DEV
		fi
	fi # end if USE_DHCP=1

	# ugly workaround for buffer management problems
	if test "$MTU_576" -eq 1; then
		echo "Setting mtu down to 576. NOTE that e.g. IPv6 would need >= 1280, so make sure you're doing the right thing here!"
		test -n "$IFCONF" && "$IFCONF" $DEV mtu 576
		if test "$?" != "0"; then echo "Error in \"$IFCONF $DEV mtu 576\". Bailing..."; exit 1; fi
	fi

else # $DEV is not found by ifconfig
  echo "Error: Failed to create device: $DEV...bailing."
  exit 1;
fi # end test for $DEV exists



# just in case ;)
$SYNC

##############################################################
# summary of craig's changes to pf33's start_net:

# added SET_LED and MTU_576 vars, moved DEV below the do-not-edit line
# changed KEY0-KEY3 vars to KEY1-KEY4 to match iwconfig's scheme
# added a line for selecting a numbered key eg: "KEY=[1]"
# moved setting wep key to last in the order for above to work
# added attempt to automagically find/use a dhcp client
# added checking for firmware in /usr/share/acx before bailing
# check for and unload the old acx100_pci module if present
# don't assume $DEV is going to always be wlan0 (needs more work)
# don't assume that $DEV exists, even after a successful module load
# if MODE=managed || USE_DHCP=1, wait for association
# upon assoc, test for correct SSID if one was specified
# added $DEV to route add default gw command
##############################################################

# end of file

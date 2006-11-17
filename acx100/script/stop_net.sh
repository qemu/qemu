#!/bin/bash
DEV=wlan0

if test "$UID" != "0"; then echo "You are not root. To insert the module into your kernel, you need to be root. Enter su and try again. Bailing..."; exit 1; fi

IFCONF=`which ifconfig`
RMMOD=`which rmmod`

if test -z "$IFCONF"; then echo "Can't deconfigure interface, and likely not
unload module."; else $IFCONF $DEV down; fi

sleep 1
if test -z "$RMMOD"; then echo "rmmod not found. Go get a sane Linux system. Bailing..."; exit 1; fi
$RMMOD acx_pci
if test "$?" = "0"; then echo "$DEV deconfigured, module unloaded."; else echo "Module not unloaded, or wasn't loaded."; fi


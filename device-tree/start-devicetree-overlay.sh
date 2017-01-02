#!/bin/bash
# Run as root.
# What is happening here is well explained in Derek Molloy's excellent
# video http://www.youtube.com/watch?v=wui_wU1AeQc
##

VERBOSE=0
DT_VERSION=00A0

usage() {
    echo "Usage: $0 <dts-file>"
    echo "Available:"
    for f in $(ls `dirname $0`/*.dts) ; do
	echo "  $f"
    done
    exit 1
}

if [ $# -ne 1 ] ; then
    usage
fi

DTS_FILE=$1
if [[ ! $DTS_FILE =~ \.dts$ ]] ; then
    echo "This does not seem to be a *.dts file".
    usage
fi

# ... there must be a simpler way to extract this...
CAPE_NAME=$(sed 's/.*part-number\s*=\s*"\([^"]*\)".*/\1/p;d' < $DTS_FILE)

if [ -z "$CAPE_NAME" ] ; then
    echo "Didn't find any part-number in $DTS_FILE ?"
    exit
fi

DTBO_FILE=$(echo "$DTS_FILE" | sed "s/.dts$/-$DT_VERSION.dtbo/")

make $DTBO_FILE
if [ $? -ne 0 ] ; then
    echo "Failed to produce $DTBO_FILE"
fi

PINS=/sys/kernel/debug/pinctrl/44e10800.pinmux/pins
if [ -e /sys/devices/platform/bone_capemgr/slots ] ; then
	SLOTS=/sys/devices/platform/bone_capemgr/slots
else
	# 3.x way of doing things.
	SLOTS=/sys/devices/bone_capemgr.*/slots
fi

# Some dance around minimal tools available on the system. We get the offsets
# and add 44e10800 to it, so that we can grep these in the $PINS
OFFSETS_800=$(for f in $(cat $DTS_FILE | grep "^\s*0x" | awk '{print $1}') ; do \
                 printf "%0d" $f | awk '{printf("44e10%03x\n", $1 + 2048)}'; \
              done)

if [ $VERBOSE -ne 0 ] ; then
    echo "This is how these pins look before."
    for f in $OFFSETS_800 ; do
	grep $f $PINS
    done
fi

cp $DTBO_FILE /lib/firmware

echo
echo "Adding $CAPE_NAME overlay"
echo "$CAPE_NAME" > $SLOTS
cat $SLOTS

if [ $VERBOSE -ne 0 ] ; then
    echo
    echo "This is how these pins look afterwards."
    for f in $OFFSETS_800 ; do
	grep $f $PINS
    done
fi

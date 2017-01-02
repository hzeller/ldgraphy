#!/bin/bash
# This requires bb-customizations be installed
#    sudo apt-get install bb-customizations
#
# Essentially what we are doing is to embed the DTBO file in the initrd file, so that it
# is available right away on boot-up. That is the quickest way to enable the cape at boot-up
# short of compiling the device-tree file into the kernel.
##

set -e

DT_VERSION=00A0
UENV_FILE=/boot/uEnv.txt

usage() {
    echo "Usage: $0 <dts-file>"
    echo "Available:"
    for f in $(ls `dirname $0`/*/*.dts) ; do
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

# The initramfs script will pick up dtbos from /lib/firmware.
# Let's put it there.
cp $DTBO_FILE /lib/firmware

if uname -r | grep -q "^4" ; then
	MAGIC_ENABLE=cape_enable=bone_capemgr.enable_partno=
else
	MAGIC_ENABLE=cape_enable=capemgr.enable_partno=
fi

# First: set up UENV_FILE.
echo "* Setting up $UENV_FILE"
# Note, this only works properly if optargs is not set yet.
if grep "^${MAGIC_ENABLE}.*${CAPE_NAME}" $UENV_FILE > /dev/null ; then
    echo "  * Already configured in $UENV_FILE"
else
    if grep "^$MAGIC_ENABLE" $UENV_FILE > /dev/null ; then
        echo "  * Adding $CAPE_NAME to cape-enable line in $UENV_FILE"
	sed -i "s/^${MAGIC_ENABLE}.*/\0,${CAPE_NAME}/g" $UENV_FILE
    else
        echo "  * Adding cape-enable line to $UENV_FILE"
	echo "${MAGIC_ENABLE}${CAPE_NAME}" >> $UENV_FILE
    fi
fi

update-initramfs -tu -k `uname -r`

sync

echo "Now reboot for the cape to be initialized at boot time"

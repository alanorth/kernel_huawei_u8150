#!/bin/bash

DATE=`date +%Y%m%d`

function setenv {
	echo -n "Setting ARM environment..."
	export ARCH=arm
	export CROSS_COMPILE=~/src/android-toolchain-eabi-linaro-4.6-2011.08/bin/arm-eabi-
	export CFLAGS="-O2 -Os -floop-interchange -floop-strip-mine -floop-block"
	echo " done."

	echo -n "Setting other environment variables..."
	# the number of CPUs to use when compiling the kernel (auto detect all available)
	export CPUS=`grep -c processor /proc/cpuinfo`
	# grab the localversion so we can append it to the boot image
	let RELVER=`cat .version`+1
	echo " done."
}

function mkinitramfs {
	echo "Zipping ramdisk..."
	release/mkbootfs release/boot.img-ramdisk | lzop > release/boot.img-ramdisk.cpio.lzo
}

function mkbootimg {
		echo "Creating boot.img..."
		release/mkbootimg-U8150 --cmdline 'mem=211M console=ttyMSM2,115200n8 androidboot.hardware=huawei console=ttyUSBCONSOLE0 androidboot.console=ttyUSBCONSOLE0' --kernel arch/arm/boot/zImage --ramdisk release/boot.img-ramdisk.cpio.lzo -o release/noma_${DATE}v${RELVER}_boot.img || exit 1
		echo "Smells like bacon... release/noma_${DATE}v${RELVER}_boot.img is ready!"
}

setenv
make -j${CPUS} && mkinitramfs && mkbootimg

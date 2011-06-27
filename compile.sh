#!/bin/bash

DATE=`date +%Y%m%d`

function setenv {
	echo -n "Setting ARM environment..."
	export ARCH=arm
	export CROSS_COMPILE=~/src/android-ndk-r5b/toolchains/arm-eabi-4.4.0/prebuilt/linux-x86/bin/arm-eabi-
	echo " done."

	echo -n "Setting other environment variables..."
	# the number of CPUs to use when compiling the kernel (auto detect all available)
	export CPUS=`grep -c processor /proc/cpuinfo`
	echo " done."
}

function mkbootimg {
		echo "Creating boot.img..."
		release/mkbootimg-U8150 --cmdline 'mem=211M console=ttyMSM2,115200n8 androidboot.hardware=huawei console=ttyUSBCONSOLE0 androidboot.console=ttyUSBCONSOLE0' --kernel arch/arm/boot/zImage --ramdisk release/boot.img-ramdisk.cpio.gz -o release/noma_${DATE}_boot.img || exit 1
		echo "Smells like bacon... release/noma_${DATE}_boot.img is ready!"
}

setenv
make -j${CPUS}
mkbootimg
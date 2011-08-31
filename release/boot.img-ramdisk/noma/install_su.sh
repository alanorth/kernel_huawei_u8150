#!/sbin/busybox sh

# remount some partitions rw so we can futz with stuff
busybox mount -o remount,rw /system
busybox mount -o remount,rw /

# set up busybox first
busybox --install -s /sbin
# now... busybox for everyone!
chmod 0755 /sbin
chmod 0755 /sbin/busybox

# enable more featureful sh for `adb shell` (tab completion, history, etc)
#rm -f /system/bin/sh
#ln -s /sbin/ash /system/bin/sh

# su binary
# delete old sus
rm -f /system/bin/su
rm -f /system/xbin/su
# now copy su and make sure it's setuid root (see: http://en.wikipedia.org/wiki/Setuid)
cp /noma/res/su-2.3.6.3-efgh /system/bin/su
chown 0.0 /system/bin/su
chmod 06755 /system/bin/su

# Superuser
# delete all other Superusers
rm /system/app/Superuser.apk
rm /data/app/Superuser.apk
rm /data/dalvik-cache/*uperuser.apk*
# copy our Superuser.apk and set permissions
cp /noma/res/Superuser.apk /system/app/Superuser.apk
chmod 0644 /system/app/Superuser.apk
chown 0.0 /system/app/Superuser.apk

# try to clean up to free some RAM
rm /noma/res/su-2.3.6.3-efgh
rm /noma/res/Superuser.apk

# back to read only
mount -o remount,ro /system
mount -o remount,ro /

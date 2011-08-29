#!/sbin/sh

# make sure the /system partition is writable
mount -o remount,rw /system

# su binary
# first, make sure ROM is coherent
rm -f /system/bin/su
rm -f /system/xbin/su
# now copy su and make sure it's setuid root (see: http://en.wikipedia.org/wiki/Setuid)
cp /res/su-2.3.6.3-efgh /system/bin/su
chown 0.0 /system/bin/su
chmod 06755 /system/bin/su

# Superuser
# delete all other Superusers
rm /system/app/Superuser.apk
rm /data/app/Superuser.apk
rm /data/dalvik-cache/*uperuser.apk*
# copy our Superuser.apk and set permissions
cp /res/Superuser.apk /system/app/Superuser.apk
chmod 0644 /system/app/Superuser.apk
chown 0.0 /system/app/Superuser.apk

# remount /system as read only
mount -o remount,ro /system

# sepukku
# try to clean up to free some RAM
mount -o remount,rw /
rm /res/su-2.3.6.3-efgh
rm /res/Superuser.apk
chmod 0644 /sbin/install_su.sh
mount -o remount,ro /

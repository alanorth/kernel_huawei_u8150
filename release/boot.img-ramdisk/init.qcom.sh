#!/system/bin/sh
# Copyright (c) 2009, Code Aurora Forum. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of Code Aurora nor
#       the names of its contributors may be used to endorse or promote
#       products derived from this software without specific prior written
#       permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

target=`getprop ro.product.device`

# /*<BU5D03701, JIALIN, 20100301, begin*/
echo "ondemand" > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
# /* < DTS2010080902195 wanghao 20100809 begin */
echo 90 > /sys/devices/system/cpu/cpu0/cpufreq/ondemand/powersave_bias
echo 80 > /sys/devices/system/cpu/cpu0/cpufreq/ondemand/up_threshold
# echo 30 > /sys/devices/system/cpu/cpu0/cpufreq/ondemand/down_differential
# /* DTS2010080902195 wanghao 20100809 end > */
echo 245760 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
# /*BU5D03701, JIALIN, 20100301, end>*/
case "$target" in
    "qsd8250_surf" | "qsd8250_ffa")
        value=`getprop persist.maxcpukhz`
        case "$value" in
            "")
                cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq >\
                /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
                ;;
            *)
                echo $value > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
                ;;
        esac
        ;;

esac

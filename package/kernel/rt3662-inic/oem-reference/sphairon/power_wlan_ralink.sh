#! /bin/sh

##### DEFINES
LOG_INFO='shell_log power_wlan_ralink info '
LOG_ERROR='shell_log power_wlan_ralink error '
LOCKFILE=$SAS_SYSTEM_ROOT/var/lock/power_wlan_ralink.lock

#### MAIN
STATUS=$SH_STAT_SUCCESS
if [ $# -ne 4 ]; then
    echo -e "wrong paramater count!\n usage: power_wlan_ralink.sh <on|off> <base_platform> <WLAN-MAC> <SwitchWlanPower 0|1>"
    exit $SH_STAT_FAILED
fi

lock ${LOCKFILE}
CMD=$1
NO_BASE_PLATFORM=0
SWITCH_WLAN_PWR=$4

if [ $NO_BASE_PLATFORM -eq 0 ]; then
    case $CMD in
        on)
            $LOG_INFO "power on wlan"
            #switch module on
            echo 1 > /sys/class/stg_platform/wlan-rst/value  #reset on
            echo 1 > /sys/class/stg_platform/wlan-pwr/value
            usleep 500000   
            ip link set sw-p5-wifi up
            echo 0 > /sys/class/stg_platform/wlan-rst/value  #reset off
            
            STATUS=$?
            ;;
        off)
            $LOG_INFO "power off wlan"
            echo 1 > /sys/class/stg_platform/wlan-rst/value     #reset on
            ip link set sw-p5-wifi down
            if [ $SWITCH_WLAN_PWR -eq 1 ]; then
                echo 0 > /sys/class/stg_platform/wlan-pwr/value     #power off
            else 
                $LOG_INFO "do not power off wlan-module!"
            fi
            
            STATUS=$?
            ;;
        *)
            $LOG_ERROR "Wrong command"
            STATUS=$?
            ;;
    esac
    if [ $STATUS -eq 0 ]; then
        $LOG_INFO "Wlan powered on/off successfully"
    else
        $LOG_ERROR "Error powering wlan"
    fi        
else
    $LOG_ERROR "No base_platform selected! Could not get mapping! -> abort!"
    STATUS=1
fi
lock -u ${LOCKFILE}
exit $STATUS

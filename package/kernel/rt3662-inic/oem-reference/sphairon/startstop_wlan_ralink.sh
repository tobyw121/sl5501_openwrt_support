#! /bin/sh

############# DEFINES
LOG_INFO='shell_log startstop_wlan_ralink info '
LOG_ERROR='shell_log startstop_wlan_ralink error '
RUNFILE=$SAS_SYSTEM_ROOT/var/run/wlan.run
LOCKFILE=$SAS_SYSTEM_ROOT/var/lock/startstop_wlan_ralink.lock

AP1IFS=""
AP2IFS=""
RESULT=""
############# SOUBROUTINES
generateApIfNames()
{
    local APIFNAME=$1
    local APCFGFILE=$2
    RESULT=""
    local RETURN=$SH_STAT_SUCCESS

    if [ -f "${APCFGFILE}" ]; then
        SSID_NUM=$(cat "${APCFGFILE}" | grep "BssidNum" | cut -d'=' -f2)
        APIFS="${APIFNAME}"         #first AP
        #further interfaces (from multiple ssids)
        IFNAMEPREFIX="${APIFNAME:0:$(expr ${#APIFNAME} - 1)}"
        for i in $(seq 1 "$(expr ${SSID_NUM} - 1)"); do
            #for multiple ssids last number of interface name will be iterated
            IFNAME="${IFNAMEPREFIX}${i}"
            APIFS="${APIFS} ${IFNAME}"
        done
        $LOG_INFO "Found AP interfaces \"${APIFS}\""
    else
        $LOG_ERROR "Error reading config file ${APCFGFILE}"
        RETURN=$SH_STAT_RESSOURCE
    fi
    
    #write result var
    RESULT="${APIFS}"

    return $RETURN
}

startap()
{
    local RET=$SH_STAT_SUCCESS

    # Disable broadcasts, multicasts and "reserve" casts for all ports
    # except CPU and WLAN module while loading the driver
    #REG_E5=$(switch-tool --read-register 0xe5 | awk -F ": " '{ print $2 }')
    #REG_E6=$(switch-tool --read-register 0xe6 | awk -F ": " '{ print $2 }')
    #REG_F6=$(switch-tool --read-register 0xf6 | awk -F ": " '{ print $2 }')
    #switch-tool --set-register '0xe5=0x7f60' #enable unicast, broadcast only between CPU and WLAN
    #switch-tool --set-register '0xe6=0x0000' #disable Multicast, disable reserved traffic
    #switch-tool --set-register '0xf6=0x000E' #allow administrative multicast

    #AP2
    if [ "${AP2IFS}" != "" ];then
        for IfName in ${AP2IFS}; do
            $LOG_INFO "Startign up AP2 interface \"${IfName}\""
            ifconfig ${IfName} up
            RET=$(expr $RET + $?)    
        done
    else
        $LOG_INFO "No interfaces for AP2 available, skip..."
    fi    

    #AP1
    if [ "${AP1IFS}" != "" ]; then
        for IfName in ${AP1IFS}; do
            $LOG_INFO "Startign up AP1 interface \"${IfName}\""
            ifconfig ${IfName} up
            RET=$(expr $RET + $?)
        done
    else
        $LOG_ERROR "No interfaces for AP1 available!"
        RET=$SH_STAT_RESSOURCE
    fi
    
    # Enable broadcasts, multicasts and "reserve" casts for all ports
    #switch-tool --set-register '0xe5='${REG_E5}
    #switch-tool --set-register '0xe6='${REG_E6}
    #switch-tool --set-register '0xf6='${REG_F6}

    if [ $RET -eq 0 ]; then
        touch $RUNFILE
    fi  
    
    return $RET
}

stopap()
{
    rm -f $RUNFILE
    local RET=$SH_STAT_SUCCESS    
    
    #AP2
    #IAD-899: stop second AP first cause it's connected via PCIe to first AP, so
    #we can't stop second AP after stopping first AP
    if [ "${AP2IFS}" != "" ];then
        for IfName in ${AP2IFS}; do
            $LOG_INFO "Stopping AP2 interface \"${IfName}\""
            ifconfig ${IfName} down
            RET=$(expr $RET + $?)    
        done
    else
        $LOG_INFO "No interfaces for AP2 available, skip..."
    fi    

    #AP1
    if [ "${AP1IFS}" != "" ]; then
        for IfName in ${AP1IFS}; do
            $LOG_INFO "Stopping AP1 interface \"${IfName}\""
            ifconfig ${IfName} down
            RET=$(expr $RET + $?)
        done
    else
        $LOG_ERROR "No interfaces for AP1 available!"
        RET=$SH_STAT_RESSOURCE
    fi
    
    return $RET
}

############# MAIN
STATUS=0
if [ $# -eq 2 ]; then
    lock ${LOCKFILE}
    CMD=$1
    WLAN_CONF_FILE=$2
    

    if [ -f "$WLAN_CONF_FILE" ]; then
        . $WLAN_CONF_FILE
    else
        $LOG_ERROR "Could not open wlan-conf"
        lock -u ${LOCKFILE}
        exit $SH_STAT_FAILED
    fi

    #grep for AP interface names
    #AP1
    generateApIfNames $Ap1If $CfgWoCp AP1IFS
    STATUS=$?    
    AP1IFS="${RESULT}"

    #AP2
    if [ "$Ap2If" != "" ]; then
        generateApIfNames $Ap2If $CfgWoCp2
        STATUS=$(expr $STATUS + $?)
        AP2IFS="${RESULT}"
    fi
    
    if [ $STATUS -ne 0 ]; then
        $LOG_ERROR "Error while getting AP interfaces!"
        exit $SH_STAT_FAILED
    fi

    case $CMD in
        start)
            $LOG_INFO "Starting AP with interface ${IfName}"
            startap $1
            STATUS=$?
            ;;
        stop)
            $LOG_INFO "Stopping AP with interface ${IfName}"
            stopap $1
            STATUS=$?
    esac

    if [ $STATUS -eq 0 ]; then
        $LOG_INFO "AP ${CMD}ed"
    else
        $LOG_ERROR "Error while ${CMD}ing AP interfaces!"
    fi
    lock -u ${LOCKFILE}
else
    $LOG_ERROR "Wrong parameter count! use 'startstop_wlan_ralink.sh <start|stop> <wlan-conf-file>'"
fi

return $STATUS

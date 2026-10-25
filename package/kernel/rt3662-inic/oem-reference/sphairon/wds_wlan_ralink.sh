#! /bin/sh

##### DEFINES
LOG_INFO='shell_log wds_wlan_ralink info '
LOG_ERROR='shell_log wds_wlan_ralink error '
LOCKFILE=$SAS_SYSTEM_ROOT/var/lock/wds_wlan_ralink.lock
STATUS=$SH_STAT_SUCCESS
WDS_BRIDGE_IF="wdsbr0"

###SUBROUTINES
CloseAllInterfaces()
{
    #close down WDS-interfaces
    for i in 0 1 2 3; do
        ifconfig wds${i} > /dev/null
        if [ $? -eq 0 ]; then
            ifconfig wds${i} down
        fi
    done
    #close down ApCli interface

}

CheckAddApCliIf()
{
    local APCLI_IF="apcli0"    

    #check interface
    ifconfig ${APCLI_IF} > /dev/null
    if [ $? -ne 0 ];then
        $LOG_ERROR "ApClient Interface 'apcli0' does not exist! Abort!"
        return $SH_STAT_FAILED
    fi
    #add if to bridge
    brctl addif ${WDS_BRIDGE_IF} ${APCLI_IF}
    if [ $? -ne 0 ]; then
        $LOG_ERROR "Error adding if '${APCLI_IF}' to bridge '$WDS_BRIDGE_IF'! Abort!"
        CloseAllInterfaces
        return $SH_STAT_FAILED
    fi
    sleep 1
    ifconfig ${APCLI_IF} up
    if [ $? -ne 0 ]; then
        $LOG_ERROR "Error doing up interface 'wds${i}'! Try another time..."
        sleep 1
        ifconfig ${APCLI_IF} up
        if [ $? -ne 0 ]; then
            $LOG_ERROR "Error doing up interface 'wds${i} second time! Abort!'"
            CloseAllInterfaces
            return $SH_STAT_FAILED
        fi
    fi

    return $SH_STAT_SUCCES
}

CheckAddWdsIf()
{
    #check interfaces
    for i in 0 1 2 3; do
        ifconfig wds${i} > /dev/null
        if [ $? -ne 0 ]; then
            $LOG_ERROR "WDS Interface 'wds${i}' does not exist! Abort!"
            return $SH_STAT_FAILED
        fi
    done
    #up interfaces and add to bridge
    for i in 0 1 2 3; do
        ifconfig wds${i} up
        if [ $? -ne 0 ]; then
            $LOG_ERROR "Error doing up interface 'wds${i}'! Try another time..."
            sleep 1
            ifconfig wds${i} up
            if [ $? -ne 0 ]; then
                $LOG_ERROR "Error doing up interface 'wds${i}' second time! Abort!"
                CloseAllInterfaces
                return $SH_STAT_FAILED
            fi
        fi
        usleep 500000
        brctl addif $WDS_BRIDGE_IF wds${i}
        if [ $? -ne 0 ]; then
            $LOG_ERROR "Error adding if 'wds${i}' to bridge '$WDS_BRIDGE_IF'! Abort!"
            CloseAllInterfaces
            return $SH_STAT_FAILED
        fi
        usleep 500000
        
    done
    return $SH_STAT_SUCCESS
}

CheckAddBridgeIf()
{
    #check if MiiMaBridgeIf is bridge
    brctl show | grep ${MiiMaIf} > /dev/null
    if [ $? -eq 0 ]; then
        WDS_BRIDGE_IF=${MiiMaIf}
    else
        brctl addbr $WDS_BRIDGE_IF
        brctl setfd $WDS_BRIDGE_IF 0
        ifconfig $WDS_BRIDGE_IF 0.0.0.0 up
    fi
    $LOG_INFO "Use ${WDS_BRIDGE_IF} as bridge if"
}

CloseBridgeIf()
{
    if [ "$WDS_BRIDGE_IF" != "MiiMaBridgeIf" ]; then
        $LOG_INFO "Bridge ${WDS_BRIDGE_IF}  is now setting down..."
        ifconfig $WDS_BRIDGE_IF > /dev/null
        if [ $? -eq 0 ]; then
            ifconfig $WDS_BRIDGE_IF down
            brctl delbr $WDS_BRIDGE_IF
        fi
    fi
}

ApCliStart()
{
    APCLI_ENABLE=$(cat "${CfgWoCp}" | grep ApCliEnable | cut -d'=' -f2)
    if [ $APCLI_ENABLE -eq 1 ]; then
        $LOG_INFO "Now starting ApClient Interfaces..."
        CheckAddBridgeIf
        CheckAddApCliIf
        if [ $? -ne $SH_STAT_SUCCESS ]; then
            CloseBridgeIf
            return $SH_STAT_FAILED
        fi
    fi

    return $SH_STAT_SUCCESS
}

WdsStart()
{
    WDS_ENABLE=$(cat "${CfgWoCp}" | grep WdsEnable | cut -d'=' -f2)
    case $WDS_ENABLE in
        0)
        #check for APCLIENT
        ApCliStart
        ;;  
        1|2|3)
        #start/check WDS interfaces
        CheckAddBridgeIf
        CheckAddWdsIf
        if [ $? -ne $SH_STAT_SUCCESS ];then
            CloseBridgeIf
            return $SH_STAT_FAILED
        fi
        ;;
    esac
    
    #add regular if to bridge and eth0?
    brctl addif ${WDS_BRIDGE_IF} ${Ap1If} 
    if [ "$WDS_BRIDGE_IF" != "MiiMaBridgeIf" ]; then
        #add MiiMaster to bridge
        brctl addif ${WDS_BRIDGE_IF} ${MiiMaIf}
    fi 

    return $SH_STAT_SUCCES
}

WdsStop()
{
    CloseAllInterfaces
    CloseBridgeIf
    STATUS=$?    

    return $STATUS
}

###MAIN

lock ${LOCKFILE}
if [ $# -eq 2 ]; then
    CMD=$1
    WLAN_CONF_FILE=$2

    if [ -f "$WLAN_CONF_FILE" ]; then
        . $WLAN_CONF_FILE
    else
        $LOG_ERROR "Could not include ${WLAN_CONF_FILE}, Abort!"
        lock -u ${LOCKFILE}
        return $SH_STAT_FAILED
    fi

    case $CMD in
        start)
            $LOG_INFO "Starting WDS..."
            WdsStart
            STATUS=$?
            ;;
        stop)
            $LOG_INFO "Stopping WDS..."
            WdsStop
            STATUS=$?
            ;;
    esac
else
    $LOG_ERROR "Wrong parameter count!"
    echo "Usage: wds_wlan_ralink.sh <start|stop> <wlanapi-conffile>"
    STATUS=$SH_STAT_PARAM
fi
lock -u ${LOCKFILE}
exit $STATUS


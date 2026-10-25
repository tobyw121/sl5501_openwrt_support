#! /bin/sh

####### DEFINES
LOG_INFO='shell_log init_wlan_ralink info '
LOG_ERROR='shell_log init_wlan_ralink error '
LOCKFILE=$SAS_SYSTEM_ROOT/var/lock/init_wlan_ralink.lock

CONCURRENT_AP=0

####### SUBROUTINES
configureApDatDefaults()
{
    TargetCfgFile=$1
    if [ $# -eq 2 ]; then 
        ApSuffix=$2
    else
        ApSuffix=""
    fi    

    #map some defaults from etl to iNIC-File before starting AP
    sed -i  -e 's/SSID1=.*/SSID1='"${wlan_ssid}${ApSuffix}"'/' \
            -e 's/WPAPSK1=.*/WPAPSK1='"${wlan_wep_key}"'/' \
            -e 's/Key1Str1=.*/Key1Str1='"${wlan_wep_key}"'/' \
            -e 's/ApCliWPAPSK=.*/ApCliWPAPSK='"${wlan_wep_key}"'/' \
            -e 's/ApCliKey1Str=.*/ApCliKey1Str='"${wlan_wep_key}"'/' \
            -e 's/Wds0Key=.*/Wds0Key='"${wlan_wep_key}"'/' \
            -e 's/WscModelNumber=.*/WscModelNumber='"${ident_no}"'/' \
            -e 's/WscSerialNumber=.*/WscSerialNumber='"${serial_no}"'/' \
            -e 's/DBG_LEVEL=.*/DBG_LEVEL=0/' \
            $TargetCfgFile
}

configureMultipleSsid()
{
    TargetCfgFile=$1
    if [ $# -eq 2 ]; then 
        ApSuffix=$2
    else
        ApSuffix=""
    fi    

    SSID_NUM=$(cat "$TargetCfgFile" | grep "BssidNum" | cut -d'=' -f2)
    $LOG_INFO "Found SSID-number ${SSID_NUM}"
    #further SSID (start from SSID2)
    for i in $(seq 2 ${SSID_NUM}); do
        sed -i -e 's/SSID'"${i}"'=.*/SSID'"${i}"'='"${wlan_ssid}${ApSuffix}"'_0'"${i}"'/' \
               -e 's/WPAPSK'"${i}"'=.*/WPAPSK'"${i}"'='"${wlan_wep_key}"'/' \
               -e 's/Key'"${i}"'Str1=.*/Key'"${i}"'Str1='"${wlan_wep_key}"'/' \
               $TargetCfgFile
    done
}

CheckForTempEepromFile()
{
    TMP_CALIBRATION_ROOT="/flash/"

    #IAD-844: change temp e2p location due to psmd
    if [ "$flash_layout" == "psmd" -o "$flash_layout" == "pssd" ]; then
        TMP_CALIBRATION_ROOT="/data/"
    fi

    if [ "$1" == "AP1" ]; then
        TMP_E2P_FILE="${TMP_CALIBRATION_ROOT}iNIC_e2p.bin"
    elif [ "$1" == "AP2" ]; then
        TMP_E2P_FILE="${TMP_CALIBRATION_ROOT}iNIC_e2p1.bin"
    fi
    
    CFG_FILE="$2"
    E2P_TARGET="$3"
    
    $LOG_INFO "Check for temporary EEPROM file for $1 in '$TMP_E2P_FILE'"
    #check if temp eeprom exitsts
    if [ -f "$TMP_E2P_FILE" ]; then
        $LOG_INFO "Found temporary EEPROM file ($TMP_E2P_FILE), now enable use this..."
        if [ -f "$E2P_TARGET" ]; then
            $LOG_INFO "EEPROM '$E2P_TARGET' already exists, override!"
            rm -r $E2P_TARGET > /dev/null
        fi
        cp -f $TMP_E2P_FILE $E2P_TARGET
        sed -i -e 's/ExtEEPROM=.*/ExtEEPROM=1/' $CFG_FILE
    else
        $LOG_INFO "Temporary EEPROM file ($TMP_E2P_FILE) is not available!"
    fi
}
    
setRequiredDefaults()
{
    CFG_FILE_SRC=$1
    CFG_FILE_TARGET=$2

    #BS-1669:
    EXT_EEPROM_SET=$(cat "${CFG_FILE_SRC}" | grep ExtEEPROM | awk -F'=' '{print $2}')
    sed -i -e 's/ExtEEPROM=.*/ExtEEPROM='"${EXT_EEPROM_SET}"'/' $CFG_FILE_TARGET

}


initstart()
{
    # analogue to Mantis 6659
    # Disable broadcasts, multicasts and "reserve" casts for all ports
    # except CPU and WLAN module while loading the driver
    #REG_E5=$(switch-tool --read-register 0xe5 | awk -F ": " '{ print $2 }')
    #REG_E6=$(switch-tool --read-register 0xe6 | awk -F ": " '{ print $2 }')
    #REG_F6=$(switch-tool --read-register 0xf6 | awk -F ": " '{ print $2 }')
    #switch-tool --set-register '0xe5=0x7f60' #enable unicast, broadcast only between CPU and WLAN
    #switch-tool --set-register '0xe6=0x0000' #disable Multicast, disable reserved traffic
    #switch-tool --set-register '0xf6=0x000E' #allow administrative multicast

    mkdir -p $WorkPath

    # copy default config file & get eeprom file if necessary
    if [ -f "$CfgSrc" ]; then
        if [ ! -f "$CfgWoCp" ]; then
            #BS-1669: do not overwrite already existing cfg file
            cp -f $CfgSrc $CfgWoCp
        else
            #BS-1669: at least we have to set minumum defaults from original config source to keep correct settings
            setRequiredDefaults $CfgSrc $CfgWoCp
        fi
        chmod 666 ${WorkPath}/*.dat        

        #AP1
        if [ -f "$CfgWoCp" ]; then
            #copy EEPROM File if exists
            #IAD-859: provide default eeprom file by default
            rm -f $EepromWoCp  > /dev/null
            cp -f $EepromSrc $EepromWoCp
            chmod 666 $EepromWoCp

            # get the calibration data from flash for on-board iNIC, if configured in iNIC
            cat "$CfgWoCp" | grep "ExtEEPROM=1" > /dev/null   #must load ExtEEPROM-File
            if [ $? -eq 0 ]; then                                   
                $LOG_INFO "External EEPROM file needed, try to get this"
                #first try to load from flash, if this fails try to get from lib-path (default)
                if ! flash-tool readcd $EepromWoCp ; then
                    $LOG_INFO "Get external EEPROM file from '$EepromSrc'"
                    rm -f $EepromWoCp  > /dev/null           #Paranoia
                    cp -f $EepromSrc $EepromWoCp
                fi
                if [ ! -f $EepromWoCp ]; then
                    $LOG_ERROR "Could not get external EEPROM-file, but needed!"
                    STATUS=$SH_STAT_FAILED
                fi
            fi 
            #if eeprom file is located in /flash/ then us this and enable external eeprom!
            CheckForTempEepromFile "AP1" $CfgWoCp $EepromWoCp

            #BS-1669: check if conf file is already modified, don't set defaults if found
            MD5_CFG_SRC=$(md5sum ${CfgSrc} | awk '{print $1}')
            MD5_CFG_WOCP=$(md5sum ${CfgWoCp} | awk '{print $1}')
    
            if [ "${MD5_CFG_SRC}" == "${MD5_CFG_WOCP}" ]; then 
                #set CfgFileDefaults
                configureApDatDefaults $CfgWoCp
                #multiple ssid 
                configureMultipleSsid $CfgWoCp
            else
                $LOG_INFO "skip setting default values while modified conf file was found in ${CfgWoCp}"
            fi
        else 
            $LOG_ERROR "Target Config '${CfgWoCp}' file does not exist!"
            STATUS=$SH_STAT_FAILED 
        fi
        
        #AP2
        if [ "${CfgSrc2}" != "" ]; then
            $LOG_INFO "Found second AP config file, proceed preparations"
            if [ ! -f $CfgWoCp2 ]; then
                #BS-1669: do not overwrite already existing cfg file
                cp -f $CfgSrc2 $CfgWoCp2
            else
                #BS-1669: at least we have to set minumum defaults from original config source to keep correct settings
                setRequiredDefaults $CfgSrc2 $CfgWoCp2
            fi

            #card file
            AP_CARD_FILE="${LibPath}iNIC_ap_card.dat"
            AP_CARD_WOCP="${WorkPath}iNIC_ap_card.dat"
            
            CONCURRENT_AP=1
            if [ -f "$AP_CARD_FILE" ]; then
                cp -f $AP_CARD_FILE $WorkPath
            fi
            
            #chmod dat files
            chmod 666 ${WorkPath}/*.dat    
            
            if [ -f "$AP_CARD_WOCP" ]; then
                #do set MACS and profile paths
                #clear file
                echo '' > $AP_CARD_WOCP 
                #set it
                echo "Default" > $AP_CARD_WOCP
                echo "00ProfilePath=${CfgWoCp}" >> $AP_CARD_WOCP
                echo "01ProfilePath=${CfgWoCp2}" >> $AP_CARD_WOCP
                echo "00MAC=${MAC1}" >> $AP_CARD_WOCP
                echo "01MAC=${MAC3}" >> $AP_CARD_WOCP
            else
                $LOG_ERRPR "Error configuring iNIC card file!" 
                STATUS=$SH_STAT_FAILED
            fi
            
            #eeprom
            #IAD-859: provide e2p files by default
            rm -f $EepromWoCp2  > /dev/null           #Paranoia
            cp -f $EepromSrc2 $EepromWoCp2
            chmod 666 $EepromWoCp2
            
            cat "$CfgWoCp2" | grep "ExtEEPROM=1" > /dev/null   #must load ExtEEPROM-File
            if [ $? -eq 0 ]; then                                   
                $LOG_INFO "External EEPROM file for AP2 in use"
            fi        
    
            #check for tmp Eeprom file        
            CheckForTempEepromFile "AP2" $CfgWoCp2 $EepromWoCp2

            #BS-1669: check if conf file is already modified, don't set defaults if found
            MD5_CFG_SRC=$(md5sum ${CfgSrc2} | awk '{print $1}')
            MD5_CFG_WOCP=$(md5sum ${CfgWoCp2} | awk '{print $1}')
    
            if [ "${MD5_CFG_SRC}" == "${MD5_CFG_WOCP}" ]; then 
                #set CfgFileDefaults
                configureApDatDefaults $CfgWoCp2 "AP2"
                #multiple SSID
                configureMultipleSsid $CfgWoCp2 "AP2"     
            else
                $LOG_INFO "skip setting default values while modified conf file was found in ${CfgWoCp2}"
            fi   
        fi    
    else 
        $LOG_ERROR "Could not proceed, ConfigFile is not configured!"
        STATUS=$SH_STAT_FAILED
    fi

    #copy firmware-file
    if [ -f "$FwSrc" ]; then
        ln -sf $FwSrc $FwWoCp
    else
        $LOG_ERROR "Could not proceed, firmware file does not exist!"
        STATUS=$SH_STAT_FAILED
    fi

    #if there are no errors until here, load driver
    if [ $STATUS -eq $SH_STAT_SUCCESS ]; then
        local MODULE_NAME=$( basename "$Driver" | cut -d'.' -f1)
        #check if module is already loaded
        lsmod | grep $MODULE_NAME > /dev/null         
        if [ $? -ne 1 ]; then
            $LOG_INFO "Module already loaded, try to unload now"
            modprobe -r $MODULE_NAME
        fi
        #switch-tool --enable-port 6 --set-static-learn-entry=$MAC1,0,64 --set-static-learn-entry=$MAC1,2,64 
        if [ $CONCURRENT_AP -eq 1 ];then
            $LOG_INFO "Concurrent AP found, retrieve mac from card-file"
            modprobe $Driver mode=ap miimaster=$MiiMaIf bridge=1 syncmiimac=0     #here must been set other parameters for concurrent module
        else
            modprobe $Driver mode=ap mac=$MAC1 miimaster=$MiiMaIf bridge=1 syncmiimac=0     #here must been set other parameters for concurrent module
        fi
        STATUS=$?
    else
        $LOG_ERROR "Error init wlan - abort!"
    fi

    # Enable broadcasts, multicasts and "reserve" casts for all ports
    #switch-tool --set-register '0xe5='${REG_E5}
    #switch-tool --set-register '0xe6='${REG_E6}
    #switch-tool --set-register '0xf6='${REG_F6}
    
    return $STATUS
}

initstop()
{
    local MODULE_NAME=$( basename "$Driver" | cut -d'.' -f1)
    lsmod | grep $MODULE_NAME > /dev/null
    if [ $? -eq 0 ]; then
        modprobe -r $MODULE_NAME
    else 
        $LOG_INFO   "Module not loaded, no need to unload!"
    fi

    #BS-1669: do not clean up work path while it may be that already written cfg files are in.
    #rm -rf $WorkPath
}

####### MAIN
STATUS=$SH_STAT_SUCCESS
if [ $# -ne 2 ]; then
    echo -e "wrong paramater count!\n usage: init_ralink_wlan.sh <start|stop> <wlan_conf_file_path>"
    exit $SH_STAT_FAILED
fi

lock ${LOCKFILE}
CMD=$1
NO_BASE_PLATFORM=0
WLAN_CONF_FILE="$2"

if [ -f "$WLAN_CONF_FILE" ]; then
    . $WLAN_CONF_FILE
else
    $LOG_ERROR "Error opening WLAN-Conf-File! Abort!"
    lock -u ${LOCKFILE}
    exit $SH_STAT_FAILED
fi

if [ -f /etc/etl.conf ]; then
    . /etc/etl.conf
else
    $LOG_ERROR "Error opening etl! Abort!"
    lock -u ${LOCKFILE}
    exit $SH_STAT_FAILED
fi

if [ $NO_BASE_PLATFORM -eq 0 ]; then
    case $CMD in
        start)
            $LOG_INFO "Start init wlan"
            initstart "$@"
            STATUS=$?
            ;;
        stop)
            $LOG_INFO "Start deinit wlan"
            initstop "$@"
            STATUS=$?
            ;;
        *)
            $LOG_ERROR "Wrong command"
            STATUS=$?
            ;;
    esac
    if [ $STATUS -eq 0 ]; then
        $LOG_INFO "Wlan (de)initialized successfully"
    else
        $LOG_ERROR "Error while (de)initializing Wlan"
    fi        
else
    $LOG_ERROR "No base_platform selected! Could not get mapping! -> abort!"
    STATUS=1
fi
lock -u ${LOCKFILE}
exit $STATUS


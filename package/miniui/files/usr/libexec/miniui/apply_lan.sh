#!/bin/sh

set -e

IP="$1"
MASK="$2"

[ -n "$IP" ] || exit 1
[ -n "$MASK" ] || MASK="255.255.255.0"

uci -q batch <<-UCI
set network.lan.ipaddr='$IP'
set network.lan.netmask='$MASK'
UCI
uci -q commit network

exit 0

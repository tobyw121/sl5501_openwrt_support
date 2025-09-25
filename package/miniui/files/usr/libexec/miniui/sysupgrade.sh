#!/bin/sh

set -e

KEEP="$1"
SRC="$2"

[ -n "$SRC" ] || exit 1

TMP_IMAGE=""
case "$SRC" in
	http://*|https://*)
		TMP_IMAGE="/tmp/miniui-sysupgrade.bin"
		if command -v uclient-fetch >/dev/null 2>&1; then
			uclient-fetch -O "$TMP_IMAGE" "$SRC"
		elif command -v wget >/dev/null 2>&1; then
			wget -O "$TMP_IMAGE" "$SRC"
		else
			exit 1
		fi
		SRC="$TMP_IMAGE"
	;;
esac

[ -f "$SRC" ] || exit 1

if [ "$KEEP" = "0" ]; then
	exec /sbin/sysupgrade -n "$SRC"
else
	exec /sbin/sysupgrade "$SRC"
fi

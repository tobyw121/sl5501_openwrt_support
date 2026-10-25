#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BOARD="$ROOT/target/linux/lantiq/xrx200/base-files/etc/board.d/02_network"
DTS="$ROOT/target/linux/lantiq/files/arch/mips/boot/dts/lantiq/vr9_sphairon_sl5501.dts"
MIG="$ROOT/package/kernel/rt3662-inic/files/etc/uci-defaults/99-sl5501-router-v19"
WIFI="$ROOT/package/kernel/rt3662-inic/files/etc/config/wireless"
CFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_cfg80211.c"
P0="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap.dat"
P1="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap1.dat"
TARGETMK="$ROOT/include/target.mk"
DHCP_DEFAULT="$ROOT/package/network/services/dnsmasq/files/dhcp.conf"
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -F 'ucidef_set_interfaces_lan_wan "lan1 lan2 lan3 lan4 wifi" "wan"' "$BOARD" >/dev/null || fail 'physical DSA WAN mapping missing'
grep -F 'if [ "$board" = "sphairon,sl5501" ]; then' "$BOARD" >/dev/null || fail 'SL5501 DSL special case missing'
grep -F 'ucidef_add_vdsl_modem "$annex" "av"' "$BOARD" >/dev/null || fail 'DSL modem description lost'
# The SL5501 special block must return before the generic helper can overwrite WAN with dsl0.
awk '/if \[ "\$board" = "sphairon,sl5501" \]; then/{f=1} f{print} /return/{if(f) exit}' "$BOARD" | grep -F 'return' >/dev/null || fail 'SL5501 DSL block does not return'

grep -A6 'label = "wan";' "$DTS" | grep -F 'phy-mode = "rgmii-id";' >/dev/null || fail 'WAN PEF7071 delay mode not fixed'
grep -A6 'label = "lan4";' "$DTS" | grep -F 'phy-mode = "rgmii-id";' >/dev/null || fail 'LAN4 PEF7071 delay mode not fixed'

grep -F "set network.wan.device='wan'" "$MIG" >/dev/null || fail 'preserved-config WAN migration missing'
grep -F "set network.wan.proto='dhcp'" "$MIG" >/dev/null || fail 'WAN DHCP client missing'
grep -F "set network.wan6.proto='dhcpv6'" "$MIG" >/dev/null || fail 'WAN DHCPv6 client missing'
grep -F "set dhcp.lan.interface='lan'" "$MIG" >/dev/null || fail 'LAN DHCP server repair missing'
grep -F "delete dhcp.lan.ignore" "$MIG" >/dev/null || fail 'LAN DHCP not forced active without WAN'
grep -F 'DEFAULT_PACKAGES.router:=' "$TARGETMK" >/dev/null || fail 'router default packages missing'
grep -F 'dnsmasq' "$TARGETMK" >/dev/null || fail 'router image does not include dnsmasq'
grep -F 'firewall4' "$TARGETMK" >/dev/null || fail 'router image does not include firewall4'
grep -F 'config dhcp lan' "$DHCP_DEFAULT" >/dev/null || fail 'stock LAN DHCP section missing'
grep -F 'config dhcp wan' "$DHCP_DEFAULT" >/dev/null || fail 'stock WAN DHCP ignore section missing'

test "$(grep -c "option network 'lan'" "$WIFI" || true)" -eq 0 || fail 'synthetic AP netdevs still bridged as payload ports'
grep -F 'runtime AP step' "$CFG" >/dev/null || fail 'profile-backed AP fallback diagnostics missing'
grep -F 'ret == -EINVAL || ret == -EOPNOTSUPP' "$CFG" >/dev/null || fail 'safe profile fallback status filter missing'
grep -xF 'HT_BW=0' "$P0" >/dev/null || fail '5 GHz profile not aligned to HT20'
grep -xF 'HT_BW=0' "$P1" >/dev/null || fail '2.4 GHz profile not aligned to HT20'

echo 'PASS: SL5501 v19 physical WAN + WAN-independent LAN DHCP + Wi-Fi profile fallback'

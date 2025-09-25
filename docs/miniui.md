# MiniUI web interface

MiniUI is a lightweight web management interface for OpenWrt images that replaces LuCI on storage-constrained systems. It bundles a small static frontend, a focused ubus helper, and shell scripts to cover day-to-day administration without the size overhead of LuCI.

## Scope and features

* HTML/JS frontend served from `/www/miniui` by the existing `uhttpd` instance.
* Authentication reuses the standard `rpcd` session login workflow.
* Status dashboard with uptime, load, memory usage, and kernel information.
* LAN IPv4 configuration form (IP and netmask) with UCI commits and optional reload trigger.
* Firmware upgrade helper that accepts either `/tmp` image paths or HTTPS URLs and hands off to `sysupgrade`.
* Convenience reboot button wired to the `system.reboot` ubus method.

## Security posture

MiniUI relies on restrictive rpcd ACLs (`/usr/share/rpcd/acl.d/miniui.json`) to grant only the ubus methods needed by the UI:

* Read access to `miniui.status`, system board/info, network interface dumps, and UCI getters.
* Write access limited to the `miniui` helper methods (`apply_lan`, `reload_network`, `sysupgrade`), specific UCI mutation functions, and the `system.reboot` call.

No additional authentication stack is shipped—the standard rpcd session login continues to gate access. The backend helper (`/usr/libexec/miniui/miniui-ubus`) forks helper scripts under `/usr/libexec/miniui` for configuration and sysupgrade operations, maintaining least-privilege boundaries.

## LuCI replacement workflow

Enabling MiniUI automatically removes LuCI from the default image manifests:

* `CONFIG_MINIUI_REPLACES_LUCI` (added under **Global build settings**) defaults to `y` and controls the replacement behaviour.
* When the toggle is enabled, `include/target.mk` appends `miniui` to the default package set and subtracts `luci` (if present).
* During first boot the UCI defaults script (`/etc/uci-defaults/99-miniui`) retargets `uhttpd` to `/www/miniui` on port 80 and renames any existing `luci-*` configuration files to prevent LuCI from reactivating.

Builders who prefer LuCI can simply disable `CONFIG_MINIUI_REPLACES_LUCI` or deselect `miniui` in the package menu, restoring the original behaviour.

## Building and menuconfig

MiniUI ships as a core package (`package/miniui`). To toggle it:

1. Run `make menuconfig`.
2. Navigate to **Global build settings → Replace LuCI with MiniUI in default images** to control whether the package is selected by default.
3. Under **Base system → Web Interfaces** you can manually select or deselect `MiniUI lightweight web interface` regardless of the global toggle.

After adjusting, rebuild the target images as usual. The helper depends on `uhttpd`, `uhttpd-mod-ubus`, `rpcd`, `rpcd-mod-uci`, `rpcd-mod-rpcsys`, `libubus`, and `libuci`, all pulled in automatically.

## Runtime notes

* Initial login uses the standard root password (`passwd`/`firstboot` workflow applies).
* LAN changes are written through `/usr/libexec/miniui/apply_lan.sh` and committed via UCI; trigger a reload to apply immediately.
* `sysupgrade` accepts local `/tmp` paths or HTTP(S) URLs. Downloads fall back to `wget` when `uclient-fetch` is unavailable.
* The MiniUI service (`/etc/init.d/miniui`) runs a procd-managed ubus provider that respawns automatically if it exits.

For troubleshooting, inspect `/usr/libexec/miniui/miniui-ubus` logs via `logread` or run the helper manually under `ubus` to observe JSON replies.

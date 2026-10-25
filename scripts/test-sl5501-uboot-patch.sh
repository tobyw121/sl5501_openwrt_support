#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PATCH="$ROOT/package/boot/uboot-lantiq/patches/0117-MIPS-add-board-support-for-Sphairon-SL5501.patch"
TMP="${TMPDIR:-/tmp}/sl5501-uboot-patch-test.$$"
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

# This is the boards.cfg neighbourhood produced by OpenWrt's 0116 BT Home Hub
# patch immediately before 0117 is applied.
cat > "$TMP/boards.cfg" <<'EOC'
Active  mips        mips32         vrx200      avm             fb3370              fb3370_sfspl                         fb3370:SYS_BOOT_SFSPL                                                                                                             Daniel Schwierzeck <daniel.schwierzeck@gmail.com>
Active  mips        mips32         vrx200      bt              bthomehubv5a        bthomehubv5a_nandspl                 bthomehubv5a:SYS_BOOT_NANDSPL                                                                                                     Martin Blumenstingl <martin.blumenstingl@googlemail.com>
Active  mips        mips32         vrx200      bt              bthomehubv5a        bthomehubv5a_ram                     bthomehubv5a:SYS_BOOT_RAM                                                                                                         Martin Blumenstingl <martin.blumenstingl@googlemail.com>
Active  mips        mips32         vrx200      lantiq          easy80920           easy80920_nandspl                    easy80920:SYS_BOOT_NANDSPL                                                                                                        Daniel Schwierzeck <daniel.schwierzeck@gmail.com>
Active  mips        mips32         vrx200      lantiq          easy80920           easy80920_nor                        easy80920:SYS_BOOT_NOR                                                                                                            Daniel Schwierzeck <daniel.schwierzeck@gmail.com>
Active  mips        mips32         vrx200      lantiq          easy80920           easy80920_norspl                     easy80920:SYS_BOOT_NORSPL                                                                                                         Daniel Schwierzeck <daniel.schwierzeck@gmail.com>
Active  mips        mips32         vrx200      lantiq          easy80920           easy80920_ram                        easy80920:SYS_BOOT_RAM                                                                                                            Daniel Schwierzeck <daniel.schwierzeck@gmail.com>
Active  mips        mips32         vrx200      lantiq          easy80920           easy80920_sfspl                      easy80920:SYS_BOOT_SFSPL                                                                                                          Daniel Schwierzeck <daniel.schwierzeck@gmail.com>
EOC

(
  cd "$TMP"
  patch -p1 --dry-run < "$PATCH" >/dev/null
  patch -p1 < "$PATCH" >/dev/null
)

HEADER="$TMP/include/configs/sl5501.h"
test -f "$HEADER"
grep -q '^#ifndef __CONFIG_SL5501_H$' "$HEADER"
grep -q '^#define __CONFIG_SL5501_H$' "$HEADER"
grep -q '^#endif /\* __CONFIG_SL5501_H \*/$' "$HEADER"

# The bug seen in the real build was caused by a bad unified-diff new-file
# line count. The SL5501 header is currently 95 lines after the device-specific
# fallback MAC was added. Verify both the hunk header and resulting file length.
grep -q '^@@ -0,0 +1,95 @@$' "$PATCH"
[ "$(wc -l < "$HEADER")" -eq 95 ]

# Simple preprocessor-balance check for the generated board header.
awk '
  /^[[:space:]]*#(if|ifdef|ifndef)([[:space:]]|$)/ { depth++ }
  /^[[:space:]]*#endif([[:space:]]|$)/ { depth--; if (depth < 0) exit 2 }
  END { if (depth != 0) exit 3 }
' "$HEADER"

echo "OK: SL5501 U-Boot 0117 patch applies and generates a balanced 95-line sl5501.h"

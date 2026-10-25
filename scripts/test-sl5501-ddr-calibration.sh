#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TMP="${TMPDIR:-/tmp}/sl5501-ddr-cal-test.$$"
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/arch/mips/cpu/mips32/vrx200" \
         "$TMP/arch/mips/cpu/mips32/lantiq-common" \
         "$TMP/board/sphairon/sl5501"

python3 - "$ROOT" "$TMP" <<'PY'
from pathlib import Path
import sys
root=Path(sys.argv[1]); tmp=Path(sys.argv[2])

def extract_new_file(patch_path, target):
    lines=patch_path.read_text().splitlines()
    out=[]; active=False
    for line in lines:
        if line.startswith('+++ b/' + target):
            active=True
            continue
        if active:
            if line.startswith('diff --git '):
                break
            if line.startswith('--- /dev/null') and out:
                break
            if line.startswith('+++ b/') and not line.startswith('+++ b/' + target):
                break
            if line.startswith('@@'):
                continue
            if line.startswith('+') and not line.startswith('+++'):
                out.append(line[1:])
            elif line.startswith(' '):
                out.append(line[1:])
    if not out:
        raise SystemExit('cannot reconstruct ' + target)
    dst=tmp/target
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text('\n'.join(out) + '\n')

extract_new_file(root/'package/boot/uboot-lantiq/patches/0014-MIPS-add-support-for-Lantiq-XWAY-SoCs.patch',
                 'arch/mips/cpu/mips32/vrx200/mem_init.S')
extract_new_file(root/'package/boot/uboot-lantiq/patches/0014-MIPS-add-support-for-Lantiq-XWAY-SoCs.patch',
                 'arch/mips/cpu/mips32/lantiq-common/spl.c')
extract_new_file(root/'package/boot/uboot-lantiq/patches/0117-MIPS-add-board-support-for-Sphairon-SL5501.patch',
                 'board/sphairon/sl5501/ddr_settings.h')
PY

(
  cd "$TMP"
  patch -p1 < "$ROOT/package/boot/uboot-lantiq/patches/0034-MIPS-lantiq-SPL-report-data-CRC-values.patch" >/dev/null
  patch -p1 < "$ROOT/package/boot/uboot-lantiq/patches/0035-MIPS-vrx200-apply-board-calibrated-DDR-delays.patch" >/dev/null
  patch -p1 < "$ROOT/package/boot/uboot-lantiq/patches/0036-MIPS-lantiq-SPL-verify-SPI-source-before-DDR.patch" >/dev/null
  patch -p1 < "$ROOT/package/boot/uboot-lantiq/patches/0118-MIPS-sl5501-use-Sphairon-trained-DDR-delays.patch" >/dev/null
)

grep -q 'MC_DDR_ECHO_DLL0_TUNED_VALUE 0x28' "$TMP/board/sphairon/sl5501/ddr_settings.h"
grep -q 'MC_CCR43_TUNED_VALUE 0x566401' "$TMP/board/sphairon/sl5501/ddr_settings.h"
grep -q 'sw.*DDR_ECHO_DLL0' "$TMP/arch/mips/cpu/mips32/vrx200/mem_init.S"
grep -q 'SPL: SPI source CRC' "$TMP/arch/mips/cpu/mips32/lantiq-common/spl.c"
grep -q 'SPL: U-Boot payload read failed' "$TMP/arch/mips/cpu/mips32/lantiq-common/spl.c"

echo "OK: SL5501 trained DDR delays and SPI-vs-DDR CRC diagnostics apply cleanly"

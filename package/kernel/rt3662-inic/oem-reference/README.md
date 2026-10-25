# OEM/GPL source references (not built)

This directory is intentionally excluded from the module Kbuild. It keeps the
source evidence used to reimplement the SL5501 iNIC host on Linux 6.6.

* `ralink-2.4.0.9.10/`: readable older GPL Ralink host source. The active v6
  driver ports the relevant behavior; this code itself is not compiled.
* `sphairon/`: SL5501 startup/power scripts, RALINK tcpdump protocol dissector,
  relevant U-Boot patch references and the function-symbol map/hash of the
  unstripped Sphairon `rtXXXX_iNIC.ko` 2.7.0.5.

The original 2.7.0.5 `.ko` is not included as a runnable module because it was
built for Sphairon's Linux 2.6.32 kernel. The symbol map is sufficient to retain
function boundaries/names while the modern driver is rebuilt for cfg80211.

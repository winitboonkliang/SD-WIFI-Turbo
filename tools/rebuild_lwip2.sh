#!/bin/bash
# Rebuild liblwip2-1460-feat.a with a bigger TCP send buffer for the SD-WIFI
# turbo firmware. Result: download throughput ~145 KB/s -> ~700 KB/s.
#
# Why: the stock prebuilt lwIP library ships TCP_SND_BUF = 2*MSS (2.9 KB in
# flight per round trip) which caps GET speed. We rebuild with 8*MSS.
#
# Run inside WSL/Linux with PlatformIO already installed (the ESP8266 platform
# and xtensa toolchain must exist under ~/.platformio).
# Re-run after any PlatformIO platform/framework update (updates restore the
# stock library). A prebuilt copy is kept next to this script:
#   liblwip2-1460-feat-8xMSS.a  -> copy to
#   ~/.platformio/packages/framework-arduinoespressif8266/tools/sdk/lib/liblwip2-1460-feat.a
# (then run "pio run -t clean" once) if you don't want to rebuild from source.

set -e
F=$HOME/.platformio/packages/framework-arduinoespressif8266
B=$F/tools/sdk/lwip2/builder
T=$HOME/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-

# 1. backup originals once
cp -n $F/tools/sdk/lib/liblwip2-1460-feat.a $F/tools/sdk/lib/liblwip2-1460-feat.a.orig || true
[ -d $F/tools/sdk/lwip2/include.orig ] || cp -r $F/tools/sdk/lwip2/include $F/tools/sdk/lwip2/include.orig

# 2. patch the glue config (guarded #if !defined defaults)
python3 - <<'PY'
import re, io, os
p = os.path.expanduser('~/.platformio/packages/framework-arduinoespressif8266/tools/sdk/lwip2/builder/glue-lwip/arduino/lwipopts.h')
s = io.open(p, encoding='utf-8', errors='replace').read()
edits = [
 (r'#define TCP_SND_BUF\s+\(2 \* TCP_MSS\)', '#define TCP_SND_BUF                     (8 * TCP_MSS) /* SDWIFI-turbo: was 2*MSS */'),
 (r'#define TCP_WND\s+\(4 \* TCP_MSS\)',     '#define TCP_WND                         (6 * TCP_MSS) /* SDWIFI-turbo: was 4*MSS */'),
 (r'#define MEMP_NUM_TCP_SEG\s+10 // 16',    '#define MEMP_NUM_TCP_SEG                32 /* SDWIFI-turbo: was 10 */'),
 (r'#define PBUF_POOL_SIZE\s+10 // 16',      '#define PBUF_POOL_SIZE                  16 /* SDWIFI-turbo: was 10 */'),
]
n = 0
for pat, rep in edits:
    s2 = re.sub(pat, rep, s, count=1)
    if s2 != s: n += 1
    s = s2
io.open(p, 'w', encoding='utf-8').write(s)
print('%d/4 edits applied (0 = already patched)' % n)
PY

# 3. lwIP 2.1.3 source (github mirror of savannah upstream)
cd $B
chmod +x makefiles/* 2>/dev/null || true
if [ ! -f lwip2-src/README ]; then
  git clone --depth=1 -b STABLE-2_1_3_RELEASE https://github.com/lwip-tcpip/lwip lwip2-src
fi

# 4. build + install only the variant the turbo firmware links (1460 + features)
make -f makefiles/Makefile.build-lwip2 \
  target=arduino DEFINE_TARGET=ARDUINO SDK=../.. \
  LWIP_ESP=glue-esp/lwip-1.4-arduino/include \
  LWIP_LIB=liblwip2-1460-feat.a \
  LWIP_LIB_RELEASE=../../lib/liblwip2-1460-feat.a \
  LWIP_INCLUDES_RELEASE=../include \
  TOOLS=$T \
  TCP_MSS=1460 LWIP_FEATURES=1 LWIP_IPV6=0 \
  BUILD=build-1460-feat-v4 \
  install

echo "done - now: pio run -t clean && pio run"

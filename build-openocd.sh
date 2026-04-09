#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Build script for OpenOCD with JLink and STLink support.

set -e
set -u

CONFIGURE_FLAGS="
  --disable-werror
  --disable-ftdi
  --disable-ti-icdi
  --disable-ulink
  --disable-usb-blaster-2
  --disable-ft232r
  --disable-vsllink
  --disable-xds110
  --disable-osbdm
  --disable-opendous
  --disable-aice
  --disable-usbprog
  --disable-openprog
  --disable-rlink
  --disable-armjtagew
  --disable-kitprog
  --disable-usb-blaster
  --disable-presto
  --disable-openjtag
  --enable-jlink
  --enable-stlink
  --enable-internal-jimtcl
  --enable-internal-libjaylink
"

#
#   May need the following line if we decide to put the openocd sources in the Rebase repository.
#
# cd openocd

echo "=== Initializing submodules ==="
git submodule update --init jimtcl
git submodule update --init src/jtag/drivers/libjaylink

echo "=== Bootstrapping libjaylink ==="
if [ -x src/jtag/drivers/libjaylink/autogen.sh ]; then
    (cd src/jtag/drivers/libjaylink && ./autogen.sh)
else
    (cd src/jtag/drivers/libjaylink && autoreconf -i)
fi

echo "=== Bootstrapping OpenOCD ==="
./bootstrap

echo "=== Configuring ==="
# shellcheck disable=SC2086
./configure $CONFIGURE_FLAGS

echo "=== Building ==="
make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

echo "=== Build complete ==="

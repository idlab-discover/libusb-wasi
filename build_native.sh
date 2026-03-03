#!/bin/bash
set -e

# libusb-wasi native build script for macOS

echo "Starting native build of libusb..."

# Clean up previous WASI-specific configurations if any
if [ -f Makefile ]; then
    echo "Cleaning previous build..."
    make distclean || true
fi

# Generate configuration scripts if they don't exist
if [ ! -f configure ]; then
    echo "Running autogen.sh..."
    ./autogen.sh
fi

echo "Configuring for native host..."
# We explicitly disable udev as it's for Linux, and we are on macOS
./configure --disable-udev

echo "Building libusb library..."
make -j$(sysctl -n hw.ncpu)

echo "Building examples natively..."
gcc -I. -Ilibusb examples/lsusb.c libusb/.libs/libusb-1.0.a -o examples/lsusb -framework CoreFoundation -framework IOKit -framework Security
gcc -I. -Ilibusb examples/listdevs.c libusb/.libs/libusb-1.0.a -o examples/listdevs -framework CoreFoundation -framework IOKit -framework Security
gcc -I. -Ilibusb examples/read_device.c libusb/.libs/libusb-1.0.a -o examples/read_device -framework CoreFoundation -framework IOKit -framework Security

echo "Native libusb build complete."
echo "The static library is located at libusb/.libs/libusb-1.0.a"
echo "Examples: examples/lsusb, examples/read_device"

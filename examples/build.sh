#!/bin/bash
set -e

# Configuration
WASI_SDK_PATH="/opt/wasi-sdk"
WASM_TOOLS="wasm-tools"
WIT_PATH="../../wasi-usb/wit"
RUSB_SYSROOT="../../rusb-wasi/examples/wasi-workload/wasi-sysroot"

# Check dependencies
if ! command -v $WASM_TOOLS &> /dev/null; then
    echo "Error: wasm-tools could not be found"
    exit 1
fi

echo "=== Building C Component Workload ==="

# Function to build a component
build_component() {
    local src_file=$1
    local name=$(basename "$src_file" .c)
    
    echo "--- Building $name ---"
    
    echo "Compiling..."
    "$WASI_SDK_PATH/bin/clang" --target=wasm32-wasip2 --sysroot="$WASI_SDK_PATH/share/wasi-sysroot" \
        -I"$RUSB_SYSROOT/usr/include/libusb-1.0" \
        -c -o "$name.o" "$src_file"

    echo "Linking..."
    "$WASI_SDK_PATH/bin/wasm-ld" -m wasm32 \
        -L"$RUSB_SYSROOT/usr/lib" \
        -L"$WASI_SDK_PATH/share/wasi-sysroot/lib/wasm32-wasip2" \
        "$WASI_SDK_PATH/share/wasi-sysroot/lib/wasm32-wasip2/crt1-command.o" \
        -lusb-1.0 "$name.o" -lc \
        "$WASI_SDK_PATH/lib/clang/21/lib/wasm32-unknown-wasip2/libclang_rt.builtins.a" \
        -o "$name.wasm"

    echo "Componentizing..."
    "$WASM_TOOLS" component embed "$WIT_PATH" "$name.wasm" -o "$name.embedded.wasm" --world cguest
    "$WASM_TOOLS" component new "$name.embedded.wasm" -o "$name.component.wasm"

    # Cleanup
    rm "$name.o" "$name.wasm" "$name.embedded.wasm"
    echo "Created: $name.component.wasm"
}

echo "=== Building C Component Workloads ==="

build_component "read_device.c"
build_component "lsusb.c"

echo "=== Build Complete ==="


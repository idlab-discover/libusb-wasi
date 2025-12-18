# Building libusb for WASI

!!! The current implemetation only works for synchronous transfers as workaround have been used, this is because there is at time of writing no standard way spawning seperate threads for WASI/Wasmtime. !!!

Compiling `libusb` for use in WASI requires setting up the environment with the appropriate compiler and toolchain paths. The compiler and archiving tools are configured using environment variables:

## Setting up the Build Environment

First, set the WASI_SDK_PATH variable to point to your WASI SDK installation:

```sh
export WASI_SDK_PATH="/your/path/to/wasi-sdk"
```
Then configure the compiler and archiving tools:
```sh
export CC="$WASI_SDK_PATH/bin/clang --sysroot=$WASI_SDK_PATH/share/wasi-sysroot --target=wasm32-wasip2"
export AR="$WASI_SDK_PATH/bin/llvm-ar"
export RANLIB="$WASI_SDK_PATH/bin/llvm-ranlib"
```

With this setup, the build configuration can be initialized from the root of the `libusb` project:

```sh
./configure --host=wasm32-unknown-wasi --disable-shared --enable-static \
  --disable-udev CC="$CC" AR="$AR" RANLIB="$RANLIB"
```

If the configuration is missing, you may need to generate the `configure` script first by running:

```sh
./autogen.sh
```

This generates a Makefile suitable for building a static version of the library. To build the library:

```sh
make
```

The resulting static archive (`.a` file) will be located in the `.libs` directory. Since this archive does not yet include the object file for the WASI bindings, use the `ar` utility to insert it:

```sh
cp libusb/.libs/libusb-1.0.a libusb-wasi.a
ar r libusb-wasi.a /path/to/cguest_component_type.o
```

> The object file can be created using `wit-bindgen` on the WASI-USB interface definition.

## Compiling a Program with libusb for WASI Preview 2

Programs targeting WASI are expected to export a function with the following signature rather than a traditional `main`:

```c
bool exports_wasi_cli_run_run(void);
```

To compile such a program with `libusb` support:

### Method 1: Using the `WASI SDK Wrapper` (Recommended)

```sh
$WASI_SDK_PATH/bin/wasm32-wasip2-clang \
  --sysroot=$WASI_SDK_PATH/share/wasi-sysroot \
  -I./libusb \
  -mexec-model=reactor \
  /path/to/your/program.c \
  ./libusb-wasi.a \
  -o program.wasm
```

### Method 2: Using `clang` Directly

```sh
clang \
  --target=wasm32-wasip2 \
  --sysroot=$WASI_SDK_PATH/share/wasi-sysroot \
  -I$WASI_SDK_PATH/share/wasi-sysroot/include \
  -I./libusb \
  -mexec-model=reactor \
  /path/to/your/program.c \
  ./libusb-wasi.a \
  -o program.wasm
```

The final output, `program.wasm`, is compatible with a runtime that implements the host side of the WASI-USB interface and can be executed in the intended environment.

## Verification

To verify the output is a valid WebAssembly component:

```sh
file program.wasm
# Output: WWebAssembly (wasm) binary module version 0x1000d
```

## Legacy: Compiling for WASI Preview 1 (with Preview 2 Adaptation)

If you are using an older toolchain or require a workflow that starts with a WASI Preview 1 (p1) module, you can compile for `wasip1` and then adapt it to a Preview 2 component using `wasm-tools`.

### 1. Compile to WASI Preview 1

Use the `wasm32-wasip1` target to create a legacy `.wasm` module:

```sh
clang --target=wasm32-wasip1 \
  --sysroot=$WASI_SDK_PATH/share/wasi-sysroot \
  -I./libusb \
  -mexec-model=reactor \
  /path/to/your/program.c \
  ./libusb-wasi.a \
  -o program_p1.wasm

```

### 2. Adapt to WASI Preview 2

Since modern runtimes expect the Component Model format, you must wrap the p1 module using an adapter:

```sh
wasm-tools component new program_p1.wasm \
  --adapt helper/wasi_snapshot_preview1.reactor.wasm \
  -o adapted_p2.wasm

```

> **Note:** This method is considered deprecated in favor of the direct `wasm32-wasip2` compilation shown above, as it relies on an external adapter module (`wasi_snapshot_preview1.wasm`) to bridge the syscalls.
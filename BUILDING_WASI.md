# Building libusb for WASI

!!! The current implemetation only works for synchronous transfers as workaround have been used, this is because there is at time of writing no standard way spawning seperate threads for WASI/Wasmtime. !!!

Compiling `libusb` for use in WASI requires setting up the environment with the appropriate compiler and toolchain paths. The compiler and archiving tools are configured using environment variables:

```sh
export CC="$WASI_SDK_PATH/bin/clang --sysroot=$WASI_SDK_PATH/share/wasi-sysroot --target=wasm32-wasi"
export AR="$WASI_SDK_PATH/bin/llvm-ar"
export RANLIB="$WASI_SDK_PATH/bin/llvm-ranlib"
```

With this setup, the build configuration can be initialized from the root of the `libusb` project:

```sh
./configure --host=wasm32-unknown-wasi --disable-shared --enable-static \
  --disable-udev CC="$CC" AR="$AR" RANLIB="$RANLIB"
```

This generates a Makefile suitable for building a static version of the library. To build the library:

```sh
make
```

The resulting static archive (`.a` file) will be located in the `.libs` directory. Since this archive does not yet include the object file for the WASI bindings, use the `ar` utility to insert it:

```sh
ar r libusb-1.0.a /path/to/cguest_component_type.o
```

> The object file can be created using `wit-bindgen` on the WASI-USB interface definition.

## Compiling a Program with libusb for WASI

Programs targeting WASI are expected to export a function with the following signature rather than a traditional `main`:

```c
bool exports_wasi_cli_run_run(void);
```

To compile such a program with `libusb` support:

```sh
clang --target=wasm32-wasip1 -Ipath/to/libusb/libusb program.c \
  path/to/libusb/libusb/.libs/libusb-1.0.a -o program.wasm \
  -mexec-model=reactor
```

## Adapting to WASI 2

The resulting `program.wasm` is a WASI 1 component and needs to be adapted to WASI 2 format using `wasm-tools`:

```sh
wasm-tools component new program.wasm \
  --adapt helper/wasi_snapshot_preview1.reactor.wasm \
  -o adapted.wasm
```

The final output, `adapted.wasm`, is compatible with a runtime that implements the host side of the WASI-USB interface and can be executed in the intended environment.

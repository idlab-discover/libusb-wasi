# libusb-wasi

`libusb-wasi` is a customized version of the standard `libusb` C library, modified to run inside a WebAssembly (WASI) environment.

## Role in the Project
Instead of talking directly to Linux (`linux_usbfs`), macOS (`darwin`), or Windows backends, this fork introduces a **WASI backend** (`wasi_usb.c`). 

When a C program (or any language linking this library) makes a standard `libusb` call, the WASI backend translates the call to the **WASI-USB** interface using generated bindings (`cguest.o`). The surrounding Wasm host environment (e.g., `wasi-usb`) then fulfills the request.

This enables existing C/C++ applications that rely on `libusb` to be seamlessly compiled to WebAssembly without changing their application logic.

## Compiling for WebAssembly

The build produces a static archive (`libusb-wasi.a`) combined with component-type metadata.

1. Configure with the WASI SDK.
2. Build as usual with `make`.
3. The custom `wasi_usb` backend will automatically be utilized for the Wasm target.

See [BUILDING_WASI.md](./BUILDING_WASI.md) for detailed step-by-step instructions.
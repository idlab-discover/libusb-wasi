//
// Created by Robbe Leroy on 24/05/2025.
//

#ifndef WASI_USB_H
#define WASI_USB_H

#include <libusb.h>
#include "libusbi.h"
#include "cguest.h"

// WASI platform-specific structures
typedef struct {
    component_usb_device_own_usb_device_t device;
} wasi_device_priv_t;

typedef struct {
    component_usb_device_own_device_handle_t handle;
} wasi_device_handle_priv_t;

typedef struct {
    component_usb_transfers_own_transfer_t transfer;
    uint8_t *buffer;
    size_t buffer_size;
    int completed;
    int canceled;
} wasi_transfer_priv_t;

// Default timeout for transfers (in milliseconds)
#define WASI_DEFAULT_TIMEOUT 1000

// Convert between libusb error codes and WASI USB error codes
int libusb_error_from_wasi(component_usb_device_libusb_error_t error);

// Export backend interface
extern const struct usbi_os_backend usbi_backend;

#endif // WASI_USB_H

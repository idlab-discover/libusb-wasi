//
// Created by Robbe Leroy on 24/05/2025.
//
// wasm_backend.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libusb.h>
#include "libusbi.h"
#include "wasi_usb.h"
#include "cguest.h"

// Helper function to issue a control transfer to get device descriptor
static int get_device_descriptor(component_usb_device_borrow_usb_device_t device, 
                                struct libusb_device_descriptor *descriptor) {
    // We need to open the device to perform control transfers
    component_usb_device_own_device_handle_t handle;
    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_usb_device_open(device, &handle, &err)) {
        return libusb_error_from_wasi(err);
    }

    // Borrow the handle for control transfer
    component_usb_device_borrow_device_handle_t borrowed_handle = 
        component_usb_device_borrow_device_handle(handle);

    // Create a transfer to get the device descriptor
    component_usb_device_transfer_setup_t setup = {
        .bm_request_type = 0x80,  // IN | standard | device
        .b_request = 0x06,        // GET_DESCRIPTOR
        .w_value = 0x0100,        // Descriptor type 1 (device)
        .w_index = 0              // Language ID (not used for device)
    };

    component_usb_device_transfer_options_t opts = {
        .endpoint = 0,            // EP-0
        .timeout_ms = 1000,       // 1 second
        .stream_id = 0,
        .iso_packets = 0
    };

    // Create the transfer
    component_usb_device_own_transfer_t transfer;
    if (!component_usb_device_method_device_handle_new_transfer(
            borrowed_handle, 
            COMPONENT_USB_TRANSFERS_TRANSFER_TYPE_CONTROL, 
            &setup, 
            18,   // Device descriptor is 18 bytes
            &opts, 
            &transfer, 
            &err)) {
        component_usb_device_method_device_handle_close(borrowed_handle);
        component_usb_device_device_handle_drop_own(handle);
        return libusb_error_from_wasi(err);
    }

    // Submit the transfer with empty data (for IN request)
    component_usb_transfers_borrow_transfer_t borrowed_transfer = 
        component_usb_transfers_borrow_transfer(transfer);
    uint8_t empty_data = 0;
    cguest_list_u8_t empty_list = { &empty_data, 0 };
    component_usb_transfers_libusb_error_t submit_err;

    if (!component_usb_transfers_method_transfer_submit_transfer(
            borrowed_transfer, 
            &empty_list, 
            &submit_err)) {
        component_usb_transfers_transfer_drop_own(transfer);
        component_usb_device_method_device_handle_close(borrowed_handle);
        component_usb_device_device_handle_drop_own(handle);
        return libusb_error_from_wasi(submit_err);
    }

    // Wait for the transfer to complete
    cguest_list_u8_t result;
    component_usb_transfers_libusb_error_t await_err;
    int ret = LIBUSB_SUCCESS;

    if (!component_usb_transfers_await_transfer(transfer, &result, &await_err)) {
        ret = libusb_error_from_wasi(await_err);
    } else if (result.len != 18) {
        ret = LIBUSB_ERROR_IO;
    } else {
        // Parse descriptor fields
        descriptor->bLength = result.ptr[0];
        descriptor->bDescriptorType = result.ptr[1];
        descriptor->bcdUSB = (result.ptr[3] << 8) | result.ptr[2];
        descriptor->bDeviceClass = result.ptr[4];
        descriptor->bDeviceSubClass = result.ptr[5];
        descriptor->bDeviceProtocol = result.ptr[6];
        descriptor->bMaxPacketSize0 = result.ptr[7];
        descriptor->idVendor = (result.ptr[9] << 8) | result.ptr[8];
        descriptor->idProduct = (result.ptr[11] << 8) | result.ptr[10];
        descriptor->bcdDevice = (result.ptr[13] << 8) | result.ptr[12];
        descriptor->iManufacturer = result.ptr[14];
        descriptor->iProduct = result.ptr[15];
        descriptor->iSerialNumber = result.ptr[16];
        descriptor->bNumConfigurations = result.ptr[17];

        // Convert descriptor to host endian format
        usbi_localize_device_descriptor(descriptor);

        // Free the result buffer
        if (result.ptr) {
            free(result.ptr);
        }
    }

    // Clean up
    component_usb_device_method_device_handle_close(borrowed_handle);
    component_usb_device_device_handle_drop_own(handle);

    return ret;
}

// Map WASI USB error codes to libusb error codes
int libusb_error_from_wasi(component_usb_device_libusb_error_t error) {
    switch (error) {
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_IO:
            return LIBUSB_ERROR_IO;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_INVALID_PARAM:
            return LIBUSB_ERROR_INVALID_PARAM;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_ACCESS:
            return LIBUSB_ERROR_ACCESS;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_NO_DEVICE:
            return LIBUSB_ERROR_NO_DEVICE;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_FOUND:
            return LIBUSB_ERROR_NOT_FOUND;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_BUSY:
            return LIBUSB_ERROR_BUSY;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_TIMEOUT:
            return LIBUSB_ERROR_TIMEOUT;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_OVERFLOW:
            return LIBUSB_ERROR_OVERFLOW;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_PIPE:
            return LIBUSB_ERROR_PIPE;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_INTERRUPTED:
            return LIBUSB_ERROR_INTERRUPTED;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_NO_MEM:
            return LIBUSB_ERROR_NO_MEM;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_SUPPORTED:
            return LIBUSB_ERROR_NOT_SUPPORTED;
        case COMPONENT_USB_ERRORS_LIBUSB_ERROR_OTHER:
        default:
            return LIBUSB_ERROR_OTHER;
    }
}

// Get transfer private data
static wasi_transfer_priv_t *get_transfer_priv(struct usbi_transfer *itransfer) {
	printf("Getting transfer private data\n");
    return usbi_get_transfer_priv(itransfer);
}

// Get device private data
static wasi_device_priv_t *get_device_priv(struct libusb_device *dev) {
    printf("Getting device private data\n");
    return usbi_get_device_priv(dev);
}

// Get device handle private data
static wasi_device_handle_priv_t *get_handle_priv(struct libusb_device_handle *handle) {
    printf("Getting device handle private data\n");
    return usbi_get_device_handle_priv(handle);
}

// Platform specific code for threads
unsigned long usbi_get_tid(void) {
    return 1; // Single-threaded WASM environment
}

// Platform specific code for events
void usbi_cond_init(usbi_cond_t *cond) {
    // No-op in WASM environment
}

int usbi_create_event(usbi_event_t *event) {
    // Simple implementation for WASM

    return 0;
}

void usbi_destroy_event(usbi_event_t *event) {
    // No-op in WASM environment
}

void usbi_signal_event(usbi_event_t *event) {

}

void usbi_clear_event(usbi_event_t *event) {

}

// ---------------------------------------------------------------------------
// Initialization & teardown
// ---------------------------------------------------------------------------
static int wasm_init(struct libusb_context *ctx) {
    printf("Initializing WASM USB backend\n");
    // Calling component_usb_device_init from cguest.c
    component_usb_device_libusb_error_t err;
    if (!component_usb_device_init(&err)) {
        printf("WASM USB backend initialization failed: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    component_usb_usb_hotplug_libusb_error_t hotplug_err;
    if (!component_usb_usb_hotplug_enable_hotplug(&hotplug_err)) {
        usbi_warn(ctx, "WASM USB hotplug initialization failed: %d", hotplug_err);
        // Continue anyway, as this is not critical
    }

    printf("WASM USB backend initialized successfully\n");
    return LIBUSB_SUCCESS;
}

static void wasm_exit(struct libusb_context *ctx) {
    printf("Shutting down WASM USB backend\n");
    // No explicit cleanup needed - WebAssembly component resources are cleaned up automatically
    printf("WASM USB backend shutdown\n");
}

// ---------------------------------------------------------------------------
// Device discovery
// ---------------------------------------------------------------------------
static int wasm_get_device_list(struct libusb_context *ctx, struct discovered_devs **discdevs) {
    printf("Getting device list in WASM backend\n");
    component_usb_device_list_own_usb_device_t device_list;
    component_usb_device_libusb_error_t err;
    int r = 0;

    printf("Enumerating all USB devices on the system\n");

    // Call component_usb_device_list_devices from cguest.c to enumerate all devices
    if (!component_usb_device_list_devices(&device_list, &err)) {
        printf("Failed to enumerate USB devices: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    // Process each device in the list
    for (size_t i = 0; i < device_list.len; i++) {
        component_usb_device_own_usb_device_t *wasm_device = &device_list.ptr[i];
        component_usb_device_borrow_usb_device_t borrowed_device =
            component_usb_device_borrow_usb_device(*wasm_device);

        // Compute a unique session ID for this device
        // We use the device handle as the session ID
        unsigned long session_id = (unsigned long)wasm_device->__handle;

        printf("Found device with session ID: %lu\n", session_id);

        // Check if libusb already knows about this device
        struct libusb_device *dev = usbi_get_device_by_session_id(ctx, session_id);

        if (dev) {
            printf("Device with session ID %lu already known\n", session_id);
        } else {
            // Create a new libusb_device for this device
            printf("Allocating new device for session ID %lu\n", session_id);
            dev = usbi_alloc_device(ctx, session_id);
            if (!dev) {
                usbi_err(ctx, "Failed to allocate libusb device");
                continue;
            }

            // Store device handle in private data
            wasi_device_priv_t *priv = get_device_priv(dev);
            priv->device = *wasm_device;

            // Populate device fields
            // Get the device descriptor to fill in fields
            struct libusb_device_descriptor descriptor;
            memset(&descriptor, 0, sizeof(descriptor));

            r = get_device_descriptor(borrowed_device, &descriptor);
            if (r < 0) {
                printf("Failed to get device descriptor: %d\n", r);
                libusb_unref_device(dev);
                continue;
            }

            // printing the device descriptor for debugging
			printf("Device Descriptor:\n");
			printf("  bLength: %d\n", descriptor.bLength);
			printf("  bDescriptorType: %d\n", descriptor.bDescriptorType);
			printf("  bcdUSB: %04x\n", descriptor.bcdUSB);
			printf("  bDeviceClass: %d\n", descriptor.bDeviceClass);
			printf("  bDeviceSubClass: %d\n", descriptor.bDeviceSubClass);
			printf("  bDeviceProtocol: %d\n", descriptor.bDeviceProtocol);
			printf("  bMaxPacketSize0: %d\n", descriptor.bMaxPacketSize0);
			printf("  idVendor: %04x\n", descriptor.idVendor);
			printf("  idProduct: %04x\n", descriptor.idProduct);
			printf("  bcdDevice: %04x\n", descriptor.bcdDevice);
			printf("  iManufacturer: %d\n", descriptor.iManufacturer);
			printf("  iProduct: %d\n", descriptor.iProduct);
			printf("  iSerialNumber: %d\n", descriptor.iSerialNumber);
			printf("  bNumConfigurations: %d\n", descriptor.bNumConfigurations);

            // Copy the descriptor to the device
            dev->device_descriptor = descriptor;

            // For WASI, we don't have direct access to bus/port/address info
            // So we synthesize reasonable values based on device index
            dev->bus_number = 1;                // All devices on virtual bus 1
            dev->port_number = (uint8_t)(i + 1); // Port numbers start at 1
            dev->device_address = (uint8_t)(i + 1); // Device addresses start at 1
            dev->speed = LIBUSB_SPEED_UNKNOWN;  // We don't know the speed

            // Perform sanity checks on the device
            r = usbi_sanitize_device(dev);
            if (r < 0) {
                printf("Failed to sanitize device: %d\n", r);
                libusb_unref_device(dev);
                continue;
            }
        }

        // Add this device to the list of discovered devices
        struct discovered_devs *new_discdevs = discovered_devs_append(*discdevs, dev);
        if (!new_discdevs) {
            usbi_err(ctx, "Failed to append device to discovered devices list");
            libusb_unref_device(dev);
            r = LIBUSB_ERROR_NO_MEM;
            break;
        }

        *discdevs = new_discdevs;

        // We've referenced the device in discovered_devs_append, so we can
        // unref it here to maintain proper reference counting
        libusb_unref_device(dev);
    }

    // Free the device list (but not the devices themselves)
    free(device_list.ptr);

    return r;
}

// ---------------------------------------------------------------------------
// Open / close
// ---------------------------------------------------------------------------
static int wasm_open(struct libusb_device_handle *handle) {
    printf("Opening device handle in WASM backend\n");
    struct libusb_context *ctx = HANDLE_CTX(handle);
    struct libusb_device *dev = handle->dev;
    wasi_device_priv_t *dpriv = get_device_priv(dev);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    // Borrow the device from the priv data
    component_usb_device_borrow_usb_device_t borrowed_device =
        component_usb_device_borrow_usb_device(dpriv->device);

    // Call open method from cguest.c
    component_usb_device_own_device_handle_t wasm_handle;
    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_usb_device_open(borrowed_device, &wasm_handle, &err)) {
        printf("Failed to open USB device: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    // Store the handle
    hpriv->handle = wasm_handle;

    return LIBUSB_SUCCESS;
}

static void wasm_close(struct libusb_device_handle *handle) {
    printf("Closing device handle in WASM backend\n");
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    // Close the device by borrowing it first
    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    // Call close method from cguest.c
    component_usb_device_method_device_handle_close(borrowed_handle);

    // The resource is automatically dropped when this function returns
}

static void wasm_destroy_device(struct libusb_device *dev) {
    printf("Destroying device in WASM backend\n");
    // Nothing to do here - resources are managed by the WebAssembly component
}

// ---------------------------------------------------------------------------
// Descriptor retrieval
// ---------------------------------------------------------------------------
static int wasm_get_active_config_descriptor(struct libusb_device *dev, void *buffer, size_t len) {
    printf("Getting active configuration descriptor in WASM backend\n");
    struct libusb_context *ctx = DEVICE_CTX(dev);
    wasi_device_priv_t *dpriv = get_device_priv(dev);

    // Get configuration first
    uint8_t config_value = 0;

    // Since we don't have a handle yet, we need to open the device temporarily
    component_usb_device_borrow_usb_device_t borrowed_device =
        component_usb_device_borrow_usb_device(dpriv->device);

    component_usb_device_own_device_handle_t temp_handle;
    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_usb_device_open(borrowed_device, &temp_handle, &err)) {
        printf("Failed to open device for getting active config: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    // Borrow the handle
    component_usb_device_borrow_device_handle_t borrowed_handle =
        component_usb_device_borrow_device_handle(temp_handle);

    // Get configuration
    if (!component_usb_device_method_device_handle_get_configuration(borrowed_handle, &config_value, &err)) {
        component_usb_device_method_device_handle_close(borrowed_handle);
        component_usb_device_device_handle_drop_own(temp_handle);

        if (err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_FOUND) {
            printf("Device is not configured\n");
            return LIBUSB_ERROR_NOT_FOUND;
        }

        printf("Failed to get active configuration: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    // Close the temporary handle
    component_usb_device_method_device_handle_close(borrowed_handle);
    component_usb_device_device_handle_drop_own(temp_handle);

    // If device is not configured
    if (config_value == 0) {
        return LIBUSB_ERROR_NOT_FOUND;
    }

    // Now get the configuration descriptor
    component_usb_device_configuration_descriptor_t config_desc;

    if (!component_usb_device_method_usb_device_get_configuration_descriptor_by_value(
            borrowed_device, config_value, &config_desc, &err)) {
        printf("Failed to get configuration descriptor: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    // Ensure buffer is large enough
    if (len < config_desc.total_length) {
        usbi_err(ctx, "Buffer too small for configuration descriptor");
        return LIBUSB_ERROR_OVERFLOW;
    }

    // Copy descriptor to buffer (for now, a basic descriptor)
    struct usbi_configuration_descriptor *dest = (struct usbi_configuration_descriptor *)buffer;
    dest->bLength = config_desc.length;
    dest->bDescriptorType = config_desc.descriptor_type;
    dest->wTotalLength = libusb_cpu_to_le16(config_desc.total_length);
    dest->bNumInterfaces = config_desc.num_interfaces;
    dest->bConfigurationValue = config_desc.configuration_value;
    dest->iConfiguration = config_desc.configuration_index;
    dest->bmAttributes = config_desc.attributes;
    dest->bMaxPower = config_desc.max_power;

    // In a real implementation, we would also need to get interface and endpoint descriptors
    // and populate the rest of the configuration descriptor, but this is complex without direct access

    return (int)config_desc.total_length;
}

static int wasm_get_config_descriptor(struct libusb_device *dev, uint8_t config_index, void *buffer, size_t len) {
    printf("Getting configuration descriptor for index %u in WASM backend\n", config_index);
    struct libusb_context *ctx = DEVICE_CTX(dev);
    wasi_device_priv_t *dpriv = get_device_priv(dev);

    component_usb_device_borrow_usb_device_t borrowed_device =
        component_usb_device_borrow_usb_device(dpriv->device);

    component_usb_device_configuration_descriptor_t config_desc;
    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_usb_device_get_configuration_descriptor(
            borrowed_device, config_index, &config_desc, &err)) {
        printf("Failed to get configuration descriptor: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    // Ensure buffer is large enough
    if (len < config_desc.total_length) {
        usbi_err(ctx, "Buffer too small for configuration descriptor");
        return LIBUSB_ERROR_OVERFLOW;
    }

    // Copy descriptor to buffer (for now, a basic descriptor)
    struct usbi_configuration_descriptor *dest = (struct usbi_configuration_descriptor *)buffer;
    dest->bLength = config_desc.length;
    dest->bDescriptorType = config_desc.descriptor_type;
    dest->wTotalLength = libusb_cpu_to_le16(config_desc.total_length);
    dest->bNumInterfaces = config_desc.num_interfaces;
    dest->bConfigurationValue = config_desc.configuration_value;
    dest->iConfiguration = config_desc.configuration_index;
    dest->bmAttributes = config_desc.attributes;
    dest->bMaxPower = config_desc.max_power;

    return (int)config_desc.total_length;
}

static int wasm_get_config_descriptor_by_value(struct libusb_device *dev, uint8_t config_value, void **buffer) {
    printf("Getting configuration descriptor by value %u in WASM backend\n", config_value);
    struct libusb_context *ctx = DEVICE_CTX(dev);
    wasi_device_priv_t *dpriv = get_device_priv(dev);

    component_usb_device_borrow_usb_device_t borrowed_device =
        component_usb_device_borrow_usb_device(dpriv->device);

    component_usb_device_configuration_descriptor_t config_desc;
    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_usb_device_get_configuration_descriptor_by_value(
            borrowed_device, config_value, &config_desc, &err)) {
        printf("Failed to get configuration descriptor by value: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    // Allocate buffer for the descriptor
    *buffer = malloc(config_desc.total_length);
    if (!*buffer) {
        return LIBUSB_ERROR_NO_MEM;
    }

    // Copy descriptor to buffer (for now, a basic descriptor)
    struct usbi_configuration_descriptor *dest = (struct usbi_configuration_descriptor *)*buffer;
    dest->bLength = config_desc.length;
    dest->bDescriptorType = config_desc.descriptor_type;
    dest->wTotalLength = libusb_cpu_to_le16(config_desc.total_length);
    dest->bNumInterfaces = config_desc.num_interfaces;
    dest->bConfigurationValue = config_desc.configuration_value;
    dest->iConfiguration = config_desc.configuration_index;
    dest->bmAttributes = config_desc.attributes;
    dest->bMaxPower = config_desc.max_power;

    return (int)config_desc.total_length;
}

// ---------------------------------------------------------------------------
// Configuration & interface management
// ---------------------------------------------------------------------------
static int wasm_get_configuration(struct libusb_device_handle *handle, uint8_t *config) {
    printf("Getting configuration in WASM backend\n");
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_get_configuration(borrowed_handle, config, &err)) {
        printf("Failed to get configuration: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_set_configuration(struct libusb_device_handle *handle, int config) {
    printf("Setting configuration %d in WASM backend\n", config);
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_config_value_t cfg_value;

    if (config < 0) {
        cfg_value.tag = COMPONENT_USB_CONFIGURATION_CONFIG_VALUE_UNCONFIGURED;
    } else {
        cfg_value.tag = COMPONENT_USB_CONFIGURATION_CONFIG_VALUE_VALUE;
        cfg_value.val.value = (uint8_t)config;
    }

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_set_configuration(borrowed_handle, &cfg_value, &err)) {
        printf("Failed to set configuration: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_claim_interface(struct libusb_device_handle *handle, uint8_t interface_number) {
    printf("Claiming interface %d in WASM backend\n", interface_number);
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_claim_interface(
            borrowed_handle, interface_number, &err)) {
        printf("Failed to claim interface: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_release_interface(struct libusb_device_handle *handle, uint8_t interface_number) {
    printf("Releasing interface %d in WASM backend\n", interface_number);
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_release_interface(
            borrowed_handle, interface_number, &err)) {
        printf("Failed to release interface: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_set_interface_altsetting(struct libusb_device_handle *handle,
                                        uint8_t interface_number, uint8_t altsetting) {
	printf("Setting interface %d altsetting %d in WASM backend\n",
            interface_number, altsetting);
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_set_interface_altsetting(
            borrowed_handle, interface_number, altsetting, &err)) {
        printf("Failed to set interface altsetting: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_clear_halt(struct libusb_device_handle *handle, unsigned char endpoint) {
    printf("Clearing halt on endpoint %02x in WASM backend\n", endpoint);
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_clear_halt(
            borrowed_handle, endpoint, &err)) {
        printf("Failed to clear halt: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_reset_device(struct libusb_device_handle *handle) {
    printf("Resetting device in WASM backend\n");
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_reset_device(borrowed_handle, &err)) {
        printf("Failed to reset device: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

// ---------------------------------------------------------------------------
// Kernel driver management
// ---------------------------------------------------------------------------
static int wasm_kernel_driver_active(struct libusb_device_handle *handle, uint8_t interface_number) {
    printf("Checking if kernel driver is active for interface %d in WASM backend\n", interface_number);
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    bool active;
    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_kernel_driver_active(
            borrowed_handle, interface_number, &active, &err)) {
        if (err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_SUPPORTED) {
            // Not supported is a valid response for this API
            return 0;
        }
        printf("Failed to check kernel driver active: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return active ? 1 : 0;
}

static int wasm_detach_kernel_driver(struct libusb_device_handle *handle, uint8_t interface_number) {
    printf("Detaching kernel driver for interface %d in WASM backend\n", interface_number);
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_detach_kernel_driver(
            borrowed_handle, interface_number, &err)) {
        if (err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_SUPPORTED) {
            return LIBUSB_ERROR_NOT_SUPPORTED;
        }
        printf("Failed to detach kernel driver: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_attach_kernel_driver(struct libusb_device_handle *handle, uint8_t interface_number) {
    printf("Attaching kernel driver for interface %d in WASM backend\n", interface_number);
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_attach_kernel_driver(
            borrowed_handle, interface_number, &err)) {
        if (err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_SUPPORTED) {
            return LIBUSB_ERROR_NOT_SUPPORTED;
        }
        printf("Failed to attach kernel driver: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

// ---------------------------------------------------------------------------
// USB 3.0 Streams
// ---------------------------------------------------------------------------
static int wasm_alloc_streams(struct libusb_device_handle *handle, uint32_t num_streams,
                             unsigned char *endpoints, int num_endpoints) {
	printf("Allocating %u streams for %d endpoints in WASM backend\n",
            num_streams, num_endpoints);
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    cguest_list_u8_t endpoint_list = {
        .ptr = endpoints,
        .len = num_endpoints
    };

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_alloc_streams(
            borrowed_handle, num_streams, &endpoint_list, &err)) {
        if (err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_SUPPORTED) {
            return LIBUSB_ERROR_NOT_SUPPORTED;
        }
        printf("Failed to allocate streams: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_free_streams(struct libusb_device_handle *handle,
                            unsigned char *endpoints, int num_endpoints) {
	printf("Freeing streams for %d endpoints in WASM backend\n", num_endpoints);
    struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    cguest_list_u8_t endpoint_list = {
        .ptr = endpoints,
        .len = num_endpoints
    };

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_free_streams(
            borrowed_handle, &endpoint_list, &err)) {
        if (err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_SUPPORTED) {
            return LIBUSB_ERROR_NOT_SUPPORTED;
        }
        printf("Failed to free streams: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

// ---------------------------------------------------------------------------
// Transfer management
// ---------------------------------------------------------------------------
static int wasm_submit_transfer(struct usbi_transfer *itransfer) {
    printf("Submitting transfer in WASM backend\n");
    struct libusb_transfer *transfer = USBI_TRANSFER_TO_LIBUSB_TRANSFER(itransfer);
    struct libusb_context *ctx = TRANSFER_CTX(transfer);
    struct libusb_device_handle *handle = transfer->dev_handle;
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);
    wasi_transfer_priv_t *tpriv = get_transfer_priv(itransfer);
    
    // Clear transfer private data first to ensure safe state
    tpriv->buffer = NULL;
    tpriv->buffer_size = 0;
    tpriv->completed = 0;
    tpriv->canceled = 0;
    
    // Log transfer information
    printf("Transfer type: %d, length: %u, endpoint: 0x%02x\n",
           transfer->type, transfer->length, transfer->endpoint);
    
    // Validate the transfer before proceeding
    if (!transfer->buffer && transfer->length > 0) {
        printf("Transfer buffer is NULL but length is %d\n", transfer->length);
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    
    // Get device handle
    component_usb_device_borrow_device_handle_t borrowed_handle = 
        component_usb_device_borrow_device_handle(hpriv->handle);
    
    // Static buffer for zero-length transfers to prevent NULL pointers
    static uint8_t dummy_buffer = 0;
    
    // Variables for transfer creation
    component_usb_device_transfer_type_t xfer_type;
    component_usb_device_transfer_setup_t setup = {0};
    component_usb_device_transfer_options_t opts = {0};
    uint32_t buffer_size = 0;
    
    // Set common options
    opts.endpoint = transfer->endpoint;
    opts.timeout_ms = transfer->timeout;
    opts.stream_id = itransfer->stream_id;
    
    // Data for submission - initialize to safe values
    cguest_list_u8_t submit_data = {&dummy_buffer, 0};
    
    // Handle different transfer types
    if (transfer->type == LIBUSB_TRANSFER_TYPE_CONTROL) {
        xfer_type = COMPONENT_USB_TRANSFERS_TRANSFER_TYPE_CONTROL;
        
        if (transfer->length < LIBUSB_CONTROL_SETUP_SIZE) {
            printf("Control transfer with insufficient buffer length: %d\n", transfer->length);
            return LIBUSB_ERROR_INVALID_PARAM;
        }
        
        // Extract control setup
        struct libusb_control_setup *ctrl = (struct libusb_control_setup *)transfer->buffer;
        uint16_t wLength = libusb_le16_to_cpu(ctrl->wLength);
        
        setup.bm_request_type = ctrl->bmRequestType;
        setup.b_request = ctrl->bRequest;
        setup.w_value = libusb_le16_to_cpu(ctrl->wValue);
        setup.w_index = libusb_le16_to_cpu(ctrl->wIndex);
        
        printf("Control transfer: bmRequestType=0x%02x, bRequest=0x%02x, wValue=0x%04x, wIndex=0x%04x\n",
               setup.bm_request_type, setup.b_request, setup.w_value, setup.w_index);
        printf("Control transfer: wLength=%u\n", wLength);
        
        // Set the buffer size to match what's expected in the control setup
        buffer_size = wLength;
        
        // For control IN transfers, we submit an empty buffer
        if (true) {
            printf("Control IN transfer, using dummy buffer\n");
            // Keep using the dummy buffer with zero length
        } else {
            // For control OUT, use data after the setup packet if available
            if (transfer->length > LIBUSB_CONTROL_SETUP_SIZE) {
                submit_data.ptr = transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE;
                submit_data.len = transfer->length - LIBUSB_CONTROL_SETUP_SIZE;
                if (submit_data.len > wLength) {
                    submit_data.len = wLength;
                }
            }
        }
    } else {
        // Handle non-control transfers
        buffer_size = transfer->length;
        
        if (transfer->type == LIBUSB_TRANSFER_TYPE_BULK) {
            xfer_type = COMPONENT_USB_TRANSFERS_TRANSFER_TYPE_BULK;
        } else if (transfer->type == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
            xfer_type = COMPONENT_USB_TRANSFERS_TRANSFER_TYPE_INTERRUPT;
        } else if (transfer->type == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
            xfer_type = COMPONENT_USB_TRANSFERS_TRANSFER_TYPE_ISOCHRONOUS;
            opts.iso_packets = transfer->num_iso_packets;
        } else {
            printf("Unsupported transfer type: %d\n", transfer->type);
            return LIBUSB_ERROR_INVALID_PARAM;
        }
        
        // For non-control transfers, data is the entire buffer if it exists
        if (buffer_size > 0 && transfer->buffer != NULL) {
            submit_data.ptr = transfer->buffer;
            submit_data.len = buffer_size;
        }
    }
    
    // Create the transfer
    component_usb_device_own_transfer_t wasm_transfer;
    component_usb_device_libusb_error_t err;
    
    if (!component_usb_device_method_device_handle_new_transfer(
            borrowed_handle, xfer_type, &setup, buffer_size, &opts, &wasm_transfer, &err)) {
        printf("Failed to create transfer: %d\n", err);
        return libusb_error_from_wasi(err);
    }
    
    // Store transfer handle in private data
    tpriv->transfer = wasm_transfer;
    
    // Submit the transfer
    component_usb_transfers_borrow_transfer_t borrowed_transfer = 
        component_usb_transfers_borrow_transfer(wasm_transfer);
    
    component_usb_transfers_libusb_error_t submit_err;
    
    if (!component_usb_transfers_method_transfer_submit_transfer(borrowed_transfer, &submit_data, &submit_err)) {
        printf("Failed to submit transfer: %d\n", submit_err);
        component_usb_transfers_transfer_drop_own(wasm_transfer);
        return libusb_error_from_wasi(submit_err);
    }
    
    // Mark the transfer as in flight
    itransfer->state_flags |= USBI_TRANSFER_IN_FLIGHT;
    
    // Await the transfer completion (blocking behavior)
    cguest_list_u8_t result;
    component_usb_transfers_libusb_error_t await_err;

    printf("DEBUG: Awaiting transfer for bmRequestType=0x%02x, bRequest=0x%02x, wValue=0x%04x, wIndex=0x%04x, wLength=%u\n",
           setup.bm_request_type, setup.b_request, setup.w_value, setup.w_index, buffer_size);
    
    if (!component_usb_transfers_await_transfer(wasm_transfer, &result, &await_err)) {
        // Transfer failed
        printf("Transfer failed: %d\n", await_err);
        
        // Handle the error
        if (await_err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_TIMEOUT) {
            itransfer->state_flags |= USBI_TRANSFER_TIMED_OUT;
            usbi_handle_transfer_completion(itransfer, LIBUSB_TRANSFER_TIMED_OUT);
        } else {
            usbi_handle_transfer_completion(itransfer, LIBUSB_TRANSFER_ERROR);
        }
        
        return LIBUSB_SUCCESS; // Return success since we handled the error
    }
    
    // Process successful transfer
    printf("DEBUG: Transfer awaited. await_err=%d, result.len=%zu\n", await_err, result.len);
    if (true) {
		printf("Processing IN transfer\n");
		//printing result
		printf("Received %zu bytes in transfer\n", result.len);
        // Only copy data if we have a valid result buffer
        if (result.ptr && result.len > 0) {
            if (transfer->type == LIBUSB_TRANSFER_TYPE_CONTROL) {
                // For control IN transfers, copy result after setup packet
                printf("Received %zu bytes in control transfer\n", result.len);
                
                // Calculate safe amount to copy
                size_t max_copy = transfer->length - LIBUSB_CONTROL_SETUP_SIZE;
                size_t to_copy = (result.len <= max_copy) ? result.len : max_copy;
                
                if (to_copy > 0) {
                    memcpy(transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE, result.ptr, to_copy);
                    transfer->actual_length = LIBUSB_CONTROL_SETUP_SIZE + to_copy;
                } else {
                    transfer->actual_length = LIBUSB_CONTROL_SETUP_SIZE;
                }
            } else {
                // For non-control IN transfers
                printf("Received %zu bytes in transfer\n", result.len);
                
                size_t to_copy = (result.len <= transfer->length) ? result.len : transfer->length;
                
                if (to_copy > 0) {
                    memcpy(transfer->buffer, result.ptr, to_copy);
                    transfer->actual_length = to_copy;
                } else {
                    transfer->actual_length = 0;
                }
            }
        } else {
            // No data received
            if (transfer->type == LIBUSB_TRANSFER_TYPE_CONTROL) {
                transfer->actual_length = LIBUSB_CONTROL_SETUP_SIZE;
            } else {
                transfer->actual_length = 0;
            }
        }
    } else {
        // For OUT transfers, set actual_length to submitted length
        transfer->actual_length = transfer->length;
    }

    printf("Transfer completed successfully, actual length: %d\n", transfer->actual_length);
    
    // Free the result data using a safer approach
    if (result.ptr != NULL) {
        // Make a local copy of the pointer to avoid any double-free issues
        void *ptr_to_free = result.ptr;
        result.ptr = NULL; // Clear the pointer first
        //free(ptr_to_free); // Then free the memory
    }
    
    printf("Transfer submitted successfully, buffer size: %zu\n", buffer_size); 

    // Mark transfer as completed
    tpriv->completed = 1;
    
    // Report completion
    usbi_handle_transfer_completion(itransfer, LIBUSB_TRANSFER_COMPLETED);
    
    return LIBUSB_SUCCESS;
}

static int wasm_cancel_transfer(struct usbi_transfer *itransfer) {
    printf("Cancelling transfer in WASM backend\n");
    struct libusb_transfer *transfer = USBI_TRANSFER_TO_LIBUSB_TRANSFER(itransfer);
    struct libusb_context *ctx = TRANSFER_CTX(transfer);
    wasi_transfer_priv_t *tpriv = get_transfer_priv(itransfer);

    // Check if transfer is in flight
    if (!(itransfer->state_flags & USBI_TRANSFER_IN_FLIGHT)) {
        return LIBUSB_ERROR_NOT_FOUND;
    }

    // Check if already completed
    if (tpriv->completed) {
        return LIBUSB_ERROR_NOT_FOUND;
    }

    // Check if already canceling
    if (tpriv->canceled || (itransfer->state_flags & USBI_TRANSFER_CANCELLING)) {
        return LIBUSB_SUCCESS;
    }

    // Mark as cancelling
    itransfer->state_flags |= USBI_TRANSFER_CANCELLING;
    tpriv->canceled = 1;

    // Cancel the transfer
    component_usb_transfers_borrow_transfer_t borrowed_transfer =
        component_usb_transfers_borrow_transfer(tpriv->transfer);

    component_usb_transfers_libusb_error_t err;

    if (!component_usb_transfers_method_transfer_cancel_transfer(borrowed_transfer, &err)) {
        printf("Failed to cancel transfer: %d\n", err);
        return libusb_error_from_wasi(err);
    }

    // Report cancellation
    usbi_handle_transfer_cancellation(itransfer);

    return LIBUSB_SUCCESS;
}

static void wasm_clear_transfer_priv(struct usbi_transfer *itransfer) {
    printf("Clearing transfer private data in WASM backend\n");
    wasi_transfer_priv_t *tpriv = get_transfer_priv(itransfer);
    
    // Free any resources
    if (tpriv && tpriv->buffer) {
        free(tpriv->buffer);
        tpriv->buffer = NULL;
    }
    
    // Clear flags
    if (tpriv) {
        tpriv->completed = 0;
        tpriv->canceled = 0;
    }
}

// ---------------------------------------------------------------------------
// Memory management
// ---------------------------------------------------------------------------
static void *wasm_dev_mem_alloc(struct libusb_device_handle *handle, size_t len) {
	printf("Allocating %zu bytes of device memory in WASM backend\n", len);
    return malloc(len);
}

static int wasm_dev_mem_free(struct libusb_device_handle *handle, void *buffer, size_t len) {
    printf("Freeing %zu bytes of device memory in WASM backend\n", len);
    free(buffer);
    return LIBUSB_SUCCESS;
}

// ---------------------------------------------------------------------------
// Event handling
// ---------------------------------------------------------------------------
static int wasm_handle_events(struct libusb_context *ctx, void *event_data,
                              unsigned int count, unsigned int num_ready) {
	printf("Handling events in WASM backend\n");
    // Poll for hotplug events

    // In a real implementation, we would check for completed transfers here
    // For now, our implementation completes transfers synchronously in submit_transfer

    return LIBUSB_SUCCESS;
}

// ---------------------------------------------------------------------------
// Event handling functions required by libusb core
// ---------------------------------------------------------------------------

// Implement usbi_cond_timedwait for WASI
int usbi_cond_timedwait(usbi_cond_t *cond, usbi_mutex_t *mutex, const struct timeval *tv) {
    // In WASI, we don't have real condition variables or threads
    // This is a simplified implementation that returns immediately with a status

    // For consistency with other backends, unlock the mutex while "waiting"
    usbi_mutex_unlock(mutex);

    // Simulate a short wait, then return an appropriate result
    // In a real implementation, we would actually wait for an event
    usbi_mutex_lock(mutex);

    // For demo purposes, we'll simulate a timeout for longer waits
    if (tv->tv_sec > 0 || tv->tv_usec > 100000) {
        return LIBUSB_ERROR_TIMEOUT;
    }

    return 0; // Simulated success (as if condition was signaled)
}

// Implement usbi_alloc_event_data for WASI
int usbi_alloc_event_data(struct libusb_context *ctx) {
    printf("Allocating event data in WASM backend\n");
    // Free any existing event data
    if (ctx->event_data) {
        free(ctx->event_data);
        ctx->event_data = NULL;
    }

    // Reset the counter
    ctx->event_data_cnt = 0;

    // Since WASI doesn't have real file descriptors for events,
    // we just need a minimal structure to satisfy libusb core
    struct pollfd *fds = calloc(1, sizeof(struct pollfd));
    if (!fds)
        return LIBUSB_ERROR_NO_MEM;

    // Create a dummy pollfd for the event
    fds[0].fd = -1;      // Not a real file descriptor
    fds[0].events = POLLIN;

    ctx->event_data = fds;
    ctx->event_data_cnt = 1;

    return 0;
}

// Implement usbi_wait_for_events for WASI
int usbi_wait_for_events(struct libusb_context *ctx,
                          struct usbi_reported_events *reported_events, int timeout_ms) {
	printf("Waiting for events in WASM backend\n");
    // Initialize the reported events structure
    reported_events->event_triggered = 0;
    reported_events->num_ready = 0;
    reported_events->event_data = NULL;
    reported_events->event_data_count = 0;

    // Check for hotplug events by calling the poll function
    component_usb_usb_hotplug_list_tuple3_event_info_own_usb_device_t events;
    component_usb_usb_hotplug_poll_events(&events);

    // If we have events, mark that the event was triggered
    if (events.len > 0) {
        reported_events->event_triggered = 1;
        reported_events->num_ready = 1;

        // Process and free the events
        for (size_t i = 0; i < events.len; i++) {
            component_usb_usb_hotplug_tuple3_event_info_own_usb_device_t *event = &events.ptr[i];
            component_usb_device_usb_device_drop_own(event->f2);
        }
        free(events.ptr);

        return LIBUSB_SUCCESS;
    }

    // Simulate timeout or interruption based on timeout value
    if (timeout_ms == 0) {
        return LIBUSB_ERROR_TIMEOUT;
    } else if (timeout_ms < 0) {
        // This is an infinite wait, but we don't want to block forever
        // So we'll just simulate an interruption
        return LIBUSB_ERROR_INTERRUPTED;
    } else if (timeout_ms < 100) {
        // Short timeout, pretend we timed out
        return LIBUSB_ERROR_TIMEOUT;
    }

    // For longer timeouts, just simulate an interruption
    return LIBUSB_ERROR_INTERRUPTED;
}

// For WASI hotplug support
static void wasm_hotplug_poll(void) {
    printf("Polling for hotplug events in WASM backend\n");
    // Poll for hotplug events
    component_usb_usb_hotplug_list_tuple3_event_info_own_usb_device_t events;
    component_usb_usb_hotplug_poll_events(&events);
    
    // Process events
    if (events.len > 0) {
        for (size_t i = 0; i < events.len; i++) {
            component_usb_usb_hotplug_tuple3_event_info_own_usb_device_t *event = &events.ptr[i];
            
            // Free the device resource
            component_usb_device_usb_device_drop_own(event->f2);
        }
        
        // Free the event list
        free(events.ptr);
    }
}

// Define the os_backend struct that libusb will use
const struct usbi_os_backend usbi_backend = {
    .name = "wasi",
    .caps = USBI_CAP_HAS_HID_ACCESS | USBI_CAP_SUPPORTS_DETACH_KERNEL_DRIVER,
    
    // Initialization & cleanup
    .init = wasm_init,
    .exit = wasm_exit,
    .set_option = NULL,
    
    // Device discovery
    .get_device_list = wasm_get_device_list,
    .hotplug_poll = wasm_hotplug_poll,  // Use our local function here
    .open = wasm_open,
    .close = wasm_close,
    .get_active_config_descriptor = wasm_get_active_config_descriptor,
    .get_config_descriptor = wasm_get_config_descriptor,
    .get_config_descriptor_by_value = wasm_get_config_descriptor_by_value,
    
    // Configuration
    .get_configuration = wasm_get_configuration,
    .set_configuration = wasm_set_configuration,
    .claim_interface = wasm_claim_interface,
    .release_interface = wasm_release_interface,
    .set_interface_altsetting = wasm_set_interface_altsetting,
    .clear_halt = wasm_clear_halt,
    .reset_device = wasm_reset_device,
    
    // USB 3.0 streams
    .alloc_streams = wasm_alloc_streams,
    .free_streams = wasm_free_streams,
    
    // Memory management
    .dev_mem_alloc = wasm_dev_mem_alloc,
    .dev_mem_free = wasm_dev_mem_free,
    
    // Kernel driver
    .kernel_driver_active = wasm_kernel_driver_active,
    .detach_kernel_driver = wasm_detach_kernel_driver,
    .attach_kernel_driver = wasm_attach_kernel_driver,
    
    // Device lifecycle
    .destroy_device = wasm_destroy_device,
    
    // Transfers
    .submit_transfer = wasm_submit_transfer,
    .cancel_transfer = wasm_cancel_transfer,
    .clear_transfer_priv = wasm_clear_transfer_priv,
    
    // Event handling
    .handle_events = wasm_handle_events,
    
    // Backend-private data sizes
    .context_priv_size = 0,
    .device_priv_size = sizeof(wasi_device_priv_t),
    .device_handle_priv_size = sizeof(wasi_device_handle_priv_t),
    .transfer_priv_size = sizeof(wasi_transfer_priv_t),
};
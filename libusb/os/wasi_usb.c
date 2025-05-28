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
	    return usbi_get_transfer_priv(itransfer);
}

// Get device private data
static wasi_device_priv_t *get_device_priv(struct libusb_device *dev) {
        return usbi_get_device_priv(dev);
}

// Get device handle private data
static wasi_device_handle_priv_t *get_handle_priv(struct libusb_device_handle *handle) {
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
        // Calling component_usb_device_init from cguest.c
    component_usb_device_libusb_error_t err;
    if (!component_usb_device_init(&err)) {
                return libusb_error_from_wasi(err);
    }

    component_usb_usb_hotplug_libusb_error_t hotplug_err;
    if (!component_usb_usb_hotplug_enable_hotplug(&hotplug_err)) {
        usbi_warn(ctx, "WASM USB hotplug initialization failed: %d", hotplug_err);
        // Continue anyway, as this is not critical
    }

        return LIBUSB_SUCCESS;
}

static void wasm_exit(struct libusb_context *ctx) {
        // No explicit cleanup needed - WebAssembly component resources are cleaned up automatically
    }

// ---------------------------------------------------------------------------
// Device discovery
// ---------------------------------------------------------------------------
static int wasm_get_device_list(struct libusb_context *ctx, struct discovered_devs **discdevs) {
        component_usb_device_list_tuple3_own_usb_device_device_descriptor_device_location_t device_list;
    component_usb_device_libusb_error_t err;
    int r = 0;

    
    // Call component_usb_device_list_devices from cguest.c to enumerate all devices
    if (!component_usb_device_list_devices(&device_list, &err)) {
                return libusb_error_from_wasi(err);
    }

    // Process each device in the list
    for (size_t i = 0; i < device_list.len; i++) {
        component_usb_device_own_usb_device_t *wasm_device = &device_list.ptr[i].f0;
        component_usb_device_borrow_usb_device_t borrowed_device =
            component_usb_device_borrow_usb_device(*wasm_device);

        // Compute a unique session ID for this device
        // We use the device handle as the session ID
        unsigned long session_id = (unsigned long)wasm_device->__handle;

        
        // Check if libusb already knows about this device
        struct libusb_device *dev = usbi_get_device_by_session_id(ctx, session_id);

        if (dev) {
                    } else {
            // Create a new libusb_device for this device
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

            descriptor.bLength = device_list.ptr[i].f1.length;
            descriptor.bDescriptorType = device_list.ptr[i].f1.descriptor_type;
            descriptor.bcdUSB = device_list.ptr[i].f1.usb_version_bcd;
            descriptor.bDeviceClass = device_list.ptr[i].f1.device_class;
            descriptor.bDeviceSubClass = device_list.ptr[i].f1.device_subclass;
            descriptor.bDeviceProtocol = device_list.ptr[i].f1.device_protocol;
            descriptor.bMaxPacketSize0 = device_list.ptr[i].f1.max_packet_size0;
            descriptor.idVendor = device_list.ptr[i].f1.vendor_id;
            descriptor.idProduct = device_list.ptr[i].f1.product_id;
            descriptor.bcdDevice = device_list.ptr[i].f1.device_version_bcd;
            descriptor.iManufacturer = device_list.ptr[i].f1.manufacturer_index;
            descriptor.iProduct = device_list.ptr[i].f1.product_index;
            descriptor.iSerialNumber = device_list.ptr[i].f1.serial_number_index;
            descriptor.bNumConfigurations = device_list.ptr[i].f1.num_configurations;
																																													
            // set the descriptor to the device
            dev->device_descriptor = descriptor;

            dev->bus_number = device_list.ptr[i].f2.bus_number;                // All devices on virtual bus 1
            dev->port_number = device_list.ptr[i].f2.port_number;                // Port number is not applicable in WASM
            dev->device_address = device_list.ptr[i].f2.device_address;  // Device address is assigned by the host
            dev->speed = device_list.ptr[i].f2.speed; // Speed is determined by the host

            // Perform sanity checks on the device
            r = usbi_sanitize_device(dev);
            if (r < 0) {
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
                return libusb_error_from_wasi(err);
    }

    // Store the handle
    hpriv->handle = wasm_handle;

    return LIBUSB_SUCCESS;
}

static void wasm_close(struct libusb_device_handle *handle) {
        wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    // Close the device by borrowing it first
    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    // Call close method from cguest.c
    component_usb_device_method_device_handle_close(borrowed_handle);

    // The resource is automatically dropped when this function returns
}

static void wasm_destroy_device(struct libusb_device *dev) {
        // Nothing to do here - resources are managed by the WebAssembly component
}

// ---------------------------------------------------------------------------
// Descriptor retrieval
// ---------------------------------------------------------------------------
static int wasm_get_active_config_descriptor(struct libusb_device *dev, void *buffer, size_t len) {
    struct libusb_context *ctx = DEVICE_CTX(dev);
    wasi_device_priv_t *dpriv = get_device_priv(dev);

    // Borrow the device
    component_usb_device_borrow_usb_device_t borrowed_device =
        component_usb_device_borrow_usb_device(dpriv->device);

    // Since we don't have a handle yet, we need to open the device temporarily
    component_usb_device_own_device_handle_t temp_handle;
    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_usb_device_open(borrowed_device, &temp_handle, &err)) {
        return libusb_error_from_wasi(err);
    }

    // Borrow the handle
    component_usb_device_borrow_device_handle_t borrowed_handle =
        component_usb_device_borrow_device_handle(temp_handle);

    // Get configuration value
    uint8_t config_value = 0;
    if (!component_usb_device_method_device_handle_get_configuration(borrowed_handle, &config_value, &err)) {
        component_usb_device_method_device_handle_close(borrowed_handle);
        component_usb_device_device_handle_drop_own(temp_handle);

        if (err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_FOUND) {
            return LIBUSB_ERROR_NOT_FOUND;
        }
        return libusb_error_from_wasi(err);
    }

    // Close the temporary handle - we don't need it anymore
    component_usb_device_method_device_handle_close(borrowed_handle);
    component_usb_device_device_handle_drop_own(temp_handle);

    // If device is not configured
    if (config_value == 0) {
        return LIBUSB_ERROR_NOT_FOUND;
    }

    // Get the active configuration descriptor using the config_value
    component_usb_device_configuration_descriptor_t config_desc;
    if (!component_usb_device_method_usb_device_get_configuration_descriptor_by_value(
            borrowed_device, config_value, &config_desc, &err)) {
        return libusb_error_from_wasi(err);
    }

    printf("Active configuration descriptor: total_length=%u, num_interfaces=%u\n", 
           config_desc.total_length, config_desc.interfaces.len);

    // Check if buffer is too small - but continue with partial copy instead of returning error
    if (len < config_desc.total_length) {
        usbi_dbg(ctx, "Buffer too small for active config descriptor (need %u, have %zu) - will do partial copy",
                 config_desc.total_length, len);
    }

    // Start filling the descriptor
    uint8_t *ptr = buffer;
    uint8_t *end = ptr + len; // Never go beyond this point

    // Fill in the main configuration descriptor
    struct usbi_configuration_descriptor *config = (struct usbi_configuration_descriptor *)ptr;
    
    // Make sure we have at least enough space for the config descriptor header
    if (len >= sizeof(struct usbi_configuration_descriptor)) {
        config->bLength = config_desc.length;
        config->bDescriptorType = config_desc.descriptor_type;
        config->wTotalLength = libusb_cpu_to_le16(config_desc.total_length);
        config->bNumInterfaces = config_desc.interfaces.len;
        config->bConfigurationValue = config_desc.configuration_value;
        config->iConfiguration = config_desc.configuration_index;
        config->bmAttributes = config_desc.attributes;
        config->bMaxPower = config_desc.max_power;
    
        ptr += config_desc.length;
    } else {
        // Not even enough space for the header - copy what we can
        size_t to_copy = len;
        memset(buffer, 0, to_copy); // Zero first to ensure fields are initialized
        memcpy(buffer, &config_desc, to_copy);
        
        // Free the descriptor resources that were allocated by the component
        component_usb_descriptors_configuration_descriptor_free(&config_desc);
        
        // Return the amount actually copied
        return (int)to_copy;
    }

    // Process each interface - only if we have space left
    for (size_t i = 0; i < config_desc.interfaces.len && ptr < end; i++) {
        component_usb_descriptors_interface_descriptor_t *interface = &config_desc.interfaces.ptr[i];
        
        // Make sure we have enough space for this interface descriptor
        if (ptr + interface->length > end) {
            // Not enough space for this entire interface, copy what we can
            size_t space_left = end - ptr;
            if (space_left > 0) {
                memcpy(ptr, interface, space_left);
                ptr += space_left;
            }
            break; // No more room
        }
        
        // Fill in interface descriptor
        struct usbi_interface_descriptor *if_desc = (struct usbi_interface_descriptor *)ptr;
        if_desc->bLength = interface->length;
        if_desc->bDescriptorType = interface->descriptor_type;
        if_desc->bInterfaceNumber = interface->interface_number;
        if_desc->bAlternateSetting = interface->alternate_setting;
        if_desc->bNumEndpoints = interface->endpoints.len;
        if_desc->bInterfaceClass = interface->interface_class;
        if_desc->bInterfaceSubClass = interface->interface_subclass;
        if_desc->bInterfaceProtocol = interface->interface_protocol;
        if_desc->iInterface = interface->interface_index;

        ptr += interface->length;

        // Process each endpoint in this interface
        for (size_t j = 0; j < interface->endpoints.len && ptr < end; j++) {
            component_usb_descriptors_endpoint_descriptor_t *ep = &interface->endpoints.ptr[j];
            
            // Make sure we have enough space for this endpoint descriptor
            if (ptr + ep->length > end) {
                // Not enough space for this entire endpoint, copy what we can
                size_t space_left = end - ptr;
                if (space_left > 0) {
                    memcpy(ptr, ep, space_left);
                    ptr += space_left;
                }
                break; // No more room
            }
            
            // Fill in endpoint descriptor
            struct usbi_descriptor_header *ep_desc = (struct usbi_descriptor_header *)ptr;
            ep_desc->bLength = ep->length;
            ep_desc->bDescriptorType = ep->descriptor_type;
            
            if (ep->length >= 3) {
                ptr[2] = ep->endpoint_address;  // bEndpointAddress
            }
            if (ep->length >= 4) {
                ptr[3] = ep->attributes;        // bmAttributes
            }
            if (ep->length >= 6) {
                // Fill in wMaxPacketSize (little-endian)
                ptr[4] = ep->max_packet_size & 0xFF;
                ptr[5] = (ep->max_packet_size >> 8) & 0xFF;
            }
            if (ep->length >= 7) {
                ptr[6] = ep->interval;          // bInterval
            }
            if (ep->length >= 8) {
                ptr[7] = ep->refresh;           // bRefresh
            }
            if (ep->length >= 9) {
                ptr[8] = ep->synch_address;     // bSynchAddress
            }
            
            ptr += ep->length;
        }
    }

    // Calculate how many bytes we actually filled in
    size_t filled_length = ptr - (uint8_t *)buffer;
    
    printf("Filled %zu bytes of active configuration descriptor (requested buffer size: %zu)\n", 
           filled_length, len);
    
    // Free the descriptor resources that were allocated by the component
    component_usb_descriptors_configuration_descriptor_free(&config_desc);
    
    // Return the amount of data we actually provided
    return (int)filled_length;
}

static int wasm_get_config_descriptor(struct libusb_device *dev, uint8_t config_index, void *buffer, size_t len) {
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

    printf("Configuration descriptor: total_length=%u, num_interfaces=%u\n", 
           config_desc.total_length, config_desc.interfaces.len);

    // Check if buffer is too small - but continue with partial copy instead of returning error
    if (len < config_desc.total_length) {
        usbi_dbg(ctx, "Buffer too small for configuration descriptor (need %u, have %zu) - will do partial copy",
                 config_desc.total_length, len);
    }

    // Start filling the descriptor
    uint8_t *ptr = buffer;
    uint8_t *end = ptr + len; // Never go beyond this point

    // Fill in the main configuration descriptor
    struct usbi_configuration_descriptor *config = (struct usbi_configuration_descriptor *)ptr;
    
    // Make sure we have at least enough space for the config descriptor header
    if (len >= sizeof(struct usbi_configuration_descriptor)) {
        config->bLength = config_desc.length;
        config->bDescriptorType = config_desc.descriptor_type;
        config->wTotalLength = libusb_cpu_to_le16(config_desc.total_length);
        config->bNumInterfaces = config_desc.interfaces.len;
        config->bConfigurationValue = config_desc.configuration_value;
        config->iConfiguration = config_desc.configuration_index;
        config->bmAttributes = config_desc.attributes;
        config->bMaxPower = config_desc.max_power;
    
        ptr += config_desc.length;
    } else {
        // Not even enough space for the header - copy what we can
        size_t to_copy = len;
        memset(buffer, 0, to_copy); // Zero first to ensure fields are initialized
        memcpy(buffer, &config_desc, to_copy);
        
        // Free the descriptor resources that were allocated by the component
        component_usb_descriptors_configuration_descriptor_free(&config_desc);
        
        // Return the amount actually copied
        return (int)to_copy;
    }

    // Process each interface - only if we have space left
    for (size_t i = 0; i < config_desc.interfaces.len && ptr < end; i++) {
        component_usb_descriptors_interface_descriptor_t *interface = &config_desc.interfaces.ptr[i];
        
        // Make sure we have enough space for this interface descriptor
        if (ptr + interface->length > end) {
            // Not enough space for this entire interface, copy what we can
            size_t space_left = end - ptr;
            if (space_left > 0) {
                memcpy(ptr, interface, space_left);
                ptr += space_left;
            }
            break; // No more room
        }
        
        // Fill in interface descriptor
        struct usbi_interface_descriptor *if_desc = (struct usbi_interface_descriptor *)ptr;
        if_desc->bLength = interface->length;
        if_desc->bDescriptorType = interface->descriptor_type;
        if_desc->bInterfaceNumber = interface->interface_number;
        if_desc->bAlternateSetting = interface->alternate_setting;
        if_desc->bNumEndpoints = interface->endpoints.len;
        if_desc->bInterfaceClass = interface->interface_class;
        if_desc->bInterfaceSubClass = interface->interface_subclass;
        if_desc->bInterfaceProtocol = interface->interface_protocol;
        if_desc->iInterface = interface->interface_index;

        ptr += interface->length;

        // Process each endpoint in this interface
        for (size_t j = 0; j < interface->endpoints.len && ptr < end; j++) {
            component_usb_descriptors_endpoint_descriptor_t *ep = &interface->endpoints.ptr[j];
            
            // Make sure we have enough space for this endpoint descriptor
            if (ptr + ep->length > end) {
                // Not enough space for this entire endpoint, copy what we can
                size_t space_left = end - ptr;
                if (space_left > 0) {
                    memcpy(ptr, ep, space_left);
                    ptr += space_left;
                }
                break; // No more room
            }
            
            // Fill in endpoint descriptor
            struct usbi_descriptor_header *ep_desc = (struct usbi_descriptor_header *)ptr;
            ep_desc->bLength = ep->length;
            ep_desc->bDescriptorType = ep->descriptor_type;
            
            if (ep->length >= 3) {
                ptr[2] = ep->endpoint_address;  // bEndpointAddress
            }
            if (ep->length >= 4) {
                ptr[3] = ep->attributes;        // bmAttributes
            }
            if (ep->length >= 6) {
                // Fill in wMaxPacketSize (little-endian)
                ptr[4] = ep->max_packet_size & 0xFF;
                ptr[5] = (ep->max_packet_size >> 8) & 0xFF;
            }
            if (ep->length >= 7) {
                ptr[6] = ep->interval;          // bInterval
            }
            if (ep->length >= 8) {
                ptr[7] = ep->refresh;           // bRefresh
            }
            if (ep->length >= 9) {
                ptr[8] = ep->synch_address;     // bSynchAddress
            }
            
            ptr += ep->length;
        }
    }

    // Calculate how many bytes we actually filled in
    size_t filled_length = ptr - (uint8_t *)buffer;
    
    printf("Filled %zu bytes of configuration descriptor (requested buffer size: %zu)\n", filled_length, len);
    
    // Free the descriptor resources that were allocated by the component
    component_usb_descriptors_configuration_descriptor_free(&config_desc);
    
    // Return the amount of data we actually provided
    return (int)filled_length;
}

static int wasm_get_config_descriptor_by_value(struct libusb_device *dev, uint8_t config_value, void **buffer) {
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

    printf("Config descriptor by value: total_length=%u, num_interfaces=%u\n", 
           config_desc.total_length, config_desc.interfaces.len);

    // Allocate a buffer large enough for the entire descriptor
    *buffer = calloc(1, config_desc.total_length);
    if (!*buffer) {
        component_usb_descriptors_configuration_descriptor_free(&config_desc);
        return LIBUSB_ERROR_NO_MEM;
    }

    // Start filling the descriptor
    uint8_t *ptr = *buffer;
    uint8_t *end = ptr + config_desc.total_length;

    // Fill in the main configuration descriptor
    struct usbi_configuration_descriptor *config = (struct usbi_configuration_descriptor *)ptr;
    config->bLength = config_desc.length;
    config->bDescriptorType = config_desc.descriptor_type;
    config->wTotalLength = libusb_cpu_to_le16(config_desc.total_length);
    config->bNumInterfaces = config_desc.interfaces.len;
    config->bConfigurationValue = config_desc.configuration_value;
    config->iConfiguration = config_desc.configuration_index;
    config->bmAttributes = config_desc.attributes;
    config->bMaxPower = config_desc.max_power;

    ptr += config_desc.length;

    // Process each interface
    for (size_t i = 0; i < config_desc.interfaces.len && ptr < end; i++) {
        component_usb_descriptors_interface_descriptor_t *interface = &config_desc.interfaces.ptr[i];
        
        // Fill in interface descriptor
        struct usbi_interface_descriptor *if_desc = (struct usbi_interface_descriptor *)ptr;
        if_desc->bLength = interface->length;
        if_desc->bDescriptorType = interface->descriptor_type;
        if_desc->bInterfaceNumber = interface->interface_number;
        if_desc->bAlternateSetting = interface->alternate_setting;
        if_desc->bNumEndpoints = interface->endpoints.len;
        if_desc->bInterfaceClass = interface->interface_class;
        if_desc->bInterfaceSubClass = interface->interface_subclass;
        if_desc->bInterfaceProtocol = interface->interface_protocol;
        if_desc->iInterface = interface->interface_index;

        ptr += interface->length;

        // Process each endpoint in this interface
        for (size_t j = 0; j < interface->endpoints.len && ptr < end; j++) {
            component_usb_descriptors_endpoint_descriptor_t *ep = &interface->endpoints.ptr[j];
            
            // Fill in endpoint descriptor
            struct usbi_descriptor_header *ep_desc = (struct usbi_descriptor_header *)ptr;
            ep_desc->bLength = ep->length;
            ep_desc->bDescriptorType = ep->descriptor_type;
            
            if (ep->length >= 3) {
                ptr[2] = ep->endpoint_address;  // bEndpointAddress
            }
            if (ep->length >= 4) {
                ptr[3] = ep->attributes;        // bmAttributes
            }
            if (ep->length >= 6) {
                // Fill in wMaxPacketSize (little-endian)
                ptr[4] = ep->max_packet_size & 0xFF;
                ptr[5] = (ep->max_packet_size >> 8) & 0xFF;
            }
            if (ep->length >= 7) {
                ptr[6] = ep->interval;          // bInterval
            }
            if (ep->length >= 8) {
                ptr[7] = ep->refresh;           // bRefresh
            }
            if (ep->length >= 9) {
                ptr[8] = ep->synch_address;     // bSynchAddress
            }
            
            ptr += ep->length;
        }
    }

    // Calculate how many bytes we actually filled in
    size_t filled_length = ptr - (uint8_t *)*buffer;
    
    printf("Filled %zu bytes of configuration descriptor by value\n", filled_length);
    
    // Free the descriptor resources that were allocated by the component
    component_usb_descriptors_configuration_descriptor_free(&config_desc);
    
    // Return the amount of data we actually provided
    return (int)filled_length;
}

// ---------------------------------------------------------------------------
// Configuration & interface management
// ---------------------------------------------------------------------------
static int wasm_get_configuration(struct libusb_device_handle *handle, uint8_t *config) {
        struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_get_configuration(borrowed_handle, config, &err)) {
                return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_set_configuration(struct libusb_device_handle *handle, int config) {
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
                return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_claim_interface(struct libusb_device_handle *handle, uint8_t interface_number) {
        struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_claim_interface(
            borrowed_handle, interface_number, &err)) {
                return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_release_interface(struct libusb_device_handle *handle, uint8_t interface_number) {
        struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_release_interface(
            borrowed_handle, interface_number, &err)) {
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
                return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_clear_halt(struct libusb_device_handle *handle, unsigned char endpoint) {
        struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_clear_halt(
            borrowed_handle, endpoint, &err)) {
                return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_reset_device(struct libusb_device_handle *handle) {
        struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_reset_device(borrowed_handle, &err)) {
                return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

// ---------------------------------------------------------------------------
// Kernel driver management
// ---------------------------------------------------------------------------
static int wasm_kernel_driver_active(struct libusb_device_handle *handle, uint8_t interface_number) {
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
                return libusb_error_from_wasi(err);
    }

    return active ? 1 : 0;
}

static int wasm_detach_kernel_driver(struct libusb_device_handle *handle, uint8_t interface_number) {
        struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_detach_kernel_driver(
            borrowed_handle, interface_number, &err)) {
        if (err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_SUPPORTED) {
            return LIBUSB_ERROR_NOT_SUPPORTED;
        }
                return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_attach_kernel_driver(struct libusb_device_handle *handle, uint8_t interface_number) {
        struct libusb_context *ctx = HANDLE_CTX(handle);
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);

    component_usb_device_borrow_device_handle_t borrowed_handle = component_usb_device_borrow_device_handle(hpriv->handle);

    component_usb_device_libusb_error_t err;

    if (!component_usb_device_method_device_handle_attach_kernel_driver(
            borrowed_handle, interface_number, &err)) {
        if (err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_NOT_SUPPORTED) {
            return LIBUSB_ERROR_NOT_SUPPORTED;
        }
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
                return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

static int wasm_free_streams(struct libusb_device_handle *handle,
                            unsigned char *endpoints, int num_endpoints) {
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
                return libusb_error_from_wasi(err);
    }

    return LIBUSB_SUCCESS;
}

// ---------------------------------------------------------------------------
// Transfer management
// ---------------------------------------------------------------------------
static int wasm_submit_transfer(struct usbi_transfer *itransfer) {
        struct libusb_transfer *transfer = USBI_TRANSFER_TO_LIBUSB_TRANSFER(itransfer);
    struct libusb_context *ctx = TRANSFER_CTX(transfer);
    struct libusb_device_handle *handle = transfer->dev_handle;
    wasi_device_handle_priv_t *hpriv = get_handle_priv(handle);
    wasi_transfer_priv_t *tpriv = get_transfer_priv(itransfer);
    
    // Initialize transfer private data
    tpriv->buffer = NULL;
    tpriv->buffer_size = 0;
    tpriv->completed = 0;
    tpriv->canceled = 0;
    
    printf("Transfer type: %d, length: %u, endpoint: 0x%02x\n",
           transfer->type, transfer->length, transfer->endpoint);
    
    // Validate the transfer
    if (!transfer->buffer && transfer->length > 0) {
                return LIBUSB_ERROR_INVALID_PARAM;
    }
    
    // Get device handle
    component_usb_device_borrow_device_handle_t borrowed_handle = 
        component_usb_device_borrow_device_handle(hpriv->handle);
    
    // Static buffer for zero-length transfers
    static uint8_t dummy_buffer = 0;
    
    // Variables for transfer setup
    component_usb_device_transfer_type_t xfer_type;
    component_usb_device_transfer_setup_t setup = {0};
    component_usb_device_transfer_options_t opts = {0};
    uint32_t buffer_size = 0;
    
    // Set common options
    opts.endpoint = transfer->endpoint;
    opts.timeout_ms = transfer->timeout;
    opts.stream_id = itransfer->stream_id;
    
    // Data for submission - initialize with safe defaults
    cguest_list_u8_t submit_data = {&dummy_buffer, 0};
    
    // Handle different transfer types
    if (transfer->type == LIBUSB_TRANSFER_TYPE_CONTROL) {
        xfer_type = COMPONENT_USB_TRANSFERS_TRANSFER_TYPE_CONTROL;
        
        if (transfer->length < LIBUSB_CONTROL_SETUP_SIZE) {
                        return LIBUSB_ERROR_INVALID_PARAM;
        }
        
        // Extract control setup
        struct libusb_control_setup *ctrl = (struct libusb_control_setup *)transfer->buffer;
        uint16_t wLength = libusb_le16_to_cpu(ctrl->wLength);
        
        setup.bm_request_type = ctrl->bmRequestType;
        setup.b_request = ctrl->bRequest;
        setup.w_value = libusb_le16_to_cpu(ctrl->wValue);
        setup.w_index = libusb_le16_to_cpu(ctrl->wIndex);
                
        // Set the buffer size to match the wLength in the setup packet
        buffer_size = wLength;
        
        if (true) {
            // For control IN transfers, submit an empty buffer
                        submit_data.ptr = &dummy_buffer;
            submit_data.len = 0;
        } else {
            // For control OUT, use data after the setup packet
                        if (transfer->length > LIBUSB_CONTROL_SETUP_SIZE) {
                submit_data.ptr = transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE;
                submit_data.len = transfer->length - LIBUSB_CONTROL_SETUP_SIZE;
                if (submit_data.len > wLength) {
                    submit_data.len = wLength;
                }
            } else {
                submit_data.ptr = &dummy_buffer;
                submit_data.len = 0;
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
                        return LIBUSB_ERROR_INVALID_PARAM;
        }
        
        // For non-control transfers, submit the entire buffer
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
                return libusb_error_from_wasi(err);
    }
    
    // Store transfer handle
    tpriv->transfer = wasm_transfer;
    
    // Submit the transfer
    component_usb_transfers_borrow_transfer_t borrowed_transfer = 
        component_usb_transfers_borrow_transfer(wasm_transfer);
    
    component_usb_transfers_libusb_error_t submit_err;
    
    if (!component_usb_transfers_method_transfer_submit_transfer(borrowed_transfer, &submit_data, &submit_err)) {
                component_usb_transfers_transfer_drop_own(wasm_transfer);
        return libusb_error_from_wasi(submit_err);
    }
    
        
    // Mark the transfer as in flight
    itransfer->state_flags |= USBI_TRANSFER_IN_FLIGHT;
    
    // Wait for the transfer to complete
    cguest_list_u8_t result = {NULL, 0};
    component_usb_transfers_libusb_error_t await_err;
    
        
    if (!component_usb_transfers_await_transfer(wasm_transfer, &result, &await_err)) {
        // Transfer failed
                
        if (await_err == COMPONENT_USB_ERRORS_LIBUSB_ERROR_TIMEOUT) {
            itransfer->state_flags |= USBI_TRANSFER_TIMED_OUT;
            usbi_handle_transfer_completion(itransfer, LIBUSB_TRANSFER_TIMED_OUT);
        } else {
            usbi_handle_transfer_completion(itransfer, LIBUSB_TRANSFER_ERROR);
        }
        
        return LIBUSB_SUCCESS; // We handled the error
    }
    
    // Process successful transfer
        
    // Debug log received data
    if (result.ptr && result.len > 0) {
                for (size_t i = 0; i < result.len && i < 32; i++) {
                    }
            }
    
    // Handle the transfer based on type and direction
    if (IS_XFERIN(transfer)) {
        if (transfer->type == LIBUSB_TRANSFER_TYPE_CONTROL) {
            // For control IN transfers
            if (result.ptr && result.len > 0) {
                // Important: For control transfers, we only copy the result data
                // to the buffer after the setup packet. We DO NOT touch the setup packet.
                if (transfer->length >= LIBUSB_CONTROL_SETUP_SIZE + result.len) {
                    // Buffer is large enough for the data
                    memcpy(transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE, result.ptr, result.len);
                    
                    // Set actual_length to exactly the data length (not including setup packet)
                    // For control transfers, libusb expects just the result length, not setup packet length
                    transfer->actual_length = result.len;
                    
                    printf("Control IN: copied %zu bytes after setup packet, actual_length=%d\n", 
                          result.len, transfer->actual_length);
                } else {
                    // Buffer is too small, copy what we can
                    size_t to_copy = transfer->length - LIBUSB_CONTROL_SETUP_SIZE;
                    if (to_copy > 0) {
                        memcpy(transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE, result.ptr, to_copy);
                        transfer->actual_length = to_copy;
                        printf("Control IN: buffer too small, copied %zu bytes, actual_length=%d\n", 
                              to_copy, transfer->actual_length);
                    } else {
                        // No space after setup packet
                        transfer->actual_length = 0;
                    }
                }
            } else {
                // No data received
                transfer->actual_length = 0;
                            }
        } else {
            // For non-control IN transfers (bulk, interrupt, iso)
            if (result.ptr && result.len > 0) {
                // Copy as much as will fit in the buffer
                size_t to_copy = (result.len <= transfer->length) ? result.len : transfer->length;
                memcpy(transfer->buffer, result.ptr, to_copy);
                transfer->actual_length = to_copy;
                printf("Non-control IN: copied %zu bytes, actual_length=%d\n", 
                      to_copy, transfer->actual_length);
            } else {
                // No data received
                transfer->actual_length = 0;
                            }
        }
    } else {
        // For OUT transfers
        if (transfer->type == LIBUSB_TRANSFER_TYPE_CONTROL) {
            // Control OUT transfers include the setup packet in the count
            transfer->actual_length = result.len;
        } else {
            // Non-control OUT transfers
            transfer->actual_length = result.len;
        }
            }
    
    // Set the transferred bytes count in the internal transfer struct
    itransfer->transferred = transfer->actual_length;
    
    // Free the result data safely
    if (result.ptr) {
        void *ptr_to_free = result.ptr;
        result.ptr = NULL;  // Clear first to avoid double-free issues
        // free(ptr_to_free);
    }
    
    // Mark transfer as completed
    tpriv->completed = 1;
    
    // Report completion
    usbi_handle_transfer_completion(itransfer, LIBUSB_TRANSFER_COMPLETED);
    
    return LIBUSB_SUCCESS;
}

static int wasm_cancel_transfer(struct usbi_transfer *itransfer) {
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
                return libusb_error_from_wasi(err);
    }

    // Report cancellation
    usbi_handle_transfer_cancellation(itransfer);

    return LIBUSB_SUCCESS;
}

static void wasm_clear_transfer_priv(struct usbi_transfer *itransfer) {
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
	    return malloc(len);
}

static int wasm_dev_mem_free(struct libusb_device_handle *handle, void *buffer, size_t len) {
        free(buffer);
    return LIBUSB_SUCCESS;
}

// ---------------------------------------------------------------------------
// Event handling
// ---------------------------------------------------------------------------
static int wasm_handle_events(struct libusb_context *ctx, void *event_data,
                              unsigned int count, unsigned int num_ready) {
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
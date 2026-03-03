#include "libusb.h"
#include <stdio.h>
#include <unistd.h>

void print_endpoint(const struct libusb_endpoint_descriptor *endpoint) {
  printf("              |__ Endpoint %02x: ", endpoint->bEndpointAddress);

  if ((endpoint->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
      LIBUSB_ENDPOINT_IN) {
    printf("IN ");
  } else {
    printf("OUT ");
  }

  switch (endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) {
  case LIBUSB_TRANSFER_TYPE_CONTROL:
    printf("Control");
    break;
  case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
    printf("Isochronous");
    break;
  case LIBUSB_TRANSFER_TYPE_BULK:
    printf("Bulk");
    break;
  case LIBUSB_TRANSFER_TYPE_INTERRUPT:
    printf("Interrupt");
    break;
  }

  printf(" MaxPacket %d\n", endpoint->wMaxPacketSize);
}

void print_altsetting(const struct libusb_interface_descriptor *interface) {
  printf("          |__ Alt %02d: Class %02x SubClass %02x Protocol %02x\n",
         interface->bAlternateSetting, interface->bInterfaceClass,
         interface->bInterfaceSubClass, interface->bInterfaceProtocol);

  int i;
  for (i = 0; i < interface->bNumEndpoints; i++) {
    print_endpoint(&interface->endpoint[i]);
  }
}

void print_interface(const struct libusb_interface *interface) {
  int i;
  for (i = 0; i < interface->num_altsetting; i++) {
    print_altsetting(&interface->altsetting[i]);
  }
}

void print_configuration(struct libusb_config_descriptor *config) {
  printf("  |__ Config %02d: MaxPower %dmA\n", config->bConfigurationValue,
         config->MaxPower * 2);

  int i;
  for (i = 0; i < config->bNumInterfaces; i++) {
    printf("      |__ Interface %02d\n", i);
    print_interface(&config->interface[i]);
  }
}

void print_device(libusb_device *dev) {
  struct libusb_device_descriptor desc;
  struct libusb_config_descriptor *config;
  int r;
  uint8_t bus = libusb_get_bus_number(dev);
  uint8_t address = libusb_get_device_address(dev);

  r = libusb_get_device_descriptor(dev, &desc);
  if (r < 0) {
    fprintf(stderr, "failed to get device descriptor\n");
    return;
  }

  printf("%03d | %03d    | %04x:%04x | Device\n", bus, address, desc.idVendor,
         desc.idProduct);

  int i;
  for (i = 0; i < desc.bNumConfigurations; i++) {
    r = libusb_get_config_descriptor(dev, i, &config);
    if (r != LIBUSB_SUCCESS) {
      fprintf(stderr, "Couldn't retrieve config descriptor %d\n", i);
      continue;
    }

    print_configuration(config);
    libusb_free_config_descriptor(config);
  }
}

int main(void) {
  libusb_context *ctx = NULL;
  libusb_device **devs;
  int r;
  ssize_t cnt;

  r = libusb_init(&ctx);
  if (r < 0)
    return r;

  cnt = libusb_get_device_list(ctx, &devs);
  if (cnt < 0) {
    libusb_exit(ctx);
    return (int)cnt;
  }

  printf("Bus | Device | VID:PID | Description\n");
  printf("-------------------------------------\n");

  ssize_t i;
  for (i = 0; i < cnt; i++) {
    print_device(devs[i]);
  }

  libusb_free_device_list(devs, 1);
  libusb_exit(ctx);
  return 0;
}

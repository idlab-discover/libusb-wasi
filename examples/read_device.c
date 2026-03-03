#include "libusb.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mass Storage Class Code
#define LIBUSB_CLASS_MASS_STORAGE 0x08

// SCSI Commands
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_READ10 0x28
#define SCSI_WRITE10 0x2A

// FAT32 Offsets
#define MBR_PARTITION_TABLE_OFFSET 446
#define FAT_SECTOR_SIZE 512

typedef struct {
  uint8_t config;
  int iface;
  int setting;
  uint8_t address_in;
  uint8_t address_out;
} endpoint_t;

// Command Block Wrapper (CBW)
struct cbw_t {
  uint8_t signature[4];
  uint32_t tag;
  uint32_t data_transfer_length;
  uint8_t flags;
  uint8_t lun;
  uint8_t cb_length;
  uint8_t cb[16];
} __attribute__((packed));

// Command Status Wrapper (CSW)
struct csw_t {
  uint8_t signature[4];
  uint32_t tag;
  uint32_t data_residue;
  uint8_t status;
} __attribute__((packed));

static uint32_t tag_counter = 1;

static bool find_mass_storage_endpoint(libusb_device *dev, endpoint_t *ep) {
  struct libusb_config_descriptor *config_desc = NULL;
  struct libusb_device_descriptor desc;

  int r = libusb_get_device_descriptor(dev, &desc);
  if (r < 0)
    return false;

  for (int i = 0; i < desc.bNumConfigurations; i++) {
    r = libusb_get_config_descriptor(dev, i, &config_desc);
    if (r < 0)
      continue;

    for (int j = 0; j < config_desc->bNumInterfaces; j++) {
      const struct libusb_interface *inter = &config_desc->interface[j];
      for (int k = 0; k < inter->num_altsetting; k++) {
        const struct libusb_interface_descriptor *inter_desc =
            &inter->altsetting[k];

        if (inter_desc->bInterfaceClass == LIBUSB_CLASS_MASS_STORAGE) {
          int in_addr = -1;
          int out_addr = -1;

          for (int l = 0; l < inter_desc->bNumEndpoints; l++) {
            const struct libusb_endpoint_descriptor *ep_desc =
                &inter_desc->endpoint[l];
            if ((ep_desc->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
                LIBUSB_TRANSFER_TYPE_BULK) {
              if ((ep_desc->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                  LIBUSB_ENDPOINT_IN) {
                in_addr = ep_desc->bEndpointAddress;
              } else {
                out_addr = ep_desc->bEndpointAddress;
              }
            }
          }

          if (in_addr != -1 && out_addr != -1) {
            ep->config = config_desc->bConfigurationValue;
            ep->iface = inter_desc->bInterfaceNumber;
            ep->setting = inter_desc->bAlternateSetting;
            ep->address_in = in_addr;
            ep->address_out = out_addr;
            libusb_free_config_descriptor(config_desc);
            return true;
          }
        }
      }
    }
    libusb_free_config_descriptor(config_desc);
  }
  return false;
}

static int send_cbw(libusb_device_handle *handle, uint8_t endpoint_out,
                    uint32_t len, uint8_t flags, uint8_t *cb, uint8_t cb_len) {
  struct cbw_t cbw;
  memset(&cbw, 0, sizeof(cbw));
  memcpy(cbw.signature, "USBC", 4);
  cbw.tag = tag_counter;
  cbw.data_transfer_length = len;
  cbw.flags = flags;
  cbw.lun = 0;
  cbw.cb_length = cb_len;
  memcpy(cbw.cb, cb, cb_len);

  int actual_length;
  int r = libusb_bulk_transfer(handle, endpoint_out, (unsigned char *)&cbw,
                               sizeof(cbw), &actual_length, 5000);

  // Note: On WASI-USB, OUT transfers might return 0 actual_length even on
  // success because the host might not report it back in the current
  // implementation.
  if (r < 0) {
    fprintf(stderr, "Failed to send CBW: %s\n", libusb_error_name(r));
    return -1;
  }

  if (actual_length != sizeof(cbw)) {
    // Just warn, don't fail, for now
    fprintf(stderr,
            "Warning: CBW sent %d bytes, expected %lu (continuing...)\n",
            actual_length, sizeof(cbw));
  }

  return 0;
}

static int read_csw(libusb_device_handle *handle, uint8_t endpoint_in) {
  struct csw_t csw;
  int actual_length;
  int r = libusb_bulk_transfer(handle, endpoint_in, (unsigned char *)&csw,
                               sizeof(csw), &actual_length, 5000);
  if (r < 0 || actual_length != sizeof(csw)) {
    fprintf(stderr, "Failed to read CSW: %s\n", libusb_error_name(r));
    return -1;
  }
  if (strncmp((char *)csw.signature, "USBS", 4) != 0) {
    fprintf(stderr, "Invalid CSW signature\n");
    return -1;
  }
  if (csw.tag != tag_counter) {
    fprintf(stderr, "Tag mismatch\n");
    return -1;
  }
  if (csw.status != 0) {
    fprintf(stderr, "Command failed with status %d\n", csw.status);
    return -1;
  }
  tag_counter++;
  return 0;
}
static int scsi_test_unit_ready(libusb_device_handle *handle,
                                const endpoint_t *ep) {
  uint8_t cb[16] = {0};
  cb[0] = SCSI_TEST_UNIT_READY; // 0x00
  // TEST UNIT READY has a 6-byte CDB and transfers 0 data bytes
  if (send_cbw(handle, ep->address_out, 0, 0x00, cb, 6) < 0)
    return -1;

  if (read_csw(handle, ep->address_in) < 0)
    return -1;
  return 0;
}

static int scsi_read_sectors(libusb_device_handle *handle, const endpoint_t *ep,
                             uint32_t lba, uint16_t sectors, uint8_t *buffer) {
  uint8_t cb[16] = {0};
  cb[0] = SCSI_READ10;
  cb[2] = (lba >> 24) & 0xFF;
  cb[3] = (lba >> 16) & 0xFF;
  cb[4] = (lba >> 8) & 0xFF;
  cb[5] = lba & 0xFF;
  cb[7] = (sectors >> 8) & 0xFF;
  cb[8] = sectors & 0xFF;

  uint32_t len = sectors * FAT_SECTOR_SIZE;

  if (send_cbw(handle, ep->address_out, len, 0x80, cb, 10) < 0)
    return -1;

  int actual_length;
  int total_read = 0;
  while (total_read < len) {
    int r = libusb_bulk_transfer(handle, ep->address_in, buffer + total_read,
                                 len - total_read, &actual_length, 5000);
    if (r < 0) {
      fprintf(stderr, "Read data failed: %s\n", libusb_error_name(r));
      return -1;
    }
    total_read += actual_length;
  }

  if (read_csw(handle, ep->address_in) < 0)
    return -1;
  return 0;
}

static int scsi_write_sectors(libusb_device_handle *handle,
                              const endpoint_t *ep, uint32_t lba,
                              uint16_t sectors, uint8_t *buffer) {
  uint8_t cb[16] = {0};
  cb[0] = SCSI_WRITE10;
  cb[2] = (lba >> 24) & 0xFF;
  cb[3] = (lba >> 16) & 0xFF;
  cb[4] = (lba >> 8) & 0xFF;
  cb[5] = lba & 0xFF;
  cb[7] = (sectors >> 8) & 0xFF;
  cb[8] = sectors & 0xFF;

  uint32_t len = sectors * FAT_SECTOR_SIZE;

  if (send_cbw(handle, ep->address_out, len, 0x00, cb, 10) < 0)
    return -1;

  int actual_length;
  int total_written = 0;
  while (total_written < len) {
    int r =
        libusb_bulk_transfer(handle, ep->address_out, buffer + total_written,
                             len - total_written, &actual_length, 5000);
    if (r < 0) {
      fprintf(stderr, "Write data failed: %s\n", libusb_error_name(r));
      return -1;
    }
    total_written += actual_length;
  }

  if (read_csw(handle, ep->address_in) < 0)
    return -1;
  return 0;
}

// Minimal FAT32 Implementation
static uint32_t partition_lba_start = 0;
static uint32_t fat_begin_lba = 0;
static uint32_t cluster_begin_lba = 0;
static uint8_t sectors_per_cluster = 0;
static uint32_t root_dir_first_cluster = 0;

static int reset_recovery(libusb_device_handle *handle, const endpoint_t *ep) {
  printf("Performing Mass Storage Reset Recovery...\n");

  // 1. Bulk-Only Mass Storage Reset
  // Request Type: 0x21 (Host-to-Device, Class, Interface)
  // Request: 0xFF
  // Value: 0
  // Index: Interface Number
  // Length: 0
  int r = libusb_control_transfer(handle,
                                  0x21,      // bmRequestType
                                  0xFF,      // bRequest
                                  0,         // wValue
                                  ep->iface, // wIndex
                                  NULL,      // Data
                                  0,         // wLength
                                  1000       // Timeout
  );

  if (r < 0) {
    printf("  Mass Storage Reset failed: %s\n", libusb_error_name(r));
  } else {
    printf("  Mass Storage Reset sent.\n");
  }

  // 2. Clear Halt on Bulk-In Endpoint
  r = libusb_clear_halt(handle, ep->address_in);
  if (r < 0) {
    printf("  Clear Halt (IN) failed: %s\n", libusb_error_name(r));
  } else {
    printf("  Clear Halt (IN) success.\n");
  }

  // 3. Clear Halt on Bulk-Out Endpoint
  r = libusb_clear_halt(handle, ep->address_out);
  if (r < 0) {
    printf("  Clear Halt (OUT) failed: %s\n", libusb_error_name(r));
  } else {
    printf("  Clear Halt (OUT) success.\n");
  }

  return 0;
}

static int init_fat32(libusb_device_handle *handle, const endpoint_t *ep) {
  uint8_t buffer[512];

  reset_recovery(handle, ep);

  printf("Testing Unit Ready...\n");
  int retries = 5;
  while (retries > 0) {
    if (scsi_test_unit_ready(handle, ep) == 0) {
      printf("  Device is ready.\n");
      break;
    }
    printf("  Not ready yet, retrying...\n");
    retries--;
    // Sleep a bit? libusb doesn't have a sleep, but simple retry usually works
    // to clear Unit Attention.
  }

  // Read MBR
  printf("Reading MBR...\n");
  if (scsi_read_sectors(handle, ep, 0, 1, buffer) < 0) {
    printf("Read MBR failed even after Test Unit Ready. Retrying once...\n");
    if (scsi_read_sectors(handle, ep, 0, 1, buffer) < 0) {
      return -1;
    }
  }

  // Check Partition Entry 1 (0x1BE)
  // 0x1BE + 8 (LBA Start)
  uint32_t lba_start = *(uint32_t *)&buffer[0x1BE + 8];
  uint32_t sectors = *(uint32_t *)&buffer[0x1BE + 12];

  if (lba_start == 0) {
    fprintf(stderr, "No valid partition found in MBR\n");
    return -1;
  }
  partition_lba_start = lba_start;
  printf("Partition 1 starts at LBA %u (%u sectors)\n", lba_start, sectors);

  // Read FAT32 Boot Sector
  if (scsi_read_sectors(handle, ep, partition_lba_start, 1, buffer) < 0)
    return -1;

  uint16_t reserved_sectors = *(uint16_t *)&buffer[14];
  uint8_t num_fats = buffer[16];
  uint32_t sectors_per_fat = *(uint32_t *)&buffer[36];
  root_dir_first_cluster = *(uint32_t *)&buffer[44];
  sectors_per_cluster = buffer[13];

  // Calculate offsets
  fat_begin_lba = partition_lba_start + reserved_sectors;
  cluster_begin_lba = fat_begin_lba + (num_fats * sectors_per_fat);

  printf("FAT32 Init: Reserved=%u, FATs=%d, SPF=%u, RootClust=%u, SPC=%u\n",
         reserved_sectors, num_fats, sectors_per_fat, root_dir_first_cluster,
         sectors_per_cluster);
  printf("DATA Start LBA: %u\n", cluster_begin_lba);

  return 0;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
  return cluster_begin_lba + ((cluster - 2) * sectors_per_cluster);
}

static int read_file(libusb_device_handle *handle, const endpoint_t *ep) {
  // Read Root Directory (First Cluster)
  uint8_t *cluster_buf = malloc(sectors_per_cluster * 512);
  if (!cluster_buf)
    return -1;

  uint32_t current_cluster = root_dir_first_cluster;

  // Simplification: Assume root dir fits in one cluster and file is
  // contiguous/small Real FAT32 driver would parse FAT table for chain

  uint32_t lba = cluster_to_lba(current_cluster);
  printf("Reading Root Dir at LBA %u\n", lba);

  if (scsi_read_sectors(handle, ep, lba, sectors_per_cluster, cluster_buf) <
      0) {
    free(cluster_buf);
    return -1;
  }

  uint32_t file_size = 0;
  uint32_t file_start_cluster = 0;
  bool found = false;

  // Parse Directory Entries (32 bytes each)
  for (int i = 0; i < (sectors_per_cluster * 512); i += 32) {
    uint8_t *entry = &cluster_buf[i];
    if (entry[0] == 0x00)
      break; // End of dir
    if (entry[0] == 0xE5)
      continue; // Deleted

    // Match "FILE    TXT" (8.3 format)
    if (strncmp((char *)entry, "FILE    TXT", 11) == 0) {
      uint16_t cluster_high = *(uint16_t *)&entry[20];
      uint16_t cluster_low = *(uint16_t *)&entry[26];
      file_start_cluster = ((uint32_t)cluster_high << 16) | cluster_low;
      file_size = *(uint32_t *)&entry[28];
      printf("Found file.txt! Size: %u bytes, Cluster: %u\n", file_size,
             file_start_cluster);
      found = true;
      break;
    }
  }

  free(cluster_buf);

  if (!found) {
    printf("file.txt not found.\n");
    return -1;
  }

  // Read File Content
  // Assume file fits in one cluster for this demo
  uint8_t *file_buf = malloc(sectors_per_cluster * 512);
  if (!file_buf)
    return -1;

  lba = cluster_to_lba(file_start_cluster);
  if (scsi_read_sectors(handle, ep, lba, sectors_per_cluster, file_buf) < 0) {
    free(file_buf);
    return -1;
  }

  printf("---------------------------------------------------\n");
  printf("Content of file.txt:\n");
  // Print only logical size
  for (uint32_t i = 0; i < file_size; i++) {
    putchar(file_buf[i]);
  }
  printf("\n---------------------------------------------------\n");

  free(file_buf);
  return 0;
}

static int write_test(libusb_device_handle *handle, const endpoint_t *ep) {
  uint8_t original[512];
  uint8_t modified[512];
  uint8_t verified[512];
  uint32_t test_lba = 0; // Test Sector 0 (MBR) - WE WILL RESTORE IT

  printf("Starting write-verify-restore test on LBA %u...\n", test_lba);

  // 1. Read
  if (scsi_read_sectors(handle, ep, test_lba, 1, original) < 0)
    return -1;

  // 2. Modify (flip a byte in the signature if it's 0x55/0xAA, or just first
  // byte)
  memcpy(modified, original, 512);
  modified[0] ^= 0xFF;

  // 3. Write
  printf("Writing modified sector...\n");
  if (scsi_write_sectors(handle, ep, test_lba, 1, modified) < 0)
    return -1;

  // 4. Verify
  printf("Verifying...\n");
  if (scsi_read_sectors(handle, ep, test_lba, 1, verified) < 0)
    return -1;

  if (memcmp(modified, verified, 512) == 0) {
    printf("SUCCESS: Verification matched!\n");
  } else {
    printf("FAILURE: Verification failed!\n");
  }

  // 5. Restore
  printf("Restoring original sector...\n");
  if (scsi_write_sectors(handle, ep, test_lba, 1, original) < 0)
    return -1;

  return 0;
}

static void read_from_device(libusb_device *dev, const endpoint_t *ep) {
  libusb_device_handle *handle = NULL;
  int r = libusb_open(dev, &handle);
  if (r < 0) {
    fprintf(stderr, "Error opening device: %s\n", libusb_error_name(r));
    return;
  }

  if (libusb_kernel_driver_active(handle, ep->iface) == 1) {
    libusb_detach_kernel_driver(handle, ep->iface);
  }

  r = libusb_claim_interface(handle, ep->iface);
  if (r < 0) {
    fprintf(stderr, "Error claiming interface: %s\n", libusb_error_name(r));
    libusb_close(handle);
    return;
  }

  if (init_fat32(handle, ep) == 0) {
    read_file(handle, ep);
    printf("\n--- Optional Write Test ---\n");
    printf("To perform a write test, run with --write-test argument.\n");
  }

  libusb_close(handle);
}

static void do_write_test(libusb_device *dev, const endpoint_t *ep) {
  libusb_device_handle *handle = NULL;
  int r = libusb_open(dev, &handle);
  if (r < 0)
    return;

  libusb_set_auto_detach_kernel_driver(handle, 1);
  r = libusb_claim_interface(handle, ep->iface);
  if (r < 0) {
    libusb_close(handle);
    return;
  }

  write_test(handle, ep);

  libusb_close(handle);
}

// Entry point for WASI-USB
int main(int argc, char **argv) {
  libusb_context *ctx = NULL;
  libusb_device **devs;
  ssize_t cnt;
  int r;
  bool run_write_test = false;

  if (argc > 1 && strcmp(argv[1], "--write-test") == 0) {
    run_write_test = true;
  }

  printf("Starting libusb workload (C) - %s USB...\n",
         run_write_test ? "Writing to" : "Reading from");

  r = libusb_init(&ctx);
  if (r < 0)
    return 1;

  cnt = libusb_get_device_list(ctx, &devs);
  if (cnt < 0) {
    libusb_exit(ctx);
    return 1;
  }

  libusb_device *target_dev = NULL;
  endpoint_t target_ep;

  for (ssize_t i = 0; i < cnt; i++) {
    libusb_device *dev = devs[i];
    struct libusb_device_descriptor desc;

    if (libusb_get_device_descriptor(dev, &desc) < 0)
      continue;

    // printf("%04x:%04x\n", desc.idVendor, desc.idProduct);

    if (target_dev == NULL && find_mass_storage_endpoint(dev, &target_ep)) {
      printf(" -> Found Mass Storage Device! (Config %d, Iface %d)\n",
             target_ep.config, target_ep.iface);
      target_dev = dev;
    }
  }

  if (target_dev) {
    if (run_write_test) {
      do_write_test(target_dev, &target_ep);
    } else {
      read_from_device(target_dev, &target_ep);
    }
  } else {
    printf("No Mass Storage device found.\n");
  }

  libusb_free_device_list(devs, 1);
  libusb_exit(ctx);
  return 0;
}

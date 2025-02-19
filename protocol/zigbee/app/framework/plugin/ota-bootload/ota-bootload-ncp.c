/***************************************************************************//**
 * @file
 * @brief Routines for bootloading an NCP UART.
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include "app/framework/util/common.h"
#include "app/framework/util/attribute-storage.h"
#include "app/framework/util/af-main.h"

#include "app/framework/plugin/ota-common/ota.h"
#include "app/framework/plugin/ota-storage-common/ota-storage.h"

#ifdef SL_COMPONENT_CATALOG_PRESENT
#include "sl_component_catalog.h"
#endif
#include "bootloader-interface-standalone.h"
#include "ota-storage-common-config.h"

#ifdef SL_CATALOG_ZIGBEE_EZSP_UART_PRESENT
#include "app/ezsp-host/ash/ash-host.h"
#include "app/ezsp-host/ash/ash-host-ui.h"
#endif // SL_CATALOG_ZIGBEE_EZSP_UART_PRESENT
#include "app/ezsp-host/ezsp-host-io.h"

#include "ota-bootload-ncp.h"
#include "ota-bootload-xmodem.h"

//------------------------------------------------------------------------------
// Globals

// Xmodem requires all blocks be 128 bytes in size
#define TRANSFER_BLOCK_SIZE 128

// Until we have a formal facility for this code, we just use core.
#define bootloadPrintln(...) emberAfCorePrintln(__VA_ARGS__)
#define bootloadDebugExec(x) emberAfCoreDebugExec(x)
#if defined(EMBER_AF_PRINT_CORE)
  #define BOOTLOAD_PRINT_ENABLED
#endif

// We arbitrarily chose 5% as the minimum update amount when we are
// transfering the file to the NCP.  This provides a good amount of feedback
// during the process but not too much.
#define BOOTLOAD_PERCENTAGE_UPDATE  5

#if !defined(SL_CATALOG_ZIGBEE_EZSP_SPI_PRESENT)
// UART assumed
  #define START_IMMEDIATELY false
#else
// SPI assumed
  #define START_IMMEDIATELY true
#endif

//------------------------------------------------------------------------------
// Forward Declarations

static bool transferFile(const EmberAfOtaImageId* id, uint16_t ncpUpgradeTagId);

//------------------------------------------------------------------------------

// This hands control of the entire application to this code to perform
// the bootloading.  It will not return until the bootload has completed
// or it has failed.

uint8_t emberAfOtaBootloadCallback(const EmberAfOtaImageId* id,
                                   uint16_t ncpUpgradeTagId)
{
  bool success = true;
  EzspStatus status;
  bootloadPrintln("Launching standalone bootloader...");

  status =
    ezspLaunchStandaloneBootloader(true);
  if (status != EMBER_SUCCESS) {
    bootloadPrintln("Launch failed: 0x%X", status);
    return 1;
  }
  ezspClose();

  bootloadPrintln("Starting bootloader communications.");
  emberAfCoreFlush();
  if (!sli_zigbee_af_start_ncp_bootloader_communications()) {
    success = false;
    bootloadPrintln("Failed to start bootloading communications.");
    emberAfCoreFlush();
  } else {
    // send all images with matching tag Id
    success = transferFile(id, ncpUpgradeTagId);

    // Regardless of success or failure we reboot the NCP in hopes
    // of returning the system back to its previous state.

    // Use &= here to preserve the possible failed status returned
    // by transferFile()
    success &= sli_zigbee_af_reboot_ncp_after_bootload();
  }

  sli_zigbee_af_post_ncp_bootload(success);

  return 0;
}

static bool transferFile(const EmberAfOtaImageId* id,
                         uint16_t ncpUpgradeTagId)
{
  uint8_t i = 0;
  uint8_t tagCount = 0;
  uint32_t offset[EMBER_AF_PLUGIN_OTA_STORAGE_COMMON_MAX_TAGS_IN_OTA_FILE];
  uint32_t endOffset[EMBER_AF_PLUGIN_OTA_STORAGE_COMMON_MAX_TAGS_IN_OTA_FILE];
  uint32_t * offsetPtr = offset;
  uint32_t * endOffsetPtr = endOffset;
  MEMSET(offset, 0, sizeof(offset));
  MEMSET(endOffset, 0, sizeof(endOffset));

  // We extract the EBL (upgrade image) from the OTA file format
  // and pass it to the bootloader.  The bootloader has no knowledge
  // of OTA files, only EBL images.

  bootloadPrintln("Transferring EBL file to NCP...");

  if (EMBER_AF_OTA_STORAGE_SUCCESS
      != sli_zigbee_af_ota_storage_get_tag_offsets_and_sizes(id,
                                                             ncpUpgradeTagId,
                                                             &offsetPtr,
                                                             &endOffsetPtr)) {
    bootloadPrintln("Failed to get offset and size for tag 0x%2X inside OTA file.",
                    ncpUpgradeTagId);
    return false;
  }

  for (i = 0; i < EMBER_AF_PLUGIN_OTA_STORAGE_COMMON_MAX_TAGS_IN_OTA_FILE; i++) {
    if (offset[i] != 0 && endOffset[i] != 0) {
      tagCount += 1;
    }
  }

  for (i = 0; i < EMBER_AF_PLUGIN_OTA_STORAGE_COMMON_MAX_TAGS_IN_OTA_FILE; i++) {
    if (offset[i] != 0 && endOffset[i] != 0) {
      endOffset[i] += offset[i];

      sli_zigbee_af_print_percentage_set_start_and_end(offset[i], endOffset[i]);
      bootloadPrintln("EBL data start: 0x%4X, end: 0x%4X, size: %d bytes",
                      offset[i],
                      endOffset[i],
                      endOffset[i] - offset[i]);

      sli_zigbee_af_init_xmodsli_zigbee_state(START_IMMEDIATELY);

      while (offset[i] < endOffset[i]) {
        uint32_t returnedLength;
        uint32_t readSize = TRANSFER_BLOCK_SIZE;
        uint8_t block[TRANSFER_BLOCK_SIZE];

        MEMSET(block, 0, TRANSFER_BLOCK_SIZE);

        if ((endOffset[i] - offset[i]) < TRANSFER_BLOCK_SIZE) {
          readSize = endOffset[i] - offset[i];
        }

        halResetWatchdog();

        if (EMBER_AF_OTA_STORAGE_SUCCESS
            != emberAfOtaStorageReadImageDataCallback(id,
                                                      offset[i],
                                                      readSize,
                                                      block,
                                                      &returnedLength)
            || returnedLength != readSize) {
          bootloadPrintln("Failed to read image data at offset 0x%4X", offset[i]);
          return false;
        }

        offset[i] += returnedLength;
        if (!sli_zigbee_af_send_xmodem_data(block,
                                            (uint16_t)returnedLength,
                                            (offset[i] == endOffset[i]))) { // finish?
          bootloadPrintln("Failed to send data to NCP.");
          emberAfCoreFlush();
          return false;
        }

        sli_zigbee_af_print_percentage_update("Transfer",
                                              BOOTLOAD_PERCENTAGE_UPDATE,
                                              offset[i]);
      }

      emberAfCoreFlush();

      // prepping NCP into flashing mode again for the next tag/image.
      if (tagCount > 0 && i + 1 < tagCount) {
        sli_zigbee_af_reboot_ncp_after_bootload();
        sli_zigbee_af_start_ncp_bootloader_communications();
      }
    }
  }

  bootloadPrintln("Transfer completed successfully.");
  return true;
}

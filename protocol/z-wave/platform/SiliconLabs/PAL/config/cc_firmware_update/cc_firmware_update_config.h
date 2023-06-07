/**
 * @file
 * @copyright 2023 Silicon Laboratories Inc.
 */

#ifndef _CC_FIRMWARE_UPDATE_CONFIG_H_
#define _CC_FIRMWARE_UPDATE_CONFIG_H_

// <<< Use Configuration Wizard in Context Menu >>>

// <h> Command Class Firmware Update configuration

// <o> OTA Cache size <1..65535:1> <f.d>
// <i> Specifies the cache size when ota multi frames is enabled
// <i> Default: 200
#define CC_FIRMWARE_UPDATE_CONFIG_OTA_CACHE_SIZE  200

// <q> Activation supported
// <i> Lets the application decide whether delayed activation of the firmware is supported.
// <i> Default: 1
#define CC_FIRMWARE_UPDATE_CONFIG_SUPPORT_ACTIVATION  1

// <q> OTA Multi frames enabled
// <i> Specifies whether CC FW Update should request multiple MD Reports in FW Update MD Get.
// <i> Default: 1
#define CC_FIRMWARE_UPDATE_CONFIG_OTA_MULTI_FRAME  1

// </h>

// <<< end of configuration section >>>

#endif /* _CC_FIRMWARE_UPDATE_CONFIG_H_ */

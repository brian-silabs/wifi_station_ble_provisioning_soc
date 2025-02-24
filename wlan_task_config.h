/*******************************************************************************
 * @file  wifi_config.h
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
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
/**
 * @file         wlan_task_config.h
 * @version      0.1
 *
 *  @brief : This file contains user configurable details to configure the device
 *
 *  @section Description  This file contains user configurable details to configure the device
 *
 *
 */
#ifndef WLAN_TASK_CONFIG_H
#define WLAN_TASK_CONFIG_H

#define DHCP_HOST_NAME    NULL
#define TIMEOUT_MS        15000
#define WIFI_SCAN_TIMEOUT 15000

// TWT Related Configuration

#define TWT_AUTO_CONFIG  1
#define TWT_SCAN_TIMEOUT 10000

// AUTO TWT based TWT selection params
#define TWT_RX_LATENCY                          60000 // in milli seconds
#define DEVICE_AVERAGE_THROUGHPUT               20000 // Kbps
#define ESTIMATE_EXTRA_WAKE_DURATION_PERCENT    0     // in percentage
#define TWT_TOLERABLE_DEVIATION                 10    // in percentage
#define TWT_DEFAULT_WAKE_INTERVAL_MS            1024  // in milli seconds
#define TWT_DEFAULT_WAKE_DURATION_MS            8     // in milli seconds
#define MAX_BEACON_WAKE_UP_AFTER_SP             2 // The number of beacons after the service period completion for which the module wakes up and listens for any pending RX.

// MANUAL TWT based TWT selection params, only used wwhen TWT_AUTO_CONFIG is set to 0
#define TWT_WAKE_DURATION                       0x60
#define TWT_WAKE_DURATION_UNIT                  0
#define TWT_WAKE_DURATION_TOL                   0x60
#define TWT_WAKE_INT_EXP                        13
#define TWT_WAKE_INT_EXP_TOL                    13
#define TWT_WAKE_INT_MANTISSA                   0x1D4C
#define TWT_WAKE_INT_MANTISSA_TOL               0x1D4C
#define TWT_WAKE_RETRY_LIMIT                    6
#define TWT_WAKE_RETRY_INTERVAL                 10

#endif // WLAN_TASK_CONFIG_H

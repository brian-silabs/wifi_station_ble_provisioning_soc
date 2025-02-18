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

#include "sl_si91x_constants.h"
#include "sl_wifi_device.h"
#include "rsi_ble_common_config.h"

static const sl_wifi_device_configuration_t
  config = { .boot_option = LOAD_NWP_FW,
             .mac_address = NULL,
             .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
             .region_code = US,
             .boot_config = {
               .oper_mode       = SL_SI91X_CLIENT_MODE,
               .coex_mode       = SL_SI91X_WLAN_BLE_MODE,
               .feature_bit_map = (SL_SI91X_FEAT_ULP_GPIO_BASED_HANDSHAKE | SL_SI91X_FEAT_DEV_TO_HOST_ULP_GPIO_1
#ifdef SLI_SI91X_MCU_INTERFACE
                                   | SL_SI91X_FEAT_WPS_DISABLE
#endif
                                   ),
               .tcp_ip_feature_bit_map = (SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT | SL_SI91X_TCP_IP_FEAT_EXTENSION_VALID
                                          | SL_SI91X_TCP_IP_FEAT_SSL),
               .custom_feature_bit_map = (SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID | SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID),
               .ext_custom_feature_bit_map =
                 (SL_SI91X_EXT_FEAT_LOW_POWER_MODE | SL_SI91X_EXT_FEAT_XTAL_CLK | MEMORY_CONFIG
#ifdef SLI_SI917
                  | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
#endif // SLI_SI917
                  | SL_SI91X_EXT_FEAT_BT_CUSTOM_FEAT_ENABLE),
               .bt_feature_bit_map         = (SL_SI91X_BT_RF_TYPE | SL_SI91X_ENABLE_BLE_PROTOCOL),
               .ext_tcp_ip_feature_bit_map = (SL_SI91X_CONFIG_FEAT_EXTENTION_VALID | SL_SI91X_EXT_EMB_MQTT_ENABLE),
               //!ENABLE_BLE_PROTOCOL in bt_feature_bit_map
               .ble_feature_bit_map =
                 ((SL_SI91X_BLE_MAX_NBR_PERIPHERALS(RSI_BLE_MAX_NBR_PERIPHERALS)
                   | SL_SI91X_BLE_MAX_NBR_CENTRALS(RSI_BLE_MAX_NBR_CENTRALS)
                   | SL_SI91X_BLE_MAX_NBR_ATT_SERV(RSI_BLE_MAX_NBR_ATT_SERV)
                   | SL_SI91X_BLE_MAX_NBR_ATT_REC(RSI_BLE_MAX_NBR_ATT_REC))
                  | SL_SI91X_FEAT_BLE_CUSTOM_FEAT_EXTENTION_VALID | SL_SI91X_BLE_PWR_INX(RSI_BLE_PWR_INX)
                  | SL_SI91X_BLE_PWR_SAVE_OPTIONS(RSI_BLE_PWR_SAVE_OPTIONS) | SL_SI91X_916_BLE_COMPATIBLE_FEAT_ENABLE
#if RSI_BLE_GATT_ASYNC_ENABLE
                  | SL_SI91X_BLE_GATT_ASYNC_ENABLE
#endif
                  ),

               .ble_ext_feature_bit_map =
                 ((SL_SI91X_BLE_NUM_CONN_EVENTS(RSI_BLE_NUM_CONN_EVENTS)
                   | SL_SI91X_BLE_NUM_REC_BYTES(RSI_BLE_NUM_REC_BYTES))
#if RSI_BLE_INDICATE_CONFIRMATION_FROM_HOST
                  | SL_SI91X_BLE_INDICATE_CONFIRMATION_FROM_HOST //indication response from app
#endif
#if RSI_BLE_MTU_EXCHANGE_FROM_HOST
                  | SL_SI91X_BLE_MTU_EXCHANGE_FROM_HOST //MTU Exchange request initiation from app
#endif
#if RSI_BLE_SET_SCAN_RESP_DATA_FROM_HOST
                  | (SL_SI91X_BLE_SET_SCAN_RESP_DATA_FROM_HOST) //Set SCAN Resp Data from app
#endif
#if RSI_BLE_DISABLE_CODED_PHY_FROM_HOST
                  | (SL_SI91X_BLE_DISABLE_CODED_PHY_FROM_HOST) //Disable Coded PHY from app
#endif
#if BLE_SIMPLE_GATT
                  | SL_SI91X_BLE_GATT_INIT
#endif
                  ),
               .config_feature_bit_map = (SL_SI91X_FEAT_SLEEP_GPIO_SEL_BITMAP| SL_SI91X_ENABLE_ENHANCED_MAX_PSP) } };


  #define DHCP_HOST_NAME    NULL
  #define TIMEOUT_MS        15000
  #define WIFI_SCAN_TIMEOUT 15000

  #define TWT_AUTO_CONFIG  1
  #define TWT_SCAN_TIMEOUT 10000

  // Use case based TWT selection params
  #define TWT_RX_LATENCY                       60000 // in milli seconds
  #define DEVICE_AVERAGE_THROUGHPUT            20000 // Kbps
  #define ESTIMATE_EXTRA_WAKE_DURATION_PERCENT 0     // in percentage
  #define TWT_TOLERABLE_DEVIATION              10    // in percentage
  #define TWT_DEFAULT_WAKE_INTERVAL_MS         1024  // in milli seconds
  #define TWT_DEFAULT_WAKE_DURATION_MS         8     // in milli seconds
  #define MAX_BEACON_WAKE_UP_AFTER_SP          2 // The number of beacons after the service period completion for which the module wakes up and listens for any pending RX.

#endif // WLAN_TASK_CONFIG_H


#ifndef NWP_TASK_CONFIG_H
#define NWP_TASK_CONFIG_H

#include "sl_wifi_device.h"
#include "ble_config.h"

#define SL_SI91X_OPERATION_MODE                       SL_SI91X_CLIENT_MODE
#define SL_SI91X_COEX_MODE                            SL_SI91X_WLAN_BLE_MODE

#define SL_SI91X_WIFI_PERFORMANCE_PROFILE             DEEP_SLEEP_WITH_RAM_RETENTION
#define SL_SI91X_WIFI_PERFORMANCE_PROFILE_CONNECTED   ASSOCIATED_POWER_SAVE
#define SL_SI91X_BT_PERFORMANCE_PROFILE               ASSOCIATED_POWER_SAVE

#define EFR32_GATT_DB_COMPAT_ENABLED            1

#if EFR32_GATT_DB_COMPAT_ENABLED
#define BLE_SIMPLE_GATT                         1
#endif

// WiFi Auto Low Power Settings
#define WIFI_AUTO_LOW_POWER_MODE_ENABLE 1

// For WiFi 6 Compatible Access Points only
#define WIFI_AUTO_LOW_POWER_TRY_TWT     0

// // For other WiFi Access Points
// #define WIFI_AUTO_LOW_POWER_MODE_LOWER_POWER        0x00
// #define WIFI_AUTO_LOW_POWER_MODE_HIGHER_COMPAT      0x01
// #define WIFI_AUTO_LOW_POWER_MODE_BEST_COMPROMISE    0x02

// #define WIFI_AUTO_LOW_POWER_MODE                    WIFI_AUTO_LOW_POWER_MODE_LOWER_POWER

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

///////// Should not be modified
/// // Check how to better deal with this config

static const sl_wifi_device_configuration_t
  station_init_configuration = { .boot_option = LOAD_NWP_FW,
             .mac_address = NULL,
             .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
             .region_code = US,
             .boot_config = {
               .oper_mode       = SL_SI91X_OPERATION_MODE,
               .coex_mode       = SL_SI91X_COEX_MODE,
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

#endif // NWP_TASK_CONFIG_H

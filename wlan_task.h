#ifndef WLAN_TASK_H
#define WLAN_TASK_H

#include "sl_status.h"

// Enumeration for states in application
typedef enum wifi_app_state_e
{
  WIFI_APP_INITIAL_STATE = 0,
  WIFI_APP_UNCONNECTED_STATE = 1,
  WIFI_APP_CONNECTED_STATE = 2,
  WIFI_APP_IPCONFIG_DONE_STATE = 3,
  WIFI_APP_SCAN_STATE = 4,
  WIFI_APP_JOIN_STATE = 5,
  WIFI_APP_SOCKET_RECEIVE_STATE = 6,
  WIFI_APP_MQTT_INIT_DONE_STATE = 7,
  WIFI_APP_MQTT_SUBSCRIBE_DONE_STATE = 8,
  BLE_APP_GATT_WRITE_EVENT = 9,
  WIFI_APP_DISCONNECTED_STATE = 10,
  WIFI_APP_DISCONN_NOTIFY_STATE = 11,
  WIFI_APP_ERROR_STATE = 12,
  WIFI_APP_FLASH_STATE = 13,
  WIFI_APP_DATA_RECEIVE_STATE = 14,
  WIFI_APP_SD_WRITE_STATE = 15,
  WIFI_APP_DEMO_COMPLETE_STATE = 16
} wifi_app_state_t;

typedef enum wifi_app_cmd_e
{
  WIFI_APP_DATA = 0,
  WIFI_APP_SCAN_RESP = 1,
  WIFI_APP_CONNECTION_STATUS = 2,
  WIFI_APP_DISCONNECTION_STATUS = 3,
  WIFI_APP_DISCONNECTION_NOTIFY = 4,
  WIFI_APP_TIMEOUT_NOTIFY = 5
} wifi_app_cmd_t;

/**
 * @brief Start the WLAN task context.
 *
 * This function initializes and starts the WLAN task context, which is responsible for managing the WiFi connection and related functionality.
 *
 * @return SL_STATUS_OK if the WLAN task context was started successfully, or an appropriate error code otherwise.
 */
sl_status_t start_wlan_task_context(void);

#endif // WLAN_TASK_H

/*******************************************************************************
* @file  app.c
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
/*************************************************************************
 *
 */

/*================================================================================
 * @brief : This file contains example application for Wlan Station BLE
 * Provisioning
 * @section Description :
 * This application explains how to get the WLAN connection functionality using
 * BLE provisioning.
 * Silicon Labs Module starts advertising and with BLE Provisioning the Access Point
 * details are fetched.
 * Silicon Labs device is configured as a WiFi station and connects to an Access Point.
 =================================================================================*/

/**
 * Include files
 **/
//! SL Wi-Fi SDK includes
#include "sl_board_configuration.h"
#include "cmsis_os2.h"
#include "sl_constants.h"
#include "app.h"
#include "thread_safe_print.h"

#include "nwp_task_config.h"

#include "nwp_task.h"
#include "wlan_task.h"
#include "ble_task.h"
#include "mqtt_task.h"
#include "heap_monitor_task.h"

#include "sl_si91x_power_manager.h"

#include "ble_gatt.h"
#include "gatt_db.h"
#include "rsi_ble_apis.h"

#include "sl_wifi.h"
#include "sl_utility.h"
#include "sl_net_constants.h"
#include "sl_net.h"

#include "sl_net_default_values.h"

// APP version
#define APP_FW_VERSION "0.1"
#define APP_NWP_OPERATION_TIMEOUT_MS  15000

#define THERMOSTAT_FLAGS_MSK 0x00000001U  // Define the flag mask

#define APP_JOIN_WITH_DEFAULT_CREDENTIALS 1

const osThreadAttr_t startup_thread_attributes = {
  .name       = "startup_thread",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 2048,
  .priority   = osPriorityRealtime,
  .tz_module  = 0,
  .reserved   = 0,
};

const osThreadAttr_t thermostat_thread_attributes = {
  .name       = "thermostat_thread",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 2048,
  .priority   = osPriorityNormal,
  .tz_module  = 0,
  .reserved   = 0,
};

osEventFlagsId_t    thermostat_evt_flags_id;  // Event flags ID

static sl_ip_address_t  ip_address = { 0 };
static uint8_t          coex_ssid[50], pwd[34], sec_type;
static uint8_t          connected_to_ap = 0;

void startup_routine(void *argument);
void thermostat_routine(void *argument);

static void app_start_wlan_scan(void);
static void app_wlan_connect_to_ap(void);
static void process_ble_attr1_command(uint8_t *att_value);
static void app_wlan_timeout_ble_notification(void);

void app_init(void)
{
  sl_status_t status = thread_safe_print_init();
  if(SL_STATUS_OK != status)
  {
    while(1); // Count on WDOG for the sample app
  }

  osThreadId_t startup_thread_id = osThreadNew((osThreadFunc_t)startup_routine, NULL, &startup_thread_attributes);
  if (startup_thread_id == NULL) {
    THREAD_SAFE_PRINT("Failed to create startup_routine\n");
  }

  osThreadId_t thermostat_thread_id = osThreadNew((osThreadFunc_t)thermostat_routine, NULL, &thermostat_thread_attributes);
  if (thermostat_thread_id == NULL) {
    THREAD_SAFE_PRINT("Failed to create thermostat_routine\n");
  }

  thermostat_evt_flags_id = osEventFlagsNew(NULL);
  if (thermostat_evt_flags_id == NULL) {
    THREAD_SAFE_PRINT("Failed to create thermostat_evt_flags_id\n");
    while(1); // Count on WDOG for the sample app
  }
}

void startup_routine(void *argument)
{
  UNUSED_PARAMETER(argument);

  THREAD_SAFE_PRINT("Setting up application tasks\n");
  start_nwp_task_context();

  //If WLAN, init powersave mode. Should always pass
  if((SL_SI91X_COEX_MODE == SL_SI91X_WLAN_BLE_MODE)
      || (SL_SI91X_COEX_MODE == SL_SI91X_WLAN_ONLY_MODE))
  {
      start_wlan_task_context();
  }

  //If BLE, Init power save mode too
  if((SL_SI91X_COEX_MODE == SL_SI91X_WLAN_BLE_MODE)
      || (SL_SI91X_COEX_MODE == SL_SI91X_BLE_MODE))
  {
      start_ble_task_context();
  }

  start_heap_monitor_task_context();

  start_mqtt_task_context();

  // THREAD_SAFE_PRINT("DEBUG : Suspending Low Power Support \n");
  // //Add PS4 Power State Requirement, to prevent M4 going to Sleep
  // sl_si91x_power_manager_add_ps_requirement(SL_SI91X_POWER_MANAGER_PS4);

  THREAD_SAFE_PRINT("Application tasks setup Done, killing startup routine\n");
  osThreadExit();
}

void thermostat_routine(void *argument)
{
  UNUSED_PARAMETER(argument);

  sl_status_t status = SL_STATUS_OK;

  uint32_t flag = osEventFlagsWait(thermostat_evt_flags_id, THERMOSTAT_FLAGS_MSK, osFlagsWaitAny, osWaitForever);
  osEventFlagsClear(thermostat_evt_flags_id, flag);
  THREAD_SAFE_PRINT("Thermostat start event received\n");

  while(1)
  {
    osDelay(5000);// TODO Make it a configurable parameter
    status = mqtt_publish_to_broker("THERMOSTAT-DATA\0", "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do\0");
    if (status != SL_STATUS_OK) {
      THREAD_SAFE_PRINT("Failed to publish to broker : 0x%lX\n", status);
    }
  }

}

sl_status_t bt_on_event(ble_event_msg_t* event)
{

  switch (event->event_id) {
    case BLE_SYSTEM_BOOT_EVENT : {
#if !APP_JOIN_WITH_DEFAULT_CREDENTIALS
      // set device in advertising mode.
      rsi_ble_start_advertising();
      THREAD_SAFE_PRINT("\r\nBLE Advertising Started...\r\n");
#endif
    } break;

    case BLE_CONNECTION_OPENED_EVENT: {
    } break;

    case BLE_CONNECTION_CLOSED_EVENT: {
    } break;

    case BLE_GATT_DATALEN_CHANGE_EVENT: {
    } break;

    case BLE_GATT_WRITE_REQUEST_EVENT: {
        rsi_ble_event_write_t *ble_write_event = (rsi_ble_event_write_t *)event->payload;
        uint16_t attr_handle = (ble_write_event->handle[1] << 8) | ble_write_event->handle[0];

        switch (attr_handle) {
          case gattdb_attribute_1:
            THREAD_SAFE_PRINT("gattdb_attribute_1 handle\n");
            process_ble_attr1_command(ble_write_event->att_value);
            break;
          case gattdb_attribute_2:
            THREAD_SAFE_PRINT("gattdb_attribute_2 handle\n");
            break;
          case gattdb_attribute_3:
            THREAD_SAFE_PRINT("gattdb_attribute_3 handle\n");
            break;
          default:
            THREAD_SAFE_PRINT("Unknown handle 0x%X\n", attr_handle);
            break;
        }

    } break;
    default:
      break;
  }//switch(ble_event_id)

  return SL_STATUS_OK;
}

sl_status_t wlan_on_event(wlan_event_msg_t* event)
{
  sl_status_t status                 = SL_STATUS_OK;
  uint8_t data[RSI_BLE_MAX_DATA_LEN] = { 0 }; //Generic Buffer for data sent over BLE

  switch (event->event_id) {
    case WLAN_BOOT_EVENT :
#if APP_JOIN_WITH_DEFAULT_CREDENTIALS
      memcpy(coex_ssid, DEFAULT_WIFI_CLIENT_PROFILE_SSID, sizeof(DEFAULT_WIFI_CLIENT_PROFILE_SSID));
      memcpy(pwd, DEFAULT_WIFI_CLIENT_CREDENTIAL, sizeof(DEFAULT_WIFI_CLIENT_CREDENTIAL));
      sec_type = DEFAULT_WIFI_CLIENT_SECURITY_TYPE;
      THREAD_SAFE_PRINT("[APP] Join Default AP Request\n");
      app_wlan_connect_to_ap();
#endif
    break;

    case WLAN_SCAN_COMPLETE_EVENT: {
      //DONE Removing the osDelay works fine as long as notifications are enabled
      sl_wifi_scan_result_t *scanresult = (sl_wifi_scan_result_t *)(event->payload);
      uint8_t scan_ix, length;

      memset(data, 0, RSI_BLE_MAX_DATA_LEN);
      data[0] = 0x03;
      data[1] = scanresult->scan_count;
      rsi_ble_set_local_att_value(gattdb_attribute_2, RSI_BLE_MAX_DATA_LEN, data);

      for (scan_ix = 0; scan_ix < scanresult->scan_count; scan_ix++) {
        memset(data, 0, RSI_BLE_MAX_DATA_LEN);
        data[0] = scanresult->scan_info[scan_ix].security_mode;
        data[1] = ',';
        strcpy((char *)data + 2, (const char *)scanresult->scan_info[scan_ix].ssid);
        length = strlen((char *)data + 2);
        length = length + 2;

        rsi_ble_set_local_att_value(gattdb_attribute_3, RSI_BLE_MAX_DATA_LEN, data);
      }
    } break;

    case WLAN_IPCONFIG_DONE_EVENT: {
      sl_ip_address_t *ip = (sl_ip_address_t *)(event->payload);
      memcpy(&ip_address, ip, sizeof(sl_ip_address_t));
    } break;

    case WLAN_JOIN_COMPLETE_EVENT : {
      sl_mac_address_t mac_addr = { 0 };
      uint8_t k;

      connected_to_ap = 1;

      memset(data, 0, RSI_BLE_MAX_DATA_LEN);
      data[0] = 0x02;
      data[1] = 0x01;
      data[2] = ',';

      // Copy the MAC address
      status = sl_wifi_get_mac_address(SL_WIFI_CLIENT_INTERFACE, &mac_addr);
      if (status == SL_STATUS_OK) {
        for (k = 0; k < 6; k++) {
          data[k + 3] = mac_addr.octet[k];
        }
      } else {
        k = 6;
      }
      data[k + 3] = ',';

      // IP Address
      for (int i = 0; k < 10; k++, i++) {
        data[k + 4] = ip_address.ip.v4.bytes[i];
      }

      rsi_ble_set_local_att_value(gattdb_attribute_2,
                                  RSI_BLE_MAX_DATA_LEN,
                                  data); // set the local attribute value.


      status = mqtt_connect_to_broker();
      if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("Failed to connect to MQTT broker : 0x%lX\n", status);
      }

      THREAD_SAFE_PRINT("AP joined successfully\n\n");
    } break;

    case WLAN_DISCONNECTED_EVENT: {
        THREAD_SAFE_PRINT("WIFI App Disconnected State\n");
        memset(data, 0, RSI_BLE_MAX_DATA_LEN);
        data[1] = 0x01;
        data[0] = 0x04;
        rsi_ble_set_local_att_value(gattdb_attribute_2, RSI_BLE_MAX_DATA_LEN, data);
    } break;

    default:
      break;
  }//switch(wlan_event_id)

  return status;
}

// Legacy compatibility with old RS Code si SI Connect works
static void process_ble_attr1_command(uint8_t *att_value)
{
  uint8_t data[RSI_BLE_MAX_DATA_LEN] = { 0 };
  uint8_t cmdid = att_value[0];

  switch (cmdid) {
        // Scan command request
        case '3': //else if(rsi_ble_write->att_value[0] == '3')
        {
          THREAD_SAFE_PRINT("Received scan request\n");
          app_start_wlan_scan();
        } break;

        // Sending SSID
        case '2': //else if(rsi_ble_write->att_value[0] == '2')
        {
          THREAD_SAFE_PRINT("[APP] Received SSID\n");
          memset(coex_ssid, 0, sizeof(coex_ssid));
          strcpy((char *)coex_ssid, (const char *)&att_value[3]);
          THREAD_SAFE_PRINT("[APP] %s\n", coex_ssid);
        } break;

        // Sending Security type
        case '5': //else if(rsi_ble_write->att_value[0] == '5')
        {
          sec_type = ((att_value[3]) - '0');
          THREAD_SAFE_PRINT("[APP] In Security Request\n");
          if (sec_type == 0) {
            THREAD_SAFE_PRINT("[APP] Join Request\n");
            app_wlan_connect_to_ap();
          }
        } break;

        // Sending PSK
        case '6': //else if(rsi_ble_write->att_value[0] == '6')
        {
          THREAD_SAFE_PRINT("[APP] Received PWD\n");
          strcpy((char *)pwd, (const char *)&att_value[3]);
          THREAD_SAFE_PRINT("[APP] %s\n", pwd);
          THREAD_SAFE_PRINT("[APP] Join Request\n");
          app_wlan_connect_to_ap();
        } break;

        // WLAN Status Request
        case '7': //else if(rsi_ble_write->att_value[0] == '7')
        {
          THREAD_SAFE_PRINT("[APP] WLAN status request received\n");
          if (connected_to_ap) {
            memset(data, 0, RSI_BLE_MAX_DATA_LEN);

            data[1] = connected_to_ap; /*This index will indicate wlan AP connect or disconnect status to Android app*/
            data[0] = 0x07;
            rsi_ble_set_local_att_value(gattdb_attribute_2, RSI_BLE_MAX_DATA_LEN, data);
          } else {
            memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            data[0] = 0x07;
            data[1] = 0x00;
            rsi_ble_set_local_att_value(gattdb_attribute_2, RSI_BLE_MAX_DATA_LEN, data);
          }
        } break;

        // WLAN disconnect request
        case '4': //else if(rsi_ble_write->att_value[0] == '4')
        {
          THREAD_SAFE_PRINT("[APP] WLAN disconnect request received\n");
          start_wlan_access_point_disconnect();
        } break;

        // FW version request
        case '8': {
          THREAD_SAFE_PRINT("[APP] FW version request\n");
          sl_status_t status = SL_STATUS_OK;
          sl_wifi_firmware_version_t firmware_version = { 0 };
          memset(data, 0, RSI_BLE_MAX_DATA_LEN);

          status = sl_wifi_get_firmware_version(&firmware_version);
          if (status == SL_STATUS_OK) {
            data[0] = 0x08;
            data[1] = sizeof(sl_wifi_firmware_version_t);
            memcpy(&data[2], &firmware_version, sizeof(sl_wifi_firmware_version_t));

            rsi_ble_set_local_att_value(gattdb_attribute_2, RSI_BLE_MAX_DATA_LEN, data);
            print_firmware_version(&firmware_version);
          }
        } break;

        default:
          THREAD_SAFE_PRINT("Default command case \n\n");
          break;
      }
}

static void app_start_wlan_scan(void)
{
  sl_status_t status = SL_STATUS_OK;
  sl_wifi_scan_configuration_t wifi_scan_configuration = { 0 };

  //Use default scan configuration
  wifi_scan_configuration = default_wifi_scan_configuration;

  THREAD_SAFE_PRINT("WLAN Start Scan\n");
  // If not, start a scan
  status = sl_wifi_start_scan(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, NULL, &wifi_scan_configuration);
  if (  (status != SL_STATUS_OK)
      &&(status != SL_STATUS_IN_PROGRESS))
  {
      THREAD_SAFE_PRINT("Failed to start scan: 0x%lX\r\n", status);
  }
}

static void app_wlan_connect_to_ap(void)
{
  sl_status_t status = SL_STATUS_OK;

  status = start_wlan_access_point_join(  (char *)coex_ssid,
                                          strlen((char *)coex_ssid),
                                          SL_WIFI_PSK_CREDENTIAL,
                                          (char *)pwd,
                                          strlen((char *)pwd),
                                          sec_type,
                                          APP_NWP_OPERATION_TIMEOUT_MS);

  if (status != SL_STATUS_OK) {
    THREAD_SAFE_PRINT("WLAN Connect Failed, Error Code : 0x%lX\r\n", status);
    app_wlan_timeout_ble_notification();
  }
}

static void app_wlan_timeout_ble_notification(void)
{
  uint8_t data[RSI_BLE_MAX_DATA_LEN] = { 0 };
  
  memset(data, 0, RSI_BLE_MAX_DATA_LEN);
  data[0] = 0x02;
  data[1] = 0x00;
  rsi_ble_set_local_att_value(gattdb_attribute_2, RSI_BLE_MAX_DATA_LEN, data);
}

sl_status_t mqtt_on_event(mqtt_event_msg_t* event)
{
  switch (event->event_id) {
    case MQTT_CONNECTION_EVENT:{
      THREAD_SAFE_PRINT("APP mqtt connected, start thermostat operations\n");
      osEventFlagsSet(thermostat_evt_flags_id, 0x00000001);
    } break;

    default:
      break;
  }//switch(mqtt_event_id)

  return SL_STATUS_OK;
}

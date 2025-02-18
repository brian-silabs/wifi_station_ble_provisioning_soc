/*******************************************************************************
* @file  wifi_app.c
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
 * */

//! SL Wi-Fi SDK includes
#include "sl_constants.h"
#include "sl_wifi.h"
#include "sl_wifi_callback_framework.h"
#include "sl_net.h"
#include "sl_net_si91x.h"
#include "sl_utility.h"

#include "cmsis_os2.h"
#include <string.h>

#include "wifi_config.h"
#include "rsi_common_apis.h"

#include "app.h"
#include "sl_constants.h"

// WLAN include file for configuration

extern osSemaphoreId_t wlan_thread_sem;

#define DHCP_HOST_NAME    NULL
#define TIMEOUT_MS        15000
#define WIFI_SCAN_TIMEOUT 15000

#define TWT_AUTO_CONFIG  1
#define TWT_SCAN_TIMEOUT 10000

#define MQTT_SERVER_ADDRESS_BYTE_4 1

// Use case based TWT selection params
#define TWT_RX_LATENCY                       60000 // in milli seconds
#define DEVICE_AVERAGE_THROUGHPUT            20000 // Kbps
#define ESTIMATE_EXTRA_WAKE_DURATION_PERCENT 0     // in percentage
#define TWT_TOLERABLE_DEVIATION              10    // in percentage
#define TWT_DEFAULT_WAKE_INTERVAL_MS         1024  // in milli seconds
#define TWT_DEFAULT_WAKE_DURATION_MS         8     // in milli seconds
#define MAX_BEACON_WAKE_UP_AFTER_SP \
  2 // The number of beacons after the service period completion for which the module wakes up and listens for any pending RX.


/*
 *********************************************************************************************************
 *                                         LOCAL GLOBAL VARIABLES
 *********************************************************************************************************
 */

sl_wifi_scan_result_t *scan_result          = NULL;
static volatile bool scan_complete          = false;
static volatile sl_status_t callback_status = SL_STATUS_OK;
uint16_t scanbuf_size = (sizeof(sl_wifi_scan_result_t) + (SL_WIFI_MAX_SCANNED_AP * sizeof(scan_result->scan_info[0])));

uint8_t connected = 0, timeout = 0;
uint8_t disconnected = 0, disassosiated = 0;
uint8_t a = 0;

sl_wifi_client_configuration_t access_point = { 0 };
sl_net_ip_configuration_t ip_address        = { 0 };

static uint32_t wlan_app_event_map;
sl_ip_address_t mqtt_broker_ip;

/*
 *********************************************************************************************************
 *                                               DATA TYPES
 *********************************************************************************************************
 */

static sl_status_t set_twt(void);
extern void wifi_app_send_to_ble(uint16_t msg_type, uint8_t *data, uint16_t data_len);
extern void wifi_app_send_to_mqtt(uint16_t msg_type, uint8_t *data, uint16_t data_len);
static sl_status_t show_scan_results();
void wifi_app_set_event(uint32_t event_num);
extern uint8_t coex_ssid[50], pwd[34], sec_type;
uint8_t retry = 1;

uint8_t conn_status;
extern uint8_t magic_word;

sl_wifi_twt_request_t default_twt_setup_configuration = {
  .twt_enable              = 1,
  .twt_flow_id             = 1,
  .wake_duration           = 0x60,
  .wake_duration_unit      = 0,
  .wake_duration_tol       = 0x60,
  .wake_int_exp            = 13,
  .wake_int_exp_tol        = 13,
  .wake_int_mantissa       = 0x1D4C,
  .wake_int_mantissa_tol   = 0x1D4C,
  .implicit_twt            = 1,
  .un_announced_twt        = 1,
  .triggered_twt           = 0,
  .twt_channel             = 0,
  .twt_protection          = 0,
  .restrict_tx_outside_tsp = 1,
  .twt_retry_limit         = 6,
  .twt_retry_interval      = 10,
  .req_type                = 1,
  .negotiation_type        = 0,
};

sl_wifi_twt_selection_t default_twt_selection_configuration = {
  .twt_enable                            = 1,
  .average_tx_throughput                 = 0,
  .tx_latency                            = 0,
  .rx_latency                            = TWT_RX_LATENCY,
  .device_average_throughput             = DEVICE_AVERAGE_THROUGHPUT,
  .estimated_extra_wake_duration_percent = ESTIMATE_EXTRA_WAKE_DURATION_PERCENT,
  .twt_tolerable_deviation               = TWT_TOLERABLE_DEVIATION,
  .default_wake_interval_ms              = TWT_DEFAULT_WAKE_INTERVAL_MS,
  .default_minimum_wake_duration_ms      = TWT_DEFAULT_WAKE_DURATION_MS,
  .beacon_wake_up_count_after_sp         = MAX_BEACON_WAKE_UP_AFTER_SP
};


sl_status_t twt_callback_handler(sl_wifi_event_t event,
                                 sl_si91x_twt_response_t *result,
                                 uint32_t result_length,
                                 void *arg);
/******************************************************
 *               Function Definitions
 ******************************************************/

sl_status_t twt_callback_handler(sl_wifi_event_t event,
                                 sl_si91x_twt_response_t *result,
                                 uint32_t result_length,
                                 void *arg)
{
  UNUSED_PARAMETER(result_length);
  UNUSED_PARAMETER(arg);

  if (SL_WIFI_CHECK_IF_EVENT_FAILED(event)) {
    return SL_STATUS_FAIL;
  }

  switch (event) {
    case SL_WIFI_TWT_RESPONSE_EVENT:
      printf("\r\nTWT Setup success");
      wifi_app_send_to_mqtt(WIFI_APP_CONNECTION_STATUS, (uint8_t *)&connected, 1);
      break;
    case SL_WIFI_TWT_UNSOLICITED_SESSION_SUCCESS_EVENT:
      printf("\r\nUnsolicited TWT Setup success");
      break;
    case SL_WIFI_TWT_AP_REJECTED_EVENT:
      printf("\r\nTWT Setup Failed. TWT Setup rejected by AP");
      break;
    case SL_WIFI_TWT_OUT_OF_TOLERANCE_EVENT:
      printf("\r\nTWT Setup Failed. TWT response out of tolerance limits");
      break;
    case SL_WIFI_TWT_RESPONSE_NOT_MATCHED_EVENT:
      printf("\r\nTWT Setup Failed. TWT Response not matched with the request parameters");
      break;
    case SL_WIFI_TWT_UNSUPPORTED_RESPONSE_EVENT:
      printf("\r\nTWT Setup Failed. TWT Response Unsupported");
      break;
    case SL_WIFI_TWT_FAIL_MAX_RETRIES_REACHED_EVENT:
      printf("\r\nTWT Setup Failed. Max retries reached");
      break;
    case SL_WIFI_TWT_INACTIVE_DUE_TO_ROAMING_EVENT:
      printf("\r\nTWT session inactive due to roaming");
      break;
    case SL_WIFI_TWT_INACTIVE_DUE_TO_DISCONNECT_EVENT:
      printf("\r\nTWT session inactive due to wlan disconnection");
      break;
    case SL_WIFI_TWT_TEARDOWN_SUCCESS_EVENT:
      printf("\r\nTWT session teardown success");
      break;
    case SL_WIFI_TWT_AP_TEARDOWN_SUCCESS_EVENT:
      printf("\r\nTWT session teardown from AP");
      break;
    case SL_WIFI_TWT_INACTIVE_NO_AP_SUPPORT_EVENT:
      printf("\r\nConnected AP Does not support TWT");
      break;
    case SL_WIFI_RESCHEDULE_TWT_SUCCESS_EVENT:
      printf("\r\nTWT rescheduled");
      break;
    case SL_WIFI_TWT_INFO_FRAME_EXCHANGE_FAILED_EVENT:
      printf("\r\nTWT rescheduling failed due to a failure in the exchange of TWT information frames.");
      break;
    default:
      printf("\r\nTWT Setup Failed.");
  }
  if (event < SL_WIFI_TWT_TEARDOWN_SUCCESS_EVENT) {
    printf("\r\n wake duration : 0x%X", result->wake_duration);
    printf("\r\n wake_duration_unit: 0x%X", result->wake_duration_unit);
    printf("\r\n wake_int_exp : 0x%X", result->wake_int_exp);
    printf("\r\n negotiation_type : 0x%X", result->negotiation_type);
    printf("\r\n wake_int_mantissa : 0x%X", result->wake_int_mantissa);
    printf("\r\n implicit_twt : 0x%X", result->implicit_twt);
    printf("\r\n un_announced_twt : 0x%X", result->un_announced_twt);
    printf("\r\n triggered_twt : 0x%X", result->triggered_twt);
    printf("\r\n twt_channel : 0x%X", result->twt_channel);
    printf("\r\n twt_protection : 0x%X", result->twt_protection);
    printf("\r\n twt_flow_id : 0x%X\r\n", result->twt_flow_id);
  } else if (event < SL_WIFI_TWT_EVENTS_END) {
    printf("\r\n twt_flow_id : 0x%X", result->twt_flow_id);
    printf("\r\n negotiation_type : 0x%X\r\n", result->negotiation_type);
  }
  return SL_STATUS_OK;
}

// rejoin failure callback handler in station mode
sl_status_t join_callback_handler(sl_wifi_event_t event, char *result, uint32_t result_length, void *arg)
{
  UNUSED_PARAMETER(event);
  UNUSED_PARAMETER(result);
  UNUSED_PARAMETER(result_length);
  UNUSED_PARAMETER(arg);

  // update wlan application state
  disconnected = 1;
  connected    = 0;

  wifi_app_set_event(WIFI_APP_DISCONNECTED_STATE);

  return SL_STATUS_OK;
}

static sl_status_t show_scan_results()
{
  SL_WIFI_ARGS_CHECK_NULL_POINTER(scan_result);
  uint8_t *bssid = NULL;
  LOG_PRINT("%lu Scan results:\n", scan_result->scan_count);

  if (scan_result->scan_count) {
    LOG_PRINT("\n   %s %24s %s", "SSID", "SECURITY", "NETWORK");
    LOG_PRINT("%12s %12s %s\n", "BSSID", "CHANNEL", "RSSI");

    for (int a = 0; a < (int)scan_result->scan_count; ++a) {
      bssid = (uint8_t *)&scan_result->scan_info[a].bssid;
      LOG_PRINT("%-24s %4u,  %4u, ",
                scan_result->scan_info[a].ssid,
                scan_result->scan_info[a].security_mode,
                scan_result->scan_info[a].network_type);
      LOG_PRINT("  %02x:%02x:%02x:%02x:%02x:%02x, %4u,  -%u\n",
                bssid[0],
                bssid[1],
                bssid[2],
                bssid[3],
                bssid[4],
                bssid[5],
                scan_result->scan_info[a].rf_channel,
                scan_result->scan_info[a].rssi_val);
    }
  }

  return SL_STATUS_OK;
}

sl_status_t wlan_app_scan_callback_handler(sl_wifi_event_t event,
                                           sl_wifi_scan_result_t *result,
                                           uint32_t result_length,
                                           void *arg)
{
  UNUSED_PARAMETER(arg);
  UNUSED_PARAMETER(result_length);

  scan_complete = true;

  if (SL_WIFI_CHECK_IF_EVENT_FAILED(event)) {
    callback_status = *(sl_status_t *)result;
    return SL_STATUS_FAIL;
  }
  SL_VERIFY_POINTER_OR_RETURN(scan_result, SL_STATUS_NULL_POINTER);
  memset(scan_result, 0, scanbuf_size);
  memcpy(scan_result, result, scanbuf_size);

  callback_status = show_scan_results();

  //  scan_complete = true;
  return SL_STATUS_OK;
}

//SL_ADDITIONAL_STATUS_ERRORS
static sl_status_t set_twt(void){
  sl_wifi_performance_profile_t performance_profile = { 0 };
  sl_status_t status                                = SL_STATUS_OK;

  LOG_PRINT("\r\nSetting up TWT\n");
  //! Set TWT Config
  sl_wifi_set_twt_config_callback(twt_callback_handler, NULL);
  if (TWT_AUTO_CONFIG == 1) {
    performance_profile.twt_selection = default_twt_selection_configuration;
    status                            = sl_wifi_target_wake_time_auto_selection(&performance_profile.twt_selection);
  } else {
    performance_profile.twt_request = default_twt_setup_configuration;
    status                          = sl_wifi_enable_target_wake_time(&performance_profile.twt_request);
  }

  VERIFY_STATUS_AND_RETURN(status);
  // A small delay is added so that the asynchronous response from TWT is printed in correct format.
  osDelay(100);

  //! Enable Broadcast data filter
  status = sl_wifi_filter_broadcast(5000, 1, 1);

  VERIFY_STATUS_AND_RETURN(status);

  //! Apply power save profile
  performance_profile.profile = ASSOCIATED_POWER_SAVE;
  status                      = sl_wifi_set_performance_profile(&performance_profile);
  if (status != SL_STATUS_OK) {
    printf("\r\nPowersave Configuration Failed, Error Code : 0x%lX\r\n", status);
    return status;
  }

  LOG_PRINT("\r\nAssociated Power Save Enabled\n");
  return SL_STATUS_OK;
}

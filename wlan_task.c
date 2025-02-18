
#include <string.h>

#include "wlan_task.h"
#include "wlan_task_config.h"

#include "cmsis_os2.h"

#include "sl_status.h"

//SL Wi-Fi SDK includes
#include "sl_wifi.h"
#include "sl_wifi_callback_framework.h"
#include "sl_net.h"
#include "sl_utility.h"
#include "sl_net_si91x.h"

// Local utilities
#include "thread_safe_print.h"

/*
 *********************************************************************************************************
 *                                         LOCAL GLOBAL VARIABLES
 *********************************************************************************************************
 */

// RTOS Variables
const osThreadAttr_t wlan_thread_attributes = {
    .name       = "wlan_thread",
    .attr_bits  = 0,
    .cb_mem     = 0,
    .cb_size    = 0,
    .stack_mem  = 0,
    .stack_size = 3072,
    .priority   = osPriorityHigh,
    .tz_module  = 0,
    .reserved   = 0,
  };

osSemaphoreId_t wlan_thread_sem;

// WLAN Variables
uint8_t magic_word = 0; //TODO what is it ?
sl_wifi_scan_result_t *scan_result          = NULL;
uint16_t scanbuf_size = (sizeof(sl_wifi_scan_result_t) + (SL_WIFI_MAX_SCANNED_AP * sizeof(scan_result->scan_info[0])));
static uint32_t wlan_app_event_map;
uint8_t connected = 0;
uint8_t timeout = 0;
uint8_t disconnected = 0;
uint8_t disassosiated = 0;
uint8_t a = 0;//TODO  what is it ? Cleanup required as this seems to be used for basic IP setup
uint8_t retry = 1; //TODO what retry 
uint8_t conn_status;
static volatile bool scan_complete          = false;
static volatile sl_status_t callback_status = SL_STATUS_OK;
sl_wifi_client_configuration_t access_point = { 0 };
sl_net_ip_configuration_t ip_address        = { 0 };

//extern uint8_t coex_ssid[50], pwd[34], sec_type;
uint8_t coex_ssid[50], pwd[34], sec_type;

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

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void wlan_task(void *argument);
sl_status_t twt_callback_handler(sl_wifi_event_t event,
    sl_si91x_twt_response_t *result,    
    uint32_t result_length,
    void *arg);

static sl_status_t show_scan_results();
sl_status_t join_callback_handler(sl_wifi_event_t event, char *result, uint32_t result_length, void *arg);
sl_status_t wlan_app_scan_callback_handler(sl_wifi_event_t event,
    sl_wifi_scan_result_t *result,
    uint32_t result_length,
    void *arg);

static sl_status_t set_twt(void);

/*
 *********************************************************************************************************
 *                                         PUBLIC FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
sl_status_t start_wlan_task_context(void)
{
    sl_status_t ret = SL_STATUS_OK;

    wlan_thread_sem = osSemaphoreNew(1, 0, NULL);
    if (wlan_thread_sem == NULL) {
      THREAD_SAFE_PRINT("Failed to create wlan_thread_sem\n");
      return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("WLAN Semaphore Creation Complete\n");

    //TODO Init Queue or flag

    osThreadId_t wlan_thread_id = osThreadNew((osThreadFunc_t)wlan_task, NULL, &wlan_thread_attributes);
    if (wlan_thread_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create wlan_task\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("WLAN Task Startup Complete\n");
    
    THREAD_SAFE_PRINT("\nWLAN Task Context Init Done\n");
    return ret;
}


/*==============================================*/
/**
 * @fn         wifi_app_set_event
 * @brief      sets the specific event.
 * @param[in]  event_num, specific event number.
 * @return     none.
 * @section description
 * This function is used to set/raise the specific event.
 */
void wifi_app_set_event(uint32_t event_num)
{
  wlan_app_event_map |= BIT(event_num);

  osSemaphoreRelease(wlan_thread_sem);

  return;
}

/*==============================================*/
/**
 * @fn         wifi_app_clear_event
 * @brief      clears the specific event.
 * @param[in]  event_num, specific event number.
 * @return     none.
 * @section description
 * This function is used to clear the specific event.
 */
void wifi_app_clear_event(uint32_t event_num)
{
  wlan_app_event_map &= ~BIT(event_num);
  return;
}

/*==============================================*/
/**
 * @fn         wifi_app_get_event
 * @brief      returns the first set event based on priority
 * @param[in]  none.
 * @return     int32_t
 *             > 0  = event number
 *             -1   = not received any event
 * @section description
 * This function returns the highest priority event among all the set events
 */
int32_t wifi_app_get_event(void)
{
  uint32_t ix;

  for (ix = 0; ix < 32; ix++) {
    if (wlan_app_event_map & (1 << ix)) {
      return ix;
    }
  }

  return (-1);
}

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
void wlan_task(void *argument)
{
    UNUSED_PARAMETER(argument);

    int32_t status                     = RSI_SUCCESS;
    sl_wifi_firmware_version_t version = { 0 };

    int32_t wlan_event_id = 0;

    //! Wi-Fi initialization
    status = sl_wifi_init(&config, NULL, sl_wifi_default_event_handler);
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nWi-Fi Initialization Failed, Error Code : 0x%lX\r\n", status);
        return;
    }
    THREAD_SAFE_PRINT("\r\n Wi-Fi initialization is successful\n");

    //! Firmware version Prints
    status = sl_wifi_get_firmware_version(&version);
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFirmware version Failed, Error Code : 0x%lX\r\n", status);
    } else {
        print_firmware_version(&version);
    }

    // Allocate memory for scan buffer
    scan_result = (sl_wifi_scan_result_t *)malloc(scanbuf_size);
    if (scan_result == NULL) {
        THREAD_SAFE_PRINT("Failed to allocate memory for scan result\n");
        return;
    }
    memset(scan_result, 0, scanbuf_size);

    while (true)
    {
        // checking for events list
        wlan_event_id = wifi_app_get_event();
        if (wlan_event_id == -1) {
            osSemaphoreAcquire(wlan_thread_sem, osWaitForever);
            // if events are not received loop will be continued.
            continue;
        }

        switch (wlan_event_id) {
            case WIFI_APP_INITIAL_STATE: {
                wifi_app_clear_event(WIFI_APP_INITIAL_STATE);
                THREAD_SAFE_PRINT("WIFI Task Initial State\n");

                //! Initialize join fail call back
                sl_wifi_set_join_callback(join_callback_handler, NULL);

                // update wlan application state
                if (magic_word) {
                    // clear the served event
                    wifi_app_set_event(WIFI_APP_FLASH_STATE);
                } else {
                    wifi_app_set_event(WIFI_APP_SCAN_STATE);
                }
            } break;

            case WIFI_APP_UNCONNECTED_STATE: {
                wifi_app_clear_event(WIFI_APP_UNCONNECTED_STATE);
                THREAD_SAFE_PRINT("WIFI App Unconnected State\n");

                // Any additional code if required
                osSemaphoreRelease(wlan_thread_sem);
            } break;

            case WIFI_APP_SCAN_STATE: {
                wifi_app_clear_event(WIFI_APP_SCAN_STATE);

                sl_wifi_scan_configuration_t wifi_scan_configuration = { 0 };
                wifi_scan_configuration                              = default_wifi_scan_configuration;

                sl_wifi_set_scan_callback(wlan_app_scan_callback_handler, NULL);

                status = sl_wifi_start_scan(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, NULL, &wifi_scan_configuration);
                if (SL_STATUS_IN_PROGRESS == status) {
                    THREAD_SAFE_PRINT("Scanning...\r\n");
                    const uint32_t start = osKernelGetTickCount();

                    while (!scan_complete && (osKernelGetTickCount() - start) <= WIFI_SCAN_TIMEOUT) {
                        osThreadYield();
                    }
                    status = scan_complete ? callback_status : SL_STATUS_TIMEOUT;
                }
                if (status != SL_STATUS_OK) {
                    THREAD_SAFE_PRINT("\r\nWLAN Scan Wait Failed, Error Code : 0x%lX\r\n", status);
                    wifi_app_set_event(WIFI_APP_SCAN_STATE);
                    osDelay(1000);
                } else {
                    // update wlan application state
                    //wifi_app_send_to_ble(WIFI_APP_SCAN_RESP, (uint8_t *)scan_result, scanbuf_size);
                }
                osSemaphoreRelease(wlan_thread_sem);
            } break;

            case WIFI_APP_JOIN_STATE: {
                sl_wifi_credential_t cred  = { 0 };
                sl_wifi_credential_id_t id = SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID;
                memset(&access_point, 0, sizeof(sl_wifi_client_configuration_t));

                cred.type = SL_WIFI_PSK_CREDENTIAL;
                memcpy(cred.psk.value, pwd, strlen((char *)pwd));

                status = sl_net_set_credential(id, SL_NET_WIFI_PSK, pwd, strlen((char *)pwd));
                if (SL_STATUS_OK == status) {
                    THREAD_SAFE_PRINT("Credentials set, id : %lu\n", id);

                    access_point.ssid.length = strlen((char *)coex_ssid);
                    memcpy(access_point.ssid.value, coex_ssid, access_point.ssid.length);
                    access_point.security      = sec_type;
                    access_point.encryption    = SL_WIFI_DEFAULT_ENCRYPTION;
                    access_point.credential_id = id;

                    THREAD_SAFE_PRINT("SSID=%s\n", access_point.ssid.value);
                    status = sl_wifi_connect(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, &access_point, TIMEOUT_MS);
                }
                if (status != RSI_SUCCESS) {
                    timeout = 1;
                    //wifi_app_send_to_ble(WIFI_APP_TIMEOUT_NOTIFY, (uint8_t *)&timeout, 1);
                    wifi_app_clear_event(WIFI_APP_JOIN_STATE);
                    THREAD_SAFE_PRINT("\r\nWLAN Connect Failed, Error Code : 0x%lX\r\n", status);

                    // update wlan application state
                    disconnected = 1;
                    connected    = 0;
                } else {
                    THREAD_SAFE_PRINT("\n WLAN connection is successful\n");
                    // update wlan application state
                    wifi_app_clear_event(WIFI_APP_JOIN_STATE);
                    wifi_app_set_event(WIFI_APP_CONNECTED_STATE);
                }
                osSemaphoreRelease(wlan_thread_sem);
                THREAD_SAFE_PRINT("WIFI App Join State\n");
            } break;

            case WIFI_APP_FLASH_STATE: {
                wifi_app_clear_event(WIFI_APP_FLASH_STATE);

                if (retry) {
                status = sl_wifi_connect(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, &access_point, TIMEOUT_MS);
                if (status != RSI_SUCCESS) {
                    THREAD_SAFE_PRINT("\r\nWLAN Connect Failed, Error Code : 0x%lX\r\n", status);
                    break;
                } else {
                    wifi_app_set_event(WIFI_APP_CONNECTED_STATE);
                }
                }

                osSemaphoreRelease(wlan_thread_sem);
            } break;

            case WIFI_APP_CONNECTED_STATE: {
                wifi_app_clear_event(WIFI_APP_CONNECTED_STATE);

                ip_address.type      = SL_IPV4;
                ip_address.mode      = SL_IP_MANAGEMENT_DHCP;
                ip_address.host_name = DHCP_HOST_NAME;

                // Configure IP
                status = sl_si91x_configure_ip_address(&ip_address, SL_SI91X_WIFI_CLIENT_VAP_ID);
                if (status != RSI_SUCCESS) {
                    a++;
                    if (a == 3) {
                        a       = 0;
                        timeout = 1;
                        status  = sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
                        if (status == RSI_SUCCESS) {
                        connected     = 0;
                        disassosiated = 1;
                        //wifi_app_send_to_ble(WIFI_APP_TIMEOUT_NOTIFY, (uint8_t *)&timeout, 1);
                        wifi_app_set_event(WIFI_APP_ERROR_STATE);
                        }
                    }
                    THREAD_SAFE_PRINT("\r\nIP Config Failed, Error Code : 0x%lX\r\n", status);
                    break;
                } else {
                    a             = 0;
                    connected     = 1;
                    conn_status   = 1;
                    disconnected  = 0;
                    disassosiated = 0;

#if defined(SL_SI91X_PRINT_DBG_LOG)
                    THREAD_SAFE_PRINT("\r\nIP Address : \r\n");
                    sl_ip_address_t ip = { 0 };
                    ip.type            = ip_address.type;
                    ip.ip.v4.value     = ip_address.ip.v4.ip_address.value;
                    print_sl_ip_address(&ip);
                    THREAD_SAFE_PRINT("\r\n");
#endif
                    // update wlan application state
                    wifi_app_set_event(WIFI_APP_IPCONFIG_DONE_STATE);
                    //wifi_app_send_to_ble(WIFI_APP_CONNECTION_STATUS, (uint8_t *)&connected, 1);
                }
                osSemaphoreRelease(wlan_thread_sem);
                THREAD_SAFE_PRINT("WIFI App Connected State\n");
            } break;

            case WIFI_APP_IPCONFIG_DONE_STATE: {
                wifi_app_clear_event(WIFI_APP_IPCONFIG_DONE_STATE);

                osSemaphoreRelease(wlan_thread_sem);
                THREAD_SAFE_PRINT("WIFI App IPCONFIG Done State, setting up TWT\n");

                status = set_twt();
                if (status != SL_STATUS_OK) {
                printf("\r\nError while configuring TWT parameters: 0x%lx \r\n", status);
                return;
                }
                printf("\r\nTWT Config Done\r\n");
            } break;

            case WIFI_APP_ERROR_STATE: {

            } break;

            case WIFI_APP_DISCONNECTED_STATE: {
                wifi_app_clear_event(WIFI_APP_DISCONNECTED_STATE);
                retry = 1;
                //wifi_app_send_to_ble(WIFI_APP_DISCONNECTION_STATUS, (uint8_t *)&disconnected, 1);
                wifi_app_set_event(WIFI_APP_FLASH_STATE);

                osSemaphoreRelease(wlan_thread_sem);
                THREAD_SAFE_PRINT("WIFI App Disconnected State\n");

            } break;

            case WIFI_APP_DISCONN_NOTIFY_STATE: {
                wifi_app_clear_event(WIFI_APP_DISCONN_NOTIFY_STATE);

                status = sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
                if (status == RSI_SUCCESS) {
        #if RSI_WISE_MCU_ENABLE
                rsi_flash_erase((uint32_t)FLASH_ADDR_TO_STORE_AP_DETAILS);
        #endif
                THREAD_SAFE_PRINT("\r\nWLAN Disconnected\r\n");
                disassosiated = 1;
                connected     = 0;
                //wifi_app_send_to_ble(WIFI_APP_DISCONNECTION_NOTIFY, (uint8_t *)&disassosiated, 1);
                wifi_app_set_event(WIFI_APP_UNCONNECTED_STATE);
                } else {
                THREAD_SAFE_PRINT("\r\nWIFI Disconnect Failed, Error Code : 0x%lX\r\n", status);
                }

                osSemaphoreRelease(wlan_thread_sem);
                THREAD_SAFE_PRINT("WIFI App Disconnect Notify State\n");
            } break;
            case WIFI_APP_SOCKET_RECEIVE_STATE:
                break;
            case WIFI_APP_MQTT_INIT_DONE_STATE:
                break;
            case WIFI_APP_MQTT_SUBSCRIBE_DONE_STATE:
                break;
            case BLE_APP_GATT_WRITE_EVENT:
                break;
            case WIFI_APP_DATA_RECEIVE_STATE:
                break;
            case WIFI_APP_SD_WRITE_STATE:
                break;
            case WIFI_APP_DEMO_COMPLETE_STATE:
                break;
            default:
                break;
        }//switch(wlan_event_id)
    }//while(1)
}

//SL_ADDITIONAL_STATUS_ERRORS
static sl_status_t set_twt(void){
    sl_wifi_performance_profile_t performance_profile = { 0 };
    sl_status_t status                                = SL_STATUS_OK;
  
    THREAD_SAFE_PRINT("\r\nSetting up TWT\n");
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
  
    THREAD_SAFE_PRINT("\r\nAssociated Power Save Enabled\n");
    return SL_STATUS_OK;
  }

/*
 *********************************************************************************************************
 *                                         CALLBACK FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

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
            //wifi_app_send_to_mqtt(WIFI_APP_CONNECTION_STATUS, (uint8_t *)&connected, 1);
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
  sl_status_t status = SL_STATUS_OK;

  SL_WIFI_ARGS_CHECK_NULL_POINTER(scan_result);
  uint8_t *bssid = NULL;
  THREAD_SAFE_PRINT("%lu Scan results:\n", scan_result->scan_count);

  if (scan_result->scan_count) {
    THREAD_SAFE_PRINT("\n   %s %24s %s", "SSID", "SECURITY", "NETWORK");
    THREAD_SAFE_PRINT("%12s %12s %s\n", "BSSID", "CHANNEL", "RSSI");

    for (int a = 0; a < (int)scan_result->scan_count; ++a) {
      bssid = (uint8_t *)&scan_result->scan_info[a].bssid;
      THREAD_SAFE_PRINT("%-24s %4u,  %4u, ",
                scan_result->scan_info[a].ssid,
                scan_result->scan_info[a].security_mode,
                scan_result->scan_info[a].network_type);
      THREAD_SAFE_PRINT("  %02x:%02x:%02x:%02x:%02x:%02x, %4u,  -%u\n",
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

  return status;
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

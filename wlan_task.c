
#include <string.h>

#include "wlan_task.h"
#include "wlan_task_config.h"

#include "nwp_task_config.h"
#include "nwp_task.h"

#include "cmsis_os2.h"
#include "sl_status.h"

// Local utilities
#include "thread_safe_print.h"

//SL Wi-Fi SDK includes
#include "sl_constants.h"
#include "sl_wifi.h"
#include "sl_wifi_callback_framework.h"
#include "sl_net.h"
#include "sl_utility.h"
#include "sl_net_si91x.h"

#include "sl_si91x_driver.h"

#include "sl_si91x_power_manager.h"


#define     WLAN_EVENT_FLAGS_MSK  0x0000FFFFU  // Define the flag mask

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
    .priority   = osPriorityHigh1,
    .tz_module  = 0,
    .reserved   = 0,
  };

osMessageQueueId_t  wlan_evt_queue_id;  // Event flags ID
static uint8_t      wlan_evt_queue_seq_num_g = 0;

// WLAN Variables

// WLAN Scan Configuration variables
sl_wifi_scan_result_t *scan_result          = NULL;
sl_wifi_scan_configuration_t wifi_scan_configuration = { 0 };
uint16_t scanbuf_size = (sizeof(sl_wifi_scan_result_t) + (SL_WIFI_MAX_SCANNED_AP * sizeof(scan_result->scan_info[0])));
sl_status_t scan_status = SL_STATUS_OK; // TODO make it an argument of flags

uint8_t connected = 0;
uint8_t timeout = 0;
uint8_t disconnected = 0;
uint8_t disassosiated = 0;
uint8_t a = 0;//TODO  what is it ? Cleanup required as this seems to be used for basic IP setup
uint8_t retry = 1; //TODO what retry 
uint8_t conn_status;

sl_wifi_client_configuration_t access_point = { 0 };
sl_net_ip_configuration_t ip_address        = { 0 };

extern uint8_t coex_ssid[50], pwd[34], sec_type;
//uint8_t coex_ssid[50], pwd[34], sec_type;

sl_wifi_twt_request_t default_twt_setup_configuration = {
    .twt_enable              = 1,
    .twt_flow_id             = 1,
    .wake_duration           = TWT_WAKE_DURATION,
    .wake_duration_unit      = TWT_WAKE_DURATION_UNIT,
    .wake_duration_tol       = TWT_WAKE_DURATION_TOL,
    .wake_int_exp            = TWT_WAKE_INT_EXP,
    .wake_int_exp_tol        = TWT_WAKE_INT_EXP_TOL,
    .wake_int_mantissa       = TWT_WAKE_INT_MANTISSA,
    .wake_int_mantissa_tol   = TWT_WAKE_INT_MANTISSA_TOL,
    .implicit_twt            = 1,
    .un_announced_twt        = 1,
    .triggered_twt           = 0,
    .twt_channel             = 0,
    .twt_protection          = 0,
    .restrict_tx_outside_tsp = 1,
    .twt_retry_limit         = TWT_WAKE_RETRY_LIMIT,
    .twt_retry_interval      = TWT_WAKE_RETRY_INTERVAL,
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
static void wlan_wait_event(wlan_event_msg_t *event_msg);

sl_status_t join_callback_handler(sl_wifi_event_t event, char *result, uint32_t result_length, void *arg);

static void show_scan_results(void);
sl_status_t wlan_app_scan_callback_handler(sl_wifi_event_t event,
    sl_wifi_scan_result_t *result,
    uint32_t result_length,
    void *arg);

static sl_status_t set_twt(void);
sl_status_t twt_callback_handler(sl_wifi_event_t event,
    sl_si91x_twt_response_t *result,
    uint32_t result_length,
    void *arg);

/*
 *********************************************************************************************************
 *                                         PUBLIC FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
sl_status_t start_wlan_task_context(void)
{
    sl_status_t ret = SL_STATUS_OK;

    THREAD_SAFE_PRINT("WLAN Task Context Init Start\n");

    wlan_evt_queue_id = osMessageQueueNew(  SL_WLAN_EVENT_QUEUE_SIZE, 
                                            sizeof(wlan_event_msg_t), 
                                            NULL);  // Create message queue
    if (wlan_evt_queue_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create wlan_evt_queue_id\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("WLAN Queue Creation Complete\n");

    osThreadId_t wlan_thread_id = osThreadNew((osThreadFunc_t)wlan_task, NULL, &wlan_thread_attributes);
    if (wlan_thread_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create wlan_task\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("WLAN Task Startup Complete\n");
    
    THREAD_SAFE_PRINT("WLAN Task Context Init Done\n\n");
    return ret;
}

/**
 * @brief Sets a WLAN event and adds it to the event queue.
 *
 * @param event_id The ID of the WLAN event to set.
 * @param event_data Pointer to the event data, or NULL if no data.
 * @param event_data_len Length of the event data, in bytes.
 *
 * This function is used to set a WLAN event and add it to the event queue. The event
 * message is constructed with the provided event ID, a sequence number, and the
 * event data (if provided). The event message is then added to the WLAN event
 * queue using osMessageQueuePut().
 */
void wlan_set_event(uint32_t event_id, void *event_data, uint32_t event_data_len)
{
    wlan_event_msg_t event_msg;

    event_msg.event_id = event_id;
    event_msg.seq_num = wlan_evt_queue_seq_num_g;

    if(     (event_data != NULL)
        &&  (event_data_len > 0))
    {
        memcpy(event_msg.payload, event_data, event_data_len);
    } else {
        memset(event_msg.payload, 0, SL_WLAN_EVENT_MAX_PAYLOAD_SIZE);
    }

    osStatus_t status = osMessageQueuePut(  wlan_evt_queue_id, 
                                            &event_msg, 
                                            0, // Message priority
                                            0); // Timeout - Return immediately

    if (status != osOK) {
        THREAD_SAFE_PRINT("Failed to send wlan event: 0x%lx\r\n", (uint32_t)status);
    } else {
        wlan_evt_queue_seq_num_g++;
    }
}

static void wlan_wait_event(wlan_event_msg_t *event_msg)
{
    osStatus_t status = osMessageQueueGet(  wlan_evt_queue_id, 
                                            event_msg, 
                                            NULL, 
                                            osWaitForever);

    if(status != osOK) {
        THREAD_SAFE_PRINT("Failed to get wlan event: 0x%lx\r\n", (uint32_t)status);
    }
}

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
void wlan_task(void *argument)
{
    UNUSED_PARAMETER(argument);

    sl_status_t status                 = SL_STATUS_OK;
    sl_wifi_firmware_version_t version = { 0 };

    wlan_event_msg_t wlan_event_msg;

    status = nwp_access_request();
    THREAD_SAFE_PRINT("WLAN Acquiring NWP Semaphore\r\n");
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to acquire NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    //TODO check how this can be better managed vs nwp task
    status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, &station_init_configuration, NULL, NULL);
    if ((status != SL_STATUS_OK)
        && (status != SL_STATUS_ALREADY_INITIALIZED)){
      printf("\r\nFailed to bring Wi-Fi client interface up: 0x%lX\r\n", status);
      return;
    }
    printf("\r\nWi-Fi client interface init success or already initialized\r\n");

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
        return; // Should be an assertion
    }
    memset(scan_result, 0, scanbuf_size);

    THREAD_SAFE_PRINT("WLAN Releasing NWP Semaphore\r\n");
    status = nwp_access_release();
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    // Initialize the message queue sequence number
    wlan_evt_queue_seq_num_g = 0;

    //Consider the WLAN task as initialized
    wlan_set_dataless_event(WLAN_BOOT_EVENT);

    while (true)
    {
        wlan_wait_event(&wlan_event_msg);

        switch (wlan_event_msg.event_id) {
            case WLAN_BOOT_EVENT: {
                THREAD_SAFE_PRINT("WLAN Boot Event\n");
                // Initialize join fail callback
                sl_wifi_set_join_callback(join_callback_handler, NULL);

                // Initialize scan callback
                sl_wifi_set_scan_callback(wlan_app_scan_callback_handler, NULL);

                //Use default scan configuration
                wifi_scan_configuration = default_wifi_scan_configuration;

                // Did we join and saved credentials?
                // If so, rejoin
                if(0){//TODO
                    THREAD_SAFE_PRINT("WLAN Connect to known AP\n");
                    status = sl_wifi_connect(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, &access_point, TIMEOUT_MS);
                    if (status != SL_STATUS_OK) {
                        THREAD_SAFE_PRINT("Failed to connect to AP: 0x%lX\r\n", status);
                    } else {
                        THREAD_SAFE_PRINT("Connected to AP\n");
                        wlan_set_dataless_event(WLAN_CONNECTED_EVENT);
                    }
                } else {
                    THREAD_SAFE_PRINT("WLAN Start Scan\n");
                    // If not, start a scan 
                    status = sl_wifi_start_scan(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, NULL, &wifi_scan_configuration);
                    if (  (status != SL_STATUS_OK)
                        ||(status != SL_STATUS_IN_PROGRESS))
                    {
                        THREAD_SAFE_PRINT("Failed to start scan: 0x%lX\r\n", status);
                    }
                }
            } break;

            case WLAN_SCAN_COMPLETE_EVENT: {
                THREAD_SAFE_PRINT("WLAN Scan Complete with status %lX\n", (uint32_t)scan_status);
                if(scan_status == SL_STATUS_OK) {
                    // TODO : JEROME IDEA TO DEFINE A Callback to app
                    //wifi_app_send_to_ble(WIFI_APP_SCAN_RESP, (uint8_t *)scan_result, scanbuf_size);
                }
            } break;

            // case WIFI_APP_JOIN_STATE: {
            //     sl_wifi_credential_t cred  = { 0 };
            //     sl_wifi_credential_id_t id = SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID;
            //     memset(&access_point, 0, sizeof(sl_wifi_client_configuration_t));

            //     cred.type = SL_WIFI_PSK_CREDENTIAL;
            //     memcpy(cred.psk.value, pwd, strlen((char *)pwd));

            //     status = sl_net_set_credential(id, SL_NET_WIFI_PSK, pwd, strlen((char *)pwd));
            //     if (SL_STATUS_OK == status) {
            //         THREAD_SAFE_PRINT("Credentials set, id : %lu\n", id);

            //         access_point.ssid.length = strlen((char *)coex_ssid);
            //         memcpy(access_point.ssid.value, coex_ssid, access_point.ssid.length);
            //         access_point.security      = sec_type;
            //         access_point.encryption    = SL_WIFI_DEFAULT_ENCRYPTION;
            //         access_point.credential_id = id;

            //         THREAD_SAFE_PRINT("SSID=%s\n", access_point.ssid.value);
            //         status = sl_wifi_connect(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, &access_point, TIMEOUT_MS);
            //     }
            //     if (status != RSI_SUCCESS) {
            //         timeout = 1;
            //         //wifi_app_send_to_ble(WIFI_APP_TIMEOUT_NOTIFY, (uint8_t *)&timeout, 1);
            //         wifi_app_clear_event(WIFI_APP_JOIN_STATE);
            //         THREAD_SAFE_PRINT("\r\nWLAN Connect Failed, Error Code : 0x%lX\r\n", status);

            //         // update wlan application state
            //         disconnected = 1;
            //         connected    = 0;
            //     } else {
            //         THREAD_SAFE_PRINT("\n WLAN connection is successful\n");
            //         // update wlan application state
            //         wifi_app_clear_event(WIFI_APP_JOIN_STATE);
            //         wifi_app_set_event(WIFI_APP_CONNECTED_STATE);
            //     }
            //     osSemaphoreRelease(wlan_thread_sem);
            //     THREAD_SAFE_PRINT("WIFI App Join State\n");
            // } break;

            case WLAN_CONNECTED_EVENT: {

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
                            //wifi_app_set_event(WIFI_APP_ERROR_STATE);
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
                    wlan_set_dataless_event(WLAN_IPCONFIG_DONE_EVENT);
                    //wifi_app_send_to_ble(WIFI_APP_CONNECTION_STATUS, (uint8_t *)&connected, 1);
                }
                THREAD_SAFE_PRINT("WIFI App Connected State\n");
            } break;

            case WLAN_IPCONFIG_DONE_EVENT: {
                THREAD_SAFE_PRINT("WIFI App IPCONFIG Done State, setting up TWT\n");

                status = set_twt();
                if (status != SL_STATUS_OK) {
                printf("\r\nError while configuring TWT parameters: 0x%lx \r\n", status);
                return;
                }
                printf("\r\nTWT Config Done\r\n");
            } break;

            case WLAN_UNCONNECTED_EVENT: {
                THREAD_SAFE_PRINT("WIFI App Disconnected State\n");
                retry = 1;
            } break;

            case WLAN_DISCONNECT_REQUEST_EVENT: {
                THREAD_SAFE_PRINT("WIFI App Disconnect Request\n");
                status = sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
                if (status == RSI_SUCCESS) {
#if RSI_WISE_MCU_ENABLE
                    rsi_flash_erase((uint32_t)FLASH_ADDR_TO_STORE_AP_DETAILS);
#endif
                    THREAD_SAFE_PRINT("\r\nWLAN Disconnected\r\n");
                    disassosiated = 1;
                    connected     = 0;
                    wlan_set_dataless_event(WLAN_UNCONNECTED_EVENT);
                } else {
                    THREAD_SAFE_PRINT("\r\nWIFI Disconnect Failed, Error Code : 0x%lX\r\n", status);
                }

            } break;
            default:
                break;
        }//switch(wlan_event_id)
    }//while(1)
}

// components/common/inc/SL_ADDITIONAL_STATUS.h
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
  UNUSED_PARAMETER(result_length);
  UNUSED_PARAMETER(arg);

  sl_status_t ret = SL_STATUS_OK;

  // In case of event failure, the `SL_WIFI_EVENT_FAIL_INDICATION` bit is set in the `event` parameter.
  // When this bit is set, the `data` parameter will be of type `sl_status_t`, and the `data_length` parameter can be ignored.

    if((event & SL_WIFI_EVENT_FAIL_INDICATION) == SL_WIFI_EVENT_FAIL_INDICATION) {
        THREAD_SAFE_PRINT("Join failed with status: %lX\n", (uint32_t)(*(sl_status_t *)result));

        // update wlan application state
        disconnected = 1;
        connected    = 0;

        ret = SL_STATUS_FAIL;
    }

    wlan_set_dataless_event(WLAN_JOIN_EVENT);

  return ret;
}

static void show_scan_results(void)
{
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
}

sl_status_t wlan_app_scan_callback_handler( sl_wifi_event_t event,
                                            sl_wifi_scan_result_t *result,
                                            uint32_t result_length,
                                            void *arg)
{
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(result_length);

    if (SL_WIFI_CHECK_IF_EVENT_FAILED(event)) {
        scan_status = *(sl_status_t *)result;
        return SL_STATUS_FAIL;
    }

    SL_VERIFY_POINTER_OR_RETURN(scan_result, SL_STATUS_NULL_POINTER);
    memset(scan_result, 0, scanbuf_size);
    memcpy(scan_result, result, scanbuf_size);

    show_scan_results();

    wlan_set_dataless_event(WLAN_SCAN_COMPLETE_EVENT);

    return SL_STATUS_OK;
}


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

osMessageQueueId_t                wlan_evt_queue_id;  // Event flags ID
static uint8_t                    wlan_evt_queue_seq_num_g = 0;
static sl_net_ip_configuration_t  ip_address        = { 0 };

// WLAN Variables
sl_wifi_client_configuration_t access_point = { 0 };//Retained AP

// WLAN Scan Configuration variables
uint8_t connected = 0;
uint8_t timeout = 0;
uint8_t disconnected = 0;
uint8_t disassosiated = 0;
uint8_t a = 0;//TODO  what is it ? Cleanup required as this seems to be used for basic IP setup
uint8_t retry = 1; //TODO what retry 
uint8_t conn_status;

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void wlan_task(void *argument);
static void wlan_wait_event(wlan_event_msg_t *event_msg);

sl_status_t join_callback_handler(sl_wifi_event_t event, char *result, uint32_t result_length, void *arg);

sl_status_t wlan_net_event_handler(sl_net_event_t event,
    sl_status_t status,
    void *data,
    uint32_t data_length);

static void show_scan_results(sl_wifi_scan_result_t *result);
sl_status_t wlan_app_scan_callback_handler(sl_wifi_event_t event,
    sl_wifi_scan_result_t *result,
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
 *                                         AF LIKE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

sl_status_t start_wlan_access_point_join(const void *ssid,
                                         uint32_t ssid_length,
                                         sl_wifi_credential_type_t type,
                                         const void *credential,
                                         uint32_t credential_length,
                                         uint8_t sec_type,
                                         uint32_t timeout_ms)
{
  sl_status_t status = SL_STATUS_OK;
  sl_wifi_credential_t cred  = { 0 };
  sl_wifi_credential_id_t id = SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID;

  THREAD_SAFE_PRINT("WLAN Connect to AP\n");
  memset(&access_point, 0, sizeof(sl_wifi_client_configuration_t));

  cred.type = type;
  memcpy(cred.psk.value, credential, credential_length);

  status = sl_net_set_credential(id, SL_NET_WIFI_PSK, credential, credential_length);
  if (SL_STATUS_OK == status) {
    THREAD_SAFE_PRINT("Credentials set, id : %lu\n", id);

    access_point.ssid.length = ssid_length;
    memcpy(access_point.ssid.value, ssid, ssid_length);
    access_point.security      = sec_type;
    access_point.encryption    = SL_WIFI_DEFAULT_ENCRYPTION;
    access_point.credential_id = id;

    THREAD_SAFE_PRINT("SSID=%s\n", access_point.ssid.value);
    status = sl_wifi_connect(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, &access_point, timeout_ms);
  }

  if (SL_STATUS_OK == status) {
    THREAD_SAFE_PRINT("WLAN AP connection is successful\n");
    wlan_set_dataless_event(WLAN_CONNECTED_EVENT);
  } else {
    THREAD_SAFE_PRINT("WLAN connection failed\n");
  }

  return status;
}

void start_wlan_access_point_disconnect(void)
{
  sl_status_t status = SL_STATUS_OK;

  status = sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
  if (status == SL_STATUS_OK) {
      wlan_set_dataless_event(WLAN_DISCONNECTED_EVENT);
  } else {
      THREAD_SAFE_PRINT("\r\nWIFI Disconnect Failed, Error Code : 0x%lX\r\n", status);
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
    status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, &station_init_configuration, NULL, wlan_net_event_handler);
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

                //TODO join existing AP if found
                // Did we join and saved credentials?
                // If so, rejoin
                if(0){
                    THREAD_SAFE_PRINT("WLAN Connect to known AP\n");
                    status = sl_wifi_connect(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, &access_point, TIMEOUT_MS);
                    if (status != SL_STATUS_OK) {
                        THREAD_SAFE_PRINT("Failed to connect to AP: 0x%lX\r\n", status);
                    } else {
                        THREAD_SAFE_PRINT("Connected to AP\n");// TODO
                        wlan_set_dataless_event(WLAN_CONNECTED_EVENT);
                    }
                }
            } break;

            case WLAN_SCAN_COMPLETE_EVENT: {
                THREAD_SAFE_PRINT("WLAN Scan Complete\n");
            } break;

            case WLAN_CONNECTED_EVENT: {
                THREAD_SAFE_PRINT("WIFI Interface Connected \n");

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
                        if (status == SL_STATUS_OK) {//TODO should be event driven
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
                    wlan_set_event(WLAN_IPCONFIG_DONE_EVENT, &ip, sizeof(sl_ip_address_t));
                }
            } break;

            case WLAN_IPCONFIG_DONE_EVENT: {
                THREAD_SAFE_PRINT("WIFI IPCONFIG Done\n");
                wlan_set_dataless_event(WLAN_JOIN_COMPLETE_EVENT);
            } break;

            case WLAN_JOIN_COMPLETE_EVENT: {
                THREAD_SAFE_PRINT("WIFI Joining complete\n");
                nwp_set_event(WLAN_EVENT, &(wlan_event_msg.event_id));
                //! Enable Broadcast data filter
                status = sl_wifi_filter_broadcast(5000, 1, 1);//TODO provide macro for settings
            } break;

            case WLAN_DISCONNECTED_EVENT: {
                THREAD_SAFE_PRINT("WIFI Disconnected\n");
            } break;

            default:
                break;
        }//switch(wlan_event_id)
        wlan_on_event(&wlan_event_msg);
    }//while(1)
}

/*
 *********************************************************************************************************
 *                                         CALLBACK FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

// rejoin failure callback handler in station mode
sl_status_t join_callback_handler(sl_wifi_event_t event, char *result, uint32_t result_length, void *arg)
{
  UNUSED_PARAMETER(result_length);
  UNUSED_PARAMETER(arg);

  sl_status_t ret = SL_STATUS_OK;

  THREAD_SAFE_PRINT("Join callback\n");
  // In case of event failure, the `SL_WIFI_EVENT_FAIL_INDICATION` bit is set in the `event` parameter.
  // When this bit is set, the `data` parameter will be of type `sl_status_t`, and the `data_length` parameter can be ignored.

    if((event & SL_WIFI_EVENT_FAIL_INDICATION) == SL_WIFI_EVENT_FAIL_INDICATION) {
        THREAD_SAFE_PRINT("Join failed with status: %lX\n", (uint32_t)(*(sl_status_t *)result));

        // update wlan application state
        disconnected = 1;
        connected    = 0;

        ret = SL_STATUS_FAIL;
    }

    wlan_set_dataless_event(WLAN_JOIN_COMPLETE_EVENT);

  return ret;
}

static void show_scan_results(sl_wifi_scan_result_t *result)
{
  uint8_t *bssid = NULL;
  THREAD_SAFE_PRINT("%lu Scan results:\n", result->scan_count);

  if (result->scan_count) {
    THREAD_SAFE_PRINT("\n   %s %24s %s", "SSID", "SECURITY", "NETWORK");
    THREAD_SAFE_PRINT("%12s %12s %s\n", "BSSID", "CHANNEL", "RSSI");

    for (int a = 0; a < (int)result->scan_count; ++a) {
      bssid = (uint8_t *)&result->scan_info[a].bssid;
      THREAD_SAFE_PRINT("%-24s %4u,  %4u, ",
                result->scan_info[a].ssid,
                result->scan_info[a].security_mode,
                result->scan_info[a].network_type);
      THREAD_SAFE_PRINT("  %02x:%02x:%02x:%02x:%02x:%02x, %4u,  -%u\n",
                bssid[0],
                bssid[1],
                bssid[2],
                bssid[3],
                bssid[4],
                bssid[5],
                result->scan_info[a].rf_channel,
                result->scan_info[a].rssi_val);
    }
  }
}

sl_status_t wlan_app_scan_callback_handler( sl_wifi_event_t event,
                                            sl_wifi_scan_result_t *result,
                                            uint32_t result_length,
                                            void *arg)
{
    UNUSED_PARAMETER(arg);

    if (SL_WIFI_CHECK_IF_EVENT_FAILED(event)) {
        return SL_STATUS_FAIL;
    }

    show_scan_results(result);
    wlan_set_event(WLAN_SCAN_COMPLETE_EVENT, result, result_length);
    return SL_STATUS_OK;
}

sl_status_t wlan_net_event_handler(sl_net_event_t event,
    sl_status_t status,
    void *data,
    uint32_t data_length)
{
    UNUSED_PARAMETER(event);
    UNUSED_PARAMETER(status);
    UNUSED_PARAMETER(data); 
    UNUSED_PARAMETER(data_length);

    THREAD_SAFE_PRINT("wlan_net_event_handler\n");

    return SL_STATUS_OK;
}

/*
 *********************************************************************************************************
 *                                   APP CALLBACK FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

SL_WEAK sl_status_t wlan_on_event(wlan_event_msg_t* event)
{

  UNUSED_PARAMETER(event);
  return SL_STATUS_OK;
}



#include <string.h>
#include <stdbool.h>

#include "ble_task.h"
#include "ble_task_config.h"
#include "ble_gatt.h"

#include "nwp_task_config.h"
#include "nwp_task.h"

#include "cmsis_os2.h"
#include "sl_status.h"

// Local utilities
#include "thread_safe_print.h"

#include "ble_config.h" // From WiseConnect template

#include "sl_si91x_ble.h"
#include "sl_constants.h"
#include "rsi_ble_apis.h"

/*
 *********************************************************************************************************
 *                                         LOCAL GLOBAL VARIABLES
 *********************************************************************************************************
 */

// RTOS Variables
const osThreadAttr_t ble_thread_attributes = {
    .name       = "ble_thread",
    .attr_bits  = 0,
    .cb_mem     = 0,
    .cb_size    = 0,
    .stack_mem  = 0,
    .stack_size = 3072,
    .priority   = osPriorityHigh,
    .tz_module  = 0,
    .reserved   = 0,
  };

  osMessageQueueId_t  ble_evt_queue_id;  // Event flags ID
  static uint8_t      ble_evt_queue_seq_num_g = 0;

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void ble_task(void *argument);
static void ble_wait_event(ble_event_msg_t *event_msg);

/*
 *********************************************************************************************************
 *                                         PUBLIC FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
sl_status_t start_ble_task_context(void)
{
    sl_status_t ret = SL_STATUS_OK;

    THREAD_SAFE_PRINT("BLE Task Context Init Start\n");

    ble_evt_queue_id = osMessageQueueNew(  SL_BLE_EVENT_QUEUE_SIZE, 
                                            sizeof(ble_event_msg_t), 
                                            NULL);  // Create message queue
    if (ble_evt_queue_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create wlan_evt_queue_id\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("BLE Queue Creation Complete\n");

    osThreadId_t ble_thread_id = osThreadNew((osThreadFunc_t)ble_task, NULL, &ble_thread_attributes);
    if (ble_thread_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create ble_task\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("BLE Task Startup Complete\n");

    THREAD_SAFE_PRINT("BLE Task Context Init Done\n\n");
    return ret;
}

void ble_set_event(uint32_t event_id, void *event_data, uint32_t event_data_len)
{
    ble_event_msg_t event_msg;

    event_msg.event_id = event_id;
    event_msg.seq_num = ble_evt_queue_seq_num_g;

    if(     (event_data != NULL)
        &&  (event_data_len > 0))
    {
        memcpy(event_msg.payload, event_data, event_data_len);
    } else {
        memset(event_msg.payload, 0, SL_BLE_EVENT_MAX_PAYLOAD_SIZE);
    }

    osStatus_t status = osMessageQueuePut(  ble_evt_queue_id, 
                                            &event_msg, 
                                            0, // Message priority
                                            0); // Timeout - Return immediately

    if (status != osOK) {
        THREAD_SAFE_PRINT("Failed to send ble event: 0x%lx\r\n", (uint32_t)status);
    } else {
        ble_evt_queue_seq_num_g++;
    }
}

static void ble_wait_event(ble_event_msg_t *event_msg)
{
    osStatus_t status = osMessageQueueGet(  ble_evt_queue_id, 
                                            event_msg, 
                                            NULL, 
                                            osWaitForever);

    if(status != osOK) {
        THREAD_SAFE_PRINT("Failed to get ble event: 0x%lx\r\n", (uint32_t)status);
    }
}

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
void ble_task(void *argument)
{
    UNUSED_PARAMETER(argument);

    sl_status_t status                 = SL_STATUS_OK;
    int rsi_ble_status                 = RSI_SUCCESS;
    ble_event_msg_t ble_event_msg;

    status = nwp_access_request();
    THREAD_SAFE_PRINT("BLE Acquiring NWP Semaphore\r\n");
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to acquire NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    //Initialize 917 GATT and BLE part
    rsi_ble_configurator_init();

    // NWP related init should go here 
    THREAD_SAFE_PRINT("BLE Releasing NWP Semaphore\r\n");
    status = nwp_access_release();
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    // Initialize the message queue sequence number
    ble_evt_queue_seq_num_g = 0;

    //Consider the WLAN task as initialized
    ble_set_dataless_event(BLE_SYSTEM_BOOT_EVENT);

    while (true)
    {
        ble_wait_event(&ble_event_msg);

        switch (ble_event_msg.event_id) {
            case BLE_SYSTEM_BOOT_EVENT :
                THREAD_SAFE_PRINT("BLE Boot\n");
                // set device in advertising mode.
                rsi_ble_start_advertising();
                THREAD_SAFE_PRINT("\r\nBLE Advertising Started...\r\n");
            break;

            case BLE_CONNECTION_OPENED_EVENT: {
                THREAD_SAFE_PRINT("BLE Connection Opened\n");
            } break;
            
            case BLE_CONNECTION_CLOSED_EVENT: {
                THREAD_SAFE_PRINT("BLE Connection Closed\n");
                rsi_ble_status = rsi_ble_start_advertising();
                if (status == RSI_SUCCESS) {
                    THREAD_SAFE_PRINT("\r\nStarted Advertising \n");
                }
            } break;

            // case RSI_APP_FW_VERSION: {
            // sl_wifi_firmware_version_t firmware_version = { 0 };
    
            // rsi_ble_app_clear_event(RSI_APP_FW_VERSION);
            // memset(data, 0, RSI_BLE_MAX_DATA_LEN);
    
            // status = sl_wifi_get_firmware_version(&firmware_version);
            // if (status == SL_STATUS_OK) {
            //     data[0] = 0x08;
            //     data[1] = sizeof(sl_wifi_firmware_version_t);
            //     memcpy(&data[2], &firmware_version, sizeof(sl_wifi_firmware_version_t));
    
            //     rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            //     print_firmware_version(&firmware_version);
            // }
            // } break;
    
            // // Connected SSID name (response to '7' command if connection is already established)
            // case RSI_WLAN_ALREADY: {
            // rsi_ble_app_clear_event(RSI_WLAN_ALREADY);
    
            // memset(data, 0, RSI_BLE_MAX_DATA_LEN);
    
            // data[1] = connected; /*This index will indicate wlan AP connect or disconnect status to Android app*/
            // data[0] = 0x07;
            // rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            // } break;
    
            // // NO WLAN connection (response to '7' command if connection is there already)
            // case RSI_WLAN_NOT_ALREADY: {
            // rsi_ble_app_clear_event(RSI_WLAN_NOT_ALREADY);
            // memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            // data[0] = 0x07;
            // data[1] = 0x00;
            // rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            // } break;
    
            // case RSI_BLE_WLAN_DISCONN_NOTIFY: {
            // rsi_ble_app_clear_event(RSI_BLE_WLAN_DISCONN_NOTIFY);
            // memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            // data[1] = 0x01;
            // data[0] = 0x04;
            // rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            // } break;
    
            // case RSI_BLE_WLAN_TIMEOUT_NOTIFY: {
            // rsi_ble_app_clear_event(RSI_BLE_WLAN_TIMEOUT_NOTIFY);
            // memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            // data[0] = 0x02;
            // data[1] = 0x00;
            // rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            // } break;
    
            // case RSI_BLE_WLAN_DISCONNECT_STATUS: {
            // rsi_ble_app_clear_event(RSI_BLE_WLAN_DISCONNECT_STATUS);
            // memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            // data[0] = 0x01;
            // rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            // } break;
    
            // case RSI_SSID: {
            // rsi_ble_app_clear_event(RSI_SSID);
            // } break;
    
            // case RSI_SECTYPE: {
            // rsi_ble_app_clear_event(RSI_SECTYPE);
            // if (sec_type == 0) {
            //     //wifi_app_set_event(WIFI_APP_JOIN_STATE);
            // }
            // } break;
    
            // // Scan results from device (response to '3' command)
            // case RSI_BLE_WLAN_SCAN_RESP: //Send the SSID data to mobile ble application WYZBEE CONFIGURATOR
            // {
            // rsi_ble_app_clear_event(RSI_BLE_WLAN_SCAN_RESP); // clear the served event
    
            // memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            // data[0] = 0x03;
            // data[1] = scanresult->scan_count;
            // rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
    
            // for (scan_ix = 0; scan_ix < scanresult->scan_count; scan_ix++) {
            //     memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            //     data[0] = scanresult->scan_info[scan_ix].security_mode;
            //     data[1] = ',';
            //     strcpy((char *)data + 2, (const char *)scanresult->scan_info[scan_ix].ssid);
            //     length = strlen((char *)data + 2);
            //     length = length + 2;
    
            //     rsi_ble_set_local_att_value(rsi_ble_att3_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            //     osDelay(10);
            // }
    
            // LOG_PRINT("Displayed scan list in Silabs app\n\n");
            // } break;
    
            // // WLAN connection response status (response to '2' command)
            // case RSI_BLE_WLAN_JOIN_STATUS: //Send the connected status to mobile ble application WYZBEE CONFIGURATOR
            // {
            // sl_mac_address_t mac_addr = { 0 };
    
            // sl_ip_address_t ip = { 0 };
            // ip.type            = ip_address.type;
            // ip.ip.v4.value     = ip_address.ip.v4.ip_address.value;
    
            // // clear the served event
            // rsi_ble_app_clear_event(RSI_BLE_WLAN_JOIN_STATUS);
    
            // memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            // data[0] = 0x02;
            // data[1] = 0x01;
            // data[2] = ',';
    
            // // Copy the MAC address
            // status = sl_wifi_get_mac_address(SL_WIFI_CLIENT_INTERFACE, &mac_addr);
            // if (status == SL_STATUS_OK) {
            //     for (k = 0; k < 6; k++) {
            //     data[k + 3] = mac_addr.octet[k];
            //     }
            // } else {
            //     k = 6;
            // }
            // data[k + 3] = ',';
    
            // // IP Address
            // for (int i = 0; k < 10; k++, i++) {
            //     data[k + 4] = ip.ip.v4.bytes[i];
            // }
    
            // rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl,
            //                             RSI_BLE_MAX_DATA_LEN,
            //                             data); // set the local attribute value.
            // LOG_PRINT("AP joined successfully\n\n");
            // } break;
            case BLE_CONNECTION_REMOTE_FEATURES_EVENT: {

            } break;

            case BLE_SYSTEM_EXTERNAL_SIGNAL_EVENT: {
//                switch (msg_type) {// Check for WIFI SIGNAL ID
//                    case WIFI_APP_SCAN_RESP:
//                      rsi_ble_app_set_event(RSI_BLE_WLAN_SCAN_RESP);
//                      break;
//                    case WIFI_APP_CONNECTION_STATUS:
//                      rsi_ble_app_set_event(RSI_BLE_WLAN_JOIN_STATUS);
//                      break;
//                    case WIFI_APP_DISCONNECTION_STATUS:
//                      rsi_ble_app_set_event(RSI_BLE_WLAN_DISCONNECT_STATUS);
//                      break;
//                    case WIFI_APP_DISCONNECTION_NOTIFY:
//                      rsi_ble_app_set_event(RSI_BLE_WLAN_DISCONN_NOTIFY);
//                      break;
//                    case WIFI_APP_TIMEOUT_NOTIFY:
//                      rsi_ble_app_set_event(RSI_BLE_WLAN_TIMEOUT_NOTIFY);
//                      break;
//                    default:
//                      break;
//                  }
            } break;
            default:
            break;
        }//switch(event_id)
    }//while(1)
}

/*
 *********************************************************************************************************
 *                                         CALLBACK FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */



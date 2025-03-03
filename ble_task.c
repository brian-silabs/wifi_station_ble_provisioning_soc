
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

  static rsi_ble_event_remote_features_t remote_dev_feature;
  static rsi_ble_event_conn_status_t conn_event_to_app;
  static rsi_ble_event_conn_status_t conn_event_to_app;
  //static rsi_ble_event_disconnect_t disconn_event_to_app;
  static rsi_ble_event_data_length_update_t updated_data_len_params;

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void ble_task(void *argument);

static void ble_wait_event(ble_event_msg_t *event_msg);
static void rsi_ble_configurator_init(void);

static void rsi_ble_on_connect_event(rsi_ble_event_conn_status_t *resp_conn);
static void rsi_ble_on_disconnect_event(rsi_ble_event_disconnect_t *resp_disconnect, uint16_t reason);
static void rsi_ble_data_length_change_event(rsi_ble_event_data_length_update_t *rsi_ble_data_length_update);
static void rsi_ble_on_remote_features_event(rsi_ble_event_remote_features_t *rsi_ble_event_remote_features);
static void rsi_ble_on_enhance_conn_status_event(rsi_ble_event_enhance_conn_status_t *resp_enh_conn);
static void rsi_ble_on_disconnect_event(rsi_ble_event_disconnect_t *resp_disconnect, uint16_t reason);
static void rsi_ble_on_conn_update_complete_event(rsi_ble_event_conn_update_t *rsi_ble_event_conn_update_complete,
    uint16_t resp_status);

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

static void rsi_ble_configurator_init(void)
{
  // registering the GAP callback functions
  rsi_ble_gap_register_callbacks(   NULL,
                                    rsi_ble_on_connect_event,
                                    rsi_ble_on_disconnect_event,
                                    NULL,
                                    NULL,
                                    rsi_ble_data_length_change_event,
                                    rsi_ble_on_enhance_conn_status_event,
                                    NULL,
                                    rsi_ble_on_conn_update_complete_event,
                                    NULL);

    //! registering the GAP extended call back functions
    rsi_ble_gap_extended_register_callbacks(rsi_ble_on_remote_features_event, NULL);
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

    //Initialize 917 BLE part
    rsi_ble_configurator_init();

    //Initialize the GATT part
    rsi_gatt_configurator_init();

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
                  //MTU exchange
                  rsi_ble_status = rsi_ble_mtu_exchange_event(conn_event_to_app.dev_addr, BLE_MTU_SIZE);
                    if (rsi_ble_status != RSI_SUCCESS) {
                        THREAD_SAFE_PRINT("\n MTU request failed with error code %d", rsi_ble_status);
                    }
                    rsi_ble_status = rsi_ble_conn_params_update(conn_event_to_app.dev_addr,
                                                        CONN_INTERVAL_DEFAULT_MIN,
                                                        CONN_INTERVAL_DEFAULT_MAX,
                                                        CONNECTION_LATENCY,
                                                        SUPERVISION_TIMEOUT);
                    if (rsi_ble_status != RSI_SUCCESS) {
                        THREAD_SAFE_PRINT("\n rsi_ble_conn_params_update command failed : %d", rsi_ble_status);
                    }
            } break;
            
            case BLE_CONNECTION_CLOSED_EVENT: {
                THREAD_SAFE_PRINT("BLE Connection Closed\n");
                rsi_ble_status = rsi_ble_start_advertising();
                if (rsi_ble_status == RSI_SUCCESS) {
                    THREAD_SAFE_PRINT("\r\nStarted Advertising \n");
                }
            } break;

            case BLE_GATT_DATALEN_CHANGE_EVENT: {
                THREAD_SAFE_PRINT("Data Length changed\n");
            } break;

            case BLE_GATT_WRITE_REQUEST_EVENT: {
                THREAD_SAFE_PRINT("Gatt Write Request\n");
            } break;

            case BLE_CONNECTION_REMOTE_FEATURES_EVENT: {
                THREAD_SAFE_PRINT("BLE_CONNECTION_REMOTE_FEATURES_EVENT\n");
                if (remote_dev_feature.remote_features[0] & 0x20) {
                    int bt_status = rsi_ble_set_data_len(conn_event_to_app.dev_addr, TX_LEN, TX_TIME);
                    if (bt_status != RSI_SUCCESS) {
                      THREAD_SAFE_PRINT("\n set data length cmd failed with error code = "
                                "%d \n",
                                bt_status);
                    }
                  }
            } break;

            case BLE_SYSTEM_EXTERNAL_SIGNAL_EVENT: {
                THREAD_SAFE_PRINT("BLE_SYSTEM_EXTERNAL_SIGNAL_EVENT\n");
            } break;
            default:
            break;
        }//switch(event_id)

        bt_on_event(&ble_event_msg);

    }//while(1)
}

/*
 *********************************************************************************************************
 *                                         CALLBACK FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */


 /**
 * @fn         rsi_ble_on_enhance_conn_status_event
 * @brief      invoked when enhanced connection complete event is received
 * @param[out] resp_enh_conn, connected remote device information
 * @return     none.
 * @section description
 * This callback function indicates the status of the connection
 */
static void rsi_ble_on_enhance_conn_status_event(rsi_ble_event_enhance_conn_status_t *resp_enh_conn)
{
  conn_event_to_app.dev_addr_type = resp_enh_conn->dev_addr_type;
  memcpy(conn_event_to_app.dev_addr, resp_enh_conn->dev_addr, RSI_DEV_ADDR_LEN);
  conn_event_to_app.status = resp_enh_conn->status;
  ble_set_event(BLE_CONNECTION_OPENED_EVENT, NULL, 0);
}

/*==============================================*/
/**
 * @fn         rsi_ble_on_connect_event
 * @brief      invoked when connection complete event is received
 * @param[out] resp_conn, connected remote device information
 * @return     none.
 * @section description
 * This callback function indicates the status of the connection
 */
static void rsi_ble_on_connect_event(rsi_ble_event_conn_status_t *resp_conn)
{
  memcpy(&conn_event_to_app, resp_conn, sizeof(rsi_ble_event_conn_status_t));
  ble_set_event(BLE_CONNECTION_OPENED_EVENT, NULL, 0);
}

/*==============================================*/
/**
 * @fn         rsi_ble_on_conn_update_complete_event
 * @brief      invoked when conn update complete event is received
 * @param[out] rsi_ble_event_conn_update_complete contains the controller
 * support conn information.
 * @param[out] resp_status contains the response status (Success or Error code)
 * @return     none.
 * @section description
 * This Callback function indicates the conn update complete event is received
 */
static void rsi_ble_on_conn_update_complete_event(rsi_ble_event_conn_update_t *rsi_ble_event_conn_update_complete,
    uint16_t resp_status)
{
    UNUSED_PARAMETER(rsi_ble_event_conn_update_complete);
    UNUSED_PARAMETER(resp_status);
    ble_set_event(BLE_CONNECTION_UPDATE_EVENT, NULL, 0);
}

/*==============================================*/
/**
 * @fn         rsi_ble_on_disconnect_event
 * @brief      invoked when disconnection event is received
 * @param[out]  resp_disconnect, disconnected remote device information
 * @param[out]  reason, reason for disconnection.
 * @return     none.
 * @section description
 * This Callback function indicates disconnected device information and status
 */
static void rsi_ble_on_disconnect_event(rsi_ble_event_disconnect_t *resp_disconnect, uint16_t reason)
{
    UNUSED_PARAMETER(reason);
    UNUSED_PARAMETER(resp_disconnect);
    ble_set_event(BLE_CONNECTION_CLOSED_EVENT, NULL, 0);
}

/*============================================================================*/
/**
 * @fn         rsi_ble_on_remote_features_event
 * @brief      invoked when LE remote features event is received.
 * @param[out] rsi_ble_event_remote_features, connected remote device information
 * @return     none.
 * @section description
 * This callback function indicates the remote device features
 */
static void rsi_ble_on_remote_features_event(rsi_ble_event_remote_features_t *rsi_ble_event_remote_features)
{
  memcpy(&remote_dev_feature, rsi_ble_event_remote_features, sizeof(rsi_ble_event_remote_features_t));
  ble_set_event(BLE_CONNECTION_REMOTE_FEATURES_EVENT, NULL, 0);
}

/*============================================================================*/
/**
 * @fn         rsi_ble_data_length_change_event
 * @brief      invoked when data length is set
 * @param[out] rsi_ble_data_length_update, data length information
 * @section description
 * This Callback function indicates data length is set
 */
static void rsi_ble_data_length_change_event(rsi_ble_event_data_length_update_t *rsi_ble_data_length_update)
{
  memcpy(&updated_data_len_params, rsi_ble_data_length_update, sizeof(rsi_ble_event_data_length_update_t));
  ble_set_event(BLE_GATT_DATALEN_CHANGE_EVENT, NULL, 0);
}

/*
 *********************************************************************************************************
 *                                   APP CALLBACK FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

SL_WEAK sl_status_t bt_on_event(ble_event_msg_t* event)
{

  UNUSED_PARAMETER(event);
  return SL_STATUS_OK;
}

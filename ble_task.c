
#include <string.h>
#include <stdbool.h>

#include "ble_task.h"
#include "ble_task_config.h"

#include "nwp_task_config.h"
#include "nwp_task.h"

#include "cmsis_os2.h"
#include "sl_status.h"

// Local utilities
#include "thread_safe_print.h"

#include "ble_config.h" // From WiseConnect template

#include "sl_si91x_ble.h"
#include "sl_constants.h"

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

// BLE Variables
uint8_t remote_dev_addr[18] = { 0 };
static rsi_ble_event_mtu_t app_ble_mtu_event;
static rsi_ble_event_conn_update_t event_conn_update_complete;
static rsi_ble_event_remote_features_t remote_dev_feature;
static rsi_ble_event_data_length_update_t updated_data_len_params;

uint8_t data[20] = { 0 };
static volatile uint32_t ble_app_event_map;
static rsi_ble_event_conn_status_t conn_event_to_app;
static rsi_ble_event_disconnect_t disconn_event_to_app;
static uint8_t rsi_ble_att1_val_hndl;
static uint16_t rsi_ble_att2_val_hndl;
static uint16_t rsi_ble_att3_val_hndl;

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void ble_task(void *argument);
static void ble_wait_event(ble_event_msg_t *event_msg);
static void rsi_ble_configurator_init(void);


// Callabacks
void rsi_ble_on_enhance_conn_status_event(rsi_ble_event_enhance_conn_status_t *resp_enh_conn);

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
    ble_event_msg_t ble_event_msg;

    status = nwp_access_request();
    THREAD_SAFE_PRINT("BLE Acquiring NWP Semaphore\r\n");
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to acquire NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    // NWP related init should go here 
    THREAD_SAFE_PRINT("BLE Releasing NWP Semaphore\r\n");
    status = nwp_access_release();
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    // Initialize the message queue sequence number
    ble_evt_queue_seq_num_g = 0;

    while (true)
    {
        ble_wait_event(&ble_event_msg);

        switch (ble_event_msg.event_id) {
            case RSI_BLE_ENH_CONN_EVENT: {
                // event invokes when connection was completed
        
                // clear the served event
                rsi_ble_app_clear_event(RSI_BLE_ENH_CONN_EVENT);
        
                //MTU exchange
                status = rsi_ble_mtu_exchange_event(conn_event_to_app.dev_addr, BLE_MTU_SIZE);
                if (status != RSI_SUCCESS) {
                  LOG_PRINT("\n MTU request failed with error code %lx", status);
                }
                status = rsi_ble_conn_params_update(conn_event_to_app.dev_addr,
                                                    CONN_INTERVAL_DEFAULT_MIN,
                                                    CONN_INTERVAL_DEFAULT_MAX,
                                                    CONNECTION_LATENCY,
                                                    SUPERVISION_TIMEOUT);
                if (status != RSI_SUCCESS) {
                  LOG_PRINT("\n rsi_ble_conn_params_update command failed : %lx", status);
                }
            } break;
    
            case RSI_BLE_DISCONN_EVENT: {
            // event invokes when disconnection was completed
    
            // clear the served event
            rsi_ble_app_clear_event(RSI_BLE_DISCONN_EVENT);
            LOG_PRINT("\r\nDisconnected - remote_dev_addr : %s\r\n",
                        rsi_6byte_dev_address_to_ascii(remote_dev_addr, disconn_event_to_app.dev_addr));
    
            // set device in advertising mode.
    adv:
            status = rsi_ble_start_advertising();
            if (status != RSI_SUCCESS) {
                goto adv;
            } else {
                LOG_PRINT("\r\nStarted Advertising \n");
            }
            } break;
            case RSI_APP_FW_VERSION: {
            sl_wifi_firmware_version_t firmware_version = { 0 };
    
            rsi_ble_app_clear_event(RSI_APP_FW_VERSION);
            memset(data, 0, RSI_BLE_MAX_DATA_LEN);
    
            status = sl_wifi_get_firmware_version(&firmware_version);
            if (status == SL_STATUS_OK) {
                data[0] = 0x08;
                data[1] = sizeof(sl_wifi_firmware_version_t);
                memcpy(&data[2], &firmware_version, sizeof(sl_wifi_firmware_version_t));
    
                rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
                print_firmware_version(&firmware_version);
            }
            } break;
    
            // Connected SSID name (response to '7' command if connection is already established)
            case RSI_WLAN_ALREADY: {
            rsi_ble_app_clear_event(RSI_WLAN_ALREADY);
    
            memset(data, 0, RSI_BLE_MAX_DATA_LEN);
    
            data[1] = connected; /*This index will indicate wlan AP connect or disconnect status to Android app*/
            data[0] = 0x07;
            rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            } break;
    
            // NO WLAN connection (response to '7' command if connection is there already)
            case RSI_WLAN_NOT_ALREADY: {
            rsi_ble_app_clear_event(RSI_WLAN_NOT_ALREADY);
            memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            data[0] = 0x07;
            data[1] = 0x00;
            rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            } break;
    
            case RSI_BLE_WLAN_DISCONN_NOTIFY: {
            rsi_ble_app_clear_event(RSI_BLE_WLAN_DISCONN_NOTIFY);
            memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            data[1] = 0x01;
            data[0] = 0x04;
            rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            } break;
    
            case RSI_BLE_WLAN_TIMEOUT_NOTIFY: {
            rsi_ble_app_clear_event(RSI_BLE_WLAN_TIMEOUT_NOTIFY);
            memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            data[0] = 0x02;
            data[1] = 0x00;
            rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            } break;
    
            case RSI_BLE_WLAN_DISCONNECT_STATUS: {
            rsi_ble_app_clear_event(RSI_BLE_WLAN_DISCONNECT_STATUS);
            memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            data[0] = 0x01;
            rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
            } break;
    
            case RSI_SSID: {
            rsi_ble_app_clear_event(RSI_SSID);
            } break;
    
            case RSI_SECTYPE: {
            rsi_ble_app_clear_event(RSI_SECTYPE);
            if (sec_type == 0) {
                //wifi_app_set_event(WIFI_APP_JOIN_STATE);
            }
            } break;
    
            // Scan results from device (response to '3' command)
            case RSI_BLE_WLAN_SCAN_RESP: //Send the SSID data to mobile ble application WYZBEE CONFIGURATOR
            {
            rsi_ble_app_clear_event(RSI_BLE_WLAN_SCAN_RESP); // clear the served event
    
            memset(data, 0, RSI_BLE_MAX_DATA_LEN);
            data[0] = 0x03;
            data[1] = scanresult->scan_count;
            rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
    
            for (scan_ix = 0; scan_ix < scanresult->scan_count; scan_ix++) {
                memset(data, 0, RSI_BLE_MAX_DATA_LEN);
                data[0] = scanresult->scan_info[scan_ix].security_mode;
                data[1] = ',';
                strcpy((char *)data + 2, (const char *)scanresult->scan_info[scan_ix].ssid);
                length = strlen((char *)data + 2);
                length = length + 2;
    
                rsi_ble_set_local_att_value(rsi_ble_att3_val_hndl, RSI_BLE_MAX_DATA_LEN, data);
                osDelay(10);
            }
    
            LOG_PRINT("Displayed scan list in Silabs app\n\n");
            } break;
    
            // WLAN connection response status (response to '2' command)
            case RSI_BLE_WLAN_JOIN_STATUS: //Send the connected status to mobile ble application WYZBEE CONFIGURATOR
            {
            sl_mac_address_t mac_addr = { 0 };
    
            sl_ip_address_t ip = { 0 };
            ip.type            = ip_address.type;
            ip.ip.v4.value     = ip_address.ip.v4.ip_address.value;
    
            // clear the served event
            rsi_ble_app_clear_event(RSI_BLE_WLAN_JOIN_STATUS);
    
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
                data[k + 4] = ip.ip.v4.bytes[i];
            }
    
            rsi_ble_set_local_att_value(rsi_ble_att2_val_hndl,
                                        RSI_BLE_MAX_DATA_LEN,
                                        data); // set the local attribute value.
            LOG_PRINT("AP joined successfully\n\n");
            } break;
            case RSI_BLE_MTU_EVENT: {
            //! clear the served event
            rsi_ble_app_clear_event(RSI_BLE_MTU_EVENT);
            //! event invokes when write/notification events received
            } break;
            case RSI_BLE_CONN_UPDATE_EVENT: {
            rsi_ble_app_clear_event(RSI_BLE_CONN_UPDATE_EVENT);
    
            } break;
            case RSI_BLE_RECEIVE_REMOTE_FEATURES: {
            //! clear the served event
            rsi_ble_app_clear_event(RSI_BLE_RECEIVE_REMOTE_FEATURES);
    
            if (remote_dev_feature.remote_features[0] & 0x20) {
                status = rsi_ble_set_data_len(conn_event_to_app.dev_addr, TX_LEN, TX_TIME);
                if (status != RSI_SUCCESS) {
                LOG_PRINT("\n set data length cmd failed with error code = "
                            "%lx \n",
                            status);
                rsi_ble_app_set_event(RSI_BLE_RECEIVE_REMOTE_FEATURES);
                }
            }
    
            } break;
            case RSI_BLE_DATA_LENGTH_CHANGE: {
            //! clear the served event
            rsi_ble_app_clear_event(RSI_BLE_DATA_LENGTH_CHANGE);
            } break;

            case EXTERNAL_SIGNAL_EVENT: {
                switch (msg_type) {// Check for WIFI SIGNAL ID 
                    case WIFI_APP_SCAN_RESP:
                      rsi_ble_app_set_event(RSI_BLE_WLAN_SCAN_RESP);
                      break;
                    case WIFI_APP_CONNECTION_STATUS:
                      rsi_ble_app_set_event(RSI_BLE_WLAN_JOIN_STATUS);
                      break;
                    case WIFI_APP_DISCONNECTION_STATUS:
                      rsi_ble_app_set_event(RSI_BLE_WLAN_DISCONNECT_STATUS);
                      break;
                    case WIFI_APP_DISCONNECTION_NOTIFY:
                      rsi_ble_app_set_event(RSI_BLE_WLAN_DISCONN_NOTIFY);
                      break;
                    case WIFI_APP_TIMEOUT_NOTIFY:
                      rsi_ble_app_set_event(RSI_BLE_WLAN_TIMEOUT_NOTIFY);
                      break;
                    default:
                      break;
                  }
            } break
            default:
            break;
        }//switch(event_id)
    }//while(1)
}

//TODO Split GATT Config and app config (adv)

/**
 * @fn         rsi_ble_app_init
 * @brief      initialize the BLE module.
 * @param[in]  none
 * @return     none.
 * @section description
 * This function is used to initialize the BLE module
 */
static void rsi_ble_configurator_init(void)
{
  uint8_t adv[31] = { 2, 1, 6 };

  //  initializing the application events map
  rsi_ble_app_init_events();

  rsi_ble_add_configurator_serv(); // adding simple BLE chat service

  // registering the GAP callback functions
  rsi_ble_gap_register_callbacks(NULL,
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

  // registering the GATT callback functions
  rsi_ble_gatt_register_callbacks(NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  rsi_ble_on_gatt_write_event,
                                  NULL,
                                  NULL,
                                  NULL,
                                  rsi_ble_on_mtu_event,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL);

  // Set local name
  rsi_bt_set_local_name((uint8_t *)RSI_BLE_APP_DEVICE_NAME);

  // prepare advertise data //local/device name
  adv[3] = strlen(RSI_BLE_APP_DEVICE_NAME) + 1;
  adv[4] = 9;
  strcpy((char *)&adv[5], RSI_BLE_APP_DEVICE_NAME);

  // set advertise data
  rsi_ble_set_advertise_data(adv, strlen(RSI_BLE_APP_DEVICE_NAME) + 5);

  // set device in advertising mode.
  rsi_ble_start_advertising();
  LOG_PRINT("\r\nBLE Advertising Started...\r\n");

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
void rsi_ble_on_enhance_conn_status_event(rsi_ble_event_enhance_conn_status_t *resp_enh_conn)
{
  conn_event_to_app.dev_addr_type = resp_enh_conn->dev_addr_type;
  memcpy(conn_event_to_app.dev_addr, resp_enh_conn->dev_addr, RSI_DEV_ADDR_LEN);

  conn_event_to_app.status = resp_enh_conn->status;
  rsi_ble_app_set_event(RSI_BLE_ENH_CONN_EVENT);
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

  rsi_ble_app_set_event(RSI_BLE_ENH_CONN_EVENT);
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
  memcpy(&disconn_event_to_app, resp_disconnect, sizeof(rsi_ble_event_disconnect_t));
  rsi_ble_app_set_event(RSI_BLE_DISCONN_EVENT);
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
void rsi_ble_on_conn_update_complete_event(rsi_ble_event_conn_update_t *rsi_ble_event_conn_update_complete,
                                           uint16_t resp_status)
{
  UNUSED_PARAMETER(resp_status);
  rsi_6byte_dev_address_to_ascii(remote_dev_addr, (uint8_t *)rsi_ble_event_conn_update_complete->dev_addr);
  memcpy(&event_conn_update_complete, rsi_ble_event_conn_update_complete, sizeof(rsi_ble_event_conn_update_t));
  rsi_ble_app_set_event(RSI_BLE_CONN_UPDATE_EVENT);
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
void rsi_ble_on_remote_features_event(rsi_ble_event_remote_features_t *rsi_ble_event_remote_features)
{
  memcpy(&remote_dev_feature, rsi_ble_event_remote_features, sizeof(rsi_ble_event_remote_features_t));
  rsi_ble_app_set_event(RSI_BLE_RECEIVE_REMOTE_FEATURES);
}

/*============================================================================*/
/**
 * @fn         rsi_ble_data_length_change_event
 * @brief      invoked when data length is set
 * @param[out] rsi_ble_data_length_update, data length information
 * @section description
 * This Callback function indicates data length is set
 */
void rsi_ble_data_length_change_event(rsi_ble_event_data_length_update_t *rsi_ble_data_length_update)
{
  memcpy(&updated_data_len_params, rsi_ble_data_length_update, sizeof(rsi_ble_event_data_length_update_t));
  rsi_ble_app_set_event(RSI_BLE_DATA_LENGTH_CHANGE);
}
/*==============================================*/
/**
 * @fn         rsi_ble_on_mtu_event
 * @brief      invoked  when an MTU size event is received
 * @param[out]  rsi_ble_mtu, it indicates MTU size.
 * @return     none.
 * @section description
 * This callback function is invoked  when an MTU size event is received
 */
static void rsi_ble_on_mtu_event(rsi_ble_event_mtu_t *rsi_ble_mtu)
{
  memcpy(&app_ble_mtu_event, rsi_ble_mtu, sizeof(rsi_ble_event_mtu_t));
  rsi_6byte_dev_address_to_ascii(remote_dev_addr, app_ble_mtu_event.dev_addr);
  rsi_ble_app_set_event(RSI_BLE_MTU_EVENT);
}

/*==============================================*/
/**
 * @fn         rsi_ble_on_gatt_write_event
 * @brief      this is call back function, it invokes when write/notify events received.
 * @param[out]  event_id, it indicates write/notification event id.
 * @param[out]  rsi_ble_write, write event parameters.
 * @return     none.
 * @section description
 * This is a callback function
 */
static void rsi_ble_on_gatt_write_event(uint16_t event_id, rsi_ble_event_write_t *rsi_ble_write)
{
  UNUSED_PARAMETER(event_id);
  uint8_t cmdid;

  //  Requests will come from Mobile app
  if ((rsi_ble_att1_val_hndl) == *((uint16_t *)rsi_ble_write->handle)) {
    cmdid = rsi_ble_write->att_value[0];

    switch (cmdid) {
      // Scan command request
      case '3': //else if(rsi_ble_write->att_value[0] == '3')
      {
        LOG_PRINT("Received scan request\n");
        retry = 0;
        memset(data, 0, sizeof(data));
        //wifi_app_set_event(WIFI_APP_SCAN_STATE);
      } break;

      // Sending SSID
      case '2': //else if(rsi_ble_write->att_value[0] == '2')
      {
        memset(coex_ssid, 0, sizeof(coex_ssid));
        strcpy((char *)coex_ssid, (const char *)&rsi_ble_write->att_value[3]);

        rsi_ble_app_set_event(RSI_SSID);
      } break;

      // Sending Security type
      case '5': //else if(rsi_ble_write->att_value[0] == '5')
      {
        sec_type = ((rsi_ble_write->att_value[3]) - '0');
        LOG_PRINT("In Security Request\n");

        rsi_ble_app_set_event(RSI_SECTYPE);
      } break;

      // Sending PSK
      case '6': //else if(rsi_ble_write->att_value[0] == '6')
      {
        memset(data, 0, sizeof(data));
        strcpy((char *)pwd, (const char *)&rsi_ble_write->att_value[3]);
        LOG_PRINT("PWD from ble app\n");
        //wifi_app_set_event(WIFI_APP_JOIN_STATE);
      } break;

      // WLAN Status Request
      case '7': //else if(rsi_ble_write->att_value[0] == '7')
      {
        LOG_PRINT("WLAN status request received\n");
        memset(data, 0, sizeof(data));
        if (connected) {
          rsi_ble_app_set_event(RSI_WLAN_ALREADY);
        } else {
          rsi_ble_app_set_event(RSI_WLAN_NOT_ALREADY);
        }
      } break;

      // WLAN disconnect request
      case '4': //else if(rsi_ble_write->att_value[0] == '4')
      {
        LOG_PRINT("WLAN disconnect request received\n");
        memset(data, 0, sizeof(data));
        //wifi_app_set_event(WIFI_APP_DISCONN_NOTIFY_STATE);
      } break;

      // FW version request
      case '8': {
        memset(data, 0, sizeof(data));
        rsi_ble_app_set_event(RSI_APP_FW_VERSION);
        LOG_PRINT("FW version request\n");
      } break;

      default:
        LOG_PRINT("Default command case \n\n");
        break;
    }
  }
}

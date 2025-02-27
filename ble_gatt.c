
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ble_gatt.h"
#include "ble_config.h"
#include "ble_task.h"
#include "thread_safe_print.h"

#include "sl_constants.h"
#include "sl_si91x_ble.h"
#include "rsi_ble_apis.h"
#include "rsi_utils.h"
#include "rsi_bt_common_apis.h"

// TODO Make this module consume EFR32 Gatt DB .h/.c files

// BLE Variables
uint8_t remote_dev_addr[18] = { 0 };

rsi_ble_event_mtu_t app_ble_mtu_event;

static uint8_t rsi_ble_att1_val_hndl;
static uint16_t rsi_ble_att2_val_hndl;
static uint16_t rsi_ble_att3_val_hndl;

static void rsi_ble_add_char_serv_att(void *serv_handler,
    uint16_t handle,
    uint8_t val_prop,
    uint16_t att_val_handle,
    uuid_t att_val_uuid);

static void rsi_ble_add_char_val_att(void *serv_handler,
    uint16_t handle,
    uuid_t att_type_uuid,
    uint8_t val_prop,
    uint8_t *data,
    uint8_t data_len);

static uint32_t rsi_ble_add_configurator_serv(void);

static void rsi_ble_on_gatt_write_event(uint16_t event_id, rsi_ble_event_write_t *rsi_ble_write);
static void rsi_ble_on_mtu_event(rsi_ble_event_mtu_t *rsi_ble_mtu);

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

/**
 * @fn         rsi_ble_add_char_val_att
 * @brief      this function is used to add characteristic value attribute.
 * @param[in]  serv_handler, new service handler.
 * @param[in]  handle, characteristic value attribute handle.
 * @param[in]  att_type_uuid, attribute uuid value.
 * @param[in]  val_prop, characteristic value property.
 * @param[in]  data, characteristic value data pointer.
 * @param[in]  data_len, characteristic value length.
 * @return     none.
 * @section description
 * This function is used at application to create new service.
 */

 static void rsi_ble_add_char_val_att(void *serv_handler,
    uint16_t handle,
    uuid_t att_type_uuid,
    uint8_t val_prop,
    uint8_t *data,
    uint8_t data_len)
{
rsi_ble_req_add_att_t new_att = { 0 };

// preparing the attributes
new_att.serv_handler = serv_handler;
new_att.handle       = handle;
memcpy(&new_att.att_uuid, &att_type_uuid, sizeof(uuid_t));
new_att.property = val_prop;

// preparing the attribute value
new_att.data_len = RSI_MIN(sizeof(new_att.data), data_len);
memcpy(new_att.data, data, new_att.data_len);

// add attribute to the service
rsi_ble_add_attribute(&new_att);

// check the attribute property with notification
if (val_prop & RSI_BLE_ATT_PROPERTY_NOTIFY) {
// if notification property supports then we need to add client characteristic service.

// preparing the client characteristic attribute & values
memset(&new_att, 0, sizeof(rsi_ble_req_add_att_t));
new_att.serv_handler       = serv_handler;
new_att.handle             = handle + 1;
new_att.att_uuid.size      = 2;
new_att.att_uuid.val.val16 = RSI_BLE_CLIENT_CHAR_UUID;
new_att.property           = RSI_BLE_ATT_PROPERTY_READ | RSI_BLE_ATT_PROPERTY_WRITE;
new_att.data_len           = 2;

// add attribute to the service
rsi_ble_add_attribute(&new_att);
}

return;
}

/**
 * @fn         rsi_ble_add_char_serv_att
 * @brief      this function is used to add characteristic service attribute
 * @param[in]  serv_handler, service handler.
 * @param[in]  handle, characteristic service attribute handle.
 * @param[in]  val_prop, characteristic value property.
 * @param[in]  att_val_handle, characteristic value handle
 * @param[in]  att_val_uuid, characteristic value uuid
 * @return     none.
 * @section description
 * This function is used at application to add characteristic attribute
 */
static void rsi_ble_add_char_serv_att(void *serv_handler,
    uint16_t handle,
    uint8_t val_prop,
    uint16_t att_val_handle,
    uuid_t att_val_uuid)
{
rsi_ble_req_add_att_t new_att = { 0 };

// preparing the attribute service structure
new_att.serv_handler       = serv_handler;
new_att.handle             = handle;
new_att.att_uuid.size      = 2;
new_att.att_uuid.val.val16 = RSI_BLE_CHAR_SERV_UUID;
new_att.property           = RSI_BLE_ATT_PROPERTY_READ;

// preparing the characteristic attribute value
new_att.data_len = 6;
new_att.data[0]  = val_prop;
rsi_uint16_to_2bytes(&new_att.data[2], att_val_handle);
rsi_uint16_to_2bytes(&new_att.data[4], att_val_uuid.val.val16);

// add attribute to the service
rsi_ble_add_attribute(&new_att);

return;
}

/**
 * @fn         rsi_ble_simple_chat_add_new_serv
 * @brief      this function is used to add new service i.e., simple chat service.
 * @param[in]  none.
 * @return     int32_t
 *             0  =  success
 *             !0 = failure
 * @section description
 * This function is used at application to create new service.
 */
static uint32_t rsi_ble_add_configurator_serv(void)
{
  uuid_t new_uuid                       = { 0 };
  rsi_ble_resp_add_serv_t new_serv_resp = { 0 };
  uint8_t data[RSI_BLE_MAX_DATA_LEN]    = { 0 };

  new_uuid.size      = 2; // adding new service
  new_uuid.val.val16 = RSI_BLE_NEW_SERVICE_UUID;

  rsi_ble_add_service(new_uuid, &new_serv_resp);

  new_uuid.size      = 2; // adding characteristic service attribute to the service
  new_uuid.val.val16 = RSI_BLE_ATTRIBUTE_1_UUID;
  rsi_ble_add_char_serv_att(new_serv_resp.serv_handler,
                            new_serv_resp.start_handle + 1,
                            RSI_BLE_ATT_PROPERTY_WRITE,
                            new_serv_resp.start_handle + 2,
                            new_uuid);

  rsi_ble_att1_val_hndl = new_serv_resp.start_handle + 2; // adding characteristic value attribute to the service
  new_uuid.size         = 2;
  new_uuid.val.val16    = RSI_BLE_ATTRIBUTE_1_UUID;
  rsi_ble_add_char_val_att(new_serv_resp.serv_handler,
                           new_serv_resp.start_handle + 2,
                           new_uuid,
                           RSI_BLE_ATT_PROPERTY_WRITE,
                           data,
                           RSI_BLE_MAX_DATA_LEN);

  new_uuid.size      = 2; // adding characteristic service attribute to the service
  new_uuid.val.val16 = RSI_BLE_ATTRIBUTE_2_UUID;
  rsi_ble_add_char_serv_att(new_serv_resp.serv_handler,
                            new_serv_resp.start_handle + 3,
                            RSI_BLE_ATT_PROPERTY_READ | RSI_BLE_ATT_PROPERTY_WRITE,
                            new_serv_resp.start_handle + 4,
                            new_uuid);

  rsi_ble_att2_val_hndl = new_serv_resp.start_handle + 4; // adding characteristic value attribute to the service
  new_uuid.size         = 2;
  new_uuid.val.val16    = RSI_BLE_ATTRIBUTE_2_UUID;
  rsi_ble_add_char_val_att(new_serv_resp.serv_handler,
                           new_serv_resp.start_handle + 4,
                           new_uuid,
                           RSI_BLE_ATT_PROPERTY_READ | RSI_BLE_ATT_PROPERTY_WRITE,
                           data,
                           RSI_BLE_MAX_DATA_LEN);

  new_uuid.size      = 2; // adding characteristic service attribute to the service
  new_uuid.val.val16 = RSI_BLE_ATTRIBUTE_3_UUID;
  rsi_ble_add_char_serv_att(new_serv_resp.serv_handler,
                            new_serv_resp.start_handle + 5,
                            RSI_BLE_ATT_PROPERTY_READ | RSI_BLE_ATT_PROPERTY_NOTIFY,
                            new_serv_resp.start_handle + 6,
                            new_uuid);

  rsi_ble_att3_val_hndl = new_serv_resp.start_handle + 6; // adding characteristic value attribute to the service
  new_uuid.size         = 2;
  new_uuid.val.val16    = RSI_BLE_ATTRIBUTE_3_UUID;
  rsi_ble_add_char_val_att(new_serv_resp.serv_handler,
                           new_serv_resp.start_handle + 6,
                           new_uuid,
                           RSI_BLE_ATT_PROPERTY_READ | RSI_BLE_ATT_PROPERTY_NOTIFY,
                           data,
                           RSI_BLE_MAX_DATA_LEN);
  return 0;
}

/**
 * @fn         rsi_ble_app_init
 * @brief      initialize the BLE module.
 * @param[in]  none
 * @return     none.
 * @section description
 * This function is used to initialize the BLE module
 */
void rsi_gatt_configurator_init(void)
{
  uint8_t adv[31] = { 2, 1, 6 };

  rsi_ble_add_configurator_serv(); // adding simple BLE chat service

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
  ble_set_event(BLE_CONNECTION_MTU_EVENT, NULL, 0);
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
  UNUSED_PARAMETER(rsi_ble_write);

  ble_set_event(BLE_GATT_WRITE_REQUEST_EVENT, NULL, 0);
//  uint8_t cmdid;

//   //  Requests will come from Mobile app
//   if ((rsi_ble_att1_val_hndl) == *((uint16_t *)rsi_ble_write->handle)) {
//     cmdid = rsi_ble_write->att_value[0];

//     switch (cmdid) {
//       // Scan command request
//       case '3': //else if(rsi_ble_write->att_value[0] == '3')
//       {
//         LOG_PRINT("Received scan request\n");
//         retry = 0;
//         memset(data, 0, sizeof(data));
//         //wifi_app_set_event(WIFI_APP_SCAN_STATE);
//       } break;

//       // Sending SSID
//       case '2': //else if(rsi_ble_write->att_value[0] == '2')
//       {
//         memset(coex_ssid, 0, sizeof(coex_ssid));
//         strcpy((char *)coex_ssid, (const char *)&rsi_ble_write->att_value[3]);

//         rsi_ble_app_set_event(RSI_SSID);
//       } break;

//       // Sending Security type
//       case '5': //else if(rsi_ble_write->att_value[0] == '5')
//       {
//         sec_type = ((rsi_ble_write->att_value[3]) - '0');
//         LOG_PRINT("In Security Request\n");

//         rsi_ble_app_set_event(RSI_SECTYPE);
//       } break;

//       // Sending PSK
//       case '6': //else if(rsi_ble_write->att_value[0] == '6')
//       {
//         memset(data, 0, sizeof(data));
//         strcpy((char *)pwd, (const char *)&rsi_ble_write->att_value[3]);
//         LOG_PRINT("PWD from ble app\n");
//         //wifi_app_set_event(WIFI_APP_JOIN_STATE);
//       } break;

//       // WLAN Status Request
//       case '7': //else if(rsi_ble_write->att_value[0] == '7')
//       {
//         LOG_PRINT("WLAN status request received\n");
//         memset(data, 0, sizeof(data));
//         if (connected) {
//           rsi_ble_app_set_event(RSI_WLAN_ALREADY);
//         } else {
//           rsi_ble_app_set_event(RSI_WLAN_NOT_ALREADY);
//         }
//       } break;

//       // WLAN disconnect request
//       case '4': //else if(rsi_ble_write->att_value[0] == '4')
//       {
//         LOG_PRINT("WLAN disconnect request received\n");
//         memset(data, 0, sizeof(data));
//         //wifi_app_set_event(WIFI_APP_DISCONN_NOTIFY_STATE);
//       } break;

//       // FW version request
//       case '8': {
//         memset(data, 0, sizeof(data));
//         rsi_ble_app_set_event(RSI_APP_FW_VERSION);
//         LOG_PRINT("FW version request\n");
//       } break;

//       default:
//         LOG_PRINT("Default command case \n\n");
//         break;
//     }
//   }
}

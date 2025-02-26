
#include "ble_gatt.h"



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


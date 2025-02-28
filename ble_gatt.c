
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

#include "gatt_db.h"

// Define constants for AD types
#define AD_TYPE_FLAGS 0x01
#define AD_TYPE_TX_POWER_LEVEL 0x0A
#define AD_TYPE_PERIPHERAL_CONN_INTERVAL_RANGE 0x12
#define AD_TYPE_16BIT_SERVICE_UUID_COMPLETE 0x03
#define AD_TYPE_16BIT_SERVICE_UUID_INCOMPLETE 0x02
#define AD_TYPE_128BIT_SERVICE_UUID_COMPLETE 0x07
#define AD_TYPE_128BIT_SERVICE_UUID_INCOMPLETE 0x06
#define AD_TYPE_LOCAL_NAME_COMPLETE 0x09
#define AD_TYPE_LOCAL_NAME_SHORTENED 0x08

/**
 * ADV_FLAGS:
 * Bit 0 (LE Limited Discoverable Mode): 0 (not set)
 * Bit 1 (LE General Discoverable Mode): 1 (set)
 * Bit 2 (BR/EDR Not Supported): 1 (set)
 * Bit 3 (Simultaneous LE and BR/EDR to Same Device Capable (Controller)): 0 (not set)
 * Bit 4 (Simultaneous LE and BR/EDR to Same Device Capable (Host)): 0 (not set)
 * 
 * This configuration indicates that the device is in LE General Discoverable Mode
 * and BR/EDR is not supported.
 */
// Define the flags field for advertising data
#define ADV_FLAGS 0x06

// Define the maximum advertising data length
#define RSI_BLE_MAX_ADV_DATA_LEN    31

// BLE attribute service types uuid values
#define RSI_BLE_CHAR_SERV_UUID 0x2803
#define RSI_BLE_CLIENT_CHAR_UUID 0x2902

// BLE characteristic service uuid
#define RSI_BLE_NEW_SERVICE_UUID 0xAABB
#define RSI_BLE_ATTRIBUTE_1_UUID 0x1AA1
#define RSI_BLE_ATTRIBUTE_2_UUID 0x1BB1
#define RSI_BLE_ATTRIBUTE_3_UUID 0x1CC1

// max data length
#define RSI_BLE_MAX_DATA_LEN 66

// // local device name
// #define RSI_BLE_APP_DEVICE_NAME "BLE_CONFIGURATOR"

// attribute properties
#define RSI_BLE_ATT_PROPERTY_READ 0x02
#define RSI_BLE_ATT_PROPERTY_WRITE 0x08
#define RSI_BLE_ATT_PROPERTY_NOTIFY 0x10
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
static void rsi_ble_on_read_resp(uint16_t resp_status,
                                    uint16_t resp_id,
                                    rsi_ble_resp_att_value_t *rsi_ble_resp_att_val);
static void rsi_ble_on_write_resp(uint16_t resp_status, uint16_t resp_id);
static void rsi_ble_on_read_req_event(uint16_t event_id, rsi_ble_read_req_t *rsi_ble_read_req);


// Forward declarations
static void add_ad_element(uint8_t *ad_data, uint8_t *ad_len, uint8_t ad_type, uint8_t *data, uint8_t data_len);
static sl_status_t lookup_gattdb_uuid(const sli_bt_gattdb_t *gatt_db, uint8_t index, uint16_t uuid, uint8_t *datatype, void **data, uint16_t *handle);
static sl_status_t service_lookup(const sli_bt_gattdb_t *gatt_db, uint16_t service_uuid, uint16_t *handle);
static sl_status_t characteristic_lookup(const sli_bt_gattdb_t *gatt_db, uint16_t char_uuid, uint8_t *properties, uint16_t *data_handle, void **char_data, uint8_t *char_data_type);
static sl_status_t lookup_device_name(const sli_bt_gattdb_t *gatt_db, uint8_t **name, uint16_t *name_len);
static sl_status_t set_adv_data_from_gattdb(const sli_bt_gattdb_t *gatt_db, uint8_t* adv_data, uint8_t* adv_data_len);
/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

/**
 * @brief Lookup function to find the Device Name characteristic in the GATT database.
 * 
 * @param gatt_db Pointer to the GATT database.
 * @param name Pointer to store the pointer to the device name.
 * @param name_len Pointer to store the length of the device name.
 * @return sl_status_t SL_STATUS_OK if the Device Name characteristic is found, otherwise an error code.
 */
static sl_status_t lookup_device_name(const sli_bt_gattdb_t *gatt_db, uint8_t **name, uint16_t *name_len) {
    uint16_t service_handle;
    uint8_t properties;
    uint16_t data_handle;
    sli_bt_gattdb_attribute_chrvalue_t *char_data;
    uint8_t char_data_type;

    // Lookup the Generic Access service (UUID 0x1800)
    if (service_lookup(gatt_db, 0x1800, &service_handle) == SL_STATUS_OK) {
        // Lookup the Device Name characteristic (UUID 0x2A00)
        if (characteristic_lookup(gatt_db, 0x2A00, &properties, &data_handle, (void *)&char_data, &char_data_type) == SL_STATUS_OK) {
            if ((char_data != NULL) && (char_data_type == 0x01)) {
                *name = char_data->data;
                *name_len = char_data->max_len;
                return SL_STATUS_OK;
            }
        }
    }
    return SL_STATUS_NOT_FOUND;
}

// Function to add an AD element to the advertising data
static void add_ad_element(uint8_t *ad_data, uint8_t *ad_len, uint8_t ad_type, uint8_t *data, uint8_t data_len) {
    ad_data[*ad_len] = data_len + 1; // Length byte
    ad_data[*ad_len + 1] = ad_type;  // Type byte
    memcpy(&ad_data[*ad_len + 2], data, data_len); // Data bytes
    *ad_len += data_len + 2;
}

/**
 * @brief Lookup function to find a specific UUID in the GATT database.
 * 
 * @param gatt_db Pointer to the GATT database.
 * @param uuid The UUID to look for.
 * @param datatype Pointer to store the datatype of the found attribute.
 * @param data Pointer to store the data field of the found attribute.
 * @param handle Pointer to store the handle of the found attribute.
 * @return sl_status_t SL_STATUS_OK if the UUID is found, otherwise an error code.
 * 
 * @note The data pointer must be casted depending on the datatype:
 *       - sli_bt_gattdb_value_t for datatype = 0x00
 *       - sli_bt_gattdb_attribute_chrvalue_t for datatype = 0x01
 */
static sl_status_t lookup_gattdb_uuid(const sli_bt_gattdb_t *gatt_db, uint8_t index, uint16_t uuid, uint8_t *datatype, void **data, uint16_t *handle)
{
        const sli_bt_gattdb_attribute_t *attr = &gatt_db->attributes[index];

        if (attr->uuid == uuid) {
            *datatype = attr->datatype;
            *handle = attr->handle;

            switch (attr->datatype) {
                case 0x00:
                    *data = (void *)attr->constdata;
                    break;
                case 0x05:
                    *data = (void *)&attr->characteristic;
                    break;
                case 0x01:
                    if (attr->dynamicdata != NULL) {
                        *data = (void *)attr->dynamicdata;
                    } else {
                        return SL_STATUS_FAIL;
                    }
                    break;
                case 0x03:
                    *data = (void *)&attr->configdata;
                    break;
                case 0x07:
                    if (attr->dynamicdata == NULL) {
                        *data = (void *)attr->dynamicdata;
                    } else {
                        return SL_STATUS_FAIL;
                    }
                    break;
                default:
                    return SL_STATUS_FAIL;
            }

            return SL_STATUS_OK;
        }

    return SL_STATUS_NOT_FOUND;
}

/**
 * @brief Lookup function to find a specific service UUID in the GATT database.
 * 
 * @param gatt_db Pointer to the GATT database.
 * @param service_uuid The service UUID to look for.
 * @param handle Pointer to store the handle of the found service.
 * @return sl_status_t SL_STATUS_OK if the service UUID is found, otherwise an error code.
 */
static sl_status_t service_lookup(const sli_bt_gattdb_t *gatt_db, uint16_t service_uuid, uint16_t *handle) {
    uint8_t datatype;
    void *data;
    uint16_t temp_handle;

    // Iterate through the GATT database attributes to find services
    for (uint16_t i = 0; i < gatt_db->attribute_table_size; i++) {
        if (lookup_gattdb_uuid(gatt_db, i, 0x0000, &datatype, &data, &temp_handle) == SL_STATUS_OK) {
            if (datatype == 0x00) {
                sli_bt_gattdb_value_t *constdata = (sli_bt_gattdb_value_t *)data;
                if (constdata->len == 2) {
                    uint16_t found_service_uuid = (constdata->data[1] << 8) | constdata->data[0];
                    if (found_service_uuid == service_uuid) {
                        *handle = temp_handle;
                        return SL_STATUS_OK;
                    }
                }
            }
        }
    }

    return SL_STATUS_NOT_FOUND;
}

/**
 * @brief Lookup function to find a specific characteristic UUID in the GATT database.
 * 
 * @param gatt_db Pointer to the GATT database.
 * @param char_uuid The characteristic UUID to look for.
 * @param properties Pointer to store the characteristic properties.
 * @param data_handle Pointer to store the handle of the characteristic data.
 * @param char_data Pointer to store the pointer to the characteristic data. Needs a cast depending on datatype:
 *                      0x00 -> sli_bt_gattdb_value_t
 *                      0x01 -> sli_bt_gattdb_attribute_chrvalue_t
 *                      0x07 -> sli_bt_gattdb_attribute_chrvalue_t
 * @return sl_status_t SL_STATUS_OK if the characteristic UUID is found, otherwise an error code.
 */
static sl_status_t characteristic_lookup(const sli_bt_gattdb_t *gatt_db, uint16_t char_uuid, uint8_t *properties, uint16_t *data_handle, void **char_data, uint8_t *char_data_type)
{
    void *data;
    uint16_t temp_handle;

    // Iterate through the GATT database attributes to find characteristics
    for (uint16_t i = 0; i < gatt_db->attribute_table_size; i++) {
        if (lookup_gattdb_uuid(gatt_db, i, 0x0002, char_data_type, &data, &temp_handle) == SL_STATUS_OK) {
            if (*char_data_type == 0x05) {
                sli_bt_gattdb_attribute_characteristic_t *characteristic = (sli_bt_gattdb_attribute_characteristic_t *)data;
                uint16_t found_char_uuid = gatt_db->uuid16[characteristic->char_uuid];

                if (found_char_uuid == char_uuid) {
                    *properties = characteristic->properties;

                    // Lookup the characteristic data
                    if (lookup_gattdb_uuid(gatt_db, temp_handle, characteristic->char_uuid, char_data_type, &data, data_handle) == SL_STATUS_OK) {
                        sl_status_t ret_value;
                        switch (*char_data_type)
                        {
                            case 0x00:
                                *char_data = (sli_bt_gattdb_value_t *)data;
                                ret_value = SL_STATUS_OK;
                                break;
                            case 0x01:
                                *char_data = (sli_bt_gattdb_attribute_chrvalue_t *)data;
                                ret_value = SL_STATUS_OK;
                                break;
                            default:
                                *char_data = NULL;
                                ret_value = SL_STATUS_FAIL;
                                break;
                        }
                        return ret_value;
                    }
                }
            } else {
                return SL_STATUS_FAIL;
            }
        }
    }
    return SL_STATUS_NOT_FOUND;
}

/**
 * @brief Function to generate advertising data from GATT database.
 *
 * @param adv_data Pointer to the buffer to store the advertising data.
 * @param adv_data_len Pointer to store the length of the advertising data.
 * @param gatt_db Pointer to the GATT database (const).
 * @return sl_status_t SL_STATUS_OK if the advertising data is generated successfully, otherwise an error code.
 */
static sl_status_t set_adv_data_from_gattdb(const sli_bt_gattdb_t *gatt_db, uint8_t* adv_data, uint8_t* adv_data_len)
{
    uint8_t ad_len = 0;
    uint8_t *device_name = NULL;
    uint16_t device_name_len = 0;
    uint16_t service_handle;
    uint8_t properties;
    uint16_t data_handle;
    sli_bt_gattdb_attribute_chrvalue_t *char_data;
    uint8_t char_data_type = 0;

    // 1. Add a flags field to advertising data
    uint8_t flags = ADV_FLAGS;
    add_ad_element(adv_data, &ad_len, AD_TYPE_FLAGS, &flags, sizeof(flags));

    // Lookup the Device Name
    if (lookup_device_name(gatt_db, &device_name, &device_name_len) != SL_STATUS_OK) {
        device_name = NULL;
        device_name_len = 0;
    }

    // 2. Look for the TX Power service (UUID 0x1804)
    if (service_lookup(gatt_db, 0x1804, &service_handle) == SL_STATUS_OK) {
        // Lookup the TX Power Level characteristic (UUID 0x2A07)
        if (characteristic_lookup(gatt_db, 0x2A07, &properties, &data_handle, (void**)(&char_data), &char_data_type) == SL_STATUS_OK) {
            if (char_data != NULL) {
                int8_t tx_power_level = char_data->data[0]; // Assuming TX Power Level is a single byte
                add_ad_element(adv_data, &ad_len, AD_TYPE_TX_POWER_LEVEL, (uint8_t *)&tx_power_level, sizeof(tx_power_level));
            }
        }
    }

    // 3. Look for the GAP Peripheral Preferred Connection Parameters characteristic (UUID 0x2A04)
    if (characteristic_lookup(gatt_db, 0x2A04, &properties, &data_handle, (void**)(&char_data), &char_data_type) == SL_STATUS_OK) {
        if (char_data != NULL) {
            uint8_t conn_interval_range[4];
            memcpy(conn_interval_range, char_data->data, sizeof(conn_interval_range)); // Assuming the data is 4 bytes
            add_ad_element(adv_data, &ad_len, AD_TYPE_PERIPHERAL_CONN_INTERVAL_RANGE, conn_interval_range, sizeof(conn_interval_range));
        }
    }

    // 4. Add a list of 16-bit service UUIDs to advertising data if the 15th bit of permissions is set
    for (uint16_t i = 0; i < gatt_db->attribute_table_size; i++) {
        const sli_bt_gattdb_attribute_t *attr = &gatt_db->attributes[i];

        if ((attr->permissions & 0x8000) && attr->uuid == 0x0000) { // Check if it's a service
            sli_bt_gattdb_value_t *constdata = (sli_bt_gattdb_value_t *)attr->constdata;
            if (constdata->len == 2) {
                uint8_t service_uuid_16bit[2] = { constdata->data[0], constdata->data[1] };
                uint8_t ad_type = AD_TYPE_16BIT_SERVICE_UUID_COMPLETE; // Assuming complete list for simplicity
                add_ad_element(adv_data, &ad_len, ad_type, service_uuid_16bit, sizeof(service_uuid_16bit));
            }
        }
    }

    // 5. Add a list of 128-bit service UUIDs to advertising data if the 15th bit of permissions is set
    for (uint16_t i = 0; i < gatt_db->attribute_table_size; i++) {
        const sli_bt_gattdb_attribute_t *attr = &gatt_db->attributes[i];

        if ((attr->permissions & 0x8000) && attr->uuid == 0x0000) { // Check if it's a service
            sli_bt_gattdb_value_t *constdata = (sli_bt_gattdb_value_t *)attr->constdata;
            if (constdata->len == 16) {
                uint8_t service_uuid_128bit[16];
                memcpy(service_uuid_128bit, constdata->data, sizeof(service_uuid_128bit));
                uint8_t ad_type = AD_TYPE_128BIT_SERVICE_UUID_COMPLETE; // Assuming complete list for simplicity
                add_ad_element(adv_data, &ad_len, ad_type, service_uuid_128bit, sizeof(service_uuid_128bit));
            }
        }
    }

    // 6. Try to add the full local name to advertising data
    uint8_t privacy_mode = 0; // Example privacy mode

    if (!privacy_mode && device_name != NULL) {
        if (device_name_len <= (RSI_BLE_MAX_ADV_DATA_LEN - ad_len)) {
            add_ad_element(adv_data, &ad_len, AD_TYPE_LOCAL_NAME_COMPLETE, device_name, device_name_len);
        } else if ((RSI_BLE_MAX_ADV_DATA_LEN - ad_len) >= 6) {
            add_ad_element(adv_data, &ad_len, AD_TYPE_LOCAL_NAME_SHORTENED, device_name, RSI_BLE_MAX_ADV_DATA_LEN - ad_len);
        } else {
            // Add local name to scan response data
        }
    }

    // Set the advertising data length
    *adv_data_len = ad_len;

    return SL_STATUS_OK;
}

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
  uint8_t adv_data[RSI_BLE_MAX_ADV_DATA_LEN] = { 0 };
  uint8_t adv_data_len = 0;
  uint8_t *device_name = NULL;
  uint16_t device_name_len = 0;

  rsi_ble_add_configurator_serv(); // adding simple BLE chat service

    // registering the GATT callback functions
    rsi_ble_gatt_register_callbacks(NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        rsi_ble_on_read_resp,
        rsi_ble_on_write_resp,
        rsi_ble_on_gatt_write_event,
        NULL,
        NULL,
        rsi_ble_on_read_req_event,
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

    // Lookup the Device Name
    if (lookup_device_name(&gattdb, &device_name, &device_name_len) != SL_STATUS_OK) {
        device_name = NULL;
        device_name_len = 0;
    }

    // Set local name
    rsi_bt_set_local_name((const uint8_t *)device_name);

    // Forge ADV data from EFR32 GATT DB
    set_adv_data_from_gattdb(&gattdb, adv_data, &adv_data_len);

   // set advertise data
   rsi_ble_set_advertise_data((const uint8_t *)adv_data, adv_data_len);
}


static void rsi_ble_on_read_resp(uint16_t resp_status,
                                    uint16_t resp_id,
                                    rsi_ble_resp_att_value_t *rsi_ble_resp_att_val)
{
    UNUSED_PARAMETER(resp_status);
    UNUSED_PARAMETER(resp_id);
    UNUSED_PARAMETER(rsi_ble_resp_att_val);
}

static void rsi_ble_on_write_resp(uint16_t resp_status, uint16_t resp_id)
{
    UNUSED_PARAMETER(resp_status);
    UNUSED_PARAMETER(resp_id);
}

static void rsi_ble_on_read_req_event(uint16_t event_id, rsi_ble_read_req_t *rsi_ble_read_req)
{
    UNUSED_PARAMETER(event_id);
    UNUSED_PARAMETER(rsi_ble_read_req);
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

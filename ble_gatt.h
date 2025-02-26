
#ifndef BLE_GATT_H
#define BLE_GATT_H

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

// local device name
#define RSI_BLE_APP_DEVICE_NAME "BLE_CONFIGURATOR"

// attribute properties
#define RSI_BLE_ATT_PROPERTY_READ 0x02
#define RSI_BLE_ATT_PROPERTY_WRITE 0x08
#define RSI_BLE_ATT_PROPERTY_NOTIFY 0x10

#endif // BLE_GATT_H
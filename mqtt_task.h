#ifndef MQTT_TASK_H
#define MQTT_TASK_H

#include "sl_status.h"


#define SL_MQTT_EVENT_QUEUE_SIZE        20
#define SL_MQTT_EVENT_MAX_PAYLOAD_SIZE  254

#define mqtt_set_dataless_event(x) mqtt_set_event(x, NULL, 0)

typedef enum mqtt_event_id_e
{
  MQTT_SYSTEM_BOOT_EVENT =                      (1 << 1),
  MQTT_CONNECT_EVENT =                          (1 << 2),
  MQTT_DISCONNECT_EVENT =                       (1 << 3),
  MQTT_PUBLISH_EVENT =                          (1 << 4),
  MQTT_SUBSCRIBE_EVENT =                        (1 << 5),
  MQTT_CONNECTION_REMOTE_FEATURES_EVENT =       (1 << 6),
  MQTT_CONNECTION_MTU_EVENT =                   (1 << 7),
  MQTT_GATT_WRITE_REQUEST_EVENT =               (1 << 8),
  MQTT_GATT_READ_REQUEST_EVENT =                (1 << 9),
  MQTT_GATT_DATALEN_CHANGE_EVENT =              (1 << 10)
} mqtt_event_id_t;   

typedef struct mqtt_event_msg_s{
  mqtt_event_id_t event_id;
  uint8_t seq_num;
  uint8_t payload[SL_MQTT_EVENT_MAX_PAYLOAD_SIZE];
} mqtt_event_msg_t;

void mqtt_set_event(uint32_t event_id, void *event_data, uint32_t event_data_len);

/**
 * @brief Start the MQTT task context.
 *
 * @return sl_status_t The status of the operation.
 */
sl_status_t start_mqtt_task_context(void);

#endif // MQTT_TASK_H

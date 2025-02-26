#ifndef BLE_TASK_H
#define BLE_TASK_H

#include "sl_status.h"

#define SL_BLE_EVENT_QUEUE_SIZE        20
#define SL_BLE_EVENT_MAX_PAYLOAD_SIZE  64

#define ble_set_dataless_event(x) ble_set_event(x, NULL, 0)

typedef enum ble_event_id_e
{
  BLE_BOOT_EVENT =               (1 << 1),
  BLE_UNCONNECTED_EVENT =        (1 << 2),
  BLE_CONNECTED_EVENT =          (1 << 3),
  BLE_IPCONFIG_DONE_EVENT =      (1 << 4),
  BLE_SCAN_COMPLETE_EVENT =      (1 << 5),
  BLE_JOIN_EVENT =               (1 << 6),
  BLE_DISCONNECT_REQUEST_EVENT = (1 << 7)
} ble_event_id_t;

typedef struct ble_event_msg_s{
  ble_event_id_t event_id;
  uint8_t seq_num;
  uint8_t payload[SL_BLE_EVENT_MAX_PAYLOAD_SIZE];
} ble_event_msg_t;

void ble_set_event(uint32_t event_id, void *event_data, uint32_t event_data_len);

/**
 * @brief Start the BLE task context.
 *
 * @return sl_status_t The status of the operation.
 */
sl_status_t start_ble_task_context(void);

#endif // BLE_TASK_H

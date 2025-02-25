#ifndef WLAN_TASK_H
#define WLAN_TASK_H

#include "sl_status.h"

#define SL_WLAN_EVENT_QUEUE_SIZE        20
#define SL_WLAN_EVENT_MAX_PAYLOAD_SIZE  64

#define wlan_set_dataless_event(x) wlan_set_event(x, NULL, 0)

typedef enum wlan_event_id_e
{
  WLAN_BOOT_EVENT =               (1 << 1),
  WLAN_UNCONNECTED_EVENT =        (1 << 2),
  WLAN_CONNECTED_EVENT =          (1 << 3),
  WLAN_IPCONFIG_DONE_EVENT =      (1 << 4),
  WLAN_SCAN_COMPLETE_EVENT =      (1 << 5),
  WLAN_JOIN_EVENT =               (1 << 6),
  WLAN_DISCONNECT_REQUEST_EVENT = (1 << 7)
} wlan_event_id_t;

typedef struct wlan_event_msg_s{
  wlan_event_id_t event_id;
  uint8_t seq_num;
  uint8_t payload[SL_WLAN_EVENT_MAX_PAYLOAD_SIZE];
} wlan_event_msg_t;

/**
 * @brief Start the WLAN task context.
 *
 * This function initializes and starts the WLAN task context, which is responsible for managing the WiFi connection and related functionality.
 *
 * @return SL_STATUS_OK if the WLAN task context was started successfully, or an appropriate error code otherwise.
 */
sl_status_t start_wlan_task_context(void);

void wlan_set_event(uint32_t event_id, void *event_data, uint32_t event_data_len);

#endif // WLAN_TASK_H

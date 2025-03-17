#ifndef WLAN_TASK_H
#define WLAN_TASK_H

#include "sl_status.h"
#include "sl_net_constants.h"

#define SL_WLAN_EVENT_QUEUE_SIZE        20
#define SL_WLAN_EVENT_MAX_PAYLOAD_SIZE  1024

#define wlan_set_dataless_event(x) wlan_set_event(x, NULL, 0)

typedef enum wlan_event_id_e
{
  WLAN_BOOT_EVENT =               (1 << 1),
  WLAN_DISCONNECTED_EVENT =       (1 << 2),
  WLAN_CONNECTED_EVENT =          (1 << 3),
  WLAN_IPCONFIG_DONE_EVENT =      (1 << 4),
  WLAN_SCAN_COMPLETE_EVENT =      (1 << 5),
  WLAN_JOIN_COMPLETE_EVENT =      (1 << 6),
  WLAN_UNKNOWN_EVENT =            (1 << 31)
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

sl_status_t start_wlan_access_point_join(const void *ssid,
                                         uint32_t ssid_length,
                                         sl_wifi_credential_type_t type,
                                         const void *credential,
                                         uint32_t credential_length,
                                         uint8_t sec_type,
                                         uint32_t timeout_ms);

void start_wlan_access_point_disconnect(void);

void wlan_set_event(uint32_t event_id, void *event_data, uint32_t event_data_len);

/**
 * @brief WLAN Event handler.
 *        Executes in the context of the WLAN Task
 *
 * @return sl_status_t The status of the operation.
 */
sl_status_t wlan_on_event(wlan_event_msg_t* event);

#endif // WLAN_TASK_H

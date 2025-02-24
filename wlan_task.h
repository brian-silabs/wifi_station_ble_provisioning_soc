#ifndef WLAN_TASK_H
#define WLAN_TASK_H

#include "sl_status.h"

typedef enum wlan_event_id
{
  WLAN_BOOT_EVENT = 0,
  WLAN_UNCONNECTED_EVENT = 1,
  WLAN_CONNECTED_EVENT = 2,
  WLAN_IPCONFIG_DONE_EVENT = 3,
  WLAN_SCAN_COMPLETE_EVENT = 4,
  WLAN_JOIN_EVENT = 5,
  WLAN_DISCONNECT_REQUEST_EVENT = 6
} wlan_event_id_t;

/**
 * @brief Start the WLAN task context.
 *
 * This function initializes and starts the WLAN task context, which is responsible for managing the WiFi connection and related functionality.
 *
 * @return SL_STATUS_OK if the WLAN task context was started successfully, or an appropriate error code otherwise.
 */
sl_status_t start_wlan_task_context(void);

#endif // WLAN_TASK_H

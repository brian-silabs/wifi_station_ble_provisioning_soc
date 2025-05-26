#ifndef NWP_TASK_H
#define NWP_TASK_H

#include "sl_status.h"

#define SL_NWP_EVENT_QUEUE_SIZE        10
#define SL_NWP_EVENT_MAX_PAYLOAD_SIZE  4    // NWP does not expect more than 4 bytes

typedef enum nwp_event_id_e
{
  NWP_EVENT     =               (1 << 1),
  WLAN_EVENT    =               (1 << 2),
  BLE_EVENT     =               (1 << 3),
  NWP_UNKNOWN_EVENT =           (1 << 31)
} nwp_event_id_t;

typedef struct nwp_event_msg_s{
  nwp_event_id_t event_id;
  uint8_t seq_num;
  uint8_t payload[SL_NWP_EVENT_QUEUE_SIZE];
} nwp_event_msg_t;

typedef struct nwp_config_s{
  uint32_t max_listen_interval;
  uint32_t ps_listen_interval;
  uint8_t twt_enabled;
  uint8_t twt_auto_configured;
  uint32_t twt_period;
} nwp_config_t;

/**
 * @brief Start the NWP task context.
 *
 * @return sl_status_t The status of the operation.
 */
sl_status_t start_nwp_task_context(void);

// Inter process Communication functions
void nwp_set_event(uint32_t event_id, void *event_data);

// Synchronization functions
sl_status_t nwp_access_request(void);
sl_status_t nwp_access_release(void);

sl_status_t nwp_get_running_config(nwp_config_t *config);

#endif // NWP_TASK_H

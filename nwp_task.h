#ifndef NWP_TASK_H
#define NWP_TASK_H

#include "sl_status.h"

/**
 * @brief Start the NWP task context.
 *
 * @return sl_status_t The status of the operation.
 */
sl_status_t start_nwp_task_context(void);

sl_status_t nwp_access_request(void);
sl_status_t nwp_access_release(void);

#endif // NWP_TASK_H

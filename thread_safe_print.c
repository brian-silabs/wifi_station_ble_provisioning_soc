#include "thread_safe_print.h"

#include "sl_status.h"
#include "cmsis_os2.h"

osMutexId_t debug_prints_mutex;

sl_status_t thread_safe_print_init(void)
{
    sl_status_t ret = SL_STATUS_OK;

    debug_prints_mutex = osMutexNew(NULL);
    if (debug_prints_mutex == NULL) {
        ret = SL_STATUS_FAIL;
    }

    return ret;
}
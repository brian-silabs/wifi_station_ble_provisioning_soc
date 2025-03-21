#include <stdbool.h>


#include "heap_monitor_task.h"
#include "heap_monitor_task_config.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"

#include "sl_status.h"
#include "thread_safe_print.h"

// RTOS Variables
const osThreadAttr_t heap_monitor_thread_attributes = {
    .name       = "heap_monitor_thread",
    .attr_bits  = 0,
    .cb_mem     = 0,
    .cb_size    = 0,
    .stack_mem  = 0,
    .stack_size = 2048,
    .priority   = osPriorityHigh7,// Highest below Realtime tasks
    .tz_module  = 0,
    .reserved   = 0,
  };

  static bool heap_monitor_limit_reached_g = false;

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void heap_monitor_task(void *argument);

/*
 *********************************************************************************************************
 *                                         PUBLIC FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

sl_status_t start_heap_monitor_task_context(void)
{
    sl_status_t ret = SL_STATUS_OK;

    THREAD_SAFE_PRINT("Heap Monitor Task Context Init Start\n");

    osThreadId_t ble_thread_id = osThreadNew((osThreadFunc_t)heap_monitor_task, NULL, &heap_monitor_thread_attributes);
    if (ble_thread_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create heap_monitor_task\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("Heap Monitor Task Startup Complete\n");

    THREAD_SAFE_PRINT("Heap Monitor Task Context Init Done\n\n");
    return ret;
}

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
void heap_monitor_task(void *argument)
{
    UNUSED_PARAMETER(argument);

    uint32_t ticks = (HEAP_MONITOR_CHECK_PERIOD_MS * osKernelGetTickFreq()) / 1000;
    uint32_t heap_redzone_limit = (uint32_t)(configTOTAL_HEAP_SIZE * (HEAP_MONITOR_RREDZONE_PERCENT / 100.0));

    heap_monitor_limit_reached_g = false;
    THREAD_SAFE_PRINT("Heap Monitor Limit Set to %ld\n", heap_redzone_limit);

    while (true)
    {
        osDelay(ticks);

        // Get the minimum ever free heap size
        size_t minEverFreeHeapSize = xPortGetMinimumEverFreeHeapSize();

        if( (minEverFreeHeapSize < heap_redzone_limit)
            && (!heap_monitor_limit_reached_g))
        {
            heap_monitor_limit_reached_g = true;
            // Print or log a warning message once
            THREAD_SAFE_PRINT("WARNING: Minimum ever free heap size has been reached: Reached %d, limit was %ld\n", minEverFreeHeapSize, heap_redzone_limit);
        }

    } // while(true)
}

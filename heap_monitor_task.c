#include <stdbool.h>


#include "heap_monitor_task.h"
#include "heap_monitor_task_config.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"

#include "sl_status.h"
#include "thread_safe_print.h"

#if HEAP_MONITOR_TICKLESS
#include "sl_si91x_power_manager.h"

// Define the event mask for all power state transitions you want to monitor
#define PS_EVENT_MASK  (SL_SI91X_POWER_MANAGER_EVENT_TRANSITION_ENTERING_PS4 \
    | SL_SI91X_POWER_MANAGER_EVENT_TRANSITION_LEAVING_PS4 \
    | SL_SI91X_POWER_MANAGER_EVENT_TRANSITION_ENTERING_PS3 \
    | SL_SI91X_POWER_MANAGER_EVENT_TRANSITION_LEAVING_PS3 \
    | SL_SI91X_POWER_MANAGER_EVENT_TRANSITION_ENTERING_PS2 \
    | SL_SI91X_POWER_MANAGER_EVENT_TRANSITION_LEAVING_PS2 \
    | SL_SI91X_POWER_MANAGER_EVENT_TRANSITION_LEAVING_SLEEP)

#endif

#define HEAP_MONITOR_FLAGS_MSK 0x00000001U  // Define the flag mask

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

  osEventFlagsId_t    heap_monitor_evt_flags_id;  // Event flags ID
  static bool heap_monitor_limit_reached_g = false;

#if HEAP_MONITOR_TICKLESS
// Declare the event handle
sl_power_manager_ps_transition_event_handle_t handle;
#endif


/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void heap_monitor_task(void *argument);

#if HEAP_MONITOR_TICKLESS
// Define the callback function that will be called during power state transitions
void transition_callback(sl_power_state_t from, sl_power_state_t to);
#endif

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


    heap_monitor_evt_flags_id = osEventFlagsNew(NULL);
    if (heap_monitor_evt_flags_id == NULL) {
      THREAD_SAFE_PRINT("Failed to create heap_monitor_evt_flags_id\n");
      while(1); // Count on WDOG for the sample app
    }

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

#if HEAP_MONITOR_TICKLESS
    // Create the event info structure with the event mask and callback function
    sl_power_manager_ps_transition_event_info_t info = { 
        .event_mask = PS_EVENT_MASK,
        .on_event = transition_callback 
    };
    
    // Subscribe to power state transition events
    sl_status_t status = sl_si91x_power_manager_subscribe_ps_transition_event(&handle, &info);
    if (status != SL_STATUS_OK) {
        // Handle subscription error
        THREAD_SAFE_PRINT("Failed to subscribe to ps transition 0x%lX\n", status);
    }
#endif

    while (true)
    {
#if HEAP_MONITOR_TICKLESS
        ticks = osWaitForever;
#endif
        uint32_t flag = osEventFlagsWait(heap_monitor_evt_flags_id, HEAP_MONITOR_FLAGS_MSK, osFlagsWaitAny, ticks);
        osEventFlagsClear(heap_monitor_evt_flags_id, flag);

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

#if HEAP_MONITOR_TICKLESS
// Define the callback function that will be called during power state transitions
void transition_callback(sl_power_state_t from, sl_power_state_t to)
{
  UNUSED_PARAMETER(from);
  if((SL_SI91X_POWER_MANAGER_PS4 == to)
    || (SL_SI91X_POWER_MANAGER_PS3 == to))
  {
    // If the system is waking up sleep, we enable the heap monitor for a spin
    osEventFlagsSet(heap_monitor_evt_flags_id, 0x00000001);
  }
}
#endif

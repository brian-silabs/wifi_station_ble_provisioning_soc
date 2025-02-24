
#include <string.h>
#include <stdbool.h>

#include "ble_task.h"
#include "ble_task_config.h"

#include "nwp_task_config.h"
#include "nwp_task.h"

#include "cmsis_os2.h"
#include "sl_status.h"

// Local utilities
#include "thread_safe_print.h"

#include "ble_config.h" // From WiseConnect template

#include "sl_si91x_ble.h"
#include "sl_constants.h"

/*
 *********************************************************************************************************
 *                                         LOCAL GLOBAL VARIABLES
 *********************************************************************************************************
 */

// RTOS Variables
const osThreadAttr_t ble_thread_attributes = {
    .name       = "ble_thread",
    .attr_bits  = 0,
    .cb_mem     = 0,
    .cb_size    = 0,
    .stack_mem  = 0,
    .stack_size = 3072,
    .priority   = osPriorityHigh,
    .tz_module  = 0,
    .reserved   = 0,
  };

osSemaphoreId_t ble_thread_sem;

// BLE Variables

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void ble_task(void *argument);

/*
 *********************************************************************************************************
 *                                         PUBLIC FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
sl_status_t start_ble_task_context(void)
{
    sl_status_t ret = SL_STATUS_OK;

    ble_thread_sem = osSemaphoreNew(1, 0, NULL);
    if (ble_thread_sem == NULL) {
      THREAD_SAFE_PRINT("Failed to create ble_thread_sem\n");
      return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("BLE Semaphore Creation Complete\n");

    //TODO Init Queue or flag

    osThreadId_t ble_thread_id = osThreadNew((osThreadFunc_t)ble_task, NULL, &ble_thread_attributes);
    if (ble_thread_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create ble_task\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("BLE Task Startup Complete\n");
    
    THREAD_SAFE_PRINT("BLE Task Context Init Done\n\n");
    return ret;
}

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
void ble_task(void *argument)
{
    UNUSED_PARAMETER(argument);

    sl_status_t status                 = SL_STATUS_OK;
    int32_t event_id = -1;

    status = nwp_access_request();
    THREAD_SAFE_PRINT("BLE Acquiring NWP Semaphore\r\n");
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to acquire NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    // NWP related init should go here 

    THREAD_SAFE_PRINT("BLE Releasing NWP Semaphore\r\n");
    status = nwp_access_release();
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    while (true)
    {
        // checking for events list
        //event_id = rsi_ble_app_get_event();

        if (event_id == -1) {
        osSemaphoreAcquire(ble_thread_sem, osWaitForever);
        // if events are not received loop will be continued.
        continue;
        }

        switch (event_id) {
            default:
            break;
        }
    }//while(1)
}

/*
 *********************************************************************************************************
 *                                         CALLBACK FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */


#include <string.h>
#include <stdbool.h>

#include "nwp_task.h"
#include "nwp_task.h"
#include "nwp_task_config.h"

#include "cmsis_os2.h"
#include "sl_status.h"

// Local utilities
#include "thread_safe_print.h"
#include "sl_constants.h"

#include "sl_si91x_driver.h"

#define NWP_FLAGS_MSK  0x00001001U  // Define the flag mask

/*
 *********************************************************************************************************
 *                                         LOCAL GLOBAL VARIABLES
 *********************************************************************************************************
 */

// RTOS Variables
const osThreadAttr_t nwp_thread_attributes = {
    .name       = "nwp_thread",
    .attr_bits  = 0,
    .cb_mem     = 0,
    .cb_size    = 0,
    .stack_mem  = 0,
    .stack_size = 3072,
    .priority   = osPriorityHigh7,
    .tz_module  = 0,
    .reserved   = 0,
  };

osSemaphoreId_t     nwp_thread_sem;
osEventFlagsId_t    nwp_evt_flags_id;  // Event flags ID

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void nwp_task(void *argument);

/*
 *********************************************************************************************************
 *                                         PUBLIC FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
sl_status_t start_nwp_task_context(void)
{
    sl_status_t ret = SL_STATUS_OK;

    THREAD_SAFE_PRINT("NWP Task Context Init Start\n");
    nwp_thread_sem = osSemaphoreNew(1, 1, NULL);
    if (nwp_thread_sem == NULL) {
      THREAD_SAFE_PRINT("Failed to create nwp_thread_sem\n");
      return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("NWP Semaphore Creation Complete\n");


    nwp_evt_flags_id = osEventFlagsNew(NULL);
    if (nwp_evt_flags_id == NULL) {
    THREAD_SAFE_PRINT("Failed to create nwp_evt_flags_id\n");
    return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("NWP Flags Creation Complete\n");

    //TODO Init Queue or flag

    osThreadId_t nwp_thread_id = osThreadNew((osThreadFunc_t)nwp_task, NULL, &nwp_thread_attributes);
    if (nwp_thread_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create nwp_task\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("NWP Task Startup Complete\n");
    
    THREAD_SAFE_PRINT("NWP Task Context Init Done\n\n");
    return ret;
}

sl_status_t nwp_access_request(void)
{
  sl_status_t ret = SL_STATUS_OK;

  osStatus_t nwpOsErr = osSemaphoreAcquire(nwp_thread_sem, osWaitForever);
  if(nwpOsErr != osOK)
  {
    THREAD_SAFE_PRINT("Failed to acquire NWP semaphore\n");
    ret = SL_STATUS_FAIL;
  }
  return ret;
}

sl_status_t nwp_access_release(void)
{
  sl_status_t ret = SL_STATUS_OK;

  osStatus_t nwpOsErr = osSemaphoreRelease(nwp_thread_sem);
  if(nwpOsErr != osOK)
  {
    THREAD_SAFE_PRINT("Failed to release NWP semaphore\n");
    ret = SL_STATUS_FAIL;
  }
  return ret;
}

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
void nwp_task(void *argument)
{
    UNUSED_PARAMETER(argument);

    sl_status_t status                 = SL_STATUS_OK;
    int32_t event_id = -1;

    status = nwp_access_request();
    THREAD_SAFE_PRINT("NWP Acquiring NWP Semaphore\r\n");
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to acquire NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    status = sl_si91x_driver_init(&station_init_configuration, NULL);
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to bring NWP interface up: 0x%lx\r\n", status);
      return;
    }
    THREAD_SAFE_PRINT("NWP interface up\r\n");

#ifdef SLI_SI91X_MCU_INTERFACE
    uint8_t xtal_enable = 1;
    status              = sl_si91x_m4_ta_secure_handshake(SL_SI91X_ENABLE_XTAL, 1, &xtal_enable, 0, NULL);
    if (status != SL_STATUS_OK) {
      THREAD_SAFE_PRINT("\r\nFailed to bring m4_ta_secure_handshake: 0x%lx\r\n", status);
      return;
    }
    THREAD_SAFE_PRINT("M4-NWP secure handshake is successful\r\n");
#endif

    //If WLAN, init powersave mode

    //If BLE, Init power save mode too

    THREAD_SAFE_PRINT("NWP Releasing NWP Semaphore\r\n");
    status = nwp_access_release();
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
        return;
    }

     while (true)
    {

        if (event_id == -1) {
            osEventFlagsWait(nwp_evt_flags_id, NWP_FLAGS_MSK, osFlagsWaitAny, osWaitForever);
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

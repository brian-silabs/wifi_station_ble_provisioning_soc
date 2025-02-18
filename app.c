/*******************************************************************************
* @file  app.c
* @brief
*******************************************************************************
* # License
* <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
*******************************************************************************
*
* The licensor of this software is Silicon Laboratories Inc. Your use of this
* software is governed by the terms of Silicon Labs Master Software License
* Agreement (MSLA) available at
* www.silabs.com/about-us/legal/master-software-license-agreement. This
* software is distributed to you in Source Code format and is governed by the
* sections of the MSLA applicable to Source Code.
*
******************************************************************************/
/*************************************************************************
 *
 */

/*================================================================================
 * @brief : This file contains example application for Wlan Station BLE
 * Provisioning
 * @section Description :
 * This application explains how to get the WLAN connection functionality using
 * BLE provisioning.
 * Silicon Labs Module starts advertising and with BLE Provisioning the Access Point
 * details are fetched.
 * Silicon Labs device is configured as a WiFi station and connects to an Access Point.
 =================================================================================*/

/**
 * Include files
 **/
//! SL Wi-Fi SDK includes
#include "sl_board_configuration.h"
#include "cmsis_os2.h"

#include "app.h"
#include "thread_safe_print.h"
#include "wlan_task.h"
// #include "ble_task.h"
// #include "mqtt_task.h"
// #include "host_task.h"

// APP version
#define APP_FW_VERSION "0.1"


const osThreadAttr_t startup_thread_attributes = {
  .name       = "startup_thread",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 3072,
  .priority   = osPriorityRealtime,
  .tz_module  = 0,
  .reserved   = 0,
};

void startup_routine(void *argument)
{
  UNUSED_PARAMETER(argument);

  THREAD_SAFE_PRINT("Setting up application tasks\n");
  start_wlan_task_context();

  THREAD_SAFE_PRINT("\nApplication tasks setup Done, killing startup routine\n");
  osThreadExit();
}

void app_init(void)
{
  sl_status_t status = thread_safe_print_init();
  if(SL_STATUS_OK != status)
  {
    while(1); // Count on WDOG for the sample app
  }

  osThreadId_t startup_thread_id = osThreadNew((osThreadFunc_t)startup_routine, NULL, &startup_thread_attributes);
  if (startup_thread_id == NULL) {
    THREAD_SAFE_PRINT("Failed to create startup_routine\n");
  }
}


#include <string.h>
#include <stdbool.h>

#include "nwp_task.h"
#include "nwp_task.h"
#include "nwp_task_config.h"

#include "wlan_task.h"

#include "cmsis_os2.h"
#include "sl_status.h"

// Local utilities
#include "thread_safe_print.h"
#include "sl_constants.h"

#include "sl_si91x_driver.h"
#include "sl_si91x_ble.h"
#include "sl_wifi.h"
#include "sl_wifi_callback_framework.h"

typedef enum nwp_event_flag_e
{
  NWP_JOINED_WITH_NO_TWT_OR_FAILED_EVENT =        (1 << 1),
  NWP_FLAG_UNKNOWN_EVENT =                        (1 << 31)
} nwp_event_flag_t;

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
osMessageQueueId_t  nwp_evt_queue_id;  // Event flags ID
static uint8_t      nwp_evt_queue_seq_num_g = 0;

static sl_wifi_performance_profile_t wifi_performance_profile_g = { .profile = SL_SI91X_WIFI_PERFORMANCE_PROFILE };
static sl_bt_performance_profile_t ble_performance_profile_g = { .profile = SL_SI91X_BT_PERFORMANCE_PROFILE };

sl_wifi_twt_request_t default_twt_setup_configuration = {
  .twt_enable              = 1,
  .twt_flow_id             = 1,
  .wake_duration           = TWT_WAKE_DURATION,
  .wake_duration_unit      = TWT_WAKE_DURATION_UNIT,
  .wake_duration_tol       = TWT_WAKE_DURATION_TOL,
  .wake_int_exp            = TWT_WAKE_INT_EXP,
  .wake_int_exp_tol        = TWT_WAKE_INT_EXP_TOL,
  .wake_int_mantissa       = TWT_WAKE_INT_MANTISSA,
  .wake_int_mantissa_tol   = TWT_WAKE_INT_MANTISSA_TOL,
  .implicit_twt            = 1,
  .un_announced_twt        = 1,
  .triggered_twt           = 0,
  .twt_channel             = 0,
  .twt_protection          = 0,
  .restrict_tx_outside_tsp = 1,
  .twt_retry_limit         = TWT_WAKE_RETRY_LIMIT,
  .twt_retry_interval      = TWT_WAKE_RETRY_INTERVAL,
  .req_type                = 1,
  .negotiation_type        = 0,
};

sl_wifi_twt_selection_t default_twt_selection_configuration = {
  .twt_enable                            = 1,
  .average_tx_throughput                 = 0,
  .tx_latency                            = 0,
  .rx_latency                            = TWT_RX_LATENCY,
  .device_average_throughput             = DEVICE_AVERAGE_THROUGHPUT,
  .estimated_extra_wake_duration_percent = ESTIMATE_EXTRA_WAKE_DURATION_PERCENT,
  .twt_tolerable_deviation               = TWT_TOLERABLE_DEVIATION,
  .default_wake_interval_ms              = TWT_DEFAULT_WAKE_INTERVAL_MS,
  .default_minimum_wake_duration_ms      = TWT_DEFAULT_WAKE_DURATION_MS,
  .beacon_wake_up_count_after_sp         = MAX_BEACON_WAKE_UP_AFTER_SP
};

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void nwp_task(void *argument);
static sl_status_t nwp_setup_twt(void);
static sl_status_t nwp_setup_low_power_wifi4(void);
static sl_status_t twt_callback_handler(sl_wifi_event_t event,
 sl_si91x_twt_response_t *result,
 uint32_t result_length,
 void *arg);
static void nwp_wait_event(nwp_event_msg_t *event_msg);

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

    nwp_evt_queue_id = osMessageQueueNew(   SL_NWP_EVENT_QUEUE_SIZE, 
                                            sizeof(nwp_event_msg_t), 
                                            NULL);  // Create message queue
    if (nwp_evt_queue_id == NULL) {
      THREAD_SAFE_PRINT("Failed to create nwp_evt_queue_id\n");
      return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("NWP Queue Creation Complete\n");

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

void nwp_set_event(uint32_t event_id, void *event_data)
{
    nwp_event_msg_t event_msg;

    event_msg.event_id = event_id;
    event_msg.seq_num = nwp_evt_queue_seq_num_g;

    if(     (event_data != NULL)
        &&  (sizeof(uint32_t) > 0))
    {
        memcpy(event_msg.payload, event_data, sizeof(uint32_t));
    } else {
        memset(event_msg.payload, 0, SL_NWP_EVENT_MAX_PAYLOAD_SIZE);
    }

    osStatus_t status = osMessageQueuePut(  nwp_evt_queue_id, 
                                            &event_msg, 
                                            0, // Message priority
                                            0); // Timeout - Return immediately

    if (status != osOK) {
        THREAD_SAFE_PRINT("Failed to send wlan event: 0x%lx\r\n", (uint32_t)status);
    } else {
        nwp_evt_queue_seq_num_g++;
    }
}

static void nwp_wait_event(nwp_event_msg_t *event_msg)
{
    osStatus_t status = osMessageQueueGet(  nwp_evt_queue_id, 
                                            event_msg, 
                                            NULL, 
                                            osWaitForever);

    if(status != osOK) {
        THREAD_SAFE_PRINT("Failed to get wlan event: 0x%lx\r\n", (uint32_t)status);
    }
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
    nwp_event_msg_t nwp_event_msg;

    nwp_evt_queue_seq_num_g = 0; // Init the message queue sequence number to 0

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

    //If WLAN, init powersave mode. Should always pass
    // Note : Turns out that NWP - WiFi needs to always be set, even for BLE ONLY apps
    if((SL_SI91X_COEX_MODE == SL_SI91X_WLAN_BLE_MODE)
        || (SL_SI91X_COEX_MODE == SL_SI91X_WLAN_ONLY_MODE)
        || (SL_SI91X_COEX_MODE == SL_SI91X_BLE_MODE))
    {
        THREAD_SAFE_PRINT("Setting Up NWP-WiFi Performance profile to %d\r\n", wifi_performance_profile_g.profile);
        status = sl_wifi_set_performance_profile(&wifi_performance_profile_g);
        if(status != SL_STATUS_OK) {
            THREAD_SAFE_PRINT("Failed to set wifi performance profile, Error Code : 0x%lX\r\n", status);
            return; // Should be an assertion
        }
    }

    //If BLE, Init power save mode too
    if((SL_SI91X_COEX_MODE == SL_SI91X_WLAN_BLE_MODE)
    || (SL_SI91X_COEX_MODE == SL_SI91X_BLE_MODE))
    {
        //! initiating power save in BLE mode
        THREAD_SAFE_PRINT("Setting Up NWP-Ble Performance profile to %d\r\n", ble_performance_profile_g.profile);
        status = sl_si91x_bt_set_performance_profile(&ble_performance_profile_g);
        if (status != SL_STATUS_OK) {
            THREAD_SAFE_PRINT("Failed to set BLE performance profile, Error Code : 0x%lX\r\n", status);
        }
    }

    THREAD_SAFE_PRINT("NWP Releasing NWP Semaphore\r\n");
    status = nwp_access_release();
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
        return;
    }

     while (true)
    {
      nwp_wait_event(&nwp_event_msg);
      uint32_t event_flag = (*(uint32_t *)(nwp_event_msg.payload));// nwp data is an uint32_t
      switch (nwp_event_msg.event_id) {
        case NWP_EVENT: {
          if (event_flag & NWP_JOINED_WITH_NO_TWT_OR_FAILED_EVENT)
          {
            THREAD_SAFE_PRINT("NWP Join Complete no TWT or TWT setup failed \n");
            if(WIFI_AUTO_LOW_POWER_MODE_ENABLE) 
            {
              THREAD_SAFE_PRINT("NWP Setting WiFi 4 Low Power Mode\n");
              nwp_setup_low_power_wifi4();
            }
          }
         } break;
        case WLAN_EVENT: {
          if (event_flag & WLAN_JOIN_COMPLETE_EVENT) {
            if(WIFI_AUTO_LOW_POWER_MODE_ENABLE) 
            {
              THREAD_SAFE_PRINT("NWP Setting Low Power Mode\n");
              if(WIFI_AUTO_LOW_POWER_TRY_TWT) 
              {
                THREAD_SAFE_PRINT("NWP Trying out TWT\n");
                nwp_setup_twt();
              } else {
                THREAD_SAFE_PRINT("NWP Trying out WiFi4 Low Power Mode\n");
                uint32_t event_flag = NWP_JOINED_WITH_NO_TWT_OR_FAILED_EVENT;
                nwp_set_event(NWP_EVENT, &event_flag);
              }
            }
          }
         } break;
        default:
          THREAD_SAFE_PRINT("NWP Unknown Event 0x%d\n", nwp_event_msg.event_id);
          break;
      }
    }//while(1)
}

//// TODO Deal with statuses returns, its a mess
static sl_status_t nwp_setup_twt(void){
  sl_status_t status                                = SL_STATUS_OK;

  status = nwp_access_request();
  THREAD_SAFE_PRINT("NWP Acquiring NWP Semaphore\r\n");
  if (status != SL_STATUS_OK) {
      THREAD_SAFE_PRINT("\r\nFailed to acquire NWP semaphore: 0x%lx\r\n", status);
      return status;
  }

  THREAD_SAFE_PRINT("\r\nSetting up TWT\n");
  //! Set TWT Config
  sl_wifi_set_twt_config_callback(twt_callback_handler, NULL);
  if (TWT_AUTO_CONFIG == 1) {
    wifi_performance_profile_g.twt_selection = default_twt_selection_configuration;
    status                            = sl_wifi_target_wake_time_auto_selection(&wifi_performance_profile_g.twt_selection);
  } else {
    wifi_performance_profile_g.twt_request = default_twt_setup_configuration;
    status                          = sl_wifi_enable_target_wake_time(&wifi_performance_profile_g.twt_request);
  }
  if (status != SL_STATUS_OK) {
    THREAD_SAFE_PRINT("Failed to set twt: 0x%lx\r\n", status);
    THREAD_SAFE_PRINT("NWP Releasing NWP Semaphore\r\n");
    status = nwp_access_release();
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
        return status ;
    }
    return status;
  }

  //! Apply power save profile
  wifi_performance_profile_g.profile = SL_SI91X_WIFI_PERFORMANCE_PROFILE_CONNECTED;
  status                      = sl_wifi_set_performance_profile(&wifi_performance_profile_g);
  if (status != SL_STATUS_OK) {
    THREAD_SAFE_PRINT("\r\nPowersave Configuration Failed, Error Code : 0x%lX\r\n", status);
    THREAD_SAFE_PRINT("NWP Releasing NWP Semaphore\r\n");
    status = nwp_access_release();
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
        return status ;
    }
    return status;
  }
  THREAD_SAFE_PRINT("\r\nAssociated Power Save Enabled\n");

  THREAD_SAFE_PRINT("NWP Releasing NWP Semaphore\r\n");
  status = nwp_access_release();
  if (status != SL_STATUS_OK) {
      THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
      return status ;
  }

  return status;
}

static sl_status_t nwp_setup_low_power_wifi4(void)
{
  sl_status_t status                                = SL_STATUS_OK;

  status = nwp_access_request();
  THREAD_SAFE_PRINT("NWP Acquiring NWP Semaphore\r\n");
  if (status != SL_STATUS_OK) {
      THREAD_SAFE_PRINT("\r\nFailed to acquire NWP semaphore: 0x%lx\r\n", status);
      return status;
  }

  // Prepare low power profile as configured in the nwp config header
  wifi_performance_profile_g.profile = SL_SI91X_WIFI_PERFORMANCE_PROFILE_CONNECTED;

  //TODO Check if clearing the structure is required, as well as TWT disablement sl_wifi_disable_target_wake_time

  // We align on every 3 BEACONs
  // TODO whenever (if ever) available auto adjust based on AP disconnection rate
  wifi_performance_profile_g.listen_interval = 3;
  wifi_performance_profile_g.dtim_aligned_type = SL_SI91X_ALIGN_WITH_BEACON;

  status                      = sl_wifi_set_performance_profile(&wifi_performance_profile_g);
  if (status != SL_STATUS_OK) {
    THREAD_SAFE_PRINT("\r\nPowersave Configuration Failed, Error Code : 0x%lX\r\n", status);
    THREAD_SAFE_PRINT("NWP Releasing NWP Semaphore\r\n");
    status = nwp_access_release();
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
        return status ;
    }
    return status;
  }
  THREAD_SAFE_PRINT("\r\nAssociated Power Save Enabled\n");

  THREAD_SAFE_PRINT("NWP Releasing NWP Semaphore\r\n");
  status = nwp_access_release();
  if (status != SL_STATUS_OK) {
      THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
      return status ;
  }

  return status;
}

/*
 *********************************************************************************************************
 *                                         CALLBACK FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

 static sl_status_t twt_callback_handler(sl_wifi_event_t event,
  sl_si91x_twt_response_t *result,
  uint32_t result_length,
  void *arg)
{
  UNUSED_PARAMETER(result_length);
  UNUSED_PARAMETER(arg);

  if (SL_WIFI_CHECK_IF_EVENT_FAILED(event)) {
      THREAD_SAFE_PRINT("\r\nTWT Setup failed");
      uint32_t event_flag = NWP_JOINED_WITH_NO_TWT_OR_FAILED_EVENT;
      nwp_set_event(NWP_EVENT, &event_flag);
      return SL_STATUS_FAIL;
  }

  switch (event) {
      case SL_WIFI_TWT_RESPONSE_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT Setup success");
      break;
      case SL_WIFI_TWT_UNSOLICITED_SESSION_SUCCESS_EVENT:
          THREAD_SAFE_PRINT("\r\nUnsolicited TWT Setup success");
          break;
      case SL_WIFI_TWT_AP_REJECTED_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT Setup Failed. TWT Setup rejected by AP");
          break;
      case SL_WIFI_TWT_OUT_OF_TOLERANCE_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT Setup Failed. TWT response out of tolerance limits");
          break;
      case SL_WIFI_TWT_RESPONSE_NOT_MATCHED_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT Setup Failed. TWT Response not matched with the request parameters");
          break;
      case SL_WIFI_TWT_UNSUPPORTED_RESPONSE_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT Setup Failed. TWT Response Unsupported");
          break;
      case SL_WIFI_TWT_FAIL_MAX_RETRIES_REACHED_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT Setup Failed. Max retries reached");
          break;
      case SL_WIFI_TWT_INACTIVE_DUE_TO_ROAMING_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT session inactive due to roaming");
          break;
      case SL_WIFI_TWT_INACTIVE_DUE_TO_DISCONNECT_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT session inactive due to wlan disconnection");
          break;
      case SL_WIFI_TWT_TEARDOWN_SUCCESS_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT session teardown success");
          break;
      case SL_WIFI_TWT_AP_TEARDOWN_SUCCESS_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT session teardown from AP");
          break;
      case SL_WIFI_TWT_INACTIVE_NO_AP_SUPPORT_EVENT:
          THREAD_SAFE_PRINT("\r\nConnected AP Does not support TWT");
          break;
      case SL_WIFI_RESCHEDULE_TWT_SUCCESS_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT rescheduled");
          break;
      case SL_WIFI_TWT_INFO_FRAME_EXCHANGE_FAILED_EVENT:
          THREAD_SAFE_PRINT("\r\nTWT rescheduling failed due to a failure in the exchange of TWT information frames.");
          break;
      default:
          THREAD_SAFE_PRINT("\r\nTWT Setup Failed.");
          uint32_t event_flag = NWP_JOINED_WITH_NO_TWT_OR_FAILED_EVENT;
          nwp_set_event(NWP_EVENT, &event_flag);
  }

  if (event < SL_WIFI_TWT_TEARDOWN_SUCCESS_EVENT) {
      THREAD_SAFE_PRINT("\r\n wake duration : 0x%X", result->wake_duration);
      THREAD_SAFE_PRINT("\r\n wake_duration_unit: 0x%X", result->wake_duration_unit);
      THREAD_SAFE_PRINT("\r\n wake_int_exp : 0x%X", result->wake_int_exp);
      THREAD_SAFE_PRINT("\r\n negotiation_type : 0x%X", result->negotiation_type);
      THREAD_SAFE_PRINT("\r\n wake_int_mantissa : 0x%X", result->wake_int_mantissa);
      THREAD_SAFE_PRINT("\r\n implicit_twt : 0x%X", result->implicit_twt);
      THREAD_SAFE_PRINT("\r\n un_announced_twt : 0x%X", result->un_announced_twt);
      THREAD_SAFE_PRINT("\r\n triggered_twt : 0x%X", result->triggered_twt);
      THREAD_SAFE_PRINT("\r\n twt_channel : 0x%X", result->twt_channel);
      THREAD_SAFE_PRINT("\r\n twt_protection : 0x%X", result->twt_protection);
      THREAD_SAFE_PRINT("\r\n twt_flow_id : 0x%X\r\n", result->twt_flow_id);
  } else if (event < SL_WIFI_TWT_EVENTS_END) {
      THREAD_SAFE_PRINT("\r\n twt_flow_id : 0x%X", result->twt_flow_id);
      THREAD_SAFE_PRINT("\r\n negotiation_type : 0x%X\r\n", result->negotiation_type);
  }
  return SL_STATUS_OK;
}

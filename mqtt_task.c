
#include <string.h>
#include <stdbool.h>

#include "mqtt_task.h"
#include "mqtt_task.h"
#include "mqtt_task_config.h"

#include "cmsis_os2.h"
#include "sl_status.h"

// Local utilities
#include "thread_safe_print.h"
#include "sl_constants.h"


/*
 *********************************************************************************************************
 *                                         LOCAL GLOBAL VARIABLES
 *********************************************************************************************************
 */

// RTOS Variables
const osThreadAttr_t mqtt_thread_attributes = {
    .name       = "mqtt_thread",
    .attr_bits  = 0,
    .cb_mem     = 0,
    .cb_size    = 0,
    .stack_mem  = 0,
    .stack_size = 3072,
    .priority   = osPriorityNormal,
    .tz_module  = 0,
    .reserved   = 0,
  };

osMessageQueueId_t  mqtt_evt_queue_id;  // Event flags ID
static uint8_t      mqtt_evt_queue_seq_num_g = 0;

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void mqtt_task(void *argument);
static void mqtt_wait_event(mqtt_event_msg_t *event_msg);


/*
 *********************************************************************************************************
 *                                         PUBLIC FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

 void mqtt_set_event(uint32_t event_id, void *event_data, uint32_t event_data_len)
 {
     mqtt_event_msg_t event_msg;
 
     event_msg.event_id = event_id;
     event_msg.seq_num = mqtt_evt_queue_seq_num_g;
 
     if(     (event_data != NULL)
         &&  (event_data_len > 0))
     {
         memcpy(event_msg.payload, event_data, event_data_len);
     } else {
         memset(event_msg.payload, 0, SL_MQTT_EVENT_MAX_PAYLOAD_SIZE);
     }
 
     osStatus_t status = osMessageQueuePut(  mqtt_evt_queue_id, 
                                             &event_msg, 
                                             0, // Message priority
                                             0); // Timeout - Return immediately
 
     if (status != osOK) {
         THREAD_SAFE_PRINT("Failed to send mqtt event: 0x%lx\r\n", (uint32_t)status);
     } else {
         mqtt_evt_queue_seq_num_g++;
     }
 }
 
 static void mqtt_wait_event(mqtt_event_msg_t *event_msg)
 {
     osStatus_t status = osMessageQueueGet(  mqtt_evt_queue_id, 
                                             event_msg, 
                                             NULL, 
                                             osWaitForever);
 
     if(status != osOK) {
         THREAD_SAFE_PRINT("Failed to get mqtt event: 0x%lx\r\n", (uint32_t)status);
     }
 }

sl_status_t start_mqtt_task_context(void)
{
    sl_status_t ret = SL_STATUS_OK;

    THREAD_SAFE_PRINT("MQTT Task Context Init Start\n");

    mqtt_evt_queue_id = osMessageQueueNew(  SL_MQTT_EVENT_QUEUE_SIZE, 
                                          sizeof(mqtt_event_msg_t), 
                                          NULL);  // Create message queue
    if (mqtt_evt_queue_id == NULL) {
      THREAD_SAFE_PRINT("Failed to create mqtt_evt_queue_id\n");
      return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("MQTT Queue Creation Complete\n");

    osThreadId_t mqtt_thread_id = osThreadNew((osThreadFunc_t)mqtt_task, NULL, &mqtt_thread_attributes);
    if (mqtt_thread_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create mqtt_task\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("MQTT Task Startup Complete\n");
    
    THREAD_SAFE_PRINT("MQTT Task Context Init Done\n\n");
    return ret;
}

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
void mqtt_task(void *argument)
{
    UNUSED_PARAMETER(argument);

    //sl_status_t status                 = SL_STATUS_OK;
    mqtt_event_msg_t mqtt_event_msg;

     while (true)
    {
      mqtt_wait_event(&mqtt_event_msg);

      switch (mqtt_event_msg.event_id) {
            case MQTT_CONNECT_EVENT: {
                THREAD_SAFE_PRINT("MQTT Connected\n");

            break;
            }
            case MQTT_DISCONNECT_EVENT: {
                THREAD_SAFE_PRINT("MQTT Disconnected\n");

            break;
            }
            case MQTT_PUBLISH_EVENT: {
                THREAD_SAFE_PRINT("MQTT Published\n");

            break;
            }
            case MQTT_SUBSCRIBE_EVENT: {
                THREAD_SAFE_PRINT("MQTT Subscribed\n");
 
            break;
            }
            default:
            break;
        }//switch(event_id)
    }//while(1)
}

/*
 *********************************************************************************************************
 *                                         CALLBACK FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

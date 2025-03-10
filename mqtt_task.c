
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
#include "sl_utility.h"

#include "sl_mqtt_client.h"

#include "nwp_task.h"
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
    .stack_size = 2048,
    .priority   = osPriorityNormal,
    .tz_module  = 0,
    .reserved   = 0,
  };

osMessageQueueId_t  mqtt_evt_queue_id;  // Event flags ID
static uint8_t      mqtt_evt_queue_seq_num_g = 0;

//static bool mqtt_initialized = false;
//static bool mqtt_connected = false;

sl_mqtt_client_t client              = { 0 };

sl_mqtt_broker_t mqtt_broker_configuration = {
  .ip                      = SL_IPV4_ADDRESS(192, 168, 9, 1),
  .port                    = MQTT_BROKER_PORT,
  .is_connection_encrypted = ENCRYPT_CONNECTION,
  .connect_timeout         = MQTT_CONNECT_TIMEOUT,
  .keep_alive_interval     = KEEP_ALIVE_INTERVAL,
};

sl_mqtt_client_last_will_message_t last_will_message = {
  .is_retained         = IS_LAST_WILL_RETAINED,
  .will_qos_level      = QOS_OF_LAST_WILL,
  .will_topic          = (uint8_t *)LAST_WILL_TOPIC,
  .will_topic_length   = strlen(LAST_WILL_TOPIC),
  .will_message        = (uint8_t *)LAST_WILL_MESSAGE,
  .will_message_length = strlen(LAST_WILL_MESSAGE),
};

sl_mqtt_client_configuration_t mqtt_client_configuration = { .is_clean_session = CLIENT_IS_CLEAN_SESSION,
  .client_id        = (uint8_t *)CLIENT_ID,
  .client_id_length = strlen(CLIENT_ID),
  .client_port      = CLIENT_PORT };


/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DECLARATIONS
 *********************************************************************************************************
 */
void mqtt_task(void *argument);
static void mqtt_wait_event(mqtt_event_msg_t *event_msg);

static void print_char_buffer(char *buffer, uint32_t buffer_length);

//static void mqtt_client_message_handler(void *client, sl_mqtt_client_message_t *message, void *context);
static void mqtt_client_event_handler(void *client, sl_mqtt_client_event_t event, void *event_data, void *context);


/*
 *********************************************************************************************************
 *                                         PUBLIC FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

sl_status_t mqtt_connect_to_broker(void)
{
  sl_status_t status;
  //memcpy(&mqtt_broker_configuration.ip, &mqtt_broker_ip, sizeof(sl_ip_address_t));
  status =
    sl_mqtt_client_connect(&client, &mqtt_broker_configuration, &last_will_message, &mqtt_client_configuration, 0);
  if (status != SL_STATUS_IN_PROGRESS) {
    THREAD_SAFE_PRINT("Failed to connect to mqtt broker: 0x%lX\r\n", status);
    return status;
  }
  return SL_STATUS_OK;
}

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

    sl_status_t status                 = SL_STATUS_OK;
    mqtt_event_msg_t mqtt_event_msg;


    status = nwp_access_request();
    THREAD_SAFE_PRINT("MQTT Acquiring NWP Semaphore\r\n");
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to acquire NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    THREAD_SAFE_PRINT("MQTT Client Init\n");
     status = sl_mqtt_client_init(&client, mqtt_client_event_handler);
     if (status != SL_STATUS_OK) {
       THREAD_SAFE_PRINT("Failed to init mqtt client: 0x%lX\r\n", status);
     }

     THREAD_SAFE_PRINT("MQTT Releasing NWP Semaphore\r\n");
     status = nwp_access_release();
     if (status != SL_STATUS_OK) {
         THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
         return;
     }

    // Initialize the message queue sequence number
    mqtt_evt_queue_seq_num_g = 0;

    //Consider the WLAN task as initialized
    mqtt_set_dataless_event(MQTT_INITIALIZED_EVENT);

     while (true)
    {
      mqtt_wait_event(&mqtt_event_msg);

      switch (mqtt_event_msg.event_id) {
          case MQTT_INITIALIZED_EVENT: {
            THREAD_SAFE_PRINT("MQTT Initialized\n");
          } break;

            case MQTT_CONNECTION_EVENT: {
              THREAD_SAFE_PRINT("MQTT connected\n");
            } break;

            case MQTT_DISCONNECTION_EVENT: {
                THREAD_SAFE_PRINT("MQTT Disconnected\n");
            } break;

            case MQTT_PUBLISH_EVENT: {
              // Publish a message
              sl_mqtt_client_message_t message_to_be_published = {
                .qos_level            = QOS_OF_PUBLISH_MESSAGE,
                .is_retained          = PUBLISH_MESSAGE_RETAINED,
                .is_duplicate_message = PUBLISH_MESSAGE_DUPLICATE,
                //.topic                = (uint8_t *)PUBLISH_TOPIC,
                //.topic_length         = strlen(PUBLISH_TOPIC),
                //.content              = (uint8_t *)PUBLISH_MESSAGE,
                //.content_length       = strlen(PUBLISH_MESSAGE),
              };

                THREAD_SAFE_PRINT("MQTT Published\n");
                status = sl_mqtt_client_publish(&client, &message_to_be_published, 0, &message_to_be_published);
                if (status != SL_STATUS_IN_PROGRESS) {
                  THREAD_SAFE_PRINT("Failed to publish message: 0x%lX\r\n", status);
                }
            } break;
            case MQTT_SUBSCRIBE_EVENT: {
                THREAD_SAFE_PRINT("MQTT Subscribed\n");

            } break;
            default:{

            } break;
        }//switch(event_id)
    }//while(1)

    osThreadExit();
}

static void print_char_buffer(char *buffer, uint32_t buffer_length)
{
  for (uint32_t index = 0; index < buffer_length; index++) {
    THREAD_SAFE_PRINT("%c", buffer[index]);
  }

  THREAD_SAFE_PRINT("\r\n");
}


/*
 *********************************************************************************************************
 *                                         CALLBACK FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */

static void mqtt_client_event_handler(void *client, sl_mqtt_client_event_t event, void *event_data, void *context)
{
#if !(ENABLE_MQTT_SUBSCRIBE_PUBLISH)
  UNUSED_PARAMETER(context);
#endif

  switch (event) {
    case SL_MQTT_CLIENT_CONNECTED_EVENT: {
      mqtt_set_dataless_event(MQTT_CONNECTION_EVENT);
      break;
    }

    case SL_MQTT_CLIENT_MESSAGE_PUBLISHED_EVENT: {
      sl_mqtt_client_message_t *published_message = (sl_mqtt_client_message_t *)context;
      THREAD_SAFE_PRINT("Published message successfully on topic: ");
      print_char_buffer((char *)published_message->topic, published_message->topic_length);
      break;
    }

    case SL_MQTT_CLIENT_SUBSCRIBED_EVENT: {
      char *subscribed_topic = (char *)context;

      //      status = sl_mqtt_client_subscribe(client,
      //                                        (uint8_t *)TOPIC_TO_BE_SUBSCRIBED,
      //                                        strlen(TOPIC_TO_BE_SUBSCRIBED),
      //                                        QOS_OF_SUBSCRIPTION,
      //                                        0,
      //                                        mqtt_client_message_handler,
      //                                        TOPIC_TO_BE_SUBSCRIBED);
      //      if (status != SL_STATUS_IN_PROGRESS) {
      //      }

      THREAD_SAFE_PRINT("Subscribed to Topic: %s\r\n", subscribed_topic);
      break;
    }

    case SL_MQTT_CLIENT_UNSUBSCRIBED_EVENT: {
      char *unsubscribed_topic = (char *)context;
      sl_status_t status;

      THREAD_SAFE_PRINT("Unsubscribed from topic: %s\r\n", unsubscribed_topic);

      status = sl_mqtt_client_disconnect(client, 0);
      if (status != SL_STATUS_IN_PROGRESS) {
        THREAD_SAFE_PRINT("Failed to disconnect : 0x%lX\r\n", status);
      }
      break;
    }

    case SL_MQTT_CLIENT_DISCONNECTED_EVENT: {
      THREAD_SAFE_PRINT("Disconnected from MQTT broker\r\n");
      break;
    }

    case SL_MQTT_CLIENT_ERROR_EVENT: {
      sl_mqtt_client_error_status_t *error = (sl_mqtt_client_error_status_t *)event_data;
      switch (*error)
      {
        case SL_MQTT_CLIENT_CONNECT_FAILED:
          THREAD_SAFE_PRINT("SL_MQTT_CLIENT_CONNECT_FAILED\r\n");
          mqtt_set_dataless_event(MQTT_CONNECTION_FAILED_EVENT);
          break;

        case SL_MQTT_CLIENT_PUBLISH_FAILED:
          THREAD_SAFE_PRINT("SL_MQTT_CLIENT_PUBLISH_FAILED\r\n");
          break;
        
        case SL_MQTT_CLIENT_SUBSCRIBE_FAILED:
          THREAD_SAFE_PRINT("SL_MQTT_CLIENT_SUBSCRIBE_FAILED\r\n");
          break;

        case SL_MQTT_CLIENT_UNSUBSCRIBED_FAILED:
          THREAD_SAFE_PRINT("SL_MQTT_CLIENT_UNSUBSCRIBED_FAILED\r\n");
          break;

        case SL_MQTT_CLIENT_DISCONNECT_FAILED:
          THREAD_SAFE_PRINT("SL_MQTT_CLIENT_DISCONNECT_FAILED\r\n");
          mqtt_set_dataless_event(MQTT_DISCONNECTION_EVENT);
          break;

        case SL_MQTT_CLIENT_UNKNOWN_ERROR:
          THREAD_SAFE_PRINT("SL_MQTT_CLIENT_UNKNOWN_ERROR\r\n");
          break;
      
        default:
          break;
      }
      break;
    }

    default: {
      break;
    }
  }
}

//static void mqtt_client_message_handler(void *client, sl_mqtt_client_message_t *message, void *context)
//{
//  UNUSED_PARAMETER(context);
//
//  sl_status_t status;
//  printf("Message Received on Topic: ");
//
//  print_char_buffer((char *)message->topic, message->topic_length);
//  print_char_buffer((char *)message->content, message->content_length);
//
//  // Unsubscribing to already subscribed topic.
//  status = sl_mqtt_client_unsubscribe(client,
//                                      (uint8_t *)TOPIC_TO_BE_SUBSCRIBED,
//                                      strlen(TOPIC_TO_BE_SUBSCRIBED),
//                                      0,
//                                      TOPIC_TO_BE_SUBSCRIBED);
//  if (status != SL_STATUS_IN_PROGRESS) {
//    printf("Failed to unsubscribe : 0x%lX\r\n", status);
//    return;
//  }
//}

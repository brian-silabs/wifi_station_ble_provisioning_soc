/***************************************************************************/ /**
 * @file
 * @brief Access point Example Application
 *******************************************************************************
 * # License
 * <b>Copyright 2022 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#include "sl_net.h"
#include "sl_utility.h"
#include "cmsis_os2.h"
#include "sl_constants.h"
#include "sl_mqtt_client.h"
#include "cacert.pem.h"
#include "sl_wifi.h"
#include "sl_wifi_device.h"
#include "string.h"
#include "sl_wifi_callback_framework.h"
#include "sl_net_wifi_types.h"

#include "wifi_config.h"
#include "app.h"

#ifdef SLI_SI91X_MCU_INTERFACE
#include "sl_si91x_m4_ps.h"
#endif

/******************************************************
 *                    Constants
 ******************************************************/


// application event list
#define MQTT_INIT_EVENT     0x01
#define MQTT_PUBLISH_EVENT  0x02

#define ENABLE_MQTT_SUBSCRIBE_PUBLISH 1

#define MQTT_BROKER_PORT 1883

#define CLIENT_PORT 1

#define CLIENT_ID "WIFI-SDK-MQTT-CLIENT"

#define TOPIC_TO_BE_SUBSCRIBED "THERMOSTAT-DATA\0"
#define QOS_OF_SUBSCRIPTION    SL_MQTT_QOS_LEVEL_1

#define PUBLISH_TOPIC          TOPIC_TO_BE_SUBSCRIBED
#define PUBLISH_MESSAGE        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do"
#define QOS_OF_PUBLISH_MESSAGE 0

#define IS_DUPLICATE_MESSAGE 0
#define IS_MESSAGE_RETAINED  0
#define IS_CLEAN_SESSION     1

#define LAST_WILL_TOPIC       "WiFiSDK-MQTT-CLIENT-LAST-WILL"
#define LAST_WILL_MESSAGE     "WiFiSDK-MQTT-CLIENT has been disconnect from network"
#define QOS_OF_LAST_WILL      1
#define IS_LAST_WILL_RETAINED 1

#define ENCRYPT_CONNECTION   0
#define KEEP_ALIVE_INTERVAL  2000 // in seconds
#define MQTT_CONNECT_TIMEOUT 5000 // in milli seconds

#define SEND_CREDENTIALS 0

#define USERNAME "WIFISDK"
#define PASSWORD "password"

/******************************************************
 *               Variable Definitions
 ******************************************************/

sl_status_t callback_status = SL_STATUS_OK;

sl_mqtt_client_t client              = { 0 };

uint8_t is_execution_completed = 0;

sl_mqtt_client_credentials_t *client_credentails = NULL;

sl_mqtt_client_configuration_t mqtt_client_configuration = { .is_clean_session = IS_CLEAN_SESSION,
                                                             .client_id        = (uint8_t *)CLIENT_ID,
                                                             .client_id_length = strlen(CLIENT_ID),
                                                             .client_port      = CLIENT_PORT };

sl_mqtt_broker_t mqtt_broker_configuration = {
  .ip                      = SL_IPV4_ADDRESS(192, 168, 1, 13),
  .port                    = MQTT_BROKER_PORT,
  .is_connection_encrypted = ENCRYPT_CONNECTION,
  .connect_timeout         = MQTT_CONNECT_TIMEOUT,
  .keep_alive_interval     = KEEP_ALIVE_INTERVAL,
};

sl_mqtt_client_message_t message_to_be_published = {
  .qos_level            = QOS_OF_PUBLISH_MESSAGE,
  .is_retained          = IS_MESSAGE_RETAINED,
  .is_duplicate_message = IS_DUPLICATE_MESSAGE,
  .topic                = (uint8_t *)PUBLISH_TOPIC,
  .topic_length         = strlen(PUBLISH_TOPIC),
  .content              = (uint8_t *)PUBLISH_MESSAGE,
  .content_length       = strlen(PUBLISH_MESSAGE),
};

sl_mqtt_client_last_will_message_t last_will_message = {
  .is_retained         = IS_LAST_WILL_RETAINED,
  .will_qos_level      = QOS_OF_LAST_WILL,
  .will_topic          = (uint8_t *)LAST_WILL_TOPIC,
  .will_topic_length   = strlen(LAST_WILL_TOPIC),
  .will_message        = (uint8_t *)LAST_WILL_MESSAGE,
  .will_message_length = strlen(LAST_WILL_MESSAGE),
};

extern osSemaphoreId_t mqtt_thread_sem;
static volatile uint32_t mqtt_app_event_map;

/******************************************************
 *               Function Declarations
 ******************************************************/
void mqtt_client_message_handler(void *client, sl_mqtt_client_message_t *message, void *context);
void mqtt_client_event_handler(void *client, sl_mqtt_client_event_t event, void *event_data, void *context);
void mqtt_client_error_event_handler(void *client, sl_mqtt_client_error_status_t *error);
void mqtt_client_cleanup();
void print_char_buffer(char *buffer, uint32_t buffer_length);
void mqtt_client_task(void *argument);
void wifi_app_send_to_mqtt(uint16_t msg_type, uint8_t *data, uint16_t data_len);
sl_status_t setup_mqtt(void);

static int32_t mqtt_app_get_event(void);
static void mqtt_app_set_event(uint32_t event_num);
static void mqtt_app_clear_event(uint32_t event_num);

/*==============================================*/
/**
 * @fn         mqtt_app_clear_event
 * @brief      clears the specific event.
 * @param[in]  event_num, specific event number.
 * @return     none.
 * @section description
 * This function is used to clear the specific event.
 */
static void mqtt_app_clear_event(uint32_t event_num)
{
  mqtt_app_event_map &= ~BIT(event_num);
  return;
}

/*==============================================*/
/**
 * @fn         mqtt_app_get_event
 * @brief      returns the first set event based on priority
 * @param[in]  none.
 * @return     int32_t
 *             > 0  = event number
 *             -1   = not received any event
 * @section description
 * This function returns the highest priority event among all the set events
 */
static int32_t mqtt_app_get_event(void)
{
  uint32_t ix;

  for (ix = 0; ix < 32; ix++) {
    if (mqtt_app_event_map & (1 << ix)) {
      return ix;
    }
  }

  return (-1);
}

/*==============================================*/
/**
 * @fn         mqtt_app_set_event
 * @brief      sets the specific event.
 * @param[in]  event_num, specific event number.
 * @return     none.
 * @section description
 * This function is used to set/raise the specific event.
 */
static void mqtt_app_set_event(uint32_t event_num)
{
  mqtt_app_event_map |= BIT(event_num);

  osSemaphoreRelease(mqtt_thread_sem);

  return;
}

void mqtt_client_cleanup()
{
  SL_CLEANUP_MALLOC(client_credentails);
  is_execution_completed = 1;
}

void mqtt_client_message_handler(void *client, sl_mqtt_client_message_t *message, void *context)
{
  UNUSED_PARAMETER(context);

  sl_status_t status;
  printf("Message Received on Topic: ");

  print_char_buffer((char *)message->topic, message->topic_length);
  print_char_buffer((char *)message->content, message->content_length);

  // Unsubscribing to already subscribed topic.
  status = sl_mqtt_client_unsubscribe(client,
                                      (uint8_t *)TOPIC_TO_BE_SUBSCRIBED,
                                      strlen(TOPIC_TO_BE_SUBSCRIBED),
                                      0,
                                      TOPIC_TO_BE_SUBSCRIBED);
  if (status != SL_STATUS_IN_PROGRESS) {
    printf("Failed to unsubscribe : 0x%lX\r\n", status);

    mqtt_client_cleanup();
    return;
  }
}

void print_char_buffer(char *buffer, uint32_t buffer_length)
{
  for (uint32_t index = 0; index < buffer_length; index++) {
    printf("%c", buffer[index]);
  }

  printf("\r\n");
}

void mqtt_client_error_event_handler(void *client, sl_mqtt_client_error_status_t *error)
{
  UNUSED_PARAMETER(client);

  printf("Terminating program, Error: %d\r\n", *error);
  mqtt_client_cleanup();
}

void mqtt_client_event_handler(void *client, sl_mqtt_client_event_t event, void *event_data, void *context)
{
#if !(ENABLE_MQTT_SUBSCRIBE_PUBLISH)
  UNUSED_PARAMETER(context);
#endif

  switch (event) {
    case SL_MQTT_CLIENT_CONNECTED_EVENT: {
      printf("MQTT client connection success\r\n");
#if ENABLE_MQTT_SUBSCRIBE_PUBLISH
      sl_status_t status;
      status = sl_mqtt_client_subscribe(client,
                                        (uint8_t *)TOPIC_TO_BE_SUBSCRIBED,
                                        strlen(TOPIC_TO_BE_SUBSCRIBED),
                                        QOS_OF_SUBSCRIPTION,
                                        0,
                                        mqtt_client_message_handler,
                                        TOPIC_TO_BE_SUBSCRIBED);
      if (status != SL_STATUS_IN_PROGRESS) {
        printf("Failed to subscribe : 0x%lX\r\n", status);

        mqtt_client_cleanup();
        return;
      }

      status = sl_mqtt_client_publish(client, &message_to_be_published, 0, &message_to_be_published);
      if (status != SL_STATUS_IN_PROGRESS) {
        printf("Failed to publish message: 0x%lX\r\n", status);

        mqtt_client_cleanup();
        return;
      }
#else
      mqtt_client_cleanup();
#endif
      break;
    }
#if ENABLE_MQTT_SUBSCRIBE_PUBLISH
    case SL_MQTT_CLIENT_MESSAGE_PUBLISHED_EVENT: {
      sl_mqtt_client_message_t *published_message = (sl_mqtt_client_message_t *)context;

      printf("Published message successfully on topic: ");
      print_char_buffer((char *)published_message->topic, published_message->topic_length);

      break;
    }

    case SL_MQTT_CLIENT_SUBSCRIBED_EVENT: {
      char *subscribed_topic = (char *)context;

      printf("Subscribed to Topic: %s\r\n", subscribed_topic);
      break;
    }

    case SL_MQTT_CLIENT_UNSUBSCRIBED_EVENT: {
      char *unsubscribed_topic = (char *)context;
      sl_status_t status;

      printf("Unsubscribed from topic: %s\r\n", unsubscribed_topic);

      status = sl_mqtt_client_disconnect(client, 0);
      if (status != SL_STATUS_IN_PROGRESS) {
        printf("Failed to disconnect : 0x%lX\r\n", status);

        mqtt_client_cleanup();
        return;
      }
      break;
    }
#endif

    case SL_MQTT_CLIENT_DISCONNECTED_EVENT: {
      printf("Disconnected from MQTT broker\r\n");

      mqtt_client_cleanup();
      break;
    }

    case SL_MQTT_CLIENT_ERROR_EVENT: {
      mqtt_client_error_event_handler(client, (sl_mqtt_client_error_status_t *)event_data);
      break;
    }

    default: {
      break;
    }
  }
}

sl_status_t setup_mqtt(void)
{
  sl_status_t status;

  status = sl_mqtt_client_init(&client, mqtt_client_event_handler);
  if (status != SL_STATUS_OK) {
    printf("Failed to init mqtt client: 0x%lX\r\n", status);

    mqtt_client_cleanup();
    return status;
  }
  printf("\r\nMQTT Client Init Successful\r\n");

  printf("\r\nConnecting to Broker at : \r\n");
  print_sl_ip_address(&mqtt_broker_ip);
  printf("\r\n");

  memcpy(&mqtt_broker_configuration.ip, &mqtt_broker_ip, sizeof(sl_ip_address_t));

  status =
    sl_mqtt_client_connect(&client, &mqtt_broker_configuration, &last_will_message, &mqtt_client_configuration, 0);
  if (status != SL_STATUS_IN_PROGRESS) {
    printf("Failed to connect to mqtt broker: 0x%lX\r\n", status);

    mqtt_client_cleanup();
    return status;
  }

  return SL_STATUS_OK;
}


void mqtt_client_task(void *argument)
{
  UNUSED_PARAMETER(argument);
  int32_t event_id;

  while(1){
        // checking for events list
    event_id = mqtt_app_get_event();

    if (event_id == -1) {
      osSemaphoreAcquire(mqtt_thread_sem, osWaitForever);
      // if events are not received loop will be continued.
      continue;
    }

    switch (event_id)
    {
    case MQTT_INIT_EVENT:
      mqtt_app_clear_event(MQTT_INIT_EVENT);
      setup_mqtt();
      break;
    
    case MQTT_PUBLISH_EVENT:
      mqtt_app_clear_event(MQTT_PUBLISH_EVENT);
      printf("MQTT PUBLISH\r\n");
      break;
    
    default:
      break;
    }
  }
}

/*==============================================*/
/**
 * @fn         wifi_app_send_to_mqtt
 * @brief      this function is used to send data to mqtt app.
 * @param[in]   msg_type, it indicates write/notification event id.
 * @param[in]  data, raw data pointer.
 * @param[in]  data_len, raw data length.
 * @return     none.
 * @section description
 */
void wifi_app_send_to_mqtt(uint16_t msg_type, uint8_t *data, uint16_t data_len)
{
  UNUSED_PARAMETER(data);
  UNUSED_PARAMETER(data_len);

  switch (msg_type) {
    case WIFI_APP_CONNECTION_STATUS:
      mqtt_app_set_event(MQTT_INIT_EVENT);
      break;
    default:
      break;
  }
}

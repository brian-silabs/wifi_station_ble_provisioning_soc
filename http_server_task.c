
#include "http_server_task.h"
#include "http_server_task_config.h"

#include "thread_safe_print.h"
#include "nwp_task.h"

#include "cmsis_os2.h"

#include "sl_http_server.h"
#include "sl_status.h"
#include "sl_constants.h"

#include <string.h>

#define SL_HTTP_SERVER_EVENT_QUEUE_SIZE 5
#define SL_HTTP_SERVER_EVENT_MAX_PAYLOAD_SIZE  254

#define http_server_set_dataless_event(x) http_server_set_event(x, NULL, 0)

typedef enum http_server_event_id_e
{
  HTTP_SERVER_INITIALIZED_EVENT =                          (1 << 1),
  HTTP_SERVER_UNKNOWN_EVENT =                              (1 << 31)
} http_server_event_id_t;

typedef struct http_server_event_msg_s{
  http_server_event_id_t event_id;
  uint8_t seq_num;
  uint8_t payload[SL_HTTP_SERVER_EVENT_MAX_PAYLOAD_SIZE];
} http_server_event_msg_t;

static sl_status_t buffered_request_handler(sl_http_server_t *handle, sl_http_server_request_t *req);

// RTOS Variables
const osThreadAttr_t http_server_thread_attributes = {
    .name       = "http_server_thread",
    .attr_bits  = 0,
    .cb_mem     = 0,
    .cb_size    = 0,
    .stack_mem  = 0,
    .stack_size = 2048,
    .priority   = osPriorityNormal,
    .tz_module  = 0,
    .reserved   = 0,
  };

osMessageQueueId_t  http_server_evt_queue_id;  // Event flags ID
static uint8_t      http_server_evt_queue_seq_num_g = 0;

static sl_http_server_t server_handle = { 0 };

static sl_http_server_handler_t request_handlers[4] = { { .uri = "/test", .handler = buffered_request_handler }
                                                    };

static char *request_type[5] = { [SL_HTTP_REQUEST_GET]    = "GET",
    [SL_HTTP_REQUEST_POST]   = "POST",
    [SL_HTTP_REQUEST_PUT]    = "PUT",
    [SL_HTTP_REQUEST_DELETE] = "DELETE",
    [SL_HTTP_REQUEST_HEAD]   = "HEAD" };

static char response[1025]            = { 0 };

void http_server_task(void *argument);

sl_status_t start_http_server_task_context(void)
{
    sl_status_t ret = SL_STATUS_OK;

    THREAD_SAFE_PRINT("HTTP Server Task Context Init Start\n");

    http_server_evt_queue_id = osMessageQueueNew(  SL_HTTP_SERVER_EVENT_QUEUE_SIZE,
                                            sizeof(http_server_event_msg_t),
                                            NULL);  // Create message queue
    if (http_server_evt_queue_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create http_server_evt_queue_id\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("HTTP Server Queue Creation Complete\n");

    osThreadId_t http_server_thread_id = osThreadNew((osThreadFunc_t)http_server_task, NULL, &http_server_thread_attributes);
    if (http_server_thread_id == NULL) {
        THREAD_SAFE_PRINT("Failed to create http_server_task\n");
        return SL_STATUS_FAIL;
    }
    THREAD_SAFE_PRINT("HTTP Server Task Startup Complete\n");
    
    THREAD_SAFE_PRINT("HTTP Server Task Context Init Done\n\n");
    return ret;
}

void http_server_set_event(uint32_t event_id, void *event_data, uint32_t event_data_len)
{
    http_server_event_msg_t event_msg;

    event_msg.event_id = event_id;
    event_msg.seq_num = http_server_evt_queue_seq_num_g;

    if(     (event_data != NULL)
        &&  (event_data_len > 0))
    {
        memcpy(event_msg.payload, event_data, event_data_len);
    } else {
        memset(event_msg.payload, 0, SL_HTTP_SERVER_EVENT_MAX_PAYLOAD_SIZE);
    }

    osStatus_t status = osMessageQueuePut(  http_server_evt_queue_id, 
                                            &event_msg, 
                                            0, // Message priority
                                            0); // Timeout - Return immediately

    if (status != osOK) {
        THREAD_SAFE_PRINT("Failed to send http_server event: 0x%lx\r\n", (uint32_t)status);
    } else {
        http_server_evt_queue_seq_num_g++;
    }
}

static void http_server_wait_event(http_server_event_msg_t *event_msg)
{
    osStatus_t status = osMessageQueueGet(  http_server_evt_queue_id, 
                                            event_msg, 
                                            NULL, 
                                            osWaitForever);

    if(status != osOK) {
        THREAD_SAFE_PRINT("Failed to get http_server event: 0x%lx\r\n", (uint32_t)status);
    }
}

sl_status_t http_server_start(void)
{
    sl_status_t ret = SL_STATUS_OK;

    ret = sl_http_server_start(&server_handle);

    return ret;
}

/*
 *********************************************************************************************************
 *                                         PRIVATE FUNCTIONS DEFINITIONS
 *********************************************************************************************************
 */
void http_server_task(void *argument)
{
    UNUSED_PARAMETER(argument);
    
    sl_status_t status                 = SL_STATUS_OK;
    sl_http_server_config_t server_config = { 0 };
    http_server_event_msg_t http_server_event_msg;

    status = nwp_access_request();
    THREAD_SAFE_PRINT("HTTP Server Acquiring NWP Semaphore\r\n");
    if (status != SL_STATUS_OK) {
        THREAD_SAFE_PRINT("\r\nFailed to acquire NWP semaphore: 0x%lx\r\n", status);
        return;
    }

    THREAD_SAFE_PRINT("HTTP Server Init\n");

    server_config.port             = HTTP_SERVER_PORT;
    server_config.default_handler  = NULL;
    server_config.handlers_list    = request_handlers;
    server_config.handlers_count   = 4;
    server_config.client_idle_time = 1;
  
    status = sl_http_server_init(&server_handle, &server_config);
    if (status != SL_STATUS_OK) {
      THREAD_SAFE_PRINT("HTTP server init failed: 0x%lX\r\n", status);
      return;
    }
    THREAD_SAFE_PRINT("HTTP server Init done\r\n");

     THREAD_SAFE_PRINT("HTTP Server Releasing NWP Semaphore\r\n");
     status = nwp_access_release();
     if (status != SL_STATUS_OK) {
         THREAD_SAFE_PRINT("\r\nFailed to release NWP semaphore: 0x%lx\r\n", status);
         return;
     }

    // Initialize the message queue sequence number
    http_server_evt_queue_seq_num_g = 0;

    //Consider the HTTP Server task as initialized
    http_server_set_dataless_event(HTTP_SERVER_INITIALIZED_EVENT);

    while (true) 
    {
        http_server_wait_event(&http_server_event_msg);
        switch (http_server_event_msg.event_id) {
            case HTTP_SERVER_INITIALIZED_EVENT:
            {
                THREAD_SAFE_PRINT("HTTP_SERVER_INITIALIZED_EVENT\n");
            } break;
            default :
            break;
        }
        
    } // while (true)
}

sl_status_t buffered_request_handler(sl_http_server_t *handle, sl_http_server_request_t *req)
{
  sl_http_recv_req_data_t recvData        = { 0 };
  sl_http_server_response_t http_response = { 0 };
  sl_http_header_t request_headers[5]     = { 0 };
  sl_http_header_t header                 = { .key = "Server", .value = "SI917-HTTPServer" };

  THREAD_SAFE_PRINT("Got request [%s] of type : %s with data length : %lu\n",
         req->uri.path,
         request_type[req->type],
         req->request_data_length);
  if (req->request_data_length > 0) {
    recvData.request       = req;
    recvData.buffer        = (uint8_t *)response;
    recvData.buffer_length = 1024;

    sl_http_server_read_request_data(handle, &recvData);
    response[recvData.received_data_length] = 0;
    THREAD_SAFE_PRINT("Got request data as : %s\n", response);
  }

  THREAD_SAFE_PRINT("Got request query parameter count : %u\n", req->uri.query_parameter_count);
  if (req->uri.query_parameter_count > 0) {
    for (int i = 0; i < req->uri.query_parameter_count; i++) {
      THREAD_SAFE_PRINT("query: %s, value: %s\n", req->uri.query_parameters[i].query, req->uri.query_parameters[i].value);
    }
  }

  THREAD_SAFE_PRINT("Got header count : %u\n", req->request_header_count);
  sl_http_server_get_request_headers(handle, req, request_headers, 5);

  int length = (req->request_header_count > 5) ? 5 : req->request_header_count;
  for (int i = 0; i < length; i++) {
    THREAD_SAFE_PRINT("Key: %s, Value: %s\n", request_headers[i].key, request_headers[i].value);
  }

  // Set the response code to 200 (OK)
  http_response.response_code = SL_HTTP_RESPONSE_OK;

  // Set the content type to plain text
  http_response.content_type = SL_HTTP_CONTENT_TYPE_TEXT_PLAIN;
  http_response.headers      = &header;
  http_response.header_count = 1;

  // Set the response data to "Hello, World!"
  char *response_data                = "Hello, World!";
  http_response.data                 = (uint8_t *)response_data;
  http_response.current_data_length  = strlen(response_data);
  http_response.expected_data_length = http_response.current_data_length;
  sl_http_server_send_response(handle, &http_response);

  return SL_STATUS_OK;
}
#ifndef HTTP_SERVER_TASK_H
#define HTTP_SERVER_TASK_H

#include "sl_status.h"

/**
 * @brief Start the HTTP Server task context.
 *
 * @return sl_status_t The status of the operation.
 */
sl_status_t start_http_server_task_context(void);

/**
 * @brief Start the HTTP server.
 *
 * @return sl_status_t The status of the HTTP server startup operation.
 */
sl_status_t http_server_start(void);

#endif /// HTTP_SERVER_TASK_H
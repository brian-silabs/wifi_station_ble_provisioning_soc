#ifndef THREAD_SAFE_PRINT_H
#define THREAD_SAFE_PRINT_H

#include <stdio.h>

#include "sl_status.h"
#include "cmsis_os2.h"


extern osMutexId_t debug_prints_mutex;

#define THREAD_SAFE_PRINT(...)                                \
    {                                                         \
        if (NULL != debug_prints_mutex)                       \
        {                                                     \
            osMutexAcquire(debug_prints_mutex, 0xFFFFFFFFUL); \
            printf(__VA_ARGS__);                              \
            osMutexRelease(debug_prints_mutex);               \
        }                                                     \
    }

/**
 * @brief Initialize the thread-safe print functionality.
 *
 * This function initializes the mutex used to ensure thread-safe printing.
 *
 * @return SL_STATUS_OK on success, or an error status on failure.
 */
sl_status_t thread_safe_print_init(void);

#endif // THREAD_SAFE_PRINT_H

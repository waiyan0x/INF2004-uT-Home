#ifndef UTHOME_HAL_STATUS_H
#define UTHOME_HAL_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Common status code returned by every HAL operation.
 *
 * Shared across the I2C, SPI, UART, GPIO, time and storage contracts, so this
 * enumeration is a team-owned interface. Change it only by agreement.
 */
typedef enum {
    HAL_OK = 0,           /* Operation completed successfully */
    HAL_ERR_INVALID_ARG,  /* Null pointer, zero length, or out-of-range field */
    HAL_ERR_NOT_INIT,     /* Bus or device used before a successful init */
    HAL_ERR_TIMEOUT,      /* Operation did not complete within the timeout */
    HAL_ERR_NACK,         /* I2C address or data byte was not acknowledged */
    HAL_ERR_BUS,          /* Bus-level fault: arbitration loss, stuck line, transfer error */
    HAL_ERR_UNSUPPORTED,  /* Requested protocol or mode not provided by this port */
    HAL_ERR_NO_RESOURCE,  /* Static handle pool exhausted */
} hal_status_t;

/* Timeout conventions, in milliseconds, for every blocking HAL call. */
#define HAL_NO_WAIT      (0u)
#define HAL_WAIT_FOREVER (0xFFFFFFFFu)

#ifdef __cplusplus
}
#endif

#endif /* UTHOME_HAL_STATUS_H */

/**
 * @file c3uart.c
 * @brief Use the existing native rings without installing another UART driver.
 */
#include "c3uart.h"
#include <tk/tkernel.h>
#include "dev_ser.h"
#include "ser_cnf.h"
#include "workport.h"

typedef struct
{
    ID   h_device;
    ID   h_mutex;
    bool b_poisoned;
} c3uart_state_t;

static int open_port (void * p_context, const uartio_config_t * p_config);
static int close_port (void * p_context);
static int read_bytes (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_count);
static int write_bytes (void *          p_context,
                        const uint8_t * p_data,
                        size_t          size,
                        size_t *        p_count);
static int flush_bytes (void * p_context);
static int now_ms (void * p_context, uint64_t * p_now);
static int set_attributes (const uartio_config_t * p_config);
static int close_device (void);
static int lock_port (void * p_context);
static int unlock_port (int result);
static int read_fault (void);
static int available (bool b_transmit, size_t * p_count);
static int receive (uint8_t * p_data, size_t capacity, size_t * p_count);
static const uartio_ops_t g_operations = {.p_open  = open_port,
                                          .p_close = close_port,
                                          .p_read  = read_bytes,
                                          .p_write = write_bytes,
                                          .p_flush = flush_bytes,
                                          .p_now   = now_ms};
static c3uart_state_t     g_state      = {0};
static uartio_t g_port = {.p_operations = &g_operations, .p_context = &g_state};
static const UB g_name[] = {'s', 'e', 'r', 'a', 0U};

/** @brief Borrow the singleton without touching the device or kernel. */
uartio_t * c3uart_port (void)
{
    return &g_port;
}

/** @brief Apply cached native attributes through one checked close/reopen. */
static int open_port (void * p_context, const uartio_config_t * p_config)
{
    if ((&g_state != p_context) || (!rt_port_is_owner()) ||
        (g_state.b_poisoned) || (0 != g_state.h_mutex) ||
        (1U != p_config->port) || (DEVCNF_SER_TX_PIN != p_config->tx_gpio) ||
        (DEVCNF_SER_RX_PIN != p_config->rx_gpio) || (p_config->baud < 9600U) ||
        (p_config->baud > 460800U) ||
        (p_config->rx_buffer_bytes > (DEVCONF_SER_BUFFSIZE - 1U)))
    {
        return UARTIO_INVALID;
    }
    const T_CMTX mutex   = {.mtxatr = TA_INHERIT};
    const ID     h_mutex = tk_cre_mtx(&mutex);
    if (h_mutex <= 0)
    {
        return UARTIO_IO;
    }
    g_state.h_mutex   = h_mutex;
    const ID h_device = tk_opn_dev(g_name, TD_UPDATE | TD_EXCL);
    if (h_device <= 0)
    {
        return UARTIO_IO;
    }
    g_state.h_device = h_device;
    int result       = set_attributes(p_config);
    if (0 == result)
    {
        result = close_device();
    }
    if (0 == result)
    {
        const ID h_reopened = tk_opn_dev(g_name, TD_UPDATE | TD_EXCL);
        if (h_reopened <= 0)
        {
            return UARTIO_IO;
        }
        g_state.h_device = h_reopened;
    }
    return result;
}

/** @brief Workers are dormant before lifecycle cleanup; retain failed state. */
static int close_port (void * p_context)
{
    if ((&g_state != p_context) || (!rt_port_is_owner()) ||
        (g_state.b_poisoned))
    {
        return UARTIO_STATE;
    }
    if ((0 < g_state.h_device) && (0 != close_device()))
    {
        return UARTIO_IO;
    }
    if (0 < g_state.h_mutex)
    {
        if (E_OK != tk_del_mtx(g_state.h_mutex))
        {
            return UARTIO_IO;
        }
        g_state.h_mutex = 0;
    }
    return 0;
}

/** @brief Reject a whole batch if errors are observed before or after RX. */
static int read_bytes (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_count)
{
    const int locked = lock_port(p_context);
    if (0 != locked)
    {
        return locked;
    }
    int result = read_fault();
    if (0 == result)
    {
        result          = receive(p_data, capacity, p_count);
        const int fault = read_fault();
        if (0 != fault)
        {
            result = fault;
        }
    }
    return unlock_port(result);
}

/** @brief Reserve the native ring's empty slot and enqueue only free space. */
static int write_bytes (void *          p_context,
                        const uint8_t * p_data,
                        size_t          size,
                        size_t *        p_count)
{
    const int locked = lock_port(p_context);
    if (0 != locked)
    {
        return locked;
    }
    size_t space  = 0U;
    int    result = available(true, &space);
    if ((0 == result) && (0U != space))
    {
        const size_t count    = (size < space) ? size : space;
        SZ           accepted = 0;
        /* count <=64; SZ is the pinned kernel's signed 32-bit size. */
        const ER written =
            tk_swri_dev(g_state.h_device, 0, p_data, (SZ)count, &accepted);
        if ((accepted < 0) || (accepted > (SZ)count))
        {
            result = UARTIO_IO;
        }
        else
        {
            *p_count = (size_t)accepted;
            result   = (E_OK == written) ? 0 : UARTIO_IO;
        }
    }
    return unlock_port(result);
}

/** @brief Drain only the entry snapshot, even while more input arrives. */
static int flush_bytes (void * p_context)
{
    const int locked = lock_port(p_context);
    if (0 != locked)
    {
        return locked;
    }
    size_t pending = 0U;
    int    result  = available(false, &pending);
    while ((0 == result) && (0U != pending))
    {
        uint8_t      bytes[UTHOME_UART_BYTES] = {0};
        const size_t size = (pending < sizeof(bytes)) ? pending : sizeof(bytes);
        size_t       count = 0U;
        result             = receive(bytes, size, &count);
        if ((0 == result) && (0U == count))
        {
            result = UARTIO_IO;
        }
        pending -= count;
    }
    /* Clearing an observed hardware fault is part of explicit discard. */
    const int fault = read_fault();
    if (UARTIO_IO == fault)
    {
        result = fault;
    }
    return unlock_port(result);
}

/** @brief Kernel operating time matches every other typed sample. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    if ((&g_state != p_context) || (g_state.h_device <= 0) ||
        (g_state.b_poisoned))
    {
        return UARTIO_STATE;
    }
    return (0 == rt_port_now_ms(p_now)) ? 0 : UARTIO_IO;
}

/** @brief The native driver caches settings until its next START operation. */
static int set_attributes (const uartio_config_t * p_config)
{
    /* Encodings come from the pinned port: word-length = 8 minus data bits. */
    UW mode = 8U - p_config->data_bits;
    if (2U == p_config->stop_bits)
    {
        mode |= DEV_SER_MODE_2STOP;
    }
    if (1U == p_config->parity)
    {
        mode |= DEV_SER_MODE_PODD;
    }
    else if (2U == p_config->parity)
    {
        mode |= DEV_SER_MODE_PEVEN;
    }
    else
    {
        /* No parity or hardware flow control. */
    }
    const UW  speed   = p_config->baud;
    const TMO timeout = TMO_POL;
    SZ        size    = 0;
    if ((E_OK !=
         tk_swri_dev(
             g_state.h_device, TDN_SER_MODE, &mode, sizeof(mode), &size)) ||
        (sizeof(mode) != size) ||
        (E_OK !=
         tk_swri_dev(
             g_state.h_device, TDN_SER_SPEED, &speed, sizeof(speed), &size)) ||
        (sizeof(speed) != size) ||
        (E_OK != tk_swri_dev(g_state.h_device,
                             TDN_SER_SNDTMO,
                             &timeout,
                             sizeof(timeout),
                             &size)) ||
        (sizeof(timeout) != size) ||
        (E_OK != tk_swri_dev(g_state.h_device,
                             TDN_SER_RCVTMO,
                             &timeout,
                             sizeof(timeout),
                             &size)) ||
        (sizeof(timeout) != size))
    {
        return UARTIO_IO;
    }
    return 0;
}

/** @brief Never retry a descriptor that native close may have already freed. */
static int close_device (void)
{
    const ER closed  = tk_cls_dev(g_state.h_device, 0U);
    g_state.h_device = 0;
    if (E_OK != closed)
    {
        g_state.b_poisoned = true;
        return UARTIO_IO;
    }
    return 0;
}

/** @brief Contention returns immediately; no driver call runs recursively. */
static int lock_port (void * p_context)
{
    if ((&g_state != p_context) || (g_state.h_device <= 0) ||
        (g_state.b_poisoned))
    {
        return UARTIO_STATE;
    }
    const ER locked = tk_loc_mtx(g_state.h_mutex, TMO_POL);
    return (E_OK == locked) ? 0
                            : ((E_TMOUT == locked) ? UARTIO_AGAIN : UARTIO_IO);
}

/** @brief Preserve an unlock failure as a transport error. */
static int unlock_port (int result)
{
    return (E_OK == tk_unl_mtx(g_state.h_mutex)) ? result : UARTIO_IO;
}

/** @brief Native COMERR reads clear the reported flags; count observed epochs.
 */
static int read_fault (void)
{
    UW faults = 0U;
    SZ size   = 0;
    if ((E_OK != tk_srea_dev(g_state.h_device,
                             TDN_SER_COMERR,
                             &faults,
                             sizeof(faults),
                             &size)) ||
        (sizeof(faults) != size))
    {
        return UARTIO_IO;
    }
    if (0U != (faults & (DEV_SER_ERR_ROVR | DEV_SER_ERR_OE)))
    {
        return UARTIO_OVERFLOW;
    }
    return (0U == faults) ? 0 : UARTIO_FRAME;
}

/** @brief Query first so native RX never sleeps or leaves a wait-task ID. */
static int available (bool b_transmit, size_t * p_count)
{
    SZ       count  = 0;
    uint8_t  byte   = 0U;
    const ER result = b_transmit
                          ? tk_swri_dev(g_state.h_device, 0, &byte, 0, &count)
                          : tk_srea_dev(g_state.h_device, 0, &byte, 0, &count);
    if ((E_OK != result) || (count < 0) || (count > DEVCONF_SER_BUFFSIZE))
    {
        return UARTIO_IO;
    }
    if (b_transmit && (count > 0))
    {
        --count;
    }
    if (count >= DEVCONF_SER_BUFFSIZE)
    {
        return UARTIO_IO;
    }
    /* Validated native ring size is nonnegative and fits size_t. */
    *p_count = (size_t)count;
    return 0;
}

/** @brief Exactly one serialized consumer means snapshot bytes cannot vanish.
 */
static int receive (uint8_t * p_data, size_t capacity, size_t * p_count)
{
    size_t pending = 0U;
    int    result  = available(false, &pending);
    if ((0 == result) && (0U != pending))
    {
        const size_t count    = (capacity < pending) ? capacity : pending;
        SZ           received = 0;
        /* count <=64, so conversion to the kernel SZ is exact. */
        if ((E_OK !=
             tk_srea_dev(g_state.h_device, 0, p_data, (SZ)count, &received)) ||
            (received != (SZ)count))
        {
            result = UARTIO_IO;
        }
        else
        {
            *p_count = count;
        }
    }
    return result;
}

/* End of file. */


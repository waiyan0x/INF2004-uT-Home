/**
 * @file c3uart.c
 * @brief Use the existing native rings without installing another UART driver.
 */
#include "c3uart.h"
#include "c3cost.h"
#include <tk/tkernel.h>
#include "c3umem.h"
#include "dev_ser.h"
#include "ser.h"
#include "workport.h"

typedef struct
{
    ID   h_device;
    ID   h_mutex;
    bool b_poisoned;
    bool b_attempted;
    bool b_ready;
    bool b_owned;
    bool b_active;
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
static int reset_buffers (void);
static int discard_receive (void);
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

/** @brief Provision once before configuration restore or worker activation. */
int c3uart_init (void)
{
    if (!rt_port_is_owner())
    {
        return UARTIO_STATE;
    }
    if (g_state.b_attempted)
    {
        return g_state.b_ready ? 0 : UARTIO_STATE;
    }
    g_state.b_attempted = true;
    if (0 != c3umem_seal())
    {
        return UARTIO_IO;
    }
    const T_CMTX mutex = {.mtxatr = TA_INHERIT};
    g_state.h_mutex    = tk_cre_mtx(&mutex);
    if (g_state.h_mutex <= 0)
    {
        return UARTIO_IO;
    }
    g_state.h_device = tk_opn_dev(g_name, TD_UPDATE | TD_EXCL);
    if (g_state.h_device <= 0)
    {
        return UARTIO_IO;
    }
    /* Native open initializes rings and starts UART. Leave it stopped. */
    if (0 != reset_buffers())
    {
        return UARTIO_IO;
    }
    g_state.b_ready = true;
    return 0;
}

/** @brief Apply cached attributes with START, retaining the native descriptor.
 */
static int open_port (void * p_context, const uartio_config_t * p_config)
{
    if ((&g_state != p_context) || (!rt_port_is_owner()) ||
        (!g_state.b_ready) || (g_state.b_poisoned) || (g_state.b_owned))
    {
        return UARTIO_STATE;
    }
    if ((1U != p_config->port) || (DEVCNF_SER_TX_PIN != p_config->tx_gpio) ||
        (DEVCNF_SER_RX_PIN != p_config->rx_gpio) || (p_config->baud < 9600U) ||
        (p_config->baud > 460800U) ||
        (p_config->rx_buffer_bytes > (DEVCONF_SER_BUFFSIZE - 1U)))
    {
        return UARTIO_INVALID;
    }
    const ER locked = tk_loc_mtx(g_state.h_mutex, TMO_POL);
    if (E_OK != locked)
    {
        return (E_TMOUT == locked) ? UARTIO_AGAIN : UARTIO_IO;
    }
    g_state.b_owned = true;
    int result      = reset_buffers();
    if (0 == result)
    {
        result = set_attributes(p_config);
    }
    if (0 == result)
    {
        result = (E_OK == dev_ser_llctl(0U, LLD_SER_START, 0U)) ? 0 : UARTIO_IO;
    }
    if (0 != result)
    {
        /* Retain the claim and make one bounded stop attempt after failure. */
        if (E_OK != dev_ser_llctl(0U, LLD_SER_STOP, 0U))
        {
            g_state.b_poisoned = true;
        }
    }
    g_state.b_active = (0 == result);
    return unlock_port(result);
}

/** @brief Release logical ownership only after bounded discard succeeds. */
static int close_port (void * p_context)
{
    if ((&g_state != p_context) ||
        ((!rt_port_is_owner()) && (!rt_port_is_cleanup_owner())) ||
        (!g_state.b_ready) || (g_state.b_poisoned))
    {
        return UARTIO_STATE;
    }
    const ER locked = tk_loc_mtx(g_state.h_mutex, TMO_POL);
    if (E_OK != locked)
    {
        return (E_TMOUT == locked) ? UARTIO_AGAIN : UARTIO_IO;
    }
    g_state.b_active = false;
    const int result = unlock_port(reset_buffers());
    if (0 == result)
    {
        g_state.b_owned = false;
    }
    return result;
}

/** @brief Reject a whole batch if errors are observed before or after RX. */
static int read_bytes (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_count)
{
    c3cost_t cost = {0};
    c3cost_begin(&cost);
    const int locked = lock_port(p_context);
    if (0 != locked)
    {
        c3cost_end(&cost, 0U, UARTIO_TIMEOUT == locked, locked < 0);
        return locked;
    }
    c3cost_acquired(&cost);
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
    const int final = unlock_port(result);
    c3cost_end(&cost,
               (0 == result) ? *p_count : 0U,
               UARTIO_TIMEOUT == final,
               final < 0);
    return final;
}

/** @brief Reserve the native ring's empty slot and enqueue only free space. */
static int write_bytes (void *          p_context,
                        const uint8_t * p_data,
                        size_t          size,
                        size_t *        p_count)
{
    c3cost_t cost = {0};
    c3cost_begin(&cost);
    const int locked = lock_port(p_context);
    if (0 != locked)
    {
        c3cost_end(&cost, 0U, UARTIO_TIMEOUT == locked, locked < 0);
        return locked;
    }
    c3cost_acquired(&cost);
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
    const int final = unlock_port(result);
    c3cost_end(&cost,
               (0 == result) ? *p_count : 0U,
               UARTIO_TIMEOUT == final,
               final < 0);
    return final;
}

/** @brief Drain only the entry snapshot, even while more input arrives. */
static int flush_bytes (void * p_context)
{
    c3cost_t cost = {0};
    c3cost_begin(&cost);
    const int locked = lock_port(p_context);
    if (0 != locked)
    {
        c3cost_end(&cost, 0U, UARTIO_TIMEOUT == locked, locked < 0);
        return locked;
    }
    c3cost_acquired(&cost);
    const int result = unlock_port(discard_receive());
    /* Discarded bytes are not successfully delivered payload. */
    c3cost_end(&cost, 0U, UARTIO_TIMEOUT == result, result < 0);
    return result;
}

/** @brief Discard at most the fixed native receive capacity. */
static int discard_receive (void)
{
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
    return result;
}

/** @brief Kernel operating time matches every other typed sample. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    if ((&g_state != p_context) || (!g_state.b_active) || (!g_state.b_owned) ||
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

/** @brief Reset hardware, mask interrupts, then discard both software rings. */
static int reset_buffers (void)
{
    /* STOP masks/clears all sources. START resets both hardware FIFOs and
     * enables RX/error only, never TX-empty. STOP again before ring access.
     * An RX ISR in that short interval is included in the later snapshot.
     * A byte already on the wire cannot be recalled. This is abort, not drain.
     */
    if (E_OK != dev_ser_llctl(0U, LLD_SER_STOP, 0U))
    {
        return UARTIO_IO;
    }
    const ER started = dev_ser_llctl(0U, LLD_SER_START, 0U);
    const ER stopped = dev_ser_llctl(0U, LLD_SER_STOP, 0U);
    if ((E_OK != started) || (E_OK != stopped))
    {
        return UARTIO_IO;
    }
    /* Native reads toggle the CPU interrupt, but all peripheral interrupt
     * sources stay masked. Only this wrapper may access the native device. */
    UW byte = 0U;
    for (size_t index = 0U; index < DEVCONF_SER_BUFFSIZE; ++index)
    {
        if (!dev_ser_get_snddat(0U, &byte))
        {
            return discard_receive();
        }
    }
    return UARTIO_IO;
}

/** @brief Contention returns immediately; no driver call runs recursively. */
static int lock_port (void * p_context)
{
    if ((&g_state != p_context) || (!g_state.b_active) || (!g_state.b_owned) ||
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
    if (E_OK != tk_unl_mtx(g_state.h_mutex))
    {
        g_state.b_poisoned = true;
        g_state.b_active   = false;
        /* The mutex state is uncertain: stop IRQ-driven communication too.
         * Failure remains terminal; never release the caller's claim. */
        (void)dev_ser_llctl(0U, LLD_SER_STOP, 0U);
        return UARTIO_IO;
    }
    return result;
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


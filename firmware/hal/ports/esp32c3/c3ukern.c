/**
 * @file c3ukern.c
 * @brief Contain the pinned native UART ABI and permanent device handles.
 */
#include "c3ukern.h"
#include <tk/tkernel.h>
#include "c3umem.h"
#include "dev_ser.h"
#include "ser.h"

static ID       gh_device = 0;
static ID       gh_mutex  = 0;
static const UB g_name[]  = {'s', 'e', 'r', 'a', 0U};

/** @brief Create once as directed by c3uart; retain partial resources. */
int c3ukern_init (void)
{
    if (0 != c3umem_seal())
    {
        return UARTIO_IO;
    }
    const T_CMTX mutex = {.mtxatr = TA_INHERIT};
    gh_mutex           = tk_cre_mtx(&mutex);
    if (gh_mutex <= 0)
    {
        return UARTIO_IO;
    }
    gh_device = tk_opn_dev(g_name, TD_UPDATE | TD_EXCL);
    return (gh_device > 0) ? 0 : UARTIO_IO;
}

/** @brief Admit only the pinned device and its fixed receive capacity. */
bool c3ukern_valid (const uartio_config_t * p_config)
{
    return (1U == p_config->port) && (DEVCNF_SER_TX_PIN == p_config->tx_gpio) &&
           (DEVCNF_SER_RX_PIN == p_config->rx_gpio) &&
           (p_config->baud >= 9600U) && (p_config->baud <= 460800U) &&
           (p_config->rx_buffer_bytes <= (DEVCONF_SER_BUFFSIZE - 1U));
}

/** @brief Include the native ring's reserved empty slot in the reset bound. */
size_t c3ukern_capacity (void)
{
    return DEVCONF_SER_BUFFSIZE;
}

/** @brief Poll the permanent priority-inheritance mutex. */
int c3ukern_lock (void)
{
    const ER result = tk_loc_mtx(gh_mutex, TMO_POL);
    return (E_OK == result) ? 0
                            : ((E_TMOUT == result) ? UARTIO_AGAIN : UARTIO_IO);
}

/** @brief Preserve unlock failures so the lifecycle can poison the port. */
int c3ukern_unlock (void)
{
    return (E_OK == tk_unl_mtx(gh_mutex)) ? 0 : UARTIO_IO;
}

/** @brief Reset hardware FIFOs and enable RX/error interrupts. */
int c3ukern_start (void)
{
    return (E_OK == dev_ser_llctl(0U, LLD_SER_START, 0U)) ? 0 : UARTIO_IO;
}

/** @brief Mask and clear all peripheral interrupt sources. */
int c3ukern_stop (void)
{
    return (E_OK == dev_ser_llctl(0U, LLD_SER_STOP, 0U)) ? 0 : UARTIO_IO;
}

/** @brief Remove one pending TX byte while peripheral IRQs are masked. */
bool c3ukern_discard_tx (void)
{
    UW byte = 0U;
    return 0U != dev_ser_get_snddat(0U, &byte);
}

/** @brief Read an already bounded snapshot with exactly one native call. */
int c3ukern_read (uint8_t * p_data, size_t size, size_t * p_count)
{
    SZ received = 0;
    /* c3uart bounds size to 64, within the native signed 32-bit size. */
    if ((E_OK != tk_srea_dev(gh_device, 0, p_data, (SZ)size, &received)) ||
        (received != (SZ)size))
    {
        return UARTIO_IO;
    }
    *p_count = size;
    return 0;
}

/** @brief Report a bounded partial acceptance even if the native write fails.
 */
int c3ukern_write (const uint8_t * p_data, size_t size, size_t * p_count)
{
    SZ accepted = 0;
    /* c3uart bounds size to 64, within the native signed 32-bit size. */
    const ER result = tk_swri_dev(gh_device, 0, p_data, (SZ)size, &accepted);
    if ((accepted < 0) || (accepted > (SZ)size))
    {
        return UARTIO_IO;
    }
    *p_count = (size_t)accepted;
    return (E_OK == result) ? 0 : UARTIO_IO;
}

/** @brief Cache checked line attributes for the next START. */
int c3ukern_attributes (const uartio_config_t * p_config)
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
         tk_swri_dev(gh_device, TDN_SER_MODE, &mode, sizeof(mode), &size)) ||
        (sizeof(mode) != size) ||
        (E_OK !=
         tk_swri_dev(gh_device, TDN_SER_SPEED, &speed, sizeof(speed), &size)) ||
        (sizeof(speed) != size) ||
        (E_OK !=
         tk_swri_dev(
             gh_device, TDN_SER_SNDTMO, &timeout, sizeof(timeout), &size)) ||
        (sizeof(timeout) != size) ||
        (E_OK !=
         tk_swri_dev(
             gh_device, TDN_SER_RCVTMO, &timeout, sizeof(timeout), &size)) ||
        (sizeof(timeout) != size))
    {
        return UARTIO_IO;
    }
    return 0;
}

/** @brief Convert and clear native fault flags. */
int c3ukern_fault (void)
{
    UW faults = 0U;
    SZ size   = 0;
    if ((E_OK !=
         tk_srea_dev(
             gh_device, TDN_SER_COMERR, &faults, sizeof(faults), &size)) ||
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

/** @brief Bound native counts and reserve the empty TX slot. */
int c3ukern_available (bool b_transmit, size_t * p_count)
{
    SZ       count  = 0;
    uint8_t  byte   = 0U;
    const ER result = b_transmit ? tk_swri_dev(gh_device, 0, &byte, 0, &count)
                                 : tk_srea_dev(gh_device, 0, &byte, 0, &count);
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

/* End of file. */


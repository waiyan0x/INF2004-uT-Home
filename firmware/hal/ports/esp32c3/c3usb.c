/**
 * @file c3usb.c
 * @brief Poll the existing USB FIFO without an interrupt or blocking driver.
 */
#include "c3usb.h"
#include "hal/usb_serial_jtag_ll.h"
#include "workport.h"
static bool gb_open = false;
static int read_bytes (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_count);
static int write_bytes (void *          p_context,
                        const uint8_t * p_data,
                        size_t          size,
                        size_t *        p_count);
static const cfgpump_ops_t g_operations = {.p_read  = read_bytes,
                                           .p_write = write_bytes};

/** @brief Refuse competing interrupt ownership; flush the last boot text. */
int c3usb_open (void)
{
    if ((!rt_port_is_owner()) || (gb_open) ||
        (0U != usb_serial_jtag_ll_get_intr_ena_status()))
    {
        return -1;
    }
    usb_serial_jtag_ll_txfifo_flush();
    gb_open = true;
    return 0;
}

/** @brief Return immutable callbacks; no hardware access. */
const cfgpump_ops_t * c3usb_operations (void)
{
    return &g_operations;
}

/** @brief Read at most one bounded chunk, with no wait for new bytes. */
static int read_bytes (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_count)
{
    if ((NULL != p_context) || (!gb_open) || (!rt_port_is_owner()) ||
        (NULL == p_data) || (NULL == p_count) || (0U == capacity) ||
        (capacity > UTHOME_CFGPUMP_CHUNK))
    {
        return -1;
    }
    /* Capacity is bounded to 64, inside both vendor uint32 and int ranges. */
    const int count =
        usb_serial_jtag_ll_read_rxfifo(p_data, (uint32_t)capacity);
    if ((count < 0) || ((size_t)count > capacity))
    {
        return -1;
    }
    *p_count = (size_t)count;
    return 0;
}

/** @brief Flush each short packet; a full FIFO returns zero, never spins. */
static int write_bytes (void *          p_context,
                        const uint8_t * p_data,
                        size_t          size,
                        size_t *        p_count)
{
    if ((NULL != p_context) || (!gb_open) || (!rt_port_is_owner()) ||
        (NULL == p_data) || (NULL == p_count) || (0U == size) ||
        (size > UTHOME_CFGWIRE_REPLY))
    {
        return -1;
    }
    /* Size <=48 avoids a full 64-byte USB packet needing a terminating ZLP. */
    const int count = usb_serial_jtag_ll_write_txfifo(p_data, (uint32_t)size);
    if ((count < 0) || ((size_t)count > size))
    {
        return -1;
    }
    *p_count = (size_t)count;
    if (0 != count)
    {
        usb_serial_jtag_ll_txfifo_flush();
    }
    return 0;
}

/* End of file. */


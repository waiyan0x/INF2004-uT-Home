/**
 * @file c3i2c.c
 * @brief Serialize finite FIFO transfers using the pinned SDK register layer.
 */
#include "c3i2c.h"
#include "c3pin.h"

#include <string.h>
#include <tk/tkernel.h>
#include "workport.h"
#include "esp_private/esp_clk.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"
#include "hal/i2c_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"

typedef struct
{
    i2cbus_config_t config;
    ID              h_mutex;
    uint32_t        source_hz;
    uint32_t        pins;
    bool            b_open;
} c3i2c_state_t;

static c3i2c_state_t g_state = {0};
static i2cbus_t      g_bus   = {0};

static int transfer (void * p_context, const i2cbus_request_t * p_request);
static int now_ms (void * p_context, uint64_t * p_now);
static int
delay_us (void * p_context, uint32_t duration_us, uint64_t deadline_ms);
static bool valid_pin (uint8_t pin);
static bool idle_owner (void);
static void setup_pin (uint8_t pin, uint32_t output, uint32_t input);
static void release_pin (uint8_t pin);
static void reset_controller (void);
static void load_commands (const i2cbus_request_t * p_request);
static void
command (uint32_t slot, uint32_t operation, uint32_t count, bool b_last_read);
static int wait_complete (uint64_t deadline_ms);
static int exchange (const i2cbus_request_t * p_request);

static const i2cbus_ops_t g_operations = {
    .p_transfer = transfer, .p_now = now_ms, .p_delay = delay_us};

/** @brief Create one inherited-priority mutex, then configure the bus pins. */
int c3i2c_open (const i2cbus_config_t * p_config, i2cbus_t ** pp_bus)
{
    if ((!idle_owner()) || (NULL == p_config) || (NULL == pp_bus) ||
        (g_state.b_open) || (0U != p_config->port) ||
        (!valid_pin(p_config->sda_gpio)) || (!valid_pin(p_config->scl_gpio)) ||
        (p_config->sda_gpio == p_config->scl_gpio) ||
        (p_config->clock_hz < 10000U) || (p_config->clock_hz > 400000U))
    {
        return I2CBUS_INVALID;
    }
    const int source_hz = esp_clk_xtal_freq();
    if (source_hz <= 0)
    {
        return I2CBUS_IO;
    }
    const uint32_t pins = (UINT32_C(1) << p_config->sda_gpio) |
                          (UINT32_C(1) << p_config->scl_gpio);
    if (0 != c3pin_reserve(pins, &g_bus))
    {
        return I2CBUS_STATE;
    }
    const T_CMTX mutex   = {.mtxatr = TA_INHERIT};
    const ID     h_mutex = tk_cre_mtx(&mutex);
    if (h_mutex <= 0)
    {
        const int released = c3pin_release(pins, &g_bus);
        return (0 == released) ? I2CBUS_IO : I2CBUS_STATE;
    }
    g_state.config  = *p_config;
    g_state.h_mutex = h_mutex;
    g_state.pins    = pins;
    /* Positive int source frequency fits the target's uint32_t. */
    g_state.source_hz = (uint32_t)source_hz;
    reset_controller();
    setup_pin(p_config->sda_gpio, I2CEXT0_SDA_OUT_IDX, I2CEXT0_SDA_IN_IDX);
    setup_pin(p_config->scl_gpio, I2CEXT0_SCL_OUT_IDX, I2CEXT0_SCL_IN_IDX);
    g_state.b_open = true;
    g_bus   = (i2cbus_t){.p_operations = &g_operations, .p_context = &g_state};
    *pp_bus = &g_bus;
    return 0;
}

/** @brief Release hardware only after workers and device claims are gone. */
int c3i2c_close (i2cbus_t * p_bus)
{
    if ((!idle_owner()) || (&g_bus != p_bus) || (!g_state.b_open) ||
        (i2cbus_has_claims(p_bus)))
    {
        return I2CBUS_STATE;
    }
    if (E_OK != tk_del_mtx(g_state.h_mutex))
    {
        return I2CBUS_IO;
    }
    i2c_dev_t * p_hardware = I2C_LL_GET_HW(0);
    i2c_ll_disable_intr_mask(p_hardware, UINT32_MAX);
    i2c_ll_enable_controller_clock(p_hardware, false);
    release_pin(g_state.config.sda_gpio);
    release_pin(g_state.config.scl_gpio);
    PERIPH_RCC_ATOMIC()
    {
        i2c_ll_enable_bus_clock(0, false);
    }
    if (0 != c3pin_release(g_state.pins, &g_bus))
    {
        return I2CBUS_STATE;
    }
    g_state = (c3i2c_state_t){0};
    g_bus   = (i2cbus_t){0};
    return 0;
}

/** @brief Budget mutex contention and the hardware transaction together. */
static int transfer (void * p_context, const i2cbus_request_t * p_request)
{
    if ((&g_state != p_context) || (!g_state.b_open))
    {
        return I2CBUS_STATE;
    }
    uint64_t now = 0U;
    if (0 != rt_port_now_ms(&now))
    {
        return I2CBUS_STATE;
    }
    if ((now >= p_request->deadline_ms) ||
        ((p_request->deadline_ms - now) > UINT32_C(2147483647)))
    {
        return I2CBUS_TIMEOUT;
    }
    /* Remaining time was checked to fit the positive kernel TMO range. */
    const ER locked =
        tk_loc_mtx(g_state.h_mutex, (TMO)(p_request->deadline_ms - now));
    if (E_OK != locked)
    {
        return (E_TMOUT == locked) ? I2CBUS_TIMEOUT : I2CBUS_IO;
    }
    int result = exchange(p_request);
    if (E_OK != tk_unl_mtx(g_state.h_mutex))
    {
        result = I2CBUS_IO;
    }
    return result;
}

/** @brief Use the same operating-time origin as sample timestamps. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    if ((&g_state != p_context) || (!g_state.b_open))
    {
        return I2CBUS_STATE;
    }
    return (0 == rt_port_now_ms(p_now)) ? 0 : I2CBUS_IO;
}

/** @brief Yield for at least the requested delay without extending its budget.
 */
static int
delay_us (void * p_context, uint32_t duration_us, uint64_t deadline_ms)
{
    uint64_t       now         = 0U;
    const uint64_t duration_ms = ((uint64_t)duration_us + 999U) / 1000U;
    if (0 != now_ms(p_context, &now))
    {
        return I2CBUS_STATE;
    }
    if ((now >= deadline_ms) || (duration_ms >= (deadline_ms - now)))
    {
        return I2CBUS_TIMEOUT;
    }
    const uint64_t target = now + duration_ms;
    while (now < target)
    {
        if ((E_OK != tk_dly_tsk(1U)) || (0 != now_ms(p_context, &now)))
        {
            return I2CBUS_IO;
        }
        if (now >= deadline_ms)
        {
            return I2CBUS_TIMEOUT;
        }
    }
    return 0;
}

/** @brief Exclude USB, flash, strapping pins and the native UART's live pins.
 */
static bool valid_pin (uint8_t pin)
{
    return c3pin_valid(pin);
}

/** @brief Restrict controller lifecycle to quiescent application ownership. */
static bool idle_owner (void)
{
    size_t count = 0U;
    return (0 == rt_port_owned_count(&count)) && (0U == count);
}

/** @brief Route bidirectional open-drain signals without driving a high level.
 */
static void setup_pin (uint8_t pin, uint32_t output, uint32_t input)
{
    gpio_dev_t * p_gpio = GPIO_LL_GET_HW(0);
    gpio_ll_func_sel(p_gpio, pin, PIN_FUNC_GPIO);
    gpio_ll_input_enable(p_gpio, pin);
    gpio_ll_od_enable(p_gpio, pin);
    gpio_ll_pullup_en(p_gpio, pin);
    gpio_ll_pulldown_dis(p_gpio, pin);
    esp_rom_gpio_connect_out_signal(pin, output, false, false);
    esp_rom_gpio_connect_in_signal(pin, input, false);
    gpio_ll_output_enable(p_gpio, pin);
}

/** @brief Detach the output and leave a released input with no internal pull.
 */
static void release_pin (uint8_t pin)
{
    gpio_dev_t * p_gpio = GPIO_LL_GET_HW(0);
    gpio_ll_output_disable(p_gpio, pin);
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    gpio_ll_pullup_dis(p_gpio, pin);
    gpio_ll_od_disable(p_gpio, pin);
}

/** @brief Fully reset FIFO/command state after errors, retaining bus settings.
 */
static void reset_controller (void)
{
    i2c_dev_t * p_hardware = I2C_LL_GET_HW(0);
    PERIPH_RCC_ATOMIC()
    {
        i2c_ll_enable_bus_clock(0, true);
        i2c_ll_reset_register(0);
    }
    i2c_ll_enable_controller_clock(p_hardware, true);
    i2c_ll_set_source_clk(p_hardware, I2C_CLK_SRC_XTAL);
    i2c_hal_clk_config_t timing = {0};
    i2c_ll_master_cal_bus_clk(
        g_state.source_hz, g_state.config.clock_hz, &timing);
    i2c_ll_master_set_bus_timing(p_hardware, &timing);
    i2c_ll_set_mode(p_hardware, I2C_BUS_MODE_MASTER);
    i2c_ll_enable_pins_open_drain(p_hardware, true);
    i2c_ll_master_set_filter(p_hardware, 7U);
    i2c_ll_enable_fifo_mode(p_hardware, true);
    i2c_ll_disable_intr_mask(p_hardware, UINT32_MAX);
    i2c_ll_clear_intr_mask(p_hardware, UINT32_MAX);
    i2c_ll_update(p_hardware);
}

/** @brief Load at most seven commands and 32 transmit bytes, with final NACK.
 */
static void load_commands (const i2cbus_request_t * p_request)
{
    uint8_t  bytes[32] = {0};
    size_t   count     = 0U;
    uint32_t slot      = 0U;
    command(slot++, I2C_LL_CMD_RESTART, 0U, false);
    if (0U != p_request->write_size)
    {
        /* A seven-bit address shifted once fits the transmitted byte. */
        bytes[count++] = (uint8_t)(p_request->address << 1U);
        memcpy(&bytes[count], p_request->p_write, p_request->write_size);
        count += p_request->write_size;
        /* The common HAL limits write_size to 30. */
        command(slot++,
                I2C_LL_CMD_WRITE,
                (uint32_t)(p_request->write_size + 1U),
                false);
    }
    if (0U != p_request->read_size)
    {
        if (0U != p_request->write_size)
        {
            command(slot++, I2C_LL_CMD_RESTART, 0U, false);
        }
        bytes[count++] = (uint8_t)((p_request->address << 1U) | 1U);
        command(slot++, I2C_LL_CMD_WRITE, 1U, false);
        if (p_request->read_size > 1U)
        {
            /* The common HAL limits read_size to 32. */
            command(slot++,
                    I2C_LL_CMD_READ,
                    (uint32_t)(p_request->read_size - 1U),
                    false);
        }
        command(slot++, I2C_LL_CMD_READ, 1U, true);
    }
    command(slot, I2C_LL_CMD_STOP, 0U, false);
    /* The two address bytes plus payload fit the hardware's 32-byte FIFO. */
    i2c_ll_write_txfifo(I2C_LL_GET_HW(0), bytes, (uint32_t)count);
}

/** @brief Encode a bounded hardware command using SDK-defined fields. */
static void
command (uint32_t slot, uint32_t operation, uint32_t count, bool b_last_read)
{
    const i2c_ll_hw_cmd_t instruction = {.byte_num = count,
                                         .ack_en =
                                             (I2C_LL_CMD_WRITE == operation),
                                         .ack_val = b_last_read,
                                         .op_code = operation};
    i2c_ll_master_write_cmd_reg(I2C_LL_GET_HW(0), instruction, slot);
}

/** @brief Poll with kernel yields and report distinct deadline/NACK failures.
 */
static int wait_complete (uint64_t deadline_ms)
{
    unsigned polls = 0U;
    for (;;)
    {
        uint64_t now = 0U;
        if (0 != rt_port_now_ms(&now))
        {
            return I2CBUS_IO;
        }
        if (now >= deadline_ms)
        {
            return I2CBUS_TIMEOUT;
        }
        uint32_t status = 0U;
        /* Poll raw status because peripheral interrupts remain disabled.
         * The pinned LL get_intr_mask helper reads masked int_status instead.
         */
        status = I2C_LL_GET_HW(0)->int_raw.val;
        if (0U != (status & (I2C_LL_INTR_TIMEOUT | I2C_LL_INTR_ST_TO)))
        {
            return I2CBUS_TIMEOUT;
        }
        if (0U != (status & I2C_LL_INTR_ARBITRATION))
        {
            return I2CBUS_IO;
        }
        if (0U != (status & I2C_LL_INTR_NACK))
        {
            return I2CBUS_NACK;
        }
        if (0U != (status & I2C_LL_INTR_MST_COMPLETE))
        {
            return 0;
        }
        esp_rom_delay_us(10U);
        ++polls;
        if (25U == polls)
        {
            polls = 0U;
            if (E_OK != tk_dly_tsk(1U))
            {
                return I2CBUS_IO;
            }
        }
    }
}

/** @brief Run and reset one transfer while retaining the bus mutex. */
static int exchange (const i2cbus_request_t * p_request)
{
    uint64_t now = 0U;
    if (0 != rt_port_now_ms(&now))
    {
        return I2CBUS_IO;
    }
    if (now >= p_request->deadline_ms)
    {
        return I2CBUS_TIMEOUT;
    }
    i2c_dev_t * p_hardware = I2C_LL_GET_HW(0);
    i2c_ll_txfifo_rst(p_hardware);
    i2c_ll_rxfifo_rst(p_hardware);
    i2c_ll_clear_intr_mask(p_hardware, UINT32_MAX);
    for (uint32_t index = 0U; index < I2C_LL_CMD_REG_NUM; ++index)
    {
        command(index, I2C_LL_CMD_END, 0U, false);
    }
    load_commands(p_request);
    i2c_ll_update(p_hardware);
    i2c_ll_start_trans(p_hardware);
    int result = wait_complete(p_request->deadline_ms);
    if ((0 == result) && (0U != p_request->read_size))
    {
        uint32_t count = 0U;
        i2c_ll_get_rxfifo_cnt(p_hardware, &count);
        if (count != p_request->read_size)
        {
            result = I2CBUS_IO;
        }
        else
        {
            i2c_ll_read_rxfifo(p_hardware, p_request->p_read, count);
        }
    }
    if (0 != result)
    {
        reset_controller();
    }
    return result;
}

/* End of file. */


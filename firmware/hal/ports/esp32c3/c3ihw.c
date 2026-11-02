/**
 * @file c3ihw.c
 * @brief Contain pinned SDK I2C register and signal encodings without polling.
 */
#include "c3ihw.h"

#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "hal/i2c_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/i2c_reg.h"
#include "soc/io_mux_reg.h"

static void setup_pin (uint8_t pin, uint32_t output, uint32_t input);
static void release_pin (uint8_t pin);

/** @brief Connect the already reserved open-drain bus pins. */
void c3ihw_pins_open (const i2cbus_config_t * p_config)
{
    setup_pin(p_config->sda_gpio, I2CEXT0_SDA_OUT_IDX, I2CEXT0_SDA_IN_IDX);
    setup_pin(p_config->scl_gpio, I2CEXT0_SCL_OUT_IDX, I2CEXT0_SCL_IN_IDX);
}

/** @brief Disable hardware before the adapter releases its pin ledger. */
void c3ihw_stop (const i2cbus_config_t * p_config)
{
    i2c_dev_t * p_hardware = I2C_LL_GET_HW(0);
    i2c_ll_disable_intr_mask(p_hardware, UINT32_MAX);
    i2c_ll_enable_controller_clock(p_hardware, false);
    release_pin(p_config->sda_gpio);
    release_pin(p_config->scl_gpio);
    PERIPH_RCC_ATOMIC()
    {
        i2c_ll_enable_bus_clock(0, false);
    }
}

/** @brief Reset only transmit and receive FIFOs before a new operation. */
void c3ihw_clear_fifo (void)
{
    i2c_ll_txfifo_rst(I2C_LL_GET_HW(0));
    i2c_ll_rxfifo_rst(I2C_LL_GET_HW(0));
}

/** @brief Acknowledge all raw interrupt causes while interrupts stay disabled.
 */
void c3ihw_clear_status (void)
{
    i2c_ll_clear_intr_mask(I2C_LL_GET_HW(0), UINT32_MAX);
}

/** @brief Mark every unused hardware command as an end instruction. */
void c3ihw_clear_commands (void)
{
    for (uint32_t index = 0U; index < I2C_LL_CMD_REG_NUM; ++index)
    {
        c3ihw_command(index, C3IHW_END_COMMAND, 0U, false);
    }
}

/** @brief Load the adapter's at-most-32-byte transmit FIFO image. */
void c3ihw_write (const uint8_t * p_bytes, uint32_t count)
{
    /* The adapter bounds count to the 32-byte FIFO before this narrowing. */
    i2c_ll_write_txfifo(I2C_LL_GET_HW(0), p_bytes, (uint8_t)count);
}

/** @brief Publish the prepared command registers and begin one hardware phase.
 */
void c3ihw_start (void)
{
    i2c_ll_update(I2C_LL_GET_HW(0));
    i2c_ll_start_trans(I2C_LL_GET_HW(0));
}

/** @brief Normalize raw bits; masked status would hide polling completion. */
uint32_t c3ihw_status (void)
{
    const uint32_t raw    = I2C_LL_GET_HW(0)->int_raw.val;
    uint32_t       status = 0U;
    if (0U != (raw & (I2C_LL_INTR_TIMEOUT | I2C_LL_INTR_ST_TO |
                      I2C_SCL_MAIN_ST_TO_INT_RAW)))
    {
        status |= C3IHW_TIMEOUT;
    }
    if (0U != (raw & I2C_LL_INTR_ARBITRATION))
    {
        status |= C3IHW_ARBITRATION;
    }
    if (0U != (raw & I2C_LL_INTR_NACK))
    {
        status |= C3IHW_NACK;
    }
    if (0U != (raw & I2C_LL_INTR_END_DETECT))
    {
        status |= C3IHW_END;
    }
    if (0U != (raw & I2C_LL_INTR_MST_COMPLETE))
    {
        status |= C3IHW_COMPLETE;
    }
    return status;
}

/** @brief Save normal timeouts before the bounded held-clock read pause. */
void c3ihw_hold (c3ihw_saved_t * p_saved)
{
    i2c_dev_t * p_hardware = I2C_LL_GET_HW(0);
    i2c_ll_get_tout(p_hardware, &p_saved->timeout);
    p_saved->scl  = p_hardware->scl_st_time_out.scl_st_to;
    p_saved->main = p_hardware->scl_main_st_time_out.scl_main_st_to;
    /* At the C3 XTAL clock this bounds the held SCL period above 10 ms. */
    i2c_ll_set_tout(p_hardware, 22);
    p_hardware->scl_st_time_out.scl_st_to           = 22U;
    p_hardware->scl_main_st_time_out.scl_main_st_to = 22U;
}

/** @brief Restore the saved normal timeouts after a successful held read. */
void c3ihw_restore (const c3ihw_saved_t * p_saved)
{
    i2c_dev_t * p_hardware = I2C_LL_GET_HW(0);
    i2c_ll_set_tout(p_hardware, p_saved->timeout);
    p_hardware->scl_st_time_out.scl_st_to           = p_saved->scl;
    p_hardware->scl_main_st_time_out.scl_main_st_to = p_saved->main;
    i2c_ll_update(p_hardware);
}

/** @brief Return the hardware FIFO count for the adapter's exact-size check. */
uint32_t c3ihw_read_count (void)
{
    uint32_t count = 0U;
    i2c_ll_get_rxfifo_cnt(I2C_LL_GET_HW(0), &count);
    return count;
}

/** @brief Copy only the already verified, at-most-32-byte received payload. */
void c3ihw_read (uint8_t * p_bytes, uint32_t count)
{
    /* The adapter verifies count equals its bounded read request. */
    i2c_ll_read_rxfifo(I2C_LL_GET_HW(0), p_bytes, (uint8_t)count);
}

/** @brief Fully reset FIFO/command state after errors, retaining bus settings.
 */
void c3ihw_reset (uint32_t source_hz, uint32_t clock_hz)
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
    i2c_ll_master_cal_bus_clk(source_hz, clock_hz, &timing);
    i2c_ll_master_set_bus_timing(p_hardware, &timing);
    i2c_ll_set_mode(p_hardware, I2C_BUS_MODE_MASTER);
    i2c_ll_enable_pins_open_drain(p_hardware, true);
    i2c_ll_master_set_filter(p_hardware, 7U);
    i2c_ll_enable_fifo_mode(p_hardware, true);
    i2c_ll_disable_intr_mask(p_hardware, UINT32_MAX);
    i2c_ll_clear_intr_mask(p_hardware, UINT32_MAX);
    i2c_ll_update(p_hardware);
}

/** @brief Encode a bounded hardware command using SDK-defined fields. */
void c3ihw_command (uint32_t        slot,
                    c3ihw_command_t operation,
                    uint32_t        count,
                    bool            b_last_read)
{
    static const uint32_t operations[] = {I2C_LL_CMD_RESTART,
                                          I2C_LL_CMD_WRITE,
                                          I2C_LL_CMD_READ,
                                          I2C_LL_CMD_END,
                                          I2C_LL_CMD_STOP};
    if ((operation < C3IHW_RESTART_COMMAND) ||
        (operation > C3IHW_STOP_COMMAND) || (slot >= I2C_LL_CMD_REG_NUM))
    {
        return;
    }
    const uint32_t        opcode      = operations[operation];
    const i2c_ll_hw_cmd_t instruction = {.byte_num = count,
                                         .ack_en = (I2C_LL_CMD_WRITE == opcode),
                                         .ack_val = b_last_read,
                                         .op_code = opcode};
    i2c_ll_master_write_cmd_reg(I2C_LL_GET_HW(0), instruction, slot);
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

/* End of file. */


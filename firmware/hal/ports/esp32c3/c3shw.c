/**
 * @file c3shw.c
 * @brief Contain pinned SPI2 register and signal operations without polling.
 */
#include "c3shw.h"

#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "hal/spi_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"

static void output_pin (uint8_t pin, uint32_t signal, bool b_initial);
static void release_pin (uint8_t pin);

/** @brief Route the adapter's already reserved bus signals. */
void c3shw_pins_open (const c3spi_config_t * p_config)
{
    output_pin(p_config->sclk_gpio, FSPICLK_OUT_IDX, false);
    output_pin(p_config->mosi_gpio, FSPID_OUT_IDX, false);
    if (UTHOME_SPI_NO_PIN != p_config->miso_gpio)
    {
        gpio_dev_t * p_gpio = GPIO_LL_GET_HW(0);
        gpio_ll_func_sel(p_gpio, p_config->miso_gpio, PIN_FUNC_GPIO);
        gpio_ll_output_disable(p_gpio, p_config->miso_gpio);
        gpio_ll_pullup_dis(p_gpio, p_config->miso_gpio);
        gpio_ll_pulldown_dis(p_gpio, p_config->miso_gpio);
        gpio_ll_input_enable(p_gpio, p_config->miso_gpio);
        esp_rom_gpio_connect_in_signal(
            p_config->miso_gpio, FSPIQ_IN_IDX, false);
    }
}

/** @brief Stop driving the bus before the adapter releases its pin ledger. */
void c3shw_stop (const c3spi_config_t * p_config)
{
    /* Repeating these writes on a retried close is harmless. */
    spi_ll_enable_clock(SPI2_HOST, false);
    release_pin(p_config->sclk_gpio);
    release_pin(p_config->mosi_gpio);
    if (UTHOME_SPI_NO_PIN != p_config->miso_gpio)
    {
        release_pin(p_config->miso_gpio);
    }
    PERIPH_RCC_ATOMIC()
    {
        spi_ll_enable_bus_clock(SPI2_HOST, false);
    }
}

/** @brief Load inactive CS before enabling the output. */
void c3shw_attach (uint8_t pin)
{
    output_pin(pin, SIG_GPIO_OUT_IDX, true);
}

/** @brief End the CS window and detach the output. */
void c3shw_detach (uint8_t pin)
{
    c3shw_select(pin, false);
    release_pin(pin);
}

/** @brief Assert or release active-low chip select without changing ownership.
 */
void c3shw_select (uint8_t pin, bool b_selected)
{
    gpio_ll_set_level(GPIO_LL_GET_HW(0), pin, b_selected ? 0U : 1U);
}

/** @brief Return the quantized native frequency for a validated request. */
int c3shw_clock (int source_hz, uint32_t clock_hz)
{
    spi_ll_clock_val_t clock = {0};
    /* The adapter admits only 100 kHz..8 MHz, within the SDK int range. */
    return spi_ll_master_cal_clock(source_hz, (int)clock_hz, 128, &clock);
}

/** @brief Read one completion state without the SDK's unbounded spin helper. */
bool c3shw_ready (bool b_update)
{
    spi_dev_t * p_hardware = SPI_LL_GET_HW(SPI2_HOST);
    return b_update ? (0U == p_hardware->cmd.update)
                    : spi_ll_usr_is_done(p_hardware);
}

/** @brief Prepare one bounded FIFO chunk with padded transmit storage. */
void c3shw_prepare (const uint8_t * p_bytes, size_t size, bool b_read)
{
    spi_dev_t * p_hardware = SPI_LL_GET_HW(SPI2_HOST);
    spi_ll_cpu_tx_fifo_reset(p_hardware);
    spi_ll_cpu_rx_fifo_reset(p_hardware);
    spi_ll_clear_int_stat(p_hardware);
    /* SDK writes rounded words; padded local storage prevents over-read. */
    spi_ll_write_buffer(p_hardware, p_bytes, size * 8U);
    spi_ll_set_mosi_bitlen(p_hardware, size * 8U);
    spi_ll_set_miso_bitlen(p_hardware, size * 8U);
    spi_ll_enable_mosi(p_hardware, 1);
    spi_ll_enable_miso(p_hardware, b_read);
    p_hardware->cmd.update = 1U;
}

/** @brief Start the already configured FIFO transaction. */
void c3shw_start (void)
{
    spi_ll_user_start(SPI_LL_GET_HW(SPI2_HOST));
}

/** @brief Copy the bounded completed receive chunk. */
void c3shw_read (uint8_t * p_bytes, size_t size)
{
    spi_ll_read_buffer(SPI_LL_GET_HW(SPI2_HOST), p_bytes, size * 8U);
}

/** @brief Abort stalled work and restore the finite FIFO baseline. */
void c3shw_reset (void)
{
    PERIPH_RCC_ATOMIC()
    {
        spi_ll_enable_bus_clock(SPI2_HOST, true);
        spi_ll_reset_register(SPI2_HOST);
    }
    spi_dev_t * p_hardware = SPI_LL_GET_HW(SPI2_HOST);
    spi_ll_enable_clock(SPI2_HOST, true);
    spi_ll_master_init(p_hardware);
    spi_ll_set_clk_source(p_hardware, SPI_CLK_SRC_XTAL);
    spi_ll_master_select_cs(p_hardware, -1);
    spi_ll_disable_int(p_hardware);
    spi_ll_clear_int_stat(p_hardware);
    spi_ll_set_half_duplex(p_hardware, false);
    spi_ll_set_tx_lsbfirst(p_hardware, false);
    spi_ll_set_rx_lsbfirst(p_hardware, false);
    spi_ll_set_command_bitlen(p_hardware, 0);
    spi_ll_set_addr_bitlen(p_hardware, 0);
    spi_ll_set_dummy(p_hardware, 0);
}

/** @brief Apply mode/clock while every CS is inactive. */
int c3shw_configure (int source_hz, const spibus_config_t * p_config)
{
    spi_dev_t *        p_hardware = SPI_LL_GET_HW(SPI2_HOST);
    spi_ll_clock_val_t clock      = {0};
    /* Attach validated this frequency and the bounded int conversion. */
    const int actual = spi_ll_master_cal_clock(
        source_hz, (int)p_config->clock_hz, 128, &clock);
    if (actual <= 0)
    {
        return SPIBUS_INVALID;
    }
    spi_ll_master_set_clock_by_reg(p_hardware, &clock);
    spi_ll_master_set_mode(p_hardware, p_config->mode);
    spi_ll_set_mosi_delay(p_hardware, 0, 0);
    spi_ll_set_miso_delay(
        p_hardware,
        ((0U == p_config->mode) || (3U == p_config->mode)) ? 2 : 1,
        0);
    /* The SDK apply_config helper spins without a timeout; poll MMIO here. */
    p_hardware->cmd.update = 1U;
    return 0;
}

/** @brief Route a push-pull output only after loading its inactive latch. */
static void output_pin (uint8_t pin, uint32_t signal, bool b_initial)
{
    gpio_dev_t * p_gpio = GPIO_LL_GET_HW(0);
    gpio_ll_output_disable(p_gpio, pin);
    gpio_ll_set_level(p_gpio, pin, b_initial);
    gpio_ll_func_sel(p_gpio, pin, PIN_FUNC_GPIO);
    gpio_ll_od_disable(p_gpio, pin);
    gpio_ll_pullup_dis(p_gpio, pin);
    gpio_ll_pulldown_dis(p_gpio, pin);
    esp_rom_gpio_connect_out_signal(pin, signal, false, false);
    gpio_ll_output_enable(p_gpio, pin);
}

/** @brief Stop driving before another module can reuse a bus pin. */
static void release_pin (uint8_t pin)
{
    gpio_ll_output_disable(GPIO_LL_GET_HW(0), pin);
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
}

/* End of file. */


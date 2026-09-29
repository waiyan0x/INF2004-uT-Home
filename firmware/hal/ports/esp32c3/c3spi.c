/**
 * @file c3spi.c
 * @brief Serialize bounded FIFO chunks inside a single chip-select window.
 */
#include "c3spi.h"
#include <string.h>
#include <tk/tkernel.h>
#include "c3pin.h"
#include "workport.h"
#include "esp_private/esp_clk.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"
#include "hal/spi_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"

typedef struct
{
    c3spi_config_t config;
    ID             h_mutex;
    uint32_t       pins;
    int            source_hz;
    bool           b_open;
} c3spi_state_t;

static c3spi_state_t g_state = {0};
static spibus_t      g_bus   = {0};
static bool idle_owner (void);
static int
attach (void * p_context, const spibus_config_t * p_config, bool b_attach);
static int transfer (void *                   p_context,
                     const spibus_config_t *  p_config,
                     const spibus_request_t * p_request);
static int now_ms (void * p_context, uint64_t * p_now);
static int
delay_ms (void * p_context, uint32_t duration_ms, uint64_t deadline_ms);
static void output_pin (uint8_t pin, uint32_t signal, bool b_initial);
static void release_pin (uint8_t pin);
static void reset_controller (void);
static int wait_ready (uint64_t deadline_ms, bool b_update);
static int configure (const spibus_config_t * p_config, uint64_t deadline_ms);
static int chunks (const spibus_request_t * p_request);
static const spibus_ops_t g_operations = {.p_attach   = attach,
                                          .p_transfer = transfer,
                                          .p_now      = now_ms,
                                          .p_delay    = delay_ms};

/** @brief Reserve bus pins before enabling SPI2; never touch the flash bus. */
int c3spi_open (const c3spi_config_t * p_config, spibus_t ** pp_bus)
{
    if ((!idle_owner()) || (NULL == p_config) || (NULL == pp_bus) ||
        (g_state.b_open) || (2U != p_config->host) ||
        (!c3pin_valid(p_config->sclk_gpio)) ||
        (!c3pin_valid(p_config->mosi_gpio)) ||
        (p_config->sclk_gpio == p_config->mosi_gpio) ||
        ((UTHOME_SPI_NO_PIN != p_config->miso_gpio) &&
         ((!c3pin_valid(p_config->miso_gpio)) ||
          (p_config->miso_gpio == p_config->sclk_gpio) ||
          (p_config->miso_gpio == p_config->mosi_gpio))))
    {
        return SPIBUS_INVALID;
    }
    const int source_hz = esp_clk_xtal_freq();
    if (source_hz <= 0)
    {
        return SPIBUS_IO;
    }
    uint32_t pins = (UINT32_C(1) << p_config->sclk_gpio) |
                    (UINT32_C(1) << p_config->mosi_gpio);
    if (UTHOME_SPI_NO_PIN != p_config->miso_gpio)
    {
        pins |= UINT32_C(1) << p_config->miso_gpio;
    }
    if (0 != c3pin_reserve(pins, &g_bus))
    {
        return SPIBUS_STATE;
    }
    const T_CMTX mutex   = {.mtxatr = TA_INHERIT};
    const ID     h_mutex = tk_cre_mtx(&mutex);
    if (h_mutex <= 0)
    {
        const int released = c3pin_release(pins, &g_bus);
        return (0 == released) ? SPIBUS_IO : SPIBUS_STATE;
    }
    g_state = (c3spi_state_t){.config    = *p_config,
                              .h_mutex   = h_mutex,
                              .pins      = pins,
                              .source_hz = source_hz,
                              .b_open    = true};
    reset_controller();
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
    g_bus   = (spibus_t){.p_context = &g_state, .p_operations = &g_operations};
    *pp_bus = &g_bus;
    return 0;
}

/** @brief Refuse closure until every attached device has been released. */
int c3spi_close (spibus_t * p_bus)
{
    if ((!idle_owner()) || (&g_bus != p_bus) || (!g_state.b_open) ||
        (spibus_has_devices(p_bus)))
    {
        return SPIBUS_STATE;
    }
    if (E_OK != tk_del_mtx(g_state.h_mutex))
    {
        return SPIBUS_IO;
    }
    spi_ll_enable_clock(SPI2_HOST, false);
    release_pin(g_state.config.sclk_gpio);
    release_pin(g_state.config.mosi_gpio);
    if (UTHOME_SPI_NO_PIN != g_state.config.miso_gpio)
    {
        release_pin(g_state.config.miso_gpio);
    }
    PERIPH_RCC_ATOMIC()
    {
        spi_ll_enable_bus_clock(SPI2_HOST, false);
    }
    const int released = c3pin_release(g_state.pins, &g_bus);
    if (0 != released)
    {
        return SPIBUS_STATE;
    }
    g_state = (c3spi_state_t){0};
    g_bus   = (spibus_t){0};
    return 0;
}

/** @brief Match the existing controller-lifecycle boundary. */
static bool idle_owner (void)
{
    size_t count = 0U;
    return (0 == rt_port_owned_count(&count)) && (0U == count);
}

/** @brief Attach/detach CS during serialized lifecycle work, never a transfer.
 */
static int
attach (void * p_context, const spibus_config_t * p_config, bool b_attach)
{
    if ((&g_state != p_context) || (!g_state.b_open) ||
        (!c3pin_valid(p_config->cs_gpio)) || (p_config->clock_hz < 100000U) ||
        (p_config->clock_hz > 8000000U))
    {
        return SPIBUS_INVALID;
    }
    const uint32_t pin = UINT32_C(1) << p_config->cs_gpio;
    if (b_attach)
    {
        spi_ll_clock_val_t clock = {0};
        /* The validated 100 kHz..8 MHz request fits the SDK int argument. */
        const int actual = spi_ll_master_cal_clock(
            g_state.source_hz, (int)p_config->clock_hz, 128, &clock);
        if ((actual <= 0) || ((uint32_t)actual > p_config->clock_hz))
        {
            return SPIBUS_UNSUPPORTED;
        }
        if (0 != c3pin_reserve(pin, &g_bus))
        {
            return SPIBUS_STATE;
        }
        output_pin(p_config->cs_gpio, SIG_GPIO_OUT_IDX, true);
        return 0;
    }
    /* The common bus validates device identity and live driver claims first. */
    gpio_ll_set_level(GPIO_LL_GET_HW(0), p_config->cs_gpio, 1U);
    release_pin(p_config->cs_gpio);
    return (0 == c3pin_release(pin, &g_bus)) ? 0 : SPIBUS_STATE;
}

/** @brief Include lock wait and all chunks in the caller's single deadline. */
static int transfer (void *                   p_context,
                     const spibus_config_t *  p_config,
                     const spibus_request_t * p_request)
{
    if ((&g_state != p_context) || (!g_state.b_open))
    {
        return SPIBUS_STATE;
    }
    if ((NULL != p_request->p_read) &&
        ((UTHOME_SPI_NO_PIN == g_state.config.miso_gpio) ||
         (p_config->clock_hz > 1000000U)))
    {
        return SPIBUS_UNSUPPORTED;
    }
    uint64_t now = 0U;
    if ((0 != now_ms(p_context, &now)) || (now >= p_request->deadline_ms))
    {
        return SPIBUS_TIMEOUT;
    }
    /* The portable front end bounded the remaining time to positive TMO. */
    const ER locked =
        tk_loc_mtx(g_state.h_mutex, (TMO)(p_request->deadline_ms - now));
    if (E_OK != locked)
    {
        return (E_TMOUT == locked) ? SPIBUS_TIMEOUT : SPIBUS_IO;
    }
    int result = configure(p_config, p_request->deadline_ms);
    if (0 == result)
    {
        gpio_ll_set_level(GPIO_LL_GET_HW(0), p_config->cs_gpio, 0U);
        esp_rom_delay_us(1U);
        result = chunks(p_request);
        esp_rom_delay_us(1U);
    }
    gpio_ll_set_level(GPIO_LL_GET_HW(0), p_config->cs_gpio, 1U);
    if (0 != result)
    {
        reset_controller();
    }
    if (E_OK != tk_unl_mtx(g_state.h_mutex))
    {
        result = SPIBUS_IO;
    }
    return result;
}

/** @brief Use the runtime operating-time clock. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    if ((&g_state != p_context) || (!g_state.b_open))
    {
        return SPIBUS_STATE;
    }
    return (0 == rt_port_now_ms(p_now)) ? 0 : SPIBUS_IO;
}

/** @brief Yield without resetting or extending the operation budget. */
static int
delay_ms (void * p_context, uint32_t duration_ms, uint64_t deadline_ms)
{
    uint64_t now = 0U;
    if ((0 != now_ms(p_context, &now)) || (now >= deadline_ms) ||
        (duration_ms >= (deadline_ms - now)))
    {
        return SPIBUS_TIMEOUT;
    }
    const uint64_t end = now + duration_ms;
    while (now < end)
    {
        if ((E_OK != tk_dly_tsk(1U)) || (0 != now_ms(p_context, &now)))
        {
            return SPIBUS_IO;
        }
        if (now >= deadline_ms)
        {
            return SPIBUS_TIMEOUT;
        }
    }
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

/** @brief Abort stalled work and restore the finite FIFO baseline. */
static void reset_controller (void)
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

/** @brief Bound both register-update completion and transfer completion. */
static int wait_ready (uint64_t deadline_ms, bool b_update)
{
    unsigned    polls      = 0U;
    spi_dev_t * p_hardware = SPI_LL_GET_HW(SPI2_HOST);
    for (;;)
    {
        uint64_t now = 0U;
        if (0 != rt_port_now_ms(&now))
        {
            return SPIBUS_IO;
        }
        if (now >= deadline_ms)
        {
            return SPIBUS_TIMEOUT;
        }
        if (b_update ? (0U == p_hardware->cmd.update)
                     : spi_ll_usr_is_done(p_hardware))
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
                return SPIBUS_IO;
            }
        }
    }
}

/** @brief Apply mode/clock while every CS is inactive. */
static int configure (const spibus_config_t * p_config, uint64_t deadline_ms)
{
    spi_dev_t *        p_hardware = SPI_LL_GET_HW(SPI2_HOST);
    spi_ll_clock_val_t clock      = {0};
    /* Attach validated this frequency and the bounded int conversion. */
    const int actual = spi_ll_master_cal_clock(
        g_state.source_hz, (int)p_config->clock_hz, 128, &clock);
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
    return wait_ready(deadline_ms, true);
}

/** @brief Keep CS asserted across 64-byte chunks without DMA or allocation. */
static int chunks (const spibus_request_t * p_request)
{
    spi_dev_t * p_hardware = SPI_LL_GET_HW(SPI2_HOST);
    for (size_t offset = 0U; offset < p_request->size; offset += 64U)
    {
        uint8_t bytes[64] = {0};
        size_t  size      = p_request->size - offset;
        if (size > sizeof(bytes))
        {
            size = sizeof(bytes);
        }
        if (NULL != p_request->p_write)
        {
            memcpy(bytes, &p_request->p_write[offset], size);
        }
        spi_ll_cpu_tx_fifo_reset(p_hardware);
        spi_ll_cpu_rx_fifo_reset(p_hardware);
        spi_ll_clear_int_stat(p_hardware);
        /* SDK writes rounded words; padded local storage prevents over-read. */
        spi_ll_write_buffer(p_hardware, bytes, size * 8U);
        spi_ll_set_mosi_bitlen(p_hardware, size * 8U);
        spi_ll_set_miso_bitlen(p_hardware, size * 8U);
        spi_ll_enable_mosi(p_hardware, 1);
        spi_ll_enable_miso(p_hardware, NULL != p_request->p_read);
        p_hardware->cmd.update = 1U;
        int result             = wait_ready(p_request->deadline_ms, true);
        if (0 == result)
        {
            spi_ll_user_start(p_hardware);
            result = wait_ready(p_request->deadline_ms, false);
        }
        if (0 != result)
        {
            return result;
        }
        if (NULL != p_request->p_read)
        {
            spi_ll_read_buffer(
                p_hardware, &p_request->p_read[offset], size * 8U);
        }
    }
    return 0;
}

/* End of file. */


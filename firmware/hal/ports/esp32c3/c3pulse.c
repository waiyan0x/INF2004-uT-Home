/**
 * @file c3pulse.c
 * @brief Capture edges in RMT while the native task yields.
 */
#include "c3pulse.h"
#include <tk/tkernel.h>
#include "workport.h"
#include "c3pin.h"
#include "esp_private/esp_clk.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hal/rmt_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/reg_base.h"

typedef struct
{
    c3pin_output_t trigger;
    c3pin_input_t  input;
    ID             h_mutex;
    uint8_t        channel;
    bool           b_clock;
} c3pulse_state_t;

static int open_port (void * p_context, const pulseio_config_t * p_config);
static int close_port (void * p_context);
static int measure (void *     p_context,
                    uint32_t   trigger_us,
                    uint32_t   timeout_us,
                    uint32_t * p_high_us);
static int now_ms (void * p_context, uint64_t * p_now);
static void arm_capture (c3pulse_state_t * p_state,
                         uint32_t          trigger_us,
                         uint32_t          timeout_us);
static int await_capture (uint32_t   channel,
                          int64_t    start_us,
                          uint32_t   timeout_us,
                          uint32_t * p_high_us);
static volatile uint32_t * memory (uint32_t channel);
static int stop_capture (c3pulse_state_t * p_state);
static c3pulse_state_t g_states[2] = {{.channel = 0U}, {.channel = 1U}};

static const pulseio_ops_t g_operations = {.p_open    = open_port,
                                           .p_close   = close_port,
                                           .p_measure = measure,
                                           .p_now     = now_ms};

static pulseio_t g_ports[2] = {
    {.p_operations = &g_operations, .p_context = &g_states[0]},
    {.p_operations = &g_operations, .p_context = &g_states[1]}};

static uint8_t g_users = 0U;

/** @brief Return static noncopyable storage with no hardware side effects. */
pulseio_t * c3pulse_port (uint8_t pair)
{
    return (pair < 2U) ? &g_ports[pair] : NULL;
}

/** @brief Claims and partial setup are retained until lifecycle cleanup. */
static int open_port (void * p_context, const pulseio_config_t * p_config)
{
    c3pulse_state_t * p_state   = p_context;
    const uint32_t    frequency = esp_clk_xtal_freq();
    if ((!rt_port_is_owner()) || (0 != p_state->h_mutex) || (0U == frequency) ||
        (0U != (frequency % 1000000U)) || ((frequency / 1000000U) > 256U))
    {
        return PULSEIO_INVALID;
    }
    const T_CMTX mutex = {.mtxatr = TA_INHERIT};
    p_state->h_mutex   = tk_cre_mtx(&mutex);
    if (p_state->h_mutex <= 0)
    {
        p_state->h_mutex = 0;
        return PULSEIO_IO;
    }
    if ((0 != c3pin_open(&p_state->trigger, p_config->trigger_gpio, false)) ||
        (0 != c3pin_input_open(&p_state->input, p_config->input_gpio)))
    {
        return PULSEIO_INVALID;
    }
    if (0U == g_users)
    {
        PERIPH_RCC_ATOMIC()
        {
            rmt_ll_enable_bus_clock(0, true);
            rmt_ll_reset_register(0);
        }
        rmt_ll_mem_force_power_on(&RMT);
        rmt_ll_enable_mem_access_nonfifo(&RMT, true);
        rmt_ll_enable_interrupt(&RMT, UINT32_MAX, false);
        rmt_ll_clear_interrupt_status(&RMT, UINT32_MAX);
        rmt_ll_tx_enable_sync(&RMT, false);
        rmt_ll_set_group_clock_src(&RMT, 0U, RMT_CLK_SRC_XTAL, 1U, 0U, 0U);
        rmt_ll_enable_group_clock(&RMT, true);
    }
    ++g_users;
    p_state->b_clock       = true;
    const uint32_t channel = p_state->channel;
    rmt_ll_tx_set_channel_clock_div(&RMT, channel, frequency / 1000000U);
    rmt_ll_rx_set_channel_clock_div(&RMT, channel, frequency / 1000000U);
    rmt_ll_tx_set_mem_blocks(&RMT, channel, 1U);
    rmt_ll_rx_set_mem_blocks(&RMT, channel, 1U);
    rmt_ll_tx_enable_wrap(&RMT, channel, false);
    rmt_ll_tx_enable_loop(&RMT, channel, false);
    rmt_ll_tx_enable_loop_count(&RMT, channel, false);
    rmt_ll_tx_enable_carrier_modulation(&RMT, channel, false);
    rmt_ll_tx_fix_idle_level(&RMT, channel, 0U, true);
    rmt_ll_rx_enable_wrap(&RMT, channel, false);
    rmt_ll_rx_enable_filter(&RMT, channel, false);
    rmt_ll_rx_enable_carrier_demodulation(&RMT, channel, false);
    return 0;
}

/** @brief Dormant users allow retryable cleanup without resetting another pair.
 */
static int close_port (void * p_context)
{
    c3pulse_state_t * p_state = p_context;
    if (!rt_port_is_owner())
    {
        return PULSEIO_STATE;
    }
    if (0 != stop_capture(p_state))
    {
        return PULSEIO_IO;
    }
    if ((0 < p_state->h_mutex) && (E_OK != tk_del_mtx(p_state->h_mutex)))
    {
        return PULSEIO_IO;
    }
    p_state->h_mutex = 0;
    if (((p_state->input.b_open) &&
         (0 != c3pin_input_close(&p_state->input))) ||
        ((p_state->trigger.b_open) && (0 != c3pin_close(&p_state->trigger))))
    {
        return PULSEIO_IO;
    }
    if (p_state->b_clock)
    {
        --g_users;
        p_state->b_clock = false;
        if (0U == g_users)
        {
            rmt_ll_enable_group_clock(&RMT, false);
            PERIPH_RCC_ATOMIC()
            {
                rmt_ll_enable_bus_clock(0, false);
            }
        }
    }
    return 0;
}

/** @brief Hardware makes the trigger short even if the task is preempted. */
static int measure (void *     p_context,
                    uint32_t   trigger_us,
                    uint32_t   timeout_us,
                    uint32_t * p_high_us)
{
    c3pulse_state_t * p_state  = p_context;
    const int64_t     start_us = esp_timer_get_time();
    if ((!p_state->b_clock) || (start_us < 0))
    {
        return PULSEIO_STATE;
    }
    const ER locked = tk_loc_mtx(p_state->h_mutex, TMO_POL);
    if (E_OK != locked)
    {
        return (E_TMOUT == locked) ? PULSEIO_BUSY : PULSEIO_IO;
    }
    bool b_high = false;
    int  result = PULSEIO_IO;
    if ((0 == c3pin_get(&p_state->input, &b_high)) && (!b_high))
    {
        arm_capture(p_state, trigger_us, timeout_us);
        result =
            await_capture(p_state->channel, start_us, timeout_us, p_high_us);
    }
    const int stopped  = stop_capture(p_state);
    const ER  unlocked = tk_unl_mtx(p_state->h_mutex);
    return ((0 == stopped) && (E_OK == unlocked)) ? result : PULSEIO_IO;
}

/** @brief Samples use native kernel time, never the SDK microsecond epoch. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    const c3pulse_state_t * p_state = p_context;
    if (!p_state->b_clock)
    {
        return PULSEIO_STATE;
    }
    return (0 == rt_port_now_ms(p_now)) ? 0 : PULSEIO_IO;
}

/** @brief Seed a falling edge internally, then capture one low/high symbol.
 * RMT starts timing at the first edge. Matrix high-to-input-low creates that
 * edge without driving Echo. The following real rise/fall completes a word.
 * The only busy waits are two fixed 2 us settling delays. */
static void arm_capture (c3pulse_state_t * p_state,
                         uint32_t          trigger_us,
                         uint32_t          timeout_us)
{
    const uint32_t channel = p_state->channel;
    rmt_ll_rx_enable(&RMT, channel, false);
    rmt_ll_rx_set_mem_owner(&RMT, channel, RMT_LL_MEM_OWNER_SW);
    volatile uint32_t * p_receive  = memory(channel + 2U);
    p_receive[0]                   = 0U;
    p_receive[1]                   = 0U;
    volatile uint32_t * p_transmit = memory(channel);
    p_transmit[0]       = 2U | (trigger_us << 16U) | UINT32_C(0x80000000);
    p_transmit[1]       = 2U;
    const uint32_t mask = (UINT32_C(0x1155) << channel);
    rmt_ll_clear_interrupt_status(&RMT, mask);
    rmt_ll_tx_reset_pointer(&RMT, channel);
    rmt_ll_rx_reset_pointer(&RMT, channel);
    rmt_ll_rx_set_idle_thres(&RMT, channel, timeout_us + 1U);
    esp_rom_gpio_connect_in_signal(
        GPIO_MATRIX_CONST_ONE_INPUT, RMT_SIG_IN0_IDX + channel, false);
    rmt_ll_rx_set_mem_owner(&RMT, channel, RMT_LL_MEM_OWNER_HW);
    rmt_ll_rx_enable(&RMT, channel, true);
    esp_rom_delay_us(2U);
    esp_rom_gpio_connect_in_signal(
        p_state->input.pin, RMT_SIG_IN0_IDX + channel, false);
    esp_rom_delay_us(2U);
    esp_rom_gpio_connect_out_signal(
        p_state->trigger.pin, RMT_SIG_OUT0_IDX + channel, false, false);
    rmt_ll_tx_start(&RMT, channel);
}

/** @brief Poll status only; read RAM after disabling RX and taking ownership.
 */
static int await_capture (uint32_t   channel,
                          int64_t    start_us,
                          uint32_t   timeout_us,
                          uint32_t * p_high_us)
{
    const uint32_t attempts = (timeout_us / 1000U) + 2U;
    for (uint32_t attempt = 0U; attempt < attempts; ++attempt)
    {
        const int64_t now_us = esp_timer_get_time();
        if (now_us < start_us)
        {
            return PULSEIO_IO;
        }
        if ((now_us - start_us) > timeout_us)
        {
            return PULSEIO_TIMEOUT;
        }
        const uint32_t status =
            rmt_ll_rx_get_interrupt_status_raw(&RMT, channel) |
            rmt_ll_tx_get_interrupt_status_raw(&RMT, channel);
        if (0U != (status & (UINT32_C(0x50) << channel)))
        {
            return PULSEIO_IO;
        }
        if (rmt_ll_rx_get_memory_writer_offset(&RMT, channel) > 0U)
        {
            rmt_ll_rx_enable(&RMT, channel, false);
            rmt_ll_rx_set_mem_owner(&RMT, channel, RMT_LL_MEM_OWNER_SW);
            const uint32_t symbol  = memory(channel + 2U)[0];
            const uint32_t low_us  = symbol & UINT32_C(0x7FFF);
            const uint32_t high_us = (symbol >> 16U) & UINT32_C(0x7FFF);
            if ((UINT32_C(0x80000000) != (symbol & UINT32_C(0x80008000))) ||
                (0U == low_us) || (0U == high_us) ||
                ((low_us + high_us) > timeout_us))
            {
                return PULSEIO_INVALID;
            }
            /* Recheck after reading in case a higher-priority task preempted
             * us. */
            const int64_t completed_us = esp_timer_get_time();
            if ((completed_us < start_us) ||
                ((completed_us - start_us) > timeout_us))
            {
                return PULSEIO_TIMEOUT;
            }
            *p_high_us = high_us;
            return 0;
        }
        if (E_OK != tk_dly_tsk(1U))
        {
            return PULSEIO_IO;
        }
    }
    return PULSEIO_TIMEOUT;
}

/** @brief C3 TRM maps four 48-word banks at RMT base + 0x400.
 * Factory-derived bank 0..3 gives aligned, static, volatile 32-bit MMIO.
 * The application reads RX memory only after returning ownership to software.
 */
static volatile uint32_t * memory (uint32_t channel)
{
    const uintptr_t address =
        DR_REG_RMT_BASE + UINT32_C(0x400) + (channel * 48U * sizeof(uint32_t));
    return (volatile uint32_t *)address;
}

/** @brief Stop capture/transmit and reconnect the trigger to a low latch. */
static int stop_capture (c3pulse_state_t * p_state)
{
    if (p_state->b_clock)
    {
        rmt_ll_rx_enable(&RMT, p_state->channel, false);
        rmt_ll_tx_stop(&RMT, p_state->channel);
        rmt_ll_rx_set_mem_owner(&RMT, p_state->channel, RMT_LL_MEM_OWNER_SW);
        esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT,
                                       RMT_SIG_IN0_IDX + p_state->channel,
                                       false);
    }
    if (p_state->trigger.b_open)
    {
        if (0 != c3pin_set(&p_state->trigger, false))
        {
            return PULSEIO_IO;
        }
        esp_rom_gpio_connect_out_signal(
            p_state->trigger.pin, SIG_GPIO_OUT_IDX, false, false);
    }
    return 0;
}

/* End of file. */

